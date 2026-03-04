#include "GE_voxel_coupling.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "fixed_point.hpp"

static inline uint16_t clamp_q15_u16(uint32_t v) {
    return (v > 32767u) ? 32767u : (uint16_t)v;
}

static inline uint8_t band_from_abs_q32_32(int64_t x_q32_32) {
    uint64_t ax = (x_q32_32 < 0) ? (uint64_t)(-x_q32_32) : (uint64_t)x_q32_32;
    // Map magnitude to 0..7 using top bits (very cheap log2 proxy).
    // If ax is 0 -> band 0.
    if (ax == 0u) return 0u;
    uint8_t b = 0u;
    while (ax > (1ull << 33) && b < 7u) { ax >>= 1; ++b; }
    return b;
}

struct VoxelPick {
    uint16_t rho_q15;
    uint16_t coupling_q15;
    uint16_t idx_u16;
};

static inline uint32_t vox_index_u32(int32_t x, int32_t y, int32_t z, int32_t dim) {
    return (uint32_t)(x + dim * (y + dim * z));
}

static inline void vox_unindex_u32(uint32_t idx, int32_t dim, int32_t& x, int32_t& y, int32_t& z) {
    z = (int32_t)(idx / (uint32_t)(dim * dim));
    const int32_t rem = (int32_t)(idx - (uint32_t)z * (uint32_t)(dim * dim));
    y = rem / dim;
    x = rem - y * dim;
}

static void build_solid_and_distance_fields(EwVoxelCouplingAnchorState& vs) {
    const int32_t dim = (int32_t)EW_VOXEL_COUPLING_DIM;
    const uint32_t vox_n = EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM;

    // 1) Solid mask from density threshold.
    // Threshold is conservative: treat only high-density as solid so the field isn't "sticky".
    const uint16_t solid_thresh_q15 = 8192u; // 0.25
    for (uint32_t i = 0; i < vox_n; ++i) {
        vs.solid_vox_u8[i] = (vs.rho_vox_q15[i] >= solid_thresh_q15) ? 1u : 0u;
        vs.wall_dist_vox_u8[i] = 255u;
        vs.boundary_strength_vox_q15[i] = 0u;
    }

    // 2) Multi-source BFS for Manhattan distance to nearest solid.
    // Fixed-size queue keeps determinism and avoids heap.
    uint16_t q[vox_n];
    uint16_t qh = 0, qt = 0;
    for (uint32_t i = 0; i < vox_n; ++i) {
        if (vs.solid_vox_u8[i]) {
            vs.wall_dist_vox_u8[i] = 0u;
            q[qt++] = (uint16_t)i;
        }
    }

    const int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    const int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    const int32_t dz[6] = {0, 0, 0, 0, 1, -1};

    while (qh < qt) {
        const uint32_t cur = (uint32_t)q[qh++];
        int32_t x, y, z;
        vox_unindex_u32(cur, dim, x, y, z);
        const uint8_t cd = vs.wall_dist_vox_u8[cur];
        if (cd == 255u) continue;
        if (cd == 254u) continue; // saturated

        for (int ni = 0; ni < 6; ++ni) {
            const int32_t nx = x + dx[ni];
            const int32_t ny = y + dy[ni];
            const int32_t nz = z + dz[ni];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= dim || ny >= dim || nz >= dim) continue;
            const uint32_t nidx = vox_index_u32(nx, ny, nz, dim);
            const uint8_t nd = vs.wall_dist_vox_u8[nidx];
            const uint8_t cand = (uint8_t)(cd + 1u);
            if (cand < nd) {
                vs.wall_dist_vox_u8[nidx] = cand;
                q[qt++] = (uint16_t)nidx;
            }
        }
    }

    // 3) Boundary strength proxy:
    // For fluid voxels near solid, boundary_strength rises. For solid voxels it is max.
    // Also mix in coupling map (collision boundary proxy).
    uint64_t sum_bs = 0;
    uint64_t sum_wd = 0;
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < vox_n; ++i) {
        const uint8_t d = vs.wall_dist_vox_u8[i];
        // Map d=0..8 into strength ~1..0 linearly (clamped).
        uint16_t base = 0u;
        if (d == 0u) base = 32767u;
        else if (d < 9u) base = (uint16_t)((9u - (uint32_t)d) * 3640u); // ~32760 at d=0
        else base = 0u;

        // Couple with existing boundary coupling map.
        const uint32_t mixed = ((uint32_t)base * (uint32_t)vs.coupling_vox_q15[i]) / 32767u;
        vs.boundary_strength_vox_q15[i] = (uint16_t)mixed;

        sum_bs += (uint64_t)vs.boundary_strength_vox_q15[i];
        sum_wd += (uint64_t)d;
        ++cnt;
    }
    if (cnt == 0u) cnt = 1u;
    vs.boundary_strength_mean_q15 = (uint16_t)std::min<uint64_t>(32767ull, sum_bs / (uint64_t)cnt);
    // Mean wall distance in Q0.15 with a conservative scaling (distance 0..255 mapped into 0..1).
    vs.wall_dist_mean_q15 = (uint16_t)std::min<uint64_t>(32767ull, (sum_wd * 128ull) / (uint64_t)cnt);
}


