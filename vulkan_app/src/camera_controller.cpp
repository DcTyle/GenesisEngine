#include "camera_controller.hpp"
#include <cmath>
#include "GE_control_packets.hpp"
#include "GE_camera_anchor.hpp"

static inline float clampf(float v, float a, float b) { return (v < a) ? a : (v > b ? b : v); }

void ew_input_reset_deltas(EwInputState& s) {
    s.mouse_dx = 0;
    s.mouse_dy = 0;
    s.wheel_delta = 0;
}

void ew_camera_init(EwCamera& cam) {
    cam.pos[0] = 0.f; cam.pos[1] = -3.f; cam.pos[2] = 1.5f;
    cam.yaw_rad = 0.f;
    cam.pitch_rad = 0.f;
    cam.focus[0] = 0.f; cam.focus[1] = 0.f; cam.focus[2] = 0.f;
    cam.orbit_distance = 3.5f;
    cam.orbit_mode = false;
    cam.fly_mode = false;

    // Default focus: 1 foot focal distance with a small focus band.
    cam.focal_distance_m = 0.3048f;
    cam.focus_band_m = 0.06f; // ~6 cm
    cam.lod_boost_max = 6.0f;
}

void ew_camera_get_forward(const EwCamera& cam, float out_fwd3[3]) {
    const float cy = std::cos(cam.yaw_rad);
    const float sy = std::sin(cam.yaw_rad);
    const float cp = std::cos(cam.pitch_rad);
    const float sp = std::sin(cam.pitch_rad);
    // Forward in UE-style: X forward, Y right, Z up (we adopt X forward, Y right).
    out_fwd3[0] = cy * cp;
    out_fwd3[1] = sy * cp;
    out_fwd3[2] = sp;
}

