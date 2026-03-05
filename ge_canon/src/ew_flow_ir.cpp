#include <cstdint>
#include <vector>

// ew_flow_ir.cpp
//
// Deterministic Flow IR (intermediate representation) primitives.
//
// This module provides a minimal, build-linked IR that can be used by tools to
// represent operator/value transport in a stable, ASCII-safe way.

namespace ew_flow_ir {

struct FlowIrNode {
    // Node id is stable within a graph (0..n-1).
    uint32_t node_id_u32 = 0;
    // Operator kind (maps to packet op_kind when applicable).
    uint32_t op_kind_u32 = 0;
    // Optional lane ids (only v0 populated by convention).
    uint64_t in_lane0_u64 = 0;
    uint64_t out_lane0_u64 = 0;
};

struct FlowIrGraph {
    std::vector<FlowIrNode> nodes;
};

uint32_t ew_flow_ir_version_u32() {
    // Increment only when the IR node contract changes.
    return 1u;
}

void ew_flow_ir_clear(FlowIrGraph& g) {
    g.nodes.clear();
}

void ew_flow_ir_append_node(FlowIrGraph& g, const FlowIrNode& n) {
    FlowIrNode nn = n;
    nn.node_id_u32 = (uint32_t)g.nodes.size();
    g.nodes.push_back(nn);
}

} // namespace ew_flow_ir
