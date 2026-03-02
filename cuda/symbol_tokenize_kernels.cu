#include <cuda_runtime.h>
#include <cstdint>
#include <cstddef>

#include "symbol_tokenize_cuda.hpp"

static __device__ __forceinline__ bool is_ident_start(uint8_t c) {
    return (c=='_') || (c>='A' && c<='Z') || (c>='a' && c<='z');
}
static __device__ __forceinline__ bool is_ident_body(uint8_t c) {
    return is_ident_start(c) || (c>='0' && c<='9');
}

static __device__ __forceinline__ void encode_symbol_9d(const uint8_t* s, uint32_t len, uint32_t* out9) {
    // Pack up to 36 bytes into 9 lanes of 4 bytes each, little-endian.
    // For len < 36, remaining bytes are 0. Deterministic and reversible for len<=36.
    #pragma unroll
    for (int i = 0; i < 9; ++i) out9[i] = 0u;
    uint32_t n = (len > 36u) ? 36u : len;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t lane = i >> 2;
        uint32_t shift = (i & 3u) * 8u;
        out9[lane] |= (uint32_t)s[i] << shift;
    }
}

__global__ void ew_kernel_tokenize_symbols_per_artifact(
    const uint8_t* bytes_concat,
    const uint32_t* offsets_u32,
    const uint32_t* lens_u32,
    const uint32_t* artifact_ids_u32,
    uint32_t count_u32,
    EwSymbolToken9* out_tokens,
    uint32_t* out_counts_u32_per_artifact,
    uint32_t max_tokens_per_artifact_u32
) {
    uint32_t art_i = (uint32_t)blockIdx.x;
    if (art_i >= count_u32) return;

    // One thread scans deterministically; per-artifact parallelism is block-level, not intra-scan.
    if (threadIdx.x != 0) return;

    const uint32_t off = offsets_u32[art_i];
    const uint32_t len = lens_u32[art_i];
    const uint32_t art_id = artifact_ids_u32[art_i];

    const uint8_t* p = bytes_concat + off;
    uint32_t out_count = 0u;

    uint32_t i = 0u;
    while (i < len) {
        uint8_t c = p[i];
        if (!is_ident_start(c)) { i++; continue; }

        uint32_t start = i;
        i++;
        while (i < len && is_ident_body(p[i])) i++;
        uint32_t tok_len = i - start;

        if (out_count < max_tokens_per_artifact_u32) {
            EwSymbolToken9 t{};
            encode_symbol_9d(p + start, tok_len, t.lanes_u32);
            t.len_u32 = tok_len;
            t.artifact_id_u32 = art_id;
            if (tok_len > 36u) {
            uint32_t sfx = 0u;
            uint32_t base = tok_len - 4u;
            for (uint32_t j = 0u; j < 4u; ++j) { sfx |= (uint32_t)p[start + base + j] << (j * 8u); }
            t.reserved_u32 = sfx;
        } else { t.reserved_u32 = 0u; }
            out_tokens[art_i * max_tokens_per_artifact_u32 + out_count] = t;
            out_count++;
        } else {
            // Deterministic truncation; count does not exceed cap.
            break;
        }
    }

    out_counts_u32_per_artifact[art_i] = out_count;
}

