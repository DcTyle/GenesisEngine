#pragma once

#include <cstdint>
#include <string>

// Coherence gate for inspector artifacts.
// This is intentionally minimal and deterministic. It does not interpret intent;
// it only validates admissibility for projection into functioning files.

struct EwCoherenceResult {
    uint16_t coherence_q15 = 0;   // [0, 32768]
    bool commit_ready = false;
    uint32_t denial_code_u32 = 0;
};

class EwCoherenceGate {
public:
    // Validate a single artifact payload for projection.
    // kind_u32 uses EwArtifactKind.
    static EwCoherenceResult validate_artifact(
        const std::string& rel_path,
        uint32_t kind_u32,
        const std::string& payload
    );

private:
    static bool rel_path_is_safe(const std::string& rel_path);
    static bool braces_balanced_cpp_like(const std::string& s);
};
