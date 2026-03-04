#pragma once
#include <cstdint>

// Planet/object anchor payload for cosmological-scale bodies.
// This payload is evolved by the substrate (ancilla update + fanout actuation)
// so that cosmological motion and voxel-field resonances are not computed in the
// renderer/viewport.

// Minimal render-facing packet for large anchored bodies.
// All fields are deterministic fixed-point.
struct EwRenderObjectPacket {
    uint64_t object_id_u64;
    uint32_t anchor_id_u32;
    int32_t pos_q16_16[3];
    int32_t radius_q16_16;
    uint32_t albedo_rgba8;
    uint32_t atmosphere_rgba8;
    int32_t atmosphere_thickness_q16_16;
    int32_t emissive_q16_16;
};

// Planet anchor state (substrate-owned).
struct EwPlanetAnchorState {
    // World position / velocity in meters (Q16.16).
    int32_t pos_q16_16[3];
    int32_t vel_q16_16[3];

    // Physical properties.
    int32_t radius_m_q16_16;
    int32_t mass_kg_q16_16;

    // Resonance observable for the local voxel field (Q0.15).
    // This is an OUTPUT written by the substrate update.
    uint16_t voxel_resonance_q15;
    uint16_t pad0;

    // Cosmological coupling (simple demo orbit model).
    // parent_anchor_id_u32 == 0 means no parent.
    uint32_t parent_anchor_id_u32;

    // Orbit radius in meters (Q32.32) and angular velocity in turns/sec (Q32.32).
    int64_t orbit_radius_m_q32_32;
    int64_t orbit_omega_turns_per_sec_q32_32;

    // Orbit phase angle in turns (Q32.32 in [0,1)).
    int64_t orbit_phase_turns_q32_32;

    // Render-facing material properties.
    uint32_t albedo_rgba8;
    uint32_t atmosphere_rgba8;
    int32_t atmosphere_thickness_m_q16_16;
    int32_t emissive_q16_16;
};
