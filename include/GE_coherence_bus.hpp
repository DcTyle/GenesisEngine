#pragma once

#include <cstdint>

struct EwState;
struct EwCtx;

// Step the global coherence bus for this tick.
// - Collects leakage packets from producers (spectral anchors).
// - Stable-sorts and reduces.
// - Emits bounded hook packets back to producers.
void ew_coherence_bus_step(EwState& cand, const EwCtx& ctx);
