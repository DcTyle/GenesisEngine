#pragma once

#include <cstddef>
#include <cstdint>

// Bytes -> frequency coefficient (f_code) using the same spider-mapped
// vector encoding and single-carrier collapse rule used by the UTF-8 path.
//
// This operator is part of the simulated substrate microprocessor domain.
// It is deterministic and contains no external integration behavior.

// profile_id selects the delta profile used by the spider compressor.
// Returns an f_code in the same scale as ew_text_utf8_to_frequency_code.
int32_t ew_bytes_to_frequency_code(const uint8_t* bytes, size_t len, uint8_t profile_id);
