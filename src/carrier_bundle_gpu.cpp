#include "carrier_bundle_gpu.hpp"

#include "anchor.hpp"
#include "ew_gpu_compute.hpp"

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

#if defined(EW_ENABLE_GPU_COMPUTE) && (EW_ENABLE_GPU_COMPUTE==1)
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

    return ew_gpu_compute_carrier_triples(
        doppler_q.data(),
        m_q.data(),
        curvature_q.data(),
        fluxg_q15.data(),
        hm.data(),
        n,
        anchor_ids.data(),
        (uint32_t)anchor_ids.size(),
        out_triples.data());
#else
    (void)anchors;
    (void)anchor_ids;
    (void)out_triples;
    return false;
#endif
}
