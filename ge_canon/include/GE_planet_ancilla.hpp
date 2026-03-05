#pragma once

#include <cstdint>

// Forward declare to avoid include cycles.
class SubstrateManager;

namespace genesis {

// Planet ancilla evolves EW_ANCHOR_KIND_PLANET anchors deterministically.
// This is a runtime subsystem (not editor logic).
void ew_tick_planet_ancilla(SubstrateManager* sm);

} // namespace genesis
