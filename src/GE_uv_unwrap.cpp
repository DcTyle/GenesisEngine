#include "GE_uv_unwrap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace genesis {

static inline float _absf(float x) { return x < 0.0f ? -x : x; }

// Chart ids: 0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z
static inline uint32_t _chart_for_normal(float nx, float ny, float nz) {
    const float ax = _absf(nx);
    const float ay = _absf(ny);
    const float az = _absf(nz);
    // Deterministic tie-breaking order: X > Y > Z.
    if (ax >= ay && ax >= az) return (nx >= 0.0f) ? 0u : 1u;
    if (ay >= ax && ay >= az) return (ny >= 0.0f) ? 2u : 3u;
    return (nz >= 0.0f) ? 4u : 5u;
}

static inline void _proj_for_chart(uint32_t chart, float px, float py, float pz, float& out_s, float& out_t) {
    // Stable projections; use right-handed pairs.
    switch (chart) {
        case 0: // +X: project to YZ
        case 1: // -X
            out_s = py;
            out_t = pz;
            return;
        case 2: // +Y: project to XZ
        case 3: // -Y
            out_s = px;
            out_t = pz;
            return;
        default: // +Z/-Z: project to XY
            out_s = px;
            out_t = py;
            return;
    }
}

bool ge_auto_uv_unwrap_box(EwMeshV1& io_mesh) {
    if (io_mesh.vertices.empty() || io_mesh.indices.empty()) return false;

    // First pass: assign charts and gather per-chart projected bounds.
    struct Bounds { float min_s, min_t, max_s, max_t; bool init; };
    Bounds b[6];
    for (int i = 0; i < 6; ++i) {
        b[i].min_s = b[i].min_t = 0.0f;
        b[i].max_s = b[i].max_t = 0.0f;
        b[i].init = false;
    }

    std::vector<uint32_t> chart_of_v;
    chart_of_v.resize(io_mesh.vertices.size());

    for (size_t i = 0; i < io_mesh.vertices.size(); ++i) {
        auto& v = io_mesh.vertices[i];
        const uint32_t c = _chart_for_normal(v.nx, v.ny, v.nz);
        chart_of_v[i] = c;
        float s = 0.0f, t = 0.0f;
        _proj_for_chart(c, v.px, v.py, v.pz, s, t);
        if (!b[c].init) {
            b[c].min_s = b[c].max_s = s;
            b[c].min_t = b[c].max_t = t;
            b[c].init = true;
        } else {
            b[c].min_s = std::min(b[c].min_s, s);
            b[c].max_s = std::max(b[c].max_s, s);
            b[c].min_t = std::min(b[c].min_t, t);
            b[c].max_t = std::max(b[c].max_t, t);
        }
    }

    // Second pass: normalize per chart into [0,1].
    for (size_t i = 0; i < io_mesh.vertices.size(); ++i) {
        auto& v = io_mesh.vertices[i];
        const uint32_t c = chart_of_v[i];
        float s = 0.0f, t = 0.0f;
        _proj_for_chart(c, v.px, v.py, v.pz, s, t);
        const float ds = (b[c].max_s - b[c].min_s);
        const float dt = (b[c].max_t - b[c].min_t);
        const float inv_ds = (ds > 1e-20f) ? (1.0f / ds) : 0.0f;
        const float inv_dt = (dt > 1e-20f) ? (1.0f / dt) : 0.0f;
        float u = (s - b[c].min_s) * inv_ds;
        float w = (t - b[c].min_t) * inv_dt;
        // clamp for safety
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
        if (w < 0.0f) w = 0.0f;
        if (w > 1.0f) w = 1.0f;
        v.u = u;
        v.v = w;
    }
    return true;
}

} // namespace genesis
