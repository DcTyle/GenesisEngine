#pragma once

#include <cstdint>
#include <cstring>

#include "GE_actuation_packet.hpp"

// -----------------------------------------------------------------------------
// Actuation container packet v1
// -----------------------------------------------------------------------------
// Purpose:
//   Compact multiple small math ops into a single bounded actuation packet,
//   used only when primary + sidecar slots are full.
//
// Encoding (payload is fixed-size for deterministic parsing):
//   payload[0] = sub_count_u8 (0..EW_ACT_CONTAINER_SUBSLOTS)
//   payload[1] = reserved (0)
//   payload[2] = reserved (0)
//   payload[3] = reserved (0)
//   payload[4 + i*16 .. 4 + i*16 + 15] = subslot i, for i in [0..EW_ACT_CONTAINER_SUBSLOTS)
//     +0:  op_tag_u8 (ADD/MUL/CLAMP)
//     +1:  flags_u8 (v1 must be 0)
//     +2:  drive_k_u16 (little-endian)
//     +4:  a0_q16_16 i32 (little-endian)
//     +8:  a1_q16_16 i32 (little-endian)
//     +12: a2_q16_16 i32 (little-endian) (CLAMP hi; otherwise 0)
//
// Notes:
//   - payload_len_u8 is set to the fixed size (4 + 16*EW_ACT_CONTAINER_SUBSLOTS)
//   - Unused subslots must be zeroed.
//   - This is NOT a text parser; all values are numeric and bounded.

static const uint32_t EW_ACT_CONTAINER_SUBSLOTS = 3u;
static const uint32_t EW_ACT_CONTAINER_PAYLOAD_BYTES = 4u + 16u * EW_ACT_CONTAINER_SUBSLOTS;

static inline void ew_write_u16_le(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline void ew_write_i32_le(uint8_t* p, int32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline uint16_t ew_read_u16_le(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline int32_t ew_read_i32_le(const uint8_t* p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static inline void ew_act_container_init(EwActuationPacket& ap) {
    ap.drive_k_u16 = 0xFFFFu;
    ap.op_tag_u8 = (uint8_t)EW_ACT_OP_CONTAINER;
    ap.flags_u8 = 0u;
    ap.delta_amp_q32_32 = 0;
    ap.profile_id_u8 = 0u;
    ap.causal_tag_u8 = (uint8_t)EW_ACT_OP_CONTAINER;
    ap.pad0 = 0u;
    ap.v_code_u16 = 0u;
    ap.i_code_u16 = 0u;

    ap.payload_len_u8 = (uint8_t)EW_ACT_CONTAINER_PAYLOAD_BYTES;
    // Fixed-length payload parsing requires full zeroing.
    for (uint32_t i = 0u; i < EW_ACTUATION_PAYLOAD_MAX; ++i) ap.payload[i] = 0u;
}

static inline uint8_t ew_act_container_get_count(const EwActuationPacket& ap) {
    if (ap.payload_len_u8 < 4u) return 0u;
    uint8_t c = ap.payload[0];
    if (c > (uint8_t)EW_ACT_CONTAINER_SUBSLOTS) c = (uint8_t)EW_ACT_CONTAINER_SUBSLOTS;
    return c;
}

static inline bool ew_act_container_has_room(const EwActuationPacket& ap) {
    return ew_act_container_get_count(ap) < (uint8_t)EW_ACT_CONTAINER_SUBSLOTS;
}

static inline bool ew_act_container_push_math_op(
    EwActuationPacket& ap,
    uint8_t op_tag_u8,
    uint16_t drive_k_u16,
    int32_t a0_q16_16,
    int32_t a1_q16_16,
    int32_t a2_q16_16
) {
    if (ap.op_tag_u8 != (uint8_t)EW_ACT_OP_CONTAINER) return false;
    if (ap.payload_len_u8 != (uint8_t)EW_ACT_CONTAINER_PAYLOAD_BYTES) return false;

    // v1 only supports these small math ops.
    if (op_tag_u8 != (uint8_t)EW_ACT_OP_ADD &&
        op_tag_u8 != (uint8_t)EW_ACT_OP_MUL &&
        op_tag_u8 != (uint8_t)EW_ACT_OP_CLAMP) {
        return false;
    }

    uint8_t c = ew_act_container_get_count(ap);
    if (c >= (uint8_t)EW_ACT_CONTAINER_SUBSLOTS) return false;

    const uint32_t base = 4u + (uint32_t)c * 16u;
    ap.payload[base + 0u] = op_tag_u8;
    ap.payload[base + 1u] = 0u; // flags
    ew_write_u16_le(&ap.payload[base + 2u], drive_k_u16);
    ew_write_i32_le(&ap.payload[base + 4u], a0_q16_16);
    ew_write_i32_le(&ap.payload[base + 8u], a1_q16_16);
    ew_write_i32_le(&ap.payload[base + 12u], a2_q16_16);

    ap.payload[0] = (uint8_t)(c + 1u);
    return true;
}

struct EwActContainerSubOp {
    uint8_t op_tag_u8;
    uint8_t flags_u8;
    uint16_t drive_k_u16;
    int32_t a0_q16_16;
    int32_t a1_q16_16;
    int32_t a2_q16_16;
};

static inline EwActContainerSubOp ew_act_container_get_subop(const EwActuationPacket& ap, uint32_t idx) {
    EwActContainerSubOp s{};
    if (ap.op_tag_u8 != (uint8_t)EW_ACT_OP_CONTAINER) return s;
    if (ap.payload_len_u8 != (uint8_t)EW_ACT_CONTAINER_PAYLOAD_BYTES) return s;
    if (idx >= EW_ACT_CONTAINER_SUBSLOTS) return s;

    const uint32_t base = 4u + idx * 16u;
    s.op_tag_u8 = ap.payload[base + 0u];
    s.flags_u8 = ap.payload[base + 1u];
    s.drive_k_u16 = ew_read_u16_le(&ap.payload[base + 2u]);
    s.a0_q16_16 = ew_read_i32_le(&ap.payload[base + 4u]);
    s.a1_q16_16 = ew_read_i32_le(&ap.payload[base + 8u]);
    s.a2_q16_16 = ew_read_i32_le(&ap.payload[base + 12u]);
    return s;
}
