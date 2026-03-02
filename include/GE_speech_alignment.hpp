#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace genesis {

// Speech corpus record for aligned (audio,text) training.
// Uses explicit file paths provided by the user (no network IO).
struct SpeechAlignedSample {
    std::string wav_path;
    std::string text_utf8;
    // Optional: speaker id / client_id field (Common Voice).
    std::string speaker_id;
    // Optional: locale tag
    std::string locale;
};

struct SpeechCorpusLoadResult {
    bool ok = false;
    std::string info;
    uint32_t samples_u32 = 0;
    std::vector<SpeechAlignedSample> samples;
};

// Load a Common Voice style TSV (validated deterministically) and map to WAV paths.
// Expects columns: path / sentence (and optionally client_id / locale).
SpeechCorpusLoadResult ge_load_common_voice_tsv(
    const std::string& tsv_path,
    const std::string& clips_dir_prefix,
    uint32_t max_samples_u32);

// Load a simple two-column manifest: "<wav_path>\t<text>" per line.
SpeechCorpusLoadResult ge_load_wav_text_manifest(
    const std::string& manifest_path,
    uint32_t max_samples_u32);

} // namespace genesis
