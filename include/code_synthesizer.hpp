#pragma once

#include <string>

class SubstrateManager;

// Deterministic code synthesis + patch emission.
//
// This is a grammar-constrained synthesizer used to turn a user request into
// coherence-gated Inspector artifacts (code/docs/config). It does NOT attempt
// unconstrained generation; it relies on templates and conservative patching.
class EwCodeSynthesizer {
public:
    // Returns true if it emitted at least one artifact candidate.
    static bool synthesize(SubstrateManager* sm, const std::string& request_utf8);
};
