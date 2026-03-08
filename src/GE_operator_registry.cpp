#include "GE_operator_registry.hpp"
#include "GE_hilbert_actuation.hpp"
#include "ancilla_ops.hpp"

#include "canonical_ops.hpp"

#include "constrained_projection.hpp"

#include "delta_profiles.hpp"
#include "qubit_lanes.hpp"

#include "substrate_alu.hpp"
#include "harmonic_signature.hpp"
#include "substrate_harmonics.hpp"
#include "ewmesh_voxelizer.hpp"
#include "GE_uv_atlas_baker.hpp"

#include <cstddef>
#include <string>

static inline int64_t clamp_i64_from_i128(__int128 v) {
    const __int128 lo = (__int128)INT64_MIN;
    const __int128 hi = (__int128)INT64_MAX;
    if (v < lo) return INT64_MIN;
    if (v > hi) return INT64_MAX;
    return (int64_t)v;
}


static inline int64_t clamp_q32_32_local(int64_t v, int64_t lo, int64_t hi) {
    return lock_fixed_point_q32_32(v, lo, hi);
}

static inline int64_t q32_32_div_i64_local(int64_t num, int64_t den) {
    if (den == 0) return 0;
    __int128 p = (__int128)num << 32;
    return (int64_t)(p / (__int128)den);
}

// Deterministic natural log approximation for Q32.32 inputs.
// Domain: x_q32_32 > 0. Clamped internally to [2^-16, 2^4].
// Output: ln(x) in Q32.32.
static inline int64_t ln_q32_32_local(int64_t x_q32_32) {
    const int64_t min_x = (1LL << 16);      // 2^-16
    const int64_t max_x = (16LL << 32);     // 16
    if (x_q32_32 < min_x) x_q32_32 = min_x;
    if (x_q32_32 > max_x) x_q32_32 = max_x;

    // Normalize x into m * 2^e with m in [1,2).
    int32_t e = 0;
    int64_t m = x_q32_32;
    while (m >= (2LL << 32)) { m >>= 1; e++; }
    while (m <  (1LL << 32)) { m <<= 1; e--; }

    // y = m - 1 in Q32.32, where m in [1,2).
    const int64_t one = (1LL << 32);
    const int64_t y = m - one;

    // ln(1+y) ≈ y - y^2/2 + y^3/3 - y^4/4 (fixed polynomial, deterministic).
    const __int128 y2 = (__int128)y * (__int128)y;
    const __int128 y3 = y2 * (__int128)y;
    const __int128 y4 = y3 * (__int128)y;

    const int64_t y2_q32_32 = (int64_t)(y2 >> 32);
    const int64_t y3_q32_32 = (int64_t)(y3 >> 64);
    const int64_t y4_q32_32 = (int64_t)(y4 >> 96);

    int64_t ln_m = 0;
    ln_m += y;
    ln_m -= (y2_q32_32 / 2);
    ln_m += (y3_q32_32 / 3);
    ln_m -= (y4_q32_32 / 4);

    // ln2 in Q32.32.
    const int64_t ln2_q32_32 = 0x00000000B17217F7LL; // ~0.6931471805599453
    const int64_t ln_pow2 = (int64_t)e * ln2_q32_32;
    return ln_m + ln_pow2;
}

static inline int64_t total_mass_q(const EwState& s) {
    int64_t sum = s.reservoir;
    for (size_t i = 0; i < s.anchors.size(); ++i) {
        sum += s.anchors[i].m_q;
    }
    return sum;
}

static inline void apply_pending_displacements(EwState& s, const EwInputs& inputs) {
    if (inputs.pending_text_x_q == 0 && inputs.pending_image_y_q == 0 && inputs.pending_audio_z_q == 0) return;
    for (size_t i = 0; i < s.anchors.size(); ++i) {
        Anchor& a = s.anchors[i];
        a.basis9.d[0] = a.basis9.d[0] + inputs.pending_text_x_q;
        a.basis9.d[1] = a.basis9.d[1] + inputs.pending_image_y_q;
        a.basis9.d[2] = a.basis9.d[2] + inputs.pending_audio_z_q;
    }
}

static inline void apply_inbound_pulses(EwState& s, const EwInputs& inputs) {
    // Deprecated in strict mode. Inbound pulses are sampled at tau_delta and
    // applied via phase-anchor extraction + transport coupling inside evolve_state.
    // This function is intentionally a no-op to prevent direct phase mutation.
    (void)s;
    (void)inputs;
}

static inline void expand_boundary(EwState& s, const EwCtx& ctx) {
    ancilla_particle* an = (!s.ancilla.empty()) ? &s.ancilla[0] : nullptr;
    if (ctx.hubble_h0_q32_32 == 0 || ctx.tick_dt_seconds_q32_32 == 0) return;
    const int64_t step_eff_q32_32 = ew_alu_mul_q32_32(ctx, an, ctx.boundary_scale_step_q32_32, ctx.envelope_headroom_q32_32);
    s.boundary_scale_q32_32 = ew_alu_mul_q32_32(ctx, an, s.boundary_scale_q32_32, step_eff_q32_32);
}


static inline void ew_update_env_and_reaction_continuous(ancilla_particle* an,
                                                        const EwState& current_state,
                                                        const EwCtx& ctx,
                                                        int64_t pulse_delta_I_mA_q32_32) {
    if (!an) return;

    // Derive an ambient thermal floor from the CMB bath energy accumulator.
    // This is a deterministic "absolute zero reference" for the simulation domain.
    // Q63 -> Q32.32 by >>31.
    int64_t ambient_temp_q32_32 = (int64_t)(current_state.cmb_bath.reservoir_energy_q63 >> 31);
    ambient_temp_q32_32 = lock_fixed_point_q32_32(ambient_temp_q32_32, 0, (8LL << 32));

    // Continuous relaxation toward ambient (very slow, deterministic).
    const int64_t one = (1LL << 32);
    const int64_t relax_k_q32_32 = (one / 256); // ~0.0039 per tick
    const int64_t dT_q32_32 = ambient_temp_q32_32 - an->env_temp_q32_32;
    an->env_temp_q32_32 = an->env_temp_q32_32 + ew_alu_mul_q32_32(ctx, an, dT_q32_32, relax_k_q32_32);

    // Pulse-driven heating proxy: local absolute current change injects heat.
    const int64_t abs_dI = abs_i64(pulse_delta_I_mA_q32_32);
    const int64_t heat_k_q32_32 = (one / 65536); // very small
    an->env_temp_q32_32 = an->env_temp_q32_32 + ew_alu_mul_q32_32(ctx, an, abs_dI, heat_k_q32_32);

    // Oxygen relaxes toward nominal (1.0) very slowly.
    const int64_t oxygen_nom_q32_32 = one;
    const int64_t dO_q32_32 = oxygen_nom_q32_32 - an->env_oxygen_q32_32;
    an->env_oxygen_q32_32 = an->env_oxygen_q32_32 + ew_alu_mul_q32_32(ctx, an, dO_q32_32, (one / 1024));

    // Continuous oxidation proxy: rate depends on (T - floor) and oxygen.
    // Floor is tied to ambient_temp, so colder baths raise the effective threshold.
    const int64_t floor_q32_32 = ew_alu_mul_q32_32(ctx, an, ambient_temp_q32_32, (one / 8));
    int64_t temp_above_q32_32 = an->env_temp_q32_32 - floor_q32_32;
    if (temp_above_q32_32 < 0) temp_above_q32_32 = 0;

    // rate ≈ k * temp_above * oxygen
    const int64_t k_rxn_q32_32 = (one / 2048);
    int64_t rate_q32_32 = ew_alu_mul_q32_32(ctx, an, temp_above_q32_32, an->env_oxygen_q32_32);
    rate_q32_32 = ew_alu_mul_q32_32(ctx, an, rate_q32_32, k_rxn_q32_32);
    if (rate_q32_32 < 0) rate_q32_32 = 0;

    // Integrate continuously (dt = 1 tick).
    an->reaction_rate_q32_32 = rate_q32_32;
    an->oxidation_q32_32 = lock_fixed_point_q32_32(an->oxidation_q32_32 + rate_q32_32, 0, one);
    an->env_oxygen_q32_32 = lock_fixed_point_q32_32(an->env_oxygen_q32_32 - rate_q32_32, 0, one);
}



