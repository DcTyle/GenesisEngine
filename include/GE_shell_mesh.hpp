#pragma once

#include <cstdint>
#include <vector>

#include "ewmesh_voxelizer.hpp"

namespace genesis {

// Build a deterministic "standard shell" mesh (icosphere) and wrap it onto a
// voxel occupancy volume by raymarching along each vertex direction.
//
// This serves as the export topology: stable vertex/edge/triangle counts with
// per-frame morphs expressed as vertex deltas.
//
// shell_subdiv_level:
//  0 => base icosahedron
//  1..6 => increasingly dense. Level 5 ~ 10k verts, ~20k tris.
bool ge_build_shell_mesh_from_voxels(uint32_t shell_subdiv_level,
                                     uint32_t grid_x_u32,
                                     uint32_t grid_y_u32,
                                     uint32_t grid_z_u32,
                                     const uint8_t* occ_u8,
                                     uint8_t occ_threshold_u8,
                                     EwMeshV1& out_shell_mesh);

} // namespace genesis
