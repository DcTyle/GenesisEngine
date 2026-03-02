#pragma once
#include <cstdint>

// Canonical GPU backend ABI surface per Spec v7.
// This repo's GPU sampling is host-provided; CUDA backend integration will
// be wired as a dedicated module later. This header reserves the ABI without
// introducing external dependencies.
struct EwCudaPulseSampleV1 {
    uint64_t tick_u64 = 0;
    uint32_t freq_hz_u32 = 0;
    int64_t amp_q32_32 = 0;
    uint32_t width_ns_u32 = 0;
    uint32_t lane_mask_u32 = 0;
};

