#include "GE_coherence_graph_store.hpp"

#include <algorithm>
#include <cstdio>

using EigenWare::EwId9;

static inline bool ew_write_u32(FILE* f, uint32_t v) {
    return std::fwrite(&v, 1, sizeof(v), f) == sizeof(v);
}
static inline bool ew_write_i32(FILE* f, int32_t v) {
    return std::fwrite(&v, 1, sizeof(v), f) == sizeof(v);
}
static inline bool ew_write_id9(FILE* f, const EwId9& id) {
    return std::fwrite(id.u32.data(), 1, sizeof(uint32_t) * 9, f) == sizeof(uint32_t) * 9;
}
static inline bool ew_read_u32(FILE* f, uint32_t& v) {
    return std::fread(&v, 1, sizeof(v), f) == sizeof(v);
}
static inline bool ew_read_i32(FILE* f, int32_t& v) {
    return std::fread(&v, 1, sizeof(v), f) == sizeof(v);
}
static inline bool ew_read_id9(FILE* f, EwId9& id) {
    return std::fread(id.u32.data(), 1, sizeof(uint32_t) * 9, f) == sizeof(uint32_t) * 9;
}

void GE_CoherenceGraphStore::clear() {
    nodes.clear();
}

static size_t ge_find_or_add_node(std::vector<GE_CoherenceNode>& nodes, const EwId9& id9) {
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].node_id9 == id9) return i;
    }
    GE_CoherenceNode n;
    n.node_id9 = id9;
    nodes.push_back(n);
    return nodes.size() - 1;
}

static void ge_upsert_edge_oneway(GE_CoherenceNode& n, const EwId9& neigh, int32_t w_q16_16, uint32_t cap) {
    // Find existing.
    for (auto& e : n.edges) {
        if (e.neighbor_id9 == neigh) {
            e.weight_q16_16 = w_q16_16;
            goto reduce;
        }
    }
    {
        GE_CoherenceEdge e;
        e.neighbor_id9 = neigh;
        e.weight_q16_16 = w_q16_16;
        n.edges.push_back(e);
    }

reduce:
    // Deterministic: sort by neighbor id then by weight (descending) to break ties.
    std::stable_sort(n.edges.begin(), n.edges.end(), [](const GE_CoherenceEdge& a, const GE_CoherenceEdge& b) {
        if (a.neighbor_id9 != b.neighbor_id9) return a.neighbor_id9 < b.neighbor_id9;
        return a.weight_q16_16 > b.weight_q16_16;
    });
    // Keep unique neighbors: after sort, duplicates adjacent.
    std::vector<GE_CoherenceEdge> uniq;
    uniq.reserve(n.edges.size());
    for (size_t i = 0; i < n.edges.size(); ++i) {
        if (!uniq.empty() && uniq.back().neighbor_id9 == n.edges[i].neighbor_id9) {
            // keep the first (highest weight due to sort)
            continue;
        }
        uniq.push_back(n.edges[i]);
    }
    n.edges.swap(uniq);

    // Degree cap: keep strongest edges by absolute weight, stable.
    if (cap != 0u && n.edges.size() > (size_t)cap) {
        std::stable_sort(n.edges.begin(), n.edges.end(), [](const GE_CoherenceEdge& a, const GE_CoherenceEdge& b) {
            const int64_t aa = (a.weight_q16_16 < 0) ? -(int64_t)a.weight_q16_16 : (int64_t)a.weight_q16_16;
            const int64_t bb = (b.weight_q16_16 < 0) ? -(int64_t)b.weight_q16_16 : (int64_t)b.weight_q16_16;
            if (aa != bb) return aa > bb;
            return a.neighbor_id9 < b.neighbor_id9;
        });
        n.edges.resize((size_t)cap);
        std::stable_sort(n.edges.begin(), n.edges.end(), [](const GE_CoherenceEdge& a, const GE_CoherenceEdge& b) {
            return a.neighbor_id9 < b.neighbor_id9;
        });
    }
}

void GE_CoherenceGraphStore::upsert_edge_undirected(const EwId9& a_id9,
                                                   const EwId9& b_id9,
                                                   int32_t weight_q16_16,
                                                   uint32_t degree_cap_u32) {
    const size_t ia = ge_find_or_add_node(nodes, a_id9);
    const size_t ib = ge_find_or_add_node(nodes, b_id9);
    ge_upsert_edge_oneway(nodes[ia], b_id9, weight_q16_16, degree_cap_u32);
    ge_upsert_edge_oneway(nodes[ib], a_id9, weight_q16_16, degree_cap_u32);

    // Keep node list ordered deterministically.
    std::stable_sort(nodes.begin(), nodes.end(), [](const GE_CoherenceNode& a, const GE_CoherenceNode& b) {
        return a.node_id9 < b.node_id9;
    });
}

bool GE_CoherenceGraphStore::get_edges(const EwId9& node_id9, std::vector<GE_CoherenceEdge>& out) const {
    for (const auto& n : nodes) {
        if (n.node_id9 == node_id9) {
            out = n.edges;
            return true;
        }
    }
    out.clear();
    return false;
}

