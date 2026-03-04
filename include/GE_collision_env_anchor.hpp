#pragma once

#include <cstdint>
#include <cstring>

#include "GE_collision_constraint_packets.hpp"

// Collision environment anchor: a dedicated, solver-facing inbox for
// deterministic environmental constraint packets.
//
// Contract:
//  - derived-only data (never authoritative simulation state)
//  - bounded ring buffer
//  - deterministic clear-on-tick
//
// This anchor exists so voxel coupling (and future world microprocessors)
// can publish constraints without owning solver-specific inbox storage.

struct EwCollisionEnvAnchorState {
    static const uint32_t EW_COLLISION_CONSTRAINT_RING_MAX = 64;

    EwCollisionConstraintPacket collision_constraints[EW_COLLISION_CONSTRAINT_RING_MAX];
    uint32_t head_u32 = 0;
    uint32_t count_u32 = 0;

    // Compact summaries (Q0.15), for diagnostics.
    uint16_t friction_mean_q15 = 0;
    uint16_t restitution_mean_q15 = 0;

    uint32_t pad0_u32 = 0;
    uint64_t last_tick_u64 = 0;

    inline void clear_for_tick(uint64_t tick_u64) {
        if (last_tick_u64 == tick_u64) return;
        std::memset(collision_constraints, 0, sizeof(collision_constraints));
        head_u32 = 0;
        count_u32 = 0;
        friction_mean_q15 = 0;
        restitution_mean_q15 = 0;
        last_tick_u64 = tick_u64;
    }

    inline void push_packet(const EwCollisionConstraintPacket& pkt) {
        const uint32_t head = head_u32 % EW_COLLISION_CONSTRAINT_RING_MAX;
        collision_constraints[head] = pkt;
        head_u32 = (head + 1u) % EW_COLLISION_CONSTRAINT_RING_MAX;
        if (count_u32 < EW_COLLISION_CONSTRAINT_RING_MAX) count_u32 += 1u;
    }
};
