#include "GE_overlap_router.hpp"

#include "ew_gpu_compute.hpp"

#include <vector>

static inline EwGpuCarrierRecord ge_make_gpu_record(const GE_CorpusAnchorRecord& r) {
    EwGpuCarrierRecord d{};
    d.f_q32_32 = r.carrier.f_carrier_turns_q32_32;
    d.phi_q32_32 = r.carrier.phi_carrier_turns_q32_32;
    d.a_q32_32 = r.carrier.A_carrier_q32_32;
    d.v_q32_32 = int64_t(r.carrier.component_count_u32) << 32;
    d.f_code = r.sc4.f_code;
    d.a_code = r.sc4.a_code;
    d.v_code = r.sc4.v_code;
    d.i_code = r.sc4.i_code;
    d.lane_u32 = uint32_t(r.lane_u8);
    for (int i = 0; i < 9; ++i) d.domain_lane[i] = r.domain_id9.u32[i];
    return d;
}

std::vector<GE_OverlapHit> GE_retrieve_topk_gpu(const GE_CorpusAnchorStore& store,
                                                const GE_CorpusAnchorRecord& query,
                                                uint8_t lane_u8,
                                                const EwId9* opt_domain_id9,
                                                size_t k) {
    const size_t n = store.records.size();
    if (n == 0) {
        return {};
    }

    std::vector<EwGpuCarrierRecord> recs;
    recs.reserve(n);
    for (size_t i = 0; i < n; ++i) recs.push_back(ge_make_gpu_record(store.records[i]));

    std::vector<int64_t> scores_q32_32;
    const EwGpuCarrierRecord q = ge_make_gpu_record(query);
    const uint32_t* domain_ptr = opt_domain_id9 ? opt_domain_id9->u32.data() : nullptr;
    if (!ew_gpu_compute_overlap_scores(q, recs.data(), recs.size(), uint32_t(lane_u8), domain_ptr, scores_q32_32)) {
        return {};
    }

    std::vector<GE_OverlapHit> out_hits;
    if (!ew_gpu_select_topk(scores_q32_32.data(), scores_q32_32.size(), k, out_hits)) return {};
    return out_hits;
}
