#pragma once
#include <cstdint>

class SubstrateManager;

// Canonical boot-freeze substrate build surface per Spec v7.
// Constructs the immutable carrier anchor set and initializes deterministic runtime state.
SubstrateManager ew_substrate_manager_build_default(uint64_t seed_u64);

