#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Inspector Fields are the substrate-resident, coherence-gated representation
// of workspace artifacts (code/docs/config). They are *not* the filesystem.
// The hydrator projects committed inspector artifacts into functioning files.
//
// NOTE (Blueprint engineering intent): Operations exist in liberty-space
// vector fields and execute as software phase-dynamics in the substrate.
// The inspector store is part of that substrate state.

enum EwArtifactKind : uint32_t {
    EW_ARTIFACT_TEXT = 1,
    EW_ARTIFACT_CPP = 2,
    EW_ARTIFACT_HPP = 3,
    EW_ARTIFACT_CMAKE = 4,
    EW_ARTIFACT_MD = 5,
};

struct EwInspectorArtifact {
    // 9D coordinate coord-tag for identity/bookkeeping ().
    uint64_t coord_coord9_u64 = 0;

    // Relative path inside a chosen RootDir.
    std::string rel_path;

    // Artifact kind for minimal coherence checks.
    uint32_t kind_u32 = EW_ARTIFACT_TEXT;

    // Payload is the literal file content.
    std::string payload;

    // Deterministic provenance (which operator produced this artifact).
    uint32_t producer_operator_id_u32 = 0;
    uint64_t producer_tick_u64 = 0;

    // Coherence gating.
    // coherence_q15 in [0, 32768] where 32768 == 1.0.
    uint16_t coherence_q15 = 0;
    bool commit_ready = false;
    uint32_t denial_code_u32 = 0;
};

class EwInspectorFields {
public:
    // Upsert by rel_path (canonical artifact addressing for workspace).
    // Returns index.
    size_t upsert(const EwInspectorArtifact& a);

    const EwInspectorArtifact* find_by_path(const std::string& rel_path) const;

    // Snapshot committed artifacts (commit_ready == true).
    void snapshot_committed(std::vector<EwInspectorArtifact>& out) const;

    // Snapshot all artifacts (committed or not) for substrate-side retrieval.
    void snapshot_all(std::vector<EwInspectorArtifact>& out) const;

    // Snapshot artifacts whose rel_path begins with prefix.
    void snapshot_prefix(const std::string& prefix, std::vector<EwInspectorArtifact>& out) const;

    // Clear committed flags after successful workspace projection.
    void clear_commit_ready();

    // Monotonic revision counter for deterministic caches.
    // Incremented on upsert and clear_commit_ready().
    uint64_t revision_u64() const { return revision_u64_; }

private:
    std::vector<EwInspectorArtifact> artifacts_;

    // NOTE: This is *not* a wall-clock timestamp. It's a deterministic
    // mutation counter used to invalidate derived indices.
    uint64_t revision_u64_ = 0;
};
