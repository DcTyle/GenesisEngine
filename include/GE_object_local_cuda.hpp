#pragma once

#include <cstddef>
#include <cstdint>

namespace genesis {

// Combined GPU step for object-local sublattice state.
//
// Inputs:
//  - occ_u8: occupancy volume (0..255)
//  - phi_in_q15_s16: local phase field per voxel (int16, Q15 signed)
//
// Outputs:
//  - phi_out_q15_s16: updated phase field (same layout)
//  - stats: deterministic integer reductions for coupling observables
//
// Determinism note: reductions use integer atomic adds; results are
// deterministic regardless of thread order.
struct EwObjectLocalStepStats {
    uint64_t occ_sum_u64 = 0;
    uint64_t vox_count_u64 = 0;
    uint64_t boundary_occ_sum_u64 = 0;
    uint64_t boundary_count_u64 = 0;

    int64_t phi_sum_i64 = 0;
    uint64_t phi_count_u64 = 0;

    uint64_t boundary_grad_sum_u64 = 0;   // sum of |grad| magnitudes on boundary
    uint64_t boundary_grad_count_u64 = 0; // number of boundary voxels contributing
};

// Boundary bias sampled from the world lattice.
// These centered signed Q15-ish means are applied only on boundary voxels.
struct EwWorldBoundaryBiasQ15 {
    int16_t e_curr_q15 = 0;
    int16_t flux_q15 = 0;
    int16_t coherence_q15 = 0;
    int16_t curvature_q15 = 0;
    int16_t doppler_q15 = 0;
};

bool ge_cuda_object_local_step_q15(const uint8_t* occ_u8,
                                  const int16_t* phi_in_q15_s16,
                                  uint32_t gx_u32, uint32_t gy_u32, uint32_t gz_u32,
                                  const EwWorldBoundaryBiasQ15& world_bias,
                                  int16_t* phi_out_q15_s16,
                                  EwObjectLocalStepStats& out_stats);

} // namespace genesis
