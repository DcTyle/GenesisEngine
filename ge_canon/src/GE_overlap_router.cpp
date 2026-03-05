#include "GE_overlap_router.hpp"

#include <algorithm>

static inline int64_t ge_abs_i64(int64_t x) { return x < 0 ? -x : x; }

int64_t GE_overlap_score_q32_32(const GE_CorpusAnchorRecord& a, const GE_CorpusAnchorRecord& b) {
    const int64_t base = (int64_t(1) << 32);
    const int64_t df = ge_abs_i64(a.carrier.f_carrier_turns_q32_32 - b.carrier.f_carrier_turns_q32_32);
    const int64_t dphi = ge_abs_i64(a.carrier.phi_carrier_turns_q32_32 - b.carrier.phi_carrier_turns_q32_32);
    const int64_t da = ge_abs_i64(a.carrier.A_carrier_q32_32 - b.carrier.A_carrier_q32_32);

    int64_t penalty = (df >> 2) + (dphi >> 2) + (da >> 3);
    int64_t score = base - penalty;
    if (score < 0) score = 0;
    return score;
}

std::vector<GE_OverlapHit> GE_retrieve_topk(const GE_CorpusAnchorStore& store,
                                           const GE_CorpusAnchorRecord& query,
                                           uint8_t lane_u8,
                                           const EwId9* opt_domain_id9,
                                           size_t k) {
    std::vector<GE_OverlapHit> hits;
    for (size_t i = 0; i < store.records.size(); ++i) {
        const auto& r = store.records[i];
        if (r.lane_u8 != lane_u8) continue;
        if (opt_domain_id9 && !(r.domain_id9 == *opt_domain_id9)) continue;
        const int64_t s = GE_overlap_score_q32_32(query, r);
        if (s == 0) continue;
        hits.push_back({i, s});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const GE_OverlapHit& a, const GE_OverlapHit& b) {
        if (a.score_q32_32 != b.score_q32_32) return a.score_q32_32 > b.score_q32_32;
        return a.record_index < b.record_index;
    });
    if (hits.size() > k) hits.resize(k);
    return hits;
}
