#pragma once

#include <cstdint>
#include <vector>

#include "GE_runtime.hpp"

// -----------------------------------------------------------------------------
// Canonical Hilbert-space anchor actuation contract
// -----------------------------------------------------------------------------
// This layer does not introduce a second evolution path. It exposes the
// deterministic budget/gating terms that govern whether anchor-space calculus
// may actuate on a tick, and what the bounded per-anchor phase/force envelope is
// for that same canonical tick.

struct EwHilbertActuationBudget {
    int64_t energy_budget_q32_32;
    int64_t abs_zero_floor_q32_32;
    int64_t ambient_temp_q32_32;
    int64_t cmb_sink_turns_q;
    int64_t force_magnitude_turns_q;
    bool allow_state_update;
    bool allow_force_update;
};

struct EwAnchorHilbertActuation {
    uint32_t anchor_id_u32;
    int64_t coherence_q32_32;
    int64_t local_phase_headroom_q32_32;
    int64_t max_phase_step_turns_q;
    int64_t max_force_step_turns_q;
    bool allow_force_update;
};

EwHilbertActuationBudget ge_build_hilbert_actuation_budget(
    const EwState& state,
    const EwInputs& inputs,
    const EwCtx& ctx);

EwAnchorHilbertActuation ge_build_anchor_hilbert_actuation(
    const Anchor& anchor,
    const EwHilbertActuationBudget& budget,
    const EwCtx& ctx);

std::vector<EwAnchorHilbertActuation> ge_build_anchor_hilbert_actuation_table(
    const EwState& state,
    const EwHilbertActuationBudget& budget,
    const EwCtx& ctx);
