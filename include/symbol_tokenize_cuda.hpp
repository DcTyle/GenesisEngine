#pragma once
#include <cstdint>
#include <cstddef>

// Deterministic GPU symbol tokenizer (identifier tokens).
// Outputs fixed-width 9D lane codes (not cryptographic hashes).
// Each token is encoded from up to 36 bytes of the identifier (9 lanes * 4 bytes).
struct EwSymbolToken9 {
    uint32_t lanes_u32[9];
    uint32_t len_u32;
    uint32_t artifact_id_u32;
    uint32_t reserved_u32;
};

#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
bool ew_cuda_tokenize_symbols_batch(
    const uint8_t* bytes_concat,
    const uint32_t* offsets_u32,
    const uint32_t* lens_u32,
    const uint32_t* artifact_ids_u32,
    uint32_t count_u32,
    EwSymbolToken9* out_tokens,
    uint32_t* out_counts_u32_per_artifact,
    uint32_t max_tokens_per_artifact_u32
);
#else
// CUDA disabled builds never call this; the build remains deterministic via CPU path.
inline bool ew_cuda_tokenize_symbols_batch(
    const uint8_t*, const uint32_t*, const uint32_t*, const uint32_t*, uint32_t,
    EwSymbolToken9*, uint32_t*, uint32_t) { return false; }
#endif
