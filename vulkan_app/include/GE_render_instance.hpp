#pragma once
#include <cstdint>

// Shader-facing instance payload (SSBO), std430-aligned.
// Camera-relative coordinates keep vertex shader cheap.
// Deterministic fixed-point: Q16.16 for spatial values.
struct EwRenderInstance {
    uint64_t object_id_u64 = 0;
    uint32_t anchor_id_u32 = 0;
    uint32_t kind_u32 = 0; // 0=generic, 1=Sun, 2=Earth

    uint32_t albedo_rgba8 = 0xFFFFFFFFu;
    uint32_t atmosphere_rgba8 = 0x00000000u;

    // Padding so the next ivec4 is 16-byte aligned for std430.
    uint32_t _pad_a0_u32 = 0;
    uint32_t _pad_a1_u32 = 0;

    // Camera-relative position (Q16.16), w is padding.
    int32_t rel_pos_q16_16[4] = {0,0,0,0};

    int32_t radius_q16_16 = 0; // physical radius in Q16.16
    int32_t emissive_q16_16 = 0;
    int32_t atmosphere_thickness_q16_16 = 0;

    // View-driven LOD controls (Q16.16)
    // lod_bias: negative values force higher mip detail when close+in-focus.
    int32_t lod_bias_q16_16 = 0;
    int32_t clarity_q16_16 = 0;

    // -----------------------------------------------------------------
    // Emergent realism carrier triple (Spec bundle: 3D carrier encoding)
    // -----------------------------------------------------------------
    // These are the compact, fixed-point carrier values sent to shaders.
    // They represent a bundled neighborhood of anchors (self + deterministic
    // neighbors) so one carrier triple can actuate multiple anchor effects.
    //
    // carrier_x: leak-density proxy (Q16.16 in [0,1])
    // carrier_y: doppler proxy (Q16.16 in [-1,1])
    // carrier_z: harmonic mean magnitude (low 16 bits Q0.15)
    uint32_t carrier_x_u32 = 0;
    uint32_t carrier_y_u32 = 0;
    uint32_t carrier_z_u32 = 0;

    uint64_t tick_u64 = 0;
};
