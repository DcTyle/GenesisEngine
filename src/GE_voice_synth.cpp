#include "GE_voice_synth.hpp"

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

std::vector<PhonemeSpan> ge_text_to_phones_fallback_english(const std::string& text) {
    // Deterministic toy G2P: letters -> rough phones. This is a placeholder to
    // enable end-to-end audio output before full corpus-trained alignment.
    // Rules are intentionally simple and measurable.
    std::vector<PhonemeSpan> out;
    auto push = [&](const std::string& ph, uint32_t ms) {
        PhonemeSpan s; s.phone = ph; s.dur_ms_u32 = ms; out.push_back(s);
    };

    std::string t = ge_upper(text);
    for (size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        if (c >= 'A' && c <= 'Z') {
            switch (c) {
                case 'A': push("AH0", 90); break;
                case 'B': push("B", 70); break;
                case 'C': push("K", 70); break;
                case 'D': push("D", 70); break;
                case 'E': push("EH0", 90); break;
                case 'F': push("F", 70); break;
                case 'G': push("G", 70); break;
                case 'H': push("HH", 60); break;
                case 'I': push("IH0", 90); break;
                case 'J': push("JH", 80); break;
                case 'K': push("K", 70); break;
                case 'L': push("L", 70); break;
                case 'M': push("M", 70); break;
                case 'N': push("N", 70); break;
                case 'O': push("AO0", 90); break;
                case 'P': push("P", 70); break;
                case 'Q': push("K", 50); push("W", 50); break;
                case 'R': push("R", 70); break;
                case 'S': push("S", 70); break;
                case 'T': push("T", 70); break;
                case 'U': push("UH0", 90); break;
                case 'V': push("V", 70); break;
                case 'W': push("W", 70); break;
                case 'X': push("K", 50); push("S", 50); break;
                case 'Y': push("Y", 70); break;
                case 'Z': push("Z", 70); break;
                default: break;
            }
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            push("SP", 90);
        } else if (c == '.' || c == '!' || c == '?') {
            push("SP", 160);
        } else if (c == ',' || c == ';' || c == ':') {
            push("SP", 120);
        }
    }
    if (out.empty()) push("SP", 200);
    return out;
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
            // Source: saw-ish (harmonic series) at f0.
            // Filter: three sin carriers at formants. This is *not* a real vocoder;
            // it just creates vowel-ish energy in the right bands.
            for (uint32_t i = 0; i < n; ++i) {
                // Saw approximation with 6 harmonics
                double src = 0.0;
                for (int k = 1; k <= 6; ++k) src += (1.0 / (double)k) * ge_sin_2pi(phase * (double)k);
                src *= 0.35;
                // Formant mix
                double t = (double)i / sr;
                double y = src * (
                    0.55 * std::sin(6.283185307179586 * f.f1 * t) +
                    0.30 * std::sin(6.283185307179586 * f.f2 * t) +
                    0.15 * std::sin(6.283185307179586 * f.f3 * t)
                );
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
