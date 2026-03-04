#pragma once

#include <cstdint>

// Coherence bus packets are tiny, hashable, and deterministic.
// They are NOT network packets; they are substrate-internal routing artifacts.

enum class EwCoherenceSuggestedAction : uint8_t {
    None = 0,
    AdjustDt = 1,
    AdjustViscosity = 2,
    ResyncPhase = 3,
    AdjustFanoutBudget = 4,
    FreezeTick = 5,
    AdjustLearning = 6,
    OperatorReplace = 7,
};

enum class EwCoherenceHookOp : uint8_t {
    None = 0,
    HookAdjustDt = 1,
    HookAdjustViscosity = 2,
    HookResyncPhase = 3,
    HookFanoutBudget = 4,
    HookFreezeTick = 5,
    HookAdjustLearning = 6,
    HookOperatorReplace = 7,
};

struct EwLeakagePublishPacket {
    uint32_t src_anchor_id_u32 = 0;
    uint8_t coherence_band_u8 = 0;
    uint8_t suggested_action_u8 = 0;
    uint16_t pad0 = 0;

    // Signed residual in Q32.32.
    int64_t leakage_q32_32 = 0;
    uint64_t payload_hash_u64 = 0;

    // Carrier authority (observables).
    uint16_t v_code_u16 = 0;
    uint16_t i_code_u16 = 0;
    uint32_t pad1 = 0;
};

struct EwHookPacket {
    uint32_t dst_anchor_id_u32 = 0;
    uint8_t hook_op_u8 = 0;
    uint8_t causal_tag_u8 = 0;
    uint16_t authority_q15 = 0;

    // Small parameter payload (signed Q32.32). Interpretation depends on op.
    int64_t p0_q32_32 = 0;
    int64_t p1_q32_32 = 0;
};

// Influx is the "reverse" channel of leakage: a bounded, deterministic proxy for
// energy/authority entering a medium and being absorbed/rewritten into learning
// coupling. It is treated as a signed Q32.32 residual, but routed separately so
// governors can clamp it independently.
struct EwInfluxPublishPacket {
    uint32_t src_anchor_id_u32 = 0;
    uint8_t coherence_band_u8 = 0;
    uint8_t suggested_action_u8 = 0;
    uint16_t pad0 = 0;

    // Signed influx residual in Q32.32.
    int64_t influx_q32_32 = 0;
    uint64_t payload_hash_u64 = 0;

    // Carrier authority (observables).
    uint16_t v_code_u16 = 0;
    uint16_t i_code_u16 = 0;
    uint32_t pad1 = 0;
};

// Temporal residual captures the discrepancy between an anchor's intended actuation
// and the measured outcome after the substrate step. This is the primary signal
// for temporal coupling (collapse-like operator replacement).
struct EwTemporalResidualPublishPacket {
    uint32_t src_anchor_id_u32 = 0;
    uint8_t coherence_band_u8 = 0;
    uint8_t suggested_action_u8 = 0;
    uint16_t residual_norm_q15 = 0;

    // Hashes of intent and measured summaries to keep routing deterministic.
    uint64_t intent_hash_u64 = 0;
    uint64_t measured_hash_u64 = 0;

    // Signed residual proxy in Q32.32 (optional but useful).
    int64_t residual_q32_32 = 0;

    // Carrier authority (observables).
    uint16_t v_code_u16 = 0;
    uint16_t i_code_u16 = 0;
    uint32_t pad0 = 0;
};
