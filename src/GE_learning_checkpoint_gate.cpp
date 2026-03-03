#include "GE_learning_checkpoint_gate.hpp"

#include "GE_runtime.hpp"
#include "fixed_point.hpp"
#include "spec_gates.hpp"
#include "learning_gate_cuda.hpp"

#include <algorithm>

namespace genesis {

LearningCheckpointGate::LearningCheckpointGate() = default;

uint64_t LearningCheckpointGate::xorshift64(uint64_t& s) {
    // Deterministic PRNG (not for security), stable across platforms.
    uint64_t x = s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s = x;
    return x;
}

static inline int64_t abs_i64(int64_t x) { return (x < 0) ? -x : x; }

int64_t LearningCheckpointGate::rel_err_q32_32(
    const MetricVector& sim,
    const MetricVector& target,
    uint32_t /*tol_num*/, uint32_t /*tol_den*/
) {
    // Returns max relative error across dims (Q32.32).
    const uint32_t dim = (sim.dim_u32 < target.dim_u32) ? sim.dim_u32 : target.dim_u32;
    int64_t worst = 0;
    for (uint32_t i = 0; i < dim; ++i) {
        const int64_t s = sim.v_q32_32[i];
        const int64_t t = target.v_q32_32[i];
        const int64_t num = abs_i64(s - t);
        const int64_t den = abs_i64(t) + (1LL << 8); // small epsilon in Q32.32
        // (num/den) in Q32.32
        __int128 q = (__int128)num << 32;
        int64_t e = (den > 0) ? (int64_t)(q / (__int128)den) : (1LL << 32);
        if (e > worst) worst = e;
    }
    return worst;
}

int64_t LearningCheckpointGate::rel_err_q32_32_vec(const std::vector<int64_t>& sim_q32_32,
                                                   const std::vector<int64_t>& target_q32_32,
                                                   int64_t denom_floor_q32_32) {
    if (sim_q32_32.size() != target_q32_32.size() || sim_q32_32.empty()) {
        return (int64_t(1) << 32); // 1.0
    }
    auto abs_i64 = [](int64_t x) -> int64_t { return x < 0 ? -x : x; };
    int64_t worst = 0;
    for (size_t i = 0; i < sim_q32_32.size(); ++i) {
        const int64_t s = sim_q32_32[i];
        const int64_t t = target_q32_32[i];
        const int64_t num = abs_i64(s - t);
        int64_t den = abs_i64(t);
        if (den < denom_floor_q32_32) den = denom_floor_q32_32;
        // err = (num/den) in Q32.32
        const __int128 scaled = (__int128(num) << 32);
        const int64_t err = int64_t(scaled / __int128(den));
        if (err > worst) worst = err;
    }
    return worst;
}

void LearningCheckpointGate::tick(SubstrateMicroprocessor* sm) {
    if (!sm) return;
    if (!registry_.has_pending()) return;

    // Process multiple tasks per engine tick if earlier tasks complete instantly
    // (e.g., 100% match on the initial reverse-calc attempt).
    uint32_t tasks_processed = 0;
    const uint32_t tasks_per_tick_cap = 4;
    while (registry_.has_pending() && tasks_processed < tasks_per_tick_cap) {
        MetricTask* t = registry_.front();
        if (!t) return;
        if (t->completed) {
            t->completed_tick_u64 = (uint64_t)sm->canonical_tick;
            registry_.record_completed(*t);
            registry_.pop_front();
            ++tasks_processed;
            continue;
        }

    // If task is missing budgets, initialize from substrate policy.
    // Budgets are derived from headroom/governor unless explicitly configured.
    if (t->tries_per_step_u32 == 0) {
        const uint32_t cfg = sm->learning_tries_per_step_u32;
        t->tries_per_step_u32 = (cfg == 0u) ? sm->derived_learning_tries_per_step_u32(sm->ctx) : cfg;
        if (t->tries_per_step_u32 == 0) t->tries_per_step_u32 = 1u;
    }
    if (t->tries_remaining_u64 == 0) {
        t->tries_remaining_u64 = sm->learning_max_object_updates_per_request_u64;
    }
    if (t->ticks_remaining_u32 == 0) {
        t->ticks_remaining_u32 = sm->learning_ticks_per_tcp_request_u32;
        if (t->ticks_remaining_u32 == 0) t->ticks_remaining_u32 = 360u;
    }

    // Distribute the remaining try budget across remaining ticks so we always
    // "use up" compute for the 1-second request window.
    const uint32_t ticks_left = t->ticks_remaining_u32;
    const uint64_t tries_left = t->tries_remaining_u64;
    uint64_t tries_this_tick = (ticks_left > 0) ? (tries_left / (uint64_t)ticks_left) : tries_left;
    if (tries_this_tick == 0 && tries_left > 0) tries_this_tick = (uint64_t)t->tries_per_step_u32;

    // Convert tries -> steps. Each step represents tries_per_step conceptual tries.
    const uint64_t tps = (uint64_t)t->tries_per_step_u32;
    uint64_t steps = (tps > 0) ? ((tries_this_tick + tps - 1u) / tps) : tries_this_tick;

    // Hard clamp so a single tick cannot stall the host thread. The semantics
    // remain: one "step" represents a batched/fan-out block of tries.
    {
        const uint32_t mspt_cfg = sm->learning_max_steps_per_tick_u32;
        const uint32_t mspt = (mspt_cfg == 0u) ? sm->derived_learning_max_steps_per_tick_u32(sm->ctx) : mspt_cfg;
        if (steps > (uint64_t)mspt) steps = (uint64_t)mspt;
    }

        // ------------------------------------------------------------------
        // Language foundation checkpoints (CPU deterministic evaluation)
        // ------------------------------------------------------------------
        {
            const uint32_t kid = (uint32_t)t->target.kind;
            if (kid >= 100u && kid < 110u) {
                // Evaluate Stage-0 stats deterministically.
                MetricVector sim;
                if (kid >= 104u && kid <= 106u) sim = sm->math_foundation.metrics_for_kind(t->target.kind);
                else sim = sm->language_foundation.metrics_for_kind(t->target.kind);
                const int64_t err = rel_err_q32_32(sim, t->target.target, t->target.tol_num_u32, t->target.tol_den_u32);
                if (err < t->best_err_q32_32) {
                    t->best_err_q32_32 = err;
                    t->best_sim = sim;
                }

                // Consume budget over the request window; no early stop.
                if (t->ticks_remaining_u32 > 0) t->ticks_remaining_u32 -= 1u;
                if (t->tries_remaining_u64 >= tries_this_tick) t->tries_remaining_u64 -= tries_this_tick;
                else t->tries_remaining_u64 = 0;

                if (t->ticks_remaining_u32 == 0u || t->tries_remaining_u64 == 0u) {
                    t->completed = true;
                    // Accept if within tolerance (tol=0 for language tasks).
                    const int64_t tol_q32_32 = (t->target.tol_den_u32 == 0u) ? 0 : (int64_t)(((__int128)t->target.tol_num_u32 << 32) / (int64_t)t->target.tol_den_u32);
                    t->accepted = (t->best_err_q32_32 <= tol_q32_32);
                }

                if (t->completed) {
                    t->completed_tick_u64 = (uint64_t)sm->canonical_tick;
                    registry_.record_completed(*t);
                    registry_.pop_front();
                    ++tasks_processed;
                    continue;
                }
                // Only advance one non-completed task per tick.
                break;
            }
        }

        // Seed the learning probe lattice from the world lattice deterministically
        // so parameter molding/evolution remains grounded in measurable state
        // without perturbing the world lattice.
        if (EwFieldLatticeGpu* world = sm->world_lattice_gpu_for_learning()) {
            if (EwFieldLatticeGpu* probe = sm->probe_lattice_gpu_for_learning()) {
                const uint32_t wx = world->device_gx_u32();
                const uint32_t wy = world->device_gy_u32();
                const uint32_t wz = world->device_gz_u32();
                const uint32_t px = probe->device_gx_u32();
                const uint32_t py = probe->device_gy_u32();
                const uint32_t pz = probe->device_gz_u32();
                const uint32_t max_x = (wx > px) ? (wx - px) : 0u;
                const uint32_t max_y = (wy > py) ? (wy - py) : 0u;
                const uint32_t max_z = (wz > pz) ? (wz - pz) : 0u;
                const uint64_t h = (t->task_id_u64 * 0x9E3779B97F4A7C15ULL) ^ (t->source_id_u64 + 0xD1B54A32D192ED03ULL);
                const uint32_t ox = (max_x > 0u) ? (uint32_t)(h % (uint64_t)(max_x + 1u)) : 0u;
                const uint32_t oy = (max_y > 0u) ? (uint32_t)((h / 1315423911ULL) % (uint64_t)(max_y + 1u)) : 0u;
                const uint32_t oz = (max_z > 0u) ? (uint32_t)((h / 2654435761ULL) % (uint64_t)(max_z + 1u)) : 0u;
                probe->seed_from_world_subregion(*world, ox, oy, oz);

                // Evolve the probe lattice for a bounded number of micro-ticks
                // using parameter-driven modality amplitudes. This makes
                // "parameters mold the lattice" literal without perturbing the
                // world lattice.
                auto clamp_q32_32 = [] (int64_t v) -> int64_t {
                    // Clamp to +/- 16.0 in Q32.32 for stability.
                    const int64_t lim = (int64_t)(16.0 * (double)(1ULL << 32));
                    if (v > lim) return lim;
                    if (v < -lim) return -lim;
                    return v;
                };

                const MetricVector& psrc = (t->first_try_done) ? t->best_params : t->target.target;
                const int64_t amp_x = clamp_q32_32(psrc.v_q32_32[0]);
                const int64_t amp_y = clamp_q32_32(psrc.v_q32_32[1]);
                const int64_t amp_z = clamp_q32_32(psrc.v_q32_32[2]);

                probe->inject_text_amplitude_q32_32(amp_x);
                probe->inject_image_amplitude_q32_32(amp_y);
                probe->inject_audio_amplitude_q32_32(amp_z);

                const uint32_t micro_cfg = sm->learning_probe_micro_ticks_per_engine_tick_u32;
                const uint32_t micro = (micro_cfg == 0u) ? sm->derived_learning_probe_micro_ticks_u32(sm->ctx) : micro_cfg;
                if (micro != 0u) probe->step_micro_ticks(micro, /*bind_as_probe=*/true);
            }
        }


        // GPU-driven form fitting step: CPU only orchestrates.
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
        (void)ew_learning_gate_tick_cuda(t, (uint64_t)sm->canonical_tick, tries_this_tick, (uint32_t)steps);
#else
        // CUDA backend is required for learning gate search. If not available,
        // keep task pending (fail-closed).
        (void)tries_this_tick;
        (void)steps;
#endif

        if (t->completed) {
            t->completed_tick_u64 = (uint64_t)sm->canonical_tick;
            registry_.record_completed(*t);
            registry_.pop_front();
            ++tasks_processed;
            continue;
        }

        // Only advance one non-completed task per engine tick.
        break;
    }
}

} // namespace genesis
