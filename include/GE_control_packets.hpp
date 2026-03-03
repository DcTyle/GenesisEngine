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
    ObjectRegister = 40,
    ObjectSetTransform = 41,
    PlanetRegister = 42,

    EditorSetSelection = 50,
    EditorSetGizmo = 51,
    EditorSetSnap = 52,
    EditorToggleSelection = 53,
    EditorSetAxisConstraint = 54,
    EditorCommitTransformTxn = 55,
    EditorUndo = 56,
    EditorRedo = 57,
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
            uint64_t object_id_u64;
            uint32_t kind_u32;
            uint32_t pad0_u32;
            int32_t pos_q16_16[3];
            int32_t rot_quat_q16_16[4];
            int32_t radius_m_q16_16;
            uint32_t albedo_rgba8;
            uint32_t atmosphere_rgba8;
            int32_t atmosphere_thickness_m_q16_16;
            int32_t emissive_q16_16;
        } object_register;
        struct {
            uint64_t object_id_u64;
            uint32_t pad0_u32;
            uint32_t pad1_u32;
            int32_t pos_q16_16[3];
            int32_t rot_quat_q16_16[4];
        } object_set_transform;

        struct {
            uint64_t selected_object_id_u64;
        } editor_set_selection;

        struct {
            uint8_t gizmo_mode_u8;
            uint8_t gizmo_space_u8;
            uint8_t pad_u8[2];
        } editor_set_gizmo;

        struct {
            uint8_t snap_enabled_u8;
            uint8_t pad_u8[3];
            int32_t grid_step_m_q16_16;
            int32_t angle_step_deg_q16_16;
        } editor_set_snap;

struct {
    uint64_t object_id_u64;
} editor_toggle_selection;

struct {
    uint8_t axis_constraint_u8; // 0 none, 1 X, 2 Y, 3 Z
    uint8_t pad_u8[3];
} editor_set_axis_constraint;

struct {
    uint64_t object_id_u64;
    int32_t before_pos_q16_16[3];
    int32_t before_rot_q16_16[4];
    int32_t after_pos_q16_16[3];
    int32_t after_rot_q16_16[4];
} editor_commit_transform_txn;

struct {
    uint32_t pad_u32;
} editor_undo;

struct {
    uint32_t pad_u32;
} editor_redo;

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
struct {
            uint64_t object_id_u64;
            int32_t pos_q16_16[3];
            int32_t rot_quat_q16_16[4];
        } object_register;

        struct {
            uint64_t object_id_u64;
            int32_t pos_q16_16[3];
            int32_t rot_quat_q16_16[4];
        } object_xform;

        // Planet anchors are authoritative cosmological bodies evolved by ancilla.
        // parent_object_id_u64 binds orbit coupling without exposing anchor ids.
        struct {
            uint64_t object_id_u64;
            uint64_t parent_object_id_u64;
            int32_t pos_q16_16[3];
            int32_t vel_q16_16[3];
            int32_t radius_m_q16_16;
            int32_t mass_kg_q16_16;
            int64_t orbit_radius_m_q32_32;
            int64_t orbit_omega_turns_per_sec_q32_32;
            int64_t orbit_phase_turns_q32_32;
            uint32_t albedo_rgba8;
            uint32_t atmosphere_rgba8;
            int32_t atmosphere_thickness_m_q16_16;
            int32_t emissive_q16_16;
        } planet_register;
    } payload;
};
