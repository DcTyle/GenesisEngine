#include <cstdio>
#include <string>

#include "GE_speech_alignment.hpp"
#include "GE_audio_wav.hpp"
#include "GE_audio_features.hpp"

using namespace genesis;

static void usage() {
    std::printf("ge_speech_ingest usage:\n");
    std::printf("  ge_speech_ingest --commonvoice_tsv <path.tsv> --clips_dir <clips_dir> [--max N]\n");
    std::printf("  ge_speech_ingest --manifest <wav_text.tsv> [--max N]\n");
    std::printf("Outputs basic measurable stats and validates WAV readability.\n");
}

int main(int argc, char** argv) {
    std::string tsv;
    std::string clips;
    std::string manifest;
    uint32_t max_n = 2000;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--commonvoice_tsv" && i + 1 < argc) tsv = argv[++i];
        else if (a == "--clips_dir" && i + 1 < argc) clips = argv[++i];
        else if (a == "--manifest" && i + 1 < argc) manifest = argv[++i];
        else if (a == "--max" && i + 1 < argc) max_n = (uint32_t)std::stoul(argv[++i]);
        else if (a == "--help") { usage(); return 0; }
    }

    SpeechCorpusLoadResult lr;
    if (!manifest.empty()) {
        lr = ge_load_wav_text_manifest(manifest, max_n);
    } else if (!tsv.empty()) {
        lr = ge_load_common_voice_tsv(tsv, clips, max_n);
    } else {
        usage();
        return 2;
    }

    if (!lr.ok) {
        std::printf("load_failed: %s\n", lr.info.c_str());
        return 3;
    }

    uint32_t ok_wavs = 0;
    uint64_t total_samples = 0;
    uint32_t sr_seen = 0;

    AudioFeatureConfig cfg;
    for (const auto& s : lr.samples) {
        WavPcm16Mono wav;
        if (!ge_wav_read_pcm16_mono(s.wav_path, &wav)) continue;
        ok_wavs++;
        total_samples += (uint64_t)wav.samples_i16.size();
        sr_seen = wav.sample_rate_hz_u32;

        // Exercise feature extraction deterministically.
        (void)ge_extract_audio_features(wav, cfg);
    }

    std::printf("speech_ingest ok_samples=%u wav_readable=%u sample_rate=%u total_pcm_samples=%llu\n",
        lr.samples_u32, ok_wavs, sr_seen, (unsigned long long)total_samples);
    return 0;
}