static inline Basis9 projected_for_local(const EwCtx& ctx, ancilla_particle* an, const Anchor& a, int64_t boundary_scale_q32_32) {
    Basis9 out = a.basis9;

    // Deterministic frame mismatch: phase axis only.
    out.d[4] = wrap_turns(out.d[4] + ctx.frame_gamma_turns_q);

    // Boundary expansion scales spatial axes.
    if (boundary_scale_q32_32 != (1LL << 32)) {
        out.d[0] = ew_alu_mul_q32_32(ctx, an, ew_alu_mul_q32_32(ctx, an, boundary_scale_q32_32, ctx.sx_q32_32), out.d[0]);
        out.d[1] = ew_alu_mul_q32_32(ctx, an, ew_alu_mul_q32_32(ctx, an, boundary_scale_q32_32, ctx.sy_q32_32), out.d[1]);
        out.d[2] = ew_alu_mul_q32_32(ctx, an, ew_alu_mul_q32_32(ctx, an, boundary_scale_q32_32, ctx.sz_q32_32), out.d[2]);
    }
    return out;
}

static inline void compute_time_dilation(std::vector<int64_t>& out_step_factor_q32_32,
                                         EwState& s,
                                         const EwCtx& ctx) {
    const int64_t one_q32_32 = (1LL << 32);
    out_step_factor_q32_32.assign(s.anchors.size(), one_q32_32);

    for (size_t i = 0; i < s.anchors.size(); ++i) {
        Anchor& a = s.anchors[i];
        ancilla_particle* an = (i < s.ancilla.size()) ? &s.ancilla[i] : nullptr;

        // Curvature surrogate: average absolute neighbor phase mismatch.
        int64_t curv_abs_sum = 0;
        size_t ncnt = 0;
        for (size_t k = 0; k < a.neighbors.size(); ++k) {
            const uint32_t nid = a.neighbors[k];
            if (nid >= s.anchors.size()) continue;
            const int64_t d = delta_turns(a.theta_q, s.anchors[nid].theta_q);
            curv_abs_sum += abs_i64(d);
            ncnt++;
        }
        const int64_t curv_avg = (ncnt > 0) ? (curv_abs_sum / (int64_t)ncnt) : 0;

        // Doppler surrogate: signed phase change since last tick.
        const int64_t dop = delta_turns(a.theta_q, a.last_theta_q);

        // Carrier-traced normalized observables (Q32.32).
        ew_alu_trace_turns_pair(ctx, an, a.chi_q, ctx.td_params.chi_ref_turns_q);
        const int64_t coh_q32_32 = clamp_q32_32_local(q32_32_div_i64_local(a.chi_q, ctx.td_params.chi_ref_turns_q), 0, (2LL << 32));

        ew_alu_trace_turns_pair(ctx, an, curv_avg, ctx.td_params.norm_turns_q);
        const int64_t curv_q32_32 = clamp_q32_32_local(q32_32_div_i64_local(curv_avg, ctx.td_params.norm_turns_q), 0, one_q32_32);

        ew_alu_trace_turns_pair(ctx, an, abs_i64(dop), ctx.td_params.norm_turns_q);
        const int64_t dop_q32_32 = clamp_q32_32_local(q32_32_div_i64_local(abs_i64(dop), ctx.td_params.norm_turns_q), 0, one_q32_32);

        const int64_t coh_delta = coh_q32_32 - one_q32_32;
        const int64_t t1 = ew_alu_mul_q32_32(ctx, an, ctx.td_params.k_coh_q32_32, coh_delta);
        const int64_t t2 = ew_alu_mul_q32_32(ctx, an, ctx.td_params.k_curv_q32_32, curv_q32_32);
        const int64_t t3 = ew_alu_mul_q32_32(ctx, an, ctx.td_params.k_dop_q32_32, dop_q32_32);

        int64_t td = one_q32_32 + t1 - t2 - t3;
        td = clamp_q32_32_local(td, ctx.td_params.td_min_q32_32, ctx.td_params.td_max_q32_32);
        out_step_factor_q32_32[i] = td;

        // Local temporal accumulator.
        __int128 tp = (__int128)td * (__int128)TURN_SCALE;
        const int64_t dtau_turns = (int64_t)(tp >> 32);
        // Carrier-traced local temporal accumulator update.
        ew_alu_trace_turns_pair(ctx, an, dtau_turns, TURN_SCALE);
        a.tau_turns_q += dtau_turns;

        a.update_derived_terms(curv_avg, dop);
    }
}

