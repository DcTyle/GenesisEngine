#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ew_id9.hpp"

// Coherence Graph Store (CGCR)
// ---------------------------
// Deterministic, bounded-degree graph over EwId9 nodes.
// - No hashing/unordered maps.
// - Merge/reduction is stable-sort based.
// - Weights are fixed-point Q16.16.

struct GE_CoherenceEdge {
    EigenWare::EwId9 neighbor_id9{};
    int32_t weight_q16_16 = 0;
};

struct GE_CoherenceNode {
    EigenWare::EwId9 node_id9{};
    std::vector<GE_CoherenceEdge> edges;
};

struct GE_CoherenceGraphStore {
    std::vector<GE_CoherenceNode> nodes;

    void clear();

    // Upsert an undirected edge (a<->b) with bounded degree.
    // degree_cap_u32 is enforced per endpoint.
    void upsert_edge_undirected(const EigenWare::EwId9& a_id9,
                               const EigenWare::EwId9& b_id9,
                               int32_t weight_q16_16,
                               uint32_t degree_cap_u32);

    // Deterministic serialization.
    bool save_to_file(const std::string& path_utf8) const;
    bool load_from_file(const std::string& path_utf8);

    // Retrieve neighbor list indices for a node. Returns false if not found.
    bool get_edges(const EigenWare::EwId9& node_id9, std::vector<GE_CoherenceEdge>& out) const;
};
