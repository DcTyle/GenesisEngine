#pragma once

#include <cstdint>

// ActuationPacket is the explicit control-plane unit.
// It is intentionally numeric and bounded so later GPU kernels can
// produce/consume it without parsing text.

static const uint32_t EW_ACTUATION_PAYLOAD_MAX = 64;

enum EwActuationOpTag : uint8_t {
    EW_ACT_OP_NONE = 0,
    EW_ACT_OP_DRIVE = 1,      // drive a carrier bin
    EW_ACT_OP_IMPULSE = 2,    // impulse-style delta
    EW_ACT_OP_OBSERVE = 3,    // emit/mark observation (reserved)

    // Minimal math/ops dialect (executed inside the substrate fanout loop).
    // Operands are encoded in payload as little-endian signed int32 Q16.16.
    //  - ADD: payload = x, y
    //  - MUL: payload = x, y
    //  - CLAMP: payload = x, lo, hi
    // Result is re-expressed as a bounded carrier impulse.
    EW_ACT_OP_ADD = 4,
    EW_ACT_OP_MUL = 5,
    EW_ACT_OP_CLAMP = 6
};

struct EwActuationPacket {
    // Target carrier/bin index for spectral-style anchors.
    uint16_t drive_k_u16 = 0;
    // Operation tag.
    uint8_t op_tag_u8 = EW_ACT_OP_DRIVE;
    // Flags for routing/spawn/publish (reserved for future).
    uint8_t flags_u8 = 0;

    // Signed amplitude delta (Q32.32) and phase selector.
    int64_t delta_amp_q32_32 = 0;
    uint8_t profile_id_u8 = 0;
    uint8_t causal_tag_u8 = 0;
    uint16_t pad0 = 0;

    // Carrier authority plane (mirrors SpiderCode4 v/i, bounded).
    uint16_t v_code_u16 = 0;
    uint16_t i_code_u16 = 0;

    // Optional bounded debug/meaning payload (UTF-8 or numeric bytes).
    uint8_t payload_len_u8 = 0;
    uint8_t payload[EW_ACTUATION_PAYLOAD_MAX] = {0};
};
