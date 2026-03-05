#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "GE_runtime.hpp"

// This test validates the bounded, deterministic overflow behavior of
// substrate actuation scheduling:
//   primary slots -> sidecars -> container (math-only) -> latent deferral.
//
// The CPU is only used to enqueue deterministic pulses; the substrate evolution
// is still responsible for bounded scheduling.

static Pulse make_pulse(uint32_t anchor_id_u32, int32_t f_code, uint16_t a_code, uint16_t v_code, uint16_t i_code, uint64_t tick_u64) {
    Pulse p{};
    p.anchor_id = anchor_id_u32;
    p.f_code = f_code;
    p.a_code = a_code;
    p.v_code = v_code;
    p.i_code = i_code;
    p.profile_id = (uint8_t)EW_PROFILE_CORE_EVOLUTION;
    p.causal_tag = 1u;
    p.pad0 = 0u;
    p.pad1 = 0u;
    p.tick = tick_u64;
    return p;
}

int main() {
    SubstrateManager s(64);
    const uint32_t spec = s.spectral_field_anchor_id_u32;
    if (spec == 0u || spec >= s.anchors.size() || s.anchors[spec].kind_u32 != EW_ANCHOR_KIND_SPECTRAL_FIELD) {
        std::cerr << "FAIL: missing spectral anchor\n";
        return 1;
    }

    // Enqueue far more pulses than primary+sidecar capacity in one tick.
    // With current caps: primary=8, sidecar total=8, latent ring=8.
    // We enqueue 30 drive pulses; expected on first tick:
    //   - 16 scheduled this tick
    //   - 8 deferred (latent)
    //   - 6 dropped
    const uint64_t t0 = s.canonical_tick + 1u;
    for (int i = 0; i < 30; ++i) {
        // Vary f_code so k-mapping spreads deterministically.
        const int32_t f = 1000 + i;
        s.enqueue_inbound_pulse(make_pulse(spec, f, 1000u, 30000u, 30000u, t0));
    }

    s.tick();

    const EwSpectralFieldAnchorState& ss0 = s.anchors[spec].spectral_field_state;

    const uint32_t primary = ss0.actuation_count_u32;
    const uint32_t s0 = ss0.actuation_sidecar_count_u32[0];
    const uint32_t s1 = ss0.actuation_sidecar_count_u32[1];
    const uint32_t latent = ss0.actuation_latent_count_u32;

    if (primary != 8u || s0 != 4u || s1 != 4u) {
        std::cerr << "FAIL: scheduled counts unexpected primary=" << primary
                  << " sidecar0=" << s0 << " sidecar1=" << s1 << "\n";
        return 1;
    }
    if (latent != 8u) {
        std::cerr << "FAIL: latent count unexpected latent=" << latent << "\n";
        return 1;
    }

    // On the next tick (no new pulses), latent replay should move up to 4 into slots.
    s.tick();
    const EwSpectralFieldAnchorState& ss1 = s.anchors[spec].spectral_field_state;
    if (ss1.act_used_latent_replay_u16 != 4u) {
        std::cerr << "FAIL: latent replay count unexpected got=" << ss1.act_used_latent_replay_u16 << "\n";
        return 1;
    }
    if (ss1.actuation_latent_count_u32 != 4u) {
        std::cerr << "FAIL: latent remaining unexpected got=" << ss1.actuation_latent_count_u32 << "\n";
        return 1;
    }

    std::cout << "PASS: actuation overflow is bounded and deterministic\n";
    return 0;
}