EwState evolve_state(const EwState& current_state, const EwInputs& inputs, const EwCtx& ctx) {
    EwState cand = current_state;

    if (cand.ancilla.size() != cand.anchors.size()) {
        cand.ancilla.assign(cand.anchors.size(), ancilla_particle{});
    }

    // Dispatcher derived parameters.
    const int64_t k_phase_current_q32_32 = (ctx.phase_max_displacement_q32_32 > 0)
        ? div_q32_32(ctx.pulse_current_max_mA_q32_32, ctx.phase_max_displacement_q32_32)
        : 0;
    const int64_t phase_quantum_q32_32 = (k_phase_current_q32_32 != 0)
        ? div_q32_32(ctx.phase_orbital_displacement_unit_mA_q32_32, k_phase_current_q32_32)
        : 0;

    // Advance canonical schedule counter.
    cand.canonical_tick = current_state.canonical_tick + 1;

    // -----------------------------------------------------------------
    // Baseline propagation rules (user directive):
    //  1) Nothing updates unless the energy budget is above absolute zero.
    //  2) No force update unless it surpasses the CMB sink.
    //
    // NOTE: These are simulation-domain thresholds (deterministic effective
    // floors), not literal thermodynamic claims.
    //
    // Energy budget is derived from state-resident reservoirs so the gate
    // remains internal to the substrate state (no external knobs).
    // - reservoir is the primary budget (Q0), promoted to Q32.32.
    // - CMB bath energy is a secondary accumulator (Q63), reduced to Q32.32.
    const EwHilbertActuationBudget hilbert_budget = ge_build_hilbert_actuation_budget(current_state, inputs, ctx);
    if (!hilbert_budget.allow_state_update) {
        // Freeze evolution when the domain has no available Hilbert-space
        // actuation budget. We still advance canonical_tick to preserve
        // deterministic time indexing, but no state variables are mutated.
        return cand;
    }
    const std::vector<EwAnchorHilbertActuation> hilbert_actuation =
        ge_build_anchor_hilbert_actuation_table(current_state, hilbert_budget, ctx);
    (void)hilbert_actuation;

    // Boundary expansion.
    expand_boundary(cand, ctx);

    // Apply modality displacement (force) only if it clears the CMB sink gate.
    if (hilbert_budget.allow_force_update) {
        apply_pending_displacements(cand, inputs);
    }
    apply_inbound_pulses(cand, inputs);

    // -----------------------------------------------------------------
    // Pulse-delta sampling at t_k_plus = t_k + tau_delta
    // -----------------------------------------------------------------
    // For each anchor, gather the latest inbound pulse codes for this tick.
    // Sampling is deterministic and uses the anchor's stored previous codes.
    std::vector<int32_t> cur_f_code;
    std::vector<uint16_t> cur_a_code;
    std::vector<uint16_t> cur_v_code;
    std::vector<uint16_t> cur_i_code;
    std::vector<uint8_t> cur_profile;
    std::vector<uint8_t> cur_tag;
    cur_f_code.assign(cand.anchors.size(), 0);
    cur_a_code.assign(cand.anchors.size(), 0);
    cur_v_code.assign(cand.anchors.size(), 0);
    cur_i_code.assign(cand.anchors.size(), 0);
    cur_profile.assign(cand.anchors.size(), ctx.default_profile_id);
    cur_tag.assign(cand.anchors.size(), 0);
    for (size_t i = 0; i < inputs.inbound.size(); ++i) {
        const Pulse& p = inputs.inbound[i];
        if (p.anchor_id < cand.anchors.size()) {
            // Deterministic selection: higher causal_tag wins.
            const uint32_t idx = p.anchor_id;
            if (p.causal_tag >= cur_tag[idx]) {
                cur_tag[idx] = p.causal_tag;
                cur_f_code[idx] = p.f_code;
                cur_a_code[idx] = p.a_code;
                cur_v_code[idx] = p.v_code;
                cur_i_code[idx] = p.i_code;
                cur_profile[idx] = p.profile_id;
            }
        }
    }

    // -----------------------------------------------------------------
    // Carrier safety governor (deterministic)
    // -----------------------------------------------------------------
    // Map (f,a,v,i) to:
    //   noise_gate = v*i (eligible update energy above floors)
    //   tau_comp  = |f|*a (normalized)  -> temporal compression proxy
    //   inv_risk  = tau_comp/(v+eps)    -> event-horizon invertibility proxy
    //
    // Enforce hard caps and a 70% target operating point to avoid sustained peaks.
    // Governor acts by clamping effective amplitude (a) and by reducing available
    // current headroom (remaining_headroom) during ancilla dispatch.

    const int64_t tau_crit_q32_32 = (ctx.governor.tau_crit_q32_32 > 0) ? ctx.governor.tau_crit_q32_32 : (1LL << 32);
    const int64_t inv_cap_q32_32 = (ctx.governor.inv_cap_q32_32 > 0) ? ctx.governor.inv_cap_q32_32 : (1LL << 32);

    const int64_t tau_target_q32_32 = (int64_t)(((__int128)tau_crit_q32_32 * (__int128)ctx.governor.target_frac_q15) >> 15);
    const int64_t inv_target_q32_32 = (int64_t)(((__int128)inv_cap_q32_32 * (__int128)ctx.governor.target_frac_q15) >> 15);

    // eps ~= 1/V_MAX in Q32.32 to avoid divide-by-zero and to model minimum headroom.
    const int64_t v_eps_q32_32 = (int64_t)(((__int128)1 << 32) / (int64_t)V_MAX);

    int64_t tau_max_q32_32 = 0;
    int64_t inv_max_q32_32 = 0;

    for (size_t i = 0; i < cand.anchors.size(); ++i) {
        const int32_t f_k = cur_f_code[i];
        const uint16_t a_k = cur_a_code[i];
        const uint16_t v_k = cur_v_code[i];
        const uint16_t i_k = cur_i_code[i];

        const int64_t absf = (f_k < 0) ? -(int64_t)f_k : (int64_t)f_k;
        const int64_t f_n_q32_32 = (F_MAX != 0) ? (int64_t)(((__int128)absf << 32) / (int64_t)F_MAX) : 0;
        const int64_t a_n_q32_32 = (A_MAX != 0) ? (int64_t)(((__int128)((int64_t)a_k + 1) << 32) / (int64_t)A_MAX) : 0;
        const int64_t v_n_q32_32 = (V_MAX != 0) ? (int64_t)(((__int128)((int64_t)v_k + 1) << 32) / (int64_t)V_MAX) : 0;

        const int64_t tau_comp_q32_32 = mul_q32_32(f_n_q32_32, a_n_q32_32);
        const int64_t inv_risk_q32_32 = div_q32_32(tau_comp_q32_32, (v_n_q32_32 + v_eps_q32_32));

        if (tau_comp_q32_32 > tau_max_q32_32) tau_max_q32_32 = tau_comp_q32_32;
        if (inv_risk_q32_32 > inv_max_q32_32) inv_max_q32_32 = inv_risk_q32_32;

        (void)i_k; // current is budgeted via ancilla headroom below
    }

    // Deterministic dwell accumulators with gentle decay (~1/16 per tick).
    cand.gov_dwell_tau_q32_32 -= (cand.gov_dwell_tau_q32_32 >> 4);
    cand.gov_dwell_inv_q32_32 -= (cand.gov_dwell_inv_q32_32 >> 4);
    if (cand.gov_dwell_tau_q32_32 < 0) cand.gov_dwell_tau_q32_32 = 0;
    if (cand.gov_dwell_inv_q32_32 < 0) cand.gov_dwell_inv_q32_32 = 0;

    if (tau_max_q32_32 > tau_target_q32_32) cand.gov_dwell_tau_q32_32 = sat_add_i64(cand.gov_dwell_tau_q32_32, (tau_max_q32_32 - tau_target_q32_32));
    if (inv_max_q32_32 > inv_target_q32_32) cand.gov_dwell_inv_q32_32 = sat_add_i64(cand.gov_dwell_inv_q32_32, (inv_max_q32_32 - inv_target_q32_32));

    const bool dwell_tau_hot = (cand.gov_dwell_tau_q32_32 > ctx.governor.dwell_tau_limit_q32_32);
    const bool dwell_inv_hot = (cand.gov_dwell_inv_q32_32 > ctx.governor.dwell_inv_limit_q32_32);

    // Governor severity: 0=none, 1=target clamp, 2=hard clamp/cooldown.
    int governor_severity = 0;
    if (tau_max_q32_32 > tau_target_q32_32 || inv_max_q32_32 > inv_target_q32_32 || dwell_tau_hot || dwell_inv_hot) governor_severity = 1;
    if (tau_max_q32_32 > tau_crit_q32_32 || inv_max_q32_32 > inv_cap_q32_32) governor_severity = 2;

    // -----------------------------------------------------------------
    // Carrier-collapse coherence model (ancillabit collapse -> unison updates)
    // -----------------------------------------------------------------
    // Anchors evolve on a slower reference cadence. A single fast carrier
    // excitation (ancilla) traverses phase paths that overlap many anchors,
    // creating temporary coherence windows. During coherence, we apply a
    // transitional constrained projection in unison relative to a global
    // carrier reference basis.
    //
    // "Temporal dilation" is emulated deterministically via amplitude/current
    // (here: aggregate ancilla delta_I relative to pulse_current_max), widening
    // the coherence bound without time-slicing.

    // Build a global carrier reference basis9 by collapsing the tick-start
    // anchor basis states. Deterministic and bounded.
    Basis9 carrier_ref_basis9{};
    if (!cand.anchors.empty()) {
        const Basis9 b0 = cand.anchors[0].basis9;
        for (int d = 0; d < kDims9; ++d) {
            if (d == 4) {
                __int128 sum = 0;
                for (size_t i = 0; i < cand.anchors.size(); ++i) {
                    const int64_t v = cand.anchors[i].basis9.d[d];
                    sum += (__int128)delta_turns(v, b0.d[d]);
                }
                const int64_t mean_delta = (int64_t)(sum / (__int128)cand.anchors.size());
                carrier_ref_basis9.d[d] = wrap_turns(b0.d[d] + mean_delta);
            } else {
                __int128 sum = 0;
                for (size_t i = 0; i < cand.anchors.size(); ++i) sum += (__int128)cand.anchors[i].basis9.d[d];
                carrier_ref_basis9.d[d] = (int64_t)(sum / (__int128)cand.anchors.size());
            }
        }
    }

    // Derive a deterministic dilation factor from aggregate ancilla delta_I.
    // dilation = 1 + mean(|delta_I|) / pulse_current_max.
    int64_t mean_abs_deltaI_q32_32 = 0;
    if (!cand.ancilla.empty() && ctx.pulse_current_max_mA_q32_32 > 0) {
        __int128 sum = 0;
        for (size_t i = 0; i < cand.ancilla.size(); ++i) {
            const int64_t di = cand.ancilla[i].delta_I_mA_q32_32;
            sum += (__int128)((di < 0) ? -di : di);
        }
        mean_abs_deltaI_q32_32 = (int64_t)(sum / (__int128)cand.ancilla.size());
    }
    const int64_t frac_q32_32 = (ctx.pulse_current_max_mA_q32_32 > 0)
        ? q32_32_div_i64_local(mean_abs_deltaI_q32_32, ctx.pulse_current_max_mA_q32_32)
        : 0;
    const int64_t dilation_q32_32 = (1LL << 32) + (frac_q32_32 > 0 ? frac_q32_32 : 0);
    const int64_t max_dtheta_eff_turns_q = (int64_t)(((__int128)ctx.max_dtheta_turns_q * (__int128)dilation_q32_32) >> 32);
    const int64_t epsilon_coh_q32_32 = epsilon_from_turn_bound_q32_32(max_dtheta_eff_turns_q);

    // Pre-compute step factors. Time dilation applies ONLY in transport.
    std::vector<int64_t> step_factor_q32_32;
    compute_time_dilation(step_factor_q32_32, cand, ctx);

    for (size_t i = 0; i < cand.anchors.size(); ++i) {
        Anchor& a = cand.anchors[i];
        a.tau_q = cand.canonical_tick;
        a.sync_basis9_from_core();

        // Omega.5: operator chaining is closed by a constrained projection Pi_G
        // relative to the reference state at tick start for this anchor.
        const Basis9 ref_basis9 = a.basis9;

        // Sample pulse observables at tau_delta and derive theta_anchor_k.
        const int32_t f_k = cur_f_code[i];
        const uint16_t a_k_raw = cur_a_code[i];
        const uint16_t v_k_raw = cur_v_code[i];
        const uint16_t i_k_raw = cur_i_code[i];

        // Deterministic linear sampling between last and current a_code.
        // a_sample = last + frac*(cur-last), frac in Q0.15.
        const int32_t da = (int32_t)a_k_raw - (int32_t)a.last_a_code;
        const int32_t a_sample_i32 = (int32_t)a.last_a_code + (int32_t)((da * (int32_t)ctx.tau_delta_q15) >> 15);
        const uint16_t a_sample = (a_sample_i32 < 0) ? 0u : (uint16_t)a_sample_i32;

        // Deterministic sampling for v/i carrier observables.
        const int32_t dv = (int32_t)v_k_raw - (int32_t)a.last_v_code;
        const int32_t v_sample_i32 = (int32_t)a.last_v_code + (int32_t)((dv * (int32_t)ctx.tau_delta_q15) >> 15);
        const uint16_t v_sample = (v_sample_i32 < 0) ? 0u : (uint16_t)v_sample_i32;

        const int32_t di = (int32_t)i_k_raw - (int32_t)a.last_i_code;
        const int32_t i_sample_i32 = (int32_t)a.last_i_code + (int32_t)((di * (int32_t)ctx.tau_delta_q15) >> 15);
        const uint16_t i_sample = (i_sample_i32 < 0) ? 0u : (uint16_t)i_sample_i32;

        // Apply governor by clamping effective amplitude (reduces harmonic fan-out).
        uint16_t a_eff = a_sample;
        if (governor_severity == 1) {
            // mild clamp: ~7/8
            a_eff = (uint16_t)(((uint32_t)a_eff * 7u) >> 3);
        } else if (governor_severity >= 2) {
            // hard clamp: 1/2
            a_eff = (uint16_t)(((uint32_t)a_eff) >> 1);
        }


        // Map sampled amplitude code into A_k in Q32.32.
        // A_k = (a_sample + 1) / A_MAX.
        const int64_t A_k_q32_32 = q32_32_div_i64_local(((int64_t)a_eff + 1) << 32, (int64_t)A_MAX);
        const int64_t ratio_q32_32 = q32_32_div_i64_local((A_k_q32_32 << 0), (ctx.A_ref_q32_32 ? ctx.A_ref_q32_32 : (1LL << 32)));
        const int64_t ln_ratio_q32_32 = ln_q32_32_local(ratio_q32_32);

        // theta_anchor_k = wrap(theta_ref + alpha_A * ln(A_k/A_ref))
        __int128 pA = (__int128)ctx.alpha_A_turns_q32_32 * (__int128)ln_ratio_q32_32;
        const int64_t dtheta_anchor_turns_q = (int64_t)(pA >> 32);
        const int64_t theta_anchor_k = wrap_turns(ctx.theta_ref_turns_q + dtheta_anchor_turns_q);

        // Anchor phase update MUST execute through the universal dispatcher template
        // against mutable ancilla state (Equations A.18).
        // Anchors do not directly commit mutable phase.
        ancilla_particle& an = cand.ancilla[i];
        int64_t theta_prime_turns_q = theta_anchor_k;

        // Temporary coherence window: anchor is coherent with the global carrier
        // reference if deviation energy <= epsilon_coh.
        bool coherent_window = false;
        if (!cand.anchors.empty()) {
            int64_t d_turns_q[9];
            for (int d = 0; d < 9; ++d) d_turns_q[d] = a.basis9.d[d] - carrier_ref_basis9.d[d];
            d_turns_q[4] = delta_turns(a.basis9.d[4], carrier_ref_basis9.d[4]);
            const int64_t e_dev_q32_32 = deviation_energy_q32_32(d_turns_q, ctx.carrier_g_q32_32, ctx, &an);
            coherent_window = (e_dev_q32_32 <= epsilon_coh_q32_32);
        }

        // Blueprint 3.5 / Omega.3: per-anchor harmonic signature.
        // This is used for trace/inspection only and can be compiled out.
#if EW_ALU_TRACE_ENABLE
        int64_t coord9_turns_q[kDims9];
        for (int d = 0; d < kDims9; ++d) coord9_turns_q[d] = a.basis9.d[d];
        const AnchorHarmonicSignatureQ63 fp = ew_build_anchor_signature(a.id, coord9_turns_q);
#endif

        // Blueprint C.4: object reference influence (anchor-permitted), expressed
        // as a bounded theta offset applied to the proposed phase.
        if (a.object_phase_seed_u64 != 0ULL && a.object_theta_scale_turns_q32_32 != 0) {
            const uint64_t seed_mod = (uint64_t)(a.object_phase_seed_u64 % (uint64_t)TURN_SCALE);
            const int64_t centered = (int64_t)seed_mod - (int64_t)(TURN_SCALE / 2);
            __int128 pp = (__int128)centered * (__int128)a.object_theta_scale_turns_q32_32;
            const int64_t dtheta_obj = (int64_t)(pp >> 32);
            theta_prime_turns_q = wrap_turns(theta_prime_turns_q + dtheta_obj);
        }

        // Convert current theta and proposed theta into q32.32 turns for ancilla dispatch.
        int64_t phi_q32_32 = turns_q_to_q32_32(a.theta_q);
        const int64_t phi_prime_q32_32 = turns_q_to_q32_32(theta_prime_turns_q);

        const int64_t max_current = (ctx.pulse_current_max_mA_q32_32 > 0) ? ctx.pulse_current_max_mA_q32_32 : 0;
        const int64_t target_current = (int64_t)(((__int128)max_current * (__int128)ctx.governor.target_frac_q15) >> 15);
        const int64_t eff_current_cap = (governor_severity >= 2) ? (target_current - (target_current >> 3)) : target_current; // extra 12.5% headroom reduction on hard clamp
        const int64_t abs_cur = (an.current_mA_q32_32 < 0 ? -an.current_mA_q32_32 : an.current_mA_q32_32);
        const int64_t remaining_headroom = (eff_current_cap > 0 && eff_current_cap > abs_cur) ? (eff_current_cap - abs_cur) : 0;

        // Commit phase proposal under ancilla constraints first.
        // Harmonic constraints must bind to the committed evolution, not the proposal.
        const int64_t phi_prev_q32_32 = phi_q32_32;

        (void)ancilla_apply_phi_prime(
            &an,
            &phi_q32_32,
            phi_prime_q32_32,
            phase_quantum_q32_32,
            k_phase_current_q32_32,
            remaining_headroom,
            ctx.gradient_headroom_mA_q32_32);

        const int64_t phi_committed_q32_32 = phi_q32_32;

        // Continuous environment + chemistry update (ancilla-driven).
        ew_update_env_and_reaction_continuous(&an, current_state, ctx, an.delta_I_mA_q32_32);

        // NOTE: harmonic constraint surfaces must bind to the committed value
        // that is actually stored into the next-state anchor representation.
        // The anchor stores theta in TURN_SCALE units, and accept_state derives
        // its enforcement values from that stored representation.

        // Substrate microprocessor trace: incorporate the anchor's harmonic identity.
        // Convert Q63 base_freq_code -> Q32.32 via right shift by 31.
#if EW_ALU_TRACE_ENABLE
        {
            const int64_t base_q32_32 = (int64_t)((uint64_t)fp.base_freq_code_q63 >> 31);
            const uint64_t cid_fp = ew_alu_carrier_id_u64_from_q32_32_pair(ctx, base_q32_32, phi_committed_q32_32);
            ew_alu_trace(&an, cid_fp);
        }

        // Substrate microprocessor rule: every dispatch is accompanied by a
        // carrier-wave collapse derived from the committed operands (Blueprint 3.x).
        {
            const uint64_t cid = ew_alu_carrier_id_u64_from_q32_32_pair(ctx, phi_prev_q32_32, phi_committed_q32_32);
            ew_alu_trace(&an, cid);
        }
#endif

        // Re-project committed phi back to TURN_SCALE for downstream operators.
        a.theta_q = wrap_turns(q32_32_to_turns_q_safe(phi_q32_32));
        a.basis9.d[4] = a.theta_q;

        // Delta encoding profile selection (Spec 3.6).
        // If inbound pulse provides a profile_id for this anchor, it overrides ctx default.
        const uint8_t profile_id = cur_profile[i];
        EwDeltaProfile prof;
        ew_get_delta_profile(profile_id, &prof);

        Basis9 proj = projected_for_local(ctx, &an, a, cand.boundary_scale_q32_32);
        int32_t f_code = a.spider_encode_9d(proj, prof.weights_q10, prof.denom_q);
        uint16_t a_code = a.amplitude_encode();

        if (!a.neighbors.empty()) {
            int64_t align_sum = 0;
            for (size_t k = 0; k < a.neighbors.size(); ++k) {
                uint32_t nid = a.neighbors[k];
                if (nid < cand.anchors.size()) align_sum += a.alignment_energy(cand.anchors[nid]);
            }
            int64_t align_avg = align_sum / (int64_t)a.neighbors.size();
            if (align_avg < (TURN_SCALE / 4)) a_code = (uint16_t)(a_code / 2);
        }

        const int64_t sf = step_factor_q32_32[i];

        // Spec 3.7/3.8: Harmonic mode mapping (deterministic internal expansion).
        // Use sampled amplitude code to select mode k and within-mode strength.
        const uint16_t bucket = prof.mode_bucket_size ? prof.mode_bucket_size : ctx.mode_bucket_size;
        uint16_t k_harm = 0;
        uint16_t strength_q15 = 0;
        a.harmonic_mode(a_eff, bucket, &k_harm, &strength_q15);
        if (k_harm < 1) k_harm = 1;
        if (k_harm > 16) k_harm = 16; // deterministic bounded work.

        const int32_t f_base = (f_code != 0) ? f_code : f_k;
        for (uint16_t n = 1; n <= k_harm; ++n) {
            const uint16_t w_q15 = (n == 1) ? strength_q15 : (uint16_t)(strength_q15 / n);
            const int32_t f_n = (int32_t)((int64_t)f_base * (int64_t)n);
            a.apply_frequency_weighted(f_n, sf, w_q15);
        }

	    // -----------------------------------------------------------------
	    // Blueprint 14.1.1: injection step semantics (closure-preserving)
	    // After transport (dtheta_base via apply_frequency_weighted), phase is
	    // modulated ONLY by deterministically quantized ratio deltas:
	    //   theta_{t+1} = wrap(theta_t + dtheta_base(t)
	    //                        + kappa_A * dlnAq_t
	    //                        + kappa_f * dlnFq_t)
	    // -----------------------------------------------------------------
	    const int64_t dlnA_q32_32 = ln_ratio_q32_32 - a.last_lnA_q32_32;
	    { const uint64_t cid = ew_alu_carrier_id_u64_from_q32_32_pair(ctx, ln_ratio_q32_32, a.last_lnA_q32_32); ew_alu_trace(&an, cid); }

	    // lnF uses magnitude only; sign remains encoded in the transport term.
	    const int32_t f_used = f_base;
	    const int64_t f_mag = (int64_t)((f_used < 0) ? -f_used : f_used) + 1;
	    const int64_t f_mag_q32_32 = q32_32_div_i64_local(f_mag << 32, (int64_t)F_SCALE);
	    const int64_t lnF_q32_32 = ln_q32_32_local(f_mag_q32_32);
	    const int64_t dlnF_q32_32 = lnF_q32_32 - a.last_lnF_q32_32;
	    { const uint64_t cid = ew_alu_carrier_id_u64_from_q32_32_pair(ctx, lnF_q32_32, a.last_lnF_q32_32); ew_alu_trace(&an, cid); }

	    // Deterministic quantization (required for stable rehydration).
        const int64_t q_step_q32_32 = (1LL << 32) / 4096;
        auto quant_q32_32 = [](int64_t x, int64_t step) -> int64_t {
            if (step <= 0) return x;
	        // Deterministic quantization: truncation toward zero.
	        const int64_t q = x / step;
            return q * step;
        };
        const int64_t dlnAq_q32_32 = quant_q32_32(dlnA_q32_32, q_step_q32_32);
        const int64_t dlnFq_q32_32 = quant_q32_32(dlnF_q32_32, q_step_q32_32);
	    { const uint64_t cid = ew_alu_carrier_id_u64_from_q32_32_pair(ctx, dlnAq_q32_32, dlnFq_q32_32); ew_alu_trace(&an, cid); }

	    // Coupling terms (kappa_A and kappa_f) applied after transport.
	    __int128 pcA = (__int128)ctx.kappa_lnA_turns_q32_32 * (__int128)dlnAq_q32_32;
	    __int128 pcF = (__int128)ctx.kappa_lnF_turns_q32_32 * (__int128)dlnFq_q32_32;
	    const int64_t dtheta_cplA_turns_q = (int64_t)(pcA >> 32);
	    const int64_t dtheta_cplF_turns_q = (int64_t)(pcF >> 32);
	    { const uint64_t cid = ew_alu_carrier_id_u64_from_turns_pair(ctx, dtheta_cplA_turns_q, dtheta_cplF_turns_q); ew_alu_trace(&an, cid); }
	    a.theta_q = wrap_turns(a.theta_q + dtheta_cplA_turns_q + dtheta_cplF_turns_q);
	    a.basis9.d[4] = a.theta_q;

        // Phase-density orientation (PAF) in TURN_SCALE domain.
        // PAF = clamp( (|dlnAq| + |dlnFq|) * (1/8 turn), 0, 1/8 turn )
        const int64_t abs_dlnAq = abs_i64(dlnAq_q32_32);
        const int64_t abs_dlnFq = abs_i64(dlnFq_q32_32);
        __int128 pp = (__int128)(abs_dlnAq + abs_dlnFq) * (__int128)(TURN_SCALE / 8);
        int64_t paf_turns_q = (int64_t)(pp >> 32);
        const int64_t paf_max = TURN_SCALE / 8;
        if (paf_turns_q < 0) paf_turns_q = 0;
        if (paf_turns_q > paf_max) paf_turns_q = paf_max;

        // Ring semantics: theta_start_{n+1} = wrap(theta_end_n + PAF_n)
        a.theta_end_turns_q = a.theta_q;
        a.paf_turns_q = paf_turns_q;
        a.theta_start_turns_q = wrap_turns(a.theta_end_turns_q + a.paf_turns_q);
        a.apply_amplitude(a_code);
        a.decay(1);
        cand.reservoir += a.leak_mass(1000);

        // Coherent time delta output (dt_star), computed only if coherence gating passes.
        if (a.chi_q >= ctx.coherence_cmin_turns_q) {
            const int64_t dphi_coh_turns_q = delta_turns(a.theta_q, theta_anchor_k);

            // rho_phi is a bounded measure from curvature (TURN_SCALE units).
            ew_alu_trace_turns_pair(ctx, &an, abs_i64(a.curvature_q), ctx.td_params.norm_turns_q);
            const int64_t rho_phi_q32_32 = clamp_q32_32_local(q32_32_div_i64_local(abs_i64(a.curvature_q), ctx.td_params.norm_turns_q), 0, (1LL << 32));
            __int128 pr = (__int128)ctx.kappa_rho_q32_32 * (__int128)rho_phi_q32_32;
            const int64_t omega_scale_q32_32 = (1LL << 32) + (int64_t)(pr >> 32);
            const int64_t omega_eff_q32_32 = ew_alu_mul_q32_32(ctx, &an, ctx.omega0_turns_per_sec_q32_32, omega_scale_q32_32);

            // dt_star = dphi_coh / omega_eff.
            const int64_t dphi_q32_32 = q32_32_div_i64_local(dphi_coh_turns_q << 32, TURN_SCALE);
            ew_alu_trace_turns_pair(ctx, &an, dphi_coh_turns_q, TURN_SCALE);
            const int64_t dt_star_q32_32 = ew_alu_div_q32_32(ctx, &an, dphi_q32_32, omega_eff_q32_32 ? omega_eff_q32_32 : (1LL << 32));
            a.dt_star_seconds_q32_32 = dt_star_q32_32;
        } else {
            a.dt_star_seconds_q32_32 = 0;
        }

        // Omega.4: apply constrained projection Pi_G to the anchor state.
        // Canonical closure prevents unconstrained drift.
        //
        // Carrier-collapse model: when the carrier excitation traverses the
        // same phase paths as multiple anchors (temporary coherence window),
        // apply a transitional projection in unison relative to the global
        // carrier reference basis, using the widened coherence bound.
        if (coherent_window) {
            int64_t d_turns_q[9];
            for (int d = 0; d < 9; ++d) d_turns_q[d] = a.basis9.d[d] - carrier_ref_basis9.d[d];
            d_turns_q[4] = delta_turns(a.basis9.d[4], carrier_ref_basis9.d[4]);
            const int64_t e_dev_q32_32 = deviation_energy_q32_32(d_turns_q, ctx.carrier_g_q32_32, ctx, &an);
            if (e_dev_q32_32 > 0 && e_dev_q32_32 > epsilon_coh_q32_32) {
                const int64_t ratio_q32_32 = ew_alu_div_q32_32(ctx, &an, epsilon_coh_q32_32, e_dev_q32_32);
                const int64_t scale_q32_32 = sqrt_q32_32(ratio_q32_32);
                ew_alu_trace(&an, ew_alu_carrier_id_u64_from_q32_32_pair(ctx, ratio_q32_32, scale_q32_32));
                for (int d = 0; d < 9; ++d) {
                    __int128 dq = (__int128)d_turns_q[d] << 32;
                    dq /= (__int128)TURN_SCALE;
                    const int64_t dq_q32_32 = clamp_i64_from_i128_local(dq);
                    const int64_t dq_scaled_q32_32 = ew_alu_mul_q32_32(ctx, &an, dq_q32_32, scale_q32_32);
                    __int128 corr_turns = (__int128)dq_scaled_q32_32 * (__int128)TURN_SCALE;
                    corr_turns >>= 32;
                    a.basis9.d[d] = carrier_ref_basis9.d[d] + (int64_t)corr_turns;
                }
                a.basis9.d[4] = wrap_turns(a.basis9.d[4]);
                a.tau_turns_q = a.basis9.d[3];
                a.theta_q = a.basis9.d[4];
                a.chi_q = a.basis9.d[5];
                if (a.chi_q < 0) a.chi_q = 0;
                a.curvature_q = a.basis9.d[6];
                a.doppler_q = a.basis9.d[7];
                a.m_q = a.basis9.d[8];
                if (a.m_q < 0) a.m_q = 0;
            }
        } else {
            apply_pi_g_to_anchor(a, ref_basis9, ctx, &an);
        }

        // -----------------------------------------------------------------
        // Harmonic constraint (carrier id derived once per tick)
        // -----------------------------------------------------------------
        // Bind to the FINAL stored representation (post Pi_G) that accept_state
        // will also use for enforcement.
        {
            const int64_t phi_stored_q32_32 = turns_q_to_q32_32(a.theta_q);
            const uint64_t tick_cid_u64 = ew_alu_carrier_id_u64_from_q32_32_pair(ctx, phi_prev_q32_32, phi_stored_q32_32);
            const EigenWare::EwId9 tick_cid9 = EigenWare::ew_id9_from_u64(tick_cid_u64);
            an.tick_carrier_id9 = tick_cid9;

            int64_t coord9_turns_q[kDims9];
            for (int d = 0; d < kDims9; ++d) coord9_turns_q[d] = a.basis9.d[d];

            an.last_artifact_id_u32 = EW_ARTIFACT_ID_ANCILLA_TICK_DISPATCH;
            an.last_artifact_id9 = ew_id9_for_artifact(
                a.id, coord9_turns_q, an.last_artifact_id_u32, tick_cid9);

            // Multi-value surface: slot0 theta, slot1 curvature, slot2 doppler.
            an.value_slot_count_u32 = 3u;
            an.last_value_tag_u32 = EW_VALUE_TAG_PHASE_THETA_Q32_32;
            an.last_value_id9 = ew_id9_for_value(tick_cid9, phi_stored_q32_32, an.last_value_tag_u32);
            an.value_tag_slots_u32[0] = an.last_value_tag_u32;
            an.value_id9_slots[0] = an.last_value_id9;

            const int64_t curv_q32_32 = turns_q_to_q32_32(a.curvature_q);
            const int64_t dop_q32_32  = turns_q_to_q32_32(a.doppler_q);
            an.value_tag_slots_u32[1] = EW_VALUE_TAG_CURVATURE_Q32_32;
            an.value_id9_slots[1] = ew_id9_for_value(tick_cid9, curv_q32_32, EW_VALUE_TAG_CURVATURE_Q32_32);
            an.value_tag_slots_u32[2] = EW_VALUE_TAG_DOPPLER_Q32_32;
            an.value_id9_slots[2] = ew_id9_for_value(tick_cid9, dop_q32_32, EW_VALUE_TAG_DOPPLER_Q32_32);
        }

        // Persist pulse observables for the next tau_delta sample.
        a.last_a_code = a_k_raw;
        a.last_v_code = v_k_raw;
        a.last_i_code = i_k_raw;
        a.last_f_code = f_k;
        a.last_lnA_q32_32 = ln_ratio_q32_32;
        a.last_lnF_q32_32 = lnF_q32_32;
        a.last_theta_q = a.theta_q;
    }

    // -----------------------------------------------------------------
    // Blueprint 14.3: lane density scaling and correction update.
    // Lanes observe the derived drift channels produced by anchor evolution.
    // -----------------------------------------------------------------
    EwLanePolicy lane_pol;
    ew_update_qubit_lanes(cand.lanes, cand.canonical_tick, cand.anchors, inputs.inbound, lane_pol);

    return cand;
}

