#pragma once

#include <cstddef>
#include <cstdint>

#include "anchor.hpp"

// GPU-derived page summary used to generate metric tasks and domain topic masks.
struct EwCrawlerPageSummary {
    SpiderCode4 page_sc;
    uint64_t metric_mask_u64 = 0ULL; // bits correspond to MetricKind id&63
    uint32_t len_u32 = 0u;
    uint32_t ascii_sum_u32 = 0u;
    uint32_t newline_count_u32 = 0u;
};

// Encode bytes into SpiderCode4 and scan for curriculum metric keywords on GPU.
bool ew_encode_page_summary_cuda(const uint8_t* bytes, size_t len, size_t chunk_bytes, EwCrawlerPageSummary* out_summary);

// CUDA-only helper for crawler ingestion: encode raw bytes into a SpiderCode4
// carrier suitable for pulse injection.
//
// Canonical constraints:
// - GPU computes the encoding; CPU only orchestrates and may copy back the
//   final 4-tuple for scheduling/injection.
// - Deterministic for identical input bytes.

// Returns true on success.
bool ew_encode_spidercode4_from_bytes_cuda(const uint8_t* bytes, size_t len, SpiderCode4* out);

// Chunked streaming variant: encodes input bytes in fixed-size chunks on GPU and
// deterministically reduces per-chunk SpiderCode4 into a single page-level carrier.
// chunk_bytes defaults to 65536.
bool ew_encode_spidercode4_from_bytes_chunked_cuda(const uint8_t* bytes, size_t len, size_t chunk_bytes, SpiderCode4* out);

// Batch variant for parallel crawler ingestion.
// Encodes N documents in parallel on GPU. Each document is provided as a slice
// into a single contiguous host buffer "bytes_concat".
//
// Inputs:
// - bytes_concat: concatenated bytes of all documents
// - offsets_u32[i]: byte offset into bytes_concat
// - lengths_u32[i]: byte length of document i
//
// Output:
// - out_arr[i]: page-level SpiderCode4 for document i
//
// Canonical constraints:
// - GPU-only encoding (CPU orchestrates and batches)
// - Deterministic for identical input bytes/offsets/lengths
// - Internally uses a lightweight Fourier-like carrier projection to derive
//   phase and amplitude observables from byte streams.
bool ew_encode_spidercode4_from_bytes_batch_cuda(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    SpiderCode4* out_arr
);

// Batch variant that also emits a fixed 32-bin harmonic magnitude profile
// (Q0.15) per document. These harmonics are intended to be written into
// substrate anchors for harmonic processing.
//
// Output layout:
// - out_harmonics_q15: array of size (n_docs * 32)
//   where out_harmonics_q15[i*32 + k] is bin k of doc i.
bool ew_encode_spidercode4_and_harmonics32_from_bytes_batch_cuda(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    SpiderCode4* out_arr,
    uint16_t* out_harmonics_q15,
    uint16_t* out_harmonics_mean_q15
);

// Batch variant that also emits per-chunk SpiderCode4 carriers for each
// document (chunk-stream mode).
//
// Outputs:
// - out_page_arr[i]: page-level SpiderCode4 for document i
// - out_chunk_arr: flattened array of size (n_docs * max_chunks_per_doc)
//   containing per-chunk SpiderCode4 for each document. Only the first
//   out_chunk_counts[i] entries per document are valid.
// - out_chunk_counts[i]: number of chunks produced for document i
//
// Canonical constraints:
// - GPU-only encoding (CPU orchestrates and batches)
// - Deterministic for identical input bytes/offsets/lengths
// - Chunk stream is bounded by max_chunks_per_doc to keep budgets stable.
bool ew_encode_spidercode4_page_and_chunks_batch_cuda(
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
);

// Chunk-stream batch variant that also emits harmonics32 profiles for the
// page-level carrier and each produced chunk carrier.
//
// Output layout:
// - out_page_harmonics_q15: (n_docs * 32)
// - out_chunk_harmonics_q15: (n_docs * max_chunks_per_doc * 32)
bool ew_encode_spidercode4_page_chunks_and_harmonics32_batch_cuda(
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
);
