#include "GE_overlap_cuda.hpp"
#include "GE_topk_cuda.hpp"

#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
#include <cuda_runtime.h>
#include <vector>

static inline bool ge_cuda_ok(cudaError_t e) { return e == cudaSuccess; }

struct GeOverlapCudaCache {
    GeCarrierDev* d_recs = nullptr;
    int64_t* d_scores = nullptr;
    uint32_t* d_domain = nullptr;
    size_t cap_recs = 0;
    size_t cap_scores = 0;

    ~GeOverlapCudaCache() {
        if (d_recs) cudaFree(d_recs);
        if (d_scores) cudaFree(d_scores);
        if (d_domain) cudaFree(d_domain);
    }
};

static GeOverlapCudaCache& ge_overlap_cache() {
    static GeOverlapCudaCache cache;
    return cache;
}

struct GeCarrierDev {
    int64_t f_q32_32;
    int64_t phi_q32_32;
    int64_t a_q32_32;
    int64_t v_q32_32;
    int32_t f_code,a_code,v_code,i_code;
    uint8_t lane_u8;
    uint32_t domain_lane[9];
};

__global__ void ge_overlap_scores_kernel(const GeCarrierDev q, const GeCarrierDev* recs, int64_t* out, size_t n,
                                        uint8_t lane_filter, const uint32_t* opt_domain_id9);

static inline GeCarrierDev ge_make_dev(const GE_CorpusAnchorRecord& r) {
    GeCarrierDev d{};
    d.f_q32_32 = r.carrier.f_carrier_turns_q32_32;
    d.phi_q32_32 = r.carrier.phi_carrier_turns_q32_32;
    d.a_q32_32 = r.carrier.a_carrier_turns_q32_32;
    d.v_q32_32 = r.carrier.v_carrier_turns_q32_32;
    d.f_code = r.spider.f_code;
    d.a_code = r.spider.a_code;
    d.v_code = r.spider.v_code;
    d.i_code = r.spider.i_code;
    d.lane_u8 = r.lane_u8;
    for (int i=0;i<9;i++) d.domain_lane[i] = r.domain_id9.lane_u32[i];
    return d;
}

bool GE_retrieve_topk_cuda(const GE_CorpusAnchorStore& store,
                           const GE_CorpusAnchorRecord& query,
                           uint8_t lane_u8,
                           const EwId9* opt_domain_id9,
                           size_t k,
                           std::vector<GE_OverlapHit>& out_hits) {
#if !(defined(EW_ENABLE_CUDA) && EW_ENABLE_CUDA)
    (void)store; (void)query; (void)lane_u8; (void)opt_domain_id9; (void)k; (void)out_hits;
    return false;
#else
    const size_t n = store.records.size();
    if (n == 0) { out_hits.clear(); return true; }

    std::vector<GeCarrierDev> h_recs;
    h_recs.reserve(n);
    for (size_t i=0;i<n;i++) h_recs.push_back(ge_make_dev(store.records[i]));

    GeCarrierDev q = ge_make_dev(query);

    GeOverlapCudaCache& cache = ge_overlap_cache();
    if (cache.cap_recs < n) {
        if (cache.d_recs) cudaFree(cache.d_recs);
        cache.d_recs = nullptr;
        if (!ge_cuda_ok(cudaMalloc((void**)&cache.d_recs, n * sizeof(GeCarrierDev)))) return false;
        cache.cap_recs = n;
    }
    if (cache.cap_scores < n) {
        if (cache.d_scores) cudaFree(cache.d_scores);
        cache.d_scores = nullptr;
        if (!ge_cuda_ok(cudaMalloc((void**)&cache.d_scores, n * sizeof(int64_t)))) return false;
        cache.cap_scores = n;
    }

    if (!ge_cuda_ok(cudaMemcpy(cache.d_recs, h_recs.data(), n * sizeof(GeCarrierDev), cudaMemcpyHostToDevice))) {
        return false;
    }

    uint32_t* d_domain = nullptr;
    if (opt_domain_id9) {
        if (!cache.d_domain) {
            if (!ge_cuda_ok(cudaMalloc((void**)&cache.d_domain, 9 * sizeof(uint32_t)))) return false;
        }
        if (!ge_cuda_ok(cudaMemcpy(cache.d_domain, opt_domain_id9->lane_u32, 9 * sizeof(uint32_t), cudaMemcpyHostToDevice))) {
            return false;
        }
        d_domain = cache.d_domain;
    }

    const int threads = 256;
    const int blocks = int((n + size_t(threads) - 1) / size_t(threads));
    ge_overlap_scores_kernel<<<blocks, threads>>>(q, cache.d_recs, cache.d_scores, n, lane_u8, d_domain);
    if (!ge_cuda_ok(cudaPeekAtLastError())) return false;

    // Default stream ordering guarantees scores are ready for top-k.
    return GE_select_topk_cuda(cache.d_scores, n, k, out_hits);
#endif
}
#else
bool GE_retrieve_topk_cuda(const GE_CorpusAnchorStore& store,
                           const GE_CorpusAnchorRecord& query,
                           uint8_t lane_u8,
                           const EwId9* opt_domain_id9,
                           size_t k,
                           std::vector<GE_OverlapHit>& out_hits) {
    (void)store;
    (void)query;
    (void)lane_u8;
    (void)opt_domain_id9;
    (void)k;
    out_hits.clear();
    return false;
}
#endif
