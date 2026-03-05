#pragma once

#include <cstdint>

// Tiny, fixed-size summaries that formalize the temporal-coupling contract.
// These are designed to be GPU-portable "measurement lanes" later:
//  - IntentSummary: what actuation was injected
//  - MeasuredSummary: what the state evolution produced
//  - ResidualSummary: discrepancy and collapse routing metadata
// Authoritative state remains fixed-point and bounded.

struct EwIntentSummary {
    // Low-band forcing magnitudes (Q0.15) for bins 0..7.
    uint16_t band_mag_q15[8] = {0,0,0,0,0,0,0,0};
    // Mean intent magnitude (Q0.15).
    uint16_t intent_norm_q15 = 0;
    // Last observed carrier authority codes from pulses.
    uint16_t last_v_code_u16 = 0;
    uint16_t last_i_code_u16 = 0;
};

struct EwMeasuredSummary {
    // Energy summary proxies (Q0.15) for visualization/assimilation.
    uint16_t energy_mean_q15 = 0;
    uint16_t energy_peak_q15 = 0;
    uint16_t leakage_abs_q15 = 0;
    uint16_t pad0 = 0;
};

struct EwResidualSummary {
    // Deterministic signatures of the summaries (mixing; not a security mechanism).
    uint64_t intent_hash_u64 = 0;
    uint64_t measured_hash_u64 = 0;
    // Scalar residual proxy (Q32.32), and a norm proxy (Q0.15).
    int64_t residual_q32_32 = 0;
    uint16_t residual_norm_q15 = 0;
    uint8_t residual_band_u8 = 0;
    uint8_t residual_pending_u8 = 0;
};

// Optional sidecar lane that captures the discrepancy between the pulse's
// actuation intent and the measured collapsed response. In the current CPU
// implementation this is derived deterministically from post-step state.
// Later, substrate GPU kernels can write this directly.
struct EwPulseMeasuredSummary {
    uint64_t pulse_intent_hash_u64 = 0;
    uint64_t pulse_measured_hash_u64 = 0;
    int64_t pulse_residual_q32_32 = 0;
    uint16_t pulse_residual_norm_q15 = 0;
    uint8_t pulse_band_u8 = 0;
    uint8_t pulse_pending_u8 = 0;
    uint32_t pad0_u32 = 0;
};


// Optional sidecar lane for pulse intent (control-plane) history.
// This parallels EwPulseMeasuredSummary so mismatch can be evaluated over a window.
struct EwPulseIntentSummary {
    uint64_t pulse_intent_hash_u64 = 0;
    uint16_t pulse_intent_norm_q15 = 0;
    uint16_t last_v_code_u16 = 0;
    uint16_t last_i_code_u16 = 0;
    uint16_t pad0_u16 = 0;
};

static constexpr uint32_t EW_PULSE_INTENT_RING_W = 16u;

static inline uint16_t ew_pulse_intent_ring_mean_q15(const EwPulseIntentSummary* ring, uint32_t head_u32, uint32_t count_u32) {
    if (!ring || count_u32 == 0u) return 0u;
    if (count_u32 > EW_PULSE_INTENT_RING_W) count_u32 = EW_PULSE_INTENT_RING_W;
    uint32_t sum = 0u;
    for (uint32_t i = 0u; i < count_u32; ++i) {
        const uint32_t idx = (head_u32 + i) % EW_PULSE_INTENT_RING_W;
        sum += (uint32_t)ring[idx].pulse_intent_norm_q15;
    }
    uint32_t mean = sum / count_u32;
    if (mean > 65535u) mean = 65535u;
    return (uint16_t)mean;
}

// Mean absolute deviation (MAD) of intent norm over the bounded window (Q0.15).
// This is used as a stability proxy: low MAD => stable intent.
static inline uint16_t ew_pulse_intent_ring_mad_q15(const EwPulseIntentSummary* ring, uint32_t head_u32, uint32_t count_u32) {
    if (!ring || count_u32 == 0u) return 0u;
    if (count_u32 > EW_PULSE_INTENT_RING_W) count_u32 = EW_PULSE_INTENT_RING_W;
    const uint16_t mean_q15 = ew_pulse_intent_ring_mean_q15(ring, head_u32, count_u32);
    uint32_t sum = 0u;
    for (uint32_t i = 0u; i < count_u32; ++i) {
        const uint32_t idx = (head_u32 + i) % EW_PULSE_INTENT_RING_W;
        const uint16_t v = ring[idx].pulse_intent_norm_q15;
        const uint16_t d = (v > mean_q15) ? (uint16_t)(v - mean_q15) : (uint16_t)(mean_q15 - v);
        sum += (uint32_t)d;
    }
    uint32_t mad = sum / count_u32;
    if (mad > 65535u) mad = 65535u;
    return (uint16_t)mad;
}

// Measured pulse lane selection helper.
// Today: derived-from-state sidecar is always used.
// Future: GPU kernels can write a dedicated sidecar and set gpu_sidecar_valid_u8 = 1.
static inline const EwPulseMeasuredSummary* ew_select_pulse_measured_lane(
    const EwPulseMeasuredSummary* derived_lane,
    const EwPulseMeasuredSummary* gpu_lane,
    uint8_t gpu_sidecar_valid_u8
) {
    if (gpu_sidecar_valid_u8 != 0u && gpu_lane) return gpu_lane;
    return derived_lane;
}

// Bounded history window for pulse-measured summaries (used for temporal coupling smoothing).
static constexpr uint32_t EW_PULSE_MEASURED_RING_W = 16u;

static inline uint16_t ew_pulse_measured_ring_mean_q15(const EwPulseMeasuredSummary* ring, uint32_t head_u32, uint32_t count_u32) {
    if (!ring || count_u32 == 0u) return 0u;
    if (count_u32 > EW_PULSE_MEASURED_RING_W) count_u32 = EW_PULSE_MEASURED_RING_W;
    uint32_t sum = 0u;
    for (uint32_t i = 0u; i < count_u32; ++i) {
        const uint32_t idx = (head_u32 + i) % EW_PULSE_MEASURED_RING_W;
        sum += (uint32_t)ring[idx].pulse_residual_norm_q15;
    }
    uint32_t mean = sum / count_u32;
    if (mean > 65535u) mean = 65535u;
    return (uint16_t)mean;
}

// Mean absolute deviation (MAD) of measured residual norm over the bounded window (Q0.15).
// This is used as a stability proxy: low MAD => stable measured response.
static inline uint16_t ew_pulse_measured_ring_mad_q15(const EwPulseMeasuredSummary* ring, uint32_t head_u32, uint32_t count_u32) {
    if (!ring || count_u32 == 0u) return 0u;
    if (count_u32 > EW_PULSE_MEASURED_RING_W) count_u32 = EW_PULSE_MEASURED_RING_W;
    const uint16_t mean_q15 = ew_pulse_measured_ring_mean_q15(ring, head_u32, count_u32);
    uint32_t sum = 0u;
    for (uint32_t i = 0u; i < count_u32; ++i) {
        const uint32_t idx = (head_u32 + i) % EW_PULSE_MEASURED_RING_W;
        const uint16_t v = ring[idx].pulse_residual_norm_q15;
        const uint16_t d = (v > mean_q15) ? (uint16_t)(v - mean_q15) : (uint16_t)(mean_q15 - v);
        sum += (uint32_t)d;
    }
    uint32_t mad = sum / count_u32;
    if (mad > 65535u) mad = 65535u;
    return (uint16_t)mad;
}
