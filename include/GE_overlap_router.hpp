#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "GE_corpus_anchor_store.hpp"

struct GE_OverlapHit {
    size_t record_index = 0;
    int64_t score_q32_32 = 0;
};

std::vector<GE_OverlapHit> GE_retrieve_topk_gpu(const GE_CorpusAnchorStore& store,
                                               const GE_CorpusAnchorRecord& query,
                                               uint8_t lane_u8,
                                               const EwId9* opt_domain_id9,
                                               size_t k);
