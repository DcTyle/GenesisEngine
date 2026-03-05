#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>

// EwId9
// -----
// A deterministic 9D coordinate identifier used throughout EigenWare/Genesis.
//
// Constraints:
// - No hashing / cryptography logic.
// - IDs are 9D vectors (9 lanes).
// - Construction is deterministic and portable.
//
// Encoding (coord_sig9, spec-bundle compliant):
// - No hashing / crypto.
// - Identity is a deterministic 9D coordinate signature computed from normalized bytes.
// - For artifacts, the normative form is coord_sig9(namespace_sig9, artifact_bytes).
// - For convenience identifiers (rule names, CLI strings), we still use coord_sig9, but
//   with an internal fixed namespace signature.
//
// NOTE: EwId9 lanes are 32-bit in this repository. The spec language describes Q32.32
// scalars; here we store the deterministic clamped integer moments in 32-bit lanes.
// This preserves the "no hashing" and replay-safety requirements while remaining
// compatible with the existing codebase.

namespace EigenWare {

struct EwId9 {
    std::array<uint32_t, 9> u32 = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

    bool operator==(const EwId9& o) const noexcept { return u32 == o.u32; }
    bool operator!=(const EwId9& o) const noexcept { return !(*this == o); }
    bool operator<(const EwId9& o) const noexcept { return u32 < o.u32; }
};

inline uint8_t ew_ascii_fold_keep(uint8_t c) {
    if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
    if (c < 0x20u || c > 0x7Eu) return 0u;
    return c;
}

// Spec-bundle normative identity: coord_sig9(namespace_sig9, artifact_bytes)
EwId9 coord_sig9(const EwId9& namespace_sig9, const uint8_t* bytes, size_t n);

// Convenience namespace signatures (fixed constants; no hashing / crypto).
// These are only used for internal identifiers that are not sourced from external artifacts.
// External artifacts MUST use domain/publisher namespace_sig9 configured in the domain map.
EwId9 ew_namespace_sig9_ascii();
EwId9 ew_namespace_sig9_anchorpack();

// Convenience: coord_sig9 over normalized ASCII bytes.
EwId9 ew_id9_from_ascii_norm(const char* s, size_t n);

inline EwId9 ew_id9_from_ascii(const char* s, size_t n) {
    return ew_id9_from_ascii_norm(s, n);
}

inline EwId9 ew_id9_from_string_ascii(const std::string& s) {
    return ew_id9_from_ascii(s.c_str(), s.size());
}


inline EwId9 ew_id9_from_u64(uint64_t v) {
    EwId9 id;
    id.u32[0] = (uint32_t)(v & 0xFFFFFFFFull);
    id.u32[1] = (uint32_t)((v >> 32) & 0xFFFFFFFFull);
    id.u32[8] = 2u;
    return id;
}

} // namespace EigenWare


// Global aliases for convenience across Genesis Engine code.
using EwId9 = EigenWare::EwId9;
inline EwId9 ew_id9_from_ascii(const uint8_t* data, size_t n) {
    return EigenWare::ew_id9_from_ascii(reinterpret_cast<const char*>(data), n);
}
inline EwId9 ew_id9_from_string_ascii(const std::string& s) { return EigenWare::ew_id9_from_string_ascii(s); }
inline EwId9 ew_id9_from_u64(uint64_t v) { return EigenWare::ew_id9_from_u64(v); }
