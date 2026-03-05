#include "GE_voice_predictive_model.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace genesis {

static bool ge_is_vowel_phone_upper(const std::string& p) {
    const char* vowels[] = {"AA","AE","AH","AO","AW","AY","EH","ER","EY","IH","IY","OW","OY","UH","UW"};
    for (const char* v : vowels) {
        if (p.rfind(v, 0) == 0) return true;
    }
    return false;
}

static std::string ge_upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}

static bool ge_is_voiced_consonant_upper(const std::string& p) {
    return (p=="M"||p=="N"||p=="L"||p=="R"||p=="W"||p=="Y"||p=="Z"||p=="V"||p=="D"||p=="B"||p=="G"||p=="JH");
}

std::vector<double> ge_voice_phone_features(const std::vector<PhonemeSpan>& seq, uint32_t i) {
    std::vector<double> f;
    f.reserve(8);
    f.push_back(1.0);
    if (seq.empty() || i >= seq.size()) {
        // safe default
        f.push_back(0.0);
        f.push_back(1.0);
        f.push_back(0.0);
        f.push_back(0.0);
        f.push_back(0.0);
        f.push_back(0.0);
        return f;
    }
    std::string p = ge_upper(seq[i].phone);
    const bool is_sp = (p == "SP");
    const bool is_vowel = (!is_sp) && ge_is_vowel_phone_upper(p);
    const bool is_vc = (!is_sp) && (!is_vowel) && ge_is_voiced_consonant_upper(p);
    double pos_norm = (seq.size() <= 1) ? 0.0 : ((double)i / (double)(seq.size() - 1));

    // punctuation proxy: our fallback G2P encodes pauses as SP with longer duration.
    // We'll treat "big pause" before/after as punctuation signals.
    double prev_punct = 0.0;
    double next_punct = 0.0;
    if (i > 0) {
        std::string pp = ge_upper(seq[i - 1].phone);
        if (pp == "SP" && seq[i - 1].dur_ms_u32 >= 140) prev_punct = 1.0;
    }
    if (i + 1 < seq.size()) {
        std::string np = ge_upper(seq[i + 1].phone);
        if (np == "SP" && seq[i + 1].dur_ms_u32 >= 140) next_punct = 1.0;
    }

    f.push_back(is_vowel ? 1.0 : 0.0);
    f.push_back(is_sp ? 1.0 : 0.0);
    f.push_back(is_vc ? 1.0 : 0.0);
    f.push_back(pos_norm);
    f.push_back(prev_punct);
    f.push_back(next_punct);
    return f;
}

static double ge_dot(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) s += a[i] * b[i];
    return s;
}

std::vector<VoiceProsodyControl> ge_voice_apply_model(
    const std::vector<PhonemeSpan>& phones,
    const VoicePredictiveModel& model,
    const VoiceSynthConfig& cfg)
{
    std::vector<VoiceProsodyControl> out;
    out.resize(phones.size());
    const double base_f0 = (double)cfg.f0_hz_q16_u32 / 65536.0;
    const double base_amp = (double)cfg.amp_q15_i16;

    for (uint32_t i = 0; i < (uint32_t)phones.size(); ++i) {
        VoiceProsodyControl c;
        // defaults
        c.dur_ms_u32 = phones[i].dur_ms_u32;
        c.f0_hz_q16_u32 = 0;
        c.amp_q15_i16 = 0;

        if (model.ok && model.feat_dim_u32 > 0) {
            auto feat = ge_voice_phone_features(phones, i);
            // pad/truncate
            feat.resize((size_t)model.feat_dim_u32, 0.0);

            double dur = model.base_dur_ms + ge_dot(model.w_dur, feat);
            double f0r = model.base_f0_ratio + ge_dot(model.w_f0_ratio, feat);
            double ampr = model.base_amp_ratio + ge_dot(model.w_amp_ratio, feat);

            if (dur < 20.0) dur = 20.0;
            if (dur > 600.0) dur = 600.0;
            if (f0r < 0.5) f0r = 0.5;
            if (f0r > 2.0) f0r = 2.0;
            if (ampr < 0.2) ampr = 0.2;
            if (ampr > 2.0) ampr = 2.0;

            c.dur_ms_u32 = (uint32_t)std::llround(dur);
            c.f0_hz_q16_u32 = (uint32_t)std::llround((base_f0 * f0r) * 65536.0);
            c.amp_q15_i16 = (int16_t)std::llround(base_amp * ampr);
        }
        out[i] = c;
    }
    return out;
}