static void build_boundary_classification_fields(EwVoxelCouplingAnchorState& vs,
                                                const uint32_t* friction_acc_u32,
                                                const uint32_t* rest_acc_u32,
                                                const uint16_t* count_u16) {
    const int32_t dim = (int32_t)EW_VOXEL_COUPLING_DIM;
    const uint32_t vox_n = EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM;

    // Reset fields.
    std::memset(vs.boundary_normal_u8, 0, sizeof(vs.boundary_normal_u8));
    std::memset(vs.interface_strength_vox_q15, 0, sizeof(vs.interface_strength_vox_q15));
    std::memset(vs.no_slip_u8, 0, sizeof(vs.no_slip_u8));
    std::memset(vs.permeability_vox_q15, 0, sizeof(vs.permeability_vox_q15));

    uint64_t sum_perm = 0;
    uint32_t perm_cnt = 0;

    uint64_t sum_iface = 0;
    uint32_t iface_cnt = 0;

    // Axis-weighted interface accumulation for anisotropy summary.
    uint64_t axis_w[3] = {0, 0, 0};

    // Determine boundary normals via 6-neighbor stencil on the solid mask.
    // The goal is a cheap, bounded "interface classification" suitable as a coupling factor.
    const int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    const int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    const int32_t dz[6] = {0, 0, 0, 0, 1, -1};

    for (uint32_t idx = 0; idx < vox_n; ++idx) {
        int32_t x, y, z;
        vox_unindex_u32(idx, dim, x, y, z);

        const uint8_t s = vs.solid_vox_u8[idx];

        // Compute cheap interface gradient from 6-neighbor solid stencil.
        // gx = s(x+1)-s(x-1), etc. Out-of-bounds treated as same as current.
        int gx = 0, gy = 0, gz = 0;
        {
            const int32_t xp = (x + 1 < dim) ? (x + 1) : x;
            const int32_t xm = (x - 1 >= 0) ? (x - 1) : x;
            const uint8_t sp = vs.solid_vox_u8[vox_index_u32(xp, y, z, dim)];
            const uint8_t sm = vs.solid_vox_u8[vox_index_u32(xm, y, z, dim)];
            gx = (int)sp - (int)sm;
        }
        {
            const int32_t yp = (y + 1 < dim) ? (y + 1) : y;
            const int32_t ym = (y - 1 >= 0) ? (y - 1) : y;
            const uint8_t sp = vs.solid_vox_u8[vox_index_u32(x, yp, z, dim)];
            const uint8_t sm = vs.solid_vox_u8[vox_index_u32(x, ym, z, dim)];
            gy = (int)sp - (int)sm;
        }
        {
            const int32_t zp = (z + 1 < dim) ? (z + 1) : z;
            const int32_t zm = (z - 1 >= 0) ? (z - 1) : z;
            const uint8_t sp = vs.solid_vox_u8[vox_index_u32(x, y, zp, dim)];
            const uint8_t sm = vs.solid_vox_u8[vox_index_u32(x, y, zm, dim)];
            gz = (int)sp - (int)sm;
        }

        const uint32_t ax = (uint32_t)(gx < 0 ? -gx : gx);
        const uint32_t ay = (uint32_t)(gy < 0 ? -gy : gy);
        const uint32_t az = (uint32_t)(gz < 0 ? -gz : gz);

        // Determine if this voxel lies on an interface: any neighbor differs.
        uint8_t is_boundary = 0;
        // Deterministic scan in fixed neighbor order.
        for (int ni = 0; ni < 6; ++ni) {
            const int32_t nx = x + dx[ni];
            const int32_t ny = y + dy[ni];
            const int32_t nz = z + dz[ni];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= dim || ny >= dim || nz >= dim) continue;
            const uint32_t nidx = vox_index_u32(nx, ny, nz, dim);
            if (vs.solid_vox_u8[nidx] != s) { is_boundary = 1; break; }
        }

        // Choose dominant axis by max gradient magnitude; deterministic ties X > Y > Z.
        uint8_t axis = 0;
        int g = gx;
        uint32_t amag = ax;
        if (ay > amag) { axis = 1; g = gy; amag = ay; }
        if (az > amag) { axis = 2; g = gz; amag = az; }

        // Encode sign: 0 for +axis, 1 for -axis.
        const uint8_t sign = (g < 0) ? 1u : 0u;
        uint8_t normal_code = 0u;
        if (is_boundary) {
            normal_code = (uint8_t)((axis & 0x3u) | ((sign & 0x1u) << 2) | (1u << 3));
        }
        vs.boundary_normal_u8[idx] = normal_code;

        // Interface strength Q0.15 from total gradient magnitude (sum_abs in {0..6}).
        // Scale so sum_abs=6 ~ 1.0.
        const uint32_t sum_abs = ax + ay + az;
        const uint32_t strength = (sum_abs * 5461u); // 32766/6
        vs.interface_strength_vox_q15[idx] = (uint16_t)std::min<uint32_t>(32767u, strength);
        sum_iface += (uint64_t)vs.interface_strength_vox_q15[idx];
        iface_cnt += 1u;

        if (is_boundary) {
            axis_w[axis] += (uint64_t)vs.interface_strength_vox_q15[idx];
        }


        // Compute per-voxel friction/restitution means if present (Q0.15).
        const uint16_t c = count_u16[idx];
        uint16_t fr_mean = 0;
        uint16_t re_mean = 0;
        if (c != 0u) {
            fr_mean = (uint16_t)std::min<uint32_t>(32767u, friction_acc_u32[idx] / (uint32_t)c);
            re_mean = (uint16_t)std::min<uint32_t>(32767u, rest_acc_u32[idx] / (uint32_t)c);
        }

        // Map collision coefficients to permeability and no-slip:
        // permeability ~ (1 - friction) * restitution
        const uint32_t inv_fr = (uint32_t)(32767u - fr_mean);
        const uint32_t perm = (inv_fr * (uint32_t)re_mean) / 32767u;

        // Near boundaries, reduce permeability further based on boundary strength.
        const uint32_t bs = vs.boundary_strength_vox_q15[idx];
        const uint32_t perm2 = (perm * (uint32_t)(32767u - (uint16_t)std::min<uint32_t>(bs, 32767u))) / 32767u;

        vs.permeability_vox_q15[idx] = (uint16_t)std::min<uint32_t>(32767u, perm2);
        sum_perm += (uint64_t)vs.permeability_vox_q15[idx];
        perm_cnt += 1u;

        // No-slip if boundary voxel and either high friction or strong boundary coupling.
        const uint8_t high_friction = (fr_mean >= 16384u) ? 1u : 0u;
        const uint8_t strong_boundary = (bs >= 16384u) ? 1u : 0u;
        vs.no_slip_u8[idx] = (uint8_t)((is_boundary && (high_friction || strong_boundary)) ? 1u : 0u);
    }

    if (perm_cnt == 0u) perm_cnt = 1u;
    vs.permeability_mean_q15 = (uint16_t)std::min<uint64_t>(32767ull, sum_perm / (uint64_t)perm_cnt);

    if (iface_cnt == 0u) iface_cnt = 1u;
    vs.interface_strength_mean_q15 = (uint16_t)std::min<uint64_t>(32767ull, sum_iface / (uint64_t)iface_cnt);

    // Compute dominant axis and anisotropy strength.
    // anisotropy ~ (max - mean) / (mean + eps), clamped to [0..1].
    const uint64_t sx = axis_w[0];
    const uint64_t sy = axis_w[1];
    const uint64_t sz = axis_w[2];
    uint64_t sum_axes = sx + sy + sz;
    if (sum_axes == 0ull) sum_axes = 1ull;
    uint8_t dom = 0;
    uint64_t mx = sx;
    if (sy > mx) { dom = 1; mx = sy; }
    if (sz > mx) { dom = 2; mx = sz; }
    vs.boundary_axis_dom_u8 = dom;

    // mean = sum/3. Numerator = (max*3 - sum).
    int64_t num = (int64_t)(mx * 3ull) - (int64_t)sum_axes;
    if (num < 0) num = 0;
    // denom = sum (acts like 3*mean). aniso_q15 = num/denom in Q0.15.
    uint32_t an_q15 = (uint32_t)std::min<uint64_t>(32767ull, ((uint64_t)num * 32767ull) / sum_axes);
    vs.boundary_anisotropy_q15 = (uint16_t)an_q15;
}

