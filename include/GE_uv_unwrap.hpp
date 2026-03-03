#pragma once

#include "ewmesh_voxelizer.hpp"

namespace genesis {

// Deterministic UV unwrap for engine synthesis.
//
// Policy:
// - Always overwrites u/v on the mesh.
// - Uses a stable 6-chart box projection chosen by vertex normal dominant axis.
// - Normalization is per-chart to maximize UV utilization.
// - Deterministic tie-breaking for axis selection and min/max aggregation.
bool ge_auto_uv_unwrap_box(EwMeshV1& io_mesh);

} // namespace genesis
