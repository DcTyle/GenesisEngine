#include "camera_controller.hpp"
#include <cmath>
#include "GE_camera_anchor.hpp"

void ew_input_reset_deltas(EwInputState& s) {
    s.mouse_dx = 0;
    s.mouse_dy = 0;
    s.wheel_delta = 0;
}

void ew_camera_init(EwCamera& cam) {
    cam.pos[0] = 0.f; cam.pos[1] = -3.f; cam.pos[2] = 1.5f;
    cam.rot_quat_q16_16[0] = 0;
    cam.rot_quat_q16_16[1] = 0;
    cam.rot_quat_q16_16[2] = 0;
    cam.rot_quat_q16_16[3] = 65536;

    // Default focus: 1 foot focal distance with a small focus band.
    cam.focal_distance_m = 0.3048f;
    cam.focus_band_m = 0.06f; // ~6 cm
    cam.lod_boost_max = 6.0f;
}
void ew_camera_apply_render_packet(EwCamera& cam, const EwRenderCameraPacket& in) {
    cam.pos[0] = (float)in.pos_xyz_q16_16[0] / 65536.0f;
    cam.pos[1] = (float)in.pos_xyz_q16_16[1] / 65536.0f;
    cam.pos[2] = (float)in.pos_xyz_q16_16[2] / 65536.0f;
    for (int i = 0; i < 4; ++i) cam.rot_quat_q16_16[i] = in.rot_quat_q16_16[i];
    cam.focal_distance_m = (float)in.focus_distance_m_q32_32 / 4294967296.0f;
}
