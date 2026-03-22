#pragma once

#include <cstddef>
#include <cstdint>

// Canonical object-DNA schema for photonic confinement control surfaces.
// These values are deterministic engine-side parameters for the simulation and
// editor, not empirical claims about real-world biology.

#define EW_OBJECT_DNA_SCALAR_FIELDS(X) \
    X(helix_primary_hz_f32, 2.40f) \
    X(helix_secondary_hz_f32, 1.92f) \
    X(helix_pitch_hz_f32, 0.48f) \
    X(helix_phase_rad_f32, 0.0f) \
    X(confinement_center_hz_f32, 3.20f) \
    X(confinement_bandwidth_hz_f32, 0.64f) \
    X(confinement_q_f32, 8.0f) \
    X(existence_gain_f32, 1.0f) \
    X(manifold_coupling_gain_f32, 0.25f)

#define EW_OBJECT_DNA_6DOF_FIELDS(X) \
    X(coupling_tx_f32, 0.025f) \
    X(coupling_ty_f32, 0.025f) \
    X(coupling_tz_f32, 0.025f) \
    X(coupling_rx_f32, 0.010f) \
    X(coupling_ry_f32, 0.010f) \
    X(coupling_rz_f32, 0.010f)

struct EwObjectDna {
#define EW_OBJECT_DNA_DECLARE_FIELD(name, default_value) float name = default_value;
    EW_OBJECT_DNA_SCALAR_FIELDS(EW_OBJECT_DNA_DECLARE_FIELD)
    EW_OBJECT_DNA_6DOF_FIELDS(EW_OBJECT_DNA_DECLARE_FIELD)
#undef EW_OBJECT_DNA_DECLARE_FIELD
};

