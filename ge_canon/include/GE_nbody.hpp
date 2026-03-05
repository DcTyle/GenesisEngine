#pragma once
#include <cstdint>

class SubstrateManager;

namespace genesis {

// Fixed-point N-body integrator state (deterministic, CPU reference).
// Units:
//  - positions: meters Q32.32
//  - velocities: meters/second Q32.32
//  - masses: kg Q32.32
//  - G: m^3/(kg*s^2) Q32.32
//  - dt: seconds Q32.32
struct EwNBodyBody {
    uint64_t object_id_u64 = 0;
    uint32_t planet_anchor_id_u32 = 0; // resolved anchor id for projection
    uint32_t pad0_u32 = 0;

    int64_t pos_m_q32_32[3] = {0,0,0};
    int64_t vel_mps_q32_32[3] = {0,0,0};

    int64_t mass_kg_q32_32 = 0;
    int64_t radius_m_q32_32 = 0;

    uint32_t albedo_rgba8 = 0xFFFFFFFFu;
    uint32_t atmosphere_rgba8 = 0u;
    int64_t atmosphere_thickness_m_q32_32 = 0;
    int64_t emissive_q32_32 = 0;
};

struct EwNBodyState {
    uint32_t enabled_u32 = 0;
    uint32_t initialized_u32 = 0;
    uint32_t body_count_u32 = 0;
    uint32_t pad_u32 = 0;

    int64_t G_q32_32 = 0;
    int64_t dt_seconds_q32_32 = 0;

    static constexpr uint32_t MAX_BODIES = 8;
    EwNBodyBody bodies[MAX_BODIES];
};

void ew_nbody_init_default(EwNBodyState* st);
void ew_nbody_tick(SubstrateManager* sm);

} // namespace genesis
