#pragma once
#include <cstdint>
#include "fixed_point.hpp"
#include "anchor.hpp"
#include "GE_runtime.hpp"
#include "ancilla_particle.hpp"
#include "substrate_alu.hpp"

// Omega.3.2 / Omega.4: deviation energy and constrained projection.
// This implementation is deterministic and fixed-point. The carrier metric
// is diagonal (ctx.carrier_g_q32_32), and epsilon is derived from the
// closure bound max_dtheta_turns_q so there is no undefined numeric path.

static inline int64_t clamp_i64_from_i128_local(__int128 v) {
    const __int128 lo = (__int128)INT64_MIN;
    const __int128 hi = (__int128)INT64_MAX;
    if (v < lo) return INT64_MIN;
    if (v > hi) return INT64_MAX;
    return (int64_t)v;
}

// Compute epsilon (Q32.32) from a TURN_SCALE-domain bound.
// epsilon = (max_dtheta / 1turn)^2.
static inline int64_t epsilon_from_turn_bound_q32_32(int64_t max_dtheta_turns_q) {
    if (max_dtheta_turns_q < 0) max_dtheta_turns_q = -max_dtheta_turns_q;
    // d = max_dtheta / TURN_SCALE in Q32.32.
    __int128 d_q = (__int128)max_dtheta_turns_q << 32;
    d_q /= (__int128)TURN_SCALE;
    // epsilon = d^2.
    __int128 e_q = (d_q * d_q) >> 32;
    if (e_q < 0) e_q = 0;
    if (e_q > (__int128)INT64_MAX) e_q = (__int128)INT64_MAX;
    return (int64_t)e_q;
}

// Integer sqrt for a 128-bit non-negative value.
static inline __int128 isqrt_u128(__int128 x) {
    if (x <= 0) return 0;
    __int128 r = 0;
    __int128 bit = (__int128)1 << 126;
    while (bit > x) bit >>= 2;
    while (bit != 0) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

// sqrt for Q32.32 input, returns Q32.32 output.
// y_q^2 = x_q << 32.
static inline int64_t sqrt_q32_32(int64_t x_q32_32) {
    if (x_q32_32 <= 0) return 0;
    __int128 x = (__int128)x_q32_32;
    __int128 y = isqrt_u128(x << 32);
    if (y > (__int128)INT64_MAX) y = (__int128)INT64_MAX;
    return (int64_t)y;
}

// Compute deviation energy E_dev = DeltaS^T G DeltaS in Q32.32.
// DeltaS components are TURN_SCALE-domain turns; they are normalized into
// Q32.32 turns by dividing by TURN_SCALE.
static inline int64_t deviation_energy_q32_32(const int64_t d_turns_q[9], const int64_t g_q32_32[9], const EwCtx& ctx, ancilla_particle* an) {
    __int128 e = 0;
    for (int i = 0; i < 9; ++i) {
        __int128 dq = (__int128)d_turns_q[i] << 32;
        dq /= (__int128)TURN_SCALE;
        const int64_t dq_q32_32 = clamp_i64_from_i128_local(dq);
        // Carrier trace: operand pair collapse for the deviation component.
        ew_alu_trace(an, ew_alu_carrier_id_u64_from_q32_32_pair(ctx, dq_q32_32, g_q32_32[i]));

        const int64_t sq_q32_32 = ew_alu_mul_q32_32(ctx, an, dq_q32_32, dq_q32_32);
        const int64_t term_q32_32 = ew_alu_mul_q32_32(ctx, an, sq_q32_32, g_q32_32[i]);
        e += (__int128)term_q32_32;
    }
    if (e < 0) e = 0;
    if (e > (__int128)INT64_MAX) e = (__int128)INT64_MAX;
    return (int64_t)e;
}

// Apply Pi_G to anchor basis9 relative to a reference basis9.
// The correction rescales DeltaS when E_dev > epsilon.
static inline void apply_pi_g_to_anchor(Anchor& a, const Basis9& ref, const EwCtx& ctx, ancilla_particle* an) {
    int64_t d_turns_q[9];
    for (int i = 0; i < 9; ++i) d_turns_q[i] = a.basis9.d[i] - ref.d[i];
    // For the phase axis (index 4), use wrapped delta to respect turn periodicity.
    d_turns_q[4] = delta_turns(a.basis9.d[4], ref.d[4]);

    const int64_t epsilon_q32_32 = epsilon_from_turn_bound_q32_32(ctx.max_dtheta_turns_q);
    const int64_t e_dev_q32_32 = deviation_energy_q32_32(d_turns_q, ctx.carrier_g_q32_32, ctx, an);
    if (e_dev_q32_32 == 0 || e_dev_q32_32 <= epsilon_q32_32) {
        return;
    }

    // scale = sqrt(epsilon / e_dev).
    const int64_t ratio_q32_32 = ew_alu_div_q32_32(ctx, an, epsilon_q32_32, e_dev_q32_32);
    const int64_t scale_q32_32 = sqrt_q32_32(ratio_q32_32);
    ew_alu_trace(an, ew_alu_carrier_id_u64_from_q32_32_pair(ctx, ratio_q32_32, scale_q32_32));

    for (int i = 0; i < 9; ++i) {
        __int128 dq = (__int128)d_turns_q[i] << 32;
        dq /= (__int128)TURN_SCALE;               // Q32.32
        const int64_t dq_q32_32 = clamp_i64_from_i128_local(dq);
        const int64_t dq_scaled_q32_32 = ew_alu_mul_q32_32(ctx, an, dq_q32_32, scale_q32_32);
        __int128 corr_turns = (__int128)dq_scaled_q32_32 * (__int128)TURN_SCALE;
        corr_turns >>= 32;
        a.basis9.d[i] = ref.d[i] + (int64_t)corr_turns;
    }

    // Ensure phase axis remains in [0, 1turn).
    a.basis9.d[4] = wrap_turns(a.basis9.d[4]);

    // Sync corrected basis back into the anchor core fields.
    a.tau_turns_q = a.basis9.d[3];
    a.theta_q = a.basis9.d[4];
    a.chi_q = a.basis9.d[5];
    if (a.chi_q < 0) a.chi_q = 0;
    a.curvature_q = a.basis9.d[6];
    a.doppler_q = a.basis9.d[7];
    a.m_q = a.basis9.d[8];
    if (a.m_q < 0) a.m_q = 0;
}
