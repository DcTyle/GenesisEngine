#pragma once

#include <cstdint>
#include <string>
#include <vector>

// EWM1 mesh reader + deterministic voxelizer.
//
// The voxelizer produces an occupancy_u8 volume suitable for Genesis synthesis.

namespace genesis {

struct EwMeshV1 {
    struct Vtx { float px, py, pz; float nx, ny, nz; float u, v; };
    std::vector<Vtx> vertices;
    std::vector<uint32_t> indices;
};

bool ewmesh_read_v1(const std::string& path_utf8, EwMeshV1& out_mesh);

// Write EWM1 mesh. Deterministic: writes contiguous header+arrays.
bool ewmesh_write_v1(const std::string& path_utf8, const EwMeshV1& mesh);

// Voxelize into a dense occupancy grid.
// - grid dims must be non-zero
// - output size is grid_x*grid_y*grid_z bytes
bool ewmesh_voxelize_occupancy_u8(const EwMeshV1& m,
                                 bool materials_calib_done,
                                 uint32_t grid_x_u32,
                                 uint32_t grid_y_u32,
                                 uint32_t grid_z_u32,
                                 std::vector<uint8_t>& out_occ_u8);

} // namespace genesis
