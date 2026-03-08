#pragma once

#include <cstdint>
#include <vector>

#include "GE_metric_registry.hpp"

class SubstrateManager;

// -----------------------------------------------------------------------------
// Learning Checkpoint Gate
//
// Executes deterministic form-fitting against measurable metric targets using
// the full available compute budget for the request window.
//
// Canonical behavior:
// - Uses the entire per-request try budget over the one-second window
//   (ticks_per_request ticks).
// - DOES NOT early-stop at tolerance; continues searching until budget exhausted.
// - Records the best fit and only accepts at end of window if best fit is within
//   6% (default) relative tolerance.
// -----------------------------------------------------------------------------

namespace genesis {

class LearningCheckpointGate {
public:
    LearningCheckpointGate();

    // Deterministic acceptance oracle for quantity-peeling updates.
    // Accept only if the proposed update improves error and is within the
    // configured lane threshold.
    static inline bool accept_rel_err(int64_t rel0_q32_32, int64_t rel1_q32_32, int64_t accept_rel_q32_32) {
        return (rel1_q32_32 <= rel0_q32_32) && (rel1_q32_32 <= accept_rel_q32_32);
    }

    // Called once per engine tick.
    void tick(::SubstrateManager* sm);

    // Access to the metric registry.
    MetricRegistry& registry() { return registry_; }
    const MetricRegistry& registry() const { return registry_; }

    // Public deterministic relative-error helper for fixed-point vectors.
    // Returns max_i |sim_i - target_i| / max(|target_i|, denom_floor) in Q32.32.
    static int64_t rel_err_q32_32_vec(const std::vector<int64_t>& sim_q32_32,
                                     const std::vector<int64_t>& target_q32_32,
                                     int64_t denom_floor_q32_32);

private:
    MetricRegistry registry_;

    // Deterministic candidate generator.
    static uint64_t xorshift64(uint64_t& s);

    // Relative error (max across dims) in Q32.32.
    static int64_t rel_err_q32_32(const MetricVector& sim, const MetricVector& target, uint32_t tol_num, uint32_t tol_den);
};

} // namespace genesis
