#include "GE_voice_synth.hpp"
#include "GE_g2p_lexicon.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace genesis {

static std::string ge_upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}

static bool ge_is_vowel_phone(const std::string& p) {
    // Minimal set (ARPABET)
    const char* vowels[] = {"AA","AE","AH","AO","AW","AY","EH","ER","EY","IH","IY","OW","OY","UH","UW"};
    for (const char* v : vowels) {
        if (p.rfind(v, 0) == 0) return true;
    }
    return false;
}

// Simple deterministic oscillator helpers (double math only; deterministic given inputs).
static double ge_sin_2pi(double phase) { return std::sin(phase * 6.283185307179586); }

static void ge_append_samples(std::vector<int16_t>* out, const std::vector<double>& s, int16_t amp_q15) {
    out->reserve(out->size() + s.size());
    for (double v : s) {
        double x = v * (double)amp_q15;
        if (x > 32767.0) x = 32767.0;
        if (x < -32768.0) x = -32768.0;
        out->push_back((int16_t)std::lrint(x));
    }
}

// Very small formant table for a handful of vowels (Hz).
struct Formants { double f1, f2, f3; };
static Formants ge_formants_for_vowel(const std::string& phone) {
    // Defaults roughly for AH
    if (phone.rfind("IY",0)==0) return {270, 2290, 3010};
    if (phone.rfind("IH",0)==0) return {390, 1990, 2550};
    if (phone.rfind("EH",0)==0) return {530, 1840, 2480};
    if (phone.rfind("AE",0)==0) return {660, 1720, 2410};
    if (phone.rfind("AA",0)==0) return {730, 1090, 2440};
    if (phone.rfind("AO",0)==0) return {570, 840, 2410};
    if (phone.rfind("UH",0)==0) return {440, 1020, 2240};
    if (phone.rfind("UW",0)==0) return {300, 870, 2240};
    if (phone.rfind("ER",0)==0) return {490, 1350, 1690};
    return {600, 1200, 2500};
}

// Simple resonator (biquad bandpass-ish) for formant shaping.
struct Reson {
    double a0 = 0, a1 = 0, a2 = 0;
    double b1 = 0, b2 = 0;
    double z1 = 0, z2 = 0;
};

static Reson ge_make_reson(double sr, double f_hz, double q) {
    // RBJ cookbook bandpass constant skirt gain.
    // Deterministic double math; stable for sr>=8k.
    const double w0 = 2.0 * 3.141592653589793 * (f_hz / sr);
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);

    double b0 = alpha;
    double b1 = 0.0;
    double b2 = -alpha;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cw;
    double a2 = 1.0 - alpha;

    Reson r;
    r.a0 = b0 / a0;
    r.a1 = b1 / a0;
    r.a2 = b2 / a0;
    r.b1 = a1 / a0;
    r.b2 = a2 / a0;
    r.z1 = 0.0;
    r.z2 = 0.0;
    return r;
}

static inline double ge_reson_tick(Reson& r, double x) {
    // Direct Form I/II hybrid.
    const double y = r.a0 * x + r.z1;
    r.z1 = r.a1 * x - r.b1 * y + r.z2;
    r.z2 = r.a2 * x - r.b2 * y;
    return y;
}

TtsResult ge_synthesize_phones_to_wav(const std::vector<PhonemeSpan>& phones, const VoiceSynthConfig& cfg) {
    TtsResult r;
    r.wav.sample_rate_hz_u32 = (cfg.sample_rate_hz_u32 == 0) ? 48000u : cfg.sample_rate_hz_u32;

    const double sr = (double)r.wav.sample_rate_hz_u32;
    const double f0 = (double)cfg.f0_hz_q16_u32 / 65536.0;
    double phase = 0.0;

    std::vector<int16_t> out;

    // Deterministic noise via xorshift.
    uint32_t noise_state = 0xC0FFEEu;
    auto noise = [&]() -> double {
        noise_state ^= noise_state << 13;
        noise_state ^= noise_state >> 17;
        noise_state ^= noise_state << 5;
        // [-1,1]
        return ((double)(noise_state & 0xFFFFu) / 32768.0) - 1.0;
    };

    for (const auto& ph : phones) {
        uint32_t dur_ms = (ph.dur_ms_u32 == 0) ? 60u : ph.dur_ms_u32;
        uint32_t n = (uint32_t)std::lrint((sr * (double)dur_ms) / 1000.0);
        std::vector<double> s;
        s.resize(n, 0.0);

        std::string p = ge_upper(ph.phone);
        if (p == "SP") {
            // Silence.
        } else if (ge_is_vowel_phone(p)) {
            Formants f = ge_formants_for_vowel(p);
            // Source: harmonic glottal-like wave at f0.
            // Filter: three resonators at vowel formants. Still lightweight, but materially clearer.
            Reson r1 = ge_make_reson(sr, f.f1, 6.0);
            Reson r2 = ge_make_reson(sr, f.f2, 8.0);
            Reson r3 = ge_make_reson(sr, f.f3, 10.0);
            for (uint32_t i = 0; i < n; ++i) {
                // Band-limited-ish harmonic sum (12 harmonics)
                double src = 0.0;
                for (int k = 1; k <= 12; ++k) src += (1.0 / (double)k) * ge_sin_2pi(phase * (double)k);
                src *= 0.25;

                // Spectral tilt to reduce buzz.
                const double tilt = 0.85;
                src = src * (1.0 - 0.15 * phase) * tilt;

                // Formant filtering.
                double y = 0.0;
                y += 0.65 * ge_reson_tick(r1, src);
                y += 0.35 * ge_reson_tick(r2, src);
                y += 0.20 * ge_reson_tick(r3, src);
                // simple attack/decay
                double a = std::min(1.0, (double)i / (0.02 * sr));
                double d = std::min(1.0, (double)(n - 1 - i) / (0.03 * sr));
                s[i] = y * a * d;
                phase += f0 / sr;
                if (phase >= 1.0) phase -= std::floor(phase);
            }
        } else {
            // Consonants: noise burst + faint voicing if sonorant.
            bool voiced = (p=="M"||p=="N"||p=="L"||p=="R"||p=="W"||p=="Y"||p=="Z"||p=="V"||p=="D"||p=="B"||p=="G"||p=="JH");
            for (uint32_t i = 0; i < n; ++i) {
                double t = (double)i / sr;
                double nb = noise() * 0.45;
                double v = voiced ? (0.25 * ge_sin_2pi(phase)) : 0.0;
                double env = std::exp(-6.0 * t); // bursty
                s[i] = (nb + v) * env;
                phase += f0 / sr;
                if (phase >= 1.0) phase -= std::floor(phase);
            }
        }

        ge_append_samples(&out, s, cfg.amp_q15_i16);
    }

    r.wav.samples_i16 = std::move(out);
    r.ok = true;
    r.info = "ok";
    return r;
}

} // namespace genesis
