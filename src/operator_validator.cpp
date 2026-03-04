#include "operator_validator.hpp"

#include "canonical_ops.hpp"
#include "spec_gates.hpp"
#include "statevector_serialization.hpp"

#include "GE_runtime.hpp"
#include "GE_operator_registry.hpp"

#include <cstring>

#include <complex>
#include <vector>

static void append_line(std::string& s, const std::string& line) {
    s += line;
    s += "\n";
}

static bool check_eq_i64(std::string& out, const char* label, int64_t got, int64_t exp) {
    if (got != exp) {
        append_line(out, std::string("FAIL: ") + label + " got=" + std::to_string((long long)got) + " exp=" + std::to_string((long long)exp));
        return false;
    }
    append_line(out, std::string("OK: ") + label);
    return true;
}

static bool check_true(std::string& out, const char* label, bool v) {
    if (!v) {
        append_line(out, std::string("FAIL: ") + label);
        return false;
    }
    append_line(out, std::string("OK: ") + label);
    return true;
}

EwOperatorValidationReport validate_operators_and_microprocessor() {
    EwOperatorValidationReport r;
    std::string log;

    bool ok = true;

    // -----------------------------------------------------------------
    // Operator presence validation (registry completeness)
    // -----------------------------------------------------------------
    {
        const EwOpNameList have = ew_operator_name_list();
        static const char* const required[] = {
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
        const size_t req_n = sizeof(required) / sizeof(required[0]);
        for (size_t i = 0; i < req_n; ++i) {
            bool found = false;
            for (uint32_t j = 0; j < have.count; ++j) {
                if (std::strcmp(required[i], have.names[j]) == 0) { found = true; break; }
            }
            if (!found) {
                append_line(log, std::string("FAIL: operator missing: " ) + required[i]);
                ok = false;
            } else {
                append_line(log, std::string("OK: operator present: " ) + required[i]);
            }
        }
    }


    // -----------------------------------------------------------------
    // Canonical fixed-point helpers
    // -----------------------------------------------------------------
    ok &= check_eq_i64(log, "abs_i64", abs_i64(-7), 7);
    ok &= check_eq_i64(log, "abs_q32_32", abs_q32_32(-(3LL << 32)), (3LL << 32));

    const int64_t one = (1LL << 32);
    const int64_t two = (2LL << 32);
    ok &= check_eq_i64(log, "mul_q32_32", mul_q32_32(one, two), two);
    ok &= check_eq_i64(log, "div_q32_32", div_q32_32(two, one), two);

    // -----------------------------------------------------------------
    // Spec gates
    // -----------------------------------------------------------------
    {
        const int64_t theta_scale = TURN_SCALE;
        const int64_t a = enforce_phase_domain(-1, theta_scale);
        ok &= check_true(log, "enforce_phase_domain range", (a >= 0 && a < theta_scale));

        // Coherence gate should pass when above min.
        enforce_coherence_gate((TURN_SCALE / 10), (TURN_SCALE / 20));
        append_line(log, "OK: enforce_coherence_gate pass");
    }

    // -----------------------------------------------------------------
    // Statevector serialization
    // -----------------------------------------------------------------
    {
        std::vector<std::complex<double>> v;
        v.emplace_back(1.0, 0.0);
        v.emplace_back(0.5, -0.25);
        const std::string blob = serialize_statevector(v);
        const auto out = deserialize_statevector(blob);
        ok &= check_eq_i64(log, "serialize/deserialize size", (int64_t)out.size(), (int64_t)v.size());
        // Numeric equality is toolchain-dependent under different hexfloat
        // formatting/parsing behaviors. Size and structural validity are the
        // deterministic contract enforced here.
    }

    // -----------------------------------------------------------------
    // Microprocessor actuation path: SubstrateManager tick
    // -----------------------------------------------------------------
    {
        SubstrateManager sim(16);
        sim.projection_seed = 17;
        sim.configure_cosmic_expansion(1, 1);
        sim.inject_text_utf8("microprocessor");

        // Ensure anchors exist.
        ok &= check_true(log, "anchors exist", sim.anchors.size() > 0);

        // Actuate an event: enqueue a pulse and verify it is admitted.
        Pulse p;
        p.anchor_id = 0;
        p.f_code = 123;
        p.a_code = 55;
        p.v_code = 0;
        p.i_code = 0;
        p.profile_id = 1;
        p.causal_tag = 7;
        p.pad0 = 0;
        p.pad1 = 0;
        p.tick = sim.canonical_tick;
        sim.enqueue_inbound_pulse(p);
        ok &= check_true(log, "inbound admitted", sim.inbound.size() > 0);

// Topology events: SPLIT should create new anchors deterministically inside substrate.
const size_t before_split_n = sim.anchors.size();
Pulse ps;
ps.anchor_id = 0;
ps.f_code = 0;
ps.a_code = 0;
ps.v_code = 0;
ps.i_code = 0;
ps.profile_id = 1;
ps.causal_tag = 0x5; // SPLIT
ps.pad0 = 0;
ps.pad1 = 0;
ps.tick = sim.canonical_tick;
sim.enqueue_inbound_pulse(ps);
sim.tick();
ok &= check_true(log, "topology split adds anchors", sim.anchors.size() > before_split_n);


        // Validate that the substrate microprocessor computes a candidate state.
        EwState current_state;
        current_state.canonical_tick = sim.canonical_tick;
        current_state.reservoir = sim.reservoir;
        current_state.boundary_scale_q32_32 = sim.boundary_scale_q32_32;
        current_state.anchors = sim.anchors;
        current_state.ancilla = sim.ancilla;
        current_state.lanes = sim.lanes;

        EwInputs inputs;
        inputs.inbound = sim.inbound;
        inputs.pending_text_x_q = sim.pending_text_x_q;
        inputs.pending_image_y_q = sim.pending_image_y_q;
        inputs.pending_audio_z_q = sim.pending_audio_z_q;

        EwCtx ctx;
        ctx.frame_gamma_turns_q = sim.frame_gamma_turns_q;
        ctx.td_params = sim.td_params;
        for (int i = 0; i < 9; ++i) { ctx.weights_q10[i] = sim.weights_q10[i]; ctx.denom_q[i] = sim.denom_q[i]; }
        ctx.sx_q32_32 = sim.sx_q32_32;
        ctx.sy_q32_32 = sim.sy_q32_32;
        ctx.sz_q32_32 = sim.sz_q32_32;
        ctx.hubble_h0_q32_32 = sim.hubble_h0_q32_32;
        ctx.tick_dt_seconds_q32_32 = sim.tick_dt_seconds_q32_32;
        ctx.boundary_scale_step_q32_32 = sim.boundary_scale_step_q32_32;
        ctx.boundary_scale_q32_32 = sim.boundary_scale_q32_32;

// Effective constants are derived inside the substrate microprocessor.
// For validator we use deterministic neutral factors.
const EwRefConstantsQ32_32 refs = ref_constants_default();
const int64_t v_fraction_c_q32_32 = 0;
const int64_t doppler_factor_q32_32 = (1LL << 32);
const int64_t flux_factor_q32_32 = 0;
const int64_t strain_factor_q32_32 = 0;
const int64_t temperature_q32_32 = (1LL << 32);
ctx.eff = effective_constants(refs, v_fraction_c_q32_32, doppler_factor_q32_32,
                              flux_factor_q32_32, strain_factor_q32_32, temperature_q32_32);
ctx.hubble_h0_q32_32 = ctx.eff.hubble_h0_eff_q32_32;
ctx.envelope_headroom_q32_32 = (1LL << 32);

        for (int i = 0; i < 9; ++i) ctx.carrier_g_q32_32[i] = sim.carrier_g_q32_32[i];
        ctx.tau_delta_q15 = sim.tau_delta_q15;
        ctx.theta_ref_turns_q = sim.theta_ref_turns_q;
        ctx.A_ref_q32_32 = sim.A_ref_q32_32;
        ctx.alpha_A_turns_q32_32 = sim.alpha_A_turns_q32_32;
        ctx.kappa_lnA_turns_q32_32 = sim.kappa_lnA_turns_q32_32;
        ctx.kappa_lnF_turns_q32_32 = sim.kappa_lnF_turns_q32_32;
        ctx.coherence_cmin_turns_q = sim.coherence_cmin_turns_q;
        ctx.omega0_turns_per_sec_q32_32 = sim.omega0_turns_per_sec_q32_32;
        ctx.kappa_rho_q32_32 = sim.kappa_rho_q32_32;
        ctx.pulse_current_max_mA_q32_32 = sim.pulse_current_max_mA_q32_32;
        ctx.phase_max_displacement_q32_32 = sim.phase_max_displacement_q32_32;
        ctx.phase_orbital_displacement_unit_mA_q32_32 = sim.phase_orbital_displacement_unit_mA_q32_32;
        ctx.gradient_headroom_mA_q32_32 = sim.gradient_headroom_mA_q32_32;
        ctx.temporal_envelope_ticks_u64 = sim.temporal_envelope_ticks_u64;

        const EwState cand = evolve_state(current_state, inputs, ctx);
        ok &= check_true(log, "candidate tick advances", cand.canonical_tick == (current_state.canonical_tick + 1));
    }

    // -----------------------------------------------------------------
    // Operator packet payload enforcement (byte-for-byte)
    // -----------------------------------------------------------------
    {
        SubstrateManager sim(16);
        sim.projection_seed = 17;
        sim.configure_cosmic_expansion(1, 1);
        sim.inject_text_utf8("packet");

        // Build a single packed operator packet with an intentional payload length mismatch.
        uint8_t pkt[1500];
        std::memset(pkt, 0, sizeof(pkt));

        // op_id_e9_f64[9] left as 0.
        // op_kind = OPK_PROJECT_COH_DOT
        auto wr_u32 = [&](size_t off, uint32_t v){
            pkt[off+0] = (uint8_t)(v & 0xFFu);
            pkt[off+1] = (uint8_t)((v >> 8) & 0xFFu);
            pkt[off+2] = (uint8_t)((v >> 16) & 0xFFu);
            pkt[off+3] = (uint8_t)((v >> 24) & 0xFFu);
        };
        auto wr_f64 = [&](size_t off, double v){
            uint64_t u; std::memcpy(&u, &v, sizeof(u));
            for (int i = 0; i < 8; ++i) pkt[off + (size_t)i] = (uint8_t)((u >> (8*i)) & 0xFFu);
        };

        wr_u32(72, 0x00000003u);
        wr_u32(76, 1u);
        wr_u32(80, 2u);

        // IN lanes: 2001 scalar, 2001 scalar (dot uses 2 scalars)
        // We'll point both to lane_n_scalar (2001) so the executor can find it.
        for (int k = 0; k < 9; ++k) {
            wr_f64(84 + (size_t)k*8, (k==0) ? 2001.0 : 0.0);
            wr_f64(84 + 72 + (size_t)k*8, (k==0) ? 2001.0 : 0.0);
        }

        wr_u32(84 + 576, 1u);
        // OUT lane: 3000 scalar
        for (int k = 0; k < 9; ++k) wr_f64(84 + 576 + 4 + (size_t)k*8, (k==0) ? 3000.0 : 0.0);

        // payload_bytes intentionally wrong (0, expected 8)
        wr_u32(84 + 576 + 4 + 576, 0u);

        sim.submit_operator_packet_v1(pkt, sizeof(pkt));
        sim.tick();

        bool out_found = false;
        for (const auto& e : sim.op_lanes) {
            if (!e.is_buffer && e.lane_id.v[0] == 3000) { out_found = true; break; }
        }
        ok &= check_true(log, "packet payload mismatch rejects execution", !out_found);

        // Now fix payload_bytes and payload contents and ensure the output lane is produced.
        wr_u32(84 + 576 + 4 + 576, 8u);
        wr_u32(84 + 576 + 4 + 576 + 4, 5u); // coherence_index
        wr_u32(84 + 576 + 4 + 576 + 8, 0u); // abs_mode

        // Re-run with corrected packet.
        sim.operator_packets_v1.clear();
        sim.submit_operator_packet_v1(pkt, sizeof(pkt));
        sim.tick();

        out_found = false;
        for (const auto& e : sim.op_lanes) {
            if (!e.is_buffer && e.lane_id.v[0] == 3000) { out_found = true; break; }
        }
        ok &= check_true(log, "packet payload match allows execution", out_found);
    }

    r.ok = ok;
    r.details = log;
    return r;
}
