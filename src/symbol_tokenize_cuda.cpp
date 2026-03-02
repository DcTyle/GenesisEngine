#include "symbol_tokenize_cuda.hpp"

#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
extern "C" bool ew_cuda_tokenize_symbols_batch_impl(
    const uint8_t* bytes_concat,
    const uint32_t* offsets_u32,
    const uint32_t* lens_u32,
    const uint32_t* artifact_ids_u32,
    uint32_t count_u32,
    EwSymbolToken9* out_tokens,
    uint32_t* out_counts_u32_per_artifact,
    uint32_t max_tokens_per_artifact_u32
);

bool ew_cuda_tokenize_symbols_batch(
    const uint8_t* bytes_concat,
    const uint32_t* offsets_u32,
    const uint32_t* lens_u32,
    const uint32_t* artifact_ids_u32,
    uint32_t count_u32,
    EwSymbolToken9* out_tokens,
    uint32_t* out_counts_u32_per_artifact,
    uint32_t max_tokens_per_artifact_u32
) {
    return ew_cuda_tokenize_symbols_batch_impl(
        bytes_concat, offsets_u32, lens_u32, artifact_ids_u32, count_u32,
        out_tokens, out_counts_u32_per_artifact, max_tokens_per_artifact_u32
    );
}
#endif
