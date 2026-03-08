#include <cstdint>
#include <iostream>
#include <cstdlib>

#include "GE_runtime.hpp"
#include "anchor.hpp"
#include "ew_id9.hpp"

static uint32_t find_first_spectral_anchor(SubstrateManager& sm) {
    for (uint32_t i = 0u; i < (uint32_t)sm.anchors.size(); ++i) {
        if (sm.anchors[i].kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) return i;
    }
    return 0u;
}

static void force_high_leakage_publish(Anchor& a, uint64_t seed_u64) {
    EwSpectralFieldAnchorState& ss = a.spectral_field_state;
    ss.leakage_pending_u8 = 1u;
    ss.leakage_band_u8 = 7u;
    ss.leakage_q32_32 = (int64_t)(512LL << 32); // extreme
    ss.leakage_id9 = ew_id9_from_u64(seed_u64);
}

int main() {
    SubstrateManager sm(64);
    sm.projection_seed = 0xBEEFCAFEULL;

    const uint32_t sid = find_first_spectral_anchor(sm);
    if (sid == 0u) {
        std::cerr << "FAIL: no spectral anchor\n";
        return 1;
    }

    Anchor& a = sm.anchors[sid];
    if (a.kind_u32 != EW_ANCHOR_KIND_SPECTRAL_FIELD) {
        std::cerr << "FAIL: spectral anchor kind mismatch\n";
        return 1;
    }

    // Ensure there is non-zero state so a resync is observable.
    a.spectral_field_state.phi_hat[0].re_q32_32 = (int64_t)(123LL << 32);

    // Tick 0: publish leakage, coherence bus will deliver HookStartCalibration.
    force_high_leakage_publish(a, 0x1111ULL);
    sm.tick();

    // Tick 1: hook should be applied at tick start, enabling calibration.
    force_high_leakage_publish(a, 0x2222ULL);
    sm.tick();

    {
        const EwSpectralFieldAnchorState& ss = sm.anchors[sid].spectral_field_state;
        if (ss.calibration_mode_u8 != 1u) {
            std::cerr << "FAIL: calibration not active after hook apply\n";
            return 1;
        }
        // The bus requests 64 ticks; the anchor decrements by 1 within the tick.
        if (ss.calibration_ticks_remaining_u32 != 63u) {
            std::cerr << "FAIL: unexpected ticks_remaining=" << ss.calibration_ticks_remaining_u32 << "\n";
            return 1;
        }
        if (ss.fanout_budget_u32 > 16u) {
            std::cerr << "FAIL: fanout budget not throttled during calibration (" << ss.fanout_budget_u32 << ")\n";
            return 1;
        }
    }

    // Force retriggers until escalation should occur (threshold=3).
    // We publish leakage each tick so the coherence bus keeps sending HookStartCalibration.
    for (int i = 0; i < 4; ++i) {
        // Make sure phi_hat is non-zero before the expected escalation tick.
        if (i == 2) {
            sm.anchors[sid].spectral_field_state.phi_hat[0].re_q32_32 = (int64_t)(999LL << 32);
        }
        force_high_leakage_publish(sm.anchors[sid], 0x3333ULL + (uint64_t)i);
        sm.tick();
    }

    {
        const EwSpectralFieldAnchorState& ss = sm.anchors[sid].spectral_field_state;
        if (ss.calibration_ticks_remaining_u32 == 0u) {
            std::cerr << "FAIL: expected calibration throttle to remain active\n";
            return 1;
        }
        // Resync must have zeroed phi_hat[0].
        if (ss.phi_hat[0].re_q32_32 != 0) {
            std::cerr << "FAIL: expected phi_hat[0] reset on resync escalation\n";
            return 1;
        }
    }

    std::cout << "PASS: calibration trigger + retrigger escalation + cooldown gating\n";
    return 0;
}
