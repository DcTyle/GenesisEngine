#include "GE_collision_env.hpp"

#include "GE_runtime.hpp"

namespace {

static inline uint16_t clamp_q15_i32(int32_t v) {
    if (v < 0) return 0;
    if (v > 32767) return 32767;
    return (uint16_t)v;
}

// Simple deterministic mapping from flux/coherence -> friction/restitution.
// This is intentionally conservative and monotonic.
static inline void map_env(uint16_t flux_q15, uint16_t phys_q15, uint16_t& out_mu_q15, uint16_t& out_e_q15) {
    // friction increases with gradient and coherence (more "stiff" environment)
    // restitution decreases with gradient (more damping) but increases slightly
    // with coherence (less numerical jitter).
    const int32_t f = (int32_t)flux_q15;
    const int32_t p = (int32_t)phys_q15;

    // mu = 0.1 + 0.6*(f) + 0.3*(p)  (all in q15)
    // constants: 0.1=3276, 0.6=19660, 0.3=9830
    int32_t mu = 3276 + ((f * 19660) >> 15) + ((p * 9830) >> 15);

    // e = 0.8 - 0.5*(f) + 0.1*(p)
    // 0.8=26214, -0.5=-16384, 0.1=3276
    int32_t e = 26214 - ((f * 16384) >> 15) + ((p * 3276) >> 15);

    out_mu_q15 = clamp_q15_i32(mu);
    out_e_q15 = clamp_q15_i32(e);
}

} // namespace

void ew_collision_env_step(EwState& cand, const EwCtx& ctx) {
    (void)ctx;
    // Find global phys coherence (from coherence bus if present).
    uint16_t phys_q15 = 0;
    for (size_t i = 0; i < cand.anchors.size(); ++i) {
        const Anchor& a = cand.anchors[i];
        if (a.kind_u32 == EW_ANCHOR_KIND_COHERENCE_BUS) {
            phys_q15 = a.coherence_bus_state.phys_coherence_q15;
            break;
        }
    }

    // Deterministic pass over anchors: compute env coefficients for objects.
    for (size_t i = 0; i < cand.anchors.size(); ++i) {
        Anchor& a = cand.anchors[i];
        if (a.kind_u32 != EW_ANCHOR_KIND_OBJECT) continue;

        uint16_t mu_q15 = 0, e_q15 = 0;
        map_env(a.world_flux_grad_mean_q15, phys_q15, mu_q15, e_q15);
        a.collision_env_friction_q15 = mu_q15;
        a.collision_env_restitution_q15 = e_q15;
    }
}