static bool ge_write_text(const std::string& path, const std::string& txt) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    if (!txt.empty()) {
        if (std::fwrite(txt.data(), 1, txt.size(), f) != txt.size()) { std::fclose(f); return false; }
    }
    std::fclose(f);
    return true;
}

static bool ge_read_text(const std::string& path, std::string* out) {
    if (!out) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); return false; }
    out->resize((size_t)sz);
    if (sz > 0) {
        if (std::fread(out->data(), 1, (size_t)sz, f) != (size_t)sz) { std::fclose(f); return false; }
    }
    std::fclose(f);
    return true;
}

static void ge_append_json_vec(std::string* s, const char* key, const std::vector<double>& v) {
    *s += "\n  \""; *s += key; *s += "\": [";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) *s += ",";
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.12g", v[i]);
        *s += buf;
    }
    *s += "]";
}

bool ge_voice_model_save(const std::string& path, const VoicePredictiveModel& m) {
    std::string j;
    j += "{\n";
    j += "  \"feat_dim\": ";
    j += std::to_string(m.feat_dim_u32);
    j += ",\n  \"base_dur_ms\": ";
    j += std::to_string(m.base_dur_ms);
    j += ",\n  \"base_f0_ratio\": ";
    j += std::to_string(m.base_f0_ratio);
    j += ",\n  \"base_amp_ratio\": ";
    j += std::to_string(m.base_amp_ratio);
    j += ",";
    ge_append_json_vec(&j, "w_dur", m.w_dur);
    j += ",";
    ge_append_json_vec(&j, "w_f0_ratio", m.w_f0_ratio);
    j += ",";
    ge_append_json_vec(&j, "w_amp_ratio", m.w_amp_ratio);
    j += "\n}\n";
    return ge_write_text(path, j);
}

static bool ge_parse_number_after(const std::string& s, const std::string& key, double* out) {
    if (!out) return false;
    size_t k = s.find(key);
    if (k == std::string::npos) return false;
    size_t c = s.find(':', k);
    if (c == std::string::npos) return false;
    size_t i = c + 1;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    size_t j = i;
    while (j < s.size() && (std::isdigit((unsigned char)s[j]) || s[j] == '.' || s[j] == '-' || s[j] == '+' || s[j] == 'e' || s[j] == 'E')) j++;
    if (j <= i) return false;
    *out = std::strtod(s.substr(i, j - i).c_str(), nullptr);
    return true;
}

static bool ge_parse_u32_after(const std::string& s, const std::string& key, uint32_t* out) {
    double d = 0.0;
    if (!ge_parse_number_after(s, key, &d)) return false;
    if (d < 0.0) d = 0.0;
    if (d > 4294967295.0) d = 4294967295.0;
    *out = (uint32_t)std::llround(d);
    return true;
}

static bool ge_parse_vec_after(const std::string& s, const std::string& key, std::vector<double>* out) {
    if (!out) return false;
    size_t k = s.find(key);
    if (k == std::string::npos) return false;
    size_t b = s.find('[', k);
    size_t e = s.find(']', b);
    if (b == std::string::npos || e == std::string::npos || e <= b) return false;
    std::string body = s.substr(b + 1, e - (b + 1));
    out->clear();
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r' || body[i] == ',')) i++;
        if (i >= body.size()) break;
        size_t j = i;
        while (j < body.size() && (std::isdigit((unsigned char)body[j]) || body[j] == '.' || body[j] == '-' || body[j] == '+' || body[j] == 'e' || body[j] == 'E')) j++;
        if (j <= i) break;
        out->push_back(std::strtod(body.substr(i, j - i).c_str(), nullptr));
        i = j;
    }
    return true;
}

