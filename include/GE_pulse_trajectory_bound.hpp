#pragma once

#include <cstdint>
#include <vector>

struct Pulse;

// Bounded, deterministic pulse admission gate.
//
// Prevents unbounded per-tick work by enforcing a fixed maximum number of
// pulses per anchor_id per tick.
//
// Determinism:
// - The input is stable-sorted by a canonical key.
// - For each anchor_id, only the first `max_per_anchor_u32` pulses are kept.
// - Output ordering is the canonical sorted order.
void ge_bound_pulses_per_anchor(
    const std::vector<Pulse>& in,
    uint32_t max_per_anchor_u32,
    std::vector<Pulse>& out,
    uint32_t* out_seen_u32,
    uint32_t* out_dropped_u32);
