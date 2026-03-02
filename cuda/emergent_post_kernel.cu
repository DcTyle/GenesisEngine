#include <stdint.h>

// Emergent bloom/interference post-pass.
// This is NOT a blur. It is a deterministic local wave summation over
// existing radiance/harmonic channels.
//
// Inputs:
//  - L0: radiance (float)
//  - L1: doppler proxy kD (float, ~[-1,1])
//  - L3: coherence proxy Ic (float)
//  - out_bgra8: slice buffer to add bloom into
//
// We intentionally avoid sin/cos or stochastic noise; interference is modeled
// as a signed accumulation of phase-weighted intensity.

__device__ __forceinline__ float ew_absf(float x) { return (x >= 0.0f) ? x : -x; }
__device__ __forceinline__ float ew_clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

extern "C" __global__
void ew_kernel_emergent_bloom_add(const float* L0, const float* L1, const float* L3,
                                  uint8_t* out_bgra8,
                                  int gx, int gy, int slice_z,
                                  float bloom_gain) {
    const int tid = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    const int n_slice = gx * gy;
    if (tid >= n_slice) return;

    const int y = tid / gx;
    const int x = tid - y * gx;
    const int i = (slice_z * (gx * gy)) + (y * gx + x);

    // Small neighborhood interference sum.
    // Radius=2 (5x5) is a local interaction scale, not a Gaussian blur.
    float acc = 0.0f;
    #pragma unroll
    for (int oy = -2; oy <= 2; ++oy) {
        const int yy = y + oy;
        if ((unsigned)yy >= (unsigned)gy) continue;
        #pragma unroll
        for (int ox = -2; ox <= 2; ++ox) {
            const int xx = x + ox;
            if ((unsigned)xx >= (unsigned)gx) continue;
            const int j = (slice_z * (gx * gy)) + (yy * gx + xx);
            const float rad = L0[j];
            const float kd  = L1[j];
            const float ic  = L3[j];
            // Signed accumulation: kd acts as a phase sign proxy.
            acc += rad * ic * kd;
        }
    }

    float bloom = ew_absf(acc) * bloom_gain;
    bloom = ew_clampf(bloom, 0.0f, 1.0f);
    const uint8_t add = (uint8_t)(bloom * 255.0f);

    const int o = tid * 4;
    // Add to RGB equally (spectral mapping already in base slice).
    uint32_t b = (uint32_t)out_bgra8[o + 0] + (uint32_t)add;
    uint32_t g = (uint32_t)out_bgra8[o + 1] + (uint32_t)add;
    uint32_t r = (uint32_t)out_bgra8[o + 2] + (uint32_t)add;
    out_bgra8[o + 0] = (uint8_t)((b > 255u) ? 255u : b);
    out_bgra8[o + 1] = (uint8_t)((g > 255u) ? 255u : g);
    out_bgra8[o + 2] = (uint8_t)((r > 255u) ? 255u : r);
    out_bgra8[o + 3] = 255;
}
