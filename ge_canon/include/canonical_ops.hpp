#pragma once
#include <cstdint>
#include <vector>

// Canonical auxiliary operators per EigenWareSpec operator registry.
// These are deterministic helpers used by substrate operators.
// NOTE: All numeric behavior is fixed-point and truncation-based (no ad-hoc rounding).

// Absolute value for int64.
int64_t abs_i64(int64_t v);

// Absolute value for Q32.32 fixed-point.
int64_t abs_q32_32(int64_t v_q32_32);

// Multiply Q32.32 * Q32.32 -> Q32.32 (truncation).
int64_t mul_q32_32(int64_t a_q32_32, int64_t b_q32_32);

// Divide Q32.32 / Q32.32 -> Q32.32 (truncation). If denominator is 0, returns 0 deterministically.
int64_t div_q32_32(int64_t num_q32_32, int64_t den_q32_32);

// Convert signed integer to Q32.32.
int64_t q32_32_from_i64(int64_t v);

// Quantize Q32.32 to a step (Q32.32). If step is 0, returns input deterministically.
int64_t quantize_q32_32(int64_t v_q32_32, int64_t step_q32_32);

// Clamp to [0,1] in Q32.32.
int64_t clamp01(int64_t v_q32_32);

// Clamp to [lo,hi] in Q32.32.
int64_t clamp_band(int64_t v_q32_32, int64_t lo_q32_32, int64_t hi_q32_32);

// 64-bit phase helpers (turns encoded as unsigned phase units).
uint64_t wrap_add_u64(uint64_t a, uint64_t b);
uint64_t phase_add_u64(uint64_t a, uint64_t b);
uint64_t phase_sub_u64(uint64_t a, uint64_t b);
int64_t phase_delta_i64(uint64_t from, uint64_t to);
int64_t phase_delta_min_i64(uint64_t from, uint64_t to);

// Convert a signed turns quantity in TURN_SCALE units to a wrapped u64 phase.
// Caller supplies turn_scale_u64 to avoid hidden constants.
uint64_t u64_phase(int64_t turns_q, uint64_t turn_scale_u64);

// Q63 multiplication helpers: treat inputs as signed Q1.63 style fixed-point stored in int64.
// Output is also Q63 (truncate).
int64_t q63_mul(int64_t a_q63, int64_t b_q63);
int64_t q63_mul_i64(int64_t a_q63, int64_t b_i64);

// Lock fixed-point value to finite representable band (deterministic saturation).
int64_t lock_fixed_point_q32_32(int64_t v_q32_32, int64_t lo_q32_32, int64_t hi_q32_32);

// Mean of a list of Q32.32 values. Empty -> 0.
int64_t mean(const std::vector<int64_t>& xs_q32_32);

// Integer norm (L1) of a vector of int64. Empty -> 0.
int64_t norm_i(const std::vector<int64_t>& xs);

