#pragma once
#include <cstdint>
#include "GE_runtime.hpp"

// CMB bath helpers: deterministic routing of rejected evolution deltas into the
// global environment reservoir.
//
// NOTE: This is accounting only. It does not claim physical cosmology.
struct EwCmbBathDelta {
    int64_t mass_to_bath_q63 = 0;
    int64_t energy_to_bath_q63 = 0;
    int64_t leakage_to_bath_q63 = 0;
    int64_t entropy_to_bath_q63 = 0;
};

int64_t ew_total_mass_plus_reservoir_q(const EwState& s);

// Route a rejected candidate delta into the bath.
// - sink_state is modified in-place (bath accumulators increased deterministically)
// - returns the delta applied to the bath for telemetry
EwCmbBathDelta ew_cmb_bath_route_reject(EwState& sink_state,
                                       const EwState& current_state,
                                       const EwState& candidate_state,
                                       const EwLedgerDelta& ledger_delta);
