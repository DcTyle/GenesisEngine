#pragma once

#include <cstdint>

// Canonical packing for HookEmitActuationOp.
//
// This is a control-plane encoding that bridges UI/AI commands into the
// spectral fanout actuation slots. The payload is carried via EwHookPacket
// p0_q32_32 / p1_q32_32 fields, interpreted as bitwise uint64.
//
// Layout (little-endian, bit positions shown as [hi..lo]):
//   pack0_u64 = [a1_i32 | a0_i32]
//   pack1_u64 = [drive_k_u16 | flags_u16 | a2_i32]
//
// Interpretation:
//   - a0/a1/a2 are signed Q16.16 operands stored in int32.
//   - For ADD/MUL: a0=x, a1=y, a2 ignored (0).
//   - For CLAMP:  a0=x, a1=lo, a2=hi.
//   - drive_k_u16 routes to a spectral bin. 0xFFFF is broadcast (bins 0..7).
//   - flags_u16 reserved for future use (must be 0 in v1).

namespace genesis {

static inline uint64_t ew_pack_hook_emit_actuation_p0_u64(int32_t a0_q16_16, int32_t a1_q16_16) {
    return ((uint64_t)(uint32_t)a1_q16_16 << 32) | (uint64_t)(uint32_t)a0_q16_16;
}

static inline uint64_t ew_pack_hook_emit_actuation_p1_u64(int32_t a2_q16_16, uint16_t drive_k_u16, uint16_t flags_u16) {
    return ((uint64_t)drive_k_u16 << 48) | ((uint64_t)flags_u16 << 32) | (uint64_t)(uint32_t)a2_q16_16;
}

static inline void ew_unpack_hook_emit_actuation_u64(uint64_t p0_u64,
                                                     uint64_t p1_u64,
                                                     int32_t& out_a0_q16_16,
                                                     int32_t& out_a1_q16_16,
                                                     int32_t& out_a2_q16_16,
                                                     uint16_t& out_drive_k_u16,
                                                     uint16_t& out_flags_u16) {
    out_a0_q16_16 = (int32_t)(p0_u64 & 0xFFFFFFFFu);
    out_a1_q16_16 = (int32_t)((p0_u64 >> 32) & 0xFFFFFFFFu);
    out_a2_q16_16 = (int32_t)(p1_u64 & 0xFFFFFFFFu);
    out_flags_u16 = (uint16_t)((p1_u64 >> 32) & 0xFFFFu);
    out_drive_k_u16 = (uint16_t)((p1_u64 >> 48) & 0xFFFFu);
}

} // namespace genesis
