#pragma once

#include <cstdint>

enum class EwFocusMode : uint8_t {
    ManualDistance = 0,
    CenterRay = 1,
    MedianDepth = 2,
    SubjectLock = 3,
    WeightedSaliency = 4,
};

// Camera state lives only in the AI substrate as an anchor payload.
struct EwCameraAnchorState {
    uint8_t focus_mode_u8 = static_cast<uint8_t>(EwFocusMode::ManualDistance);
    uint8_t pad0_u8[3] = {0,0,0};

    int64_t manual_focus_distance_m_q32_32 = (int64_t)(5) * (1ll<<32);

    int32_t focal_length_mm_q16_16 = (int32_t)(50 * 65536);
    int32_t aperture_f_q16_16 = (int32_t)(28 * 65536 / 10);
    int32_t exposure_ev_q16_16 = 0;

    // Camera pose (camera space). Updated via control packets only.
    int32_t pos_xyz_q16_16[3] = {0,0,(int32_t)(-5 * 65536)};
    int32_t rot_quat_q16_16[4] = {0,0,0,65536};

    int32_t tau_seconds_q16_16 = (int32_t)(0.25f * 65536.0f);
    int32_t max_refocus_vel_mps_q16_16 = (int32_t)(5.0f * 65536.0f);
    int32_t deadband_m_q16_16 = (int32_t)(0.02f * 65536.0f);

    int64_t focus_distance_m_q32_32 = (int64_t)(5) * (1ll<<32);

    // Derived audio-environment observables (Q1.15).
    // Hard rule: these are read-only projections computed by the substrate;
    // they must never become authoritative state drivers.
    int16_t audio_env_field_q1_15 = 0;
    int16_t audio_env_grad_q1_15 = 0;
    uint16_t audio_env_coherence_q15 = 0;
    // Derived "color band" proxy (0..7) from absorption/emission semantics.
    // This is a read-only projection; renderer/audio may map band->false color.
    uint8_t color_band_u8 = 0;
    uint8_t color_r_u8 = 0;
    uint8_t color_g_u8 = 0;
    uint8_t color_b_u8 = 0;
    uint8_t audio_eq_preset_u8 = 0;
    uint8_t audio_reverb_preset_u8 = 0;
    uint8_t audio_occlusion_preset_u8 = 0;
    uint8_t pad_audio_u8[1] = {0};
};

// Render packet consumed by renderer.
struct EwRenderCameraPacket {
    int32_t pos_xyz_q16_16[3] = {0,0,0};
    int32_t rot_quat_q16_16[4] = {0,0,0,65536};
    int32_t focal_length_mm_q16_16 = (int32_t)(50 * 65536);
    int32_t aperture_f_q16_16 = (int32_t)(28 * 65536 / 10);
    int32_t exposure_ev_q16_16 = 0;
    int64_t focus_distance_m_q32_32 = (int64_t)(5) * (1ll<<32);

    // Derived audio-environment observables (Q1.15).
    // Hard rule: these are read-only projections computed by the substrate;
    // they must never become authoritative state drivers.
    int16_t audio_env_field_q1_15 = 0;
    int16_t audio_env_grad_q1_15 = 0;
    uint16_t audio_env_coherence_q15 = 0;
    uint8_t color_band_u8 = 0;
    uint8_t color_r_u8 = 0;
    uint8_t color_g_u8 = 0;
    uint8_t color_b_u8 = 0;
    uint8_t audio_eq_preset_u8 = 0;
    uint8_t audio_reverb_preset_u8 = 0;
    uint8_t audio_occlusion_preset_u8 = 0;
    uint8_t pad_audio_u8[1] = {0};
    uint8_t focus_mode_u8 = static_cast<uint8_t>(EwFocusMode::ManualDistance);
    uint8_t pad0_u8[7] = {0,0,0,0,0,0,0};

    // View matrix in Q16.16 (row-major 4x4). This is a projection of camera anchor
    // state produced by the substrate. Renderer consumes it directly.
    int32_t view_mat_q16_16[16] = {
        65536,0,0,0,
        0,65536,0,0,
        0,0,65536,0,
        0,0,0,65536
    };
};

// Render assist packet: derived values computed inside substrate for the renderer.
// Renderer should avoid computing focus/LOD heuristics locally; it consumes
// these pre-derived coefficients and uses only simple multiply-add + clamps.
struct EwRenderAssistPacket {
    // Focus distance and band in meters (Q32.32)
    int64_t focus_distance_m_q32_32 = (int64_t)(5) * (1ll<<32);

    // Derived audio-environment observables (Q1.15).
    // Hard rule: these are read-only projections computed by the substrate;
    // they must never become authoritative state drivers.
    int16_t audio_env_field_q1_15 = 0;
    int16_t audio_env_grad_q1_15 = 0;
    uint16_t audio_env_coherence_q15 = 0;
    uint8_t color_band_u8 = 0;
    uint8_t color_r_u8 = 0;
    uint8_t color_g_u8 = 0;
    uint8_t color_b_u8 = 0;
    uint8_t audio_eq_preset_u8 = 0;
    uint8_t audio_reverb_preset_u8 = 0;
    uint8_t audio_occlusion_preset_u8 = 0;
    uint8_t pad_audio_u8[1] = {0};
    int64_t focus_band_m_q32_32 = (int64_t)(0.06 * (double)(1ull<<32));

    // Squared near/far focus band bounds in meters^2 (Q32.32)
    uint64_t focus_near_m2_q32_32 = 0;
    uint64_t focus_far_m2_q32_32 = 0;

    // inv_range in squared-distance domain (Q16.16): 1/(far2-near2)
    int32_t inv_focus_range_m2_q16_16 = 0;

    // Near boost window in meters^2 (Q32.32)
    uint64_t near_min_m2_q32_32 = 0;
    uint64_t near_max_m2_q32_32 = 0;
    int32_t inv_near_range_m2_q16_16 = 0;

    // Screen proxy scale (Q16.16): approx (radius/dist) * scale
    int32_t screen_proxy_scale_q16_16 = (int32_t)(8 * 65536);
    // LOD boost maximum (Q16.16)
    int32_t lod_boost_max_q16_16 = (int32_t)(2 * 65536);
};

// XR per-eye projected view packet (Q16.16).
// Renderer consumes this directly; it must not normalize basis from raw poses.
struct EwRenderXrEyePacket {
    int32_t view_mat_q16_16[16] = {0};
    uint64_t tick_u64 = 0;
};

class SubstrateManager;
class Anchor;

void ge_camera_anchor_tick(SubstrateManager& sm, Anchor& cam_anchor, uint64_t tick_u64);
