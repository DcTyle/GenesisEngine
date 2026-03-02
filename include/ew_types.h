#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "fixed_point.hpp"

// Canonical data-layout header per Spec v7.
// This file defines the stable structs and field names used by the substrate
// runtime dispatcher, ingress validation, and adapter interfaces.
//
// Design rules:
// - ASCII-safe identifiers only.
// - Deterministic: no dynamic allocation inside structs except std::string/std::vector payloads
//   that are filled by adapters and never interpreted as control flow by the substrate.
//
// NOTE: This repo already exposes EwExternalApiRequest/Response in GE_runtime.hpp.
// ew_types.h re-exports them to satisfy the canonical artifact contract without
// duplicating definitions elsewhere.

struct PulsePacketV1 {
    // Tick coordinate provided by the caller (host side). The substrate treats this as ingress metadata only.
    uint64_t tick_u64 = 0;

    // GPU pulse carrier inputs (host measured). All values are interpreted deterministically.
    // Frequency in Hertz. Must be non-zero.
    uint32_t freq_hz_u32 = 0;

    // Effective amplitude (dimensionless) in Q32.32.
    int64_t amp_q32_32 = 0;

    // Pulse width in nanoseconds.
    uint32_t width_ns_u32 = 0;

    // Optional lane mask for selective stimulation (bitmask).
    uint32_t lane_mask_u32 = 0;

    // Optional auxiliary payload for future ingress contracts (must be ignored unless explicitly enabled by spec gates).
    uint32_t aux_u32 = 0;
};

