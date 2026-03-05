#pragma once

#include <cstdint>

// -----------------------------------------------------------------------------
//  AI Action Log (Spec/Blueprint: observable action ledger)
// -----------------------------------------------------------------------------
// The AI is considered "complete and testable" only when it emits explicit,
// deterministic action events that can be verified by a harness without UE.
//
// Design rules:
//   - Fixed-size, deterministic ordering.
//   - No IO here; host tools can serialize.
//   - Coordinate coord-tags (sig9_u64) identify the decision context.
//
// Action kinds are intentionally small and explicit.

enum EwAiActionKind : uint16_t {
    EW_AI_ACTION_NONE = 0,
    // Internal control surface change (frame-gamma shift).
    EW_AI_ACTION_FRAME_GAMMA_ADJUST = 1,
    // Emitted an internal pulse through the standard admission path.
    EW_AI_ACTION_PULSE_EMIT = 2,
    // Wrote or updated a workspace artifact in substrate inspector fields.
    EW_AI_ACTION_ARTIFACT_WRITE = 3,
};

struct EwAiActionEvent {
    uint64_t tick_u64;
    uint64_t sig9_u64;             // decision context coord-tag
    uint32_t class_id_u32;
    uint16_t kind_u16;             // EwAiActionKind
    uint16_t profile_id_u16;       // EwDeltaProfileId (when kind == PULSE_EMIT)

    uint32_t target_anchor_id_u32; // 0 if none
    int32_t  f_code_i32;           // f_code for pulse (when kind == PULSE_EMIT)
    uint32_t a_code_u32;           // a_code for pulse (when kind == PULSE_EMIT)
    uint32_t v_code_u32;           // v_code for pulse (when kind == PULSE_EMIT)
    uint32_t i_code_u32;           // i_code for pulse (when kind == PULSE_EMIT)

    int64_t  confidence_q32_32;
    int64_t  attractor_strength_q32_32;
    int64_t  frame_gamma_turns_q;  // resulting gamma after adjust (or current)

    // Artifact metadata (when kind == ARTIFACT_WRITE). These are deterministic
    // bookkeeping signals; they are stable identifiers.
    uint64_t artifact_coord_sig9_u64 = 0;
    uint32_t artifact_kind_u32 = 0;
    uint32_t artifact_path_code_u32 = 0;
};
