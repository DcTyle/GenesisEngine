#include "GE_pulse_trajectory_bound.hpp"

#include <algorithm>

#include "anchor.hpp"

static inline uint32_t u32_abs_i32(int32_t v) {
    return (uint32_t)((v < 0) ? -v : v);
}

void ge_bound_pulses_per_anchor(
    const std::vector<Pulse>& in,
    uint32_t max_per_anchor_u32,
    std::vector<Pulse>& out,
    uint32_t* out_seen_u32,
    uint32_t* out_dropped_u32)
{
    if (out_seen_u32) *out_seen_u32 = (uint32_t)in.size();
    if (out_dropped_u32) *out_dropped_u32 = 0u;

    out.clear();
    if (in.empty() || max_per_anchor_u32 == 0u) {
        if (!in.empty() && out_dropped_u32) *out_dropped_u32 = (uint32_t)in.size();
        return;
    }

    // Canonical sort key. This intentionally avoids any pointer/address order.
    std::vector<Pulse> tmp = in;
    std::stable_sort(tmp.begin(), tmp.end(), [](const Pulse& a, const Pulse& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.anchor_id != b.anchor_id) return a.anchor_id < b.anchor_id;
        if (a.causal_tag != b.causal_tag) return a.causal_tag < b.causal_tag;
        if (a.profile_id != b.profile_id) return a.profile_id < b.profile_id;
        if (a.f_code != b.f_code) return a.f_code < b.f_code;
        if (a.a_code != b.a_code) return a.a_code < b.a_code;
        if (a.v_code != b.v_code) return a.v_code < b.v_code;
        if (a.i_code != b.i_code) return a.i_code < b.i_code;
        // Deterministic tie-breaker that is still derived only from fields.
        return u32_abs_i32(a.f_code) < u32_abs_i32(b.f_code);
    });

    // Per-anchor counters for the current tick. Note: anchors are addressed by id.
    // We enforce per-tick bounds without unbounded memory: just a map of last seen
    // anchor_id + count within the sorted stream.
    uint32_t cur_anchor = 0xFFFFFFFFu;
    uint32_t cur_count = 0u;
    uint32_t dropped = 0u;

    out.reserve(tmp.size());
    for (size_t i = 0; i < tmp.size(); ++i) {
        const Pulse& p = tmp[i];
        if (p.anchor_id != cur_anchor) {
            cur_anchor = p.anchor_id;
            cur_count = 0u;
        }
        if (cur_count < max_per_anchor_u32) {
            out.push_back(p);
            ++cur_count;
        } else {
            ++dropped;
        }
    }

    if (out_dropped_u32) *out_dropped_u32 = dropped;
}
