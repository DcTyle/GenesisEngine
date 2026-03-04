#include <cstdio>
#include <string>

#include "GE_voice_synth.hpp"
#include "GE_voice_predictive_model.hpp"
#include "GE_prosody_planner.hpp"
#include "GE_prosody_priors.hpp"
#include "GE_phrase_priors.hpp"
#include "GE_pause_priors.hpp"
#include "GE_audio_wav.hpp"
#include "GE_g2p_lexicon.hpp"

using namespace genesis;

static void usage() {
    std::printf("ge_tts_speak usage:\n");
    std::printf("  ge_tts_speak --text \"hello world\" --out out.wav [--sr 48000] [--f0 130] [--model model.json] [--priors model.json.priors.ewcfg] [--priors_blend 0.5] [--phrase_priors model.json.phrase_priors.ewcfg] [--phrase_blend 0.35] [--pause_priors model.json.pause_priors.ewcfg] [--pause_blend 0.35]\n");
}

int main(int argc, char** argv) {
    std::string text;
    std::string out_path = "out.wav";
    std::string model_path;
    std::string priors_path;
    std::string phrase_priors_path;
    std::string pause_priors_path;
    uint32_t sr = 48000;
    uint32_t f0 = 130;
    uint32_t priors_blend_q16 = 0x00008000u; // 0.5
    uint32_t phrase_blend_q16 = (uint32_t)(0.35 * 65536.0 + 0.5);
    uint32_t pause_blend_q16 = (uint32_t)(0.35 * 65536.0 + 0.5);

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--text" && i + 1 < argc) text = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (a == "--sr" && i + 1 < argc) sr = (uint32_t)std::stoul(argv[++i]);
        else if (a == "--f0" && i + 1 < argc) f0 = (uint32_t)std::stoul(argv[++i]);
        else if (a == "--model" && i + 1 < argc) model_path = argv[++i];
        else if (a == "--priors" && i + 1 < argc) priors_path = argv[++i];
        else if (a == "--priors_blend" && i + 1 < argc) {
            double v = std::stod(argv[++i]);
            if (v < 0.0) v = 0.0;
            if (v > 1.0) v = 1.0;
            priors_blend_q16 = (uint32_t)(v * 65536.0 + 0.5);
        }
        else if (a == "--phrase_priors" && i + 1 < argc) phrase_priors_path = argv[++i];
        else if (a == "--phrase_blend" && i + 1 < argc) {
            double v = std::stod(argv[++i]);
            if (v < 0.0) v = 0.0;
            if (v > 1.0) v = 1.0;
            phrase_blend_q16 = (uint32_t)(v * 65536.0 + 0.5);
        }
        else if (a == "--pause_priors" && i + 1 < argc) pause_priors_path = argv[++i];
        else if (a == "--pause_blend" && i + 1 < argc) {
            double v = std::stod(argv[++i]);
            if (v < 0.0) v = 0.0;
            if (v > 1.0) v = 1.0;
            pause_blend_q16 = (uint32_t)(v * 65536.0 + 0.5);
        }
        else if (a == "--help") { usage(); return 0; }
    }
    if (text.empty()) { usage(); return 2; }

    VoiceSynthConfig cfg;
    cfg.sample_rate_hz_u32 = sr;
    cfg.f0_hz_q16_u32 = f0 << 16;

    std::vector<PhonemeSpan> phones;
    std::vector<PauseKind> pause_kinds;
    std::vector<uint8_t> pause_strength_u8;
    if (!ge_text_to_phones_english_with_pause_meta(text, &phones, &pause_kinds)) {
        std::printf("g2p_failed
");
        return 6;
    }

    // Optional priors (auditable per-phone means). If not specified, try model.priors.ewcfg.
    ProsodyPriors pri;
    bool have_pri = false;
    if (!priors_path.empty()) {
        have_pri = ge_prosody_priors_read_ewcfg(priors_path, &pri);
    } else if (!model_path.empty()) {
        have_pri = ge_prosody_priors_read_ewcfg(model_path + ".priors.ewcfg", &pri);
    }

    // Optional phrase priors.
    PhrasePriors ppri;
    bool have_ppri = false;
    if (!phrase_priors_path.empty()) {
        have_ppri = ge_phrase_priors_read_ewcfg(phrase_priors_path, &ppri);
    } else if (!model_path.empty()) {
        have_ppri = ge_phrase_priors_read_ewcfg(model_path + ".phrase_priors.ewcfg", &ppri);
    }

    // Optional pause priors.
    PausePriors pz;
    bool have_pz = false;
    if (!pause_priors_path.empty()) {
        have_pz = ge_pause_priors_read_ewcfg(pause_priors_path, &pz);
    } else if (!model_path.empty()) {
        have_pz = ge_pause_priors_read_ewcfg(model_path + ".pause_priors.ewcfg", &pz);
    }

    const PhraseMode mode = ge_phrase_mode_from_text(text);

    TtsResult res;
    if (!model_path.empty()) {
        VoicePredictiveModel m;
        if (!ge_voice_model_load(model_path, &m) || !m.ok) {
            std::printf("model_load_failed: %s\n", model_path.c_str());
            return 5;
        }
        auto controls = ge_voice_apply_model(phones, m, cfg);
        if (have_pri) {
            std::vector<std::string> ps;
            ps.reserve(phones.size());
            for (const auto& ph : phones) ps.push_back(ph.phone);
            ge_prosody_apply_priors(pri, ps, priors_blend_q16, &controls);
        }
        if (have_ppri) {
            std::vector<std::string> ps;
            ps.reserve(phones.size());
            for (const auto& ph : phones) ps.push_back(ph.phone);
            ge_phrase_apply_priors(ppri, mode, ps, phrase_blend_q16, &controls);
        }
        if (have_pz) {
            std::vector<std::string> ps;
            ps.reserve(phones.size());
            for (const auto& ph : phones) ps.push_back(ph.phone);
            ge_pause_apply_priors(pz, pause_kinds, ps, pause_blend_q16, &controls);
        }
        res = ge_synthesize_phones_to_wav_controlled(phones, controls, cfg);
    } else {
        // Deterministic prosody planning (always available, no external models).
        auto controls = ge_voice_plan_prosody_with_pause_meta(phones, cfg, pause_kinds, pause_strength_u8, text);
        if (have_pri) {
            std::vector<std::string> ps;
            ps.reserve(phones.size());
            for (const auto& ph : phones) ps.push_back(ph.phone);
            ge_prosody_apply_priors(pri, ps, priors_blend_q16, &controls);
        }
        if (have_ppri) {
            std::vector<std::string> ps;
            ps.reserve(phones.size());
            for (const auto& ph : phones) ps.push_back(ph.phone);
            ge_phrase_apply_priors(ppri, mode, ps, phrase_blend_q16, &controls);
        }
        if (have_pz) {
            std::vector<std::string> ps;
            ps.reserve(phones.size());
            for (const auto& ph : phones) ps.push_back(ph.phone);
            ge_pause_apply_priors(pz, pause_kinds, ps, pause_blend_q16, &controls);
        }
        res = ge_synthesize_phones_to_wav_controlled(phones, controls, cfg);
    }
    if (!res.ok) {
        std::printf("tts_failed: %s\n", res.info.c_str());
        return 3;
    }
    if (!ge_wav_write_pcm16_mono(out_path, res.wav)) {
        std::printf("write_failed\n");
        return 4;
    }
    std::printf("tts_ok out=%s samples=%llu sr=%u\n", out_path.c_str(),
        (unsigned long long)res.wav.samples_i16.size(), res.wav.sample_rate_hz_u32);
    return 0;
}