EwLedger compute_ledger(const EwState& state) {
    EwLedger L;
    L.reservoir_q = state.reservoir;

    int64_t mass_sum = 0;
    __int128 energy_sum = 0;
    __int128 mom_sum = 0;

    for (size_t i = 0; i < state.anchors.size(); ++i) {
        const Anchor& a = state.anchors[i];
        mass_sum += a.m_q;

        // Deterministic pseudo-energy: sum of squared derived channels.
        // Uses internal integer units; bounded via downshift.
        // Anchor does not store a dedicated flux channel in this expo prototype;
        // use theta_q magnitude as a deterministic phase-transport surrogate.
        const __int128 th2   = (__int128)a.theta_q * (__int128)a.theta_q;
        const __int128 curv2 = (__int128)a.curvature_q * (__int128)a.curvature_q;
        const __int128 dop2  = (__int128)a.doppler_q * (__int128)a.doppler_q;
        energy_sum += (th2 + curv2 + dop2) >> 20; // downshift to prevent overflow.

        // Deterministic pseudo-momentum: |v| * m, where v uses the spatial basis components.
        const int64_t vx = a.basis9.d[0];
        const int64_t vy = a.basis9.d[1];
        const int64_t vz = a.basis9.d[2];
        const __int128 vmag = (__int128)abs_i64(vx) + (__int128)abs_i64(vy) + (__int128)abs_i64(vz);
        mom_sum += vmag * (__int128)a.m_q;
    }

    L.total_mass_q = mass_sum;
    L.total_mass_plus_res_q = mass_sum + L.reservoir_q;
    L.total_energy_q = clamp_i64_from_i128(energy_sum);
    L.total_momentum_q = clamp_i64_from_i128(mom_sum >> 16);
    return L;
}

