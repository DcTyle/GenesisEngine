#include "GE_prosody_planner.hpp"

#include <algorithm>
#include <cctype>

#include "GE_pause_priors.hpp"

namespace genesis {

static std::string ge_upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}

static bool ge_is_vowel_phone_upper(const std::string& p) {
    const char* vowels[] = {"AA","AE","AH","AO","AW","AY","EH","ER","EY","IH","IY","OW","OY","UH","UW"};
    for (const char* v : vowels) if (p.rfind(v, 0) == 0) return true;
    return false;
}

static char ge_last_nonspace(const std::string& s) {
    for (size_t i = s.size(); i-- > 0;) {
        unsigned char c = (unsigned char)s[i];
        if (c==' '||c=='\t'||c=='\n'||c=='\r') continue;
        return (char)c;
    }
    return 0;
}

static uint32_t ge_ratio_q16(double r) {
    if (r < 0.25) r = 0.25;
    if (r > 4.0) r = 4.0;
    return (uint32_t)(r * 65536.0 + 0.5);
}

static uint32_t ge_pause_default_ms(PauseKind k) {
    switch (k) {
        case PauseKind::Comma: return 120u;
        case PauseKind::Clause: return 160u;
        case PauseKind::TerminalPeriod: return 220u;
        case PauseKind::TerminalQuestion: return 240u;
        case PauseKind::TerminalExclaim: return 200u;
        case PauseKind::Newline: return 260u;
        case PauseKind::Space: return 60u;
        default: return 60u;
    }
}

std::vector<VoiceProsodyControl> ge_voice_plan_prosody_with_pause_meta(
    const std::vector<PhonemeSpan>& phones,
    const VoiceSynthConfig& cfg,
    const std::vector<PauseKind>& pause_kinds,
    const std::vector<uint8_t>& pause_strength_u8,
    const std::string& text_utf8)
{
    (void)cfg;
    std::vector<VoiceProsodyControl> out;
    out.resize(phones.size());

    const char punct = ge_last_nonspace(text_utf8);
    const bool is_q = (punct == '?');
    const bool is_ex = (punct == '!');

    for (size_t i = 0; i < phones.size(); ++i) {
        VoiceProsodyControl c;
        c.dur_ms_u32 = phones[i].dur_ms_u32 ? phones[i].dur_ms_u32 : 80u;
        c.f0_hz_q16_u32 = 0;
        c.amp_q15_i16 = 0;

        std::string p = ge_upper(phones[i].phone);
        if (p == "SP") {
            const PauseKind pk = (i < pause_kinds.size()) ? pause_kinds[i] : PauseKind::Space;
            c.dur_ms_u32 = ge_pause_default_ms(pk);
            uint8_t strength = 1u;
            if (!pause_strength_u8.empty() && pause_strength_u8.size() == phones.size()) {
                strength = pause_strength_u8[i];
                if (strength == 0u) strength = 1u;
            }
            // Scale pause duration by run-length strength: 1->1.0, 2->1.25, 3->1.5, ... capped at 2.0.
            uint32_t scale_q16 = 65536u;
            if (strength > 1u) {
                uint32_t extra = (uint32_t)(strength - 1u) * 16384u; // 0.25 per step
                scale_q16 = 65536u + extra;
                if (scale_q16 > 131072u) scale_q16 = 131072u;
            }
            c.dur_ms_u32 = (uint32_t)(((uint64_t)c.dur_ms_u32 * (uint64_t)scale_q16) >> 16);
            out[i] = c;
            continue;
        }

        const bool is_vowel = ge_is_vowel_phone_upper(p);
        if (is_vowel) {
            c.dur_ms_u32 = (uint32_t)std::min<uint64_t>(600ull, (uint64_t)c.dur_ms_u32 + 18ull);
        }

        const double pos = (phones.size() <= 1) ? 0.0 : (double)i / (double)(phones.size() - 1);
        const double dome = 1.0 + 0.10 * (1.0 - std::abs(2.0 * pos - 1.0));
        double pitch = dome;
        if (pos > 0.75) {
            if (is_q) pitch *= 1.18;
            else if (is_ex) pitch *= 1.10;
            else pitch *= 0.92;
        }

        c.f0_hz_q16_u32 = 0x80000000u | ge_ratio_q16(pitch);

        double amp = 1.0 + 0.06 * (1.0 - std::abs(2.0 * pos - 1.0));
        uint32_t amp_q15 = (uint32_t)(amp * 32768.0 + 0.5);
        if (amp_q15 > 65535u) amp_q15 = 65535u;
        c.amp_q15_i16 = (int16_t)-(int32_t)amp_q15;

        out[i] = c;
    }

    return out;
}

std::vector<VoiceProsodyControl> ge_voice_plan_prosody(
    const std::vector<PhonemeSpan>& phones,
    const VoiceSynthConfig& cfg,
    const std::string& text_utf8)
{
    const auto pause_kinds = ge_pause_kinds_from_text_and_phones(text_utf8, phones);
    std::vector<uint8_t> pause_strength_u8(phones.size(), 0u);
    for (size_t i = 0; i < phones.size(); ++i) {
        if (phones[i].phone == "SP") pause_strength_u8[i] = 1u;
    }
    return ge_voice_plan_prosody_with_pause_meta(phones, cfg, pause_kinds, pause_strength_u8, text_utf8);
}

std::vector<VoiceProsodyControl> ge_voice_plan_prosody_with_pause_kinds(
    const std::vector<PhonemeSpan>& phones,
    const VoiceSynthConfig& cfg,
    const std::vector<PauseKind>& pause_kinds,
    const std::string& text_utf8) {
    std::vector<uint8_t> strength;
    strength.resize(phones.size(), 0u);
    for (size_t i = 0; i < phones.size(); ++i) {
        if (phones[i].phone == "SP") strength[i] = 1u;
    }
    return ge_voice_plan_prosody_with_pause_meta(phones, cfg, pause_kinds, strength, text_utf8);
}

} // namespace genesis
