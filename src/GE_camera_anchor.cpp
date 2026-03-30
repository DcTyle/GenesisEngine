#include "GE_camera_anchor.hpp"
#include "GE_color_palette.hpp"
#include "GE_fourier_fanout.hpp"
#include "GE_runtime.hpp"
#include "GE_anchor_select.hpp"
#include "anchor.hpp"
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

void ge_camera_anchor_tick(SubstrateManager& sm, Anchor& cam_anchor, uint64_t tick_u64) {
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

    // Derived environment sampling (read-only projection):
    // - spectral field probes at the camera position
    // - absorption/emission "color band" proxy from voxel influx bands
    // HARD RULE: these fields never become authoritative state drivers.
    int16_t field_q = 0;
    int16_t grad_q = 0;
    uint16_t coh_q15 = 0;
    uint8_t color_band = 0;

    const int32_t p[3] = {
        cam_anchor.camera_state.pos_xyz_q16_16[0],
        cam_anchor.camera_state.pos_xyz_q16_16[1],
        cam_anchor.camera_state.pos_xyz_q16_16[2]
    };

    // Probe nearest spectral field anchor deterministically by distance to region center.
    {
        const Anchor* best = ew_find_nearest_anchor_const(
            sm.anchors,
            EW_ANCHOR_KIND_SPECTRAL_FIELD,
            p,
            [](const Anchor& a, int32_t out_center_q16_16[3]) -> bool {
                out_center_q16_16[0] = a.spectral_field_state.region_center_q16_16[0];
                out_center_q16_16[1] = a.spectral_field_state.region_center_q16_16[1];
                out_center_q16_16[2] = a.spectral_field_state.region_center_q16_16[2];
                return true;
            }
        );

        if (best) {
        // Respect HOLD: when the spectral microprocessor holds the tick, do not
        // project transient probe values for audio/viewport modulation.
        if (best->spectral_field_state.hold_tick_u8 == 0u) {
            field_q = ew_spectral_probe_field_q1_15(best->spectral_field_state, p);
            grad_q = ew_spectral_probe_grad_q1_15(best->spectral_field_state, p);
            const uint16_t g = (grad_q < 0) ? (uint16_t)(-grad_q) : (uint16_t)grad_q;
            coh_q15 = (g > 32767u) ? 32767u : g;
        } else {
            field_q = 0;
            grad_q = 0;
            coh_q15 = 0;
        }
        }
    }

    // Absorption/emission banding: choose the nearest voxel-coupling anchor's influx band
    // as a proxy for "light enters medium and exits as a color".
    // Deterministic: pick nearest by squared distance to the voxel block center;
    // stable tie-break on anchor id.
    {
        const Anchor* best = ew_find_nearest_anchor_const(
            sm.anchors,
            EW_ANCHOR_KIND_VOXEL_COUPLING,
            p,
            [](const Anchor& a, int32_t out_center_q16_16[3]) -> bool {
                const EwVoxelCouplingAnchorState& vs = a.voxel_coupling_state;
                const int32_t half_extent = (int32_t)((EW_VOXEL_COUPLING_DIM * vs.voxel_size_m_q16_16) / 2);
                out_center_q16_16[0] = vs.origin_q16_16[0] + half_extent;
                out_center_q16_16[1] = vs.origin_q16_16[1] + half_extent;
                out_center_q16_16[2] = vs.origin_q16_16[2] + half_extent;
                return true;
            }
        );
        if (best) {
            color_band = best->voxel_coupling_state.influx_band_u8;
        }
    }

    cam_anchor.camera_state.audio_env_field_q1_15 = field_q;
    cam_anchor.camera_state.audio_env_grad_q1_15 = grad_q;
    cam_anchor.camera_state.audio_env_coherence_q15 = coh_q15;
    cam_anchor.camera_state.color_band_u8 = color_band;
    const EwPaletteEntry pe = ew_palette_lookup(color_band);
    cam_anchor.camera_state.color_r_u8 = pe.r_u8;
    cam_anchor.camera_state.color_g_u8 = pe.g_u8;
    cam_anchor.camera_state.color_b_u8 = pe.b_u8;
    cam_anchor.camera_state.audio_eq_preset_u8 = pe.audio_eq_preset_u8;
    cam_anchor.camera_state.audio_reverb_preset_u8 = pe.audio_reverb_preset_u8;
    cam_anchor.camera_state.audio_occlusion_preset_u8 = pe.audio_occlusion_preset_u8;
}
