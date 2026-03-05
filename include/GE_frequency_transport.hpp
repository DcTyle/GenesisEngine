#pragma once

#include <cstdint>
#include "GE_causality_clamp.hpp"

namespace GE {

// Single, canonical frequency transport funnel.
// All frequency usage MUST pass through this function so causality bounds are always enforced.
inline int16_t transport_frequency_harmonic(
    int16_t f_code,
    int32_t dilation_q15,
    int32_t max_dtheta_turns_q,
    int32_t voxel_scale_q15)
{
    return clamp_frequency_causal(f_code, dilation_q15, max_dtheta_turns_q, voxel_scale_q15);
}

} // namespace GE
