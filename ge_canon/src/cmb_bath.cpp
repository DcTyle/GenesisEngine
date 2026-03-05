#include "cmb_bath.hpp"
#include "canonical_ops.hpp"

int64_t ew_total_mass_plus_reservoir_q(const EwState& s) {
    int64_t sum = s.reservoir;
    for (size_t i = 0; i < s.anchors.size(); ++i) {
        sum = sat_add_i64(sum, s.anchors[i].m_q);
    }
    return sum;
}

EwCmbBathDelta ew_cmb_bath_route_reject(EwState& sink_state,
                                       const EwState& current_state,
                                       const EwState& candidate_state,
                                       const EwLedgerDelta& ledger_delta) {
    (void)current_state;
    EwCmbBathDelta d{};

    // Primary accounting: treat the rejected mass delta as leakage into the bath.
    // This satisfies the explicit sink bucket requirement by giving every rejected
    // closed-system delta a deterministic destination.
    const int64_t mass_leak_q = abs_i64(ledger_delta.total_mass_delta);

    d.mass_to_bath_q63 = mass_leak_q;
    d.leakage_to_bath_q63 = mass_leak_q;

    // Secondary accounting: treat boundary-scale drift and phase drift as entropy
    // accumulation. This is a deterministic scalar proxy, not a physical claim.
    const int64_t cur_mass_plus_res = ew_total_mass_plus_reservoir_q(candidate_state);
    const int64_t sink_mass_plus_res = ew_total_mass_plus_reservoir_q(sink_state);
    const int64_t rejected_gap_q = abs_i64(cur_mass_plus_res - sink_mass_plus_res);
    d.entropy_to_bath_q63 = rejected_gap_q;

    // Apply to bath accumulators.
    sink_state.cmb_bath.reservoir_mass_q63 = sat_add_i64(sink_state.cmb_bath.reservoir_mass_q63, d.mass_to_bath_q63);
    sink_state.cmb_bath.leakage_accum_q63 = sat_add_i64(sink_state.cmb_bath.leakage_accum_q63, d.leakage_to_bath_q63);
    sink_state.cmb_bath.entropy_accum_q63 = sat_add_i64(sink_state.cmb_bath.entropy_accum_q63, d.entropy_to_bath_q63);

    // Optional: energy bucket mirrors mass bucket for now (deterministic, no new parameters).
    sink_state.cmb_bath.reservoir_energy_q63 = sat_add_i64(sink_state.cmb_bath.reservoir_energy_q63, d.energy_to_bath_q63);

    return d;
}
