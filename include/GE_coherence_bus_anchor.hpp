#pragma once

#include <cstdint>
#include "GE_coherence_packets.hpp"

// Global coherence bus anchor payload.
// This stores bounded ring buffers for leakage packets and the most recent
// emitted hook packets.

static const uint32_t EW_COHERENCE_BANDS = 8;
static const uint32_t EW_COHERENCE_RING_PER_BAND = 32;
static const uint32_t EW_COHERENCE_HOOK_OUT_MAX = 64;

struct EwCoherenceBusBandRing {
    // Ring buffer of published leakage packets.
    EwLeakagePublishPacket ring[EW_COHERENCE_RING_PER_BAND];
    uint32_t head_u32 = 0;
    uint32_t count_u32 = 0;
};

struct EwCoherenceBusInfluxBandRing {
    // Ring buffer of published influx packets.
    EwInfluxPublishPacket ring[EW_COHERENCE_RING_PER_BAND];
    uint32_t head_u32 = 0;
    uint32_t count_u32 = 0;
};

struct EwCoherenceBusAnchorState {
    uint64_t last_tick_u64 = 0;

    /* Derived physics coherence proxy (Q15) from recent leakage. */
    uint16_t phys_coherence_q15 = 0;
    uint16_t learning_coherence_q15 = 0;
    // Temporal coupling activity proxy (Q15) from recent residuals.
    uint16_t temporal_coherence_q15 = 0;
    uint16_t pad0_u16 = 0;

    // Per-band rings.
    EwCoherenceBusBandRing band[EW_COHERENCE_BANDS];
    EwCoherenceBusInfluxBandRing influx_band[EW_COHERENCE_BANDS];

    // Per-band caps (packets per tick) and thresholds.
    uint16_t max_packets_per_band_per_tick_u16[EW_COHERENCE_BANDS];
    uint16_t authority_cap_q15[EW_COHERENCE_BANDS];

    // Router seed (deterministic).
    uint64_t router_seed_u64 = 0;

    // Most recent emitted hooks.
    EwHookPacket hook_out[EW_COHERENCE_HOOK_OUT_MAX];
    uint32_t hook_out_count_u32 = 0;

    // Deterministic budgets.
    uint32_t budget_packets_u32 = 0;
    uint32_t budget_hold_ticks_u32 = 0;
};
