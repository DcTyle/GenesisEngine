#pragma once
#include <cstddef>
#include <cstdint>

enum EwRenderKind : uint32_t {
    EW_RENDER_KIND_GENERIC = 0u,
    EW_RENDER_KIND_SUN = 1u,
    EW_RENDER_KIND_EARTH = 2u,
    EW_RENDER_KIND_RESEARCH_PHOTON = 3u,
    EW_RENDER_KIND_RESEARCH_WEIGHTED = 4u,
    EW_RENDER_KIND_RESEARCH_FLAVOR = 5u,
    EW_RENDER_KIND_RESEARCH_CHARGED = 6u,
    EW_RENDER_KIND_LATTICE_FIELD = 7u,
    EW_RENDER_KIND_LATTICE_ECHO = 8u,
};

// Shader-facing instance payload (SSBO), std430-aligned.
// This is the canonical CPU/GPU render contract for instanced billboards.
// Camera-relative coordinates keep vertex shader cheap.
// Deterministic fixed-point: Q16.16 for spatial values.
struct EwRenderInstance {
    uint32_t anchor_id_u32 = 0;
    uint32_t kind_u32 = EW_RENDER_KIND_GENERIC;

    uint32_t albedo_rgba8 = 0xFFFFFFFFu;
    uint32_t atmosphere_rgba8 = 0x00000000u;

    int32_t radius_q16_16 = 0; // physical radius in Q16.16
    int32_t emissive_q16_16 = 0;
    int32_t atmosphere_thickness_q16_16 = 0;

    // View-driven LOD controls (Q16.16)
    // lod_bias: negative values force higher mip detail when close+in-focus.
    int32_t lod_bias_q16_16 = 0;

    // Camera-relative position (Q16.16), w is padding.
    int32_t rel_pos_q16_16[4] = {0,0,0,0};

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
};

static_assert(offsetof(EwRenderInstance, rel_pos_q16_16) == 32, "EwRenderInstance rel_pos_q16_16 must stay std430-aligned.");
static_assert(sizeof(EwRenderInstance) == 64, "EwRenderInstance must remain a 64-byte canonical GPU payload.");
