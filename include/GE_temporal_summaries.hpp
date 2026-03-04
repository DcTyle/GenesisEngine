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
    // Hashes of the summaries (not cryptographic; deterministic mixing).
    uint64_t intent_hash_u64 = 0;
    uint64_t measured_hash_u64 = 0;
    // Scalar residual proxy (Q32.32), and a norm proxy (Q0.15).
    int64_t residual_q32_32 = 0;
    uint16_t residual_norm_q15 = 0;
    uint8_t residual_band_u8 = 0;
    uint8_t residual_pending_u8 = 0;
};