bool ge_voice_model_load(const std::string& path, VoicePredictiveModel* out) {
    if (!out) return false;
    std::string txt;
    if (!ge_read_text(path, &txt)) return false;

    VoicePredictiveModel m;
    if (!ge_parse_u32_after(txt, "\"feat_dim\"", &m.feat_dim_u32)) { out->info = "bad_model"; return false; }
    (void)ge_parse_number_after(txt, "\"base_dur_ms\"", &m.base_dur_ms);
    (void)ge_parse_number_after(txt, "\"base_f0_ratio\"", &m.base_f0_ratio);
    (void)ge_parse_number_after(txt, "\"base_amp_ratio\"", &m.base_amp_ratio);
    (void)ge_parse_vec_after(txt, "\"w_dur\"", &m.w_dur);
    (void)ge_parse_vec_after(txt, "\"w_f0_ratio\"", &m.w_f0_ratio);
    (void)ge_parse_vec_after(txt, "\"w_amp_ratio\"", &m.w_amp_ratio);

    // normalize sizes
    m.w_dur.resize(m.feat_dim_u32, 0.0);
    m.w_f0_ratio.resize(m.feat_dim_u32, 0.0);
    m.w_amp_ratio.resize(m.feat_dim_u32, 0.0);

    m.ok = true;
    m.info = "ok";
    *out = std::move(m);
    return true;
}

// Controlled synthesis is implemented as a thin wrapper around the existing synthesizer by
// mapping controls into an adjusted PhonemeSpan stream and config overrides.
TtsResult ge_synthesize_phones_to_wav_controlled(
    const std::vector<PhonemeSpan>& phones,
    const std::vector<VoiceProsodyControl>& controls,
    const VoiceSynthConfig& cfg)
{
    // We implement per-phone f0 by temporarily adjusting cfg for each phone.
    // This preserves the small deterministic synthesizer while enabling prosody.
    TtsResult r;
    r.ok = false;

    if (phones.empty()) { r.info = "empty"; return r; }
    if (!controls.empty() && controls.size() != phones.size()) { r.info = "controls_size_mismatch"; return r; }

    // Reuse internal implementation by duplicating minimal parts of GE_voice_synth.cpp.
    // To avoid tight coupling, we call the base synth per-phone and concatenate WAVs.
    // This is deterministic and keeps logic simple.
    WavPcm16Mono out_wav;
    out_wav.sample_rate_hz_u32 = (cfg.sample_rate_hz_u32 == 0) ? 48000u : cfg.sample_rate_hz_u32;

    for (size_t i = 0; i < phones.size(); ++i) {
        VoiceSynthConfig c = cfg;
        PhonemeSpan ph = phones[i];
        if (!controls.empty()) {
            ph.dur_ms_u32 = controls[i].dur_ms_u32;
            if (controls[i].f0_hz_q16_u32 != 0) c.f0_hz_q16_u32 = controls[i].f0_hz_q16_u32;
            if (controls[i].amp_q15_i16 != 0) c.amp_q15_i16 = controls[i].amp_q15_i16;
        }
        std::vector<PhonemeSpan> one;
        one.push_back(ph);
        TtsResult tr = ge_synthesize_phones_to_wav(one, c);
        if (!tr.ok) { r.info = tr.info; return r; }
        out_wav.samples_i16.insert(out_wav.samples_i16.end(), tr.wav.samples_i16.begin(), tr.wav.samples_i16.end());
    }

    r.ok = true;
    r.info = "ok";
    r.wav = std::move(out_wav);
    return r;
}

} // namespace genesis
