#pragma once

#include <cstdint>

// Deterministic budget helpers for throttling materialized work.
//
// These helpers are intentionally coarse and integer-only.
// They exist to make the carrier semantics mechanically enforceable:
//   - v_code: vector budget / representation fidelity (higher => more budget)
//   - i_code: compute density / work pressure (higher => less budget)
//
// No floats. No platform-dependent rounding. No side effects.

namespace GE {

static inline uint32_t work_budget_from_vi_u16(uint16_t v_code_u16, uint16_t i_code_u16, uint32_t hard_cap_u32) {
    if (hard_cap_u32 == 0u) hard_cap_u32 = 1u;
    // Map v and i into Q0.15 norms.
    const uint32_t v_q15 = ((uint32_t)v_code_u16 * 32767u) / 65535u;
    const uint32_t i_q15 = ((uint32_t)i_code_u16 * 32767u) / 65535u;
    const uint32_t inv_i_q15 = 32767u - i_q15;
    // scale_q15 = v_norm * (1 - i_norm)
    const uint32_t scale_q15 = (uint32_t)(((uint64_t)v_q15 * (uint64_t)inv_i_q15) / 32767u);

    // Budget shape: small always-on floor, then allocate the remainder by scale.
    const uint32_t floor_u32 = 16u;
    const uint32_t range_u32 = (hard_cap_u32 > floor_u32) ? (hard_cap_u32 - floor_u32) : 0u;
    uint32_t b = floor_u32;
    if (range_u32 > 0u) {
        b += (uint32_t)(((uint64_t)range_u32 * (uint64_t)scale_q15) / 32767u);
    }

    if (b < 1u) b = 1u;
    if (b > hard_cap_u32) b = hard_cap_u32;
    return b;
}

} // namespace GE
