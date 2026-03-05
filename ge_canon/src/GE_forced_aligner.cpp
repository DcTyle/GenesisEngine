#include "GE_forced_aligner.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace genesis {

static std::string ge_upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}

enum class PhoneClass : uint32_t {
    Pause = 0,
    Boundary = 1,
    Vowel = 2,
    Sonorant = 3,
    Fricative = 4,
    Stop = 5,
    Other = 6
};

static bool ge_is_pause(const std::string& p) { return p == "SP"; }
static bool ge_is_boundary(const std::string& p) { return p == "WB"; }

static bool ge_is_vowel(const std::string& p) {
    const char* vowels[] = {"AA","AE","AH","AO","AW","AY","EH","ER","EY","IH","IY","OW","OY","UH","UW"};
    for (const char* v : vowels) if (p.rfind(v, 0) == 0) return true;
    return false;
}

static bool ge_is_sonorant(const std::string& p) {
    return (p=="M"||p=="N"||p=="NG"||p=="L"||p=="R"||p=="W"||p=="Y");
}

static bool ge_is_fricative(const std::string& p) {
    return (p=="S"||p=="Z"||p=="SH"||p=="ZH"||p=="F"||p=="V"||p=="TH"||p=="DH"||p=="HH");
}

static bool ge_is_stop(const std::string& p) {
    return (p=="P"||p=="B"||p=="T"||p=="D"||p=="K"||p=="G"||p=="CH"||p=="JH");
}

static PhoneClass ge_phone_class(const std::string& p) {
    if (ge_is_pause(p)) return PhoneClass::Pause;
    if (ge_is_boundary(p)) return PhoneClass::Boundary;
    if (ge_is_vowel(p)) return PhoneClass::Vowel;
    if (ge_is_sonorant(p)) return PhoneClass::Sonorant;
    if (ge_is_fricative(p)) return PhoneClass::Fricative;
    if (ge_is_stop(p)) return PhoneClass::Stop;
    return PhoneClass::Other;
}

static inline double q16_to_f64(uint32_t q16) { return (double)q16 / 65536.0; }

static double ge_frame_log_energy(const AudioFrameFeatures& fr) {
    const double rms = std::max(1e-6, q16_to_f64(fr.rms_q16_u32));
    return std::log(rms);
}

static double ge_frame_centroid_hz(const AudioFrameFeatures& fr) {
    return q16_to_f64(fr.centroid_hz_q16_u32);
}

static double ge_frame_voiced(const AudioFrameFeatures& fr) {
    return std::min(1.0, std::max(0.0, q16_to_f64(fr.voiced_q16_u32)));
}

static double ge_phone_frame_cost(PhoneClass pc, const AudioFrameFeatures& fr) {
    const double le = ge_frame_log_energy(fr);
    const double c = ge_frame_centroid_hz(fr);
    const double v = ge_frame_voiced(fr);
    const double le_speech = -2.8;
    auto sq = [](double x){ return x*x; };

    switch (pc) {
        case PhoneClass::Pause: {
            return sq(std::max(0.0, le - (-4.3))) + 2.0 * v;
        }
        case PhoneClass::Boundary: {
            return 0.0;
        }
        case PhoneClass::Vowel: {
            const double ct = 1100.0;
            return 0.8 * sq(le - le_speech) + 1.6 * sq((c - ct) / 1200.0) + 2.5 * sq(1.0 - v);
        }
        case PhoneClass::Sonorant: {
            const double ct = 1600.0;
            return 1.0 * sq(le - le_speech) + 1.2 * sq((c - ct) / 1500.0) + 2.0 * sq(1.0 - v);
        }
        case PhoneClass::Fricative: {
            const double ct = 3200.0;
            return 1.2 * sq(le - le_speech) + 1.8 * sq((c - ct) / 2000.0) + 0.6 * sq(v - 0.3);
        }
        case PhoneClass::Stop: {
            const double ct = 2500.0;
            return 1.5 * sq(le - le_speech) + 1.3 * sq((c - ct) / 2200.0) + 0.5 * sq(v - 0.4);
        }
        default: {
            return 1.3 * sq(le - le_speech) + 0.9 * sq((c - 2000.0) / 2500.0);
        }
    }
}

static void ge_min_max_frames(PhoneClass pc, uint32_t* min_f, uint32_t* max_f) {
    switch (pc) {
        case PhoneClass::Pause: *min_f = 4; *max_f = 90; return;
        case PhoneClass::Boundary: *min_f = 0; *max_f = 0; return;
        case PhoneClass::Vowel: *min_f = 6; *max_f = 75; return;
        case PhoneClass::Sonorant: *min_f = 4; *max_f = 55; return;
        case PhoneClass::Fricative: *min_f = 3; *max_f = 45; return;
        case PhoneClass::Stop: *min_f = 2; *max_f = 18; return;
        default: *min_f = 3; *max_f = 45; return;
    }
}

