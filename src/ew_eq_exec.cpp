#include "ew_eq_exec.h"
#include "GE_runtime.hpp"

#include <cstring>

static inline void wr_u32_le(uint8_t* b, size_t off, uint32_t v) {
    b[off+0] = (uint8_t)(v & 0xFFu);
    b[off+1] = (uint8_t)((v >> 8) & 0xFFu);
    b[off+2] = (uint8_t)((v >> 16) & 0xFFu);
    b[off+3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline void wr_f64_le(uint8_t* b, size_t off, double v) {
    uint64_t u = 0;
    std::memcpy(&u, &v, sizeof(u));
    for (int i = 0; i < 8; ++i) b[off + (size_t)i] = (uint8_t)((u >> (8*i)) & 0xFFu);
}

static inline void wr_lane_id9_from_u64(uint8_t* b, size_t off, uint64_t lane0_u64) {
    // LaneId9 is stored as f64[9] in AnchorOpPacked_v1.
    // This executor surface uses lane ids where only v[0] is populated.
    for (int k = 0; k < 9; ++k) {
        const double d = (k == 0) ? (double)lane0_u64 : 0.0;
        wr_f64_le(b, off + (size_t)k * 8, d);
    }
}

static inline uint32_t expected_n_in_for_kind(uint32_t kind) {
    switch (kind) {
        case 0x00000001u: return 0u; // text encode uses internal input lanes
        case 0x00000002u: return 1u; // aggregate from one buffer
        case 0x00000003u: return 2u; // dot from two scalars
        case 0x00000004u: return 1u; // constrain Pi_G consumes one buffer
        case 0x00000005u: return 1u; // chain apply consumes one lane
        case 0x00000006u: return 1u; // observable project consumes one lane
        case 0x00000007u: return 0u; // effective constants uses ctx
        case 0x00000008u: return 1u; // sink omega consumes one lane
        default: return 0u;
    }
}

static inline uint32_t expected_n_out_for_kind(uint32_t kind) {
    switch (kind) {
        case 0x00000001u: return 1u;
        case 0x00000002u: return 1u;
        case 0x00000003u: return 1u;
        case 0x00000004u: return 1u;
        case 0x00000005u: return 1u;
        case 0x00000006u: return 1u;
        case 0x00000007u: return 1u;
        case 0x00000008u: return 1u;
        default: return 0u;
    }
}

static inline uint32_t expected_payload_bytes_for_kind(uint32_t kind) {
    switch (kind) {
        case 0x00000001u: return 56u;
        case 0x00000002u: return 80u;
        case 0x00000003u: return 8u;
        case 0x00000004u: return 160u;
        case 0x00000005u: return 256u;
        case 0x00000006u: return 72u;
        case 0x00000007u: return 72u;
        case 0x00000008u: return 76u;
        default: return 0u;
    }
}

EwEqExecResult ew_eq_exec_packet(SubstrateManager* sm,
                                 uint32_t opcode_u32,
                                 const std::vector<uint64_t>& args_u64) {
    EwEqExecResult r;
    if (!sm) return r;

    // This executor packs an AnchorOpPacked_v1 record (1500 bytes) and submits
    // it to the substrate microprocessor.
    //
    // Contract (deterministic, minimal):
    // - opcode_u32 is the op_kind.
    // - args_u64 provides lane ids and payload words.
    //   Lane ids are u64 values mapped into lane_id9 with only v[0] populated.
    // - If lane ids are not provided, canonical defaults are used.
    const uint32_t op_kind = opcode_u32;
    const uint32_t n_in = expected_n_in_for_kind(op_kind);
    const uint32_t n_out = expected_n_out_for_kind(op_kind);
    const uint32_t payload_bytes = expected_payload_bytes_for_kind(op_kind);

    uint8_t pkt[1500];
    std::memset(pkt, 0, sizeof(pkt));

    // op_id_e9_f64[9] left as 0 (reserved).
    wr_u32_le(pkt, 72, op_kind);

    // exec_order: args_u64[0] if present, else canonical_tick.
    const uint32_t exec_order = (!args_u64.empty()) ? (uint32_t)args_u64[0] : (uint32_t)sm->canonical_tick;
    wr_u32_le(pkt, 76, exec_order);

    // IN lanes
    wr_u32_le(pkt, 80, n_in);
    size_t arg_i = (!args_u64.empty()) ? 1u : 0u;
    const size_t in_base = 84;
    for (uint32_t i = 0; i < n_in; ++i) {
        const uint64_t lane0 = (arg_i < args_u64.size()) ? args_u64[arg_i++] : ((i == 0) ? 2001u : 2001u);
        wr_lane_id9_from_u64(pkt, in_base + (size_t)i * 72, lane0);
    }

    // OUT lanes
    const size_t out_n_off = 84 + 576;
    wr_u32_le(pkt, out_n_off, n_out);
    const size_t out_base = out_n_off + 4;
    for (uint32_t i = 0; i < n_out; ++i) {
        const uint64_t lane0 = (arg_i < args_u64.size()) ? args_u64[arg_i++] : (3000u + (uint64_t)i);
        wr_lane_id9_from_u64(pkt, out_base + (size_t)i * 72, lane0);
    }

    // Payload
    const size_t payload_n_off = out_base + 576;
    wr_u32_le(pkt, payload_n_off, payload_bytes);
    uint8_t* payload = pkt + payload_n_off + 4;

    // Payload words are u64 in args; pack little-endian and truncate if needed.
    // For f64 payload fields, callers should pass IEEE754 bits via uint64.
    for (uint32_t i = 0; i < payload_bytes; ++i) payload[i] = 0;
    for (uint32_t off = 0; off + 8 <= payload_bytes && arg_i < args_u64.size(); off += 8) {
        const uint64_t u = args_u64[arg_i++];
        for (int k = 0; k < 8; ++k) payload[off + (uint32_t)k] = (uint8_t)((u >> (8*k)) & 0xFFu);
    }

    sm->submit_operator_packet_v1(pkt, sizeof(pkt));
    r.ok = true;
    r.op_executed_u32 = op_kind;
    return r;
}

