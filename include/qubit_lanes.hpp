#pragma once

#include <cstdint>
#include <vector>

#include "fixed_point.hpp"

// -----------------------------------------------------------------------------
//  Qubit lane substrate (Blueprint 14.3)
// -----------------------------------------------------------------------------
// Deterministic lane abstraction for phase-delta prediction/correction.
// All math is fixed-point and uses truncation toward zero.

struct EwQubitLane {
    int64_t phase_turns_q = 0;
    int64_t delta_phi_pred_turns_q = 0;
    int64_t drift_obs_turns_q = 0;
    int64_t residual_turns_q = 0;
    int64_t confidence_q32_32 = (1LL << 32);
    int64_t weight_q32_32 = (1LL << 32);
};

struct EwLanePolicy {
    uint32_t min_lanes = 8;
    uint32_t max_lanes = 64;

    int64_t pred_gain_q32_32 = (1LL << 30); // 0.25
    int64_t conf_gain_up_q32_32 = (1LL << 29);   // 0.125
    int64_t conf_gain_down_q32_32 = (1LL << 30); // 0.25
    int64_t residual_thresh_turns_q = (TURN_SCALE / 64);
};

uint32_t ew_compute_lane_count(const std::vector<struct Pulse>& inbound, const EwLanePolicy& pol);

void ew_update_qubit_lanes(std::vector<EwQubitLane>& lanes,
                           uint64_t canonical_tick,
                           const std::vector<class Anchor>& anchors,
                           const std::vector<struct Pulse>& inbound,
                           const EwLanePolicy& pol);
