#include "ew_cordic.hpp"

// Q32.32 constants as exact integers (no floating evaluation at compile time).
// pi * 2^32 = 13493037705.360...
static const int64_t PI_Q32_32      = 0x00000003243F6A89LL; // 3.141592653589793 in Q32.32
static const int64_t TWO_PI_Q32_32  = 0x00000006487ED511LL; // 2*pi in Q32.32
static const int64_t HALF_PI_Q32_32 = 0x00000001921FB544LL; // pi/2

// CORDIC gain compensation (1/K) in Q32.32 for 32 iterations.
// 1/K ~= 0.6072529350088812561694
static const int64_t K_INV_Q32_32 = 0x000000009B74EDA8LL;

// atan(2^-i) table in Q32.32 radians for i=0..31.
static const int64_t ATAN_Q32_32[32] = {
    0x00000000c90fdaa2LL,
    0x0000000076b19c16LL,
    0x000000003eb6ebf2LL,
    0x000000001fd5ba9bLL,
    0x000000000ffaaddcLL,
    0x0000000007ff556fLL,
    0x0000000003ffeaabLL,
    0x0000000001fffd55LL,
    0x0000000000ffffabLL,
    0x00000000007ffff5LL,
    0x00000000003fffffLL,
    0x0000000000200000LL,
    0x0000000000100000LL,
    0x0000000000080000LL,
    0x0000000000040000LL,
    0x0000000000020000LL,
    0x0000000000010000LL,
    0x0000000000008000LL,
    0x0000000000004000LL,
    0x0000000000002000LL,
    0x0000000000001000LL,
    0x0000000000000800LL,
    0x0000000000000400LL,
    0x0000000000000200LL,
    0x0000000000000100LL,
    0x0000000000000080LL,
    0x0000000000000040LL,
    0x0000000000000020LL,
    0x0000000000000010LL,
    0x0000000000000008LL,
    0x0000000000000004LL,
    0x0000000000000002LL
};

static inline int64_t wrap_to_pi(int64_t theta_q32_32) {
    // Wrap to (-pi, +pi]
    int64_t t = theta_q32_32 % TWO_PI_Q32_32;
    if (t <= -PI_Q32_32) t += TWO_PI_Q32_32;
    if (t >  PI_Q32_32)  t -= TWO_PI_Q32_32;
    return t;
}

EwSinCosQ32_32 ew_cordic_sincos_q32_32(int64_t theta_q32_32) {
    // Range reduction to [-pi, +pi]
    int64_t z = wrap_to_pi(theta_q32_32);

    // Quadrant mapping to improve convergence: map to [-pi/2, +pi/2]
    // and apply sign flips for sin/cos.
    int sign_sin = 1;
    int sign_cos = 1;
    if (z > HALF_PI_Q32_32) {
        z = PI_Q32_32 - z;
        sign_cos = -1;
    } else if (z < -HALF_PI_Q32_32) {
        z = -PI_Q32_32 - z;
        sign_cos = -1;
        sign_sin = -1;
    }

    // CORDIC rotation mode.
    int64_t x = K_INV_Q32_32;
    int64_t y = 0;

    for (int i = 0; i < 32; ++i) {
        const int64_t x_shift = x >> i;
        const int64_t y_shift = y >> i;
        if (z >= 0) {
            x = x - y_shift;
            y = y + x_shift;
            z = z - ATAN_Q32_32[i];
        } else {
            x = x + y_shift;
            y = y - x_shift;
            z = z + ATAN_Q32_32[i];
        }
    }

    EwSinCosQ32_32 out{};
    out.cos_q32_32 = (sign_cos > 0) ? x : -x;
    out.sin_q32_32 = (sign_sin > 0) ? y : -y;
    return out;
}