EwLedgerDelta compute_ledger_delta(const EwState& current_state, const EwState& candidate_next_state, const EwCtx&) {
    EwLedgerDelta d;
    d.reservoir_delta = candidate_next_state.reservoir - current_state.reservoir;
    d.total_mass_delta = total_mass_q(candidate_next_state) - total_mass_q(current_state);
    return d;
}

bool accept_state(const EwState& current_state, const EwState& candidate_next_state, const EwLedgerDelta& ledger_delta, const EwCtx& ctx) {
    // Identity: anchor count must remain stable.
    if (candidate_next_state.anchors.size() != current_state.anchors.size()) return false;

    // Identity: anchor ids must remain stable and in range.
    for (size_t i = 0; i < candidate_next_state.anchors.size(); ++i) {
        if (candidate_next_state.anchors[i].id != current_state.anchors[i].id) return false;
    }

    // Blueprint 14.3: lane substrate bounds (density must remain bounded).
    if (candidate_next_state.lanes.size() < 1) return false;
    if (candidate_next_state.lanes.size() > 64) return false;

    // Conservation: total mass must remain constant.
    if (ledger_delta.total_mass_delta != 0) return false;

    // Harmonic constraint: ancilla count must track anchors (Directive 115).
    if (candidate_next_state.ancilla.size() != candidate_next_state.anchors.size()) return false;

    // -----------------------------------------------------------------
    // 14.2 Closed-system / causality bounds
    // -----------------------------------------------------------------
    // Injection is treated as a constraint selector. These checks prevent
    // committing states that imply unconstrained phase jumps or unbounded
    // inferred time offsets.
    if (candidate_next_state.reservoir < 0) return false;

    for (size_t i = 0; i < candidate_next_state.anchors.size(); ++i) {
        const Anchor& a_prev = current_state.anchors[i];
        const Anchor& a_next = candidate_next_state.anchors[i];

        // -----------------------------------------------------------------
        // Harmonic constraint enforcement: operation artifact frequency
        // -----------------------------------------------------------------
        // Each anchor evolution tick must be encoded as an inertial-frequency
        // artifact constrained by the per-tick carrier id.
        {
            const ancilla_particle& an = candidate_next_state.ancilla[i];

            const int64_t phi_prev_q32_32 = turns_q_to_q32_32(a_prev.theta_q);
            const int64_t phi_next_q32_32 = turns_q_to_q32_32(a_next.theta_q);
            const uint64_t expect_cid_u64 = ew_alu_carrier_id_u64_from_q32_32_pair(ctx, phi_prev_q32_32, phi_next_q32_32);
            const EigenWare::EwId9 expect_cid9 = EigenWare::ew_id9_from_u64(expect_cid_u64);
            if (an.tick_carrier_id9 != expect_cid9) return false;

            int64_t coord9_turns_q[kDims9];
            for (int d = 0; d < kDims9; ++d) coord9_turns_q[d] = a_prev.basis9.d[d];
            const int64_t opt_vals_q32_32[3] = {
                turns_q_to_q32_32(a_next.curvature_q),
                turns_q_to_q32_32(a_next.doppler_q),
                0
            };
            const uint32_t opt_tags_u32[3] = {
                EW_VALUE_TAG_CURVATURE_Q32_32,
                EW_VALUE_TAG_DOPPLER_Q32_32,
                0u
            };

            ancilla_particle tmp = an;
            if (!ew_harmonic_constraints_ok_multi(&tmp,
                                                 a_prev.id,
                                                 coord9_turns_q,
                                                 EW_ARTIFACT_ID_ANCILLA_TICK_DISPATCH,
                                                 phi_next_q32_32,
                                                 EW_VALUE_TAG_PHASE_THETA_Q32_32,
                                                 opt_vals_q32_32,
                                                 opt_tags_u32)) {
                return false;
            }
        }

        const int64_t dtheta = delta_turns(a_next.theta_q, a_prev.theta_q);
        if (abs_i64(dtheta) > ctx.max_dtheta_turns_q) return false;

        if (abs_i64(a_next.paf_turns_q) > ctx.max_paf_turns_q) return false;

        if (abs_i64(a_next.last_lnA_q32_32) > ctx.max_abs_ln_q32_32) return false;
        if (abs_i64(a_next.last_lnF_q32_32) > ctx.max_abs_ln_q32_32) return false;

        // dt_star is output-only; keep it bounded to prevent non-physical
        // projection artifacts from being committed.
        if (abs_i64(a_next.dt_star_seconds_q32_32) > ctx.max_abs_dt_star_seconds_q32_32) return false;
    }

    return true;
}

