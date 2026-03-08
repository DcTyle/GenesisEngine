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

    struct SemanticPatchTarget {
        std::string rel_path;
        uint16_t patch_mode_u16 = 0; // EwPatchModeU16-compatible when available.
        std::string anchor_a;
        std::string anchor_b;
        std::string reason_utf8;
        uint32_t score_u32 = 0;
    };

    struct SemanticPatchDecision {
        uint8_t resolved_u8 = 0u;
        uint8_t ambiguity_level_u8 = 0u;
        uint8_t human_review_prudent_u8 = 0u;
        SemanticPatchTarget winner;
        std::string winner_reason_utf8;
        std::string rejected_candidates_utf8;
        std::vector<SemanticPatchTarget> ranked_candidates;
    };

    void rebuild_from_inspector(const EwInspectorFields& insp);

    void query_best(const std::string& request_utf8, uint32_t max_out,
                    std::vector<Match>& out) const;

    // Plan a rename by listing artifacts likely to reference `old_token_ascii`.
    // This does not mutate any files; it is a deterministic planning aid and a hook point
    // for the future rename-propagation system.
    void plan_rename_ascii(const std::string& old_token_ascii, const std::string& new_token_ascii,
                           uint32_t max_out, std::vector<Match>& out) const;

    // Resolve likely canonical source spans/export regions for a request.
    // The coherence view stays derived; these targets point back to canonical
    // source/export artifacts and their anchor-bounded regions.
    void query_semantic_patch_targets(const std::string& request_utf8, uint32_t max_out,
                                      std::vector<SemanticPatchTarget>& out) const;
    void resolve_semantic_patch_target(const std::string& request_utf8, uint32_t max_out,
                                       SemanticPatchDecision& out) const;

    // Lightweight determinism + bounds sanity check.
    // Does not perform IO and does not mutate any files.
    // Intended as a production smoke test for the coherence index.
    bool selftest(std::string& report_utf8) const;


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
        std::string payload;
    };

    struct AnchorRegion {
        uint32_t art_index_u32 = 0;
        std::string anchor_a;
        std::string anchor_b;
        std::string insertion_anchor;
        std::string region_text;
    };

    static void tokenize_identifiers_ascii_(const std::string& s, std::vector<std::string>& out);
    static uint32_t fnv1a_u32_(const std::string& s);

    std::vector<ArtifactInfo> artifacts_;
    std::vector<AnchorRegion> anchor_regions_;
    std::vector<SymRef> sym_refs_;
    uint32_t unique_syms_u32_ = 0;
};
