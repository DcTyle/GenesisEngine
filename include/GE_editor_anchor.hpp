#pragma once

#include <cstdint>

// Editor state lives in the substrate as an anchor payload.
// It is authoritative for editor interaction contracts (selection/gizmo/snap),
// and is updated ONLY via control packets.
//
// Hard rule: renderer never reads UI widgets directly; it consumes projected
// packets and/or debug overlays derived from anchors.

enum class EwGizmoMode : uint8_t {
    None = 0,
    Translate = 1,
    Rotate = 2,
};

enum class EwGizmoSpace : uint8_t {
    World = 0,
    Local = 1,
};

static const uint32_t EW_EDITOR_MAX_SELECTION = 16u;
static const uint32_t EW_EDITOR_UNDO_DEPTH = 16u;
static const uint32_t EW_EDITOR_SPECTRAL_VIZ_SAMPLES = 16u;
static const uint32_t EW_EDITOR_BOUNDARY_VIZ_SAMPLES = 16u;

// Read-only, derived visualization samples for the viewport.
// Hard rule: these fields are NEVER used as authoritative simulation inputs.
struct EwEditorSpectralVizSample {
    // Sample position in world space (Q16.16)
    int32_t pos_q16_16[3] = {0,0,0};
    // Field amplitude proxy in Q1.15.
    int16_t field_q1_15 = 0;
    // Field gradient magnitude proxy in Q1.15.
    int16_t grad_q1_15 = 0;
    // Coherence band observed at sampling time (0..255)
    uint8_t band_u8 = 0;
    uint8_t pad_u8[3] = {0,0,0};
};

// Read-only, derived boundary visualization samples for the viewport.
// Hard rule: these fields are NEVER used as authoritative simulation inputs.
struct EwEditorBoundaryVizSample {
    int32_t pos_q16_16[3] = {0,0,0};
    uint16_t boundary_strength_q15 = 0;
    uint16_t permeability_q15 = 0;
    uint8_t boundary_normal_u8 = 0;
    uint8_t no_slip_u8 = 0;
    uint8_t color_band_u8 = 0;
    uint8_t pad_u8 = 0;
};

struct EwEditorTransformTxn {
    uint64_t object_id_u64 = 0;
    int32_t before_pos_q16_16[3] = {0,0,0};
    int32_t before_rot_q16_16[4] = {0,0,0, (int32_t)(1 * 65536)};
    int32_t after_pos_q16_16[3]  = {0,0,0};
    int32_t after_rot_q16_16[4]  = {0,0,0, (int32_t)(1 * 65536)};
};

struct EwEditorAnchorState {
    // Primary selection (object_id, not anchor_id). Kept for fast-paths.
    uint64_t selected_object_id_u64 = 0;

    // Multi-selection list (deterministic, bounded).
    uint32_t selection_count_u32 = 0;
    uint32_t selection_pad0_u32 = 0;
    uint64_t selection_object_id_u64[EW_EDITOR_MAX_SELECTION] = {0};

    uint8_t gizmo_mode_u8 = static_cast<uint8_t>(EwGizmoMode::Translate);
    uint8_t gizmo_space_u8 = static_cast<uint8_t>(EwGizmoSpace::World);
    uint8_t snap_enabled_u8 = 0;

    // Axis constraint: 0 none, 1 X, 2 Y, 3 Z.
    uint8_t axis_constraint_u8 = 0;

    // Grid snap in meters (Q16.16)
    int32_t grid_step_m_q16_16 = (int32_t)(1 * 65536);

    // Angle snap in degrees (Q16.16)
    int32_t angle_step_deg_q16_16 = (int32_t)(15 * 65536);

    // Undo/redo stacks as deterministic bounded arrays of transform transactions.
    uint32_t undo_count_u32 = 0;
    uint32_t redo_count_u32 = 0;
    uint32_t undo_redo_pad_u32[2] = {0,0};
    EwEditorTransformTxn undo_stack[EW_EDITOR_UNDO_DEPTH] = {};
    EwEditorTransformTxn redo_stack[EW_EDITOR_UNDO_DEPTH] = {};

    // Derived spectral visualization samples around the selected object.
    uint32_t spectral_viz_count_u32 = 0;
    uint32_t spectral_viz_pad0_u32 = 0;
    EwEditorSpectralVizSample spectral_viz[EW_EDITOR_SPECTRAL_VIZ_SAMPLES] = {};

    // Derived boundary visualization samples around the selected object.
    uint32_t boundary_viz_count_u32 = 0;
    uint32_t boundary_viz_pad0_u32 = 0;
    EwEditorBoundaryVizSample boundary_viz[EW_EDITOR_BOUNDARY_VIZ_SAMPLES] = {};
};


