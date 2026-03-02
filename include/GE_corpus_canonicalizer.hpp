#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct GE_CorpusCanonicalizeStats {
    uint64_t bytes_in_u64 = 0;
    uint64_t bytes_out_u64 = 0;
    uint64_t invalid_utf8_rejects_u64 = 0;
};

bool GE_canonicalize_utf8_strict(const uint8_t* bytes, size_t len,
                                std::string& out_canon_utf8,
                                GE_CorpusCanonicalizeStats& io_stats);
