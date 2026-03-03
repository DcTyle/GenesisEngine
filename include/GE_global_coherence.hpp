#pragma once

#include <cstdint>

namespace genesis {

// Global coherence accumulator. Q15 magnitudes.
// This is a deterministic control surface that gates AI emissions and
// actuations. It aggregates multiple partition coherences and supports
// user-input "attention spike" behavior.
struct GE_GlobalCoherence {
    // Global coherence (Q15, 0..32768).
    uint16_t global_q15 = 0;
    // Partition snapshots (Q15)
    uint16_t lang_q15 = 0;
    uint16_t phys_q15 = 0;
    uint16_t crawl_q15 = 0;
    uint16_t exp_q15 = 0;

    uint64_t last_tick_u64 = 0;

    // Deterministic aggregation update.
    void update(uint64_t tick_u64,
                uint16_t lang_in_q15,
                uint16_t phys_in_q15,
                uint16_t crawl_in_q15,
                uint16_t exp_in_q15);

    // Deterministic leak/decay per tick (small integer rate).
    void leak(uint64_t tick_u64, uint16_t leak_rate_q15);

    // User input attention spike.
    // strength_q15 is the maximum additive bump to global_q15.
    void spike_from_user_input(uint64_t tick_u64, uint32_t byte_len_u32, uint16_t strength_q15);
};

} // namespace genesis
