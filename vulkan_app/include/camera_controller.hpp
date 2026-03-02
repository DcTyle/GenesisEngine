#pragma once
#include <cstdint>
#include <windows.h>

struct EwCamera {
    float pos[3];
    float yaw_rad;
    float pitch_rad;
    float focus[3];
    float orbit_distance;
    bool orbit_mode;
    bool fly_mode;

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
void ew_camera_tick(EwCamera& cam, EwInputState& in, float dt_seconds);

// Deterministic view basis outputs for renderer state.
void ew_camera_get_forward(const EwCamera& cam, float out_fwd3[3]);
