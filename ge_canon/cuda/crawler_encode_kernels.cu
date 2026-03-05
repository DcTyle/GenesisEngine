#include <cuda_runtime.h>
#include <cstdint>
#include <cstddef>

#include "anchor.hpp"
#include "crawler_encode_cuda.hpp"

// Very small, deterministic byte->SpiderCode4 encoder.
// This is designed as a production-safe ingestion primitive:
// - O(len) streaming reduction
// - deterministic across runs
// - no reliance on host-side parsing

struct Accum {
    uint64_t sum;
    uint64_t sum2;
    uint64_t xors;
    uint32_t count;
    int64_t fcos1;
    int64_t fsin1;
    int64_t fcos3;
    int64_t fsin3;
};

__device__ __forceinline__ uint32_t rotl32(uint32_t x, int r) {
    return (x << r) | (x >> (32 - r));
}

// Deterministic, tiny Fourier-like carrier projection.
// 32-point Q15 cosine/sine tables for k=1 base; k=3 uses (idx*3)&31.
__device__ __constant__ int16_t EW_LUT_COS_Q15_32[32] = {
    32767, 32138, 30273, 27245, 23170, 18204, 12539, 6392,
    0, -6392, -12539, -18204, -23170, -27245, -30273, -32138,
    -32767, -32138, -30273, -27245, -23170, -18204, -12539, -6392,
    0, 6392, 12539, 18204, 23170, 27245, 30273, 32138
};

__device__ __constant__ int16_t EW_LUT_SIN_Q15_32[32] = {
    0, 6392, 12539, 18204, 23170, 27245, 30273, 32138,
    32767, 32138, 30273, 27245, 23170, 18204, 12539, 6392,
    0, -6392, -12539, -18204, -23170, -27245, -30273, -32138,
    -32767, -32138, -30273, -27245, -23170, -18204, -12539, -6392
};

__device__ __forceinline__ int16_t ew_cos_q15_32(uint32_t idx) { return EW_LUT_COS_Q15_32[idx & 31u]; }
__device__ __forceinline__ int16_t ew_sin_q15_32(uint32_t idx) { return EW_LUT_SIN_Q15_32[idx & 31u]; }

__global__ void ew_kernel_encode_spider4(const uint8_t* bytes, size_t len, SpiderCode4* out) {
    // One-block reduction for deterministic result.
    // len is bounded by caller chunking.
    __shared__ Accum sh[256];
    const uint32_t tid = (uint32_t)threadIdx.x;
    Accum a{};
    a.sum = 0;
    a.sum2 = 0;
    a.xors = 0;
    a.count = 0;
    a.fcos1 = 0;
    a.fsin1 = 0;
    a.fcos3 = 0;
    a.fsin3 = 0;

    // Stride loop.
    for (size_t i = tid; i < len; i += (size_t)blockDim.x) {
        const uint8_t b = bytes[i];
        a.sum += (uint64_t)b;
        a.sum2 += (uint64_t)b * (uint64_t)b;
        a.xors ^= (uint64_t)(rotl32((uint32_t)b + 0x9E3779B9u, (int)(i & 31u))) << (i & 7u);
        const uint32_t idx = (uint32_t)i & 31u;
        const int16_t c1 = ew_cos_q15_32(idx);
        const int16_t s1 = ew_sin_q15_32(idx);
        const int16_t c3 = ew_cos_q15_32((idx * 3u) & 31u);
        const int16_t s3 = ew_sin_q15_32((idx * 3u) & 31u);
        a.fcos1 += (int64_t)c1 * (int64_t)b;
        a.fsin1 += (int64_t)s1 * (int64_t)b;
        a.fcos3 += (int64_t)c3 * (int64_t)b;
        a.fsin3 += (int64_t)s3 * (int64_t)b;
        a.count += 1u;
    }
    sh[tid] = a;
    __syncthreads();

    // Deterministic tree reduce.
    for (uint32_t step = 128; step > 0; step >>= 1) {
        if (tid < step) {
            sh[tid].sum += sh[tid + step].sum;
            sh[tid].sum2 += sh[tid + step].sum2;
            sh[tid].xors ^= sh[tid + step].xors;
            sh[tid].count += sh[tid + step].count;
            sh[tid].fcos1 += sh[tid + step].fcos1;
            sh[tid].fsin1 += sh[tid + step].fsin1;
            sh[tid].fcos3 += sh[tid + step].fcos3;
            sh[tid].fsin3 += sh[tid + step].fsin3;
        }
        __syncthreads();
    }

    if (tid == 0) {
        const Accum r = sh[0];
        // Map to carrier values.
        // f_code: signed from centered mean and xor.
        const uint64_t mean = (r.count > 0u) ? (r.sum / (uint64_t)r.count) : 0u;
        const int64_t centered = (int64_t)mean - 128;
        // Inject Fourier-like carrier observables into f/a. This is a deterministic
        // phase-proxy that improves stability of code-space mapping.
        const int64_t fc = (r.fcos1 >> 10) + (r.fcos3 >> 12);
        const int32_t f_code = (int32_t)(centered * 256 + (int32_t)(r.xors & 0xFFFFu) + (int32_t)(fc & 0x7FFFF));

        // a_code: variance proxy.
        const uint64_t mean2 = (r.count > 0u) ? (r.sum2 / (uint64_t)r.count) : 0u;
        uint64_t var = 0;
        if (mean2 > mean * mean) var = mean2 - mean * mean;
        const int64_t fm = llabs(r.fcos1) + llabs(r.fsin1) + (llabs(r.fcos3) >> 1) + (llabs(r.fsin3) >> 1);
        uint32_t a = (uint32_t)((var + (uint64_t)(fm >> 14)) & 0xFFFFu);
        if (a == 0u) a = 1u;

        // v_code: payload headroom proxy based on length bucket.
        uint32_t v = 0;
        if (len >= 65536) v = 65535;
        else v = (uint32_t)((len * 65535ULL) / 65536ULL);
        if (v == 0u) v = 1u;

        // i_code: throughput proxy based on count and xors.
        uint32_t i = (uint32_t)((r.count & 0xFFFFu) ^ (uint32_t)((r.xors >> 16) & 0xFFFFu));
        if (i == 0u) i = 1u;

        SpiderCode4 sc;
        sc.f_code = f_code;
        sc.a_code = (uint16_t)a;
        sc.v_code = (uint16_t)v;
        sc.i_code = (uint16_t)i;
        *out = sc;
    }
}


// -----------------------------------------------------------------------------
// Batch encoder: parallel per-document encoding on GPU.
// -----------------------------------------------------------------------------

