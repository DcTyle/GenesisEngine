#include "GE_fft_fixed.hpp"

#include "ew_cordic.hpp"

static inline uint32_t ew_bitrev_u32(uint32_t x, uint32_t bits) {
    uint32_t r = 0;
    for (uint32_t i = 0; i < bits; ++i) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
    }
    return r;
}

static inline int64_t ew_mul_q32_32(int64_t a_q32_32, int64_t b_q32_32) {
    __int128 p = (__int128)a_q32_32 * (__int128)b_q32_32;
    return (int64_t)(p >> 32);
}

static inline EwComplexQ32_32 ew_cmul_q32_32(const EwComplexQ32_32& a, const EwComplexQ32_32& b) {
    // (a.re + i a.im) * (b.re + i b.im)
    const int64_t re = ew_mul_q32_32(a.re_q32_32, b.re_q32_32) - ew_mul_q32_32(a.im_q32_32, b.im_q32_32);
    const int64_t im = ew_mul_q32_32(a.re_q32_32, b.im_q32_32) + ew_mul_q32_32(a.im_q32_32, b.re_q32_32);
    return {re, im};
}

void ew_fft_radix2_inplace_q32_32(EwComplexQ32_32* data, uint32_t n, bool inverse) {
    if (!data || n < 2u) return;

    // Validate power-of-two.
    if ((n & (n - 1u)) != 0u) return;

    uint32_t bits = 0;
    while ((1u << bits) < n) ++bits;

    // Bit-reversal permutation.
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t j = ew_bitrev_u32(i, bits);
        if (j > i) {
            EwComplexQ32_32 tmp = data[i];
            data[i] = data[j];
            data[j] = tmp;
        }
    }

    // Constants in Q32.32.
    static const int64_t TWO_PI_Q32_32 = 26986075409LL;

    // Iterative Cooley-Tukey.
    for (uint32_t len = 2u; len <= n; len <<= 1u) {
        const uint32_t half = len >> 1u;
        // angle step = +/- 2pi/len
        const int64_t step_q32_32 = (inverse ? +1 : -1) * (TWO_PI_Q32_32 / (int64_t)len);
        for (uint32_t i = 0; i < n; i += len) {
            for (uint32_t j = 0; j < half; ++j) {
                const int64_t ang_q32_32 = (int64_t)j * step_q32_32;
                const EwSinCosQ32_32 sc = ew_cordic_sincos_q32_32(ang_q32_32);
                EwComplexQ32_32 w{sc.cos_q32_32, sc.sin_q32_32};

                const EwComplexQ32_32 u = data[i + j];
                const EwComplexQ32_32 v = ew_cmul_q32_32(data[i + j + half], w);
                data[i + j].re_q32_32 = u.re_q32_32 + v.re_q32_32;
                data[i + j].im_q32_32 = u.im_q32_32 + v.im_q32_32;
                data[i + j + half].re_q32_32 = u.re_q32_32 - v.re_q32_32;
                data[i + j + half].im_q32_32 = u.im_q32_32 - v.im_q32_32;
            }
        }
    }

    if (inverse) {
        // Scale by 1/n.
        const int64_t invn_q32_32 = (n != 0u) ? (int64_t)(((__int128)1 << 32) / (int64_t)n) : 0;
        for (uint32_t i = 0; i < n; ++i) {
            data[i].re_q32_32 = ew_mul_q32_32(data[i].re_q32_32, invn_q32_32);
            data[i].im_q32_32 = ew_mul_q32_32(data[i].im_q32_32, invn_q32_32);
        }
    }
}
