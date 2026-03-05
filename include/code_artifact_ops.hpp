#pragma once

#include <cstdint>
#include <string>

class SubstrateMicroprocessor;

// -----------------------------------------------------------------------------
//  Code Artifact Operators (Blueprint/Spec: liberty-space computation)
// -----------------------------------------------------------------------------
// These operators do not edit the filesystem. They write workspace artifacts
// into substrate inspector fields. The hydrator later projects committed
// inspector artifacts into functioning files.
//
// Determinism requirements:
//   - No platform randomness.
//   - Only integer/fixed-point derived bookkeeping.
//   - Coherence gate must run before commit_ready is set.

// Emit a minimal, self-contained C++ module (hpp+cpp) and a CMake stanza.
// The module_name is sanitized deterministically.
void code_emit_minimal_cpp_module(SubstrateMicroprocessor* sm, const std::string& module_name_utf8);

// Determine artifact kind from a repository-relative path (no hashing/crypto).
uint32_t code_artifact_kind_from_rel_path(const std::string& rel_path);

// -----------------------------------------------------------------------------
// Deterministic patch/edit operators (Blueprint: coherence-gated writing)
// -----------------------------------------------------------------------------

enum EwPatchModeU16 : uint16_t {
    EW_PATCH_APPEND_EOF = 1,
    EW_PATCH_INSERT_AFTER_ANCHOR = 2,
    EW_PATCH_REPLACE_BETWEEN_ANCHORS = 3,
    EW_PATCH_DELETE_BETWEEN_ANCHORS = 4,
};

struct EwPatchSpec {
    uint16_t mode_u16 = EW_PATCH_APPEND_EOF;
    std::string anchor_a; // used for INSERT_AFTER / REPLACE/DELETE start
    std::string anchor_b; // used for REPLACE/DELETE end
    std::string text;     // inserted or replacement text
};

// Apply a deterministic patch to an inspector artifact in a coherence-gated way.
// - Generates a fixed number of candidate edits (deterministic variants)
// - Validates each candidate with conservative compiler-surrogate rules
// - Accepts the best candidate only if coherence improves by a minimum delta
// - On reject, routes the attempt into dark excitation (non-projecting)
// Returns true on commit-ready artifact write.
bool code_apply_patch_coherence_gated(
    SubstrateMicroprocessor* sm,
    const std::string& rel_path,
    uint32_t kind_u32,
    const EwPatchSpec& spec,
    uint32_t producer_operator_id_u32
);

// Emit a hydration hint artifact (does not write filesystem).
void code_emit_hydration_hint(SubstrateMicroprocessor* sm, const std::string& root_dir_rel);

// Deterministic fold for path bookkeeping (non-security).
uint32_t path_fold_u32_from_rel_path(const std::string& rel_path);