void commit_state(EwState& current_state, const EwState& next_state) {
    current_state = next_state;
}

EwState make_sink_state(const EwState& current_state, const EwCtx&) {
    EwState sink = current_state;

    // Deterministic sink behavior: non-projecting dark state.
    // Keep mass ledger stable; collapse coherence and freeze phase transport.
    for (size_t i = 0; i < sink.anchors.size(); ++i) {
        Anchor& a = sink.anchors[i];
        // Blueprint dark sink rule: if coherence low 8 bits are zero, accumulate bounded dark mass.
        const uint64_t coh8 = (uint64_t)(abs_i64(a.chi_q)) & 0xFFu;
        if (coh8 == 0u) {
            const uint64_t add_q63 = (uint64_t)(1ULL << 63) / 1024ULL;
            const uint64_t prev = sink.dark_mass_q63_u64;
            const uint64_t next = prev + add_q63;
            sink.dark_mass_q63_u64 = (next < prev) ? UINT64_MAX : next;
        }

        a.chi_q = 0;
        // Non-projecting dark excitation still contributes curvature deterministically.
        // Map a bounded slice of dark_mass into TURN_SCALE-domain curvature.
        const int64_t curv_add = (TURN_SCALE / 1024);
        a.curvature_q = sat_add_i64(a.curvature_q, curv_add);
        a.doppler_q = 0;
        a.sync_basis9_from_core();
    }
    return sink;
}

