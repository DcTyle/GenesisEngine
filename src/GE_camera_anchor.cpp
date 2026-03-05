#include "GE_camera_anchor.hpp"
#include "GE_runtime.hpp"
#include "ew_eq_exec.h"
#include "crawler_encode_cuda.hpp"
#include "frequency_collapse.hpp"

static inline void ge_wr_u32_le(uint8_t* b, uint32_t off, uint32_t v) {
    b[off+0] = (uint8_t)(v & 0xFFu);
    b[off+1] = (uint8_t)((v >> 8) & 0xFFu);
    b[off+2] = (uint8_t)((v >> 16) & 0xFFu);
    b[off+3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline void ge_wr_i32_le(uint8_t* b, uint32_t off, int32_t v) {
    ge_wr_u32_le(b, off, (uint32_t)v);
}

static inline void ge_wr_u64_le(uint8_t* b, uint32_t off, uint64_t v) {
    for (int k = 0; k < 8; ++k) b[off + (uint32_t)k] = (uint8_t)((v >> (8*k)) & 0xFFu);
}

// Build a carrier-coded compute request payload.
// This ensures payload_bytes are NOT raw structs: the request is encoded
// into SpiderCode4 + deterministic carrier collapse, matching the same
// instruction-set encoding used by crawler/corpus ingestion.
static bool ge_build_compute_payload72_carrier(uint32_t subop_u32,
                                               uint64_t tick_u64,
                                               uint8_t out_payload72[72]) {
    if (!out_payload72) return false;
    for (uint32_t i = 0; i < 72u; ++i) out_payload72[i] = 0;

    // Canonical request bytes (small, fixed).
    uint8_t req[32];
    for (uint32_t i = 0; i < 32u; ++i) req[i] = 0;
    req[0] = 'C'; req[1] = 'B'; req[2] = 'U'; req[3] = 'S';
    ge_wr_u32_le(req, 4, subop_u32);
    ge_wr_u64_le(req, 8, tick_u64);
    // remaining bytes stay zero (reserved)

    SpiderCode4 sc{};
    if (!ew_encode_spidercode4_from_bytes_chunked_cuda(req, sizeof(req), 64u, &sc)) return false;

    // Collapse SpiderCode4 into a deterministic carrier.
    std::vector<EwFreqComponentQ32_32> comps;
    comps.reserve(4);
    auto push_comp = [&](int32_t f_code, int32_t a_code, int32_t phi_code) {
        EwFreqComponentQ32_32 c;
        c.f_turns_q32_32 = int64_t(f_code) << 16;
        c.a_q32_32 = (int64_t(a_code) << 16);
        c.phi_turns_q32_32 = int64_t(phi_code) << 16;
        comps.push_back(c);
    };
    push_comp(sc.f_code, (int32_t)sc.a_code, (int32_t)sc.v_code);
    push_comp((int32_t)sc.a_code, (int32_t)sc.v_code, (int32_t)sc.i_code);
    push_comp((int32_t)sc.v_code, (int32_t)sc.i_code, sc.f_code);
    push_comp((int32_t)sc.i_code, sc.f_code, (int32_t)sc.a_code);

    EwCarrierWaveQ32_32 carrier{};
    if (!ew_collapse_frequency_components_q32_32(comps, carrier)) return false;

    // Payload layout (72 bytes):
    //  0..3   u32 subop
    //  4..19  SpiderCode4 transport fields
    //  20..47 EwCarrierWaveQ32_32 (i64 f, i64 A, i64 phi, u32 count)
    //  48..71 reserved
    ge_wr_u32_le(out_payload72, 0, subop_u32);
    ge_wr_i32_le(out_payload72, 4, sc.f_code);
    ge_wr_u32_le(out_payload72, 8, (uint32_t)sc.a_code);
    ge_wr_u32_le(out_payload72, 12, (uint32_t)sc.v_code);
    ge_wr_u32_le(out_payload72, 16, (uint32_t)sc.i_code);
    ge_wr_u64_le(out_payload72, 20, (uint64_t)carrier.f_carrier_turns_q32_32);
    ge_wr_u64_le(out_payload72, 28, (uint64_t)carrier.A_carrier_q32_32);
    ge_wr_u64_le(out_payload72, 36, (uint64_t)carrier.phi_carrier_turns_q32_32);
    ge_wr_u32_le(out_payload72, 44, carrier.component_count_u32);
    return true;
}

void ge_camera_anchor_tick(SubstrateMicroprocessor& sm, Anchor& cam_anchor, uint64_t tick_u64) {
    if (cam_anchor.kind_u32 != EW_ANCHOR_KIND_CAMERA) return;

    // HARD RULE: no direct compute here.
    // Camera focus updates are executed as substrate phase-dynamics via the
    // compute-bus operator packet (OPK_COMPUTE_BUS_DISPATCH).
    (void)tick_u64;

    const uint32_t OP_ID_CAMERA_FOCUS_UPDATE = 1u;
    uint8_t payload[72];
    if (!ge_build_compute_payload72_carrier(OP_ID_CAMERA_FOCUS_UPDATE, tick_u64, payload)) {
        // Fail closed deterministically: do not update focus if encoding fails.
        return;
    }

    // Pack payload into u64 words for the generic operator packet executor.
    std::vector<uint64_t> args;
    args.reserve(1 + (72u/8u));
    args.push_back((uint64_t)sm.canonical_tick);
    for (uint32_t off = 0; off < 72u; off += 8u) {
        uint64_t u = 0;
        for (int k = 0; k < 8; ++k) u |= ((uint64_t)payload[off + (uint32_t)k]) << (8*k);
        args.push_back(u);
    }

    (void)ew_eq_exec_packet(&sm, 0x00000009u, args);
}
