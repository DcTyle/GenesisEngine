#include "GE_canonicalize_cuda.hpp"

#include <cuda_runtime.h>
#include <vector>

static inline void ge_cuda_check(cudaError_t e, const char* what) {
    if (e != cudaSuccess) {
        // Fail closed: determinism contract expects CUDA path.
        // Caller will treat false as rejection.
        (void)what;
    }
}

struct GE_CanonDeviceOut {
    uint32_t out_len_u32;
    uint32_t invalid_utf8_u32;
    uint32_t paragraph_breaks_u32;
    uint32_t reserved_u32;
};

__global__ void ge_canonicalize_kernel(const uint8_t* in_bytes, uint32_t in_len,
                                       uint8_t* out_bytes, uint32_t out_cap,
                                       GE_CanonDeviceOut* out_meta);

struct GeCanonCudaCache {
    uint8_t* d_in = nullptr;
    uint8_t* d_out = nullptr;
    GE_CanonDeviceOut* d_meta = nullptr;
    uint32_t cap_in = 0;
    uint32_t cap_out = 0;

    ~GeCanonCudaCache() {
        if (d_in) cudaFree(d_in);
        if (d_out) cudaFree(d_out);
        if (d_meta) cudaFree(d_meta);
    }
};

static GeCanonCudaCache& ge_canon_cache() {
    static GeCanonCudaCache cache;
    return cache;
}

bool GE_canonicalize_utf8_strict_cuda(const uint8_t* bytes_host, size_t len,
                                     std::string& out_canon_utf8,
                                     GE_CorpusCanonicalizeStats& stats) {
    stats.bytes_in_u64 += uint64_t(len);
    if (!bytes_host || len == 0) {
        out_canon_utf8.clear();
        return true;
    }

    const uint32_t in_len_u32 = (len > 0xFFFFFFFFu) ? 0xFFFFFFFFu : uint32_t(len);
    // Worst-case output cap: input length (canonicalization never expands beyond +0 for this MVP).
    const uint32_t out_cap_u32 = in_len_u32;

    GeCanonCudaCache& cache = ge_canon_cache();
    if (cache.cap_in < in_len_u32) {
        if (cache.d_in) cudaFree(cache.d_in);
        cache.d_in = nullptr;
        if (cudaMalloc((void**)&cache.d_in, in_len_u32) != cudaSuccess) return false;
        cache.cap_in = in_len_u32;
    }
    if (cache.cap_out < out_cap_u32) {
        if (cache.d_out) cudaFree(cache.d_out);
        cache.d_out = nullptr;
        if (cudaMalloc((void**)&cache.d_out, out_cap_u32) != cudaSuccess) return false;
        cache.cap_out = out_cap_u32;
    }
    if (!cache.d_meta) {
        if (cudaMalloc((void**)&cache.d_meta, sizeof(GE_CanonDeviceOut)) != cudaSuccess) return false;
    }

    cudaError_t e = cudaMemcpy(cache.d_in, bytes_host, in_len_u32, cudaMemcpyHostToDevice);
    if (e != cudaSuccess) return false;

    const dim3 blocks(1);
    const dim3 threads(1);
    ge_canonicalize_kernel<<<blocks, threads>>>(cache.d_in, in_len_u32, cache.d_out, out_cap_u32, cache.d_meta);
    if (cudaPeekAtLastError() != cudaSuccess) return false;

    GE_CanonDeviceOut meta{};
    e = cudaMemcpy(&meta, cache.d_meta, sizeof(meta), cudaMemcpyDeviceToHost);
    if (e != cudaSuccess) return false;

    if (meta.invalid_utf8_u32) {
        stats.invalid_utf8_rejects_u64 += 1;
        return false;
    }

    std::vector<uint8_t> out(meta.out_len_u32);
    if (meta.out_len_u32) {
        e = cudaMemcpy(out.data(), cache.d_out, meta.out_len_u32, cudaMemcpyDeviceToHost);
        if (e != cudaSuccess) return false;
    }

    out_canon_utf8.assign(reinterpret_cast<const char*>(out.data()), out.size());
    stats.bytes_out_u64 += uint64_t(out.size());
    return true;
}