ForcedAlignResult ge_forced_align_phones_to_audio_frames(
    const std::vector<PhonemeSpan>& phones,
    const std::vector<AudioFrameFeatures>& frames)
{
    ForcedAlignResult r;
    r.ok = false;
    if (phones.empty()) { r.info = "no_phones"; return r; }
    if (frames.empty()) { r.info = "no_frames"; return r; }

    std::vector<uint32_t> consume;
    consume.reserve(phones.size());
    for (uint32_t pi = 0; pi < (uint32_t)phones.size(); ++pi) {
        std::string p = ge_upper(phones[pi].phone);
        if (ge_is_boundary(p)) continue;
        consume.push_back(pi);
    }
    if (consume.empty()) { r.info = "no_consuming_phones"; return r; }

    const uint32_t P = (uint32_t)consume.size();
    const uint32_t T = (uint32_t)frames.size();
    const double INF = 1e30;

    std::vector<double> dp((size_t)(P + 1) * (size_t)(T + 1), INF);
    std::vector<int32_t> back_len((size_t)(P + 1) * (size_t)(T + 1), -1);
    dp[0 * (T + 1) + 0] = 0.0;

    for (uint32_t pidx = 1; pidx <= P; ++pidx) {
        const uint32_t pi = consume[pidx - 1];
        const std::string ph = ge_upper(phones[pi].phone);
        const PhoneClass pc = ge_phone_class(ph);
        uint32_t min_f = 0, max_f = 0;
        ge_min_max_frames(pc, &min_f, &max_f);

        std::vector<double> cst(T, 0.0);
        for (uint32_t t = 0; t < T; ++t) cst[t] = ge_phone_frame_cost(pc, frames[t]);
        std::vector<double> pref(T + 1, 0.0);
        for (uint32_t t = 0; t < T; ++t) pref[t + 1] = pref[t] + cst[t];

        for (uint32_t t1 = 0; t1 <= T; ++t1) {
            double best = INF;
            int32_t best_len = -1;
            const uint32_t len_min = min_f;
            const uint32_t len_max = std::min(max_f, t1);
            for (uint32_t len = len_min; len <= len_max; ++len) {
                uint32_t t0 = t1 - len;
                double prev = dp[(size_t)(pidx - 1) * (T + 1) + t0];
                if (prev >= INF * 0.5) continue;
                double seg = pref[t1] - pref[t0];
                double mid = 0.5 * (double)(len_min + len_max);
                double lp = 0.0025 * (double)(len - (uint32_t)mid) * (double)(len - (uint32_t)mid);
                double cost = prev + seg + lp;
                if (cost < best) { best = cost; best_len = (int32_t)len; }
            }
            dp[(size_t)pidx * (T + 1) + t1] = best;
            back_len[(size_t)pidx * (T + 1) + t1] = best_len;
        }
    }

    double best_end = INF;
    uint32_t best_t = T;
    for (uint32_t t = 0; t <= T; ++t) {
        double v = dp[(size_t)P * (T + 1) + t];
        if (v >= INF * 0.5) continue;
        double tail = 0.01 * (double)(T - t);
        if (v + tail < best_end) { best_end = v + tail; best_t = t; }
    }
    if (best_end >= INF * 0.5) { r.info = "dp_failed"; return r; }

    std::vector<PhoneFrameSpan> spans_consuming;
    spans_consuming.resize(P);
    uint32_t t1 = best_t;
    for (int32_t pidx = (int32_t)P; pidx >= 1; --pidx) {
        int32_t len = back_len[(size_t)pidx * (T + 1) + t1];
        if (len < 0) { r.info = "backtrack_failed"; return r; }
        uint32_t t0 = t1 - (uint32_t)len;
        spans_consuming[(size_t)pidx - 1] = PhoneFrameSpan{consume[(size_t)pidx - 1], t0, t1};
        t1 = t0;
    }

    r.spans.clear();
    r.spans.reserve(phones.size());
    std::vector<PhoneFrameSpan> span_by_phone(phones.size());
    std::vector<uint8_t> has_span(phones.size(), 0);
    for (const auto& s : spans_consuming) {
        span_by_phone[s.phone_index_u32] = s;
        has_span[s.phone_index_u32] = 1;
    }
    for (uint32_t pi = 0; pi < (uint32_t)phones.size(); ++pi) {
        std::string p = ge_upper(phones[pi].phone);
        if (ge_is_boundary(p)) {
            r.spans.push_back(PhoneFrameSpan{pi, 0, 0});
        } else if (has_span[pi]) {
            r.spans.push_back(span_by_phone[pi]);
        } else {
            r.spans.push_back(PhoneFrameSpan{pi, 0, 0});
        }
    }

    r.ok = true;
    r.info = "ok";
    r.total_cost = best_end;
    return r;
}

std::vector<uint32_t> ge_alignment_phone_durations_ms(
    const std::vector<PhonemeSpan>& phones,
    const std::vector<AudioFrameFeatures>& frames,
    uint32_t hop_samples_u32,
    uint32_t sample_rate_hz_u32,
    const ForcedAlignResult& ar)
{
    std::vector<uint32_t> d;
    d.resize(phones.size(), 0);
    if (!ar.ok || ar.spans.size() != phones.size() || frames.empty()) return d;
    if (sample_rate_hz_u32 == 0) return d;
    if (hop_samples_u32 == 0) hop_samples_u32 = 256;
    const double ms_per_frame = 1000.0 * (double)hop_samples_u32 / (double)sample_rate_hz_u32;

    for (const auto& sp : ar.spans) {
        if (sp.phone_index_u32 >= phones.size()) continue;
        std::string p = ge_upper(phones[sp.phone_index_u32].phone);
        if (ge_is_boundary(p)) { d[sp.phone_index_u32] = 0; continue; }
        uint32_t nf = (sp.frame_end_u32 > sp.frame_begin_u32) ? (sp.frame_end_u32 - sp.frame_begin_u32) : 0;
        uint32_t ms = (uint32_t)std::llround((double)nf * ms_per_frame);
        d[sp.phone_index_u32] = std::max<uint32_t>(ms, 15u);
    }
    return d;
}

} // namespace genesis
