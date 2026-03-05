#pragma once
#include <cstdint>
#include <vector>

#include "GE_corpus_anchor_store.hpp"

// GPU overlap retrieval (score + top-k selection) for the hot path.
// Returns hits ordered by descending score then ascending record_index.
bool GE_retrieve_topk_cuda(const GE_CorpusAnchorStore& store,
                           const GE_CorpusAnchorRecord& query,
                           uint8_t lane_u8,
                           const EwId9* opt_domain_id9,
                           size_t k,
                           std::vector<GE_OverlapHit>& out_hits);

// Convenience wrapper matching the CPU API style.
static inline std::vector<GE_OverlapHit> GE_retrieve_topk_cuda(const GE_CorpusAnchorStore& store,
                                                               const GE_CorpusAnchorRecord& query,
                                                               uint8_t lane_u8,
                                                               const EwId9* opt_domain_id9,
                                                               size_t k) {
    std::vector<GE_OverlapHit> hits;
    (void)GE_retrieve_topk_cuda(store, query, lane_u8, opt_domain_id9, k, hits);
    return hits;
}
