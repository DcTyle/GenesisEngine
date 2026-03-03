#pragma once

#include <cstdint>

enum class EwControlPacketKind : uint16_t {
    Invalid = 0,
    InputAction = 1,
    InputAxis = 2,
    InputBindingSet = 3,
    InputBindingsReload = 4,
    CameraSet = 10,
    CameraSetFocusMode = 11,
    ProjectSettingsSet = 20,
    AllowlistReload = 30,
    AllowlistUpdateInline = 31,
};

struct EwControlPacket {
    EwControlPacketKind kind = EwControlPacketKind::Invalid;
    uint16_t source_u16 = 0; // 0 runtime, 1 editor
    uint32_t pad0_u32 = 0;
    uint64_t tick_u64 = 0;
    union {
        struct {
            uint32_t action_id_u32;
            uint8_t pressed_u8;
            uint8_t pad_u8[3];
        } input_action;
        struct {
            uint32_t axis_id_u32;
            int32_t value_q16_16;
        } input_axis;
        struct {
            uint8_t focus_mode_u8;
            uint8_t pad_u8[3];
            int64_t manual_focus_distance_m_q32_32;
            int32_t focal_length_mm_q16_16;
            int32_t aperture_f_q16_16;
            int32_t exposure_ev_q16_16;
            int32_t pos_xyz_q16_16[3];
            int32_t rot_quat_q16_16[4];
        } camera_set;
        struct {
            uint32_t tab_u32;
            uint32_t field_u32;
            int64_t value_q32_32;
        } settings_set;
        struct {
            uint8_t is_axis_u8; // 0 action binding, 1 axis binding
            uint8_t pad_u8[3];
            uint32_t raw_id_u32;
            uint32_t mapped_u32;
            int32_t scale_q16_16;
        } binding_set;
        struct {
            uint64_t request_id_u64;
        } allowlist_inline;
    } payload;
};
