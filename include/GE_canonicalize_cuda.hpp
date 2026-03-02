#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "GE_corpus_canonicalizer.hpp"

// GPU canonicalizer: strict UTF-8 validation + newline normalization + whitespace collapse.
// - Validates UTF-8 deterministically (rejects invalid).
// - Normalizes CRLF/CR -> LF.
// - Collapses runs of ASCII whitespace (space, tab, LF) into single spaces, preserving paragraph
//   breaks as single '\n' when there are 2+ consecutive newlines after canonicalization.
// NOTE: IO still originates on CPU/RAM; compute is GPU-side.

bool GE_canonicalize_utf8_strict_cuda(const uint8_t* bytes_host, size_t len,
                                     std::string& out_canon_utf8,
                                     GE_CorpusCanonicalizeStats& stats);
