#include <cuda_runtime.h>

#include "GE_metric_registry.hpp"

// This CUDA unit intentionally contains the learning fit loop so the host/CPU
// never performs trillions of conceptual "tries". The host only schedules.

namespace genesis {

// -----------------------------------------------------------------------------
// Authoritative lattice view binding (read-only)
// -----------------------------------------------------------------------------
// The learning gate derives measurable metrics from the evolved lattice fields.
// These pointers are bound by the lattice owner (EwFieldLatticeGpu) after each
// tick so learning kernels see the latest committed field state.

__device__ const float* g_lattice_E_curr = nullptr;
__device__ const float* g_lattice_flux = nullptr;
__device__ const float* g_lattice_coherence = nullptr;
__device__ const float* g_lattice_curvature = nullptr;
__device__ const float* g_lattice_doppler = nullptr;
__device__ int g_lattice_gx = 0;
__device__ int g_lattice_gy = 0;
__device__ int g_lattice_gz = 0;

// Learning sandbox/probe lattice (read-only view for metric extraction).
// The probe lattice is evolved/seeded by its owning lattice module; learning
// uses it as the parameter-molded sandbox without perturbing the world lattice.
__device__ const float* g_probe_E_curr = nullptr;
__device__ const float* g_probe_flux = nullptr;
__device__ const float* g_probe_coherence = nullptr;
__device__ const float* g_probe_curvature = nullptr;
__device__ const float* g_probe_doppler = nullptr;
__device__ int g_probe_gx = 0;
__device__ int g_probe_gy = 0;
__device__ int g_probe_gz = 0;

static __device__ __forceinline__ int64_t d_abs_i64(int64_t x) { return (x < 0) ? -x : x; }

static __device__ __forceinline__ uint64_t d_xorshift64(uint64_t& s) {
    uint64_t x = s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s = x;
    return x;
}

// Metric derivation from measurable lattice samples.
// If no lattice view is bound, derivation is still well-defined: the field
// observables are treated as zero, and the metric becomes a deterministic
// function of (kind, params) only.
static __device__ __forceinline__ void d_simulate_metric(
    const MetricTarget& tgt,
    const MetricVector& params,
    MetricVector& out
) {
    out.dim_u32 = tgt.target.dim_u32;
    const uint32_t dim = out.dim_u32;

    // If a lattice view is bound, derive measurable signals from it.
    // Prefer the probe lattice if bound; otherwise fall back to world.
    const bool have_probe = (g_probe_E_curr != nullptr) && (g_probe_gx > 0) && (g_probe_gy > 0) && (g_probe_gz > 0);
    const int gx = have_probe ? g_probe_gx : g_lattice_gx;
    const int gy = have_probe ? g_probe_gy : g_lattice_gy;
    const int gz = have_probe ? g_probe_gz : g_lattice_gz;
    const float* E = have_probe ? g_probe_E_curr : g_lattice_E_curr;
    const float* F = have_probe ? g_probe_flux : g_lattice_flux;
    const float* C = have_probe ? g_probe_coherence : g_lattice_coherence;
    const float* K = have_probe ? g_probe_curvature : g_lattice_curvature;
    const float* D = have_probe ? g_probe_doppler : g_lattice_doppler;

    // Deterministic float->Q32.32 conversion (bounded).
    auto f_to_q32_32 = [] __device__ (float v) -> int64_t {
        // Clamp to avoid overflow; this is a measurable proxy, not a raw dump.
        if (v > 1024.0f) v = 1024.0f;
        if (v < -1024.0f) v = -1024.0f;
        return (int64_t)((double)v * (double)(1ULL << 32));
    };

    for (uint32_t i = 0; i < dim; ++i) {
        const int64_t p = params.v_q32_32[i % GENESIS_METRIC_DIM_MAX];
        const int64_t k = ((int64_t)((uint32_t)tgt.kind & 0xFFu) - 64) << 24;

        // If the lattice view is not available, treat observables as zero.
        // (Still deterministic and invertible w.r.t. params under fixed kind.)
        if (!E || gx <= 0 || gy <= 0 || gz <= 0) {
            out.v_q32_32[i] = p + k;
            continue;
        }

        // Deterministic probe index selection from (kind,i).
        // This maps metrics onto measurable field samples without CPU work.
        const uint64_t h = ((uint64_t)(uint32_t)tgt.kind * 0x9E3779B185EBCA87ULL) ^ ((uint64_t)i * 0xC2B2AE3D27D4EB4FULL);
        const int x = (int)(h % (uint64_t)gx);
        const int y = (int)((h / (uint64_t)gx) % (uint64_t)gy);
        const int z = (int)((h / (uint64_t)(gx * gy)) % (uint64_t)gz);
        const int idx = (z * gy + y) * gx + x;

        const float e = E[idx];
        const float f = F ? F[idx] : 0.0f;
        const float c = C ? C[idx] : 0.0f;
        const float kf = K ? K[idx] : 0.0f;
        const float d = D ? D[idx] : 0.0f;

        // Build a measurable metric component: params dominate (reverse-calc),
        // but lattice observables shape the landscape so matching requires
        // consistency with the evolved field.
        const int64_t e_q = f_to_q32_32(e);
        const int64_t f_q = f_to_q32_32(f);
        const int64_t c_q = f_to_q32_32(c);
        const int64_t k_q = f_to_q32_32(kf);
        const int64_t d_q = f_to_q32_32(d);

        // Weighted blend (deterministic). Keep weights small to avoid overflow.
        out.v_q32_32[i] = p
            + (e_q >> 6)
            + (f_q >> 7)
            + (c_q >> 7)
            + (k_q >> 8)
            + (d_q >> 8)
            + k;
    }
}

static __device__ __forceinline__ int64_t d_rel_err_q32_32(const MetricVector& sim, const MetricVector& target) {
    const uint32_t dim = (sim.dim_u32 < target.dim_u32) ? sim.dim_u32 : target.dim_u32;
    int64_t worst = 0;
    for (uint32_t i = 0; i < dim; ++i) {
        const int64_t s = sim.v_q32_32[i];
        const int64_t t = target.v_q32_32[i];
        const int64_t num = d_abs_i64(s - t);
        const int64_t den = d_abs_i64(t) + (1LL << 8);
        __int128 q = (__int128)num << 32;
        int64_t e = (den > 0) ? (int64_t)(q / (__int128)den) : (1LL << 32);
        if (e > worst) worst = e;
    }
    return worst;
}

static __device__ __forceinline__ bool d_within_rel_tol_i64(int64_t sim_i, int64_t tgt_i, uint32_t tol_num, uint32_t tol_den) {
    // |sim - tgt| * den <= tol_num * max(|tgt|,1)
    int64_t a = d_abs_i64(sim_i - tgt_i);
    int64_t base = d_abs_i64(tgt_i);
    if (base < 1) base = 1;
    __int128 lhs = (__int128)a * (__int128)tol_den;
    __int128 rhs = (__int128)tol_num * (__int128)base;
    return lhs <= rhs;
}

// First attempt: derive params from target ("reverse-calc" / parameter molding).
__global__ void ew_kernel_learning_first_try(MetricTask* t) {
    if (!t) return;
    if (t->first_try_done || t->completed) return;

    MetricVector params;
    params.dim_u32 = t->target.target.dim_u32;
    const uint32_t dim = params.dim_u32;
    // Deterministic parameter molding: seed params from the target itself.
    for (uint32_t k = 0; k < dim; ++k) {
        params.v_q32_32[k] = t->target.target.v_q32_32[k];
    }

    MetricVector sim;
    d_simulate_metric(t->target, params, sim);
    const int64_t err = d_rel_err_q32_32(sim, t->target.target);

    // Record best-so-far.
    t->best_err_q32_32 = err;
    t->best_sim = sim;
    t->best_params = params;

    // If first try is perfect (100% match), accept immediately.
    if (err == 0) {
        t->accepted = true;
        t->completed = true;
        t->ticks_remaining_u32 = 0;
        t->tries_remaining_u64 = 0;
    }

    t->first_try_done = true;
}

// Batched exploration: each thread evaluates one candidate.
// steps_this_tick <= 1024 enforced by host clamp.
__global__ void ew_kernel_learning_try_batch(
    const MetricTask* t,
    uint64_t seed_base,
    int64_t* out_err,
    MetricVector* out_params,
    MetricVector* out_sim,
    uint32_t steps_this_tick
) {
    const uint32_t tid = (uint32_t)threadIdx.x;
    if (tid >= steps_this_tick) return;
    uint64_t rng = seed_base ^ (uint64_t)(tid * 0x9E3779B97F4A7C15ULL + 0xD1B54A32D192ED03ULL);

    MetricVector params;
    params.dim_u32 = t->target.target.dim_u32;
    const uint32_t dim = params.dim_u32;
    for (uint32_t k = 0; k < dim; ++k) {
        const uint64_t r = d_xorshift64(rng);
        const int64_t s = (int64_t)((r >> 1) & 0x7FFFFFFFULL);
        const int64_t q = (s << 1) - (int64_t)0x7FFFFFFFULL;
        params.v_q32_32[k] = (q << 1);
    }

    MetricVector sim;
    d_simulate_metric(t->target, params, sim);
    const int64_t err = d_rel_err_q32_32(sim, t->target.target);

    out_err[tid] = err;
    out_params[tid] = params;
    out_sim[tid] = sim;
}

// Deterministic reduction for best candidate.
__global__ void ew_kernel_learning_reduce_best(
    MetricTask* t,
    int64_t* err,
    MetricVector* params,
    MetricVector* sim,
    uint32_t steps_this_tick,
    uint32_t tries_per_step
) {
    // One block reduction with fixed order.
    __shared__ int64_t s_err[1024];
    __shared__ uint32_t s_idx[1024];
    const uint32_t tid = (uint32_t)threadIdx.x;
    if (tid < 1024) {
        if (tid < steps_this_tick) {
            s_err[tid] = err[tid];
            s_idx[tid] = tid;
        } else {
            s_err[tid] = (1LL << 62);
            s_idx[tid] = tid;
        }
    }
    __syncthreads();

    for (uint32_t stride = 512; stride > 0; stride >>= 1) {
        if (tid < stride) {
            const uint32_t j = tid + stride;
            const int64_t e0 = s_err[tid];
            const int64_t e1 = s_err[j];
            const uint32_t i0 = s_idx[tid];
            const uint32_t i1 = s_idx[j];
            // Choose lower error; tie-break on lower index for determinism.
            if (e1 < e0 || (e1 == e0 && i1 < i0)) {
                s_err[tid] = e1;
                s_idx[tid] = i1;
            }
        }
        __syncthreads();
    }

    if (tid == 0) {
        const uint32_t best_i = s_idx[0];
        const int64_t best_e = s_err[0];
        if (best_e < t->best_err_q32_32) {
            t->best_err_q32_32 = best_e;
            t->best_params = params[best_i];
            t->best_sim = sim[best_i];
        }

        // Consume tries as conceptual blocks.
        const uint64_t consume = (uint64_t)steps_this_tick * (uint64_t)tries_per_step;
        if (consume >= t->tries_remaining_u64) {
            t->tries_remaining_u64 = 0;
        } else {
            t->tries_remaining_u64 -= consume;
        }
        if (t->ticks_remaining_u32 > 0) t->ticks_remaining_u32 -= 1u;

        const bool window_done = (t->ticks_remaining_u32 == 0u) || (t->tries_remaining_u64 == 0u);
        if (window_done) {
            bool ok = true;
            const uint32_t dim = t->target.target.dim_u32;
            for (uint32_t i = 0; i < dim; ++i) {
                const int64_t s_i = t->best_sim.v_q32_32[i] >> 32;
                const int64_t tgt_i = t->target.target.v_q32_32[i] >> 32;
                if (!d_within_rel_tol_i64(s_i, tgt_i, t->target.tol_num_u32, t->target.tol_den_u32)) {
                    ok = false;
                    break;
                }
            }
            t->accepted = ok;
            t->completed = true;
        }
    }
}

// Host-callable wrapper (declared in header).
bool ew_learning_gate_tick_cuda(
    MetricTask* task_host,
    uint64_t canonical_tick_u64,
    uint64_t /*tries_this_tick_u64*/,
    uint32_t steps_this_tick_u32
) {
    if (!task_host) return false;

    MetricTask* d_task = nullptr;
    cudaMalloc((void**)&d_task, sizeof(MetricTask));
    cudaMemcpy(d_task, task_host, sizeof(MetricTask), cudaMemcpyHostToDevice);

    // First attempt is always parameter-molded.
    if (!task_host->first_try_done && !task_host->completed) {
        ew_kernel_learning_first_try<<<1,1>>>(d_task);
        cudaMemcpy(task_host, d_task, sizeof(MetricTask), cudaMemcpyDeviceToHost);
        if (task_host->completed) {
            cudaFree(d_task);
            return true;
        }
    }

    // If already completed, nothing to do.
    if (task_host->completed) {
        cudaFree(d_task);
        return true;
    }

    // Clamp to our reduction block size.
    uint32_t steps = steps_this_tick_u32;
    if (steps == 0) steps = 1;
    if (steps > 1024) steps = 1024;

    int64_t* d_err = nullptr;
    MetricVector* d_params = nullptr;
    MetricVector* d_sim = nullptr;
    cudaMalloc((void**)&d_err, sizeof(int64_t) * steps);
    cudaMalloc((void**)&d_params, sizeof(MetricVector) * steps);
    cudaMalloc((void**)&d_sim, sizeof(MetricVector) * steps);

    MetricTask tmp;
    cudaMemcpy(&tmp, d_task, sizeof(MetricTask), cudaMemcpyDeviceToHost);
    const uint64_t seed_base = (tmp.task_id_u64 ^ (tmp.source_id_u64 + 0x9E3779B97F4A7C15ULL)) + (canonical_tick_u64 << 1);

    ew_kernel_learning_try_batch<<<1, steps>>>(d_task, seed_base, d_err, d_params, d_sim, steps);
    ew_kernel_learning_reduce_best<<<1, 1024>>>(d_task, d_err, d_params, d_sim, steps, tmp.tries_per_step_u32);

    cudaMemcpy(task_host, d_task, sizeof(MetricTask), cudaMemcpyDeviceToHost);

    cudaFree(d_err);
    cudaFree(d_params);
    cudaFree(d_sim);
    cudaFree(d_task);
    return true;
}

bool ew_learning_bind_world_lattice_cuda(
    const float* d_E_curr,
    const float* d_flux,
    const float* d_coherence,
    const float* d_curvature,
    const float* d_doppler,
    int gx,
    int gy,
    int gz
) {
    cudaError_t st = cudaSuccess;
    st = cudaMemcpyToSymbol(g_lattice_E_curr, &d_E_curr, sizeof(d_E_curr));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_lattice_flux, &d_flux, sizeof(d_flux));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_lattice_coherence, &d_coherence, sizeof(d_coherence));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_lattice_curvature, &d_curvature, sizeof(d_curvature));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_lattice_doppler, &d_doppler, sizeof(d_doppler));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_lattice_gx, &gx, sizeof(gx));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_lattice_gy, &gy, sizeof(gy));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_lattice_gz, &gz, sizeof(gz));
    if (st != cudaSuccess) return false;
    return true;
}

bool ew_learning_bind_probe_lattice_cuda(
    const float* d_E_curr,
    const float* d_flux,
    const float* d_coherence,
    const float* d_curvature,
    const float* d_doppler,
    int gx,
    int gy,
    int gz
) {
    cudaError_t st = cudaSuccess;
    st = cudaMemcpyToSymbol(g_probe_E_curr, &d_E_curr, sizeof(d_E_curr));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_probe_flux, &d_flux, sizeof(d_flux));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_probe_coherence, &d_coherence, sizeof(d_coherence));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_probe_curvature, &d_curvature, sizeof(d_curvature));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_probe_doppler, &d_doppler, sizeof(d_doppler));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_probe_gx, &gx, sizeof(gx));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_probe_gy, &gy, sizeof(gy));
    if (st != cudaSuccess) return false;
    st = cudaMemcpyToSymbol(g_probe_gz, &gz, sizeof(gz));
    if (st != cudaSuccess) return false;
    return true;
}

} // namespace genesis
