#pragma once
#include <cstdint>
#include <windows.h>

struct EwCamera {
    float pos[3];
    // Orientation is consumed directly as a quaternion from the substrate.
    int32_t rot_quat_q16_16[4];

    // Depth-of-field / focus controls (meters). Used to drive view-dependent LOD.
    float focal_distance_m;
    float focus_band_m;
    float lod_boost_max; // maximum negative mip bias (mip levels)
};

struct EwInputState {
    bool key_down[256];
    bool lmb;
    bool rmb;
    bool mmb;
    bool alt;
    bool shift;
    bool ctrl;
    int32_t mouse_x;
    int32_t mouse_y;
    int32_t mouse_dx;
    int32_t mouse_dy;
    int32_t wheel_delta;
};

void ew_input_reset_deltas(EwInputState& s);

void ew_camera_init(EwCamera& cam);
struct EwRenderCameraPacket;
void ew_camera_apply_render_packet(EwCamera& cam, const EwRenderCameraPacket& in);
