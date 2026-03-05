#pragma once

#include <cstdint>

// Deterministic sin/cos for EigenWare text embedding.
//
// This implementation is strictly integer CORDIC in fixed-point and avoids
// std::sin/std::cos, vendor transcendentals, and platform-dependent math modes.
//
// Angle domain: Q32.32 radians, wrapped to [-pi, +pi].
// Output domain: Q32.32 for sin and cos.

struct EwSinCosQ32_32 {
    int64_t sin_q32_32;
    int64_t cos_q32_32;
};

// Compute sin/cos(theta) deterministically.
// - theta_q32_32 is in radians in Q32.32.
// - Outputs are Q32.32.
EwSinCosQ32_32 ew_cordic_sincos_q32_32(int64_t theta_q32_32);
