#pragma once

#include <cstdint>

// Derived-only collision/environment constraint packet produced by substrate anchors.
// This is NOT a solver; it is an inbox contract for a future/current collision system.
// All fields are fixed-point and bounded.

enum EwCollisionConstraintKind : uint8_t {
    EW_COLLISION_CONSTRAINT_KIND_ENV = 1,
};

struct EwCollisionConstraintPacket {
    uint64_t object_id_u64 = 0;

    // Environmental coefficients (Q0.15)
    uint16_t friction_q15 = 0;
    uint16_t restitution_q15 = 0;

    // Boundary coupling strength (Q0.15)
    uint16_t boundary_strength_q15 = 0;

    // Boundary conditions
    uint8_t no_slip_u8 = 0;

    // Permeability (Q0.15)
    uint16_t permeability_q15 = 0;

    uint8_t kind_u8 = (uint8_t)EW_COLLISION_CONSTRAINT_KIND_ENV;

    uint8_t pad0_u8 = 0;
    uint16_t pad1_u16 = 0;
};
