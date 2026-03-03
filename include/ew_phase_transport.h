#pragma once
#include <cstdint>

class SubstrateMicroprocessor;

// Canonical operator surface per Spec v7.
// Computes deterministic dtheta (TURN_SCALE domain) for the current tick context.
// This callable is required by Spec v7, even if the packed operator page also implements
// a transport surrogate.
uint64_t ew_phase_transport_dtheta_u64(const SubstrateMicroprocessor* sm);

