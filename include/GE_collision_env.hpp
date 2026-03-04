#pragma once

#include <cstdint>

struct EwState;
struct EwCtx;

// Computes deterministic collision environment coefficients per object anchor
// using only canonical anchor observables (world_flux + global phys coherence).
// Writes results into Anchor::collision_env_* fields and emits optional packets
// via the state's bounded packet ring (if enabled).
void ew_collision_env_step(EwState& cand, const EwCtx& ctx);
