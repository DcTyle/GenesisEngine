#pragma once

#include <cstdint>
#include <vector>

#include "anchor.hpp"
#include "GE_coherence_packets.hpp"


// CANONICAL HEADER: All simulation logic for photonic confinement, field tensor force, particle coupling, and field constraint processing
// must be defined or routed through this header for reuse and single source of truth in the photonic confinement research engine.
// - Updates rho/coupling maps from boundary observables (currently anchored proxies).
// - Spawns/updates coupling particles deterministically from voxel density.
// - Computes influx (reverse leakage) and publishes to coherence bus via out_influx.

void ew_voxel_coupling_step(uint64_t canonical_tick_u64,
                           std::vector<Anchor>& anchors,
                           std::vector<EwInfluxPublishPacket>& out_influx);
