#include <cstdio>
#include <string>

#include "GE_voice_synth.hpp"
#include "GE_voice_predictive_model.hpp"
#include "GE_audio_wav.hpp"

using namespace genesis;

static void usage() {
    std::printf("ge_tts_speak usage:\n");
    std::printf("  ge_tts_speak --text \"hello world\" --out out.wav [--sr 48000] [--f0 130] [--model model.json]\n");
}

int main(int argc, char** argv) {
    std::string text;
    std::string out_path = "out.wav";
    std::string model_path;
    uint32_t sr = 48000;
    uint32_t f0 = 130;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--text" && i + 1 < argc) text = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (a == "--sr" && i + 1 < argc) sr = (uint32_t)std::stoul(argv[++i]);
        else if (a == "--f0" && i + 1 < argc) f0 = (uint32_t)std::stoul(argv[++i]);
        else if (a == "--model" && i + 1 < argc) model_path = argv[++i];
        else if (a == "--help") { usage(); return 0; }
    }
    if (text.empty()) { usage(); return 2; }

    VoiceSynthConfig cfg;
    cfg.sample_rate_hz_u32 = sr;
    cfg.f0_hz_q16_u32 = f0 << 16;

    auto phones = ge_text_to_phones_english(text);

    TtsResult res;
    if (!model_path.empty()) {
        VoicePredictiveModel m;
        if (!ge_voice_model_load(model_path, &m) || !m.ok) {
            std::printf("model_load_failed: %s\n", model_path.c_str());
            return 5;
        }
        auto controls = ge_voice_apply_model(phones, m, cfg);
        res = ge_synthesize_phones_to_wav_controlled(phones, controls, cfg);
    } else {
        res = ge_synthesize_phones_to_wav(phones, cfg);
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
