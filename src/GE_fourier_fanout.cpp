#include "GE_fourier_fanout.hpp"
#include "GE_voxel_coupling_anchor.hpp"

#include "GE_runtime.hpp"
#include "ew_cordic.hpp"

#include <algorithm>
#include <vector>

// -----------------------------------------------------------------------------
// Derived probe helpers (read-only, for viewport/debug only)
// -----------------------------------------------------------------------------

static inline int16_t ew_sat_i32_to_q15(int32_t v) {
    if (v < -32768) v = -32768;
    if (v > 32767) v = 32767;
    return (int16_t)v;
}

static inline int16_t ew_probe_field_internal_q1_15(const EwSpectralFieldAnchorState& s, const int32_t pos_q16_16[3]) {
    // Deterministic, bounded pseudo-evaluation of the spectral field at a position.
    // This is NOT a full IFFT; it is a lightweight probe using a fixed number of modes.
    // theta is derived from dot(k, x) with a stable hash-based k.
    const uint64_t seed = (uint64_t)(uint32_t)pos_q16_16[0]
                        ^ (ew_mix64((uint64_t)(uint32_t)pos_q16_16[1]) << 1)
                        ^ (ew_mix64((uint64_t)(uint32_t)pos_q16_16[2]) << 2)
                        ^ (ew_mix64((uint64_t)s.last_step_committed_u32) << 3);

    int64_t acc_q32_32 = 0;
    const uint32_t N = (s.n_u32 == 0u) ? EW_SPECTRAL_N : s.n_u32;
    const uint32_t modes = 8u;
    for (uint32_t m = 0u; m < modes; ++m) {
        const uint64_t h = ew_mix64(seed ^ (0x9e3779b97f4a7c15ULL * (uint64_t)(m + 1u)));
        const uint32_t k = (uint32_t)(h % (uint64_t)N);

        // theta in turns Q32.32. Use position components folded to turns.
        // We intentionally avoid non-deterministic transcendentals.
        const int64_t tx = ((int64_t)pos_q16_16[0] << 16); // Q32.32
        const int64_t ty = ((int64_t)pos_q16_16[1] << 16);
        const int64_t tz = ((int64_t)pos_q16_16[2] << 16);
        int64_t theta_q32_32 = (tx + (ty >> 1) + (tz >> 2));
        // Mix in k to decorrelate.
        theta_q32_32 ^= (int64_t)(k * 0x1f123bb5u);

        // Map to [0,1) turn in Q32.32.
        const int64_t one_turn = (1LL << 32);
        theta_q32_32 %= one_turn;
        if (theta_q32_32 < 0) theta_q32_32 += one_turn;

        const EwSinCosQ32_32 sc = ew_cordic_sincos_q32_32(theta_q32_32);
        const int64_t re = s.phi_hat[k].re_q32_32;
        const int64_t im = s.phi_hat[k].im_q32_32;
        // re*cos - im*sin
        const int64_t term = ew_mul_q32_32(re, sc.cos_q32_32) - ew_mul_q32_32(im, sc.sin_q32_32);
        acc_q32_32 += term;
    }

    // Normalize and map to Q1.15.
    acc_q32_32 /= (int64_t)modes;
    const int32_t q15 = (int32_t)(acc_q32_32 >> 17); // Q32.32 -> Q1.15
    return ew_sat_i32_to_q15(q15);
}

int16_t ew_spectral_probe_field_q1_15(const EwSpectralFieldAnchorState& s, const int32_t pos_q16_16[3]) {
    return ew_probe_field_internal_q1_15(s, pos_q16_16);
}

int16_t ew_spectral_probe_grad_q1_15(const EwSpectralFieldAnchorState& s, const int32_t pos_q16_16[3]) {
    // Finite difference gradient magnitude proxy in Q1.15.
    const int32_t h = (int32_t)(1 * 65536); // 1m
    int32_t p1[3] = {pos_q16_16[0] + h, pos_q16_16[1], pos_q16_16[2]};
    int32_t p2[3] = {pos_q16_16[0] - h, pos_q16_16[1], pos_q16_16[2]};
    const int32_t fx = (int32_t)ew_probe_field_internal_q1_15(s, p1) - (int32_t)ew_probe_field_internal_q1_15(s, p2);
    p1[0] = pos_q16_16[0]; p2[0] = pos_q16_16[0];
    p1[1] = pos_q16_16[1] + h; p2[1] = pos_q16_16[1] - h;
    const int32_t fy = (int32_t)ew_probe_field_internal_q1_15(s, p1) - (int32_t)ew_probe_field_internal_q1_15(s, p2);
    p1[1] = pos_q16_16[1]; p2[1] = pos_q16_16[1];
    p1[2] = pos_q16_16[2] + h; p2[2] = pos_q16_16[2] - h;
    const int32_t fz = (int32_t)ew_probe_field_internal_q1_15(s, p1) - (int32_t)ew_probe_field_internal_q1_15(s, p2);

    // L1 norm magnitude, scaled.
    int32_t mag = (fx < 0 ? -fx : fx) + (fy < 0 ? -fy : fy) + (fz < 0 ? -fz : fz);
    mag >>= 2; // scale down
    return ew_sat_i32_to_q15(mag);
}

