#pragma once

#include <cstdint>
#include <string>
#include <vector>

class EwInspectorFields;

// Deterministic symbol→artifact coherence index.
//
// Purpose:
//   Provide a substrate-side, reproducible way to map a request to the most
//   relevant workspace artifacts.
//
// Constraints:
//   - No external IO.
//   - Deterministic tokenization and stable sorting.
class EwCoherenceGraph {
public:
    struct Match {
        std::string rel_path;
        uint32_t score_u32 = 0;
    };

    void rebuild_from_inspector(const EwInspectorFields& insp);

    void query_best(const std::string& request_utf8, uint32_t max_out,
                    std::vector<Match>& out) const;

    std::string debug_stats() const;

private:
    struct SymRef {
        uint32_t sym_hash_u32 = 0;
        uint32_t art_index_u32 = 0;
        uint16_t weight_q15 = 0;
    };

    struct ArtifactInfo {
        std::string rel_path;
        uint32_t kind_u32 = 0;
    };

    static void tokenize_identifiers_ascii_(const std::string& s, std::vector<std::string>& out);
    static uint32_t fnv1a_u32_(const std::string& s);

    std::vector<ArtifactInfo> artifacts_;
    std::vector<SymRef> sym_refs_;
    uint32_t unique_syms_u32_ = 0;
};
