#pragma once

#include <cstdint>
#include <vector>

#include "ewmesh_voxelizer.hpp"

namespace genesis {

// Bake per-object variables (density/coherence/curvature/id) into an RGBA8 UV atlas.
//
// Inputs:
//  - mesh: must have deterministic UVs in [0,1]
//  - occupancy_u8: dense voxel grid (grid_x*grid_y*grid_z bytes)
// Output:
//  - out_rgba8: size atlas_w*atlas_h*4
//
// Determinism:
//  - Rasterization uses fixed-point barycentric stepping.
//  - Tie-breaking on overlaps favors the highest density then lowest tri index.
bool ge_bake_uv_atlas_rgba8(const EwMeshV1& mesh,
                            uint64_t object_id_u64,
                            uint32_t grid_x_u32,
                            uint32_t grid_y_u32,
                            uint32_t grid_z_u32,
                            const uint8_t* occupancy_u8,
                            uint32_t atlas_w_u32,
                            uint32_t atlas_h_u32,
                            std::vector<uint8_t>& out_rgba8);

} // namespace genesis
