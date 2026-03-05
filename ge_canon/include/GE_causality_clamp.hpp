#pragma once

#include <cstdint>

// Deterministic causality clamp utilities.
//
// Goal: ensure transported phase per tick cannot exceed a configured bound
// (max_dtheta_eff_turns_q) after time dilation and harmonic weighting.
//
// All math is integer / fixed-point; no floats.

namespace GE {

// Compute the transport delta in TURN_SCALE units for a given f_code,
// then clamp it to +/- max_dtheta_eff_turns_q by scaling f_code if needed.
//
// Inputs:
//  - f_code: signed frequency code (F_SCALE domain)
//  - step_factor_q32_32: time dilation step factor (Q32.32)
//  - weight_q15: harmonic weight (Q0.15)
//  - max_dtheta_eff_turns_q: signed bound in TURN_SCALE units (>=0)
//  - TURN_SCALE: scale of turns domain
//  - F_SCALE: scale of f_code domain
//
// Outputs:
//  - clamped f_code (same sign as input, possibly reduced)
//  - out_delta_turns_q: the clamped delta that would be applied
static inline int32_t clamp_frequency_by_causality(
    int32_t f_code,
    int64_t step_factor_q32_32,
    uint16_t weight_q15,
    int64_t max_dtheta_eff_turns_q,
    int64_t TURN_SCALE,
    int64_t F_SCALE,
    int64_t* out_delta_turns_q
) {
    if (out_delta_turns_q) *out_delta_turns_q = 0;
    if (F_SCALE <= 0 || TURN_SCALE <= 0) return f_code;
    if (weight_q15 == 0 || max_dtheta_eff_turns_q <= 0) return f_code;
    if (step_factor_q32_32 <= 0) return f_code;
    if (f_code == 0) return 0;

    const int64_t sign = (f_code < 0) ? -1 : 1;
    int64_t f_abs = (f_code < 0) ? -(int64_t)f_code : (int64_t)f_code;

    // delta_scaled = f_code * (TURN_SCALE/F_SCALE) * step_factor * weight
    // implemented as:
    //   delta = f_abs * (TURN_SCALE / F_SCALE)
    //   delta_scaled = ((delta * step_factor_q32_32) >> 32) * weight_q15 >> 15
    // but kept in wide accumulator to avoid overflow.
    __int128 delta = (__int128)f_abs * (__int128)(TURN_SCALE / F_SCALE);
    __int128 p = delta * (__int128)step_factor_q32_32;
    p = p * (__int128)weight_q15;
    int64_t delta_scaled = (int64_t)(p >> (32 + 15));
    if (delta_scaled < 0) delta_scaled = -delta_scaled; // magnitude

    if (delta_scaled <= max_dtheta_eff_turns_q) {
        if (out_delta_turns_q) *out_delta_turns_q = (int64_t)(sign * delta_scaled);
        return f_code;
    }

    // Scale f_abs down so delta_scaled <= bound.
    // Since delta_scaled is ~linear in f_abs, scale factor ~= bound/delta_scaled.
    // Use integer division (floor) for deterministic truncation.
    const int64_t bound = max_dtheta_eff_turns_q;
    const int64_t scaled_f_abs = (delta_scaled > 0) ? (int64_t)(((__int128)f_abs * (__int128)bound) / (__int128)delta_scaled) : 0;
    int64_t f_new = scaled_f_abs;
    if (f_new < 1) f_new = 1; // preserve non-zero transport direction
    if (f_new > 2147483647LL) f_new = 2147483647LL;

    // Recompute the clamped delta for reporting.
    __int128 delta2 = (__int128)f_new * (__int128)(TURN_SCALE / F_SCALE);
    __int128 p2 = delta2 * (__int128)step_factor_q32_32;
    p2 = p2 * (__int128)weight_q15;
    int64_t delta2_scaled = (int64_t)(p2 >> (32 + 15));
    if (delta2_scaled < 0) delta2_scaled = -delta2_scaled;
    if (delta2_scaled > bound) delta2_scaled = bound;
    if (out_delta_turns_q) *out_delta_turns_q = (int64_t)(sign * delta2_scaled);

    const int64_t f_signed = sign * f_new;
    if (f_signed < -(int64_t)2147483647LL) return (int32_t)-2147483647;
    if (f_signed > (int64_t)2147483647LL) return (int32_t)2147483647;
    return (int32_t)f_signed;
}

} // namespace GE
