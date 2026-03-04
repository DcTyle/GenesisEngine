#include "ew_substrate_manager.h"
#include "GE_runtime.hpp"

SubstrateManager ew_substrate_manager_build_default(uint64_t seed_u64) {
    // SubstrateManager requires an explicit count. U170 baseline uses a
    // minimal default count (0) and grows via explicit anchor creation.
    SubstrateManager sm((size_t)0);
    sm.set_projection_seed(seed_u64);
    // Enable deterministic celestial dynamics by default.
    sm.nbody.enabled_u32 = 1u;
    sm.nbody.initialized_u32 = 0u;
    return sm;
}