__global__ void ew_kernel_encode_spider4_batch(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    SpiderCode4* out_arr
) {
    const uint32_t doc = (uint32_t)blockIdx.x;
    if (doc >= n_docs) return;
    const uint32_t off = offsets_u32[doc];
    const uint32_t len0 = lengths_u32[doc];
    if ((size_t)off >= bytes_concat_len) return;
    size_t len = (size_t)len0;
    if ((size_t)off + len > bytes_concat_len) {
        if ((size_t)off < bytes_concat_len) len = bytes_concat_len - (size_t)off;
        else len = 0;
    }
    if (len == 0) return;

    __shared__ Accum shb[256];
    const uint32_t tid = (uint32_t)threadIdx.x;
    Accum a{};
    a.sum = 0; a.sum2 = 0; a.xors = 0; a.count = 0;
    a.fcos1 = 0; a.fsin1 = 0; a.fcos3 = 0; a.fsin3 = 0;
    for (size_t i = tid; i < len; i += (size_t)blockDim.x) {
        const uint8_t b = bytes_concat[(size_t)off + i];
        a.sum += (uint64_t)b;
        a.sum2 += (uint64_t)b * (uint64_t)b;
        const uint64_t gi = (uint64_t)off + (uint64_t)i;
        a.xors ^= (uint64_t)(rotl32((uint32_t)b + 0x9E3779B9u, (int)(gi & 31u))) << (gi & 7u);
        const uint32_t idx = (uint32_t)gi & 31u;
        const int16_t c1 = ew_cos_q15_32(idx);
        const int16_t s1 = ew_sin_q15_32(idx);
        const int16_t c3 = ew_cos_q15_32((idx * 3u) & 31u);
        const int16_t s3 = ew_sin_q15_32((idx * 3u) & 31u);
        a.fcos1 += (int64_t)c1 * (int64_t)b;
        a.fsin1 += (int64_t)s1 * (int64_t)b;
        a.fcos3 += (int64_t)c3 * (int64_t)b;
        a.fsin3 += (int64_t)s3 * (int64_t)b;
        a.count += 1u;
    }
    shb[tid] = a;
    __syncthreads();
    for (uint32_t step = 128; step > 0; step >>= 1) {
        if (tid < step) {
            shb[tid].sum += shb[tid + step].sum;
            shb[tid].sum2 += shb[tid + step].sum2;
            shb[tid].xors ^= shb[tid + step].xors;
            shb[tid].count += shb[tid + step].count;
            shb[tid].fcos1 += shb[tid + step].fcos1;
            shb[tid].fsin1 += shb[tid + step].fsin1;
            shb[tid].fcos3 += shb[tid + step].fcos3;
            shb[tid].fsin3 += shb[tid + step].fsin3;
        }
        __syncthreads();
    }
    if (tid == 0) {
        const Accum r = shb[0];
        const uint64_t mean = (r.count > 0u) ? (r.sum / (uint64_t)r.count) : 0u;
        const int64_t centered = (int64_t)mean - 128;
        const int64_t fc = (r.fcos1 >> 10) + (r.fcos3 >> 12);
        const int32_t f_code = (int32_t)(centered * 256 + (int32_t)(r.xors & 0xFFFFu) + (int32_t)(fc & 0x7FFFF));
        const uint64_t mean2 = (r.count > 0u) ? (r.sum2 / (uint64_t)r.count) : 0u;
        uint64_t var = 0;
        if (mean2 > mean * mean) var = mean2 - mean * mean;
        const int64_t fm = llabs(r.fcos1) + llabs(r.fsin1) + (llabs(r.fcos3) >> 1) + (llabs(r.fsin3) >> 1);
        uint32_t a_code = (uint32_t)((var + (uint64_t)(fm >> 14)) & 0xFFFFu);
        if (a_code == 0u) a_code = 1u;
        // v_code: payload headroom proxy based on length relative to chunk_bytes.
        // Derived scaling: map up to one chunk of bytes into full-scale voltage.
        uint32_t v_code = (uint32_t)((len * (uint64_t)V_MAX) / (uint64_t)((chunk_bytes == 0) ? 1 : chunk_bytes));
        if (v_code > V_MAX) v_code = V_MAX;
        if (v_code == 0u) v_code = 1u;
        uint32_t i_code = (uint32_t)((r.count & 0xFFFFu) ^ (uint32_t)((r.xors >> 16) & 0xFFFFu));
        if (i_code == 0u) i_code = 1u;
        SpiderCode4 sc;
        sc.f_code = f_code;
        sc.a_code = (uint16_t)a_code;
        sc.v_code = (uint16_t)v_code;
        sc.i_code = (uint16_t)i_code;
        out_arr[doc] = sc;
    }
}

// -----------------------------------------------------------------------------
// Batch encoder with harmonics32 output.
// Harmonics bins are deterministic magnitudes over 32 phase bins (idx&31).
// -----------------------------------------------------------------------------

