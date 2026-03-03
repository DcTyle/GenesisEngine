#pragma once
#include <cstdint>

class SubstrateMicroprocessor;

// Canonical boot-freeze substrate build surface per Spec v7.
// Constructs the immutable carrier anchor set and initializes deterministic runtime state.
SubstrateMicroprocessor ew_substrate_microprocessor_build_default(uint64_t seed_u64);