bool GE_CoherenceGraphStore::save_to_file(const std::string& path_utf8) const {
    FILE* f = std::fopen(path_utf8.c_str(), "wb");
    if (!f) return false;
    // Magic: GECG
    const uint32_t magic = 0x47454347u;
    const uint32_t ver = 1u;
    bool ok = ew_write_u32(f, magic) && ew_write_u32(f, ver);
    ok = ok && ew_write_u32(f, (uint32_t)nodes.size());
    for (const auto& n : nodes) {
        ok = ok && ew_write_id9(f, n.node_id9);
        ok = ok && ew_write_u32(f, (uint32_t)n.edges.size());
        for (const auto& e : n.edges) {
            ok = ok && ew_write_id9(f, e.neighbor_id9);
            ok = ok && ew_write_i32(f, e.weight_q16_16);
        }
    }
    std::fclose(f);
    return ok;
}

bool GE_CoherenceGraphStore::load_from_file(const std::string& path_utf8) {
    FILE* f = std::fopen(path_utf8.c_str(), "rb");
    if (!f) return false;
    uint32_t magic = 0, ver = 0, n_nodes = 0;
    if (!ew_read_u32(f, magic) || !ew_read_u32(f, ver) || magic != 0x47454347u || ver != 1u) {
        std::fclose(f);
        return false;
    }
    if (!ew_read_u32(f, n_nodes)) {
        std::fclose(f);
        return false;
    }
    nodes.clear();
    nodes.reserve(n_nodes);
    for (uint32_t i = 0; i < n_nodes; ++i) {
        GE_CoherenceNode n;
        uint32_t deg = 0;
        if (!ew_read_id9(f, n.node_id9) || !ew_read_u32(f, deg)) {
            std::fclose(f);
            return false;
        }
        n.edges.reserve(deg);
        for (uint32_t j = 0; j < deg; ++j) {
            GE_CoherenceEdge e;
            if (!ew_read_id9(f, e.neighbor_id9) || !ew_read_i32(f, e.weight_q16_16)) {
                std::fclose(f);
                return false;
            }
            n.edges.push_back(e);
        }
        nodes.push_back(n);
    }
    std::fclose(f);
    std::stable_sort(nodes.begin(), nodes.end(), [](const GE_CoherenceNode& a, const GE_CoherenceNode& b) {
        return a.node_id9 < b.node_id9;
    });
    return true;
}

void GE_CoherenceGraphStore::sort_and_compact(uint32_t degree_cap_u32) {
    // Stable-sort nodes.
    std::stable_sort(nodes.begin(), nodes.end(), [](const GE_CoherenceNode& a, const GE_CoherenceNode& b) {
        return a.node_id9 < b.node_id9;
    });

    // For each node: sort edges, dedupe, cap degree.
    for (auto& n : nodes) {
        // Sort by neighbor id, then by |weight| descending to keep strongest first per neighbor.
        std::stable_sort(n.edges.begin(), n.edges.end(), [](const GE_CoherenceEdge& a, const GE_CoherenceEdge& b) {
            if (a.neighbor_id9 != b.neighbor_id9) return a.neighbor_id9 < b.neighbor_id9;
            const int64_t aa = (a.weight_q16_16 < 0) ? -(int64_t)a.weight_q16_16 : (int64_t)a.weight_q16_16;
            const int64_t bb = (b.weight_q16_16 < 0) ? -(int64_t)b.weight_q16_16 : (int64_t)b.weight_q16_16;
            return aa > bb;
        });

        // Dedupe neighbors (keep first due to sort).
        std::vector<GE_CoherenceEdge> uniq;
        uniq.reserve(n.edges.size());
        for (size_t i = 0; i < n.edges.size(); ++i) {
            if (!uniq.empty() && uniq.back().neighbor_id9 == n.edges[i].neighbor_id9) continue;
            uniq.push_back(n.edges[i]);
        }
        n.edges.swap(uniq);

        // Degree cap: keep strongest by |weight|.
        if (degree_cap_u32 != 0u && n.edges.size() > (size_t)degree_cap_u32) {
            std::stable_sort(n.edges.begin(), n.edges.end(), [](const GE_CoherenceEdge& a, const GE_CoherenceEdge& b) {
                const int64_t aa = (a.weight_q16_16 < 0) ? -(int64_t)a.weight_q16_16 : (int64_t)a.weight_q16_16;
                const int64_t bb = (b.weight_q16_16 < 0) ? -(int64_t)b.weight_q16_16 : (int64_t)b.weight_q16_16;
                if (aa != bb) return aa > bb;
                return a.neighbor_id9 < b.neighbor_id9;
            });
            n.edges.resize((size_t)degree_cap_u32);
        }

        // Restore neighbor-id order for deterministic iteration.
        std::stable_sort(n.edges.begin(), n.edges.end(), [](const GE_CoherenceEdge& a, const GE_CoherenceEdge& b) {
            return a.neighbor_id9 < b.neighbor_id9;
        });
    }
}