int reality_label(int64_t nexus_turns_q, const EwCtx& ctx) {
    // Deterministic discretization of the nexus coordinate into a small number
    // of ledger labels. This does not perform any ledger switching; it is a
    // classification helper for accept_state / monitoring.
    // Bucket size: 1/64 of a full turn.
    const int64_t bucket = (ctx.mode_bucket_size > 0) ? (TURN_SCALE / (int64_t)ctx.mode_bucket_size) : 0;
    if (bucket <= 0) return 0;
    int64_t v = nexus_turns_q;
    // Wrap into [0, TURN_SCALE).
    v = wrap_turns(v);
    return (int)(v / bucket);
}

bool is_reality_shift(int64_t prev_nexus_turns_q, int64_t next_nexus_turns_q, const EwCtx& ctx) {
    return reality_label(prev_nexus_turns_q, ctx) != reality_label(next_nexus_turns_q, ctx);
}

// -----------------------------------------------------------------------------
//  OMRO
// -----------------------------------------------------------------------------

bool object_store_upsert(EwState& state, const EwObjectEntry& entry) {
    return state.object_store.upsert(entry);
}

bool object_import_request(EwState& state,
                           uint32_t target_anchor_id,
                           uint64_t object_id_u64,
                           int64_t energy_budget_q32_32,
                           const EwCtx& /*ctx*/,
                           uint32_t* out_reject_code) {
    if (out_reject_code) *out_reject_code = 0;
    if (target_anchor_id >= state.anchors.size()) {
        if (out_reject_code) *out_reject_code = 1;
        return false;
    }

    const EwObjectEntry* e = state.object_store.find(object_id_u64);
    if (!e) {
        if (out_reject_code) *out_reject_code = 1;
        return false;
    }

    // Genesis synthesis rule: objects must be voxelized before import is valid.
    // Reject code 4 = synthesis_missing.
    if (e->voxel_format_u32 != 1 || e->voxel_grid_x_u32 == 0 || e->voxel_grid_y_u32 == 0 || e->voxel_grid_z_u32 == 0) {
        if (out_reject_code) *out_reject_code = 4;
        return false;
    }

    // Energy debit must be anchor-defined; in this contract harness the
    // required debit is the object entry's mass_or_cost_q32_32.
    const int64_t required_q32_32 = e->mass_or_cost_q32_32;
    if (energy_budget_q32_32 < required_q32_32) {
        if (out_reject_code) *out_reject_code = 2;
        return false;
    }
    if (state.reservoir < required_q32_32) {
        if (out_reject_code) *out_reject_code = 3;
        return false;
    }

    state.reservoir -= required_q32_32;
    Anchor& a = state.anchors[target_anchor_id];
    a.object_id_u64 = object_id_u64;
    a.object_phase_seed_u64 = e->phase_seed_u64;
    return true;
}

