#pragma once

#include <cstdint>

// Deterministic fixed-point radix-2 FFT (Q32.32 complex).
// This is a small building block used by substrate spectral operators.

struct EwComplexQ32_32 {
    int64_t re_q32_32;
    int64_t im_q32_32;
};

// In-place radix-2 FFT.
// - n must be a power of two.
// - if inverse=true, computes IFFT and applies 1/n scaling.
void ew_fft_radix2_inplace_q32_32(EwComplexQ32_32* data, uint32_t n, bool inverse);