__global__ void ew_kernel_encode_spider4_batch_h32(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    SpiderCode4* out_arr,
    uint16_t* out_harmonics_q15,
    uint16_t* out_harmonics_mean_q15
) {
    const uint32_t doc = (uint32_t)blockIdx.x;
    if (doc >= n_docs) return;
    const uint32_t off = offsets_u32[doc];
    const uint32_t len0 = lengths_u32[doc];
    if ((size_t)off >= bytes_concat_len) return;
    size_t len = (size_t)len0;
    if ((size_t)off + len > bytes_concat_len) {
        if ((size_t)off < bytes_concat_len) len = bytes_concat_len - (size_t)off;
        else len = 0;
    }
    if (len == 0) return;

    // Compute 32-bin phase histogram deterministically.
    __shared__ uint32_t sh_bins[32];
    const uint32_t tid = (uint32_t)threadIdx.x;
    if (tid < 32u) {
        uint64_t sum = 0;
        for (size_t i = (size_t)tid; i < len; i += 32u) {
            sum += (uint64_t)bytes_concat[(size_t)off + i];
        }
        sh_bins[tid] = (uint32_t)(sum & 0xFFFFFFFFu);
    }
    __syncthreads();

    // Reuse the existing SpiderCode4 batch reduction for carrier observables.
    __shared__ Accum shb[256];
    Accum a{};
    a.sum = 0; a.sum2 = 0; a.xors = 0; a.count = 0;
    a.fcos1 = 0; a.fsin1 = 0; a.fcos3 = 0; a.fsin3 = 0;
    for (size_t i = tid; i < len; i += (size_t)blockDim.x) {
        const uint8_t b = bytes_concat[(size_t)off + i];
        a.sum += (uint64_t)b;
        a.sum2 += (uint64_t)b * (uint64_t)b;
        const uint64_t gi = (uint64_t)off + (uint64_t)i;
        a.xors ^= (uint64_t)(rotl32((uint32_t)b + 0x9E3779B9u, (int)(gi & 31u))) << (gi & 7u);
        const uint32_t idx = (uint32_t)gi & 31u;
        const int16_t c1 = ew_cos_q15_32(idx);
        const int16_t s1 = ew_sin_q15_32(idx);
        const int16_t c3 = ew_cos_q15_32((idx * 3u) & 31u);
        const int16_t s3 = ew_sin_q15_32((idx * 3u) & 31u);
        a.fcos1 += (int64_t)c1 * (int64_t)b;
        a.fsin1 += (int64_t)s1 * (int64_t)b;
        a.fcos3 += (int64_t)c3 * (int64_t)b;
        a.fsin3 += (int64_t)s3 * (int64_t)b;
        a.count += 1u;
    }
    shb[tid] = a;
    __syncthreads();
    for (uint32_t step = 128; step > 0; step >>= 1) {
        if (tid < step) {
            shb[tid].sum += shb[tid + step].sum;
            shb[tid].sum2 += shb[tid + step].sum2;
            shb[tid].xors ^= shb[tid + step].xors;
            shb[tid].count += shb[tid + step].count;
            shb[tid].fcos1 += shb[tid + step].fcos1;
            shb[tid].fsin1 += shb[tid + step].fsin1;
            shb[tid].fcos3 += shb[tid + step].fcos3;
            shb[tid].fsin3 += shb[tid + step].fsin3;
        }
        __syncthreads();
    }

    if (tid == 0) {
        const Accum r = shb[0];
        const uint64_t mean = (r.count > 0u) ? (r.sum / (uint64_t)r.count) : 0u;
        const int64_t centered = (int64_t)mean - 128;
        const int64_t fc = (r.fcos1 >> 10) + (r.fcos3 >> 12);
        const int32_t f_code = (int32_t)(centered * 256 + (int32_t)(r.xors & 0xFFFFu) + (int32_t)(fc & 0x7FFFF));
        const uint64_t mean2 = (r.count > 0u) ? (r.sum2 / (uint64_t)r.count) : 0u;
        uint64_t var = 0;
        if (mean2 > mean * mean) var = mean2 - mean * mean;
        const int64_t fm = llabs(r.fcos1) + llabs(r.fsin1) + (llabs(r.fcos3) >> 1) + (llabs(r.fsin3) >> 1);
        uint32_t a_code = (uint32_t)((var + (uint64_t)(fm >> 14)) & 0xFFFFu);
        if (a_code == 0u) a_code = 1u;
        uint32_t v_code = (uint32_t)((len * (uint64_t)V_MAX) / (uint64_t)((chunk_bytes == 0) ? 1 : chunk_bytes));
        if (v_code > V_MAX) v_code = V_MAX;
        if (v_code == 0u) v_code = 1u;
        uint32_t i_code = (uint32_t)((r.count & 0xFFFFu) ^ (uint32_t)((r.xors >> 16) & 0xFFFFu));
        if (i_code == 0u) i_code = 1u;

        out_arr[doc] = SpiderCode4{f_code, (uint16_t)a_code, (uint16_t)v_code, (uint16_t)i_code};

        // Normalize bins to Q15.
        // Max bin sum is approx ceil(len/32)*255.
        const uint32_t max_count = (uint32_t)((len + 31u) / 32u);
        const uint32_t denom = (max_count == 0u) ? 1u : (max_count * 255u);
        for (uint32_t k = 0; k < 32u; ++k) {
            const uint32_t s = sh_bins[k];
            uint32_t q15 = (denom == 0u) ? 0u : (uint32_t)((s * 32767ull) / (uint64_t)denom);
            if (q15 > 32767u) q15 = 32767u;
            out_harmonics_q15[doc * 32u + k] = (uint16_t)q15;
            // Accumulate for mean in Q15.
            if (out_harmonics_mean_q15) sh_bins[k] = q15; // reuse sh_bins as scratch for mean
        }
        if (out_harmonics_mean_q15) {
            uint64_t sum_q15 = 0;
            for (uint32_t k = 0; k < 32u; ++k) sum_q15 += (uint64_t)sh_bins[k];
            uint32_t mean_q15 = (uint32_t)(sum_q15 / 32u);
            if (mean_q15 > 32767u) mean_q15 = 32767u;
            out_harmonics_mean_q15[doc] = (uint16_t)mean_q15;
        }
    }
}

// -----------------------------------------------------------------------------
// Batch chunk-stream encoder: emits per-chunk SpiderCode4 in doc-major order.
// chunk index = doc * max_chunks_per_doc + c
// -----------------------------------------------------------------------------

__global__ void ew_kernel_encode_spider4_batch_chunks(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    uint32_t max_chunks_per_doc,
    SpiderCode4* out_chunk_arr,
    uint32_t* out_chunk_counts
) {
    const uint32_t gid = (uint32_t)blockIdx.x;
    const uint32_t doc = gid / max_chunks_per_doc;
    const uint32_t c = gid - doc * max_chunks_per_doc;
    if (doc >= n_docs) return;
    const uint32_t off0 = offsets_u32[doc];
    const uint32_t len0 = lengths_u32[doc];
    if ((size_t)off0 >= bytes_concat_len) return;
    size_t len = (size_t)len0;
    if (chunk_bytes == 0) chunk_bytes = 65536;
    // Derived cap per doc: bounded by configured chunk stream budget.
    const size_t cap_doc = (size_t)chunk_bytes * (size_t)((max_chunks_per_doc == 0u) ? 1u : max_chunks_per_doc);
    if (len > cap_doc) len = cap_doc;
    if ((size_t)off0 + len > bytes_concat_len) {
        if ((size_t)off0 < bytes_concat_len) len = bytes_concat_len - (size_t)off0;
        else len = 0;
    }
    if (len == 0) return;
    // chunk_bytes already enforced above.
    const uint32_t n_chunks = (uint32_t)((len + chunk_bytes - 1) / chunk_bytes);
    if (c == 0) {
        // Write count once per doc.
        out_chunk_counts[doc] = (n_chunks > max_chunks_per_doc) ? max_chunks_per_doc : n_chunks;
    }
    if (c >= n_chunks || c >= max_chunks_per_doc) {
        // Leave output as zeros (will be ignored by reducer).
        return;
    }
    const size_t c_off = (size_t)off0 + (size_t)c * chunk_bytes;
    size_t c_len = chunk_bytes;
    if (c_off + c_len > (size_t)off0 + len) c_len = ((size_t)off0 + len) - c_off;

    // Reuse the same deterministic Fourier-like projection per chunk.
    __shared__ Accum shb[256];
    const uint32_t tid = (uint32_t)threadIdx.x;
    Accum a{};
    a.sum = 0; a.sum2 = 0; a.xors = 0; a.count = 0;
    a.fcos1 = 0; a.fsin1 = 0; a.fcos3 = 0; a.fsin3 = 0;
    for (size_t i = tid; i < c_len; i += (size_t)blockDim.x) {
        const uint8_t b = bytes_concat[c_off + i];
        a.sum += (uint64_t)b;
        a.sum2 += (uint64_t)b * (uint64_t)b;
        const uint64_t gi = (uint64_t)c_off + (uint64_t)i;
        a.xors ^= (uint64_t)(rotl32((uint32_t)b + 0x9E3779B9u, (int)(gi & 31u))) << (gi & 7u);
        const uint32_t idx = (uint32_t)gi & 31u;
        const int16_t c1 = ew_cos_q15_32(idx);
        const int16_t s1 = ew_sin_q15_32(idx);
        const int16_t c3p = ew_cos_q15_32((idx * 3u) & 31u);
        const int16_t s3p = ew_sin_q15_32((idx * 3u) & 31u);
        a.fcos1 += (int64_t)c1 * (int64_t)b;
        a.fsin1 += (int64_t)s1 * (int64_t)b;
        a.fcos3 += (int64_t)c3p * (int64_t)b;
        a.fsin3 += (int64_t)s3p * (int64_t)b;
        a.count += 1u;
    }
    shb[tid] = a;
    __syncthreads();
    for (uint32_t step = 128; step > 0; step >>= 1) {
        if (tid < step) {
            shb[tid].sum += shb[tid + step].sum;
            shb[tid].sum2 += shb[tid + step].sum2;
            shb[tid].xors ^= shb[tid + step].xors;
            shb[tid].count += shb[tid + step].count;
            shb[tid].fcos1 += shb[tid + step].fcos1;
            shb[tid].fsin1 += shb[tid + step].fsin1;
            shb[tid].fcos3 += shb[tid + step].fcos3;
            shb[tid].fsin3 += shb[tid + step].fsin3;
        }
        __syncthreads();
    }
    if (tid == 0) {
        const Accum r = shb[0];
        const uint64_t mean = (r.count > 0u) ? (r.sum / (uint64_t)r.count) : 0u;
        const int64_t centered = (int64_t)mean - 128;
        const int64_t fc = (r.fcos1 >> 10) + (r.fcos3 >> 12);
        const int32_t f_code = (int32_t)(centered * 256 + (int32_t)(r.xors & 0xFFFFu) + (int32_t)(fc & 0x7FFFF));
        const uint64_t mean2 = (r.count > 0u) ? (r.sum2 / (uint64_t)r.count) : 0u;
        uint64_t var = 0;
        if (mean2 > mean * mean) var = mean2 - mean * mean;
        const int64_t fm = llabs(r.fcos1) + llabs(r.fsin1) + (llabs(r.fcos3) >> 1) + (llabs(r.fsin3) >> 1);
        uint32_t a_code = (uint32_t)((var + (uint64_t)(fm >> 14)) & 0xFFFFu);
        if (a_code == 0u) a_code = 1u;
        uint32_t v_code = (uint32_t)((c_len * (uint64_t)V_MAX) / (uint64_t)((chunk_bytes == 0) ? 1 : chunk_bytes));
        if (v_code > V_MAX) v_code = V_MAX;
        if (v_code == 0u) v_code = 1u;
        uint32_t i_code = (uint32_t)((r.count & 0xFFFFu) ^ (uint32_t)((r.xors >> 16) & 0xFFFFu));
        if (i_code == 0u) i_code = 1u;
        SpiderCode4 sc;
        sc.f_code = f_code;
        sc.a_code = (uint16_t)a_code;
        sc.v_code = (uint16_t)v_code;
        sc.i_code = (uint16_t)i_code;
        out_chunk_arr[doc * max_chunks_per_doc + c] = sc;
    }
}

