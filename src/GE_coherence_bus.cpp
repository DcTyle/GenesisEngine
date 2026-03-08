#include "GE_coherence_bus.hpp"

#include "GE_temporal_summaries.hpp"
#include "GE_runtime.hpp"
#include "GE_anchor_select.hpp"

#include <algorithm>
#include <vector>

static inline uint8_t ew_coherence_band_from_abs_q32_32(int64_t v_q32_32) {
    // Map |v| to a small log2-ish band in [0..EW_COHERENCE_BANDS-1].
    uint64_t a = (uint64_t)((v_q32_32 < 0) ? -v_q32_32 : v_q32_32);
    uint8_t band = 0;
    // Shift down by 32 to bring to integer domain, then log2 bucket.
    a >>= 32;
    while (a > 0 && band + 1u < (uint8_t)EW_COHERENCE_BANDS) {
        a >>= 1;
        ++band;
    }
    return band;
}

void ew_coherence_bus_step(EwState& cand, const EwCtx& ctx) {
    (void)ctx;
    // Locate bus anchor deterministically by anchor.id_u32.
    Anchor* bus = ew_find_lowest_id_anchor(cand.anchors, EW_ANCHOR_KIND_COHERENCE_BUS);
    if (!bus) return;
    EwCoherenceBusAnchorState& bs = bus->coherence_bus_state;

    // Clear emitted hook list for this tick.
    bs.hook_out_count_u32 = 0u;
    bs.last_tick_u64 = cand.canonical_tick;

    // Collect leakage packets from all spectral anchors.
    struct TmpLeak {
        EwLeakagePublishPacket p;
        EwId9 key_id9{};
    };
    std::vector<TmpLeak> tmp;
    tmp.reserve(64);

    struct TmpInflux {
        EwInfluxPublishPacket p;
        EwId9 key_id9{};
    };
    std::vector<TmpInflux> tmp_in;
    tmp_in.reserve(64);

    struct TmpTemporal {
        EwTemporalResidualPublishPacket p;
        EwId9 key_id9{};
    };
    std::vector<TmpTemporal> tmp_tr;
    tmp_tr.reserve(64);

    for (uint32_t i = 0u; i < (uint32_t)cand.anchors.size(); ++i) {
        Anchor& a = cand.anchors[i];
        if (a.kind_u32 != EW_ANCHOR_KIND_SPECTRAL_FIELD) continue;
        EwSpectralFieldAnchorState& ss = a.spectral_field_state;
        if (ss.leakage_pending_u8 == 0u && ss.temporal_residual.residual_pending_u8 == 0u) continue;

        if (ss.leakage_pending_u8 != 0u) {
            EwLeakagePublishPacket lp;
            lp.src_anchor_id_u32 = i;
            lp.coherence_band_u8 = ss.leakage_band_u8;
            lp.suggested_action_u8 = (uint8_t)EwCoherenceSuggestedAction::AdjustViscosity;
            lp.leakage_q32_32 = ss.leakage_q32_32;
            lp.payload_id9 = ss.leakage_id9;
            lp.v_code_u16 = ss.intent_summary.last_v_code_u16;
            lp.i_code_u16 = ss.intent_summary.last_i_code_u16;
            tmp.push_back({lp, lp.payload_id9});

            // Consume the pending flag (one-shot publish).
            ss.leakage_pending_u8 = 0u;
        }

        if (ss.temporal_residual.residual_pending_u8 != 0u) {
            EwTemporalResidualPublishPacket tp;
            tp.src_anchor_id_u32 = i;
            tp.coherence_band_u8 = ss.temporal_residual.residual_band_u8;
            tp.suggested_action_u8 = (uint8_t)EwCoherenceSuggestedAction::OperatorReplace;
            tp.residual_norm_q15 = ss.temporal_residual.residual_norm_q15;
            tp.intent_id9 = ss.temporal_residual.intent_id9;
            tp.measured_id9 = ss.temporal_residual.measured_id9;
            tp.residual_q32_32 = ss.temporal_residual.residual_q32_32;
            tp.v_code_u16 = ss.intent_summary.last_v_code_u16;
            tp.i_code_u16 = ss.intent_summary.last_i_code_u16;
            tmp_tr.push_back({tp, tp.intent_id9});
            ss.temporal_residual.residual_pending_u8 = 0u;
        }
    }


    // Collect influx packets from voxel coupling anchors.
    for (uint32_t i = 0u; i < (uint32_t)cand.anchors.size(); ++i) {
        Anchor& a = cand.anchors[i];
        if (a.kind_u32 != EW_ANCHOR_KIND_VOXEL_COUPLING) continue;
        EwVoxelCouplingAnchorState& vs = a.voxel_coupling_state;

        // Temporal residual packets from voxel coupling anchors participate in the
        // same collapse-like operator replacement channel.
        if (vs.temporal_residual.residual_pending_u8 != 0u) {
            EwTemporalResidualPublishPacket tp;
            tp.src_anchor_id_u32 = i;
            tp.coherence_band_u8 = vs.temporal_residual.residual_band_u8;
            tp.suggested_action_u8 = (uint8_t)EwCoherenceSuggestedAction::OperatorReplace;
            tp.residual_norm_q15 = vs.temporal_residual.residual_norm_q15;
            tp.intent_id9 = vs.temporal_residual.intent_id9;
            tp.measured_id9 = vs.temporal_residual.measured_id9;
            tp.residual_q32_32 = vs.temporal_residual.residual_q32_32;
            tp.v_code_u16 = a.last_v_code;
            tp.i_code_u16 = a.last_i_code;
            tmp_tr.push_back({tp, tp.intent_id9});
            vs.temporal_residual.residual_pending_u8 = 0u;
        }

        if (vs.influx_pending_u8 == 0u) continue;

        EwInfluxPublishPacket ip;
        ip.src_anchor_id_u32 = i;
        ip.coherence_band_u8 = vs.influx_band_u8;
        ip.suggested_action_u8 = (uint8_t)EwCoherenceSuggestedAction::AdjustLearning;
        ip.influx_q32_32 = vs.influx_q32_32;
        ip.payload_id9 = vs.influx_id9;
        ip.v_code_u16 = a.last_v_code;
        ip.i_code_u16 = a.last_i_code;
        tmp_in.push_back({ip, ip.payload_id9});

        vs.influx_pending_u8 = 0u;
    }

    // Stable sort for determinism.
    std::stable_sort(tmp.begin(), tmp.end(), [](const TmpLeak& a, const TmpLeak& b) {
        if (a.p.coherence_band_u8 != b.p.coherence_band_u8) return a.p.coherence_band_u8 < b.p.coherence_band_u8;
        if (a.key_id9 != b.key_id9) return a.key_id9 < b.key_id9;
        if (a.p.src_anchor_id_u32 != b.p.src_anchor_id_u32) return a.p.src_anchor_id_u32 < b.p.src_anchor_id_u32;
        return a.p.payload_id9 < b.p.payload_id9;
    });

    std::stable_sort(tmp_in.begin(), tmp_in.end(), [](const TmpInflux& a, const TmpInflux& b) {
        if (a.p.coherence_band_u8 != b.p.coherence_band_u8) return a.p.coherence_band_u8 < b.p.coherence_band_u8;
        if (a.key_id9 != b.key_id9) return a.key_id9 < b.key_id9;
        if (a.p.src_anchor_id_u32 != b.p.src_anchor_id_u32) return a.p.src_anchor_id_u32 < b.p.src_anchor_id_u32;
        return a.p.payload_id9 < b.p.payload_id9;
    });

    std::stable_sort(tmp_tr.begin(), tmp_tr.end(), [](const TmpTemporal& a, const TmpTemporal& b) {
        if (a.p.coherence_band_u8 != b.p.coherence_band_u8) return a.p.coherence_band_u8 < b.p.coherence_band_u8;
        if (a.key_id9 != b.key_id9) return a.key_id9 < b.key_id9;
        if (a.p.src_anchor_id_u32 != b.p.src_anchor_id_u32) return a.p.src_anchor_id_u32 < b.p.src_anchor_id_u32;
        if (a.p.intent_id9 != b.p.intent_id9) return a.p.intent_id9 < b.p.intent_id9;
        return a.p.measured_id9 < b.p.measured_id9;
    });


    // Default caps (if not configured): per-band cap=4, authority cap=0.5.
    for (uint32_t b = 0u; b < EW_COHERENCE_BANDS; ++b) {
        if (bs.max_packets_per_band_per_tick_u16[b] == 0u) bs.max_packets_per_band_per_tick_u16[b] = 4u;
        if (bs.authority_cap_q15[b] == 0u) bs.authority_cap_q15[b] = 16384u;
    }

    // Reduce duplicates and push into rings.
    uint32_t band_counts[EW_COHERENCE_BANDS];
    for (uint32_t b = 0u; b < EW_COHERENCE_BANDS; ++b) band_counts[b] = 0u;

    // Derived physics coherence proxy from leakage magnitudes this tick.
    uint64_t phys_acc_u64 = 0;

    for (size_t i = 0; i < tmp.size(); ++i) {
        EwLeakagePublishPacket lp = tmp[i].p;
        // Reduce duplicates: merge consecutive identical (band + payload_id9).
        while (i + 1 < tmp.size()) {
            const EwLeakagePublishPacket& nx = tmp[i + 1].p;
            if (nx.coherence_band_u8 != lp.coherence_band_u8) break;
            if (nx.payload_id9 != lp.payload_id9) break;
            // Deterministic reduction: sum leakage.
            lp.leakage_q32_32 += nx.leakage_q32_32;
            ++i;
        }

        const uint32_t b = (uint32_t)(lp.coherence_band_u8 % EW_COHERENCE_BANDS);
        if (band_counts[b] >= bs.max_packets_per_band_per_tick_u16[b]) continue;
        band_counts[b]++;

        {
            // Accumulate physics coherence proxy from admitted leakage.
            uint64_t a = (uint64_t)((lp.leakage_q32_32 < 0) ? -lp.leakage_q32_32 : lp.leakage_q32_32);
            if (a > (uint64_t)(1ULL << 32)) a = (uint64_t)(1ULL << 32);
            const uint64_t q15 = (a * 32767ULL) >> 32;
            phys_acc_u64 += q15;
        }

        EwCoherenceBusBandRing& ring = bs.band[b];
        const uint32_t idx = (ring.head_u32 + ring.count_u32) % EW_COHERENCE_RING_PER_BAND;
        ring.ring[idx] = lp;
        if (ring.count_u32 < EW_COHERENCE_RING_PER_BAND) {
            ring.count_u32++;
        } else {
            ring.head_u32 = (ring.head_u32 + 1u) % EW_COHERENCE_RING_PER_BAND;
        }

        // Emit a bounded hook back to the source anchor.
        if (bs.hook_out_count_u32 < EW_COHERENCE_HOOK_OUT_MAX) {
            EwHookPacket hp;
            hp.dst_anchor_id_u32 = lp.src_anchor_id_u32;

            // Map leakage magnitude to a band and choose hook.
            const uint8_t mag_band = ew_coherence_band_from_abs_q32_32(lp.leakage_q32_32);
            if (mag_band >= 6u) hp.hook_op_u8 = (uint8_t)EwCoherenceHookOp::HookFreezeTick;
            else if (mag_band >= 4u) hp.hook_op_u8 = (uint8_t)EwCoherenceHookOp::HookFanoutBudget;
            else hp.hook_op_u8 = (uint8_t)EwCoherenceHookOp::HookAdjustViscosity;

            hp.causal_tag_u8 = 0xC1;
            hp.authority_q15 = bs.authority_cap_q15[b];

            // p0: signed adjustment suggestion proportional to leakage.
            // Clamp to +/-0.25 in Q32.32.
            const int64_t quarter = (1LL << 30);
            int64_t adj = lp.leakage_q32_32 >> 6; // reduce gain
            if (adj > quarter) adj = quarter;
            if (adj < -quarter) adj = -quarter;
            hp.p0_q32_32 = adj;

            // p1: derived fanout budget suggestion (0..256).
            int64_t fb = 64 - (int64_t)mag_band * 8;
            if (fb < 8) fb = 8;
            if (fb > 256) fb = 256;
            hp.p1_q32_32 = fb << 32;

            bs.hook_out[bs.hook_out_count_u32++] = hp;
        }
    }


    // Reduce influx packets and push into influx rings, producing learning hooks.
    uint32_t influx_band_counts[EW_COHERENCE_BANDS];
    for (uint32_t b = 0u; b < EW_COHERENCE_BANDS; ++b) influx_band_counts[b] = 0u;

    uint64_t learn_acc_u64 = 0;

    for (size_t i = 0; i < tmp_in.size(); ++i) {
        EwInfluxPublishPacket ip = tmp_in[i].p;
        while (i + 1 < tmp_in.size()) {
            const EwInfluxPublishPacket& nx = tmp_in[i + 1].p;
            if (nx.coherence_band_u8 != ip.coherence_band_u8) break;
            if (nx.payload_id9 != ip.payload_id9) break;
            ip.influx_q32_32 += nx.influx_q32_32;
            ++i;
        }

        const uint32_t b = (uint32_t)(ip.coherence_band_u8 % EW_COHERENCE_BANDS);
        if (influx_band_counts[b] >= bs.max_packets_per_band_per_tick_u16[b]) continue;
        influx_band_counts[b]++;

        {
            uint64_t a = (uint64_t)((ip.influx_q32_32 < 0) ? -ip.influx_q32_32 : ip.influx_q32_32);
            if (a > (uint64_t)(1ULL << 32)) a = (uint64_t)(1ULL << 32);
            const uint64_t q15 = (a * 32767ULL) >> 32;
            learn_acc_u64 += q15;
        }

        EwCoherenceBusInfluxBandRing& ring = bs.influx_band[b];
        const uint32_t idx = (ring.head_u32 + ring.count_u32) % EW_COHERENCE_RING_PER_BAND;
        ring.ring[idx] = ip;
        if (ring.count_u32 < EW_COHERENCE_RING_PER_BAND) {
            ring.count_u32++;
        } else {
            ring.head_u32 = (ring.head_u32 + 1u) % EW_COHERENCE_RING_PER_BAND;
        }

        // Emit a bounded learning hook to spectral anchors (broadcast).
        if (bs.hook_out_count_u32 < EW_COHERENCE_HOOK_OUT_MAX) {
            EwHookPacket hp;
            hp.dst_anchor_id_u32 = 0u; // 0 means broadcast in delivery loop
            hp.hook_op_u8 = (uint8_t)EwCoherenceHookOp::HookAdjustLearning;
            hp.causal_tag_u8 = 0xC2;
            hp.authority_q15 = bs.authority_cap_q15[b];

            const int64_t half = (1LL << 31);
            int64_t adj = ip.influx_q32_32 >> 4;
            if (adj > half) adj = half;
            if (adj < -half) adj = -half;
            hp.p0_q32_32 = adj;
            hp.p1_q32_32 = 0;

            bs.hook_out[bs.hook_out_count_u32++] = hp;
        }
    }

    // Update derived learning coherence proxy (bounded).
    if (learn_acc_u64 > 0u) {
        uint64_t v = learn_acc_u64;
        if (v > 32767ULL) v = 32767ULL;
        bs.learning_coherence_q15 = (uint16_t)v;
    } else {
        bs.learning_coherence_q15 = (uint16_t)((bs.learning_coherence_q15 > 64u) ? (bs.learning_coherence_q15 - 64u) : 0u);
    }

    // Reduce temporal residual packets and emit operator-replace hooks.
    uint32_t tr_band_counts[EW_COHERENCE_BANDS];
    for (uint32_t b = 0u; b < EW_COHERENCE_BANDS; ++b) tr_band_counts[b] = 0u;
    uint64_t temporal_acc_u64 = 0;
    uint64_t mismatch_acc_u64 = 0;
    uint64_t stability_acc_u64 = 0;
    uint32_t mismatch_count_u32 = 0;

    for (size_t i = 0; i < tmp_tr.size(); ++i) {
        EwTemporalResidualPublishPacket tp = tmp_tr[i].p;
        while (i + 1 < tmp_tr.size()) {
            const EwTemporalResidualPublishPacket& nx = tmp_tr[i + 1].p;
            if (nx.coherence_band_u8 != tp.coherence_band_u8) break;
            if (nx.intent_id9 != tp.intent_id9) break;
            if (nx.measured_id9 != tp.measured_id9) break;
            // Deterministic reduction: max residual_norm, sum residual_q32_32.
            if (nx.residual_norm_q15 > tp.residual_norm_q15) tp.residual_norm_q15 = nx.residual_norm_q15;
            tp.residual_q32_32 += nx.residual_q32_32;
            ++i;
        }

        const uint32_t b = (uint32_t)(tp.coherence_band_u8 % EW_COHERENCE_BANDS);
        if (tr_band_counts[b] >= bs.max_packets_per_band_per_tick_u16[b]) continue;
        tr_band_counts[b]++;

        temporal_acc_u64 += (uint64_t)tp.residual_norm_q15;

        // Emit operator replacement hook back to the source anchor.
        if (bs.hook_out_count_u32 < EW_COHERENCE_HOOK_OUT_MAX) {
            EwHookPacket hp;
            hp.dst_anchor_id_u32 = tp.src_anchor_id_u32;
            hp.hook_op_u8 = (uint8_t)EwCoherenceHookOp::HookOperatorReplace;
            hp.causal_tag_u8 = 0xC3;
            hp.authority_q15 = bs.authority_cap_q15[b];

            // p0: gain_q15 packed into Q32.32 (lower 16 bits are used).
            // Deterministic gain: proportional to residual norm and coherence.
            // gain_q15 in [0..16384].
            uint32_t g = (uint32_t)tp.residual_norm_q15;
            // Incorporate bounded pulse intent + measured history for gain.
            // This makes operator replacement respond to persistent mismatch, not one-tick noise.
            uint16_t mean_meas_q15 = 0u;
            uint16_t mad_meas_q15 = 0u;
            uint16_t mean_int_q15 = 0u;
            uint16_t mad_int_q15 = 0u;
            if (tp.src_anchor_id_u32 < cand.anchors.size()) {
                const Anchor& srca = cand.anchors[tp.src_anchor_id_u32];
                if (srca.kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) {
                    const EwSpectralFieldAnchorState& ss2 = srca.spectral_field_state;
                    const uint32_t cntm = ss2.pulse_measured_ring_count_u32;
                    const uint32_t oldm = (cntm == 0u) ? 0u : ((ss2.pulse_measured_ring_head_u32 + EW_PULSE_MEASURED_RING_W - (cntm % EW_PULSE_MEASURED_RING_W)) % EW_PULSE_MEASURED_RING_W);
                    mean_meas_q15 = ew_pulse_measured_ring_mean_q15(ss2.pulse_measured_ring, oldm, cntm);
                    mad_meas_q15  = ew_pulse_measured_ring_mad_q15(ss2.pulse_measured_ring, oldm, cntm);

                    const uint32_t cnti = ss2.pulse_intent_ring_count_u32;
                    const uint32_t oldi = (cnti == 0u) ? 0u : ((ss2.pulse_intent_ring_head_u32 + EW_PULSE_INTENT_RING_W - (cnti % EW_PULSE_INTENT_RING_W)) % EW_PULSE_INTENT_RING_W);
                    mean_int_q15 = ew_pulse_intent_ring_mean_q15(ss2.pulse_intent_ring, oldi, cnti);
                    mad_int_q15  = ew_pulse_intent_ring_mad_q15(ss2.pulse_intent_ring, oldi, cnti);
                } else if (srca.kind_u32 == EW_ANCHOR_KIND_VOXEL_COUPLING) {
                    const EwVoxelCouplingAnchorState& vs2 = srca.voxel_coupling_state;
                    const uint32_t cntm = vs2.pulse_measured_ring_count_u32;
                    const uint32_t oldm = (cntm == 0u) ? 0u : ((vs2.pulse_measured_ring_head_u32 + EW_PULSE_MEASURED_RING_W - (cntm % EW_PULSE_MEASURED_RING_W)) % EW_PULSE_MEASURED_RING_W);
                    mean_meas_q15 = ew_pulse_measured_ring_mean_q15(vs2.pulse_measured_ring, oldm, cntm);
                    mad_meas_q15  = ew_pulse_measured_ring_mad_q15(vs2.pulse_measured_ring, oldm, cntm);

                    const uint32_t cnti = vs2.pulse_intent_ring_count_u32;
                    const uint32_t oldi = (cnti == 0u) ? 0u : ((vs2.pulse_intent_ring_head_u32 + EW_PULSE_INTENT_RING_W - (cnti % EW_PULSE_INTENT_RING_W)) % EW_PULSE_INTENT_RING_W);
                    mean_int_q15 = ew_pulse_intent_ring_mean_q15(vs2.pulse_intent_ring, oldi, cnti);
                    mad_int_q15  = ew_pulse_intent_ring_mad_q15(vs2.pulse_intent_ring, oldi, cnti);
                }
            }

            // Persistent mismatch score: |mean(measured) - mean(intent)| (Q0.15)
            const uint32_t mismatch_q15 = (mean_meas_q15 > mean_int_q15)
                ? (uint32_t)(mean_meas_q15 - mean_int_q15)
                : (uint32_t)(mean_int_q15 - mean_meas_q15);

            // Stability proxy from MAD: stability = 1 - (mad_avg / denom).
            const uint32_t mad_avg_q15 = ((uint32_t)mad_meas_q15 + (uint32_t)mad_int_q15) >> 1;
            uint32_t denom_q15 = ((uint32_t)mean_meas_q15 + (uint32_t)mean_int_q15) >> 1;
            if (denom_q15 < 256u) denom_q15 = 256u;
            uint32_t ratio_q15 = (mad_avg_q15 << 15) / denom_q15;
            if (ratio_q15 > 32767u) ratio_q15 = 32767u;
            uint32_t stability_q15 = 32767u - ratio_q15;

            // Blend instantaneous residual with mismatch history, then damp by stability.
            if (mismatch_q15 > 0u) g = (g + mismatch_q15) >> 1;
            g = (g * stability_q15) >> 15;

            mismatch_acc_u64 += (uint64_t)mismatch_q15;
            stability_acc_u64 += (uint64_t)stability_q15;
            mismatch_count_u32++;
            // Bias gain down if both phys+learning coherence are low.
            const uint32_t coh = (uint32_t)bs.phys_coherence_q15 + (uint32_t)bs.learning_coherence_q15;
            if (coh < 4096u) g >>= 2;
            else if (coh < 8192u) g >>= 1;
            if (g > 16384u) g = 16384u;
            hp.p0_q32_32 = ((int64_t)g) << 32;

            // p1: signed residual proxy (Q32.32) so the anchor can pick a direction.
            hp.p1_q32_32 = tp.residual_q32_32;

            bs.hook_out[bs.hook_out_count_u32++] = hp;
        }
    }

    // Update derived temporal coherence proxy (bounded) with slow decay.
    if (temporal_acc_u64 > 0u) {
        uint64_t v = temporal_acc_u64;
        if (v > 32767ULL) v = 32767ULL;
        bs.temporal_coherence_q15 = (uint16_t)v;
    } else {
        bs.temporal_coherence_q15 = (uint16_t)((bs.temporal_coherence_q15 > 64u) ? (bs.temporal_coherence_q15 - 64u) : 0u);
    }

    // Update derived temporal mismatch/stability proxies (bounded) with slow decay.
    if (mismatch_count_u32 > 0u) {
        uint64_t mm = mismatch_acc_u64 / (uint64_t)mismatch_count_u32;
        uint64_t ss = stability_acc_u64 / (uint64_t)mismatch_count_u32;
        if (mm > 32767ULL) mm = 32767ULL;
        if (ss > 32767ULL) ss = 32767ULL;
        bs.temporal_mismatch_q15 = (uint16_t)mm;
        bs.temporal_stability_q15 = (uint16_t)ss;
    } else {
        bs.temporal_mismatch_q15 = (uint16_t)((bs.temporal_mismatch_q15 > 64u) ? (bs.temporal_mismatch_q15 - 64u) : 0u);
        bs.temporal_stability_q15 = (uint16_t)((bs.temporal_stability_q15 > 64u) ? (bs.temporal_stability_q15 - 64u) : 0u);
    }

    // Update derived physics coherence proxy (bounded).
    // This is a soft availability/energy signal: more leakage => higher phys coherence.
    if (phys_acc_u64 > 0u) {
        uint64_t v = phys_acc_u64;
        if (v > 32767ULL) v = 32767ULL;
        bs.phys_coherence_q15 = (uint16_t)v;
    } else {
        // Slow decay to avoid sticking.
        bs.phys_coherence_q15 = (uint16_t)((bs.phys_coherence_q15 > 64u) ? (bs.phys_coherence_q15 - 64u) : 0u);
    }

    // Deliver hooks to spectral anchors deterministically.
    for (uint32_t i = 0u; i < bs.hook_out_count_u32; ++i) {
        const EwHookPacket& hp = bs.hook_out[i];
        if (hp.dst_anchor_id_u32 == 0u) {
            // Broadcast: deliver to all spectral anchors.
            for (uint32_t j = 0u; j < (uint32_t)cand.anchors.size(); ++j) {
                Anchor& dst = cand.anchors[j];
                if (dst.kind_u32 != EW_ANCHOR_KIND_SPECTRAL_FIELD) continue;
                EwSpectralFieldAnchorState& ss = dst.spectral_field_state;
                if (ss.hook_inbox_count_u32 >= EW_SPECTRAL_HOOK_INBOX_MAX) continue;
                ss.hook_inbox[ss.hook_inbox_count_u32++] = hp;
            }
            continue;
        }
        if (hp.dst_anchor_id_u32 >= cand.anchors.size()) continue;
        Anchor& dst = cand.anchors[hp.dst_anchor_id_u32];
        if (dst.kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) {
            EwSpectralFieldAnchorState& ss = dst.spectral_field_state;
            if (ss.hook_inbox_count_u32 >= EW_SPECTRAL_HOOK_INBOX_MAX) continue;
            ss.hook_inbox[ss.hook_inbox_count_u32++] = hp;
        } else if (dst.kind_u32 == EW_ANCHOR_KIND_VOXEL_COUPLING) {
            EwVoxelCouplingAnchorState& vs = dst.voxel_coupling_state;
            if (vs.hook_inbox_count_u32 >= EW_VOXEL_COUPLING_HOOK_INBOX_MAX) continue;
            vs.hook_inbox[vs.hook_inbox_count_u32++] = hp;
        }
    }
}
