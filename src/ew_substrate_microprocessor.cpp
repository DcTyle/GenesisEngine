#include "ew_substrate_microprocessor.h"
#include "GE_runtime.hpp"

SubstrateManager ew_substrate_microprocessor_build_default(uint64_t seed_u64) {
    // SubstrateManager requires an explicit count. U170 baseline uses a
    // minimal default count (0) and grows via explicit anchor creation.
    SubstrateManager sm((size_t)0);
    sm.set_projection_seed(seed_u64);
    return sm;
}

