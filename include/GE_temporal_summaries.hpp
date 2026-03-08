#pragma once

#include <cstdint>
#include <array>
#include <cstdio>
#include <string>
#include "ew_id9.hpp"

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
    // Canonical vector identities of the summaries.
    EwId9 intent_id9{};
    EwId9 measured_id9{};
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
    EwId9 pulse_intent_id9{};
    EwId9 pulse_measured_id9{};
    int64_t pulse_residual_q32_32 = 0;
    uint16_t pulse_residual_norm_q15 = 0;
    uint8_t pulse_band_u8 = 0;
    uint8_t pulse_pending_u8 = 0;
    uint32_t pad0_u32 = 0;
};


// Optional sidecar lane for pulse intent (control-plane) history.
// This parallels EwPulseMeasuredSummary so mismatch can be evaluated over a window.
struct EwPulseIntentSummary {
    EwId9 pulse_intent_id9{};
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


static inline EwId9 ew_temporal_summary_id9_from_label_and_words(const char* label, const uint32_t* words, size_t word_count) {
    std::string packed = label ? std::string(label) : std::string("summary");
    char buf[64];
    for (size_t i = 0; i < word_count; ++i) {
        std::snprintf(buf, sizeof(buf), "|%u", words[i]);
        packed += buf;
    }
    return ew_id9_from_string_ascii(packed);
}

static inline EwId9 ew_temporal_intent_id9(uint32_t anchor_id_u32, const EwIntentSummary& s) {
    std::array<uint32_t, 13> words{};
    words[0] = anchor_id_u32;
    for (size_t i = 0; i < 8; ++i) words[1 + i] = (uint32_t)s.band_mag_q15[i];
    words[9] = (uint32_t)s.intent_norm_q15;
    words[10] = (uint32_t)s.last_v_code_u16;
    words[11] = (uint32_t)s.last_i_code_u16;
    words[12] = 1u;
    return ew_temporal_summary_id9_from_label_and_words("temporal_intent", words.data(), words.size());
}

static inline EwId9 ew_temporal_measured_id9(uint32_t anchor_id_u32, const EwMeasuredSummary& s, uint32_t band_u32) {
    const uint32_t words[] = {
        anchor_id_u32,
        (uint32_t)s.energy_mean_q15,
        (uint32_t)s.energy_peak_q15,
        (uint32_t)s.leakage_abs_q15,
        band_u32,
        2u
    };
    return ew_temporal_summary_id9_from_label_and_words("temporal_measured", words, sizeof(words) / sizeof(words[0]));
}

static inline EwId9 ew_pulse_intent_id9_from_intent(const EwId9& intent_id9, uint32_t pulse_norm_q15, uint32_t last_v_code_u16, uint32_t last_i_code_u16) {
    const uint32_t words[] = {
        intent_id9.u32[0], intent_id9.u32[1], intent_id9.u32[2], intent_id9.u32[3],
        pulse_norm_q15, last_v_code_u16, last_i_code_u16, 3u
    };
    return ew_temporal_summary_id9_from_label_and_words("pulse_intent", words, sizeof(words) / sizeof(words[0]));
}

static inline EwId9 ew_pulse_measured_id9_from_measured(const EwId9& measured_id9, uint32_t pulse_norm_q15, uint32_t pulse_band_u8) {
    const uint32_t words[] = {
        measured_id9.u32[0], measured_id9.u32[1], measured_id9.u32[2], measured_id9.u32[3],
        pulse_norm_q15, pulse_band_u8, 4u
    };
    return ew_temporal_summary_id9_from_label_and_words("pulse_measured", words, sizeof(words) / sizeof(words[0]));
}

static inline EwId9 ew_leakage_payload_id9(uint32_t anchor_id_u32, uint32_t energy_peak_q15, uint32_t energy_mean_q15, uint32_t anchor_slot_u32) {
    const uint32_t words[] = { anchor_id_u32, energy_peak_q15, energy_mean_q15, anchor_slot_u32, 5u };
    return ew_temporal_summary_id9_from_label_and_words("leakage_payload", words, sizeof(words) / sizeof(words[0]));
}

static inline EwId9 ew_influx_payload_id9(uint32_t anchor_id_u32, uint32_t particle_count_u32, uint32_t influx_band_u8, uint32_t learning_coupling_q15) {
    const uint32_t words[] = { anchor_id_u32, particle_count_u32, influx_band_u8, learning_coupling_q15, 6u };
    return ew_temporal_summary_id9_from_label_and_words("influx_payload", words, sizeof(words) / sizeof(words[0]));
}
