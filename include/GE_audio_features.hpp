#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "GE_audio_wav.hpp"

namespace genesis {

// Deterministic audio feature extraction (CPU reference).
// Used for measurable checkpoints and cross-modal alignment (audio <-> text).
struct AudioFrameFeatures {
    // Root mean square energy (Q16.16)
    uint32_t rms_q16_u32 = 0;
    // Zero-crossing rate per frame (Q16.16, crossings / sample)
    uint32_t zcr_q16_u32 = 0;
    // Spectral centroid in Hz (Q16.16)
    uint32_t centroid_hz_q16_u32 = 0;

    // Fundamental frequency estimate in Hz (Q16.16). 0 if unvoiced/unknown.
    uint32_t f0_hz_q16_u32 = 0;
    // Voiced confidence (Q16.16). 0..1 in Q16.
    uint32_t voiced_q16_u32 = 0;
};

struct AudioFeatureConfig {
    uint32_t frame_samples_u32 = 1024;
    uint32_t hop_samples_u32 = 256;
};

std::vector<AudioFrameFeatures> ge_extract_audio_features(const WavPcm16Mono& wav, const AudioFeatureConfig& cfg);

} // namespace genesis
