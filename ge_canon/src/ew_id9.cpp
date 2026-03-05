#include "ew_id9.hpp"

#include <vector>

namespace EigenWare {

static inline uint32_t ew_clamp_u64_to_u32(uint64_t v) {
    if (v > 0xFFFFFFFFull) return 0xFFFFFFFFu;
    return (uint32_t)v;
}

// Normative coord_sig9 (spec bundle):
// s0..s3 copied from namespace_sig9
// s4 = n
// s5 = sum(b[i])
// s6 = sum(i*b[i])
// s7 = sum((i*i)*b[i])
// s8 = edge = b[0]+b[n//2]+b[n-1] (with boundary rules)
// All non-namespace components are deterministically clamped into 32-bit lanes.
EwId9 coord_sig9(const EwId9& namespace_sig9, const uint8_t* bytes, size_t n) {
    EwId9 out;
    out.u32[0] = namespace_sig9.u32[0];
    out.u32[1] = namespace_sig9.u32[1];
    out.u32[2] = namespace_sig9.u32[2];
    out.u32[3] = namespace_sig9.u32[3];

    // Moments
    uint64_t sum0 = 0ull;
    uint64_t mom1 = 0ull;
    uint64_t mom2 = 0ull;
    for (size_t i = 0; i < n; ++i) {
        const uint64_t b = (uint64_t)bytes[i];
        sum0 += b;
        mom1 += (uint64_t)i * b;
        mom2 += (uint64_t)i * (uint64_t)i * b;
    }

    uint64_t edge = 0ull;
    if (n == 0) {
        edge = 0ull;
    } else if (n == 1) {
        edge = (uint64_t)bytes[0];
    } else if (n == 2) {
        edge = (uint64_t)bytes[0] + (uint64_t)bytes[1];
    } else {
        edge = (uint64_t)bytes[0] + (uint64_t)bytes[n / 2] + (uint64_t)bytes[n - 1];
    }

    out.u32[4] = ew_clamp_u64_to_u32((uint64_t)n);
    out.u32[5] = ew_clamp_u64_to_u32(sum0);
    out.u32[6] = ew_clamp_u64_to_u32(mom1);
    out.u32[7] = ew_clamp_u64_to_u32(mom2);
    out.u32[8] = ew_clamp_u64_to_u32(edge);
    return out;
}

// Fixed namespace signatures.
// These constants are not derived by hashing; they are explicitly chosen stable lane values.
EwId9 ew_namespace_sig9_ascii() {
    EwId9 ns;
    ns.u32 = {0x41534349u, 0x495F4E53u, 0x00000009u, 0x00000001u, 0u, 0u, 0u, 0u, 0u};
    return ns;
}

EwId9 ew_namespace_sig9_anchorpack() {
    EwId9 ns;
    ns.u32 = {0x414E4348u, 0x4F525F50u, 0x41434B00u, 0x00000001u, 0u, 0u, 0u, 0u, 0u};
    return ns;
}

EwId9 ew_id9_from_ascii_norm(const char* s, size_t n) {
    // Normalize: ASCII fold to lowercase and keep bytes in [0x20..0x7E].
    // These normalized bytes are the "artifact_bytes" for this convenience identifier.
    std::vector<uint8_t> norm;
    norm.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t c0 = (uint8_t)s[i];
        const uint8_t c = ew_ascii_fold_keep(c0);
        if (c == 0u) continue;
        norm.push_back(c);
    }
    const EwId9 ns = ew_namespace_sig9_ascii();
    return coord_sig9(ns, norm.data(), norm.size());
}

} // namespace EigenWare
