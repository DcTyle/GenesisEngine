#include "GE_shell_mesh.hpp"

#include "GE_uv_unwrap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace genesis {

struct V3 { float x,y,z; };

static inline V3 v3_add(const V3& a, const V3& b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static inline V3 v3_mul(const V3& a, float s) { return {a.x*s, a.y*s, a.z*s}; }
static inline float v3_dot(const V3& a, const V3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline float v3_len(const V3& a) { return std::sqrt(v3_dot(a,a)); }
static inline V3 v3_norm(const V3& a) {
    const float l = v3_len(a);
    if (l <= 1e-30f) return {0,1,0};
    const float inv = 1.0f / l;
    return {a.x*inv, a.y*inv, a.z*inv};
}

struct EdgePair { uint32_t a,b; uint32_t mid; };

static inline uint64_t edge_key(uint32_t a, uint32_t b) {
    const uint32_t lo = (a < b) ? a : b;
    const uint32_t hi = (a < b) ? b : a;
    return ((uint64_t)lo << 32) | (uint64_t)hi;
}

static void build_icosahedron(std::vector<V3>& verts, std::vector<uint32_t>& tris) {
    verts.clear();
    tris.clear();
    const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
    const float a = 1.0f;
    const float b = 1.0f / phi;

    // 12 vertices
    const V3 v[] = {
        {-b,  a,  0}, { b,  a,  0}, {-b, -a,  0}, { b, -a,  0},
        { 0, -b,  a}, { 0,  b,  a}, { 0, -b, -a}, { 0,  b, -a},
        { a,  0, -b}, { a,  0,  b}, {-a,  0, -b}, {-a,  0,  b},
    };
    verts.assign(v, v + 12);
    for (auto& p : verts) p = v3_norm(p);

    const uint32_t t[] = {
        0,11,5,  0,5,1,  0,1,7,  0,7,10, 0,10,11,
        1,5,9,   5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4,   3,4,2,  3,2,6,  3,6,8,  3,8,9,
        4,9,5,   2,4,11, 6,2,10, 8,6,7,  9,8,1
    };
    tris.assign(t, t + (sizeof(t)/sizeof(t[0])));
}

static void subdivide_icosphere(std::vector<V3>& verts, std::vector<uint32_t>& tris, uint32_t level) {
    for (uint32_t l = 0; l < level; ++l) {
        // Collect all edges.
        const uint32_t tri_count = (uint32_t)(tris.size() / 3u);
        std::vector<EdgePair> edges;
        edges.reserve((size_t)tri_count * 3u);
        for (uint32_t ti = 0; ti < tri_count; ++ti) {
            const uint32_t i0 = tris[ti*3u+0u];
            const uint32_t i1 = tris[ti*3u+1u];
            const uint32_t i2 = tris[ti*3u+2u];
            edges.push_back({i0,i1,0});
            edges.push_back({i1,i2,0});
            edges.push_back({i2,i0,0});
        }
        // Sort edges by key deterministically.
        std::sort(edges.begin(), edges.end(), [](const EdgePair& e1, const EdgePair& e2){
            return edge_key(e1.a,e1.b) < edge_key(e2.a,e2.b);
        });
        // Unique edges and create midpoints.
        uint64_t last_key = UINT64_MAX;
        uint32_t last_mid = 0;
        for (auto& e : edges) {
            const uint64_t k = edge_key(e.a,e.b);
            if (k != last_key) {
                const V3 p = v3_norm(v3_mul(v3_add(verts[e.a], verts[e.b]), 0.5f));
                last_mid = (uint32_t)verts.size();
                verts.push_back(p);
                last_key = k;
            }
            e.mid = last_mid;
        }
        // Rebuild triangles.
        std::vector<uint32_t> new_tris;
        new_tris.reserve(tris.size() * 4u);

        // Helper to find mid for an edge: binary search in sorted edges.
        auto mid_for = [&](uint32_t a, uint32_t b)->uint32_t {
            const uint64_t k = edge_key(a,b);
            size_t lo = 0, hi = edges.size();
            while (lo < hi) {
                size_t mid = lo + (hi - lo)/2;
                const uint64_t km = edge_key(edges[mid].a, edges[mid].b);
                if (km == k) return edges[mid].mid;
                if (km < k) lo = mid + 1;
                else hi = mid;
            }
            return 0u;
        };

        for (uint32_t ti = 0; ti < tri_count; ++ti) {
            const uint32_t i0 = tris[ti*3u+0u];
            const uint32_t i1 = tris[ti*3u+1u];
            const uint32_t i2 = tris[ti*3u+2u];
            const uint32_t m01 = mid_for(i0,i1);
            const uint32_t m12 = mid_for(i1,i2);
            const uint32_t m20 = mid_for(i2,i0);
            // 4 new tris
            new_tris.push_back(i0); new_tris.push_back(m01); new_tris.push_back(m20);
            new_tris.push_back(i1); new_tris.push_back(m12); new_tris.push_back(m01);
            new_tris.push_back(i2); new_tris.push_back(m20); new_tris.push_back(m12);
            new_tris.push_back(m01); new_tris.push_back(m12); new_tris.push_back(m20);
        }
        tris.swap(new_tris);
    }
}

static inline uint8_t occ_at(const uint8_t* occ, uint32_t gx, uint32_t gy, uint32_t gz, int32_t x, int32_t y, int32_t z) {
    if (!occ) return 0;
    if (x < 0 || y < 0 || z < 0) return 0;
    if ((uint32_t)x >= gx || (uint32_t)y >= gy || (uint32_t)z >= gz) return 0;
    const uint64_t idx = (uint64_t)z * (uint64_t)gx * (uint64_t)gy + (uint64_t)y * (uint64_t)gx + (uint64_t)x;
    return occ[(size_t)idx];
}

static float raymarch_to_surface(const uint8_t* occ, uint32_t gx, uint32_t gy, uint32_t gz,
                                 uint8_t thr,
                                 const V3& dir_unit) {
    // Center in voxel coords.
    const float cx = (float)(gx - 1) * 0.5f;
    const float cy = (float)(gy - 1) * 0.5f;
    const float cz = (float)(gz - 1) * 0.5f;
    // Max radius to bounds.
    const float maxr = std::sqrt(cx*cx + cy*cy + cz*cz);
    const float step = 0.5f; // voxel units

    float r = 0.0f;
    uint8_t last = 0;
    for (uint32_t i = 0; i < 8192u; ++i) {
        const float px = cx + dir_unit.x * r;
        const float py = cy + dir_unit.y * r;
        const float pz = cz + dir_unit.z * r;
        const int32_t xi = (int32_t)std::floor(px + 0.5f);
        const int32_t yi = (int32_t)std::floor(py + 0.5f);
        const int32_t zi = (int32_t)std::floor(pz + 0.5f);
        const uint8_t o = occ_at(occ, gx, gy, gz, xi, yi, zi);
        if (o >= thr && last < thr) {
            // refine by binary search between r-step and r
            float lo = (r >= step) ? (r - step) : 0.0f;
            float hi = r;
            for (int it = 0; it < 10; ++it) {
                const float mid = (lo + hi) * 0.5f;
                const float mx = cx + dir_unit.x * mid;
                const float my = cy + dir_unit.y * mid;
                const float mz = cz + dir_unit.z * mid;
                const int32_t mxi = (int32_t)std::floor(mx + 0.5f);
                const int32_t myi = (int32_t)std::floor(my + 0.5f);
                const int32_t mzi = (int32_t)std::floor(mz + 0.5f);
                const uint8_t mo = occ_at(occ, gx, gy, gz, mxi, myi, mzi);
                if (mo >= thr) hi = mid;
                else lo = mid;
            }
            return hi;
        }
        last = o;
        r += step;
        if (r > maxr) break;
    }
    // If we never hit, return maxr.
    return maxr;
}

bool ge_build_shell_mesh_from_voxels(uint32_t shell_subdiv_level,
                                     uint32_t grid_x_u32,
                                     uint32_t grid_y_u32,
                                     uint32_t grid_z_u32,
                                     const uint8_t* occ_u8,
                                     uint8_t occ_threshold_u8,
                                     EwMeshV1& out_shell_mesh) {
    if (!occ_u8) return false;
    if (grid_x_u32 == 0 || grid_y_u32 == 0 || grid_z_u32 == 0) return false;
    if (shell_subdiv_level > 6u) return false;

    std::vector<V3> base_v;
    std::vector<uint32_t> tris;
    build_icosahedron(base_v, tris);
    subdivide_icosphere(base_v, tris, shell_subdiv_level);

    // Wrap verts to surface by raymarching in voxel coords.
    const float cx = (float)(grid_x_u32 - 1) * 0.5f;
    const float cy = (float)(grid_y_u32 - 1) * 0.5f;
    const float cz = (float)(grid_z_u32 - 1) * 0.5f;
    const float inv_scale = 1.0f / std::max(1.0f, std::max((float)grid_x_u32, std::max((float)grid_y_u32, (float)grid_z_u32)));
    out_shell_mesh.vertices.clear();
    out_shell_mesh.indices = tris;
    out_shell_mesh.vertices.resize(base_v.size());

    for (size_t i = 0; i < base_v.size(); ++i) {
        const V3 dir = v3_norm(base_v[i]);
        const float r = raymarch_to_surface(occ_u8, grid_x_u32, grid_y_u32, grid_z_u32, occ_threshold_u8, dir);
        // Convert from voxel units to object units roughly normalized.
        const float px = (cx + dir.x * r) - cx;
        const float py = (cy + dir.y * r) - cy;
        const float pz = (cz + dir.z * r) - cz;
        EwMeshV1::Vtx v{};
        v.px = px * inv_scale;
        v.py = py * inv_scale;
        v.pz = pz * inv_scale;
        v.nx = dir.x;
        v.ny = dir.y;
        v.nz = dir.z;
        v.u = 0.0f;
        v.v = 0.0f;
        out_shell_mesh.vertices[i] = v;
    }

    // Deterministic UV unwrap on the shell.
    (void)ge_auto_uv_unwrap_box(out_shell_mesh);
    return true;
}

} // namespace genesis
