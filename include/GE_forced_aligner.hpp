#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "GE_audio_features.hpp"
#include "GE_voice_synth.hpp"

namespace genesis {

struct ForcedAlignConfig {
    uint32_t min_frames_u32 = 1;
    uint32_t max_frames_u32 = 40;
    uint32_t advance_penalty_q16_u32 = 3277; // ~0.05
    uint32_t stay_penalty_q16_u32 = 655;     // ~0.01
};

struct ForcedAlignSpan {
    std::string phone;
    uint32_t frame_start_u32 = 0;
    uint32_t frame_end_u32 = 0; // inclusive
};

bool ge_forced_align_dtw(const std::vector<AudioFrameFeatures>& frames,
                         const std::vector<PhonemeSpan>& phones,
                         const ForcedAlignConfig& cfg,
                         std::vector<ForcedAlignSpan>* out_spans);

} // namespace genesis
