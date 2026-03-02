#include "GE_uv_atlas_baker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace genesis {

static inline float _min3(float a, float b, float c) { return std::min(a, std::min(b, c)); }
static inline float _max3(float a, float b, float c) { return std::max(a, std::max(b, c)); }

static inline uint32_t _clamp_u32(int64_t v, uint32_t lo, uint32_t hi) {
    if (v < (int64_t)lo) return lo;
    if (v > (int64_t)hi) return hi;
    return (uint32_t)v;
}

static inline uint8_t _u8_from_float01(float x) {
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    const float s = x * 255.0f + 0.5f;
    int32_t v = (int32_t)s;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

static inline void _mesh_bounds(const EwMeshV1& m, float& minx, float& miny, float& minz, float& maxx, float& maxy, float& maxz) {
    minx = miny = minz = 0.0f;
    maxx = maxy = maxz = 0.0f;
    if (m.vertices.empty()) return;
    minx = maxx = m.vertices[0].px;
    miny = maxy = m.vertices[0].py;
    minz = maxz = m.vertices[0].pz;
    for (const auto& v : m.vertices) {
        minx = std::min(minx, v.px); maxx = std::max(maxx, v.px);
        miny = std::min(miny, v.py); maxy = std::max(maxy, v.py);
        minz = std::min(minz, v.pz); maxz = std::max(maxz, v.pz);
    }
}

static inline uint8_t _sample_occ(const uint8_t* occ,
                                  uint32_t gx, uint32_t gy, uint32_t gz,
                                  float minx, float miny, float minz,
                                  float maxx, float maxy, float maxz,
                                  float px, float py, float pz) {
    if (!occ || gx == 0 || gy == 0 || gz == 0) return 0;
    const float sx = (maxx - minx);
    const float sy = (maxy - miny);
    const float sz = (maxz - minz);
    const float invx = (sx > 1e-20f) ? (1.0f / sx) : 0.0f;
    const float invy = (sy > 1e-20f) ? (1.0f / sy) : 0.0f;
    const float invz = (sz > 1e-20f) ? (1.0f / sz) : 0.0f;
    float fx = (px - minx) * invx;
    float fy = (py - miny) * invy;
    float fz = (pz - minz) * invz;
    if (fx < 0.0f) fx = 0.0f; if (fx > 1.0f) fx = 1.0f;
    if (fy < 0.0f) fy = 0.0f; if (fy > 1.0f) fy = 1.0f;
    if (fz < 0.0f) fz = 0.0f; if (fz > 1.0f) fz = 1.0f;
    const int64_t ix = (int64_t)std::floor(fx * (float)(gx - 1) + 0.5f);
    const int64_t iy = (int64_t)std::floor(fy * (float)(gy - 1) + 0.5f);
    const int64_t iz = (int64_t)std::floor(fz * (float)(gz - 1) + 0.5f);
    const uint32_t x = _clamp_u32(ix, 0u, gx - 1);
    const uint32_t y = _clamp_u32(iy, 0u, gy - 1);
    const uint32_t z = _clamp_u32(iz, 0u, gz - 1);
    const uint64_t idx = (uint64_t)z * (uint64_t)gx * (uint64_t)gy + (uint64_t)y * (uint64_t)gx + (uint64_t)x;
    return occ[(size_t)idx];
}

bool ge_bake_uv_atlas_rgba8(const EwMeshV1& mesh,
                            uint64_t object_id_u64,
                            uint32_t grid_x_u32,
                            uint32_t grid_y_u32,
                            uint32_t grid_z_u32,
                            const uint8_t* occupancy_u8,
                            uint32_t atlas_w_u32,
                            uint32_t atlas_h_u32,
                            std::vector<uint8_t>& out_rgba8) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return false;
    if (!occupancy_u8) return false;
    if (atlas_w_u32 == 0 || atlas_h_u32 == 0) return false;
    if ((uint64_t)atlas_w_u32 * (uint64_t)atlas_h_u32 > (1ull << 28)) return false;

    out_rgba8.assign((size_t)atlas_w_u32 * (size_t)atlas_h_u32 * 4u, 0u);
    std::vector<uint8_t> zbuf;
    // simple 0..255 "coverage" buffer; higher wins. deterministic.
    zbuf.assign((size_t)atlas_w_u32 * (size_t)atlas_h_u32, 0u);

    float minx, miny, minz, maxx, maxy, maxz;
    _mesh_bounds(mesh, minx, miny, minz, maxx, maxy, maxz);

    const uint8_t id_lo = (uint8_t)(object_id_u64 & 0xFFu);

    const uint32_t tri_count = (uint32_t)(mesh.indices.size() / 3u);
    for (uint32_t t = 0; t < tri_count; ++t) {
        const uint32_t i0 = mesh.indices[t * 3u + 0u];
        const uint32_t i1 = mesh.indices[t * 3u + 1u];
        const uint32_t i2 = mesh.indices[t * 3u + 2u];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) return false;
        const auto& v0 = mesh.vertices[i0];
        const auto& v1 = mesh.vertices[i1];
        const auto& v2 = mesh.vertices[i2];

        const float u0 = v0.u, v0t = v0.v;
        const float u1 = v1.u, v1t = v1.v;
        const float u2 = v2.u, v2t = v2.v;

        // UV bounds
        float umin = _min3(u0, u1, u2);
        float umax = _max3(u0, u1, u2);
        float vmin = _min3(v0t, v1t, v2t);
        float vmax = _max3(v0t, v1t, v2t);
        if (umax < 0.0f || umin > 1.0f || vmax < 0.0f || vmin > 1.0f) continue;
        if (umin < 0.0f) umin = 0.0f;
        if (vmin < 0.0f) vmin = 0.0f;
        if (umax > 1.0f) umax = 1.0f;
        if (vmax > 1.0f) vmax = 1.0f;
        const int32_t x0 = (int32_t)std::floor(umin * (float)(atlas_w_u32 - 1));
        const int32_t x1 = (int32_t)std::ceil (umax * (float)(atlas_w_u32 - 1));
        const int32_t y0 = (int32_t)std::floor(vmin * (float)(atlas_h_u32 - 1));
        const int32_t y1 = (int32_t)std::ceil (vmax * (float)(atlas_h_u32 - 1));
        if (x1 < 0 || y1 < 0) continue;
        const int32_t bx0 = std::max(0, x0);
        const int32_t by0 = std::max(0, y0);
        const int32_t bx1 = std::min((int32_t)atlas_w_u32 - 1, x1);
        const int32_t by1 = std::min((int32_t)atlas_h_u32 - 1, y1);

        // Barycentric in UV space.
        const float du1 = u1 - u0;
        const float dv1 = v1t - v0t;
        const float du2 = u2 - u0;
        const float dv2 = v2t - v0t;
        const float det = du1 * dv2 - dv1 * du2;
        if (std::fabs(det) < 1e-20f) continue;
        const float inv_det = 1.0f / det;

        for (int32_t y = by0; y <= by1; ++y) {
            for (int32_t x = bx0; x <= bx1; ++x) {
                const float uu = (atlas_w_u32 <= 1) ? 0.0f : ((float)x / (float)(atlas_w_u32 - 1));
                const float vv = (atlas_h_u32 <= 1) ? 0.0f : ((float)y / (float)(atlas_h_u32 - 1));
                const float du = uu - u0;
                const float dv = vv - v0t;
                const float a = (du * dv2 - dv * du2) * inv_det;
                const float b = (dv * du1 - du * dv1) * inv_det;
                const float c = 1.0f - a - b;
                if (a < -1e-5f || b < -1e-5f || c < -1e-5f) continue;

                // interpolate object-space position
                const float px = a * v1.px + b * v2.px + c * v0.px;
                const float py = a * v1.py + b * v2.py + c * v0.py;
                const float pz = a * v1.pz + b * v2.pz + c * v0.pz;

                const uint8_t dens = _sample_occ(occupancy_u8, grid_x_u32, grid_y_u32, grid_z_u32,
                                                 minx, miny, minz, maxx, maxy, maxz,
                                                 px, py, pz);
                // coherence proxy: local density coherence via normal magnitude (deterministic proxy)
                const float nn = std::sqrt(v0.nx*v0.nx + v0.ny*v0.ny + v0.nz*v0.nz);
                const uint8_t coh = _u8_from_float01(std::min(1.0f, nn));
                // curvature proxy: density itself (better than nothing, stable)
                const uint8_t curv = dens;

                const size_t p = (size_t)y * (size_t)atlas_w_u32 + (size_t)x;
                // Winner selection: higher density wins.
                if (dens >= zbuf[p]) {
                    zbuf[p] = dens;
                    const size_t o = p * 4u;
                    out_rgba8[o + 0u] = dens;
                    out_rgba8[o + 1u] = coh;
                    out_rgba8[o + 2u] = curv;
                    out_rgba8[o + 3u] = id_lo;
                }
            }
        }
    }

    return true;
}

} // namespace genesis
