#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "GE_corpus_anchor_store.hpp"
#include "GE_overlap_router.hpp"
#include "carrier_bundle_gpu.hpp"

struct EwSymbolToken9 {
    uint32_t lanes_u32[9];
    uint32_t len_u32;
    uint32_t artifact_id_u32;
    uint32_t reserved_u32;
};

struct alignas(8) EwGpuCarrierRecord {
    int64_t f_q32_32 = 0;
    int64_t phi_q32_32 = 0;
    int64_t a_q32_32 = 0;
    int64_t v_q32_32 = 0;
    int32_t f_code = 0;
    int32_t a_code = 0;
    int32_t v_code = 0;
    int32_t i_code = 0;
    uint32_t lane_u32 = 0;
    uint32_t domain_lane[9] = {};
};
static_assert(sizeof(EwGpuCarrierRecord) == 88, "Unexpected GPU carrier record layout");

bool ew_gpu_compute_available();

bool ew_gpu_canonicalize_utf8_strict(
    const uint8_t* bytes_host,
    size_t len,
    std::string& out_canon_utf8,
    bool* out_invalid_utf8,
    uint32_t* out_paragraph_breaks
);

bool ew_gpu_tokenize_symbols_batch(
    const uint8_t* bytes_concat,
    const uint32_t* offsets_u32,
    const uint32_t* lens_u32,
    const uint32_t* artifact_ids_u32,
    uint32_t count_u32,
    EwSymbolToken9* out_tokens,
    uint32_t* out_counts_u32_per_artifact,
    uint32_t max_tokens_per_artifact_u32
);

bool ew_gpu_compute_carrier_triples(
    const int64_t* doppler_q_turns,
    const int64_t* m_q_turns,
    const int64_t* curvature_q_turns,
    const uint16_t* flux_grad_mean_q15,
    const uint16_t* harmonics_mean_q15,
    uint32_t anchors_n,
    const uint32_t* anchor_ids,
    uint32_t ids_n,
    EwCarrierTriple* out_triples
);

bool ew_gpu_compute_overlap_scores(
    const EwGpuCarrierRecord& query,
    const EwGpuCarrierRecord* recs,
    size_t n_recs,
    uint32_t lane_filter_u32,
    const uint32_t* opt_domain_id9,
    std::vector<int64_t>& out_scores_q32_32
);

bool ew_gpu_select_topk(
    const int64_t* scores_q32_32,
    size_t n_scores,
    size_t k_in,
    std::vector<GE_OverlapHit>& out_hits
);
