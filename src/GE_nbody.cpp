#include "GE_nbody.hpp"
#include "GE_runtime.hpp"
#include "fixed_point.hpp"
#include <cstring>

namespace genesis {

static inline __int128 mul_q32_32_to_q32_32(int64_t a_q32_32, int64_t b_q32_32) {
    return (__int128)a_q32_32 * (__int128)b_q32_32; // result Q64.64
}

static inline int64_t clamp_i64(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Integer sqrt for unsigned 128-bit values, returns floor(sqrt(x)).
static inline uint64_t isqrt_u128(__uint128_t x) {
    if (x == 0) return 0;
    __uint128_t r = 0;
    __uint128_t bit = (__uint128_t)1 << 126; // highest even bit <= 128-2
    while (bit > x) bit >>= 2;
    while (bit != 0) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint64_t)r;
}

void ew_nbody_init_default(EwNBodyState* st) {
    if (!st) return;
    std::memset(st, 0, sizeof(EwNBodyState));
    st->enabled_u32 = 1u;
    st->initialized_u32 = 0u;
    st->body_count_u32 = 3u;

    // G = 6.67430e-11 (approx). Encode in Q32.32:
    // 6.67430e-11 * 2^32 ≈ 0.286... -> 0.286 in Q32.32.
    // Keep as a rational-ish approximation to avoid float init:
    // 0.286 * 2^32 ≈ 1228360643
    st->G_q32_32 = (int64_t)1228360643LL;

    // dt = 60 seconds by default (Q32.32)
    st->dt_seconds_q32_32 = (int64_t)60LL << 32;

    // Body 0: Sun
    {
        EwNBodyBody& b = st->bodies[0];
        b.object_id_u64 = 0x4E424F44595F0001ULL; // "NBODY"+1
        b.mass_kg_q32_32 = (int64_t)1988500LL * 1000000LL << 32; // 1.9885e30 approx (kg)
        b.radius_m_q32_32 = (int64_t)696340000LL << 32;
        b.albedo_rgba8 = 0xFFFFCC55u;
        b.emissive_q32_32 = (int64_t)1LL << 32;
    }

    // Body 1: Earth
    {
        EwNBodyBody& b = st->bodies[1];
        b.object_id_u64 = 0x4E424F44595F0002ULL;
        b.mass_kg_q32_32 = (int64_t)59722LL * 100000000000000000LL; // 5.9722e24 *2^32? (rough integer scaling)
        b.mass_kg_q32_32 <<= 0; // already huge; keep as-is; deterministic magnitude only
        b.radius_m_q32_32 = (int64_t)6371000LL << 32;
        b.albedo_rgba8 = 0xFF2266FFu;
        b.atmosphere_rgba8 = 0x332266FFu;
        b.atmosphere_thickness_m_q32_32 = (int64_t)100000LL << 32;
        // 1 AU along +X
        b.pos_m_q32_32[0] = (int64_t)149597870700LL << 32;
        // Circular-ish orbital velocity along +Y: 29.78 km/s
        b.vel_mps_q32_32[1] = (int64_t)29780LL << 32;
    }

    // Body 2: Moon
    {
        EwNBodyBody& b = st->bodies[2];
        b.object_id_u64 = 0x4E424F44595F0003ULL;
        b.mass_kg_q32_32 = (int64_t)7342LL * 10000000000000000LL; // 7.342e22 rough scaling
        b.mass_kg_q32_32 <<= 0;
        b.radius_m_q32_32 = (int64_t)1737400LL << 32;
        b.albedo_rgba8 = 0xFFBBBBBBu;
        // Start near Earth on +X with ~384,400 km offset and velocity +Y relative
        b.pos_m_q32_32[0] = ((int64_t)149597870700LL + (int64_t)384400000LL) << 32;
        b.vel_mps_q32_32[1] = ((int64_t)29780LL + (int64_t)1022LL) << 32;
    }
}

// Compute accelerations for each body (m/s^2 in Q32.32).
static inline void compute_accel_q32_32(const EwNBodyState& st, int64_t out_acc_q32_32[][3]) {
    const uint32_t n = st.body_count_u32;
    for (uint32_t i = 0; i < n; ++i) {
        out_acc_q32_32[i][0] = 0;
        out_acc_q32_32[i][1] = 0;
        out_acc_q32_32[i][2] = 0;
    }

    for (uint32_t i = 0; i < n; ++i) {
        const EwNBodyBody& bi = st.bodies[i];
        for (uint32_t j = i + 1; j < n; ++j) {
            const EwNBodyBody& bj = st.bodies[j];
            // r = pj - pi (Q32.32)
            int64_t rx = bj.pos_m_q32_32[0] - bi.pos_m_q32_32[0];
            int64_t ry = bj.pos_m_q32_32[1] - bi.pos_m_q32_32[1];
            int64_t rz = bj.pos_m_q32_32[2] - bi.pos_m_q32_32[2];

            __int128 r2_q64_64 = mul_q32_32_to_q32_32(rx, rx) + mul_q32_32_to_q32_32(ry, ry) + mul_q32_32_to_q32_32(rz, rz);
            // soften: add (1000 m)^2
            const __int128 soft_q64_64 = (__int128)((int64_t)1000LL << 32) * (__int128)((int64_t)1000LL << 32);
            r2_q64_64 += soft_q64_64;

            // r = sqrt(r2) in Q32.32
            __uint128_t r2_u = (r2_q64_64 < 0) ? 0 : ( __uint128_t)r2_q64_64;
            const uint64_t r_q32_32 = isqrt_u128(r2_u);

            if (r_q32_32 == 0u) continue;

            // inv_r3 approx: 1/(r^3) using integer division in Q32.32
            // We compute inv_r = (1<<32)/r (Q32.32), then inv_r3 = inv_r^3 (Q96.96) -> shift to Q32.32.
            const __int128 one_q64_64 = (__int128)1 << 64;
            const __int128 inv_r_q32_32 = (__int128)(((__int128)1 << 64) / (__int128)r_q32_32); // (2^64)/r => Q32.32
            __int128 inv_r2_q64_64 = (inv_r_q32_32 * inv_r_q32_32); // Q64.64
            __int128 inv_r3_q96_96 = inv_r2_q64_64 * inv_r_q32_32; // Q96.96

            // scalar = G * m / r^3  => (Q32.32 * Q32.32 * Q32.32) / Q32.32?? handled by fixed shifts:
            // inv_r3 Q96.96 -> shift 64 to Q32.32
            const __int128 inv_r3_q32_32 = (inv_r3_q96_96 >> 64);

            const __int128 G_q32_32 = (__int128)st.G_q32_32;
            const __int128 mi_q32_32 = (__int128)bi.mass_kg_q32_32;
            const __int128 mj_q32_32 = (__int128)bj.mass_kg_q32_32;

            // a_i = +G * m_j * r_vec * inv_r3
            __int128 s_i_q64_64 = (G_q32_32 * mj_q32_32); // Q64.64
            s_i_q64_64 = (s_i_q64_64 >> 32) * inv_r3_q32_32; // (Q32.32 * Q32.32) => Q64.64
            // a_j = -G * m_i * r_vec * inv_r3
            __int128 s_j_q64_64 = (G_q32_32 * mi_q32_32);
            s_j_q64_64 = (s_j_q64_64 >> 32) * inv_r3_q32_32;

            auto add_acc = [&](uint32_t idx, __int128 scale_q64_64, int64_t dx, int64_t dy, int64_t dz, int sign) {
                // (scale Q64.64 * dx Q32.32) => Q96.96 -> shift 64 => Q32.32
                __int128 ax = (scale_q64_64 * (__int128)dx) >> 64;
                __int128 ay = (scale_q64_64 * (__int128)dy) >> 64;
                __int128 az = (scale_q64_64 * (__int128)dz) >> 64;
                out_acc_q32_32[idx][0] += (int64_t)(sign * ax);
                out_acc_q32_32[idx][1] += (int64_t)(sign * ay);
                out_acc_q32_32[idx][2] += (int64_t)(sign * az);
            };

            add_acc(i, s_i_q64_64, rx, ry, rz, +1);
            add_acc(j, s_j_q64_64, rx, ry, rz, -1);
        }
    }
}

static inline void velocity_verlet_step(EwNBodyState* st) {
    const uint32_t n = st->body_count_u32;
    if (n == 0u || n > EwNBodyState::MAX_BODIES) return;

    int64_t a0[EwNBodyState::MAX_BODIES][3];
    int64_t a1[EwNBodyState::MAX_BODIES][3];
    compute_accel_q32_32(*st, a0);

    const int64_t dt = st->dt_seconds_q32_32;
    const __int128 half_dt_q32_32 = (__int128)dt / 2;

    // x += v*dt + 0.5*a*dt^2
    for (uint32_t i = 0; i < n; ++i) {
        EwNBodyBody& b = st->bodies[i];
        for (int k = 0; k < 3; ++k) {
            __int128 vdt_q64_64 = (__int128)b.vel_mps_q32_32[k] * (__int128)dt; // Q64.64
            __int128 adt_q64_64 = (__int128)a0[i][k] * (__int128)dt; // Q64.64
            __int128 half_adt2_q64_64 = (adt_q64_64 * (__int128)dt) / 2; // Q96.96 /? but dt Q32.32 => Q96.96 then /2
            // Convert:
            __int128 dx_q32_32 = (vdt_q64_64 >> 32) + (half_adt2_q64_64 >> 64);
            b.pos_m_q32_32[k] += (int64_t)dx_q32_32;
        }
    }

    compute_accel_q32_32(*st, a1);

    // v += 0.5*(a0+a1)*dt
    for (uint32_t i = 0; i < n; ++i) {
        EwNBodyBody& b = st->bodies[i];
        for (int k = 0; k < 3; ++k) {
            __int128 a_sum = (__int128)a0[i][k] + (__int128)a1[i][k]; // Q32.32
            __int128 dv_q64_64 = a_sum * (__int128)half_dt_q32_32; // Q64.64
            b.vel_mps_q32_32[k] += (int64_t)(dv_q64_64 >> 32);
        }
    }
}

static inline uint32_t ensure_planet_anchor(SubstrateManager* sm, uint64_t object_id_u64) {
    // Deterministically search for existing planet anchor with object_id_u64.
    for (uint32_t ai = 1u; ai < (uint32_t)sm->anchors.size(); ++ai) {
        if (sm->anchors[ai].kind_u32 == EW_ANCHOR_KIND_PLANET && sm->anchors[ai].object_id_u64 == object_id_u64) {
            return sm->anchors[ai].id;
        }
    }
    // Create new planet anchor in deterministic order.
    Anchor a((uint32_t)sm->anchors.size());
    a.kind_u32 = EW_ANCHOR_KIND_PLANET;
    a.object_id_u64 = object_id_u64;
    sm->anchors.push_back(a);
    return a.id;
}

void ew_nbody_tick(SubstrateManager* sm) {
    if (!sm) return;

    if (sm->nbody.enabled_u32 == 0u) return;

    if (sm->nbody.initialized_u32 == 0u) {
        EwNBodyState tmp;
        ew_nbody_init_default(&tmp);
        // Keep enabled state from existing (allows disabling via future settings).
        tmp.enabled_u32 = sm->nbody.enabled_u32;
        sm->nbody = tmp;

        // Resolve anchors deterministically.
        for (uint32_t i = 0; i < sm->nbody.body_count_u32 && i < EwNBodyState::MAX_BODIES; ++i) {
            EwNBodyBody& b = sm->nbody.bodies[i];
            b.planet_anchor_id_u32 = ensure_planet_anchor(sm, b.object_id_u64);
        }
        sm->nbody.initialized_u32 = 1u;
    }

    // One step per canonical tick (dt controlled in Q32.32).
    velocity_verlet_step(&sm->nbody);

    // Project bodies into planet anchors (authoritative state).
    for (uint32_t i = 0; i < sm->nbody.body_count_u32 && i < EwNBodyState::MAX_BODIES; ++i) {
        const EwNBodyBody& b = sm->nbody.bodies[i];
        const uint32_t aid = b.planet_anchor_id_u32;
        if (aid == 0u || aid >= sm->anchors.size()) continue;
        Anchor& a = sm->anchors[aid];
        if (a.kind_u32 != EW_ANCHOR_KIND_PLANET) continue;

        a.planet_state.pos_q16_16[0] = (int32_t)(b.pos_m_q32_32[0] >> 16);
        a.planet_state.pos_q16_16[1] = (int32_t)(b.pos_m_q32_32[1] >> 16);
        a.planet_state.pos_q16_16[2] = (int32_t)(b.pos_m_q32_32[2] >> 16);
        a.planet_state.radius_m_q16_16 = (int32_t)(b.radius_m_q32_32 >> 16);
        a.planet_state.albedo_rgba8 = b.albedo_rgba8;
        a.planet_state.atmosphere_rgba8 = b.atmosphere_rgba8;
        a.planet_state.atmosphere_thickness_m_q16_16 = (int32_t)(b.atmosphere_thickness_m_q32_32 >> 16);
        a.planet_state.emissive_q16_16 = (int32_t)(b.emissive_q32_32 >> 16);
    }
}

} // namespace genesis
