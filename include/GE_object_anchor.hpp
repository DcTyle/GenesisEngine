#pragma once
#include <cstdint>

// Object anchor payload (substrate-owned authoritative transform).
// All fields are deterministic fixed-point; no renderer-owned truth.
struct EwObjectAnchorState {
    // World position in meters (Q16.16).
    int32_t pos_q16_16[3];
    int32_t pad0_i32;

    // Orientation quaternion (x,y,z,w) in Q16.16 (unit length expected).
    int32_t rot_quat_q16_16[4];

    // Render-facing scalar parameters.
    int32_t radius_m_q16_16;
    int32_t atmosphere_thickness_m_q16_16;
    int32_t emissive_q16_16;
    int32_t pad1_i32;

    uint32_t albedo_rgba8;
    uint32_t atmosphere_rgba8;

    uint32_t pad2_u32;
    uint32_t pad3_u32;

    EwObjectAnchorState() {
        pos_q16_16[0] = 0; pos_q16_16[1] = 0; pos_q16_16[2] = 0; pad0_i32 = 0;
        rot_quat_q16_16[0] = 0; rot_quat_q16_16[1] = 0; rot_quat_q16_16[2] = 0; rot_quat_q16_16[3] = 65536;
        radius_m_q16_16 = (int32_t)(1 * 65536);
        atmosphere_thickness_m_q16_16 = 0;
        emissive_q16_16 = 0;
        pad1_i32 = 0;
        albedo_rgba8 = 0xFFFFFFFFu;
        atmosphere_rgba8 = 0x00000000u;
        pad2_u32 = 0u;
        pad3_u32 = 0u;
    }
};
