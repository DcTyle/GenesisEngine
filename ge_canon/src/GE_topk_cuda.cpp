#include "GE_topk_cuda.hpp"

#include <cuda_runtime.h>
#include <vector>

static inline bool ge_cuda_ok(cudaError_t e) { return e == cudaSuccess; }

struct GeTopkCudaCache {
    uint32_t* d_idx_a = nullptr;
    int64_t* d_score_a = nullptr;
    uint32_t* d_idx_b = nullptr;
    int64_t* d_score_b = nullptr;
    size_t cap_bytes_idx = 0;
    size_t cap_bytes_score = 0;

    ~GeTopkCudaCache() {
        if (d_idx_a) cudaFree(d_idx_a);
        if (d_score_a) cudaFree(d_score_a);
        if (d_idx_b) cudaFree(d_idx_b);
        if (d_score_b) cudaFree(d_score_b);
    }
};

static GeTopkCudaCache& ge_topk_cache() {
    static GeTopkCudaCache cache;
    return cache;
}

__global__ void ge_topk_block_kernel(const int64_t* scores, uint32_t n,
                                     uint32_t k,
                                     uint32_t chunk,
                                     uint32_t* out_idx,
                                     int64_t* out_score);

__global__ void ge_topk_merge_kernel(const uint32_t* in_idx, const int64_t* in_score,
                                     uint32_t lists, uint32_t k,
                                     uint32_t* out_idx, int64_t* out_score);

static inline bool ge_copy_hits(uint32_t* d_idx, int64_t* d_score, uint32_t k,
                                std::vector<GE_OverlapHit>& out_hits) {
    std::vector<uint32_t> h_idx(k);
    std::vector<int64_t> h_score(k);
    if (!ge_cuda_ok(cudaMemcpy(h_idx.data(), d_idx, k * sizeof(uint32_t), cudaMemcpyDeviceToHost))) return false;
    if (!ge_cuda_ok(cudaMemcpy(h_score.data(), d_score, k * sizeof(int64_t), cudaMemcpyDeviceToHost))) return false;

    out_hits.clear();
    out_hits.reserve(k);
    for (uint32_t i = 0; i < k; ++i) {
        if (h_score[i] <= 0) continue;
        out_hits.push_back({size_t(h_idx[i]), h_score[i]});
    }
    return true;
}

bool GE_select_topk_cuda(const int64_t* d_scores_q32_32, size_t n_scores,
                         size_t k_in,
                         std::vector<GE_OverlapHit>& out_hits) {
    if (!d_scores_q32_32 || n_scores == 0 || k_in == 0) { out_hits.clear(); return true; }
    uint32_t k = (k_in > 32) ? 32u : uint32_t(k_in);
    uint32_t n = (n_scores > 0xFFFFFFFFu) ? 0xFFFFFFFFu : uint32_t(n_scores);

    // Choose chunk size so we get a reasonable number of blocks; deterministic formula.
    uint32_t chunk = 4096u;
    uint32_t blocks = (n + chunk - 1) / chunk;
    if (blocks == 0) blocks = 1;

    size_t list_bytes_idx = size_t(blocks) * k * sizeof(uint32_t);
    size_t list_bytes_score = size_t(blocks) * k * sizeof(int64_t);

    GeTopkCudaCache& cache = ge_topk_cache();
    if (cache.cap_bytes_idx < list_bytes_idx) {
        if (cache.d_idx_a) cudaFree(cache.d_idx_a);
        if (cache.d_idx_b) cudaFree(cache.d_idx_b);
        cache.d_idx_a = nullptr;
        cache.d_idx_b = nullptr;
        if (!ge_cuda_ok(cudaMalloc((void**)&cache.d_idx_a, list_bytes_idx))) return false;
        if (!ge_cuda_ok(cudaMalloc((void**)&cache.d_idx_b, list_bytes_idx))) { cudaFree(cache.d_idx_a); cache.d_idx_a = nullptr; return false; }
        cache.cap_bytes_idx = list_bytes_idx;
    }
    if (cache.cap_bytes_score < list_bytes_score) {
        if (cache.d_score_a) cudaFree(cache.d_score_a);
        if (cache.d_score_b) cudaFree(cache.d_score_b);
        cache.d_score_a = nullptr;
        cache.d_score_b = nullptr;
        if (!ge_cuda_ok(cudaMalloc((void**)&cache.d_score_a, list_bytes_score))) return false;
        if (!ge_cuda_ok(cudaMalloc((void**)&cache.d_score_b, list_bytes_score))) { cudaFree(cache.d_score_a); cache.d_score_a = nullptr; return false; }
        cache.cap_bytes_score = list_bytes_score;
    }

    uint32_t* d_idx_a = cache.d_idx_a;
    int64_t* d_score_a = cache.d_score_a;
    uint32_t* d_idx_b = cache.d_idx_b;
    int64_t* d_score_b = cache.d_score_b;

    ge_topk_block_kernel<<<blocks, 1>>>(d_scores_q32_32, n, k, chunk, d_idx_a, d_score_a);
    if (!ge_cuda_ok(cudaPeekAtLastError())) return false;

    // Reduce lists by merging pairs on GPU until one list remains.
    uint32_t lists = blocks;
    bool flip = false;
    while (lists > 1) {
        uint32_t out_lists = (lists + 1) / 2;
        const uint32_t* in_idx = flip ? d_idx_b : d_idx_a;
        const int64_t* in_score = flip ? d_score_b : d_score_a;
        uint32_t* out_idx = flip ? d_idx_a : d_idx_b;
        int64_t* out_score = flip ? d_score_a : d_score_b;

        ge_topk_merge_kernel<<<out_lists, 1>>>(in_idx, in_score, lists, k, out_idx, out_score);
        if (!ge_cuda_ok(cudaPeekAtLastError())) return false;

        lists = out_lists;
        flip = !flip;
    }

    uint32_t* final_idx = flip ? d_idx_b : d_idx_a;
    int64_t* final_score = flip ? d_score_b : d_score_a;

    // Device->host copies synchronize the default stream.
    return ge_copy_hits(final_idx, final_score, k, out_hits);
}
