#pragma once

#include <cstdint>

// -----------------------------------------------------------------------------
// Anchor harmonic fingerprint (Blueprint 3.5 / Omega.3)
// -----------------------------------------------------------------------------
// Intent:
// - Provide a fully enumerated, deterministic per-anchor harmonic identity.
// - This identity is used as a stable "carrier-band" descriptor.
// - It is computed from immutable anchor identity + the anchor's 9D coordinate.
// - It MUST NOT depend on floating point, randomness, or external time.

static const int kDims9 = 9;
static const int64_t Q63_ONE = (int64_t)(1ULL << 63);

struct AnchorHarmonicFingerprintQ63 {
    // Q63 carrier in [0..Q63_ONE].
    int64_t base_freq_code_q63;
    // Q63 bandwidth proxy in [0..Q63_ONE].
    int64_t bandwidth_q63;
    // Per-dimension harmonic order in [1..9].
    uint32_t harmonic_order[kDims9];
    // Q63 weights (1/k decay).
    int64_t harmonic_weight_q63[kDims9];
};

// Build a fully enumerated harmonic fingerprint for one anchor.
// coord_turns_q is a 9D coordinate in TURN_SCALE-domain turns.
AnchorHarmonicFingerprintQ63 ew_build_anchor_fp(uint32_t anchor_id, const int64_t coord_turns_q[kDims9]);
