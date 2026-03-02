#pragma once

#include <cstdint>

// EwGovernorParams
// ---------------
// Deterministic carrier safety governor parameters.
// Grouped to keep the EwCtx surface stable and avoid scattered fields.
struct EwGovernorParams {
    int64_t tau_crit_q32_32 = (1LL << 32);
    int64_t inv_cap_q32_32 = (1LL << 32);
    uint16_t target_frac_q15 = 22938; // ~0.70 * 32768
    int64_t dwell_tau_limit_q32_32 = (1LL << 32);
    int64_t dwell_inv_limit_q32_32 = (1LL << 32);
};
