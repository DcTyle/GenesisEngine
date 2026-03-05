#include "GE_corpus_canonicalizer.hpp"
#include "GE_canonicalize_cuda.hpp"

// Corpus canonicalization is substrate-native: strict UTF-8 validation and canonicalization
// are performed on the GPU. CPU is used only for IO and to receive the canonical bytes.

bool GE_canonicalize_utf8_strict(const uint8_t* bytes, size_t len,
                                std::string& out_canon_utf8,
                                GE_CorpusCanonicalizeStats& stats) {
#if defined(EW_ENABLE_CUDA) && EW_ENABLE_CUDA
    return GE_canonicalize_utf8_strict_cuda(bytes, len, out_canon_utf8, stats);
#else
    (void)bytes; (void)len; (void)out_canon_utf8; (void)stats;
    return false;
#endif
}
