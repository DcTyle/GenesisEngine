#pragma once
#include <cstdint>

// Canonical invariant interface per Spec v7.
// The runtime is fail-closed: any invariant violation must route evolution to sink state.
enum EwInvariantViolation : uint32_t {
    EW_INV_OK = 0,
    EW_INV_CONSERVATION_FAIL = 1,
    EW_INV_CAUSALITY_FAIL = 2,
    EW_INV_TOLERANCE_FAIL = 3,
    EW_INV_COORD_SIG_FAIL = 4,
    EW_INV_CARRIER_DIVERGENCE = 5
};

