#include "ewmesh_voxelizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace genesis {

static bool read_exact(std::ifstream& f, void* dst, size_t n) {
    if (n == 0) return true;
    f.read(reinterpret_cast<char*>(dst), (std::streamsize)n);
    return f.good();
}

bool ewmesh_read_v1(const std::string& path_utf8, EwMeshV1& out_mesh) {
    out_mesh.vertices.clear();
    out_mesh.indices.clear();

    std::ifstream f(path_utf8, std::ios::binary);
    if (!f) return false;

    uint32_t magic = 0;
    uint32_t vcount = 0;
    uint32_t icount = 0;
    if (!read_exact(f, &magic, 4)) return false;
    if (!read_exact(f, &vcount, 4)) return false;
    if (!read_exact(f, &icount, 4)) return false;
    if (magic != 0x314D5745u) return false; // 'EWM1'
    if (vcount > (1u << 26)) return false;
    if (icount > (1u << 28)) return false;
    if (icount % 3u != 0u) return false;

    out_mesh.vertices.resize(vcount);
    out_mesh.indices.resize(icount);
    if (!read_exact(f, out_mesh.vertices.data(), sizeof(EwMeshV1::Vtx) * (size_t)vcount)) return false;
    if (!read_exact(f, out_mesh.indices.data(), sizeof(uint32_t) * (size_t)icount)) return false;
    return true;
}



bool ewmesh_write_v1(const std::string& path_utf8, const EwMeshV1& mesh) {
    const uint32_t magic = 0x314D5745u; // 'EWM1'
    const uint32_t vcount = (uint32_t)mesh.vertices.size();
    const uint32_t icount = (uint32_t)mesh.indices.size();
    if (icount % 3u != 0u) return false;
    if (vcount > (1u << 26)) return false;
    if (icount > (1u << 28)) return false;

    std::ofstream f(path_utf8, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&vcount), 4);
    f.write(reinterpret_cast<const char*>(&icount), 4);
    if (!f.good()) return false;
    if (vcount) {
        f.write(reinterpret_cast<const char*>(mesh.vertices.data()),
                (std::streamsize)(sizeof(EwMeshV1::Vtx) * (size_t)vcount));
        if (!f.good()) return false;
    }
    if (icount) {
        f.write(reinterpret_cast<const char*>(mesh.indices.data()),
                (std::streamsize)(sizeof(uint32_t) * (size_t)icount));
        if (!f.good()) return false;
    }
    return true;
}

// Ray-triangle intersection for ray +X from (x0,y0,z0).
// Returns true if intersects at x >= x0 (t >= 0).
static bool ray_x_intersect_tri(double x0, double y0, double z0,
                               const EwMeshV1::Vtx& a,
                               const EwMeshV1::Vtx& b,
                               const EwMeshV1::Vtx& c)
{
    // Moller-Trumbore with dir=(1,0,0).
    const double ax = (double)a.px, ay = (double)a.py, az = (double)a.pz;
    const double bx = (double)b.px, by = (double)b.py, bz = (double)b.pz;
    const double cx = (double)c.px, cy = (double)c.py, cz = (double)c.pz;

    const double e1x = bx - ax;
    const double e1y = by - ay;
    const double e1z = bz - az;
    const double e2x = cx - ax;
    const double e2y = cy - ay;
    const double e2z = cz - az;

    // p = dir x e2, with dir=(1,0,0) => p=(0,-e2z,e2y)
    const double px = 0.0;
    const double py = -e2z;
    const double pz = e2y;

    const double det = e1x * px + e1y * py + e1z * pz;
    if (det == 0.0) return false;
    const double inv_det = 1.0 / det;

    const double tx = x0 - ax;
    const double ty = y0 - ay;
    const double tz = z0 - az;

    const double u = (tx * px + ty * py + tz * pz) * inv_det;
    if (u < 0.0 || u > 1.0) return false;

    // q = t x e1
    const double qx = ty * e1z - tz * e1y;
    const double qy = tz * e1x - tx * e1z;
    const double qz = tx * e1y - ty * e1x;

    // v = dir · q with dir=(1,0,0) => v=qx
    const double v = qx * inv_det;
    if (v < 0.0 || (u + v) > 1.0) return false;

    // t = e2 · q
    const double t = (e2x * qx + e2y * qy + e2z * qz) * inv_det;
    return t >= 0.0;
}

