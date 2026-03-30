#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "GE_corpus_anchor_store.hpp"
#include "GE_overlap_router.hpp"

// GPU top-K selector from a device score array (score_q32_32, 0 means filtered).
// Deterministic: scalar scan per block + deterministic pairwise merges on GPU.
// Returns top-k hits as (record_index, score_q32_32) ordered by descending score then ascending record_index.
bool GE_select_topk_cuda(const int64_t* d_scores_q32_32, size_t n_scores,
                         size_t k,
                         std::vector<GE_OverlapHit>& out_hits);
