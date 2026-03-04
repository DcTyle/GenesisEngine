#pragma once

#include <cstdint>
#include "GE_coherence_packets.hpp"
#include "GE_collision_constraint_packets.hpp"

// Voxel coupling anchor: deterministic substrate-instantiated microprocessor anchor
// that represents dense-object persistence + boundary coupling terms for fluid-like
// interactions. This is a contract-first implementation:
//  - fixed-size buffers (no heap)
//  - fixed-point only
//  - deterministic spawning of "coupling particles" from voxel density
//  - publishes an INFLUX packet (reverse of leakage) to the coherence bus

static const uint32_t EW_VOXEL_COUPLING_DIM = 8;          // 8x8x8 voxel block
static const uint32_t EW_VOXEL_COUPLING_PARTICLES_MAX = 64;

struct EwVoxelCouplingParticle {
    // Position in meters (Q16.16)
    int32_t pos_q16_16[3] = {0, 0, 0};
    // Velocity in meters/sec (Q16.16)
    int32_t vel_q16_16[3] = {0, 0, 0};

    // Density proxy (Q0.15)
    uint16_t rho_q15 = 0;
    // Boundary coupling factor (Q0.15)
    uint16_t coupling_q15 = 0;

    // Doppler-normalized phase accumulator (Q32.32 turns)
    int64_t doppler_norm_turns_q32_32 = 0;
    // "Compton-rate" phase increment proxy per tick (Q32.32 turns/tick)
    int64_t compton_rate_turns_q32_32 = 0;

    uint32_t pad0_u32 = 0;
};

struct EwVoxelCouplingAnchorState {
    // Voxel block origin in meters (Q16.16) and voxel size (Q16.16)
    int32_t origin_q16_16[3] = {0, 0, 0};
    int32_t voxel_size_m_q16_16 = (int32_t)(1 * 65536); // 1m default

    // Deterministic seed for spawning.
    uint64_t spawn_seed_u64 = 0;

    // Bounded voxel density map (Q0.15). Flattened index: x + dim*(y + dim*z)
    uint16_t rho_vox_q15[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];

    // Bounded coupling map derived from voxel collision boundaries (Q0.15).
    uint16_t coupling_vox_q15[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];

    // Solid mask derived from density threshold (0 or 1). Flattened like rho_vox.
    uint8_t solid_vox_u8[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];

    // Wall distance field (Manhattan distance to nearest solid voxel), clamped to 255.
    // This is a cheap, bounded proxy for boundary influence scaling.
    uint8_t wall_dist_vox_u8[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];

    // Boundary strength proxy (Q0.15): higher near solid/fluid interfaces.
    uint16_t boundary_strength_vox_q15[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];

    // Interface strength (Q0.15), derived from 6-neighbor gradient magnitude.
    uint16_t interface_strength_vox_q15[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];

    // Boundary interface classification (derived from solid/fluid stencil).
    // Encoding: bit0..1 axis (0=x,1=y,2=z), bit2 sign (0=+ ,1=-), bit3 boundary_flag (1 if boundary)
    uint8_t boundary_normal_u8[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];

    // Boundary condition toggles and permeability (derived, bounded).
    uint8_t no_slip_u8[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];
    uint16_t permeability_vox_q15[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];

    // Compact summaries for downstream coupling (Q0.15).
    uint16_t boundary_strength_mean_q15 = 0;
    uint16_t wall_dist_mean_q15 = 0;
    uint16_t permeability_mean_q15 = 0;
    uint16_t interface_strength_mean_q15 = 0;

    // Dominant interface axis (0=x,1=y,2=z) and anisotropy strength (Q0.15).
    // This is a compact boundary-coupling descriptor for downstream operators.
    uint8_t boundary_axis_dom_u8 = 0;
    uint8_t pad_axis_u8 = 0;
    uint16_t boundary_anisotropy_q15 = 0;

    // Spawned coupling particles.
    EwVoxelCouplingParticle particles[EW_VOXEL_COUPLING_PARTICLES_MAX];
    uint32_t particle_count_u32 = 0;


    // Collision/environment constraint inbox for solvers (derived-only, bounded ring).
    static const uint32_t EW_COLLISION_CONSTRAINT_RING_MAX = 32;
    EwCollisionConstraintPacket collision_constraints[EW_COLLISION_CONSTRAINT_RING_MAX];
    uint32_t collision_constraints_head_u32 = 0;
    uint32_t collision_constraints_count_u32 = 0;

    // "Mass leakage" reverse term: influx accumulator (Q32.32).
    int64_t influx_q32_32 = 0;
    uint8_t influx_band_u8 = 0;
    uint8_t influx_pending_u8 = 0;
    uint16_t pad1_u16 = 0;
    uint64_t influx_hash_u64 = 0;

    // Learning coupling proxy (Q0.15) derived from influx (used by operators).
    uint16_t learning_coupling_q15 = 0;
    uint16_t pad2_u16 = 0;

    // Budget/caps.
    uint32_t max_particles_u32 = EW_VOXEL_COUPLING_PARTICLES_MAX;
    uint32_t pad3_u32 = 0;

    uint64_t last_tick_u64 = 0;
};
