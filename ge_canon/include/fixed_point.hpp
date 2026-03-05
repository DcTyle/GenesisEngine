
#pragma once
#include <cstdint>

static const int64_t TURN_SCALE = 1000000000000000000LL; // 1e18

// Safe conversions between TURN_SCALE-domain turns and Q32.32 turns.
// These helpers avoid int64 overflow from patterns like (turns_q << 32).
inline int64_t turns_q_to_q32_32(int64_t turns_q) {
    if (TURN_SCALE == 0) return 0;
    __int128 n = (__int128)turns_q;
    n = (n << 32);
    return (int64_t)(n / (__int128)TURN_SCALE);
}

inline int64_t q32_32_to_turns_q_safe(int64_t turns_q32_32) {
    __int128 p = (__int128)turns_q32_32 * (__int128)TURN_SCALE;
    return (int64_t)(p >> 32);
}
static const int32_t F_SCALE = 1 << 20;
static const int32_t F_MIN = -(1 << 30);
static const int32_t F_MAX = (1 << 30) - 1;
static const uint16_t A_MAX = 65535;

inline int64_t wrap_turns(int64_t theta) {
    int64_t mod = theta % TURN_SCALE;
    if (mod < 0) mod += TURN_SCALE;
    return mod;
}

inline int64_t delta_turns(int64_t a, int64_t b) {
    int64_t d = a - b;
    d %= TURN_SCALE;
    if (d >= TURN_SCALE / 2) d -= TURN_SCALE;
    if (d < -TURN_SCALE / 2) d += TURN_SCALE;
    return d;
}

inline int64_t round_half_even(int64_t value, int64_t divisor) {
    int64_t quotient = value / divisor;
    int64_t remainder = value % divisor;
    if (remainder > divisor / 2) return quotient + 1;
    if (remainder < divisor / 2) return quotient;
    return (quotient % 2 == 0) ? quotient : quotient + 1;
}

inline int32_t clamp_i32(int32_t v) {
    if (v < F_MIN) return F_MIN;
    if (v > F_MAX) return F_MAX;
    return v;
}

inline uint16_t clamp_u16(uint32_t v) {
    return (v > A_MAX) ? A_MAX : static_cast<uint16_t>(v);
}

inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Q32.32 helpers (deterministic fixed-point).
inline int64_t mul_q32_32(int64_t a_q32_32, int64_t b_q32_32) {
    __int128 p = (__int128)a_q32_32 * (__int128)b_q32_32;
    return (int64_t)(p >> 32);
}

inline int64_t div_q32_32(int64_t num_q32_32, int64_t den_q32_32) {
    if (den_q32_32 == 0) return 0;
    __int128 n = (__int128)num_q32_32 << 32;
    return (int64_t)(n / (__int128)den_q32_32);
}