extern "C" bool ew_cuda_tokenize_symbols_batch_impl(
    const uint8_t* bytes_concat,
    const uint32_t* offsets_u32,
    const uint32_t* lens_u32,
    const uint32_t* artifact_ids_u32,
    uint32_t count_u32,
    EwSymbolToken9* out_tokens,
    uint32_t* out_counts_u32_per_artifact,
    uint32_t max_tokens_per_artifact_u32
) {
    if (!bytes_concat || !offsets_u32 || !lens_u32 || !artifact_ids_u32 || !out_tokens || !out_counts_u32_per_artifact) return false;
    if (count_u32 == 0u || max_tokens_per_artifact_u32 == 0u) return false;

    // Allocate device buffers
    uint8_t* d_bytes = nullptr;
    uint32_t* d_off = nullptr;
    uint32_t* d_len = nullptr;
    uint32_t* d_ids = nullptr;
    EwSymbolToken9* d_out = nullptr;
    uint32_t* d_counts = nullptr;

    // Compute total bytes from last offset+len on host assumptions: caller provides contiguous concat.
    // Caller must guarantee bytes_concat points to host memory with total bytes = max(offset+len).
    // We conservatively copy total bytes = max(offset+len) computed on host in wrapper; but kernel uses provided pointers.
    // Here we treat bytes_concat as host ptr and copy based on max range.
    uint32_t max_end = 0u;
    // Copy offsets/lens to host temp to compute max_end in a deterministic way (small array).
    // This function is called from host; offsets_u32 and lens_u32 are host pointers.
    for (uint32_t i = 0u; i < count_u32; ++i) {
        uint32_t end = offsets_u32[i] + lens_u32[i];
        if (end > max_end) max_end = end;
    }

    cudaError_t e = cudaMalloc((void**)&d_bytes, (size_t)max_end);
    if (e != cudaSuccess) return false;
    e = cudaMalloc((void**)&d_off, (size_t)count_u32 * sizeof(uint32_t)); if (e != cudaSuccess) return false;
    e = cudaMalloc((void**)&d_len, (size_t)count_u32 * sizeof(uint32_t)); if (e != cudaSuccess) return false;
    e = cudaMalloc((void**)&d_ids, (size_t)count_u32 * sizeof(uint32_t)); if (e != cudaSuccess) return false;
    e = cudaMalloc((void**)&d_out, (size_t)count_u32 * (size_t)max_tokens_per_artifact_u32 * sizeof(EwSymbolToken9)); if (e != cudaSuccess) return false;
    e = cudaMalloc((void**)&d_counts, (size_t)count_u32 * sizeof(uint32_t)); if (e != cudaSuccess) return false;

    e = cudaMemcpy(d_bytes, bytes_concat, (size_t)max_end, cudaMemcpyHostToDevice); if (e != cudaSuccess) return false;
    e = cudaMemcpy(d_off, offsets_u32, (size_t)count_u32 * sizeof(uint32_t), cudaMemcpyHostToDevice); if (e != cudaSuccess) return false;
    e = cudaMemcpy(d_len, lens_u32, (size_t)count_u32 * sizeof(uint32_t), cudaMemcpyHostToDevice); if (e != cudaSuccess) return false;
    e = cudaMemcpy(d_ids, artifact_ids_u32, (size_t)count_u32 * sizeof(uint32_t), cudaMemcpyHostToDevice); if (e != cudaSuccess) return false;

    dim3 grid(count_u32, 1, 1);
    dim3 block(32, 1, 1);
    ew_kernel_tokenize_symbols_per_artifact<<<grid, block>>>(d_bytes, d_off, d_len, d_ids, count_u32, d_out, d_counts, max_tokens_per_artifact_u32);
    e = cudaGetLastError();
    if (e != cudaSuccess) return false;
    e = cudaDeviceSynchronize();
    if (e != cudaSuccess) return false;

    e = cudaMemcpy(out_tokens, d_out, (size_t)count_u32 * (size_t)max_tokens_per_artifact_u32 * sizeof(EwSymbolToken9), cudaMemcpyDeviceToHost);
    if (e != cudaSuccess) return false;
    e = cudaMemcpy(out_counts_u32_per_artifact, d_counts, (size_t)count_u32 * sizeof(uint32_t), cudaMemcpyDeviceToHost);
    if (e != cudaSuccess) return false;

    cudaFree(d_bytes);
    cudaFree(d_off);
    cudaFree(d_len);
    cudaFree(d_ids);
    cudaFree(d_out);
    cudaFree(d_counts);
    return true;
}
