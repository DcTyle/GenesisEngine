#include "GE_state_fingerprint.hpp"

#include "GE_runtime.hpp"
#include <cstdio>
#include <cstdlib>

static inline uint64_t mix_u64(uint64_t x) {
    // SplitMix64-style mixing (deterministic, fast). This is for fingerprints,
    // not security.
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x = x ^ (x >> 31);
    return x;
}

static inline void fold(uint64_t& h, uint64_t v) {
    h ^= mix_u64(v + 0xD6E8FEB86659FD93ull);
    h = (h << 17) | (h >> (64 - 17));
    h *= 0x9E3779B185EBCA87ull;
}

uint64_t ge_compute_state_fingerprint_9d(const SubstrateManager* sm) {
    if (!sm) return 0;
    uint64_t h = 0xC3A5C85C97CB3127ull;

    // Canonical tick.
    fold(h, sm->canonical_tick);

    // Anchor count and a few key observables.
    fold(h, (uint64_t)sm->anchors.size());
    const size_t n = sm->anchors.size();
    // Sample up to 32 anchors deterministically (first, middle, last).
    const size_t sample_n = (n < 32) ? n : 32;
    for (size_t i = 0; i < sample_n; ++i) {
        const size_t idx = (n <= sample_n) ? i : (i * (n - 1) / (sample_n - 1));
        const Anchor& a = sm->anchors[idx];
        fold(h, ((uint64_t)a.id) | ((uint64_t)a.kind_u32 << 32));
        fold(h, (uint64_t)a.object_id_u64);
        fold(h, (uint64_t)a.theta_q);
        fold(h, (uint64_t)a.tau_turns_q);
        fold(h, (uint64_t)a.world_flux_grad_mean_q15);
        fold(h, (uint64_t)a.collision_env_friction_q15);
        fold(h, (uint64_t)a.collision_env_restitution_q15);
        fold(h, (uint64_t)a.harmonics_mean_q15);
        if (a.kind_u32 == EW_ANCHOR_KIND_CAMERA) {
            fold(h, (uint64_t)a.camera_state.focus_distance_m_q32_32);
            fold(h, (uint64_t)a.camera_state.focus_mode_u8);
            fold(h, (uint64_t)(uint16_t)a.camera_state.audio_env_field_q1_15);
            fold(h, (uint64_t)(uint16_t)a.camera_state.audio_env_grad_q1_15);
            fold(h, (uint64_t)a.camera_state.audio_env_coherence_q15);
            fold(h, (uint64_t)a.camera_state.color_band_u8);
            fold(h, (uint64_t)a.camera_state.color_r_u8);
            fold(h, (uint64_t)a.camera_state.color_g_u8);
            fold(h, (uint64_t)a.camera_state.color_b_u8);
            fold(h, (uint64_t)a.camera_state.audio_eq_preset_u8);
            fold(h, (uint64_t)a.camera_state.audio_reverb_preset_u8);
            fold(h, (uint64_t)a.camera_state.audio_occlusion_preset_u8);
        }
        if (a.kind_u32 == EW_ANCHOR_KIND_OBJECT) {
            fold(h, (uint64_t)(uint32_t)a.object_state.pos_q16_16[0]);
            fold(h, (uint64_t)(uint32_t)a.object_state.pos_q16_16[1]);
            fold(h, (uint64_t)(uint32_t)a.object_state.pos_q16_16[2]);
        }
        if (a.kind_u32 == EW_ANCHOR_KIND_PLANET) {
            fold(h, (uint64_t)(uint32_t)a.planet_state.pos_q16_16[0]);
            fold(h, (uint64_t)(uint32_t)a.planet_state.pos_q16_16[1]);
            fold(h, (uint64_t)(uint32_t)a.planet_state.pos_q16_16[2]);
            fold(h, (uint64_t)a.planet_state.voxel_resonance_q15);
        }
        if (a.kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) {
            const EwSpectralFieldAnchorState& ss = a.spectral_field_state;
            fold(h, (uint64_t)(uint32_t)ss.region_center_q16_16[0]);
            fold(h, (uint64_t)(uint32_t)ss.region_center_q16_16[1]);
            fold(h, (uint64_t)(uint32_t)ss.region_center_q16_16[2]);
            fold(h, (uint64_t)(uint32_t)ss.region_radius_m_q16_16);
            fold(h, (uint64_t)ss.dt_scale_q32_32);
            fold(h, (uint64_t)ss.viscosity_bias_q32_32);
            fold(h, (uint64_t)ss.noise_floor_q15);
            fold(h, (uint64_t)ss.min_delta_q15);
            fold(h, (uint64_t)ss.fanout_budget_u32);
            fold(h, (uint64_t)ss.learning_coupling_q15);
            fold(h, (uint64_t)ss.op_gain_q15);
            fold(h, (uint64_t)ss.op_phase_bias_q15);
            for (uint32_t k = 0u; k < 8u; ++k) fold(h, (uint64_t)ss.op_band_w_q15[k]);
            fold(h, (uint64_t)ss.boundary_strength_mean_q15);
            fold(h, (uint64_t)ss.permeability_mean_q15);
            fold(h, (uint64_t)ss.boundary_axis_dom_u8);
            fold(h, (uint64_t)ss.boundary_anisotropy_q15);
            fold(h, (uint64_t)ss.calibration_mode_u8);
            fold(h, (uint64_t)ss.calibration_ticks_remaining_u32);
            fold(h, (uint64_t)ss.energy_mean_q15);
            fold(h, (uint64_t)ss.energy_peak_q15);
            fold(h, (uint64_t)ss.leakage_abs_q15);
            fold(h, (uint64_t)ss.leakage_q32_32);
            fold(h, (uint64_t)ss.leakage_hash_u64);
            // Temporal coupling measurement lanes.
            fold(h, (uint64_t)ss.temporal_residual.intent_hash_u64);
            fold(h, (uint64_t)ss.temporal_residual.measured_hash_u64);
            fold(h, (uint64_t)ss.temporal_residual.residual_q32_32);
            fold(h, (uint64_t)ss.temporal_residual.residual_norm_q15);
            fold(h, (uint64_t)ss.intent_summary.intent_norm_q15);
            for (uint32_t kk = 0u; kk < 8u; ++kk) fold(h, (uint64_t)ss.intent_summary.band_mag_q15[kk]);
        }
        if (a.kind_u32 == EW_ANCHOR_KIND_COHERENCE_BUS) {
            const EwCoherenceBusAnchorState& bs = a.coherence_bus_state;
            fold(h, bs.router_seed_u64);
            fold(h, bs.last_tick_u64);
            fold(h, (uint64_t)bs.phys_coherence_q15);
            fold(h, (uint64_t)bs.learning_coherence_q15);
            fold(h, (uint64_t)bs.temporal_coherence_q15);
            fold(h, (uint64_t)bs.hook_out_count_u32);
            // Sample a few ring entries deterministically.
            for (uint32_t b = 0u; b < EW_COHERENCE_BANDS; ++b) {
                const EwCoherenceBusBandRing& r = bs.band[b];
                fold(h, (uint64_t)r.count_u32);
                if (r.count_u32 != 0u) {
                    const uint32_t idx = (r.head_u32 + (r.count_u32 - 1u)) % EW_COHERENCE_RING_PER_BAND;
                    const EwLeakagePublishPacket& lp = r.ring[idx];
                    fold(h, (uint64_t)lp.payload_hash_u64);
                    fold(h, (uint64_t)lp.leakage_q32_32);
                }

                const EwCoherenceBusInfluxBandRing& ir = bs.influx_band[b];
                fold(h, (uint64_t)ir.count_u32);
                if (ir.count_u32 != 0u) {
                    const uint32_t jdx = (ir.head_u32 + (ir.count_u32 - 1u)) % EW_COHERENCE_RING_PER_BAND;
                    const EwInfluxPublishPacket& ip = ir.ring[jdx];
                    fold(h, (uint64_t)ip.payload_hash_u64);
                    fold(h, (uint64_t)ip.influx_q32_32);
                }
            }
        }
        if (a.kind_u32 == EW_ANCHOR_KIND_VOXEL_COUPLING) {
            const EwVoxelCouplingAnchorState& vs = a.voxel_coupling_state;
            fold(h, vs.spawn_seed_u64);
            fold(h, vs.last_tick_u64);
            fold(h, (uint64_t)vs.particle_count_u32);
            fold(h, (uint64_t)vs.influx_band_u8);
            fold(h, (uint64_t)vs.influx_q32_32);
            fold(h, (uint64_t)vs.influx_hash_u64);
            fold(h, (uint64_t)vs.learning_coupling_q15);
            fold(h, (uint64_t)vs.boundary_strength_mean_q15);
            fold(h, (uint64_t)vs.wall_dist_mean_q15);
            fold(h, (uint64_t)vs.permeability_mean_q15);
            fold(h, (uint64_t)vs.interface_strength_mean_q15);
            fold(h, (uint64_t)vs.boundary_axis_dom_u8);
            fold(h, (uint64_t)vs.boundary_anisotropy_q15);
            fold(h, (uint64_t)vs.collision_constraints_count_u32);
            if (vs.collision_constraints_count_u32 != 0u) {
                const uint32_t last = (vs.collision_constraints_head_u32 + EwVoxelCouplingAnchorState::EW_COLLISION_CONSTRAINT_RING_MAX - 1u) % EwVoxelCouplingAnchorState::EW_COLLISION_CONSTRAINT_RING_MAX;
                const EwCollisionConstraintPacket& cp = vs.collision_constraints[last];
                fold(h, (uint64_t)cp.object_id_u64);
                fold(h, (uint64_t)cp.boundary_strength_q15);
                fold(h, (uint64_t)cp.permeability_q15);
                fold(h, (uint64_t)cp.no_slip_u8);
            }
        }

        if (a.kind_u32 == EW_ANCHOR_KIND_COLLISION_ENV) {
            const EwCollisionEnvAnchorState& cs = a.collision_env_state;
            fold(h, cs.last_tick_u64);
            fold(h, (uint64_t)cs.count_u32);
            fold(h, (uint64_t)cs.friction_mean_q15);
            fold(h, (uint64_t)cs.restitution_mean_q15);
            if (cs.count_u32 != 0u) {
                const uint32_t last = (cs.head_u32 + EwCollisionEnvAnchorState::EW_COLLISION_CONSTRAINT_RING_MAX - 1u) % EwCollisionEnvAnchorState::EW_COLLISION_CONSTRAINT_RING_MAX;
                const EwCollisionConstraintPacket& cp = cs.collision_constraints[last];
                fold(h, (uint64_t)cp.object_id_u64);
                fold(h, (uint64_t)cp.boundary_strength_q15);
                fold(h, (uint64_t)cp.permeability_q15);
                fold(h, (uint64_t)cp.no_slip_u8);
            }
        }
    }

    // Control inbox size (control surface activity).
    fold(h, (uint64_t)sm->control_inbox_count_u32);

    // Render packet ticks.
    fold(h, sm->render_camera_packet_tick_u64);
    fold(h, sm->render_assist_packet_tick_u64);
    fold(h, sm->render_object_packets_tick_u64);

    return h;
}

bool ge_load_fingerprint_reference(const char* path_utf8, std::vector<uint64_t>& out) {
    out.clear();
    if (!path_utf8 || !path_utf8[0]) return false;
    FILE* f = std::fopen(path_utf8, "rb");
    if (!f) return false;

    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        char* endp = nullptr;
        // Accept hex with 0x prefix or decimal.
        uint64_t v = std::strtoull(line, &endp, 0);
        if (endp == line) continue;
        out.push_back(v);
    }
    std::fclose(f);
    return !out.empty();
}