struct EwObjectDnaDerived {
    float helix_mean_hz_f32 = 0.0f;
    float helix_beat_hz_f32 = 0.0f;
    float confinement_floor_hz_f32 = 0.0f;
    float confinement_ceiling_hz_f32 = 0.0f;
    float confinement_effective_hz_f32 = 0.0f;
    float existence_resonance_hz_f32 = 0.0f;
    float manifold_6dof_hz_f32[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float manifold_6dof_l1_hz_f32 = 0.0f;
};

inline float ew_object_dna_abs_f32(float v) {
    return (v < 0.0f) ? -v : v;
}

inline float ew_object_dna_min_f32(float a, float b) {
    return (a < b) ? a : b;
}

inline float ew_object_dna_max_f32(float a, float b) {
    return (a > b) ? a : b;
}

inline float ew_object_dna_clamp_f32(float v, float lo, float hi) {
    return ew_object_dna_min_f32(ew_object_dna_max_f32(v, lo), hi);
}

inline float ew_object_dna_axis_value_f32(const EwObjectDna& dna, size_t axis_idx) {
    switch (axis_idx) {
        case 0u: return dna.coupling_tx_f32;
        case 1u: return dna.coupling_ty_f32;
        case 2u: return dna.coupling_tz_f32;
        case 3u: return dna.coupling_rx_f32;
        case 4u: return dna.coupling_ry_f32;
        case 5u: return dna.coupling_rz_f32;
        default: return 0.0f;
    }
}

inline EwObjectDnaDerived ew_object_dna_derive(const EwObjectDna& dna) {
    EwObjectDnaDerived out{};
    const float q = ew_object_dna_max_f32(0.25f, dna.confinement_q_f32);
    const float bandwidth = ew_object_dna_abs_f32(dna.confinement_bandwidth_hz_f32);
    out.helix_mean_hz_f32 = 0.5f * (dna.helix_primary_hz_f32 + dna.helix_secondary_hz_f32);
    out.helix_beat_hz_f32 = ew_object_dna_abs_f32(dna.helix_primary_hz_f32 - dna.helix_secondary_hz_f32) +
                            0.5f * ew_object_dna_abs_f32(dna.helix_pitch_hz_f32);
    out.confinement_floor_hz_f32 = dna.confinement_center_hz_f32 - 0.5f * bandwidth;
    out.confinement_ceiling_hz_f32 = dna.confinement_center_hz_f32 + 0.5f * bandwidth;
    out.confinement_effective_hz_f32 = out.helix_mean_hz_f32 + dna.confinement_center_hz_f32 + (out.helix_beat_hz_f32 / q);
    out.existence_resonance_hz_f32 = out.confinement_effective_hz_f32 * ew_object_dna_max_f32(0.0f, dna.existence_gain_f32);

    const float manifold_gain = ew_object_dna_max_f32(0.0f, dna.manifold_coupling_gain_f32);
    for (size_t i = 0; i < 6u; ++i) {
        out.manifold_6dof_hz_f32[i] = out.existence_resonance_hz_f32 * manifold_gain * ew_object_dna_axis_value_f32(dna, i);
        out.manifold_6dof_l1_hz_f32 += ew_object_dna_abs_f32(out.manifold_6dof_hz_f32[i]);
    }
    return out;
}

inline EwObjectDna ew_object_dna_seed_from_geometry(uint64_t object_id_u64,
                                                    uint32_t triangle_count_u32,
                                                    uint32_t vertex_count_u32,
                                                    float extent_x_m_f32,
                                                    float extent_y_m_f32,
                                                    float extent_z_m_f32) {
    EwObjectDna dna{};

    const float sx = ew_object_dna_abs_f32(extent_x_m_f32);
    const float sy = ew_object_dna_abs_f32(extent_y_m_f32);
    const float sz = ew_object_dna_abs_f32(extent_z_m_f32);
    const float max_extent = ew_object_dna_max_f32(sx, ew_object_dna_max_f32(sy, sz));
    const float min_extent = ew_object_dna_max_f32(0.10f, ew_object_dna_min_f32(sx, ew_object_dna_min_f32(sy, sz)));
    const float mean_extent = ew_object_dna_max_f32(0.10f, (sx + sy + sz) / 3.0f);
    const float aspect_ratio = ew_object_dna_clamp_f32(max_extent / min_extent, 1.0f, 8.0f);
    const float topo_density = 1.0f + (float)((triangle_count_u32 % 257u) + (vertex_count_u32 % 193u)) * 0.0015f;
    const float oid_bias = (float)(object_id_u64 & 255ull) / 255.0f;

    dna.helix_primary_hz_f32 = 1.50f + mean_extent * 0.75f + topo_density * 0.20f;
    dna.helix_secondary_hz_f32 = dna.helix_primary_hz_f32 * (0.78f + oid_bias * 0.18f);
    dna.helix_pitch_hz_f32 = 0.12f + aspect_ratio * 0.08f;
    dna.helix_phase_rad_f32 = oid_bias * 6.28318530718f;
    dna.confinement_center_hz_f32 = dna.helix_primary_hz_f32 + dna.helix_secondary_hz_f32 * 0.50f;
    dna.confinement_bandwidth_hz_f32 = 0.25f + mean_extent * 0.15f + ew_object_dna_abs_f32(dna.helix_primary_hz_f32 - dna.helix_secondary_hz_f32) * 0.50f;
    dna.confinement_q_f32 = ew_object_dna_clamp_f32(4.0f + topo_density + aspect_ratio * 0.50f, 4.0f, 24.0f);
    dna.existence_gain_f32 = ew_object_dna_clamp_f32(0.85f + mean_extent * 0.05f, 0.85f, 2.25f);
    dna.manifold_coupling_gain_f32 = ew_object_dna_clamp_f32(0.15f + mean_extent * 0.05f, 0.15f, 0.90f);

    dna.coupling_tx_f32 = ew_object_dna_clamp_f32(0.018f + sx * 0.010f, 0.018f, 0.120f);
    dna.coupling_ty_f32 = ew_object_dna_clamp_f32(0.018f + sy * 0.010f, 0.018f, 0.120f);
    dna.coupling_tz_f32 = ew_object_dna_clamp_f32(0.018f + sz * 0.010f, 0.018f, 0.120f);
    dna.coupling_rx_f32 = ew_object_dna_clamp_f32(0.008f + aspect_ratio * 0.004f, 0.008f, 0.060f);
    dna.coupling_ry_f32 = ew_object_dna_clamp_f32(0.008f + topo_density * 0.003f, 0.008f, 0.060f);
    dna.coupling_rz_f32 = ew_object_dna_clamp_f32(0.008f + (float)((object_id_u64 >> 8) & 63ull) * 0.0005f, 0.008f, 0.060f);

    return dna;
}
