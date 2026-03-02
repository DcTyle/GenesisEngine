#include "GE_safety_governor.hpp"

GE_SafetyCaps GE_default_safety_caps() {
    GE_SafetyCaps c;
    // Conservative defaults intended for RTX 2060-class budgets (can be tightened).
    c.max_anchor_writes_u64 = 100000;
    c.max_edge_writes_u64 = 300000;
    c.max_materialize_bytes_u64 = (uint64_t)192 * 1024 * 1024; // 192 MiB
    c.max_fanout_u32 = 64;
    return c;
}