static inline uint64_t ew_mix64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static inline int64_t ew_mul_q32_32(int64_t a_q32_32, int64_t b_q32_32) {
    __int128 p = (__int128)a_q32_32 * (__int128)b_q32_32;
    return (int64_t)(p >> 32);
}

static inline int64_t ew_clamp_i64(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline uint16_t ew_q32_32_to_q15_sat(int64_t v_q32_32) {
    // Map |v| in Q32.32 to Q15 in [0..32767] by clamping to [0..1].
    uint64_t a = (uint64_t)((v_q32_32 < 0) ? -v_q32_32 : v_q32_32);
    if (a > (uint64_t)(1ULL << 32)) a = (uint64_t)(1ULL << 32);
    return (uint16_t)((a * 32767ULL) >> 32);
}

static inline uint8_t ew_band_from_q15(uint16_t q15) {
    // crude log band from 0..32767.
    uint8_t band = 0;
    uint32_t v = (uint32_t)q15;
    while (v > 1u && band + 1u < 8u) { v >>= 1u; ++band; }
    return band;
}

static inline uint64_t ew_abs_i64_to_u64(int64_t x) {
    return (uint64_t)((x < 0) ? -x : x);
}

static inline uint32_t ew_u32_min(uint32_t a, uint32_t b) { return (a < b) ? a : b; }

static inline uint8_t ew_band_from_q15(uint16_t v_q15) {
    uint32_t x = (uint32_t)v_q15;
    uint8_t b = 0u;
    while (x > 0u && b + 1u < 8u) {
        x >>= 1;
        ++b;
    }
    return b;
}

static inline bool ew_in_region_q16_16(const int32_t center_q16_16[3], int32_t radius_q16_16, const int32_t p_q16_16[3]) {
    const int64_t dx = (int64_t)p_q16_16[0] - (int64_t)center_q16_16[0];
    const int64_t dy = (int64_t)p_q16_16[1] - (int64_t)center_q16_16[1];
    const int64_t dz = (int64_t)p_q16_16[2] - (int64_t)center_q16_16[2];
    const int64_t r = (int64_t)radius_q16_16;
    const __int128 d2 = (__int128)dx*dx + (__int128)dy*dy + (__int128)dz*dz;
    const __int128 r2 = (__int128)r*r;
    return d2 <= r2;
}

static void ew_apply_hooks(EwSpectralFieldAnchorState& ss) {
    if (ss.hook_inbox_count_u32 == 0u) return;

    // Stable-sort inbox deterministically by (op, causal_tag, authority desc, p0, p1).
    std::stable_sort(ss.hook_inbox, ss.hook_inbox + ss.hook_inbox_count_u32,
        [](const EwHookPacket& a, const EwHookPacket& b) {
            if (a.hook_op_u8 != b.hook_op_u8) return a.hook_op_u8 < b.hook_op_u8;
            if (a.causal_tag_u8 != b.causal_tag_u8) return a.causal_tag_u8 < b.causal_tag_u8;
            if (a.authority_q15 != b.authority_q15) return a.authority_q15 > b.authority_q15;
            if (a.p0_q32_32 != b.p0_q32_32) return a.p0_q32_32 < b.p0_q32_32;
            return a.p1_q32_32 < b.p1_q32_32;
        }
    );

    // Apply hooks.
    for (uint32_t i = 0u; i < ss.hook_inbox_count_u32; ++i) {
        const EwHookPacket& hp = ss.hook_inbox[i];
        const EwCoherenceHookOp op = (EwCoherenceHookOp)hp.hook_op_u8;
        if (op == EwCoherenceHookOp::HookAdjustDt) {
            // dt_scale += p0, clamp [0.25, 4]
            ss.dt_scale_q32_32 = ew_clamp_i64(ss.dt_scale_q32_32 + hp.p0_q32_32, (1LL << 30), (4LL << 32));
        } else if (op == EwCoherenceHookOp::HookAdjustViscosity) {
            // viscosity_bias += p0, clamp [-1, +1]
            ss.viscosity_bias_q32_32 = ew_clamp_i64(ss.viscosity_bias_q32_32 + hp.p0_q32_32, -(1LL << 32), (1LL << 32));
        } else if (op == EwCoherenceHookOp::HookFanoutBudget) {
            // p1 encodes desired budget in Q32.32.
            int64_t b = hp.p1_q32_32 >> 32;
            if (b < 1) b = 1;
            if (b > 1024) b = 1024;
            ss.fanout_budget_u32 = (uint32_t)b;
        } else if (op == EwCoherenceHookOp::HookFreezeTick) {
            // Freeze tick (HOLD) for this evolution.
            ss.hold_tick_u8 = 1u;
        } else if (op == EwCoherenceHookOp::HookAdjustLearning) {
            // p0 is signed adjustment in Q32.32. Map to delta Q15 conservatively.
            const int64_t d = hp.p0_q32_32;
            int32_t dq15 = (int32_t)(((d < 0) ? -d : d) >> 17);
            if (dq15 > 4096) dq15 = 4096;
            int32_t cur = (int32_t)ss.learning_coupling_q15;
            if (d >= 0) cur += dq15; else cur -= dq15;
            if (cur < 0) cur = 0;
            if (cur > 32767) cur = 32767;
            ss.learning_coupling_q15 = (uint16_t)cur;
        } else if (op == EwCoherenceHookOp::HookOperatorReplace) {
            // p0 encodes gain_q15 in the high 16 bits of Q32.32.
            // p1 is a signed residual proxy (Q32.32).
            const uint32_t gain_q15 = (uint32_t)((uint64_t)hp.p0_q32_32 >> 32);
            const int64_t residual = hp.p1_q32_32;

            // Update op_gain_q15 toward a target based on residual sign.
            // Deterministic bounded update: op_gain += sign(residual) * gain/8.
            int32_t cur = (int32_t)ss.op_gain_q15;
            int32_t dg = (int32_t)(gain_q15 >> 3);
            if (dg < 1) dg = 1;
            if (residual >= 0) cur += dg; else cur -= dg;
            if (cur < 8192) cur = 8192;
            if (cur > 32767) cur = 32767;
            ss.op_gain_q15 = (uint16_t)cur;

            // Update low-frequency band weights slightly (collapse-like operator replacement).
            // We bias weights toward bin0 on negative residual, toward bin1..3 on positive residual.
            const uint16_t wstep = (uint16_t)ew_u32_min((uint32_t)(gain_q15 >> 6), 256u);
            if (residual >= 0) {
                for (uint32_t k = 1u; k < 4u; ++k) {
                    uint32_t w = (uint32_t)ss.op_band_w_q15[k] + (uint32_t)wstep;
                    if (w > 32767u) w = 32767u;
                    ss.op_band_w_q15[k] = (uint16_t)w;
                }
            } else {
                uint32_t w = (uint32_t)ss.op_band_w_q15[0] + (uint32_t)wstep;
                if (w > 32767u) w = 32767u;
                ss.op_band_w_q15[0] = (uint16_t)w;
            }

            // Gentle normalization (keep sum within a soft bound).
            uint32_t sum = 0u;
            for (uint32_t k = 0u; k < 8u; ++k) sum += (uint32_t)ss.op_band_w_q15[k];
            if (sum > 65534u) {
                for (uint32_t k = 0u; k < 8u; ++k) {
                    ss.op_band_w_q15[k] = (uint16_t)(((uint32_t)ss.op_band_w_q15[k] * 65534u) / sum);
                }
            }
        } else if (op == EwCoherenceHookOp::HookResyncPhase) {
            // Minimal deterministic resync: reset spectral state to zero.
            for (uint32_t k = 0; k < EW_SPECTRAL_N; ++k) {
                ss.phi_hat[k].re_q32_32 = 0;
                ss.phi_hat[k].im_q32_32 = 0;
                ss.forcing_hat[k].re_q32_32 = 0;
                ss.forcing_hat[k].im_q32_32 = 0;
            }
        }
    }

    ss.hook_inbox_count_u32 = 0u;
}

static void ew_clear_forcing(EwSpectralFieldAnchorState& ss) {
    for (uint32_t k = 0; k < EW_SPECTRAL_N; ++k) {
        ss.forcing_hat[k].re_q32_32 = 0;
        ss.forcing_hat[k].im_q32_32 = 0;
    }
}

static void ew_calibration_inject_forcing(EwSpectralFieldAnchorState& ss, uint64_t tick_u64) {
    // Deterministic pulse-train-like forcing that does not rely on external queues.
    // This is bounded and only active while calibration_mode_u8==1.
    //
    // Profile 0: rotating single-bin impulse with fixed amplitude gated by last v/i.
    const uint32_t k = (uint32_t)(tick_u64 & (EW_SPECTRAL_N - 1u));

    // Use last v/i as authority proxy. If absent, assume mid authority.
    uint16_t v = ss.intent_summary.last_v_code_u16;
    uint16_t i = ss.intent_summary.last_i_code_u16;
    if (v == 0u) v = (uint16_t)(V_MAX / 2u);
    if (i == 0u) i = (uint16_t)(I_MAX / 2u);

    const uint64_t vi = (uint64_t)v * (uint64_t)i;
    const uint64_t vi_max = (uint64_t)V_MAX * (uint64_t)I_MAX;
    const int64_t auth_q32_32 = (vi_max != 0u) ? (int64_t)(((__int128)vi << 32) / (uint64_t)vi_max) : 0;


    // Fixed amplitude baseline in [0..1] Q32.32.
    const int64_t a_q32_32 = (int64_t)(1ULL << 30); // 0.25
    int64_t amp = ew_mul_q32_32(a_q32_32, auth_q32_32);

    // Learning influx coupling: scale forcing by (1 + 0.5*learning_coupling).
    // learning_coupling_q15 in [0..32767] -> gain in Q32.32 scaled by 0.5.
    const int64_t learn_gain_q32_32 = ((int64_t)ss.learning_coupling_q15) << 16;
    amp = amp + ew_mul_q32_32(amp, learn_gain_q32_32);

    // Phase offset from k.
    const uint64_t h = ew_mix64(((uint64_t)ss.calibration_profile_u8 << 56) ^ (uint64_t)k);
    const int64_t phase_q32_32 = (int64_t)(h & 0xFFFFFFFFull);
    static const int64_t TWO_PI_Q32_32 = 26986075409LL;
    int64_t theta = (int64_t)(((__int128)phase_q32_32 * (__int128)TWO_PI_Q32_32) >> 32);
    static const int64_t PI_Q32_32 = 13493037704LL;
    while (theta > PI_Q32_32) theta -= TWO_PI_Q32_32;
    while (theta < -PI_Q32_32) theta += TWO_PI_Q32_32;
    const EwSinCosQ32_32 sc = ew_cordic_sincos_q32_32(theta);

    ss.forcing_hat[k].re_q32_32 += ew_mul_q32_32(amp, sc.cos_q32_32);
    ss.forcing_hat[k].im_q32_32 += ew_mul_q32_32(amp, sc.sin_q32_32);
}

void ew_fourier_fanout_step(EwState& cand, const EwInputs& inputs, const EwCtx& ctx) {
    (void)ctx;

    // Pre-sort pulses for determinism (only those that target spectral anchors).
    struct TmpPulse { Pulse p; };
    std::vector<TmpPulse> ps;
    ps.reserve(inputs.inbound.size());
    for (size_t i = 0; i < inputs.inbound.size(); ++i) {
        const Pulse& p = inputs.inbound[i];
        if (p.anchor_id < cand.anchors.size() && cand.anchors[p.anchor_id].kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) {
            ps.push_back({p});
        }
    }
    std::stable_sort(ps.begin(), ps.end(), [](const TmpPulse& a, const TmpPulse& b) {
        if (a.p.tick != b.p.tick) return a.p.tick < b.p.tick;
        if (a.p.anchor_id != b.p.anchor_id) return a.p.anchor_id < b.p.anchor_id;
        if (a.p.causal_tag != b.p.causal_tag) return a.p.causal_tag < b.p.causal_tag;
        if (a.p.profile_id != b.p.profile_id) return a.p.profile_id < b.p.profile_id;
        if (a.p.f_code != b.p.f_code) return a.p.f_code < b.p.f_code;
        return a.p.a_code < b.p.a_code;
    });

    // Process each spectral anchor.
    for (uint32_t ai = 0u; ai < (uint32_t)cand.anchors.size(); ++ai) {
        Anchor& a = cand.anchors[ai];
        if (a.kind_u32 != EW_ANCHOR_KIND_SPECTRAL_FIELD) continue;
        EwSpectralFieldAnchorState& ss = a.spectral_field_state;

        // Consume hook packets addressed to this anchor.
        ew_apply_hooks(ss);

        // Clear forcing bins.
        ew_clear_forcing(ss);

        // Clear bounded actuation slots.
        ss.actuation_count_u32 = 0u;

        // Build explicit actuation-plane packets from pulses targeting this anchor.
        uint16_t last_v = 0, last_i = 0;
        for (size_t pi = 0; pi < ps.size(); ++pi) {
            const Pulse& p = ps[pi].p;
            if (p.anchor_id != ai) continue;
            last_v = p.v_code;
            last_i = p.i_code;

            if (ss.actuation_count_u32 >= EW_SPECTRAL_TRAJ_SLOTS) continue;

            // Map f_code to spectral bin.
            const uint32_t k = (uint32_t)((p.f_code < 0 ? -p.f_code : p.f_code) & (EW_SPECTRAL_N - 1u));

            // Carrier authority gate (v*i in Q32.32, normalized by max).
            const uint64_t vi = (uint64_t)p.v_code * (uint64_t)p.i_code;
            const uint64_t vi_max = (uint64_t)V_MAX * (uint64_t)I_MAX;
            const int64_t auth_q32_32 = (vi_max != 0u) ? (int64_t)(((__int128)vi << 32) / (uint64_t)vi_max) : 0;

            // Amplitude from a_code in Q32.32 in [0..1].
            const int64_t a_q32_32 = (int64_t)(((__int128)p.a_code << 32) / 65535);

            // Base signed amplitude impulse as (a * auth) with sign from f_code.
            int64_t base_amp = ew_mul_q32_32(a_q32_32, auth_q32_32);
            if (p.f_code < 0) base_amp = -base_amp;

            EwActuationPacket& ap = ss.actuation_slots[ss.actuation_count_u32++];
            ap.drive_k_u16 = (uint16_t)k;
            ap.op_tag_u8 = EW_ACT_OP_DRIVE;
            ap.flags_u8 = 0;
            ap.delta_amp_q32_32 = base_amp;
            ap.profile_id_u8 = p.profile_id;
            ap.causal_tag_u8 = p.causal_tag;
            ap.pad0 = 0;
            ap.v_code_u16 = p.v_code;
            ap.i_code_u16 = p.i_code;
            ap.payload_len_u8 = 0;
        }

        ss.intent_summary.last_v_code_u16 = last_v;
        ss.intent_summary.last_i_code_u16 = last_i;

        // Apply actuation packets to forcing_hat.
        for (uint32_t wi = 0u; wi < ss.actuation_count_u32; ++wi) {
            const EwActuationPacket& ap = ss.actuation_slots[wi];
            const uint32_t k = (uint32_t)(ap.drive_k_u16 & (EW_SPECTRAL_N - 1u));
            int64_t amp = ap.delta_amp_q32_32;

            // Apply learning coupling boost (absorption proxy).
            const int64_t learn_gain_q32_32 = ((int64_t)ss.learning_coupling_q15) << 16;
            amp = amp + ew_mul_q32_32(amp, learn_gain_q32_32);

            // Apply temporal operator gain (collapse-like coupling).
            const int64_t op_gain_q32_32 = ((int64_t)ss.op_gain_q15) << 17; // ~[0..1]
            amp = ew_mul_q32_32(amp, op_gain_q32_32);

            // Apply low-frequency band weighting based on k&7.
            const uint16_t bw = ss.op_band_w_q15[k & 7u];
            const int64_t bw_q32_32 = ((int64_t)bw) << 17;
            amp = ew_mul_q32_32(amp, bw_q32_32);

            // Mode injection uses a deterministic phase offset from profile_id.
            const uint64_t h = ew_mix64(((uint64_t)ap.profile_id_u8 << 32) ^ (uint64_t)k);
            const int64_t phase_q32_32 = (int64_t)(h & 0xFFFFFFFFull); // [0..2^32)

            // Convert phase to radians in [-pi, +pi] using 2pi wrap.
            static const int64_t TWO_PI_Q32_32 = 26986075409LL;
            int64_t theta = (int64_t)(((__int128)phase_q32_32 * (__int128)TWO_PI_Q32_32) >> 32);
            static const int64_t PI_Q32_32 = 13493037704LL;
            while (theta > PI_Q32_32) theta -= TWO_PI_Q32_32;
            while (theta < -PI_Q32_32) theta += TWO_PI_Q32_32;
            const EwSinCosQ32_32 sc = ew_cordic_sincos_q32_32(theta);

            ss.forcing_hat[k].re_q32_32 += ew_mul_q32_32(amp, sc.cos_q32_32);
            ss.forcing_hat[k].im_q32_32 += ew_mul_q32_32(amp, sc.sin_q32_32);
        }

        // Intent summary: hash + magnitude proxy from forcing (first 8 bins).
        // This forms the actuation "intent" plane for temporal coupling.
        uint64_t intent_hash = 0xA17E5EEDULL ^ ((uint64_t)ss.twiddle_profile_u32 << 32) ^ (uint64_t)cand.tick_u64;
        uint64_t intent_acc_q15 = 0u;
        for (uint32_t kk = 0u; kk < 8u; ++kk) {
            const uint16_t ar = ew_q32_32_to_q15_sat(ss.forcing_hat[kk].re_q32_32);
            const uint16_t ai = ew_q32_32_to_q15_sat(ss.forcing_hat[kk].im_q32_32);
            const uint16_t m = (uint16_t)ew_u32_min((uint32_t)ar + (uint32_t)ai, 32767u);
            intent_acc_q15 += (uint64_t)m;
            intent_hash = ew_mix64(intent_hash ^ ((uint64_t)m << (kk & 31u)) ^ ((uint64_t)kk * 0x9E3779B97F4A7C15ULL));
        }
        const uint16_t intent_norm_q15 = (uint16_t)ew_u32_min((uint32_t)(intent_acc_q15 / 8u), 32767u);
        for (uint32_t kk = 0u; kk < 8u; ++kk) {
            const uint16_t ar = ew_q32_32_to_q15_sat(ss.forcing_hat[kk].re_q32_32);
            const uint16_t ai = ew_q32_32_to_q15_sat(ss.forcing_hat[kk].im_q32_32);
            const uint16_t m = (uint16_t)ew_u32_min((uint32_t)ar + (uint32_t)ai, 32767u);
            ss.intent_summary.band_mag_q15[kk] = m;
        }
        ss.intent_summary.intent_norm_q15 = intent_norm_q15;
        ss.temporal_residual.intent_hash_u64 = intent_hash;

        // Unified fanout work budget (materialized updates + coupling work).
        // This keeps coupling injection from dominating tick cost and ensures
        // deterministic throttling via the same knob.
        uint32_t work_budget = ew_u32_min(ss.fanout_budget_u32, 512u);

        // Inject coupling-particle forcing from voxel coupling anchors.
        // This is the substrate-native bridge from voxel collision boundaries into
        // the fluid-like spectral evolution path.
        // Deterministic traversal: sort voxel coupling anchors by anchor.id_u32.
        std::vector<const Anchor*> voxels;
        voxels.reserve(8);
        for (const Anchor& vx : cand.anchors) {
            if (vx.kind_u32 == EW_ANCHOR_KIND_VOXEL_COUPLING) voxels.push_back(&vx);
        }
        std::stable_sort(voxels.begin(), voxels.end(), [](const Anchor* a, const Anchor* b) {
            return a->id < b->id;
        });

        for (const Anchor* vxp : voxels) {
            if (work_budget == 0u) break;
            const EwVoxelCouplingAnchorState& vs = vxp->voxel_coupling_state;
            const uint32_t pc = (vs.particle_count_u32 > EW_VOXEL_COUPLING_PARTICLES_MAX) ? EW_VOXEL_COUPLING_PARTICLES_MAX : vs.particle_count_u32;
            for (uint32_t pi = 0; pi < pc && work_budget > 0u; ++pi) {
                const EwVoxelCouplingParticle& p = vs.particles[pi];
                if (p.rho_q15 == 0u) continue;
                // Count this particle as fanout work.
                --work_budget;

                // Deterministic mode mapping from particle position.
                const uint64_t h = ew_mix64(((uint64_t)(uint32_t)p.pos_q16_16[0] << 32) ^ (uint64_t)(uint32_t)p.pos_q16_16[2] ^ (uint64_t)pi);
                const uint32_t k = (uint32_t)(h & (EW_SPECTRAL_N - 1u));

                // Base amplitude from rho*coupling in Q32.32.
                const uint64_t rc = (uint64_t)p.rho_q15 * (uint64_t)p.coupling_q15;
                int64_t amp = (int64_t)((rc << 32) / (uint64_t)(32767u * 32767u));

                // Apply learning coupling (absorption proxy): boosts coupling forcing.
                const int64_t learn_gain_q32_32 = ((int64_t)ss.learning_coupling_q15) << 16;
                amp = amp + ew_mul_q32_32(amp, learn_gain_q32_32);

                // Apply temporal operator gain + band weights (collapse-like coupling).
                const int64_t op_gain_q32_32 = ((int64_t)ss.op_gain_q15) << 17;
                amp = ew_mul_q32_32(amp, op_gain_q32_32);
                const uint16_t bw = ss.op_band_w_q15[k & 7u];
                const int64_t bw_q32_32 = ((int64_t)bw) << 17;
                amp = ew_mul_q32_32(amp, bw_q32_32);

                // Apply voxel boundary mean strength (no-slip / boundary coupling proxy).
                const int64_t bs_gain_q32_32 = ((int64_t)vs.boundary_strength_mean_q15) << 16;
                amp = amp + ew_mul_q32_32(amp, (bs_gain_q32_32 >> 1));

                // Deterministic phase per particle.
                const int64_t phase_q32_32 = (int64_t)(h >> 32);
                static const int64_t TWO_PI_Q32_32 = 26986075409LL;
                int64_t theta = (int64_t)(((__int128)phase_q32_32 * (__int128)TWO_PI_Q32_32) >> 32);
                static const int64_t PI_Q32_32 = 13493037704LL;
                while (theta > PI_Q32_32) theta -= TWO_PI_Q32_32;
                while (theta < -PI_Q32_32) theta += TWO_PI_Q32_32;
                const EwSinCosQ32_32 sc = ew_cordic_sincos_q32_32(theta);

                ss.forcing_hat[k].re_q32_32 += ew_mul_q32_32(amp, sc.cos_q32_32);
                ss.forcing_hat[k].im_q32_32 += ew_mul_q32_32(amp, sc.sin_q32_32);
            }
        }

        // Calibration forcing is injected after external pulses.
        if (ss.calibration_mode_u8 == 1u && ss.calibration_ticks_remaining_u32 > 0u) {
            ew_calibration_inject_forcing(ss, cand.tick_u64);
        }

        // If frozen by hook, HOLD this tick: keep state stable and emit minimal leakage status.
        if (ss.hold_tick_u8) {
            ss.hold_tick_u8 = 0u;
            ss.last_step_committed_u32 = 0u;
            ss.leakage_pending_u8 = 0u;
            continue;
        }

        // Evolve phi_hat in k-space: simple damped wave bootstrap.
        // phi_hat += dt*(forcing - damping*phi_hat)
        const int64_t base_dt = (1LL << 28); // ~0.0625
        const int64_t dt = ew_mul_q32_32(base_dt, ss.dt_scale_q32_32);
        // Base damping from viscosity bias.
        int64_t damping = ew_clamp_i64((1LL << 29) + (ss.viscosity_bias_q32_32 >> 1), 0, (1LL << 31)); // 0..0.5

        // Voxel-boundary coupling term (Navier–Stokes-style boundary influence):
        // - stronger boundaries increase effective damping (no-slip)
        // - higher permeability reduces the boundary contribution (slip/flow-through)
        const int64_t bs_q32_32 = ((int64_t)ss.boundary_strength_mean_q15) << 17; // ~[0..1] in Q32.32
        const int64_t perm_q32_32 = ((int64_t)ss.permeability_mean_q15) << 17;   // ~[0..1] in Q32.32
        const int64_t inv_perm_q32_32 = (1LL << 32) - ew_clamp_i64(perm_q32_32, 0, (1LL << 32));
        int64_t boundary_term = ew_mul_q32_32(bs_q32_32, inv_perm_q32_32); // higher when strong boundary and low permeability

        // Boundary anisotropy coupling: if the boundary field is strongly axis-dominant,
        // increase the effective boundary damping contribution. This is a compact proxy
        // for directional coupling without a full vector Navier–Stokes solve.
        const int64_t aniso_q32_32 = ((int64_t)ss.boundary_anisotropy_q15) << 17; // ~[0..1]
        // boundary_term *= (1 + 0.5*aniso)
        boundary_term = boundary_term + (ew_mul_q32_32(boundary_term, aniso_q32_32) >> 1);

        damping = ew_clamp_i64(damping + (boundary_term >> 3), 0, (1LL << 31));  // bounded additive influence

        uint16_t e_mean = 0;
        uint16_t e_peak = 0;
        uint64_t e_acc = 0;
        for (uint32_t k = 0; k < EW_SPECTRAL_N; ++k) {
            // damping term = damping * phi
            const int64_t dr = ew_mul_q32_32(damping, ss.phi_hat[k].re_q32_32);
            const int64_t di = ew_mul_q32_32(damping, ss.phi_hat[k].im_q32_32);
            const int64_t fr = ss.forcing_hat[k].re_q32_32 - dr;
            const int64_t fi = ss.forcing_hat[k].im_q32_32 - di;
            ss.phi_hat[k].re_q32_32 += ew_mul_q32_32(dt, fr);
            ss.phi_hat[k].im_q32_32 += ew_mul_q32_32(dt, fi);

            // Energy proxy = |phi| clamped.
            const uint16_t em = ew_q32_32_to_q15_sat(ss.phi_hat[k].re_q32_32) + ew_q32_32_to_q15_sat(ss.phi_hat[k].im_q32_32);
            if (em > e_peak) e_peak = em;
            e_acc += (uint64_t)em;
        }
        e_mean = (uint16_t)((EW_SPECTRAL_N != 0u) ? (e_acc / EW_SPECTRAL_N) : 0u);
        ss.energy_mean_q15 = e_mean;
        ss.energy_peak_q15 = e_peak;
        ss.measured_summary.energy_mean_q15 = e_mean;
        ss.measured_summary.energy_peak_q15 = e_peak;

        // Measured summary: hash of resulting low-frequency bins (first 8 bins).
        uint64_t measured_hash = 0xC011A9EULL ^ ((uint64_t)ss.twiddle_profile_u32 << 32) ^ (uint64_t)cand.tick_u64;
        for (uint32_t kk = 0u; kk < 8u; ++kk) {
            const uint16_t ar = ew_q32_32_to_q15_sat(ss.phi_hat[kk].re_q32_32);
            const uint16_t ai = ew_q32_32_to_q15_sat(ss.phi_hat[kk].im_q32_32);
            const uint16_t m = (uint16_t)ew_u32_min((uint32_t)ar + (uint32_t)ai, 32767u);
            measured_hash = ew_mix64(measured_hash ^ ((uint64_t)m << (kk & 31u)) ^ ((uint64_t)kk * 0xD1B54A32D192ED03ULL));
        }
        ss.temporal_residual.measured_hash_u64 = measured_hash;

        // Temporal residual: discrepancy between intended forcing magnitude and measured energy peak.
        const int32_t r_q15 = (int32_t)e_peak - (int32_t)intent_norm_q15;
        const uint16_t rabs_q15 = (uint16_t)((r_q15 < 0) ? -r_q15 : r_q15);
        ss.temporal_residual.residual_norm_q15 = rabs_q15;
        ss.temporal_residual.residual_band_u8 = ew_band_from_q15(rabs_q15);
        ss.temporal_residual.residual_q32_32 = ((int64_t)r_q15) << 17;
        if (rabs_q15 > ss.noise_floor_q15 && rabs_q15 > ss.min_delta_q15) {
            ss.temporal_residual.residual_pending_u8 = 1u;
        } else {
            ss.temporal_residual.residual_pending_u8 = 0u;
        }

        // Leakage residual proxy: excess above noise floor.
        uint16_t leak_abs = 0;
        if (e_peak > ss.noise_floor_q15) leak_abs = (uint16_t)(e_peak - ss.noise_floor_q15);
        ss.leakage_abs_q15 = leak_abs;
        ss.measured_summary.leakage_abs_q15 = leak_abs;

        // Tick-hold rule.
        const uint16_t delta = (e_peak > e_mean) ? (uint16_t)(e_peak - e_mean) : 0u;
        const uint16_t min_gate = (ss.min_delta_q15 > ss.noise_floor_q15) ? ss.min_delta_q15 : ss.noise_floor_q15;
        const bool commit = (delta >= ss.min_delta_q15) && (e_peak >= min_gate);
        ss.last_step_committed_u32 = commit ? 1u : 0u;

        // Publish leakage when energy peak exceeds noise floor.
        if (leak_abs > 0u) {
            // leakage = leak_abs mapped to Q32.32 in [0..1].
            const int64_t leak_q32_32 = (int64_t)(((__int128)leak_abs << 32) / 32767);
            ss.leakage_q32_32 = leak_q32_32;
            ss.leakage_band_u8 = ew_band_from_q15(leak_abs);
            ss.leakage_hash_u64 = ew_mix64(((uint64_t)e_peak << 32) ^ (uint64_t)e_mean ^ (uint64_t)ai);
            ss.leakage_pending_u8 = 1u;
        } else {
            ss.leakage_pending_u8 = 0u;
        }

        // Calibration accumulation and finalization.
        if (ss.calibration_mode_u8 == 1u && ss.calibration_ticks_remaining_u32 > 0u) {
            ss.cal_energy_sum_u64 += (uint64_t)e_peak;
            ss.cal_leak_abs_sum_u64 += (uint64_t)leak_abs;
            ss.cal_count_u32 += 1u;
            ss.calibration_ticks_remaining_u32 -= 1u;
            if (ss.calibration_ticks_remaining_u32 == 0u) {
                // Derive deterministic scalars.
                const uint32_t n = (ss.cal_count_u32 == 0u) ? 1u : ss.cal_count_u32;
                const uint64_t mean_e = ss.cal_energy_sum_u64 / (uint64_t)n;
                const uint64_t mean_leak = ss.cal_leak_abs_sum_u64 / (uint64_t)n;

                // Noise floor: mean_leak + small guard, clamped.
                uint32_t nf = (uint32_t)mean_leak + 8u;
                if (nf > 32767u) nf = 32767u;
                ss.noise_floor_q15 = (uint16_t)nf;

                // dt_scale: bias toward stability if mean energy is high.
                // Map mean_e in [0..32767] to dt_scale in [0.5..1.5].
                const uint64_t me = (mean_e > 32767u) ? 32767u : mean_e;
                const int64_t half = (1LL << 31);
                const int64_t one = (1LL << 32);
                const int64_t dt_bump = (int64_t)(((__int128)me * (__int128)(1LL<<31)) / 32767); // [0..0.5]
                ss.dt_scale_q32_32 = ew_clamp_i64(one - half + dt_bump, (1LL<<31), (2LL<<32));

                // viscosity_bias: if mean leak is high, increase damping slightly.
                const uint64_t ml = (mean_leak > 32767u) ? 32767u : mean_leak;
                const int64_t vb = (int64_t)(((__int128)ml * (__int128)(1LL<<30)) / 32767); // [0..0.25]
                ss.viscosity_bias_q32_32 = ew_clamp_i64(ss.viscosity_bias_q32_32 + vb, -(1LL<<32), (1LL<<32));

                ss.calibration_mode_u8 = 2u;
            }
        }

        // Fan-out influence (bounded): write a deterministic flux magnitude to object/planet anchors in region.
        if (commit) {
            // Remaining work budget is used for materialized influence writes.
            uint32_t budget = work_budget;

            for (uint32_t j = 0u; j < (uint32_t)cand.anchors.size() && budget > 0u; ++j) {
                Anchor& o = cand.anchors[j];

                if (o.kind_u32 == EW_ANCHOR_KIND_OBJECT) {
                    const int32_t p_q16_16[3] = {
                        o.object_state.pos_q16_16[0],
                        o.object_state.pos_q16_16[1],
                        o.object_state.pos_q16_16[2]
                    };
                    if (!ew_in_region_q16_16(ss.region_center_q16_16, ss.region_radius_m_q16_16, p_q16_16)) continue;

                    const uint64_t h = ew_mix64(o.object_id_u64 ^ ((uint64_t)ai << 1));
                    const uint32_t k = (uint32_t)(h & (EW_SPECTRAL_N - 1u));
                    const uint16_t mag = (uint16_t)ew_u32_min(
                        (uint32_t)ew_q32_32_to_q15_sat(ss.phi_hat[k].re_q32_32) + (uint32_t)ew_q32_32_to_q15_sat(ss.phi_hat[k].im_q32_32),
                        32767u
                    );
                    o.world_flux_grad_mean_q15 = mag;
                    --budget;
                } else if (o.kind_u32 == EW_ANCHOR_KIND_PLANET) {
                    const int32_t p_q16_16[3] = {
                        o.planet_state.pos_q16_16[0],
                        o.planet_state.pos_q16_16[1],
                        o.planet_state.pos_q16_16[2]
                    };
                    if (!ew_in_region_q16_16(ss.region_center_q16_16, ss.region_radius_m_q16_16, p_q16_16)) continue;

                    const uint64_t h = ew_mix64(((uint64_t)o.planet_state.parent_anchor_id_u32 << 32) ^ ((uint64_t)j << 3) ^ (uint64_t)ai);
                    const uint32_t k = (uint32_t)(h & (EW_SPECTRAL_N - 1u));
                    const uint16_t mag = (uint16_t)ew_u32_min(
                        (uint32_t)ew_q32_32_to_q15_sat(ss.phi_hat[k].re_q32_32) + (uint32_t)ew_q32_32_to_q15_sat(ss.phi_hat[k].im_q32_32),
                        32767u
                    );
                    // Bias voxel resonance and flux observable (bounded, deterministic).
                    const uint32_t v0 = (uint32_t)o.planet_state.voxel_resonance_q15;
                    uint32_t v1 = v0 + (uint32_t)(mag >> 2); // small bias
                    if (v1 > 32767u) v1 = 32767u;
                    o.planet_state.voxel_resonance_q15 = (uint16_t)v1;
                    o.world_flux_grad_mean_q15 = (mag > o.world_flux_grad_mean_q15) ? mag : o.world_flux_grad_mean_q15;
                    --budget;
                }
            }
        }
    }
}