bool ewmesh_voxelize_occupancy_u8(const EwMeshV1& m,
                                 bool materials_calib_done,
                                 uint32_t grid_x_u32,
                                 uint32_t grid_y_u32,
                                 uint32_t grid_z_u32,
                                 std::vector<uint8_t>& out_occ_u8)
{
    // Gate: voxel synthesis requires materials calibration.
    if (!materials_calib_done) return false;

    out_occ_u8.clear();
    if (grid_x_u32 == 0 || grid_y_u32 == 0 || grid_z_u32 == 0) return false;
    if (m.vertices.empty() || m.indices.empty()) return false;

    // Compute AABB.
    float minx = m.vertices[0].px, miny = m.vertices[0].py, minz = m.vertices[0].pz;
    float maxx = minx, maxy = miny, maxz = minz;
    for (const auto& v : m.vertices) {
        minx = (v.px < minx) ? v.px : minx;
        miny = (v.py < miny) ? v.py : miny;
        minz = (v.pz < minz) ? v.pz : minz;
        maxx = (v.px > maxx) ? v.px : maxx;
        maxy = (v.py > maxy) ? v.py : maxy;
        maxz = (v.pz > maxz) ? v.pz : maxz;
    }
    const double dx = (double)maxx - (double)minx;
    const double dy = (double)maxy - (double)miny;
    const double dz = (double)maxz - (double)minz;
    if (dx == 0.0 || dy == 0.0 || dz == 0.0) return false;

    const uint64_t vox_n = (uint64_t)grid_x_u32 * (uint64_t)grid_y_u32 * (uint64_t)grid_z_u32;
    if (vox_n == 0) return false;
    out_occ_u8.assign((size_t)vox_n, 0u);

    // For each voxel center, cast +X ray and apply odd-even rule.
    // Deterministic ordering: z-major, y, x.
    for (uint32_t z = 0; z < grid_z_u32; ++z) {
        for (uint32_t y = 0; y < grid_y_u32; ++y) {
            // Place query point at voxel center in normalized AABB space.
            const double pz = (double)minz + ((double)z + 0.5) * dz / (double)grid_z_u32;
            const double py = (double)miny + ((double)y + 0.5) * dy / (double)grid_y_u32;
            for (uint32_t x = 0; x < grid_x_u32; ++x) {
                const double px = (double)minx + ((double)x + 0.5) * dx / (double)grid_x_u32;
                uint32_t hits = 0;
                for (size_t ti = 0; ti < m.indices.size(); ti += 3) {
                    const uint32_t i0 = m.indices[ti + 0];
                    const uint32_t i1 = m.indices[ti + 1];
                    const uint32_t i2 = m.indices[ti + 2];
                    if (i0 >= m.vertices.size() || i1 >= m.vertices.size() || i2 >= m.vertices.size()) continue;
                    const EwMeshV1::Vtx& a = m.vertices[i0];
                    const EwMeshV1::Vtx& b = m.vertices[i1];
                    const EwMeshV1::Vtx& c = m.vertices[i2];

                    // Quick reject on YZ slab.
                    const double tminy = std::min({(double)a.py, (double)b.py, (double)c.py});
                    const double tmaxy = std::max({(double)a.py, (double)b.py, (double)c.py});
                    const double tminz = std::min({(double)a.pz, (double)b.pz, (double)c.pz});
                    const double tmaxz = std::max({(double)a.pz, (double)b.pz, (double)c.pz});
                    if (py < tminy || py > tmaxy || pz < tminz || pz > tmaxz) continue;

                    if (ray_x_intersect_tri(px, py, pz, a, b, c)) hits++;
                }
                const bool inside = (hits & 1u) != 0u;
                const size_t idx = ((size_t)z * (size_t)grid_y_u32 + (size_t)y) * (size_t)grid_x_u32 + (size_t)x;
                out_occ_u8[idx] = inside ? 255u : 0u;
            }
        }
    }

    return true;
}

} // namespace genesis
