#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "GE_runtime.hpp"
#include "ew_actuation_op_pack.hpp"

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

static void push_math_hook(
    SubstrateManager& s,
    uint32_t dst_anchor_id_u32,
    uint8_t op_tag_u8,
    int32_t a0_q16_16,
    int32_t a1_q16_16,
    int32_t a2_q16_16,
    uint16_t drive_k_u16
) {
    if (dst_anchor_id_u32 >= s.anchors.size()) return;
    Anchor& a = s.anchors[dst_anchor_id_u32];
    if (a.kind_u32 != EW_ANCHOR_KIND_SPECTRAL_FIELD) return;
    EwSpectralFieldAnchorState& ss = a.spectral_field_state;
    if (ss.hook_inbox_count_u32 >= EW_SPECTRAL_HOOK_INBOX_MAX) return;

    EwHookPacket hp{};
    hp.dst_anchor_id_u32 = dst_anchor_id_u32;
    hp.hook_op_u8 = (uint8_t)EwCoherenceHookOp::HookEmitActuationOp;
    hp.causal_tag_u8 = op_tag_u8;
    hp.authority_q15 = 32767u;

    const uint64_t p0 = genesis::ew_pack_hook_emit_actuation_p0_u64(a0_q16_16, a1_q16_16);
    const uint64_t p1 = genesis::ew_pack_hook_emit_actuation_p1_u64(a2_q16_16, drive_k_u16, 0u);
    std::memcpy(&hp.p0_q32_32, &p0, sizeof(p0));
    std::memcpy(&hp.p1_q32_32, &p1, sizeof(p1));

    ss.hook_inbox[ss.hook_inbox_count_u32++] = hp;
}

int main() {
    SubstrateManager s(64);
    const uint32_t spec = s.spectral_field_anchor_id_u32;
    if (spec == 0u || spec >= s.anchors.size() || s.anchors[spec].kind_u32 != EW_ANCHOR_KIND_SPECTRAL_FIELD) {
        std::cerr << "FAIL: missing spectral anchor\n";
        return 1;
    }

    // ------------------------------------------------------------------
    // 1) Prime latent ring by overflowing drive pulses once.
    // ------------------------------------------------------------------
    {
        const uint64_t t0 = s.canonical_tick + 1u;
        for (int i = 0; i < 30; ++i) {
            const int32_t f = 2000 + i;
            s.enqueue_inbound_pulse(make_pulse(spec, f, 1000u, 30000u, 30000u, t0));
        }
        s.tick();

        const EwSpectralFieldAnchorState& ss = s.anchors[spec].spectral_field_state;
        if (ss.actuation_latent_count_u32 != EW_SPECTRAL_LATENT_RING_N) {
            std::cerr << "FAIL: expected latent ring to fill, got=" << ss.actuation_latent_count_u32 << "\n";
            return 1;
        }
    }

    // ------------------------------------------------------------------
    // 2) Prove containerization is used for math hooks when primary/sidecar
    //    space is reduced by latent replay.
    // ------------------------------------------------------------------
    {
        // Next tick: latent replay consumes 4 slots first.
        // Then we inject 16 math hooks, which should overflow by 4, and those
        // should be packed into container slots (2 containers total).
        const int32_t one_q16_16 = (1 << 16);
        const int32_t two_q16_16 = (2 << 16);
        const int32_t three_q16_16 = (3 << 16);
        for (int i = 0; i < (int)EW_SPECTRAL_HOOK_INBOX_MAX; ++i) {
            const uint8_t tag = (i & 1) ? (uint8_t)EW_ACT_OP_ADD : (uint8_t)EW_ACT_OP_MUL;
            push_math_hook(s, spec, tag, one_q16_16, two_q16_16, three_q16_16, 0xFFFFu);
        }

        s.tick();

        const EwSpectralFieldAnchorState& ss = s.anchors[spec].spectral_field_state;
        if (ss.act_used_latent_replay_u16 != (uint16_t)EW_SPECTRAL_LATENT_REPLAY_MAX) {
            std::cerr << "FAIL: expected latent replay 4, got=" << ss.act_used_latent_replay_u16 << "\n";
            return 1;
        }
        if (ss.act_used_container_u16 != 2u || ss.actuation_container_count_u32 != 2u) {
            std::cerr << "FAIL: expected container usage 2, got used=" << ss.act_used_container_u16
                      << " count=" << ss.actuation_container_count_u32 << "\n";
            return 1;
        }
        if (ss.act_overflow_u16 != 0u) {
            std::cerr << "FAIL: containerization should prevent latent overflow for hooks, got ov=" << ss.act_overflow_u16 << "\n";
            return 1;
        }
    }

    // ------------------------------------------------------------------
    // 3) Stress: saturate scheduling for multiple ticks and validate
    //    bounded behavior + stable drop accounting.
    // ------------------------------------------------------------------
    {
        // After steady state: latent is full at tick start, replay moves 4,
        // leaving 12 primary/sidecar slots for new pulses; 30 pulses => 18 overflow.
        // Latent has room for 4 => 14 dropped.
        for (int t = 0; t < 6; ++t) {
            const uint64_t tick = s.canonical_tick + 1u;
            for (int i = 0; i < 30; ++i) {
                const int32_t f = 3000 + (t * 128) + i;
                s.enqueue_inbound_pulse(make_pulse(spec, f, 1000u, 30000u, 30000u, tick));
            }
            s.tick();

            const EwSpectralFieldAnchorState& ss = s.anchors[spec].spectral_field_state;
            if (ss.actuation_count_u32 != EW_SPECTRAL_TRAJ_SLOTS ||
                ss.actuation_sidecar_count_u32[0] != EW_SPECTRAL_ACT_SIDECAR_SLOTS ||
                ss.actuation_sidecar_count_u32[1] != EW_SPECTRAL_ACT_SIDECAR_SLOTS) {
                std::cerr << "FAIL: scheduled slots not saturated at t=" << t
                          << " p=" << ss.actuation_count_u32
                          << " s0=" << ss.actuation_sidecar_count_u32[0]
                          << " s1=" << ss.actuation_sidecar_count_u32[1] << "\n";
                return 1;
            }
            if (ss.actuation_latent_count_u32 != EW_SPECTRAL_LATENT_RING_N) {
                std::cerr << "FAIL: latent ring not full at t=" << t << " got=" << ss.actuation_latent_count_u32 << "\n";
                return 1;
            }
            if (ss.act_used_latent_replay_u16 != (uint16_t)EW_SPECTRAL_LATENT_REPLAY_MAX) {
                std::cerr << "FAIL: latent replay not max at t=" << t << " got=" << ss.act_used_latent_replay_u16 << "\n";
                return 1;
            }
            // t==0 is a warm tick where latent was not necessarily full before replay.
            if (t >= 1) {
                if (ss.act_dropped_u16 != 14u) {
                    std::cerr << "FAIL: dropped count mismatch at t=" << t << " got=" << ss.act_dropped_u16 << " expect=14\n";
                    return 1;
                }
            }
        }
    }

    std::cout << "PASS: actuation scheduling remains bounded under sustained load\n";
    return 0;
}
