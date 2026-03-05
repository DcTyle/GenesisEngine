#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace ew {

static inline void ew_write_u32_le(uint8_t* dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xFFu);
    dst[1] = (uint8_t)((v >> 8) & 0xFFu);
    dst[2] = (uint8_t)((v >> 16) & 0xFFu);
    dst[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline void ew_write_u64_le(uint8_t* dst, uint64_t v) {
    for (int i = 0; i < 8; ++i) dst[i] = (uint8_t)((v >> (8 * i)) & 0xFFull);
}

static inline void ew_write_i64_le(uint8_t* dst, int64_t v) {
    ew_write_u64_le(dst, (uint64_t)v);
}

static inline void ew_write_f64_le(uint8_t* dst, double x) {
    uint64_t u = 0;
    static_assert(sizeof(double) == 8, "double must be 8 bytes");
    std::memcpy(&u, &x, 8);
    ew_write_u64_le(dst, u);
}

} // namespace ew