__global__ void ew_kernel_reduce_spider4_docs_from_chunks(
    const SpiderCode4* chunk_arr,
    const uint32_t* chunk_counts,
    uint32_t n_docs,
    uint32_t max_chunks_per_doc,
    SpiderCode4* out_page_arr
) {
    const uint32_t doc = (uint32_t)blockIdx.x;
    if (doc >= n_docs) return;
    const uint32_t n = chunk_counts[doc];
    if (n == 0) return;
    // Deterministic reduction matching ew_kernel_reduce_spider4 semantics.
    __shared__ int64_t sh_f[256];
    __shared__ uint32_t sh_a[256];
    __shared__ uint32_t sh_v[256];
    __shared__ uint32_t sh_i[256];
    const uint32_t tid = (uint32_t)threadIdx.x;
    int64_t f = 0;
    uint32_t a = 0, v = 0, i = 0;
    const uint32_t base = doc * max_chunks_per_doc;
    for (uint32_t idx = tid; idx < n; idx += (uint32_t)blockDim.x) {
        const SpiderCode4 sc = chunk_arr[base + idx];
        f += (int64_t)sc.f_code;
        if ((uint32_t)sc.a_code > a) a = (uint32_t)sc.a_code;
        v += (uint32_t)sc.v_code;
        i += (uint32_t)sc.i_code;
    }
    sh_f[tid] = f;
    sh_a[tid] = a;
    sh_v[tid] = v;
    sh_i[tid] = i;
    __syncthreads();
    for (uint32_t step = 128; step > 0; step >>= 1) {
        if (tid < step) {
            sh_f[tid] += sh_f[tid + step];
            sh_v[tid] += sh_v[tid + step];
            sh_i[tid] += sh_i[tid + step];
            if (sh_a[tid + step] > sh_a[tid]) sh_a[tid] = sh_a[tid + step];
        }
        __syncthreads();
    }
    if (tid == 0) {
        SpiderCode4 sc;
        sc.f_code = (int32_t)sh_f[0];
        sc.a_code = (uint16_t)(sh_a[0] ? sh_a[0] : 1u);
        uint32_t vv = sh_v[0]; if (vv > 65535u) vv = 65535u;
        uint32_t ii = sh_i[0]; if (ii > 65535u) ii = 65535u;
        sc.v_code = (uint16_t)(vv ? vv : 1u);
        sc.i_code = (uint16_t)(ii ? ii : 1u);
        out_page_arr[doc] = sc;
    }
}

