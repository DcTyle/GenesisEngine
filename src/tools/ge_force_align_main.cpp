#include <cstdio>
#include <string>

#include "GE_audio_wav.hpp"
#include "GE_audio_features.hpp"
#include "GE_g2p_lexicon.hpp"
#include "GE_forced_aligner.hpp"

using namespace genesis;

static void usage() {
    std::printf("ge_force_align usage:\n");
    std::printf("  ge_force_align --wav file.wav --text \"hello world\" --out align.tsv\n");
    std::printf("Outputs: phone\tframe_start\tframe_end\n");
}

int main(int argc, char** argv) {
    std::string wav_path;
    std::string text;
    std::string out_path = "align.tsv";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--wav" && i + 1 < argc) wav_path = argv[++i];
        else if (a == "--text" && i + 1 < argc) text = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (a == "--help") { usage(); return 0; }
    }

    if (wav_path.empty() || text.empty()) { usage(); return 2; }

    WavPcm16Mono wav;
    if (!ge_wav_read_pcm16_mono(wav_path, &wav)) {
        std::printf("wav_read_failed path=%s\n", wav_path.c_str());
        return 3;
    }

    AudioFeatureConfig fcfg;
    fcfg.frame_samples_u32 = 1024;
    fcfg.hop_samples_u32 = 256;
    auto feats = ge_extract_audio_features(wav, fcfg);
    if (feats.empty()) {
        std::printf("feature_extract_failed\n");
        return 4;
    }

    auto phones = ge_text_to_phones_english(text);
    if (phones.empty()) {
        std::printf("g2p_failed\n");
        return 5;
    }

    std::vector<std::string> p;
    p.reserve(phones.size());
    for (const auto& ph : phones) p.push_back(ph.phone);

    ForcedAlignConfig acfg;
    acfg.feat_cfg = fcfg;
    auto ar = ge_forced_align_dtw(feats, p, acfg);
    if (!ar.ok) {
        std::printf("align_failed: %s\n", ar.info.c_str());
        return 6;
    }

    std::FILE* f = std::fopen(out_path.c_str(), "wb");
    if (!f) {
        std::printf("open_failed out=%s\n", out_path.c_str());
        return 7;
    }
    for (const auto& sp : ar.spans) {
        std::fprintf(f, "%s\t%u\t%u\n", sp.phone.c_str(), sp.frame_start_u32, sp.frame_end_u32);
    }
    std::fclose(f);

    std::printf("align_ok phones=%u frames=%u out=%s\n", (unsigned)ar.spans.size(), (unsigned)feats.size(), out_path.c_str());
    return 0;
}
