#include "GE_corpus_canonicalizer.hpp"
#include "ew_gpu_compute.hpp"

// Corpus canonicalization is substrate-native: strict UTF-8 validation and canonicalization
// are performed on the GPU. CPU is used only for IO and to receive the canonical bytes.

bool GE_canonicalize_utf8_strict(const uint8_t* bytes, size_t len,
                                std::string& out_canon_utf8,
                                GE_CorpusCanonicalizeStats& stats) {
#if defined(EW_ENABLE_GPU_COMPUTE) && EW_ENABLE_GPU_COMPUTE
    bool invalid_utf8 = false;
    uint32_t paragraph_breaks = 0u;
    stats.bytes_in_u64 += uint64_t(len);
    const bool ok = ew_gpu_canonicalize_utf8_strict(bytes, len, out_canon_utf8, &invalid_utf8, &paragraph_breaks);
    if (!ok) {
        if (invalid_utf8) stats.invalid_utf8_rejects_u64 += 1u;
        return false;
    }
    (void)paragraph_breaks;
    stats.bytes_out_u64 += uint64_t(out_canon_utf8.size());
    return true;
#else
    (void)bytes; (void)len; (void)out_canon_utf8; (void)stats;
    return false;
#endif
}
