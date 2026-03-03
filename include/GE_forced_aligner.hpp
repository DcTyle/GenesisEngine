#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "GE_audio_features.hpp"
#include "GE_voice_synth.hpp"

namespace genesis {

struct PhoneFrameSpan {
    uint32_t phone_index_u32 = 0;
    uint32_t frame_begin_u32 = 0; // inclusive
    uint32_t frame_end_u32 = 0;   // exclusive
};

struct ForcedAlignResult {
    bool ok = false;
    std::string info;
    std::vector<PhoneFrameSpan> spans; // size==phones.size(); WB phones have empty spans
    double total_cost = 0.0;
};

ForcedAlignResult ge_forced_align_phones_to_audio_frames(
    const std::vector<PhonemeSpan>& phones,
    const std::vector<AudioFrameFeatures>& frames);

std::vector<uint32_t> ge_alignment_phone_durations_ms(
    const std::vector<PhonemeSpan>& phones,
    const std::vector<AudioFrameFeatures>& frames,
    uint32_t hop_samples_u32,
    uint32_t sample_rate_hz_u32,
    const ForcedAlignResult& ar);

} // namespace genesis