extern "C" bool ew_cuda_encode_spider4_batch(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    SpiderCode4* out_arr
) {
    (void)chunk_bytes; // reserved for future chunked-per-doc; current kernel is streaming.
    if (!bytes_concat || bytes_concat_len == 0 || !offsets_u32 || !lengths_u32 || n_docs == 0 || !out_arr) return false;
    uint8_t* d_bytes = nullptr;
    uint32_t* d_off = nullptr;
    uint32_t* d_len = nullptr;
    SpiderCode4* d_out = nullptr;

    // Cap total concat bytes for safety.
    // Caller provides a bounded concat buffer; do not impose an additional fixed cap.
    const size_t cap_total = bytes_concat_len;
    if (cudaMalloc(&d_bytes, cap_total) != cudaSuccess) return false;
    if (cudaMalloc(&d_off, sizeof(uint32_t) * (size_t)n_docs) != cudaSuccess) { cudaFree(d_bytes); return false; }
    if (cudaMalloc(&d_len, sizeof(uint32_t) * (size_t)n_docs) != cudaSuccess) { cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMalloc(&d_out, sizeof(SpiderCode4) * (size_t)n_docs) != cudaSuccess) { cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMemcpy(d_bytes, bytes_concat, cap_total, cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(d_out); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMemcpy(d_off, offsets_u32, sizeof(uint32_t) * (size_t)n_docs, cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(d_out); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMemcpy(d_len, lengths_u32, sizeof(uint32_t) * (size_t)n_docs, cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(d_out); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }

    const dim3 block(256, 1, 1);
    const dim3 grid(n_docs, 1, 1);
    ew_kernel_encode_spider4_batch<<<grid, block>>>(d_bytes, cap_total, d_off, d_len, n_docs, chunk_bytes, d_out);
    if (cudaDeviceSynchronize() != cudaSuccess) { cudaFree(d_out); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMemcpy(out_arr, d_out, sizeof(SpiderCode4) * (size_t)n_docs, cudaMemcpyDeviceToHost) != cudaSuccess) { cudaFree(d_out); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }

    cudaFree(d_out);
    cudaFree(d_len);
    cudaFree(d_off);
    cudaFree(d_bytes);
    return true;
}

extern "C" bool ew_cuda_encode_spider4_and_harmonics32_batch(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    SpiderCode4* out_arr,
    uint16_t* out_harmonics_q15,
    uint16_t* out_harmonics_mean_q15
) {
    if (!bytes_concat || !offsets_u32 || !lengths_u32 || !out_arr || !out_harmonics_q15 || !out_harmonics_mean_q15) return false;
    if (n_docs == 0) return true;

    const size_t cap_total = bytes_concat_len;

    uint8_t* d_bytes = nullptr;
    uint32_t* d_off = nullptr;
    uint32_t* d_len = nullptr;
    SpiderCode4* d_out = nullptr;
    uint16_t* d_h = nullptr;
    uint16_t* d_hm = nullptr;

    cudaError_t e;
    e = cudaMalloc((void**)&d_bytes, cap_total); if (e != cudaSuccess) return false;
    e = cudaMalloc((void**)&d_off, sizeof(uint32_t) * (size_t)n_docs); if (e != cudaSuccess) { cudaFree(d_bytes); return false; }
    e = cudaMalloc((void**)&d_len, sizeof(uint32_t) * (size_t)n_docs); if (e != cudaSuccess) { cudaFree(d_bytes); cudaFree(d_off); return false; }
    e = cudaMalloc((void**)&d_out, sizeof(SpiderCode4) * (size_t)n_docs); if (e != cudaSuccess) { cudaFree(d_bytes); cudaFree(d_off); cudaFree(d_len); return false; }
    e = cudaMalloc((void**)&d_h, sizeof(uint16_t) * (size_t)n_docs * 32u); if (e != cudaSuccess) { cudaFree(d_bytes); cudaFree(d_off); cudaFree(d_len); cudaFree(d_out); return false; }
    e = cudaMalloc((void**)&d_hm, sizeof(uint16_t) * (size_t)n_docs); if (e != cudaSuccess) { cudaFree(d_bytes); cudaFree(d_off); cudaFree(d_len); cudaFree(d_out); cudaFree(d_h); return false; }

    e = cudaMemcpy(d_bytes, bytes_concat, cap_total, cudaMemcpyHostToDevice);
    e = (e == cudaSuccess) ? cudaMemcpy(d_off, offsets_u32, sizeof(uint32_t) * (size_t)n_docs, cudaMemcpyHostToDevice) : e;
    e = (e == cudaSuccess) ? cudaMemcpy(d_len, lengths_u32, sizeof(uint32_t) * (size_t)n_docs, cudaMemcpyHostToDevice) : e;
    if (e != cudaSuccess) { cudaFree(d_bytes); cudaFree(d_off); cudaFree(d_len); cudaFree(d_out); cudaFree(d_h); cudaFree(d_hm); return false; }

    dim3 grid(n_docs);
    dim3 block(256);
    ew_kernel_encode_spider4_batch_h32<<<grid, block>>>(d_bytes, cap_total, d_off, d_len, n_docs, chunk_bytes, d_out, d_h, d_hm);
    e = cudaDeviceSynchronize();
    if (e != cudaSuccess) { cudaFree(d_bytes); cudaFree(d_off); cudaFree(d_len); cudaFree(d_out); cudaFree(d_h); cudaFree(d_hm); return false; }

    e = cudaMemcpy(out_arr, d_out, sizeof(SpiderCode4) * (size_t)n_docs, cudaMemcpyDeviceToHost);
    e = (e == cudaSuccess) ? cudaMemcpy(out_harmonics_q15, d_h, sizeof(uint16_t) * (size_t)n_docs * 32u, cudaMemcpyDeviceToHost) : e;
    e = (e == cudaSuccess) ? cudaMemcpy(out_harmonics_mean_q15, d_hm, sizeof(uint16_t) * (size_t)n_docs, cudaMemcpyDeviceToHost) : e;

    cudaFree(d_bytes);
    cudaFree(d_off);
    cudaFree(d_len);
    cudaFree(d_out);
    cudaFree(d_h);
    cudaFree(d_hm);
    return (e == cudaSuccess);
}

extern "C" bool ew_cuda_encode_spider4_page_and_chunks_batch(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    uint32_t max_chunks_per_doc,
    SpiderCode4* out_page_arr,
    SpiderCode4* out_chunk_arr,
    uint32_t* out_chunk_counts
) {
    if (!bytes_concat || bytes_concat_len == 0 || !offsets_u32 || !lengths_u32 || n_docs == 0) return false;
    if (!out_page_arr || !out_chunk_arr || !out_chunk_counts) return false;
    if (chunk_bytes == 0) chunk_bytes = 65536;
    if (max_chunks_per_doc == 0) max_chunks_per_doc = 1;
    if (max_chunks_per_doc > 64u) max_chunks_per_doc = 64u; // hard safety cap

    uint8_t* d_bytes = nullptr;
    uint32_t* d_off = nullptr;
    uint32_t* d_len = nullptr;
    SpiderCode4* d_chunks = nullptr;
    uint32_t* d_counts = nullptr;
    SpiderCode4* d_pages = nullptr;

    const size_t cap_total = bytes_concat_len;
    if (cudaMalloc(&d_bytes, cap_total) != cudaSuccess) return false;
    if (cudaMalloc(&d_off, sizeof(uint32_t) * (size_t)n_docs) != cudaSuccess) { cudaFree(d_bytes); return false; }
    if (cudaMalloc(&d_len, sizeof(uint32_t) * (size_t)n_docs) != cudaSuccess) { cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMalloc(&d_chunks, sizeof(SpiderCode4) * (size_t)n_docs * (size_t)max_chunks_per_doc) != cudaSuccess) { cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMalloc(&d_counts, sizeof(uint32_t) * (size_t)n_docs) != cudaSuccess) { cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMalloc(&d_pages, sizeof(SpiderCode4) * (size_t)n_docs) != cudaSuccess) { cudaFree(d_counts); cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }

    if (cudaMemcpy(d_bytes, bytes_concat, cap_total, cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(d_pages); cudaFree(d_counts); cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMemcpy(d_off, offsets_u32, sizeof(uint32_t) * (size_t)n_docs, cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(d_pages); cudaFree(d_counts); cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMemcpy(d_len, lengths_u32, sizeof(uint32_t) * (size_t)n_docs, cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(d_pages); cudaFree(d_counts); cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMemset(d_chunks, 0, sizeof(SpiderCode4) * (size_t)n_docs * (size_t)max_chunks_per_doc) != cudaSuccess) { cudaFree(d_pages); cudaFree(d_counts); cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMemset(d_counts, 0, sizeof(uint32_t) * (size_t)n_docs) != cudaSuccess) { cudaFree(d_pages); cudaFree(d_counts); cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }

    const dim3 block(256, 1, 1);
    const dim3 grid_chunks(n_docs * max_chunks_per_doc, 1, 1);
    ew_kernel_encode_spider4_batch_chunks<<<grid_chunks, block>>>(d_bytes, cap_total, d_off, d_len, n_docs, chunk_bytes, max_chunks_per_doc, d_chunks, d_counts);
    const dim3 grid_pages(n_docs, 1, 1);
    ew_kernel_reduce_spider4_docs_from_chunks<<<grid_pages, block>>>(d_chunks, d_counts, n_docs, max_chunks_per_doc, d_pages);

    if (cudaDeviceSynchronize() != cudaSuccess) {
        cudaFree(d_pages); cudaFree(d_counts); cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes);
        return false;
    }

    if (cudaMemcpy(out_page_arr, d_pages, sizeof(SpiderCode4) * (size_t)n_docs, cudaMemcpyDeviceToHost) != cudaSuccess) { cudaFree(d_pages); cudaFree(d_counts); cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMemcpy(out_chunk_arr, d_chunks, sizeof(SpiderCode4) * (size_t)n_docs * (size_t)max_chunks_per_doc, cudaMemcpyDeviceToHost) != cudaSuccess) { cudaFree(d_pages); cudaFree(d_counts); cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }
    if (cudaMemcpy(out_chunk_counts, d_counts, sizeof(uint32_t) * (size_t)n_docs, cudaMemcpyDeviceToHost) != cudaSuccess) { cudaFree(d_pages); cudaFree(d_counts); cudaFree(d_chunks); cudaFree(d_len); cudaFree(d_off); cudaFree(d_bytes); return false; }

    cudaFree(d_pages);
    cudaFree(d_counts);
    cudaFree(d_chunks);
    cudaFree(d_len);
    cudaFree(d_off);
    cudaFree(d_bytes);
    return true;
}

extern "C" bool ew_cuda_encode_spider4_page_chunks_and_harmonics32_batch(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    uint32_t max_chunks_per_doc,
    SpiderCode4* out_page_arr,
    SpiderCode4* out_chunk_arr,
    uint32_t* out_chunk_counts,
    uint16_t* out_page_harmonics_q15,
    uint16_t* out_chunk_harmonics_q15,
    uint16_t* out_page_harmonics_mean_q15,
    uint16_t* out_chunk_harmonics_mean_q15
) {
    // Production note: page-level harmonics are computed from bytes.
    // Chunk harmonics are optional; we emit zeros deterministically until
    // a dedicated per-chunk harmonic kernel is enabled.
    if (!out_page_harmonics_q15 || !out_chunk_harmonics_q15 || !out_page_harmonics_mean_q15 || !out_chunk_harmonics_mean_q15) return false;

    const bool ok = ew_cuda_encode_spider4_page_and_chunks_batch(
        bytes_concat, bytes_concat_len, offsets_u32, lengths_u32, n_docs, chunk_bytes,
        max_chunks_per_doc, out_page_arr, out_chunk_arr, out_chunk_counts);
    if (!ok) return false;

    // Compute page-level harmonics.
    // Note: this also overwrites out_page_arr with the same page-level SpiderCode4
    // values deterministically, which is acceptable.
    const bool ok2 = ew_cuda_encode_spider4_and_harmonics32_batch(
        bytes_concat, bytes_concat_len, offsets_u32, lengths_u32, n_docs, chunk_bytes, out_page_arr, out_page_harmonics_q15, out_page_harmonics_mean_q15);
    if (!ok2) return false;

    // Zero chunk harmonics deterministically.
    const size_t total = (size_t)n_docs * (size_t)max_chunks_per_doc * 32u;
    for (size_t i = 0; i < total; ++i) out_chunk_harmonics_q15[i] = 0;
    const size_t total_mean = (size_t)n_docs * (size_t)max_chunks_per_doc;
    for (size_t i = 0; i < total_mean; ++i) out_chunk_harmonics_mean_q15[i] = 0;
    return true;
}

extern "C" bool ew_cuda_encode_spider4(const uint8_t* bytes, size_t len, SpiderCode4* out) {
    if (!bytes || len == 0 || !out) return false;
    uint8_t* d_bytes = nullptr;
    SpiderCode4* d_out = nullptr;
    const size_t cap = (len > (size_t)(256 * 1024)) ? (size_t)(256 * 1024) : len;
    if (cudaMalloc(&d_bytes, cap) != cudaSuccess) return false;
    if (cudaMalloc(&d_out, sizeof(SpiderCode4)) != cudaSuccess) {
        cudaFree(d_bytes);
        return false;
    }
    if (cudaMemcpy(d_bytes, bytes, cap, cudaMemcpyHostToDevice) != cudaSuccess) {
        cudaFree(d_bytes);
        cudaFree(d_out);
        return false;
    }
    const dim3 block(256, 1, 1);
    const dim3 grid(1, 1, 1);
    ew_kernel_encode_spider4<<<grid, block>>>(d_bytes, cap, d_out);
    if (cudaDeviceSynchronize() != cudaSuccess) {
        cudaFree(d_bytes);
        cudaFree(d_out);
        return false;
    }
    if (cudaMemcpy(out, d_out, sizeof(SpiderCode4), cudaMemcpyDeviceToHost) != cudaSuccess) {
        cudaFree(d_bytes);
        cudaFree(d_out);
        return false;
    }
    cudaFree(d_bytes);
    cudaFree(d_out);
    return true;
}


// -----------------------------------------------------------------------------
// Chunked streaming encoder + deterministic reduction.
// -----------------------------------------------------------------------------

__global__ void ew_kernel_encode_spider4_chunks(const uint8_t* bytes, size_t len, size_t chunk_bytes, SpiderCode4* out_arr) {
    const uint32_t chunk = (uint32_t)blockIdx.x;
    const size_t start = (size_t)chunk * chunk_bytes;
    if (start >= len) return;
    size_t end = start + chunk_bytes;
    if (end > len) end = len;
    const size_t clen = end - start;

    __shared__ Accum sh2[256];
    const uint32_t tid = (uint32_t)threadIdx.x;
    Accum a{};
    a.sum = 0; a.sum2 = 0; a.xors = 0; a.count = 0;
    for (size_t i = tid; i < clen; i += (size_t)blockDim.x) {
        const uint8_t b = bytes[start + i];
        a.sum += (uint64_t)b;
        a.sum2 += (uint64_t)b * (uint64_t)b;
        a.xors ^= (uint64_t)(rotl32((uint32_t)b + 0x9E3779B9u, (int)((start + i) & 31u))) << ((start + i) & 7u);
        a.count += 1u;
    }
    sh2[tid] = a;
    __syncthreads();
    for (uint32_t step = 128; step > 0; step >>= 1) {
        if (tid < step) {
            sh2[tid].sum += sh2[tid + step].sum;
            sh2[tid].sum2 += sh2[tid + step].sum2;
            sh2[tid].xors ^= sh2[tid + step].xors;
            sh2[tid].count += sh2[tid + step].count;
        }
        __syncthreads();
    }
    if (tid == 0) {
        const Accum r = sh2[0];
        const uint64_t mean = (r.count > 0u) ? (r.sum / (uint64_t)r.count) : 0u;
        const int64_t centered = (int64_t)mean - 128;
        const int32_t f_code = (int32_t)(centered * 256 + (int32_t)(r.xors & 0xFFFFu));
        const uint64_t mean2 = (r.count > 0u) ? (r.sum2 / (uint64_t)r.count) : 0u;
        uint64_t var = 0;
        if (mean2 > mean * mean) var = mean2 - mean * mean;
        uint32_t a = (uint32_t)(var & 0xFFFFu);
        if (a == 0u) a = 1u;
        uint32_t v = 0;
        if (clen >= 65536) v = 65535;
        else v = (uint32_t)((clen * 65535ULL) / 65536ULL);
        if (v == 0u) v = 1u;
        uint32_t i = (uint32_t)((r.count & 0xFFFFu) ^ (uint32_t)((r.xors >> 16) & 0xFFFFu));
        if (i == 0u) i = 1u;
        SpiderCode4 sc;
        sc.f_code = f_code;
        sc.a_code = (uint16_t)a;
        sc.v_code = (uint16_t)v;
        sc.i_code = (uint16_t)i;
        out_arr[chunk] = sc;
    }
}

__global__ void ew_kernel_reduce_spider4(const SpiderCode4* arr, uint32_t n, SpiderCode4* out) {
    // Deterministic reduction: sum f, max a, sum v/i clamped.
    __shared__ int64_t sh_f[256];
    __shared__ uint32_t sh_a[256];
    __shared__ uint32_t sh_v[256];
    __shared__ uint32_t sh_i[256];
    const uint32_t tid = (uint32_t)threadIdx.x;
    int64_t f = 0;
    uint32_t a = 0, v = 0, i = 0;
    for (uint32_t idx = tid; idx < n; idx += (uint32_t)blockDim.x) {
        const SpiderCode4 sc = arr[idx];
        f += (int64_t)sc.f_code;
        if ((uint32_t)sc.a_code > a) a = (uint32_t)sc.a_code;
        v += (uint32_t)sc.v_code;
        i += (uint32_t)sc.i_code;
    }
    sh_f[tid] = f;
    sh_a[tid] = a;
    sh_v[tid] = v;
    sh_i[tid] = i;
    __syncthreads();
    for (uint32_t step = 128; step > 0; step >>= 1) {
        if (tid < step) {
            sh_f[tid] += sh_f[tid + step];
            sh_v[tid] += sh_v[tid + step];
            sh_i[tid] += sh_i[tid + step];
            if (sh_a[tid + step] > sh_a[tid]) sh_a[tid] = sh_a[tid + step];
        }
        __syncthreads();
    }
    if (tid == 0) {
        SpiderCode4 sc;
        sc.f_code = (int32_t)sh_f[0];
        sc.a_code = (uint16_t)(sh_a[0] ? sh_a[0] : 1u);
        uint32_t vv = sh_v[0]; if (vv > 65535u) vv = 65535u;
        uint32_t ii = sh_i[0]; if (ii > 65535u) ii = 65535u;
        sc.v_code = (uint16_t)(vv ? vv : 1u);
        sc.i_code = (uint16_t)(ii ? ii : 1u);
        *out = sc;
    }
}

extern "C" bool ew_cuda_encode_spider4_chunked(const uint8_t* bytes, size_t len, size_t chunk_bytes, SpiderCode4* out) {
    if (!bytes || len == 0 || !out) return false;
    if (chunk_bytes == 0) chunk_bytes = 65536;
    uint8_t* d_bytes = nullptr;
    SpiderCode4* d_arr = nullptr;
    SpiderCode4* d_out = nullptr;

    const size_t cap = (len > (size_t)(1024 * 1024)) ? (size_t)(1024 * 1024) : len; // 1MB safety cap per call
    const uint32_t n_chunks = (uint32_t)((cap + chunk_bytes - 1) / chunk_bytes);
    if (n_chunks == 0) return false;

    if (cudaMalloc(&d_bytes, cap) != cudaSuccess) return false;
    if (cudaMalloc(&d_arr, sizeof(SpiderCode4) * (size_t)n_chunks) != cudaSuccess) { cudaFree(d_bytes); return false; }
    if (cudaMalloc(&d_out, sizeof(SpiderCode4)) != cudaSuccess) { cudaFree(d_arr); cudaFree(d_bytes); return false; }
    if (cudaMemcpy(d_bytes, bytes, cap, cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(d_out); cudaFree(d_arr); cudaFree(d_bytes); return false; }

    ew_kernel_encode_spider4_chunks<<<n_chunks, 256>>>(d_bytes, cap, chunk_bytes, d_arr);
    ew_kernel_reduce_spider4<<<1, 256>>>(d_arr, n_chunks, d_out);

    if (cudaMemcpy(out, d_out, sizeof(SpiderCode4), cudaMemcpyDeviceToHost) != cudaSuccess) { cudaFree(d_out); cudaFree(d_arr); cudaFree(d_bytes); return false; }
    cudaFree(d_out);
    cudaFree(d_arr);
    cudaFree(d_bytes);
    return true;
}


// -----------------------------------------------------------------------------
// Page summary: SpiderCode4 + curriculum keyword scan (GPU-only)
// -----------------------------------------------------------------------------

__device__ __forceinline__ uint8_t to_lower_u8(uint8_t c) {
    if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') return (uint8_t)(c - 'A' + 'a');
    return c;
}

__device__ __forceinline__ bool match_lit(const uint8_t* s, size_t i, size_t n, const char* lit) {
    // Match lowercase literal against bytes (case-insensitive via to_lower).
    size_t j = 0;
    while (lit[j] != 0) {
        if (i + j >= n) return false;
        const uint8_t c = to_lower_u8(s[i + j]);
        if (c != (uint8_t)lit[j]) return false;
        j++;
    }
    return true;
}

__global__ void ew_kernel_scan_metric_keywords(const uint8_t* bytes, size_t len, uint64_t* out_mask, uint32_t* out_ascii_sum, uint32_t* out_nl) {
    __shared__ uint64_t sh_mask[256];
    __shared__ uint32_t sh_sum[256];
    __shared__ uint32_t sh_nl[256];
    const uint32_t tid = (uint32_t)threadIdx.x;
    uint64_t m = 0ULL;
    uint64_t sum = 0ULL;
    uint32_t nl = 0u;

    // MetricKind IDs (must match metric_registry.hpp).
    const uint32_t K_DS = 10u;
    const uint32_t K_PIB = 11u;
    const uint32_t K_HO = 12u;
    const uint32_t K_TUN = 13u;
    const uint32_t K_ORB_E = 20u;
    const uint32_t K_ORB_N = 21u;
    const uint32_t K_BOND_L = 22u;
    const uint32_t K_BOND_V = 23u;
    const uint32_t K_CHEM_RATE = 30u;
    const uint32_t K_CHEM_EQ = 31u;
    const uint32_t K_CHEM_DIFF = 32u;
    const uint32_t K_MAT_TH = 40u;
    const uint32_t K_MAT_EC = 41u;
    const uint32_t K_MAT_SS = 42u;
    const uint32_t K_MAT_PC = 43u;
    const uint32_t K_COS_ORB = 50u;
    const uint32_t K_COS_RAD = 51u;
    const uint32_t K_COS_ATM = 52u;
    const uint32_t K_BIO_OSM = 60u;

    auto bit = [] __device__ (uint32_t id) -> uint64_t { return 1ULL << (id & 63u); };

    for (size_t i = tid; i < len; i += (size_t)blockDim.x) {
        const uint8_t b = bytes[i];
        sum += (uint64_t)b;
        if (b == (uint8_t)'\n') nl++;

        // Bounded keyword scan: only check when current char matches first letter.
        const uint8_t c = to_lower_u8(b);
        if (c == (uint8_t)'d') {
            // "double slit"
            if (match_lit(bytes, i, len, "double slit")) m |= bit(K_DS);
        } else if (c == (uint8_t)'p') {
            // "particle in a box"
            if (match_lit(bytes, i, len, "particle in a box")) m |= bit(K_PIB);
        } else if (c == (uint8_t)'h') {
            // "harmonic oscillator"
            if (match_lit(bytes, i, len, "harmonic oscillator")) m |= bit(K_HO);
        } else if (c == (uint8_t)'t') {
            // "tunneling"
            if (match_lit(bytes, i, len, "tunneling")) m |= bit(K_TUN);
            // "thermal conductivity"
            if (match_lit(bytes, i, len, "thermal conductivity")) m |= bit(K_MAT_TH);
        } else if (c == (uint8_t)'o') {
            // "orbital energy" / "radial node"
            if (match_lit(bytes, i, len, "orbital energy")) m |= bit(K_ORB_E);
        } else if (c == (uint8_t)'r') {
            if (match_lit(bytes, i, len, "radial node")) m |= bit(K_ORB_N);
            if (match_lit(bytes, i, len, "reaction rate")) m |= bit(K_CHEM_RATE);
            if (match_lit(bytes, i, len, "radiation spectrum")) m |= bit(K_COS_RAD);
        } else if (c == (uint8_t)'b') {
            if (match_lit(bytes, i, len, "bond length")) m |= bit(K_BOND_L);
            if (match_lit(bytes, i, len, "bond vibration")) m |= bit(K_BOND_V);
        } else if (c == (uint8_t)'e') {
            if (match_lit(bytes, i, len, "equilibrium constant")) m |= bit(K_CHEM_EQ);
            if (match_lit(bytes, i, len, "electrical conductivity")) m |= bit(K_MAT_EC);
        } else if (c == (uint8_t)'s') {
            if (match_lit(bytes, i, len, "stress strain")) m |= bit(K_MAT_SS);
        } else if (c == (uint8_t)'c') {
            if (match_lit(bytes, i, len, "phase change")) m |= bit(K_MAT_PC);
            if (match_lit(bytes, i, len, "diffusion coefficient")) m |= bit(K_CHEM_DIFF);
            if (match_lit(bytes, i, len, "orbit period")) m |= bit(K_COS_ORB);
            if (match_lit(bytes, i, len, "pressure profile")) m |= bit(K_COS_ATM);
            if (match_lit(bytes, i, len, "osmosis")) m |= bit(K_BIO_OSM);
        }
    }

    sh_mask[tid] = m;
    sh_sum[tid] = (uint32_t)(sum & 0xFFFFFFFFu);
    sh_nl[tid] = nl;
    __syncthreads();

    for (uint32_t step = 128; step > 0; step >>= 1) {
        if (tid < step) {
            sh_mask[tid] |= sh_mask[tid + step];
            sh_sum[tid] += sh_sum[tid + step];
            sh_nl[tid] += sh_nl[tid + step];
        }
        __syncthreads();
    }

    if (tid == 0) {
        *out_mask = sh_mask[0];
        *out_ascii_sum = sh_sum[0];
        *out_nl = sh_nl[0];
    }
}

extern "C" bool ew_cuda_encode_page_summary(const uint8_t* bytes, size_t len, size_t chunk_bytes, EwCrawlerPageSummary* out_summary) {
    if (!bytes || len == 0 || !out_summary) return false;
    uint8_t* d_bytes = nullptr;
    SpiderCode4* d_page = nullptr;
    uint64_t* d_mask = nullptr;
    uint32_t* d_sum = nullptr;
    uint32_t* d_nl = nullptr;

    const size_t cap = (len > (size_t)(512 * 1024)) ? (size_t)(512 * 1024) : len;
    if (cudaMalloc(&d_bytes, cap) != cudaSuccess) return false;
    if (cudaMemcpy(d_bytes, bytes, cap, cudaMemcpyHostToDevice) != cudaSuccess) { cudaFree(d_bytes); return false; }

    if (cudaMalloc(&d_page, sizeof(SpiderCode4)) != cudaSuccess) { cudaFree(d_bytes); return false; }
    if (cudaMalloc(&d_mask, sizeof(uint64_t)) != cudaSuccess) { cudaFree(d_bytes); cudaFree(d_page); return false; }
    if (cudaMalloc(&d_sum, sizeof(uint32_t)) != cudaSuccess) { cudaFree(d_bytes); cudaFree(d_page); cudaFree(d_mask); return false; }
    if (cudaMalloc(&d_nl, sizeof(uint32_t)) != cudaSuccess) { cudaFree(d_bytes); cudaFree(d_page); cudaFree(d_mask); cudaFree(d_sum); return false; }

    // Chunked spider encoding and deterministic reduction.
    // Reuse existing chunked path: allocate per-chunk array and reduce.
    const size_t cb = (chunk_bytes == 0) ? (size_t)65536 : chunk_bytes;
    const uint32_t n_chunks = (uint32_t)((cap + cb - 1) / cb);
    SpiderCode4* d_arr = nullptr;
    if (cudaMalloc(&d_arr, (size_t)n_chunks * sizeof(SpiderCode4)) != cudaSuccess) {
        cudaFree(d_bytes); cudaFree(d_page); cudaFree(d_mask); cudaFree(d_sum); cudaFree(d_nl);
        return false;
    }
    const dim3 block(256,1,1);
    const dim3 grid(n_chunks,1,1);
    ew_kernel_encode_spider4_chunks<<<grid, block>>>(d_bytes, cap, cb, d_arr);
    ew_kernel_reduce_spider4<<<dim3(1,1,1), block>>>(d_arr, n_chunks, d_page);

    // Keyword scan + counts.
    ew_kernel_scan_metric_keywords<<<dim3(1,1,1), block>>>(d_bytes, cap, d_mask, d_sum, d_nl);

    if (cudaDeviceSynchronize() != cudaSuccess) {
        cudaFree(d_bytes); cudaFree(d_page); cudaFree(d_mask); cudaFree(d_sum); cudaFree(d_nl); cudaFree(d_arr);
        return false;
    }

    EwCrawlerPageSummary h{};
    if (cudaMemcpy(&h.page_sc, d_page, sizeof(SpiderCode4), cudaMemcpyDeviceToHost) != cudaSuccess) {
        cudaFree(d_bytes); cudaFree(d_page); cudaFree(d_mask); cudaFree(d_sum); cudaFree(d_nl); cudaFree(d_arr);
        return false;
    }
    if (cudaMemcpy(&h.metric_mask_u64, d_mask, sizeof(uint64_t), cudaMemcpyDeviceToHost) != cudaSuccess) {
        cudaFree(d_bytes); cudaFree(d_page); cudaFree(d_mask); cudaFree(d_sum); cudaFree(d_nl); cudaFree(d_arr);
        return false;
    }
    if (cudaMemcpy(&h.ascii_sum_u32, d_sum, sizeof(uint32_t), cudaMemcpyDeviceToHost) != cudaSuccess) {
        cudaFree(d_bytes); cudaFree(d_page); cudaFree(d_mask); cudaFree(d_sum); cudaFree(d_nl); cudaFree(d_arr);
        return false;
    }
    if (cudaMemcpy(&h.newline_count_u32, d_nl, sizeof(uint32_t), cudaMemcpyDeviceToHost) != cudaSuccess) {
        cudaFree(d_bytes); cudaFree(d_page); cudaFree(d_mask); cudaFree(d_sum); cudaFree(d_nl); cudaFree(d_arr);
        return false;
    }
    h.len_u32 = (uint32_t)cap;
    *out_summary = h;

    cudaFree(d_bytes); cudaFree(d_page); cudaFree(d_mask); cudaFree(d_sum); cudaFree(d_nl); cudaFree(d_arr);
    return true;
}
