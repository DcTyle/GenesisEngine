#include <cuda_runtime.h>
#include <stdint.h>

struct GE_CanonDeviceOut {
    uint32_t out_len_u32;
    uint32_t invalid_utf8_u32;
    uint32_t paragraph_breaks_u32;
    uint32_t reserved_u32;
};

static __device__ __forceinline__ bool ge_is_ascii_ws(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static __device__ __forceinline__ bool ge_utf8_next(const uint8_t* s, uint32_t n, uint32_t* io_i) {
    uint32_t i = *io_i;
    if (i >= n) return true;
    uint8_t c = s[i];
    if (c <= 0x7F) { *io_i = i + 1; return true; }

    // 2-byte
    if ((c & 0xE0u) == 0xC0u) {
        if (i + 1 >= n) return false;
        uint8_t c1 = s[i + 1];
        if ((c1 & 0xC0u) != 0x80u) return false;
        uint32_t cp = ((uint32_t(c & 0x1Fu) << 6) | (uint32_t(c1 & 0x3Fu)));
        if (cp < 0x80u) return false; // overlong
        *io_i = i + 2;
        return true;
    }
    // 3-byte
    if ((c & 0xF0u) == 0xE0u) {
        if (i + 2 >= n) return false;
        uint8_t c1 = s[i + 1], c2 = s[i + 2];
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u) return false;
        uint32_t cp = ((uint32_t(c & 0x0Fu) << 12) | (uint32_t(c1 & 0x3Fu) << 6) | (uint32_t(c2 & 0x3Fu)));
        if (cp < 0x800u) return false; // overlong
        if (cp >= 0xD800u && cp <= 0xDFFFu) return false; // surrogate
        *io_i = i + 3;
        return true;
    }
    // 4-byte
    if ((c & 0xF8u) == 0xF0u) {
        if (i + 3 >= n) return false;
        uint8_t c1 = s[i + 1], c2 = s[i + 2], c3 = s[i + 3];
        if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u) return false;
        uint32_t cp = ((uint32_t(c & 0x07u) << 18) | (uint32_t(c1 & 0x3Fu) << 12) |
                       (uint32_t(c2 & 0x3Fu) << 6) | (uint32_t(c3 & 0x3Fu)));
        if (cp < 0x10000u) return false; // overlong
        if (cp > 0x10FFFFu) return false;
        *io_i = i + 4;
        return true;
    }

    return false;
}

extern "C" __global__ void ge_canonicalize_kernel(const uint8_t* in_bytes, uint32_t in_len,
                                                  uint8_t* out_bytes, uint32_t out_cap,
                                                  GE_CanonDeviceOut* out_meta) {
    // Single-thread deterministic canonicalization per block.
    // This is intentionally scalar to guarantee stable behavior; throughput comes from many chunks.
    if (blockIdx.x != 0 || threadIdx.x != 0) return;

    uint32_t i = 0;
    uint32_t o = 0;
    uint32_t invalid = 0;

    // Phase 1: strict UTF-8 validation pass
    while (i < in_len) {
        uint32_t j = i;
        if (!ge_utf8_next(in_bytes, in_len, &j)) { invalid = 1; break; }
        i = j;
    }
    if (invalid) {
        out_meta->out_len_u32 = 0;
        out_meta->invalid_utf8_u32 = 1;
        out_meta->paragraph_breaks_u32 = 0;
        out_meta->reserved_u32 = 0;
        return;
    }

    // Phase 2: newline normalize + whitespace collapse.
    // Rule: CRLF or CR -> LF. Collapse runs of spaces/tabs into single space.
    // Preserve paragraph breaks: two or more consecutive LF become exactly two LFs.
    i = 0;
    uint32_t pending_space = 0;
    uint32_t pending_lf = 0;
    uint32_t paragraph_breaks = 0;

    while (i < in_len && o < out_cap) {
        uint8_t c = in_bytes[i++];

        // Normalize CRLF/CR -> LF
        if (c == '\r') {
            if (i < in_len && in_bytes[i] == '\n') { i++; }
            c = '\n';
        }

        if (c == '\n') {
            // flush pending space before newline? no; newline breaks tokens
            pending_space = 0;
            pending_lf++;
            if (pending_lf == 1) {
                out_bytes[o++] = '\n';
            } else if (pending_lf == 2) {
                out_bytes[o++] = '\n';
                // Transition into a paragraph break (>=2 consecutive LFs).
                paragraph_breaks++;
            } else {
                // >2 newlines collapse to 2, do nothing
            }
            continue;
        }

        // For ASCII whitespace other than newline
        if (c == ' ' || c == '\t') {
            pending_space = 1;
            continue;
        }

        // Non-ws: reset paragraph counter
        pending_lf = 0;

        if (pending_space) {
            if (o < out_cap) out_bytes[o++] = ' ';
            pending_space = 0;
        }

        // Copy byte as-is (UTF-8 validated already); for non-ASCII multi-byte, bytes copy through.
        out_bytes[o++] = c;
    }

    out_meta->out_len_u32 = o;
    out_meta->invalid_utf8_u32 = 0;
    out_meta->paragraph_breaks_u32 = paragraph_breaks;
    out_meta->reserved_u32 = 0;
}
