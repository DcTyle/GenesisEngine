#include "carrier_bundle_cuda.hpp"
#include "anchor.hpp"
#include "fixed_point.hpp"

static inline int32_t ew_clamp_i32_local(int64_t v, int32_t lo, int32_t hi) {
    if (v < (int64_t)lo) return lo;
    if (v > (int64_t)hi) return hi;
    return (int32_t)v;
}

static inline int32_t ew_doppler_k_q16_16_from_turns_i64(int64_t doppler_turns_q) {
    const int64_t absd = (doppler_turns_q < 0) ? -doppler_turns_q : doppler_turns_q;
    const int64_t denom = (int64_t)TURN_SCALE + absd;
    if (denom <= 0) return 0;
    const int64_t num = (doppler_turns_q << 16);
    return ew_clamp_i32_local(num / denom, -(int32_t)65536, (int32_t)65536);
}

static inline int32_t ew_leak_density_q16_16_from_mass_i64(int64_t mass_turns_q) {
    int64_t d = (int64_t)TURN_SCALE - mass_turns_q;
    if (d < 0) d = 0;
    const int64_t num = (d << 16);
    return ew_clamp_i32_local(num / (int64_t)TURN_SCALE, 0, (int32_t)65536);
}

// Flux-gradient magnitude is sampled from the world lattice during
// object↔world boundary exchange and stored on the anchor as Q0.15.
static inline uint16_t ew_flux_grad_q0_15_from_anchor_u16(uint16_t q15) {
    return (q15 > 32768u) ? 32768u : q15;
}

#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
extern "C" bool ew_cuda_compute_carrier_triples_impl(
    const int64_t* doppler_q_turns,
    const int64_t* m_q_turns,
    const int64_t* curvature_q_turns,
    const uint16_t* flux_grad_mean_q15,
    const uint16_t* harmonics_mean_q15,
    uint32_t anchors_n,
    const uint32_t* anchor_ids,
    uint32_t ids_n,
    EwCarrierTriple* out_triples
);
#endif

bool ew_compute_carrier_triples_for_anchor_ids(
    const std::vector<Anchor>& anchors,
    const std::vector<uint32_t>& anchor_ids,
    std::vector<EwCarrierTriple>& out_triples
) {
    out_triples.clear();
    out_triples.resize(anchor_ids.size());

    const uint32_t n = (uint32_t)anchors.size();
    if (anchor_ids.empty()) return true;
    if (n == 0u) return true;

#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
    // SoA build for deterministic device execution.
    std::vector<int64_t> doppler_q(n);
    std::vector<int64_t> m_q(n);
    std::vector<int64_t> curvature_q(n);
    std::vector<uint16_t> fluxg_q15(n);
    std::vector<uint16_t> hm(n);
    for (uint32_t i = 0; i < n; ++i) {
        doppler_q[i] = anchors[i].doppler_q;
        m_q[i] = anchors[i].m_q;
        curvature_q[i] = anchors[i].curvature_q;
        fluxg_q15[i] = anchors[i].world_flux_grad_mean_q15;
        hm[i] = anchors[i].harmonics_mean_q15;
    }

    return ew_cuda_compute_carrier_triples_impl(
        doppler_q.data(), m_q.data(), curvature_q.data(), fluxg_q15.data(), hm.data(), n,
        anchor_ids.data(), (uint32_t)anchor_ids.size(),
        out_triples.data()
    );
#else
    // CPU build (no CUDA). Same integer math, deterministic.
    for (size_t i = 0; i < anchor_ids.size(); ++i) {
        const uint32_t a0 = anchor_ids[i];
        if (a0 >= n) {
            out_triples[i] = EwCarrierTriple{0u, 0u, 0u};
            continue;
        }
        const uint32_t a1 = (a0 + 1u) % n;
        const uint32_t a2 = (a0 + 2u) % n;
        const Anchor& A0 = anchors[a0];
        const Anchor& A1 = anchors[a1];
        const Anchor& A2 = anchors[a2];

        const int32_t d0 = ew_doppler_k_q16_16_from_turns_i64(A0.doppler_q);
        const int32_t d1 = ew_doppler_k_q16_16_from_turns_i64(A1.doppler_q);
        const int32_t d2 = ew_doppler_k_q16_16_from_turns_i64(A2.doppler_q);
        const int64_t ds = (int64_t)d0 + (int64_t)d1 + (int64_t)d2;
        const int32_t doppler_bundled = ew_clamp_i32_local(ds / 3, -(int32_t)65536, (int32_t)65536);

        const int32_t l0 = ew_leak_density_q16_16_from_mass_i64(A0.m_q);
        const int32_t l1 = ew_leak_density_q16_16_from_mass_i64(A1.m_q);
        const int32_t l2 = ew_leak_density_q16_16_from_mass_i64(A2.m_q);
        const int64_t ls = (int64_t)l0 + (int64_t)l1 + (int64_t)l2;
        const int32_t leak_bundled = ew_clamp_i32_local(ls / 3, 0, (int32_t)65536);

        uint32_t h0 = (uint32_t)A0.harmonics_mean_q15;
        uint32_t h1 = (uint32_t)A1.harmonics_mean_q15;
        uint32_t h2 = (uint32_t)A2.harmonics_mean_q15;
        uint32_t h = ((h0 + h1 + h2) / 3u) & 65535u;

        // Flux-gradient magnitude sampled from world boundary exchange.
        const uint32_t g0 = (uint32_t)ew_flux_grad_q0_15_from_anchor_u16(A0.world_flux_grad_mean_q15);
        const uint32_t g1 = (uint32_t)ew_flux_grad_q0_15_from_anchor_u16(A1.world_flux_grad_mean_q15);
        const uint32_t g2 = (uint32_t)ew_flux_grad_q0_15_from_anchor_u16(A2.world_flux_grad_mean_q15);
        const uint32_t g = ((g0 + g1 + g2) / 3u) & 65535u;

        // Pack: high16=flux_grad Q0.15, low16=harm_mean Q0.15.
        const uint32_t z = ((g & 65535u) << 16) | (h & 65535u);

        out_triples[i] = EwCarrierTriple{(uint32_t)leak_bundled, (uint32_t)doppler_bundled, z};
    }
    return true;
#endif
}
