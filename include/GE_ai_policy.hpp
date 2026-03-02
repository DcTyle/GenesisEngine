#pragma once

#include <cstdint>

// -----------------------------------------------------------------------------
//  AI Interface Layer (Blueprint Appendix BE/BF): command vectors + compression
// -----------------------------------------------------------------------------
// The AI emits bounded command vectors. Commands never write phase/coherence
// directly; they bias routing and bounded task modulation inside the substrate.
//
// Fixed-array schema (BF.3A) is the default in this prototype.

static constexpr uint32_t EW_AI_COMMAND_MAX = 16;

enum EwAiOpcodeU16 : uint16_t {
    EW_AI_OP_NOOP          = 0x0000,
    EW_AI_OP_IO_READ       = 0x0001,
    EW_AI_OP_IO_WRITE      = 0x0002,
    EW_AI_OP_ROUTE         = 0x0003,
    EW_AI_OP_STORE         = 0x0004,
    EW_AI_OP_FETCH         = 0x0005,
    EW_AI_OP_RENDER_UPDATE = 0x0006,
    EW_AI_OP_TASK_SELECT   = 0x0007,
    EW_AI_OP_PRIORITY_HINT = 0x0008,
};

struct EwAiCommand {
    uint16_t opcode_u16;     // EwAiOpcodeU16
    uint16_t priority_u16;   // higher = earlier in stable order
    int64_t  weight_q63;     // [0..INT64_MAX]
};


// -----------------------------------------------------------------------------
//  AI Policy Table (Spec/Blueprint: classification -> operator -> action)
// -----------------------------------------------------------------------------
// The policy is a deterministic mapping from:
//   (class_id, confidence, attractor_strength)
// to:
//   (delta_profile_id, pulse shape codes)
//
// No dynamic learning occurs in the actuator layer. Policy selection is
// purely a function of committed substrate observables.

struct EwAiPolicyDecision {
    uint8_t profile_id_u8;   // EwDeltaProfileId
    int32_t f_code_i32;      // pulse frequency code
    uint16_t a_code_u16;     // pulse amplitude code
    uint16_t v_code_u16;     // pulse voltage code (carrier potential)
    uint16_t i_code_u16;     // pulse amperage code (carrier load)
    uint16_t reserved0_u16;
};

class EwAiPolicyTable {
public:
    EwAiPolicyTable();

    // Deterministic key-based policy shaping (stable across platforms).
    void init(uint64_t projection_seed);

    // Select an action for the current observation.
    // confidence_q32_32 in [0, 1] mapped to [0, 2^32].
    // attractor_strength_q32_32 in [0, 8] mapped to [0, 8*2^32].
    EwAiPolicyDecision decide(uint32_t class_id_u32,
                              int64_t confidence_q32_32,
                              int64_t attractor_strength_q32_32) const;

private:
    uint64_t seed_u64_;
    // Per-class base profile preference (0..2).
    uint8_t class_profile_u8_[256];
};
