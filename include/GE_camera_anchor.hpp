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
};

// Render packet consumed by renderer.
struct EwRenderCameraPacket {
    int32_t pos_xyz_q16_16[3] = {0,0,0};
    int32_t rot_quat_q16_16[4] = {0,0,0,65536};
    int32_t focal_length_mm_q16_16 = (int32_t)(50 * 65536);
    int32_t aperture_f_q16_16 = (int32_t)(28 * 65536 / 10);
    int32_t exposure_ev_q16_16 = 0;
    int64_t focus_distance_m_q32_32 = (int64_t)(5) * (1ll<<32);
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

class SubstrateManager;
class Anchor;

void ge_camera_anchor_tick(SubstrateManager& sm, Anchor& cam_anchor, uint64_t tick_u64);
