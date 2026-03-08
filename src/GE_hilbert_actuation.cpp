#include "GE_hilbert_actuation.hpp"

#include "fixed_point.hpp"
#include "canonical_ops.hpp"

namespace {

static inline int64_t clamp_q32_32_local(int64_t v, int64_t lo, int64_t hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline int64_t abs_i64_local(int64_t x) {
    return (x >= 0) ? x : -x;
}

} // namespace

EwHilbertActuationBudget ge_build_hilbert_actuation_budget(
    const EwState& state,
    const EwInputs& inputs,
    const EwCtx& ctx) {
    (void)ctx;
    EwHilbertActuationBudget out{};
    out.energy_budget_q32_32 = ((int64_t)state.reservoir << 32)
        + (int64_t)(state.cmb_bath.reservoir_energy_q63 >> 31);
    out.abs_zero_floor_q32_32 = 0;
    out.allow_state_update = (out.energy_budget_q32_32 > out.abs_zero_floor_q32_32);
    out.ambient_temp_q32_32 = lock_fixed_point_q32_32(out.energy_budget_q32_32, 0, (8LL << 32));
    out.cmb_sink_turns_q = (int64_t)(((__int128)out.ambient_temp_q32_32 * (__int128)ctx.max_dtheta_turns_q) >> (32 + 3));
    out.force_magnitude_turns_q = abs_i64_local(inputs.pending_text_x_q)
        + abs_i64_local(inputs.pending_image_y_q)
        + abs_i64_local(inputs.pending_audio_z_q);
    out.allow_force_update = out.allow_state_update && (out.force_magnitude_turns_q > out.cmb_sink_turns_q);
    return out;
}

EwAnchorHilbertActuation ge_build_anchor_hilbert_actuation(
    const Anchor& anchor,
    const EwHilbertActuationBudget& budget,
    const EwCtx& ctx) {
    EwAnchorHilbertActuation out{};
    out.anchor_id_u32 = anchor.id;
    if (!budget.allow_state_update) {
        out.coherence_q32_32 = 0;
        out.local_phase_headroom_q32_32 = 0;
        out.max_phase_step_turns_q = 0;
        out.max_force_step_turns_q = 0;
        out.allow_force_update = false;
        return out;
    }

    const int64_t chi_ref = (ctx.td_params.chi_ref_turns_q > 0) ? ctx.td_params.chi_ref_turns_q : 1;
    int64_t coherence_q32_32 = (((__int128)anchor.chi_q) << 32) / chi_ref;
    coherence_q32_32 = clamp_q32_32_local(coherence_q32_32, 0, (2LL << 32));

    const int64_t one_q32_32 = (1LL << 32);
    int64_t headroom_q32_32 = coherence_q32_32;
    if (headroom_q32_32 > one_q32_32) headroom_q32_32 = one_q32_32;
    if (headroom_q32_32 < (one_q32_32 / 16)) headroom_q32_32 = (one_q32_32 / 16);

    out.coherence_q32_32 = coherence_q32_32;
    out.local_phase_headroom_q32_32 = headroom_q32_32;
    out.max_phase_step_turns_q = (int64_t)(((__int128)ctx.max_dtheta_turns_q * (__int128)headroom_q32_32) >> 32);
    out.max_force_step_turns_q = budget.allow_force_update
        ? (int64_t)(((__int128)budget.force_magnitude_turns_q * (__int128)headroom_q32_32) >> 32)
        : 0;
    if (out.max_force_step_turns_q < 0) out.max_force_step_turns_q = 0;
    out.allow_force_update = budget.allow_force_update && (out.max_force_step_turns_q > budget.cmb_sink_turns_q);
    return out;
}

std::vector<EwAnchorHilbertActuation> ge_build_anchor_hilbert_actuation_table(
    const EwState& state,
    const EwHilbertActuationBudget& budget,
    const EwCtx& ctx) {
    std::vector<EwAnchorHilbertActuation> out;
    out.reserve(state.anchors.size());
    for (size_t i = 0; i < state.anchors.size(); ++i) {
        out.push_back(ge_build_anchor_hilbert_actuation(state.anchors[i], budget, ctx));
    }
    return out;
}
