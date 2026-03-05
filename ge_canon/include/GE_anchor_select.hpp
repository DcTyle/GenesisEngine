// Deterministic anchor selection helpers.
// These helpers exist to avoid "first in vector order" behavior and to ensure
// multi-anchor setups remain stable across replays.

#pragma once

#include <cstdint>
#include <vector>

#include "anchor.hpp"
// Do not include heavy math headers here; keep this header dependency-light.

// Find the lowest-id anchor of a given kind. Returns nullptr if none.
static inline Anchor* ew_find_lowest_id_anchor(std::vector<Anchor>& anchors, uint32_t kind_u32) {
    Anchor* best = nullptr;
    for (auto& a : anchors) {
        if (a.kind_u32 != kind_u32) continue;
        if (!best || a.id < best->id) best = &a;
    }
    return best;
}

static inline const Anchor* ew_find_lowest_id_anchor_const(const std::vector<Anchor>& anchors, uint32_t kind_u32) {
    const Anchor* best = nullptr;
    for (const auto& a : anchors) {
        if (a.kind_u32 != kind_u32) continue;
        if (!best || a.id < best->id) best = &a;
    }
    return best;
}

// Select the nearest anchor by squared distance to a provided "center" (Q16.16).
// Caller must provide the anchor center in Q16.16 in out_center_q16_16.
// Ties are broken by anchor.id_u32.
template <typename CenterFn>
static inline const Anchor* ew_find_nearest_anchor_const(const std::vector<Anchor>& anchors,
                                                         uint32_t kind_u32,
                                                         const int32_t ref_q16_16[3],
                                                         CenterFn center_fn) {
    const Anchor* best = nullptr;
    __int128 best_d2 = 0;
    for (const auto& a : anchors) {
        if (a.kind_u32 != kind_u32) continue;
        int32_t c_q16_16[3] = {0, 0, 0};
        if (!center_fn(a, c_q16_16)) continue;
        const int64_t dx = (int64_t)c_q16_16[0] - (int64_t)ref_q16_16[0];
        const int64_t dy = (int64_t)c_q16_16[1] - (int64_t)ref_q16_16[1];
        const int64_t dz = (int64_t)c_q16_16[2] - (int64_t)ref_q16_16[2];
        const __int128 d2 = (__int128)dx*dx + (__int128)dy*dy + (__int128)dz*dz;
        if (!best || d2 < best_d2 || (d2 == best_d2 && a.id < best->id)) {
            best = &a;
            best_d2 = d2;
        }
    }
    return best;
}
