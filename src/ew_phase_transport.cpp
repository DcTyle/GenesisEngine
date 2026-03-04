#include "ew_phase_transport.h"
#include "GE_runtime.hpp"
#include "fixed_point.hpp"
#include "substrate_alu.hpp"

uint64_t ew_phase_transport_dtheta_u64(const SubstrateManager* sm) {
    if (!sm) return 0ULL;

    // Match the deterministic surrogate used by the packed operator template:
    // dtheta = omega0 * dt, clamped to [-0.5, +0.5) turns (TURN_SCALE domain).
    // NOTE: We intentionally reuse the same scaling and clamp shape to preserve
    // runtime behavior while exposing a canonical named callable.
    ancilla_particle* tr = (!sm->ancilla.empty()) ? const_cast<ancilla_particle*>(&sm->ancilla[0]) : nullptr;

    const int64_t dtheta_q32_32 = ew_alu_mul_q32_32(sm->ctx_snapshot, tr,
                                                   sm->ctx_snapshot.omega0_turns_per_sec_q32_32,
                                                   sm->ctx_snapshot.tick_dt_seconds_q32_32);

    __int128 turns_q = (__int128)dtheta_q32_32 * (__int128)TURN_SCALE;
    turns_q >>= 32;
    int64_t dtheta_turns_q = (int64_t)turns_q;

    // Clamp to [-0.5, +0.5) turns (TURN_SCALE domain).
    const int64_t half_turns_q = (int64_t)(TURN_SCALE / 2ULL);
    if (dtheta_turns_q < -half_turns_q) dtheta_turns_q = -half_turns_q;
    if (dtheta_turns_q >= half_turns_q) dtheta_turns_q = half_turns_q - 1;

    // Return as unsigned in canonical TURN_SCALE ring (wrap).
    return (uint64_t)dtheta_turns_q;
}

