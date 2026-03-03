#pragma once

#include <cstdint>

class SubstrateMicroprocessor;

namespace genesis {

// Reduced coupling observables derived from an object's local voxel state.
// These are used to (1) update object update anchors deterministically and
// (2) inject bounded pulses into the global lattice.
struct EwObjectCouplingObs {
    uint64_t object_id_u64 = 0;
    // Q15 in [0..1].
    uint16_t density_mean_q15 = 0;
    // Q15 in [0..1].
    uint16_t boundary_density_q15 = 0;
    // TURN_SCALE-domain proxies.
    int64_t curvature_turns_q = 0;
    int64_t doppler_turns_q = 0;

    // World-sampled flux-gradient magnitude (Q0.15).
    uint16_t world_flux_grad_mean_q15 = 0;
};

class EwObjectAncilla {
public:
    // Canonical global ancilla anchor ids.
    static constexpr uint32_t LEDGER_ANCILLA_ID = 1u;
    static constexpr uint32_t OBJECTS_ANCILLA_ID = 2u;

    // Ensure canonical global ancillas exist and are correctly tagged.
    static void ensure_canonical_global_ancillas(SubstrateMicroprocessor* sm);

    // Ensure a per-object update anchor exists and is bound to object_id.
    // Returns the anchor id.
    static uint32_t ensure_object_update_anchor(SubstrateMicroprocessor* sm, uint64_t object_id_u64);

    // Advance a bounded number of objects this tick.
    // GPU-first: reduces voxel volumes on GPU; CPU fallback is disallowed
    // (fails closed) unless EW_ENABLE_CUDA=0 and verification mode is enabled.
    static void tick_object_updates(SubstrateMicroprocessor* sm, uint32_t max_objects_per_tick);

private:
    static bool compute_coupling_obs_gpu(SubstrateMicroprocessor* sm,
                                         uint64_t object_id_u64,
                                         EwObjectCouplingObs& out_obs);
    static void apply_obs_to_anchor_and_emit(SubstrateMicroprocessor* sm,
                                             uint32_t object_anchor_id_u32,
                                             const EwObjectCouplingObs& obs);
};

} // namespace genesis
