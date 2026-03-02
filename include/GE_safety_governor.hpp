#pragma once
#include <cstdint>

// ----------------------------------------------------------------------------
// Genesis Engine Safety Governor (deterministic caps)
// ----------------------------------------------------------------------------
// This governor enforces hard upper bounds per epoch/tick on:
// - explicit materialized anchor writes
// - explicit coherence edge writes
// - explicit byte materialization
//
// Compute remains GPU-side; these caps are applied as a deterministic commit gate
// so replay is stable and VRAM/bandwidth budgets are respected.
//
struct GE_SafetyCaps {
    uint64_t max_anchor_writes_u64 = 100000;   // per epoch
    uint64_t max_edge_writes_u64   = 500000;   // per epoch
    uint64_t max_materialize_bytes_u64 = (uint64_t)256 * 1024 * 1024; // 256 MiB per epoch
    uint32_t max_fanout_u32 = 64;              // top-K / degree cap
};

// A single deterministic default.
GE_SafetyCaps GE_default_safety_caps();
