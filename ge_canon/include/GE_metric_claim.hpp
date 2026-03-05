#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace genesis {

// Forward declaration (defined in GE_metric_registry.hpp).
enum class MetricKind : uint32_t;

// -----------------------------------------------------------------------------
// MetricClaim
//
// A small, deterministic extraction record for a numeric metric observed in a
// corpus page. It is intentionally coarse but stable.
//
// Determinism rules:
// - Extract in left-to-right byte order from a bounded ASCII view.
// - Fixed upper bound on number of claims per document.
// - No floats; numeric values stored as Q32.32.
// -----------------------------------------------------------------------------

enum class MetricUnitCode : uint32_t {
    Unknown = 0u,

    // Time / frequency
    Seconds = 1u,
    Hertz = 2u,

    // Length
    Meters = 3u,

    // Temperature
    Kelvin = 4u,
    Celsius = 5u,

    // Pressure / modulus
    Pascal = 10u,

    // Conductivity
    W_Per_MK = 20u,   // thermal conductivity
    S_Per_M = 21u,    // electrical conductivity

    // Diffusion
    M2_Per_S = 30u,
};

struct MetricClaim {
    MetricKind kind = (MetricKind)0u;

    // Primary numeric value in Q32.32 (after simple unit scaling where possible).
    int64_t value_q32_32 = 0;

    // Normalized unit code.
    uint32_t unit_code_u32 = (uint32_t)MetricUnitCode::Unknown;

    // Coarse context flags (bitfield). Reserved for future conditions.
    uint32_t context_flags_u32 = 0u;

    // Ordinal of the claim inside the source document (0..N-1).
    uint32_t claim_ordinal_u32 = 0u;
};

// Extract up to max_claims MetricClaim records from a bounded ASCII view of utf8.
// Returns number of claims appended to out_claims.
uint32_t ew_extract_metric_claims_from_utf8_bounded(
    const std::string& utf8,
    uint32_t text_cap_bytes_u32,
    uint32_t max_claims_u32,
    std::vector<MetricClaim>& out_claims
);

} // namespace genesis