static void basis_from_yaw_pitch(float yaw, float pitch, float out_fwd[3], float out_right[3], float out_up[3]) {
    float fwd[3];
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    fwd[0] = cy * cp;
    fwd[1] = sy * cp;
    fwd[2] = sp;

    // right = normalize(cross(fwd, world_up)) with world_up=(0,0,1)
    float right[3] = { fwd[1], -fwd[0], 0.f };
    const float rl = std::sqrt(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
    if (rl > 1e-6f) { right[0]/=rl; right[1]/=rl; right[2]/=rl; }

    // up = cross(right, fwd)
    float up[3] = {
        right[1]*fwd[2] - right[2]*fwd[1],
        right[2]*fwd[0] - right[0]*fwd[2],
        right[0]*fwd[1] - right[1]*fwd[0]
    };

    out_fwd[0]=fwd[0]; out_fwd[1]=fwd[1]; out_fwd[2]=fwd[2];
    out_right[0]=right[0]; out_right[1]=right[1]; out_right[2]=right[2];
    out_up[0]=up[0]; out_up[1]=up[1]; out_up[2]=up[2];
}

void ew_camera_tick(EwCamera& cam, EwInputState& in, float dt_seconds) {
    // Mode selection approximating Unreal viewport:
    // Alt+LMB orbit, Alt+MMB pan, Alt+RMB dolly.
    // RMB without Alt enables fly (WASD).

    const bool alt = in.alt;

    const bool orbit = alt && in.lmb;
    const bool pan = alt && in.mmb;
    const bool dolly = alt && in.rmb;

    cam.orbit_mode = orbit;
    cam.fly_mode = (!alt) && in.rmb;

    const float rot_speed = 0.005f;
    const float pan_speed = 0.004f * cam.orbit_distance;
    const float dolly_speed = 0.01f * cam.orbit_distance;

    if (orbit) {
        cam.yaw_rad += (float)in.mouse_dx * rot_speed;
        cam.pitch_rad += (float)(-in.mouse_dy) * rot_speed;
        cam.pitch_rad = clampf(cam.pitch_rad, -1.55f, 1.55f);
    }

    float fwd[3], right[3], up[3];
    basis_from_yaw_pitch(cam.yaw_rad, cam.pitch_rad, fwd, right, up);

    if (pan) {
        cam.focus[0] += (-in.mouse_dx) * pan_speed * right[0] + (in.mouse_dy) * pan_speed * up[0];
        cam.focus[1] += (-in.mouse_dx) * pan_speed * right[1] + (in.mouse_dy) * pan_speed * up[1];
        cam.focus[2] += (-in.mouse_dx) * pan_speed * right[2] + (in.mouse_dy) * pan_speed * up[2];
    }

    if (dolly) {
        cam.orbit_distance += (float)in.mouse_dy * dolly_speed;
        cam.orbit_distance = clampf(cam.orbit_distance, 0.2f, 500.f);
    }

    if (in.wheel_delta != 0) {
        // Wheel dolly in/out.
        cam.orbit_distance *= (in.wheel_delta > 0) ? 0.9f : 1.1f;
        cam.orbit_distance = clampf(cam.orbit_distance, 0.2f, 500.f);
    }

    if (cam.fly_mode) {
        float speed = in.shift ? 8.0f : 3.0f;
        speed *= dt_seconds;

        if (in.key_down['W']) { cam.pos[0] += fwd[0]*speed; cam.pos[1] += fwd[1]*speed; cam.pos[2] += fwd[2]*speed; }
        if (in.key_down['S']) { cam.pos[0] -= fwd[0]*speed; cam.pos[1] -= fwd[1]*speed; cam.pos[2] -= fwd[2]*speed; }
        if (in.key_down['A']) { cam.pos[0] -= right[0]*speed; cam.pos[1] -= right[1]*speed; cam.pos[2] -= right[2]*speed; }
        if (in.key_down['D']) { cam.pos[0] += right[0]*speed; cam.pos[1] += right[1]*speed; cam.pos[2] += right[2]*speed; }
        if (in.key_down['Q']) { cam.pos[2] -= speed; }
        if (in.key_down['E']) { cam.pos[2] += speed; }

        cam.yaw_rad += (float)in.mouse_dx * rot_speed;
        cam.pitch_rad += (float)(-in.mouse_dy) * rot_speed;
        cam.pitch_rad = clampf(cam.pitch_rad, -1.55f, 1.55f);

        // Keep focus in front of camera so orbit snaps to current view when Alt is pressed.
        cam.focus[0] = cam.pos[0] + fwd[0]*cam.orbit_distance;
        cam.focus[1] = cam.pos[1] + fwd[1]*cam.orbit_distance;
        cam.focus[2] = cam.pos[2] + fwd[2]*cam.orbit_distance;
    } else {
        // Orbit mode position derived from focus and distance.
        cam.pos[0] = cam.focus[0] - fwd[0]*cam.orbit_distance;
        cam.pos[1] = cam.focus[1] - fwd[1]*cam.orbit_distance;
        cam.pos[2] = cam.focus[2] - fwd[2]*cam.orbit_distance;
    }

    // Clear per-frame deltas.
    ew_input_reset_deltas(in);
}

static void quat_from_yaw_pitch(float yaw, float pitch, int32_t out_q16_16[4]) {
    // Yaw around Z, pitch around X. Deterministic float math is acceptable for UI.
    const float hy = yaw * 0.5f;
    const float hp = pitch * 0.5f;
    const float cy = std::cos(hy);
    const float sy = std::sin(hy);
    const float cp = std::cos(hp);
    const float sp = std::sin(hp);
    // q = q_yaw * q_pitch
    const float qw = cy*cp;
    const float qx = cy*sp;
    const float qy = sy*sp;
    const float qz = sy*cp;
    out_q16_16[0] = (int32_t)(qx * 65536.0f);
    out_q16_16[1] = (int32_t)(qy * 65536.0f);
    out_q16_16[2] = (int32_t)(qz * 65536.0f);
    out_q16_16[3] = (int32_t)(qw * 65536.0f);
}

static void yaw_pitch_from_quat_q16_16(const int32_t q[4], float& out_yaw, float& out_pitch) {
    const float qx = (float)q[0] / 65536.0f;
    const float qy = (float)q[1] / 65536.0f;
    const float qz = (float)q[2] / 65536.0f;
    const float qw = (float)q[3] / 65536.0f;
    // yaw (Z) and pitch (X) from quaternion (approx)
    const float sinp = 2.0f * (qw*qx - qy*qz);
    out_pitch = std::asin(clampf(sinp, -1.0f, 1.0f));
    const float siny = 2.0f * (qw*qz + qx*qy);
    const float cosy = 1.0f - 2.0f * (qx*qx + qz*qz);
    out_yaw = std::atan2(siny, cosy);
}

void ew_camera_fill_control_packet_from_camera(const EwCamera& cam, EwControlPacket& out) {
    out = EwControlPacket{};
    out.kind = EwControlPacketKind::CameraSet;
    out.payload.camera_set.focus_mode_u8 = (uint8_t)EwFocusMode::ManualDistance;
    out.payload.camera_set.manual_focus_distance_m_q32_32 = (int64_t)(cam.focal_distance_m * 4294967296.0f);
    out.payload.camera_set.focal_length_mm_q16_16 = (int32_t)(50 * 65536);
    out.payload.camera_set.aperture_f_q16_16 = (int32_t)(28 * 65536 / 10);
    out.payload.camera_set.exposure_ev_q16_16 = 0;
    out.payload.camera_set.pos_xyz_q16_16[0] = (int32_t)(cam.pos[0] * 65536.0f);
    out.payload.camera_set.pos_xyz_q16_16[1] = (int32_t)(cam.pos[1] * 65536.0f);
    out.payload.camera_set.pos_xyz_q16_16[2] = (int32_t)(cam.pos[2] * 65536.0f);
    quat_from_yaw_pitch(cam.yaw_rad, cam.pitch_rad, out.payload.camera_set.rot_quat_q16_16);
}

void ew_camera_apply_render_packet(EwCamera& cam, const EwRenderCameraPacket& in) {
    cam.pos[0] = (float)in.pos_xyz_q16_16[0] / 65536.0f;
    cam.pos[1] = (float)in.pos_xyz_q16_16[1] / 65536.0f;
    cam.pos[2] = (float)in.pos_xyz_q16_16[2] / 65536.0f;
    yaw_pitch_from_quat_q16_16(in.rot_quat_q16_16, cam.yaw_rad, cam.pitch_rad);
    cam.focal_distance_m = (float)in.focus_distance_m_q32_32 / 4294967296.0f;
}