bool object_synthesize_voxelize(EwState& state,
                                uint64_t object_id_u64,
                                uint32_t grid_x_u32,
                                uint32_t grid_y_u32,
                                uint32_t grid_z_u32,
                                const EwCtx& /*ctx*/,
                                uint32_t* out_reject_code) {
    if (out_reject_code) *out_reject_code = 0;
    const EwObjectEntry* e = state.object_store.find(object_id_u64);
    if (!e) {
        if (out_reject_code) *out_reject_code = 1;
        return false;
    }
    if (grid_x_u32 == 0 || grid_y_u32 == 0 || grid_z_u32 == 0) {
        if (out_reject_code) *out_reject_code = 2;
        return false;
    }

    // Genesis synthesis: import -> EWM1 mesh -> voxelize.
    // The voxel volume is derived from the object's associated mesh file.
    const std::string mesh_path = std::string("AssetSubstrate/Assets/Imported/") + e->label_utf8 + ".ewmesh";
    genesis::EwMeshV1 mesh;
    if (!genesis::ewmesh_read_v1(mesh_path, mesh)) {
        if (out_reject_code) *out_reject_code = 5; // missing_mesh
        return false;
    }

    std::vector<uint8_t> occ;
    if (!genesis::ewmesh_voxelize_occupancy_u8(mesh, state.materials_calib_done, grid_x_u32, grid_y_u32, grid_z_u32, occ)) {
        if (out_reject_code) *out_reject_code = 6; // voxelize_failed
        return false;
    }

    if (!state.object_store.upsert_voxel_volume_occupancy_u8(object_id_u64, grid_x_u32, grid_y_u32, grid_z_u32, occ.data(), occ.size())) {
        if (out_reject_code) *out_reject_code = 2;
        return false;
    }

    // Bake UV atlas variables into UV space (RGBA8).
    // Fixed atlas size is deterministic and sufficient for export workflows.
    const uint32_t atlas_w_u32 = 256;
    const uint32_t atlas_h_u32 = 256;
    std::vector<uint8_t> atlas_rgba8;
    if (!genesis::ge_bake_uv_atlas_rgba8(mesh,
                                         object_id_u64,
                                         grid_x_u32,
                                         grid_y_u32,
                                         grid_z_u32,
                                         occ.data(),
                                         atlas_w_u32,
                                         atlas_h_u32,
                                         atlas_rgba8)) {
        if (out_reject_code) *out_reject_code = 7; // uv_atlas_failed
        return false;
    }
    if (!state.object_store.upsert_uv_atlas_rgba8(object_id_u64, atlas_w_u32, atlas_h_u32, atlas_rgba8.data(), atlas_rgba8.size())) {
        if (out_reject_code) *out_reject_code = 7;
        return false;
    }

    // Also update the entry header so the voxel volume is discoverable.
    EwObjectEntry e2 = *e;
    e2.voxel_grid_x_u32 = grid_x_u32;
    e2.voxel_grid_y_u32 = grid_y_u32;
    e2.voxel_grid_z_u32 = grid_z_u32;
    e2.voxel_format_u32 = 1;
    e2.voxel_blob_id_u64 = object_id_u64;

    e2.uv_atlas_w_u32 = atlas_w_u32;
    e2.uv_atlas_h_u32 = atlas_h_u32;
    e2.uv_atlas_format_u32 = 1;
    e2.uv_atlas_blob_id_u64 = object_id_u64;

    // Initialize object-local lattice state (phi_q15_s16) deterministically.
    // phi = centered occupancy mapped to signed Q15.
    {
        const size_t nvox = occ.size();
        std::vector<int16_t> phi;
        phi.resize(nvox);
        for (size_t i = 0; i < nvox; ++i) {
            const uint8_t o = occ[i];
            const int32_t q = (int32_t)((uint32_t)o * 32767u) / 255u;
            phi[i] = (int16_t)(q - 16384);
        }
        (void)state.object_store.upsert_local_phi_q15_s16(object_id_u64, grid_x_u32, grid_y_u32, grid_z_u32,
                                                          phi.data(), phi.size() * sizeof(int16_t));
        e2.local_grid_x_u32 = grid_x_u32;
        e2.local_grid_y_u32 = grid_y_u32;
        e2.local_grid_z_u32 = grid_z_u32;
        e2.local_format_u32 = 1u;
        e2.local_blob_id_u64 = object_id_u64;
    }
    state.object_store.upsert(e2);
    return true;
}

// -----------------------------------------------------------------------------
//  Operator name surface (validator support)
// -----------------------------------------------------------------------------
EwOpNameList ew_operator_name_list() {
    static const char* const kNames[] = {
        "evolve_state",
        "compute_ledger",
        "compute_ledger_delta",
        "accept_state",
        "commit_state",
        "make_sink_state",
        "reality_label",
        "is_reality_shift",
        "object_store_upsert",
        "object_import_request",
        "object_synthesize_voxelize",
    };
    EwOpNameList r;
    r.names = kNames;
    r.count = (uint32_t)(sizeof(kNames) / sizeof(kNames[0]));
    return r;
}
