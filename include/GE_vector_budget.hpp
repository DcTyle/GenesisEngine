#pragma once

#include <cstdint>
#include "GE_budget_helpers.hpp"

namespace GE {

// Deterministic vector-element budget per tick, shaped by voltage (v_code) and compute pressure (i_code).
// Higher v_code => higher budget. Higher i_code => lower budget.
inline uint32_t compute_vector_budget_u32(
    uint16_t v_code_u16,
    uint16_t i_code_u16,
    uint32_t base_budget_u32)
{
    // budget = base * v_norm * (1 - i_norm)  in Q0.16 normalization.
    const uint64_t v = (uint64_t)v_code_u16;          // [0..65535]
    const uint64_t i = (uint64_t)i_code_u16;          // [0..65535]
    const uint64_t one_minus_i = 65535ull - i;

    // ((base * v) * (1-i)) >> 32  keeps it deterministic without floats.
    uint64_t t = (uint64_t)base_budget_u32 * v;
    t = t * one_minus_i;
    uint32_t budget = (uint32_t)(t >> 32);

    if (budget < 8u) budget = 8u;
    return budget;
}

// Estimate object count from encoded a_code for distributing budget.
// Uses the same cap passed to encode_object_count(). This is an approximation but deterministic.
inline uint32_t decode_object_count_est_u32(uint16_t a_code_u16, uint32_t n_objects_cap_u32)
{
    // a_code ~ (n * 65535) / cap  => n ~ (a_code * cap) / 65535
    if (n_objects_cap_u32 == 0u) return 0u;
    uint32_t n = (uint32_t)(((uint64_t)a_code_u16 * (uint64_t)n_objects_cap_u32) / 65535ull);
    if (n == 0u && a_code_u16 != 0u) n = 1u;
    return n;
}

} // namespace GE
