#include "GE_forced_aligner.hpp"

#include <algorithm>
#include <limits>

namespace genesis {

static bool is_pause_phone(const std::string& p) {
    return p == "SP" || p == "sp";
}

static bool is_vowelish_phone(const std::string& p) {
    if (p.size() < 2) return false;
    const char a = p[0];
    const char b = p[1];
    if ((a=='A' && (b=='A'||b=='E'||b=='H'||b=='O'||b=='W'||b=='Y')) ||
        (a=='E' && (b=='H'||b=='R'||b=='Y')) ||
        (a=='I' && (b=='H'||b=='Y')) ||
        (a=='O' && (b=='W'||b=='Y')) ||
        (a=='U' && (b=='H'||b=='W')) ) {
        return true;
    }
    return false;
}

static inline uint32_t q16_abs_diff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static uint32_t frame_phone_cost_q16(const AudioFrameFeatures& fr, const std::string& phone) {
    const uint32_t voiced = fr.voiced_q16_u32;
    const uint32_t rms = fr.rms_q16_u32;
    const uint32_t f0 = fr.f0_hz_q16_u32;

    if (is_pause_phone(phone)) {
        uint32_t c = 0;
        c += (rms > 65536u) ? 65536u : rms;
        c += (voiced > 65536u) ? 65536u : voiced;
        return c;
    }
    if (is_vowelish_phone(phone)) {
        uint32_t c = q16_abs_diff(voiced, 65536u);
        if (f0 < (30u << 16)) c += (30u << 16) - f0;
        if (rms < 2000u) c += 2000u - rms;
        return c;
    }
    uint32_t c = (voiced > 20000u) ? (voiced - 20000u) : (20000u - voiced);
    if (rms < 800u) c += 800u - rms;
    return c;
}

bool ge_forced_align_dtw(const std::vector<AudioFrameFeatures>& frames,
                         const std::vector<PhonemeSpan>& phones,
                         const ForcedAlignConfig& cfg,
                         std::vector<ForcedAlignSpan>* out_spans) {
    if (!out_spans) return false;
    out_spans->clear();
    if (frames.empty() || phones.empty()) return false;

    const uint32_t T = (uint32_t)frames.size();
    const uint32_t P = (uint32_t)phones.size();
    const uint64_t INF = std::numeric_limits<uint64_t>::max() / 4;

    std::vector<uint64_t> dp((size_t)T * P, INF);
    std::vector<uint8_t> bt((size_t)T * P, 0);
    std::vector<uint16_t> spanlen((size_t)T * P, 0);
    auto idx = [&](uint32_t t, uint32_t p) -> size_t { return (size_t)t * P + p; };

    dp[idx(0,0)] = frame_phone_cost_q16(frames[0], phones[0].phone);
    spanlen[idx(0,0)] = 1;

    for (uint32_t t = 1; t < T; ++t) {
        for (uint32_t p = 0; p < P; ++p) {
            const uint32_t c = frame_phone_cost_q16(frames[t], phones[p].phone);

            // stay
            if (dp[idx(t-1,p)] != INF) {
                uint16_t sl = spanlen[idx(t-1,p)];
                if (sl < cfg.max_frames_u32) {
                    uint64_t v = dp[idx(t-1,p)] + c + cfg.stay_penalty_q16_u32;
                    dp[idx(t,p)] = v;
                    bt[idx(t,p)] = 0;
                    spanlen[idx(t,p)] = (uint16_t)(sl + 1);
                }
            }

            // advance
            if (p > 0 && dp[idx(t-1,p-1)] != INF) {
                uint16_t prev_sl = spanlen[idx(t-1,p-1)];
                if (prev_sl >= cfg.min_frames_u32) {
                    uint64_t v = dp[idx(t-1,p-1)] + c + cfg.advance_penalty_q16_u32;
                    if (v <= dp[idx(t,p)]) {
                        dp[idx(t,p)] = v;
                        bt[idx(t,p)] = 1;
                        spanlen[idx(t,p)] = 1;
                    }
                }
            }
        }
    }

    uint32_t t = T - 1;
    uint32_t p = P - 1;
    if (dp[idx(t,p)] == INF) return false;

    std::vector<uint32_t> assign(T, 0);
    while (true) {
        assign[t] = p;
        if (t == 0) break;
        const uint8_t b = bt[idx(t,p)];
        if (b == 1 && p > 0) p -= 1;
        t -= 1;
    }

    // Convert to spans.
    out_spans->reserve(P);
    uint32_t cur_p = assign[0];
    uint32_t start = 0;
    for (uint32_t i = 1; i < T; ++i) {
        if (assign[i] != cur_p) {
            ForcedAlignSpan s;
            s.phone = phones[cur_p].phone;
            s.frame_start_u32 = start;
            s.frame_end_u32 = i - 1;
            out_spans->push_back(s);
            start = i;
            cur_p = assign[i];
        }
    }
    ForcedAlignSpan s;
    s.phone = phones[cur_p].phone;
    s.frame_start_u32 = start;
    s.frame_end_u32 = T - 1;
    out_spans->push_back(s);

    // Ensure one span per phone.
    if (out_spans->size() != P) {
        std::vector<uint32_t> first(P, T), last(P, 0);
        std::vector<uint8_t> seen(P, 0);
        for (uint32_t i = 0; i < T; ++i) {
            uint32_t pi = assign[i];
            if (pi >= P) continue;
            if (!seen[pi]) { first[pi] = i; last[pi] = i; seen[pi] = 1; }
            else { if (i < first[pi]) first[pi] = i; if (i > last[pi]) last[pi] = i; }
        }
        std::vector<ForcedAlignSpan> fixed(P);
        for (uint32_t i = 0; i < P; ++i) {
            fixed[i].phone = phones[i].phone;
            if (seen[i]) {
                fixed[i].frame_start_u32 = first[i];
                fixed[i].frame_end_u32 = last[i];
            } else {
                uint32_t borrow = 0;
                if (i > 0 && seen[i-1]) borrow = last[i-1];
                else if (i + 1 < P && seen[i+1]) borrow = first[i+1];
                fixed[i].frame_start_u32 = borrow;
                fixed[i].frame_end_u32 = borrow;
            }
        }
        uint32_t prev_end = 0;
        for (uint32_t i = 0; i < P; ++i) {
            uint32_t a = fixed[i].frame_start_u32;
            uint32_t b = fixed[i].frame_end_u32;
            if (i == 0) {
                if (a > b) a = b;
                prev_end = b;
            } else {
                if (a <= prev_end) a = prev_end;
                if (b < a) b = a;
                prev_end = b;
            }
            fixed[i].frame_start_u32 = a;
            fixed[i].frame_end_u32 = std::min(b, T - 1);
        }
        *out_spans = fixed;
    }
    return true;
}

} // namespace genesis
