#pragma once

#include <cstdint>

// Deterministic environmental constraint packet for collision/contact solvers.
// This is a CONTRACT only: it can be consumed by any future collision system
// without introducing parallel truth.

struct EwCollisionEnvPacket {
    uint64_t tick_u64 = 0;
    uint32_t object_anchor_id_u32 = 0;
    uint16_t friction_bias_q15 = 0;      // Q0.15 in [0..32767]
    uint16_t restitution_bias_q15 = 0;   // Q0.15 in [0..32767]

    // Optional: band that motivated the bias (0..EW_COHERENCE_BANDS-1).
    uint8_t coherence_band_u8 = 0;
    uint8_t pad0_u8[3] = {0,0,0};
};

