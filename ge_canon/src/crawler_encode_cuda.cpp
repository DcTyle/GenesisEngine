#include "crawler_encode_cuda.hpp"

#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
extern "C" bool ew_cuda_encode_spider4(const uint8_t* bytes, size_t len, SpiderCode4* out);
extern "C" bool ew_cuda_encode_spider4_chunked(const uint8_t* bytes, size_t len, size_t chunk_bytes, SpiderCode4* out);
extern "C" bool ew_cuda_encode_spider4_batch(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    SpiderCode4* out_arr
);
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
);
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
);
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
);
extern "C" bool ew_cuda_encode_page_summary(const uint8_t* bytes, size_t len, size_t chunk_bytes, EwCrawlerPageSummary* out_summary);
#endif

bool ew_encode_spidercode4_from_bytes_cuda(const uint8_t* bytes, size_t len, SpiderCode4* out) {
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
    return ew_cuda_encode_spider4(bytes, len, out);
#else
    (void)bytes; (void)len; (void)out;
    return false;
#endif
}

bool ew_encode_spidercode4_from_bytes_chunked_cuda(const uint8_t* bytes, size_t len, size_t chunk_bytes, SpiderCode4* out) {
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
    return ew_cuda_encode_spider4_chunked(bytes, len, chunk_bytes, out);
#else
    (void)bytes; (void)len; (void)chunk_bytes; (void)out;
    return false;
#endif
}


bool ew_encode_page_summary_cuda(const uint8_t* bytes, size_t len, size_t chunk_bytes, EwCrawlerPageSummary* out_summary) {
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
    return ew_cuda_encode_page_summary(bytes, len, chunk_bytes, out_summary);
#else
    (void)bytes; (void)len; (void)chunk_bytes; (void)out_summary;
    return false;
#endif
}

bool ew_encode_spidercode4_from_bytes_batch_cuda(
    const uint8_t* bytes_concat,
    size_t bytes_concat_len,
    const uint32_t* offsets_u32,
    const uint32_t* lengths_u32,
    uint32_t n_docs,
    size_t chunk_bytes,
    SpiderCode4* out_arr
) {
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
    return ew_cuda_encode_spider4_batch(bytes_concat, bytes_concat_len, offsets_u32, lengths_u32, n_docs, chunk_bytes, out_arr);
#else
    (void)bytes_concat; (void)bytes_concat_len; (void)offsets_u32; (void)lengths_u32; (void)n_docs; (void)chunk_bytes; (void)out_arr;
    return false;
#endif
}

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
) {
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
    return ew_cuda_encode_spider4_and_harmonics32_batch(
        bytes_concat, bytes_concat_len, offsets_u32, lengths_u32, n_docs, chunk_bytes, out_arr, out_harmonics_q15, out_harmonics_mean_q15);
#else
    (void)bytes_concat; (void)bytes_concat_len; (void)offsets_u32; (void)lengths_u32; (void)n_docs; (void)chunk_bytes; (void)out_arr; (void)out_harmonics_q15; (void)out_harmonics_mean_q15;
    return false;
#endif
}

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
) {
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
    return ew_cuda_encode_spider4_page_and_chunks_batch(
        bytes_concat,
        bytes_concat_len,
        offsets_u32,
        lengths_u32,
        n_docs,
        chunk_bytes,
        max_chunks_per_doc,
        out_page_arr,
        out_chunk_arr,
        out_chunk_counts
    );
#else
    (void)bytes_concat; (void)bytes_concat_len; (void)offsets_u32; (void)lengths_u32;
    (void)n_docs; (void)chunk_bytes; (void)max_chunks_per_doc;
    (void)out_page_arr; (void)out_chunk_arr; (void)out_chunk_counts;
    return false;
#endif
}

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
) {
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
    return ew_cuda_encode_spider4_page_chunks_and_harmonics32_batch(
        bytes_concat,
        bytes_concat_len,
        offsets_u32,
        lengths_u32,
        n_docs,
        chunk_bytes,
        max_chunks_per_doc,
        out_page_arr,
        out_chunk_arr,
        out_chunk_counts,
        out_page_harmonics_q15,
        out_chunk_harmonics_q15,
        out_page_harmonics_mean_q15,
        out_chunk_harmonics_mean_q15
    );
#else
    (void)bytes_concat; (void)bytes_concat_len; (void)offsets_u32; (void)lengths_u32;
    (void)n_docs; (void)chunk_bytes; (void)max_chunks_per_doc;
    (void)out_page_arr; (void)out_chunk_arr; (void)out_chunk_counts;
    (void)out_page_harmonics_q15; (void)out_chunk_harmonics_q15; (void)out_page_harmonics_mean_q15; (void)out_chunk_harmonics_mean_q15;
    return false;
#endif
}
