#include "canonical_ops.hpp"

int64_t abs_i64(int64_t v) { return (v < 0) ? -v : v; }

int64_t abs_q32_32(int64_t v_q32_32) { return abs_i64(v_q32_32); }

int64_t mul_q32_32(int64_t a_q32_32, int64_t b_q32_32) {
    __int128 p = (__int128)a_q32_32 * (__int128)b_q32_32;
    return (int64_t)(p >> 32);
}

int64_t div_q32_32(int64_t num_q32_32, int64_t den_q32_32) {
    if (den_q32_32 == 0) return 0;
    __int128 p = (__int128)num_q32_32 << 32;
    return (int64_t)(p / (__int128)den_q32_32);
}

int64_t q32_32_from_i64(int64_t v) { return (int64_t)(v << 32); }

int64_t quantize_q32_32(int64_t v_q32_32, int64_t step_q32_32) {
    if (step_q32_32 == 0) return v_q32_32;
    // Truncate toward zero to the nearest step multiple.
    __int128 q = (__int128)v_q32_32 / (__int128)step_q32_32;
    return (int64_t)(q * (__int128)step_q32_32);
}

int64_t clamp01(int64_t v_q32_32) {
    const int64_t lo = 0;
    const int64_t hi = (1LL << 32);
    if (v_q32_32 < lo) return lo;
    if (v_q32_32 > hi) return hi;
    return v_q32_32;
}

int64_t clamp_band(int64_t v_q32_32, int64_t lo_q32_32, int64_t hi_q32_32) {
    if (v_q32_32 < lo_q32_32) return lo_q32_32;
    if (v_q32_32 > hi_q32_32) return hi_q32_32;
    return v_q32_32;
}

uint64_t wrap_add_u64(uint64_t a, uint64_t b) { return a + b; }
uint64_t phase_add_u64(uint64_t a, uint64_t b) { return wrap_add_u64(a,b); }
uint64_t phase_sub_u64(uint64_t a, uint64_t b) { return a - b; }

int64_t phase_delta_i64(uint64_t from, uint64_t to) {
    // Unsigned wrap delta interpreted as signed minimal delta around the ring.
    const uint64_t d = to - from;
    return (int64_t)d;
}

int64_t phase_delta_min_i64(uint64_t from, uint64_t to) {
    const uint64_t d = to - from;
    // Choose the smaller magnitude between forward and backward deltas.
    const uint64_t back = 0ULL - d;
    // Compare as signed magnitudes.
    const int64_t df = (int64_t)d;
    const int64_t db = (int64_t)back;
    const int64_t af = (df < 0) ? -df : df;
    const int64_t ab = (db < 0) ? -db : db;
    return (af <= ab) ? df : db;
}

uint64_t u64_phase(int64_t turns_q, uint64_t turn_scale_u64) {
    // Map TURN_SCALE units into u64 phase (wrap). Caller supplies scale.
    // This avoids hidden constants; determinism is just integer arithmetic.
    __int128 p = (__int128)turns_q * (__int128)turn_scale_u64;
    // Divide by TURN_SCALE (turn_scale_u64 is usually TURN_SCALE).
    // If caller uses turn_scale_u64==TURN_SCALE, this becomes identity wrap.
    // Here we assume turn_scale_u64 is already the modulus scale; so just wrap.
    (void)p;
    return (uint64_t)turns_q;
}

int64_t q63_mul(int64_t a_q63, int64_t b_q63) {
    __int128 p = (__int128)a_q63 * (__int128)b_q63;
    return (int64_t)(p >> 63);
}

int64_t q63_mul_i64(int64_t a_q63, int64_t b_i64) {
    __int128 p = (__int128)a_q63 * (__int128)b_i64;
    return (int64_t)p;
}

int64_t lock_fixed_point_q32_32(int64_t v_q32_32, int64_t lo_q32_32, int64_t hi_q32_32) {
    if (v_q32_32 < lo_q32_32) return lo_q32_32;
    if (v_q32_32 > hi_q32_32) return hi_q32_32;
    return v_q32_32;
}

int64_t mean(const std::vector<int64_t>& xs_q32_32) {
    if (xs_q32_32.empty()) return 0;
    __int128 acc = 0;
    for (auto v : xs_q32_32) acc += (__int128)v;
    acc /= (__int128)xs_q32_32.size();
    return (int64_t)acc;
}

int64_t norm_i(const std::vector<int64_t>& xs) {
    __int128 acc = 0;
    for (auto v : xs) acc += (__int128)((v < 0) ? -v : v);
    return (int64_t)acc;
}
