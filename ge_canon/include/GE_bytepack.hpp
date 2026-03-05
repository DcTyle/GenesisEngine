#pragma once

#include <cstdint>

// Deterministic little-endian byte pack helpers.
// Kept header-only so both runtime and tools can share without linking.

static inline void ge_wr_u32_le(uint8_t* b, uint32_t off, uint32_t v) {
    b[off + 0] = (uint8_t)(v & 0xFFu);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFFu);
    b[off + 2] = (uint8_t)((v >> 16) & 0xFFu);
    b[off + 3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline void ge_wr_i32_le(uint8_t* b, uint32_t off, int32_t v) {
    ge_wr_u32_le(b, off, (uint32_t)v);
}

static inline void ge_wr_u64_le(uint8_t* b, uint32_t off, uint64_t v) {
    for (int i = 0; i < 8; ++i) b[off + (uint32_t)i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
}