void ew_voxel_coupling_step(uint64_t canonical_tick_u64,
                           std::vector<Anchor>& anchors,
                           std::vector<EwInfluxPublishPacket>& out_influx) {
    // Optional: a dedicated solver-facing collision environment inbox.
    // Deterministic selection: first collision-env anchor by vector order.
    EwCollisionEnvAnchorState* col_env = nullptr;
    for (Anchor& a : anchors) {
        if (a.kind_u32 != EW_ANCHOR_KIND_COLLISION_ENV) continue;
        col_env = &a.collision_env_state;
        col_env->clear_for_tick(canonical_tick_u64);
        break;
    }

    // Deterministic scan in anchor vector order.
    for (Anchor& a : anchors) {
        if (a.kind_u32 != EW_ANCHOR_KIND_VOXEL_COUPLING) continue;
        EwVoxelCouplingAnchorState& vs = a.voxel_coupling_state;

        // Initialize on first use.
        if (vs.spawn_seed_u64 == 0u) {
            // Derive a deterministic seed from anchor id.
            vs.spawn_seed_u64 = 0xC0A1BEEFull ^ (uint64_t)a.id;
            vs.particle_count_u32 = 0;
            std::memset(vs.rho_vox_q15, 0, sizeof(vs.rho_vox_q15));
            std::memset(vs.coupling_vox_q15, 0, sizeof(vs.coupling_vox_q15));
            std::memset(vs.solid_vox_u8, 0, sizeof(vs.solid_vox_u8));
            std::memset(vs.wall_dist_vox_u8, 0, sizeof(vs.wall_dist_vox_u8));
            std::memset(vs.boundary_strength_vox_q15, 0, sizeof(vs.boundary_strength_vox_q15));
        std::memset(vs.interface_strength_vox_q15, 0, sizeof(vs.interface_strength_vox_q15));
            std::memset(vs.interface_strength_vox_q15, 0, sizeof(vs.interface_strength_vox_q15));
            std::memset(vs.particles, 0, sizeof(vs.particles));
            std::memset(vs.boundary_normal_u8, 0, sizeof(vs.boundary_normal_u8));
            std::memset(vs.no_slip_u8, 0, sizeof(vs.no_slip_u8));
            std::memset(vs.permeability_vox_q15, 0, sizeof(vs.permeability_vox_q15));
            std::memset(vs.collision_constraints, 0, sizeof(vs.collision_constraints));
            vs.collision_constraints_head_u32 = 0;
            vs.collision_constraints_count_u32 = 0;
        }

        // Clear voxel maps each tick (authoritative but deterministic).
        std::memset(vs.rho_vox_q15, 0, sizeof(vs.rho_vox_q15));
        std::memset(vs.coupling_vox_q15, 0, sizeof(vs.coupling_vox_q15));
        std::memset(vs.solid_vox_u8, 0, sizeof(vs.solid_vox_u8));
        std::memset(vs.wall_dist_vox_u8, 0, sizeof(vs.wall_dist_vox_u8));
        std::memset(vs.boundary_strength_vox_q15, 0, sizeof(vs.boundary_strength_vox_q15));
        std::memset(vs.interface_strength_vox_q15, 0, sizeof(vs.interface_strength_vox_q15));
        vs.boundary_strength_mean_q15 = 0;
        vs.wall_dist_mean_q15 = 0;
        vs.interface_strength_mean_q15 = 0;
        std::memset(vs.boundary_normal_u8, 0, sizeof(vs.boundary_normal_u8));
        std::memset(vs.no_slip_u8, 0, sizeof(vs.no_slip_u8));
        std::memset(vs.permeability_vox_q15, 0, sizeof(vs.permeability_vox_q15));
        // Reset constraint ring (derived-only) deterministically.
        std::memset(vs.collision_constraints, 0, sizeof(vs.collision_constraints));
        vs.collision_constraints_head_u32 = 0;
        vs.collision_constraints_count_u32 = 0;

        const int32_t dim = (int32_t)EW_VOXEL_COUPLING_DIM;
        const int32_t vsz = vs.voxel_size_m_q16_16;
        const int32_t ox = vs.origin_q16_16[0];
        const int32_t oy = vs.origin_q16_16[1];
        const int32_t oz = vs.origin_q16_16[2];

        const uint32_t vox_n = EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM;

        // Per-voxel accumulators for collision coefficients (for permeability/no-slip mapping).
        // These are local (not stored) and rebuilt deterministically every tick.
        uint32_t friction_acc_u32[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];
        uint32_t rest_acc_u32[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];
        uint16_t count_u16[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];
        std::memset(friction_acc_u32, 0, sizeof(friction_acc_u32));
        std::memset(rest_acc_u32, 0, sizeof(rest_acc_u32));
        std::memset(count_u16, 0, sizeof(count_u16));

        // Accumulate density/coupling from object anchors inside voxel block.
        for (const Anchor& obj : anchors) {
            if (obj.kind_u32 != EW_ANCHOR_KIND_OBJECT) continue;
            const int32_t px = obj.object_state.pos_q16_16[0];
            const int32_t py = obj.object_state.pos_q16_16[1];
            const int32_t pz = obj.object_state.pos_q16_16[2];

            const int32_t lx = px - ox;
            const int32_t ly = py - oy;
            const int32_t lz = pz - oz;
            if (lx < 0 || ly < 0 || lz < 0) continue;
            const int32_t xi = lx / vsz;
            const int32_t yi = ly / vsz;
            const int32_t zi = lz / vsz;
            if (xi < 0 || yi < 0 || zi < 0 || xi >= dim || yi >= dim || zi >= dim) continue;

            const uint32_t idx = (uint32_t)(xi + dim * (yi + dim * zi));

            // Density proxy: world_flux_grad_mean_q15 (0..32767)
            const uint16_t rho = obj.world_flux_grad_mean_q15;
            const uint16_t coup = obj.collision_env_restitution_q15; // boundary coupling proxy

            const uint32_t rho_acc = (uint32_t)vs.rho_vox_q15[idx] + (uint32_t)rho;
            vs.rho_vox_q15[idx] = clamp_q15_u16(rho_acc);

            const uint32_t c_acc = (uint32_t)vs.coupling_vox_q15[idx] + (uint32_t)coup;
            vs.coupling_vox_q15[idx] = clamp_q15_u16(c_acc);

            // Collision coefficient accumulators (for boundary condition mapping).
            friction_acc_u32[idx] += (uint32_t)obj.collision_env_friction_q15;
            rest_acc_u32[idx] += (uint32_t)obj.collision_env_restitution_q15;
            if (count_u16[idx] != 0xFFFFu) { count_u16[idx] += 1u; }
        }

        // Build boundary coupling fields (solid mask + wall distance + boundary strength).
        build_solid_and_distance_fields(vs);
        build_boundary_classification_fields(vs, friction_acc_u32, rest_acc_u32, count_u16);

        // Emit collision/environment constraint packets for objects in this voxel block.
        // Deterministic order: anchors vector order.
        uint64_t col_fric_sum_u64 = 0u;
        uint64_t col_rest_sum_u64 = 0u;
        uint32_t col_n_u32 = 0u;
        for (const Anchor& obj : anchors) {
            if (obj.kind_u32 != EW_ANCHOR_KIND_OBJECT) continue;

            const int32_t px = obj.object_state.pos_q16_16[0];
            const int32_t py = obj.object_state.pos_q16_16[1];
            const int32_t pz = obj.object_state.pos_q16_16[2];

            const int32_t lx = px - ox;
            const int32_t ly = py - oy;
            const int32_t lz = pz - oz;
            if (lx < 0 || ly < 0 || lz < 0) continue;
            const int32_t xi = lx / vsz;
            const int32_t yi = ly / vsz;
            const int32_t zi = lz / vsz;
            if (xi < 0 || yi < 0 || zi < 0 || xi >= dim || yi >= dim || zi >= dim) continue;

            const uint32_t vidx = (uint32_t)(xi + dim * (yi + dim * zi));

            EwCollisionConstraintPacket pkt;
            std::memset(&pkt, 0, sizeof(pkt));
            pkt.object_id_u64 = (uint64_t)obj.object_id_u64;
            pkt.friction_q15 = obj.collision_env_friction_q15;
            pkt.restitution_q15 = obj.collision_env_restitution_q15;
            pkt.boundary_strength_q15 = vs.boundary_strength_vox_q15[vidx];
            pkt.no_slip_u8 = vs.no_slip_u8[vidx];
            pkt.permeability_q15 = vs.permeability_vox_q15[vidx];

            const uint32_t head = vs.collision_constraints_head_u32 % EwVoxelCouplingAnchorState::EW_COLLISION_CONSTRAINT_RING_MAX;
            vs.collision_constraints[head] = pkt;
            vs.collision_constraints_head_u32 = (head + 1u) % EwVoxelCouplingAnchorState::EW_COLLISION_CONSTRAINT_RING_MAX;
            if (vs.collision_constraints_count_u32 < EwVoxelCouplingAnchorState::EW_COLLISION_CONSTRAINT_RING_MAX) {
                vs.collision_constraints_count_u32 += 1u;
            }

            // Also publish into the dedicated collision environment anchor inbox, if present.
            if (col_env) {
                col_env->push_packet(pkt);
                col_fric_sum_u64 += (uint64_t)pkt.friction_q15;
                col_rest_sum_u64 += (uint64_t)pkt.restitution_q15;
                col_n_u32 += 1u;
            }
        }

        if (col_env && col_n_u32) {
            col_env->friction_mean_q15 = (uint16_t)std::min<uint64_t>(32767ull, col_fric_sum_u64 / (uint64_t)col_n_u32);
            col_env->restitution_mean_q15 = (uint16_t)std::min<uint64_t>(32767ull, col_rest_sum_u64 / (uint64_t)col_n_u32);
        }

        // Pick top voxels by density to spawn coupling particles.
        VoxelPick picks[EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM * EW_VOXEL_COUPLING_DIM];
        for (uint32_t i = 0; i < vox_n; ++i) {
            picks[i].rho_q15 = vs.rho_vox_q15[i];
            picks[i].coupling_q15 = vs.coupling_vox_q15[i];
            picks[i].idx_u16 = (uint16_t)i;
        }

        std::stable_sort(picks, picks + vox_n, [](const VoxelPick& a, const VoxelPick& b) {
            if (a.rho_q15 != b.rho_q15) return a.rho_q15 > b.rho_q15; // high density first
            return a.idx_u16 < b.idx_u16; // stable tie-break
        });

        const uint32_t n_spawn = std::min<uint32_t>(vs.max_particles_u32, EW_VOXEL_COUPLING_PARTICLES_MAX);
        uint32_t out_count = 0;

        for (uint32_t pi = 0; pi < vox_n && out_count < n_spawn; ++pi) {
            if (picks[pi].rho_q15 == 0) break;
            const uint32_t idx = (uint32_t)picks[pi].idx_u16;
            int32_t xi, yi, zi;
            vox_unindex_u32(idx, dim, xi, yi, zi);

            EwVoxelCouplingParticle& p = vs.particles[out_count];
            // Voxel center.
            p.pos_q16_16[0] = ox + xi * vsz + (vsz / 2);
            p.pos_q16_16[1] = oy + yi * vsz + (vsz / 2);
            p.pos_q16_16[2] = oz + zi * vsz + (vsz / 2);

            // Density/coupling.
            p.rho_q15 = picks[pi].rho_q15;
            // Upgrade coupling to include voxel boundary strength contribution.
            // This is the "coupling factor from voxel collision boundaries" contract.
            const uint16_t bs = vs.boundary_strength_vox_q15[idx];
            const uint32_t coup_up = (uint32_t)picks[pi].coupling_q15 + ((uint32_t)bs >> 2); // +0.25*bs
            p.coupling_q15 = clamp_q15_u16(coup_up);

            // Compton-rate proxy: rho * coupling scaled into Q32.32 turns/tick.
            const uint64_t rc = (uint64_t)p.rho_q15 * (uint64_t)p.coupling_q15; // up to ~1e9
            p.compton_rate_turns_q32_32 = (int64_t)((rc << 32) / (uint64_t)(32767u * 32767u));

            // Local wall distance influences normalization: nearer walls increase coupling speed.
            const uint8_t wd = vs.wall_dist_vox_u8[idx];
            const uint32_t wd_boost_q15 = (wd < 9u) ? (uint32_t)((9u - (uint32_t)wd) * 3640u) : 0u;
            p.compton_rate_turns_q32_32 += (int64_t)((uint64_t)wd_boost_q15 << 17); // small additive turns

            // Doppler normalization uses the anchor's derived doppler as a carrier (turn domain).
            // We store it in Q32.32 turns for deterministic coupling.
            p.doppler_norm_turns_q32_32 = (int64_t)a.doppler_q; // already TURN_SCALE domain in core; treated as proxy.

            // Velocities remain zero in this contract-first pass (solver will drive later).
            p.vel_q16_16[0] = 0;
            p.vel_q16_16[1] = 0;
            p.vel_q16_16[2] = 0;

            ++out_count;
        }

        // Zero unused particles deterministically.
        for (uint32_t i = out_count; i < EW_VOXEL_COUPLING_PARTICLES_MAX; ++i) {
            std::memset(&vs.particles[i], 0, sizeof(EwVoxelCouplingParticle));
        }
        vs.particle_count_u32 = out_count;

        // Compute influx: sum(rho*coup) - expectation proxy.
        uint64_t sum_rc = 0;
        uint64_t sum_r = 0;
        for (uint32_t i = 0; i < out_count; ++i) {
            sum_rc += (uint64_t)vs.particles[i].rho_q15 * (uint64_t)vs.particles[i].coupling_q15;
            sum_r += (uint64_t)vs.particles[i].rho_q15;
        }
        // Expectation: half the raw density scaled into same units.
        const uint64_t exp_rc = (sum_r * 32767ull) / 2ull;
        int64_t influx_raw = (int64_t)sum_rc - (int64_t)exp_rc;
        // Normalize to Q32.32 by dividing by (32767^2).
        vs.influx_q32_32 = (int64_t)((influx_raw << 32) / (int64_t)(32767ll * 32767ll));
        vs.influx_band_u8 = band_from_abs_q32_32(vs.influx_q32_32);

        // Publish only if magnitude exceeds a tiny threshold.
        const int64_t abs_influx = (vs.influx_q32_32 < 0) ? -vs.influx_q32_32 : vs.influx_q32_32;
        const int64_t thresh = (1ll << 20); // ~1e-4 in Q32.32
        vs.influx_pending_u8 = (abs_influx > thresh) ? 1u : 0u;

        // Learning coupling proxy (Q15) from abs influx.
        // Map Q32.32 magnitude down into Q15 with a conservative scale.
        const uint64_t abs_u = (abs_influx < 0) ? 0u : (uint64_t)abs_influx;
        const uint32_t lc = (uint32_t)(abs_u >> 17); // Q32.32 -> approx Q15
        vs.learning_coupling_q15 = clamp_q15_u16(lc);

        // Deterministic hash of motivating slice.
        uint64_t h = 1469598103934665603ull;
        h ^= (uint64_t)(a.id); h *= 1099511628211ull;
        h ^= (uint64_t)(vs.particle_count_u32); h *= 1099511628211ull;
        h ^= (uint64_t)(vs.influx_band_u8); h *= 1099511628211ull;
        h ^= (uint64_t)(vs.learning_coupling_q15); h *= 1099511628211ull;
        vs.influx_hash_u64 = h;

        if (vs.influx_pending_u8) {
            EwInfluxPublishPacket p;
            p.src_anchor_id_u32 = a.id;
            p.coherence_band_u8 = vs.influx_band_u8;
            p.suggested_action_u8 = (uint8_t)EwCoherenceSuggestedAction::AdjustLearning;
            p.influx_q32_32 = vs.influx_q32_32;
            p.payload_hash_u64 = vs.influx_hash_u64;
            p.v_code_u16 = a.last_v_code;
            p.i_code_u16 = a.last_i_code;
            out_influx.push_back(p);
        }

        vs.last_tick_u64 = canonical_tick_u64;
    }
}
