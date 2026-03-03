#include <cuda_runtime.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Scalar-factor snap grid (fixed-point-like quantization)
//
// Purpose:
//   - Reduce sensitivity to fp32 rounding noise
//   - Improve cross-run determinism by snapping updated values to a stable grid
//
// Design:
//   - Quantize select evolved fields to int32 bins via per-field scale factors
//   - Convert back to float for downstream visualization / learning bindings
//
// Notes:
//   - This does NOT eliminate all fp32 use, but it bounds drift and makes
//     small differences collapse to the same representable bins.
//   - Scales are chosen to keep int32 safe under expected magnitudes.
// ---------------------------------------------------------------------------
static __device__ __forceinline__ float ew_quantize_f32(float v, float scale) {
    // Clamp to avoid int32 overflow when scaling.
    const float vmax = 2147483000.0f / scale;
    if (v > vmax) v = vmax;
    if (v < -vmax) v = -vmax;
    const int q = __float2int_rn(v * scale);
    return ((float)q) / scale;
}

// Per-field snap scales (powers of two are cheap and stable).
// If you adjust these, do it deterministically and keep them stable across builds.
static const float EW_SNAP_E_SCALE        = 1048576.0f; // 2^20
static const float EW_SNAP_FLUX_SCALE     = 1048576.0f; // 2^20
static const float EW_SNAP_DOPPLER_SCALE  = 1048576.0f; // 2^20
static const float EW_SNAP_CURV_SCALE     = 262144.0f;  // 2^18
static const float EW_SNAP_COHERENCE_SCALE= 1048576.0f; // 2^20 (keeps [0,1] tight)
static const float EW_SNAP_OP_SCALE       = 1048576.0f; // 2^20

static __device__ __forceinline__ int idx3(int x, int y, int z, int gx, int gy) {
    return (z * gy + y) * gx + x;
}

extern "C" __global__ void ew_kernel_clear_f32(float* a, int n, float v) {
    int i = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    if (i < n) a[i] = v;
}

extern "C" __global__ void ew_kernel_clear_u8(uint8_t* a, int n, uint8_t v) {
    int i = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    if (i < n) a[i] = v;
}

// Deterministic source injection: add a localized impulse at the domain center.
extern "C" __global__ void ew_kernel_inject_center(float* E_curr, float* flux, float* doppler,
                                                   float* opA, float* opB,
                                                   const uint8_t* bh_exclude_u8,
                                                   int gx, int gy, int gz,
                                                   float amp_text, float amp_image, float amp_audio,
                                                   uint64_t tick_index) {
    // Single-thread deterministic injection.
    if (blockIdx.x == 0 && threadIdx.x == 0) {
        const int cx = gx / 2;
        const int cy = gy / 2;
        const int cz = gz / 2;
        const int i0 = idx3(cx, cy, cz, gx, gy);

        // Do not inject inside a black hole event horizon.
        if (bh_exclude_u8 && bh_exclude_u8[i0] != 0u) return;

        // TEXT -> x driver, IMAGE -> y driver, AUDIO -> z driver.
        // All three contribute to flux/energy as separate manifolds.
        const float src = amp_text + amp_image + amp_audio;
        E_curr[i0] += src;
        flux[i0] += src;

        // Operator-field modulation injection (minimal operator-as-field mechanism).
        opA[i0] += 0.25f * src;
        opB[i0] += 0.10f * src;

        // Doppler proxy: deterministic tick-modulated color driver.
        const float dk = (float)((tick_index % 1024ULL) / 1024.0f);
        doppler[i0] = dk;
    }
}

// Deterministic merged pulse injection.
// Each cmd targets a single voxel; host code must merge duplicates.
struct EwPulseInjectCmd {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    float amp_text;
    float amp_image;
    float amp_audio;
};

extern "C" __global__ void ew_kernel_inject_cmds(float* E_curr, float* flux, float* doppler,
                                                 float* opA, float* opB,
                                                 const uint8_t* bh_exclude_u8,
                                                 int gx, int gy, int gz,
                                                 const EwPulseInjectCmd* cmds, int n_cmd,
                                                 uint64_t tick_index) {
    int i = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    if (i >= n_cmd) return;
    const EwPulseInjectCmd c = cmds[i];
    if (c.x >= (uint32_t)gx || c.y >= (uint32_t)gy || c.z >= (uint32_t)gz) return;
    const int idx = idx3((int)c.x, (int)c.y, (int)c.z, gx, gy);
    if (bh_exclude_u8 && bh_exclude_u8[idx] != 0u) return;

    const float src = c.amp_text + c.amp_image + c.amp_audio;
    E_curr[idx] += src;
    flux[idx] += src;

    // Operator-field modulation (deterministic, local).
    opA[idx] += 0.25f * src;
    opB[idx] += 0.10f * src;

    // Doppler proxy tag (tick-stable).
    const float dk = (float)((tick_index % 1024ULL) / 1024.0f);
    doppler[idx] = dk;
}

// Wave transport step with damping and surface interaction via density mask.
// E_next = 2E_curr - E_prev + c2*lap(E_curr)*dt^2 - beta*E_curr*dt^2
// Surface response: local density reduces transport and increases damping.
extern "C" __global__ void ew_kernel_wave_step(const float* E_prev, const float* E_curr, float* E_next,
                                               float* flux, float* coherence, float* curvature,
                                               float* doppler,
                                               float* opA, float* opB,
                                               const uint8_t* density_u8,
                                               const uint8_t* bh_exclude_u8,
                                               int gx, int gy, int gz,
                                               float c2, float beta, float dt) {
    int tid = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    const int n = gx * gy * gz;
    if (tid >= n) return;

    const int xy = gx * gy;
    const int z = tid / xy;
    const int rem = tid - z * xy;
    const int y = rem / gx;
    const int x = rem - y * gx;

    // 6-neighbor laplacian with clamp boundaries.
    const int xm = (x > 0) ? (x - 1) : x;
    const int xp = (x + 1 < gx) ? (x + 1) : x;
    const int ym = (y > 0) ? (y - 1) : y;
    const int yp = (y + 1 < gy) ? (y + 1) : y;
    const int zm = (z > 0) ? (z - 1) : z;
    const int zp = (z + 1 < gz) ? (z + 1) : z;

    const int iC = tid;
    const int iXm = idx3(xm, y, z, gx, gy);
    const int iXp = idx3(xp, y, z, gx, gy);
    const int iYm = idx3(x, ym, z, gx, gy);
    const int iYp = idx3(x, yp, z, gx, gy);
    const int iZm = idx3(x, y, zm, gx, gy);
    const int iZp = idx3(x, y, zp, gx, gy);

    // Black hole event-horizon clamp: do not evolve inside.
    if (bh_exclude_u8 && bh_exclude_u8[iC] != 0u) {
        E_next[iC] = ew_quantize_f32((E_curr[iC]), EW_SNAP_E_SCALE);
        if (coherence) coherence[iC] = ew_quantize_f32((0.0f), EW_SNAP_COHERENCE_SCALE);
        if (curvature) curvature[iC] = ew_quantize_f32((0.0f), EW_SNAP_CURV_SCALE);
        if (flux) flux[iC] = ew_quantize_f32(((0.98f * flux[iC])), EW_SNAP_FLUX_SCALE);
        if (doppler) doppler[iC] = ew_quantize_f32((0.0f), EW_SNAP_DOPPLER_SCALE);
        if (opA) opA[iC] = ew_quantize_f32(((0.999f * opA[iC])), EW_SNAP_OP_SCALE);
        if (opB) opB[iC] = ew_quantize_f32(((0.999f * opB[iC])), EW_SNAP_OP_SCALE);
        return;
    }

    // ---------------------------------------------------------------------
    // Local conditional hold (user directive):
    // Holding must be *local*, never global. We therefore gate evolution per
    // voxel using a deterministic local energy proxy, rather than rejecting
    // the entire tick.
    //
    // local_budget = |E| + |flux| + |opA| + |opB|
    // If below a tiny floor, we hold this voxel (no transport), while
    // neighbors still evolve normally.
    // ---------------------------------------------------------------------
    const float Ec0 = E_curr[iC];
    const float f0 = (flux ? flux[iC] : 0.0f);
    const float oa0 = (opA ? opA[iC] : 0.0f);
    const float ob0 = (opB ? opB[iC] : 0.0f);
    const float absEc0 = (Ec0 >= 0.0f) ? Ec0 : -Ec0;
    const float absf0 = (f0 >= 0.0f) ? f0 : -f0;
    const float absoa0 = (oa0 >= 0.0f) ? oa0 : -oa0;
    const float absob0 = (ob0 >= 0.0f) ? ob0 : -ob0;
    const float local_budget = absEc0 + absf0 + absoa0 + absob0;
    // Floor chosen above fp32 denormals but effectively zero for simulation.
    const float abs0_floor = 1.0e-7f;
    if (local_budget <= abs0_floor) {
        E_next[iC] = Ec0;
        if (coherence) coherence[iC] = ew_quantize_f32((0.0f), EW_SNAP_COHERENCE_SCALE);
        if (curvature) curvature[iC] = ew_quantize_f32((0.0f), EW_SNAP_CURV_SCALE);
        if (flux) flux[iC] = ew_quantize_f32(((0.999f * f0)), EW_SNAP_FLUX_SCALE);
        if (doppler) doppler[iC] = ew_quantize_f32(((0.999f * doppler[iC])), EW_SNAP_DOPPLER_SCALE);
        if (opA) opA[iC] = ew_quantize_f32(((0.999f * oa0)), EW_SNAP_OP_SCALE);
        if (opB) opB[iC] = ew_quantize_f32(((0.999f * ob0)), EW_SNAP_OP_SCALE);
        return;
    }

    const float Ec = Ec0;
    const float lap = (E_curr[iXm] + E_curr[iXp] + E_curr[iYm] + E_curr[iYp] + E_curr[iZm] + E_curr[iZp] - 6.0f * Ec);

    // Local CMB-sink-like gate for transport: if laplacian magnitude is below
    // a small floor, suppress the transport term (no local "force" update)
    // while still allowing damping/decay.
    const float abs_lap = (lap >= 0.0f) ? lap : -lap;
    const float cmb_lap_floor = 5.0e-7f;
    const float lap_eff = (abs_lap > cmb_lap_floor) ? lap : 0.0f;

    // Density-driven response coefficient.
    const float dens = (float)density_u8[iC] / 255.0f;
    // Base operator terms plus operator-field modulation.
    const float damp = (beta * (1.0f + 4.0f * dens)) + opB[iC];
    const float trans = (c2 * (1.0f - 0.65f * dens)) + opA[iC];

    const float dt2 = dt * dt;
    float En = 2.0f * Ec - E_prev[iC] + trans * lap_eff * dt2 - damp * Ec * dt2;

    // ---------------------------------------------------------------------
    // Emergent contact constraint (field-native, no collider/solver stack)
    //
    // High local density is treated as a proxy for multi-occupancy/contact.
    // We apply a conservative pressure-like correction driven by the density
    // Laplacian to encourage separation and stable stacking.
    //
    // contact_term = k_contact * overlap * lap(density)
    // overlap = max(0, dens - dens_cap)
    // ---------------------------------------------------------------------
    {
        const float dens_cap = 0.78f;
        const float overlap = (dens > dens_cap) ? (dens - dens_cap) : 0.0f;
        if (overlap > 0.0f) {
            const float dm = (float)density_u8[iXm] / 255.0f;
            const float dp = (float)density_u8[iXp] / 255.0f;
            const float em = (float)density_u8[iYm] / 255.0f;
            const float ep = (float)density_u8[iYp] / 255.0f;
            const float fm = (float)density_u8[iZm] / 255.0f;
            const float fp = (float)density_u8[iZp] / 255.0f;
            const float dens_lap = (dm + dp + em + ep + fm + fp - 6.0f * dens);

            const float k_contact = 0.35f;
            En += (k_contact * overlap * dens_lap) * dt2;

            if (flux) {
                const float k_flux = 0.08f;
                flux[iC] = ew_quantize_f32((flux[iC] - k_flux * overlap * dens_lap), EW_SNAP_FLUX_SCALE);
            }
        }
    }

    // Coherence gate: higher density reduces coherence.
    coherence[iC] = ew_quantize_f32(((1.0f - dens)), EW_SNAP_COHERENCE_SCALE);

    // Curvature proxy: magnitude of laplacian.
    curvature[iC] = ew_quantize_f32((((lap >= 0.0f) ? lap : -lap)), EW_SNAP_CURV_SCALE);

    // Flux tracks energy transport.
    flux[iC] = ew_quantize_f32(((0.98f * flux[iC] + 0.02f * En)), EW_SNAP_FLUX_SCALE);

    // Doppler proxy: local gradient magnitude in x.
    const float dx = E_curr[iXp] - E_curr[iXm];
    doppler[iC] = ew_quantize_f32(((0.995f * doppler[iC] + 0.005f * ((dx >= 0.0f) ? dx : -dx))), EW_SNAP_DOPPLER_SCALE);

    // Deterministic operator-field persistence/decay.
    opA[iC] = ew_quantize_f32(((0.999f * opA[iC])), EW_SNAP_OP_SCALE);
    opB[iC] = ew_quantize_f32(((0.999f * opB[iC])), EW_SNAP_OP_SCALE);

    E_next[iC] = ew_quantize_f32((En), EW_SNAP_E_SCALE);
}

// Deterministic per-block sum reduction (fixed order). Writes one float per block.
extern "C" __global__ void ew_kernel_block_sum_abs(const float* a, float* out_block, int n) {
    __shared__ float sh[1024];
    int tid = (int)threadIdx.x;
    int i = (int)(blockIdx.x * blockDim.x + tid);
    float v = 0.0f;
    if (i < n) {
        float x = a[i];
        v = (x >= 0.0f) ? x : -x;
    }
    sh[tid] = v;
    __syncthreads();

    // Fixed-order binary reduction.
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) sh[tid] = sh[tid] + sh[tid + s];
        __syncthreads();
    }
    if (tid == 0) out_block[blockIdx.x] = sh[0];
}

extern "C" __global__ void ew_kernel_block_sum(const float* a, float* out_block, int n) {
    __shared__ float sh[1024];
    int tid = (int)threadIdx.x;
    int i = (int)(blockIdx.x * blockDim.x + tid);
    float v = (i < n) ? a[i] : 0.0f;
    sh[tid] = v;
    __syncthreads();
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) sh[tid] = sh[tid] + sh[tid + s];
        __syncthreads();
    }
    if (tid == 0) out_block[blockIdx.x] = sh[0];
}

// Compute rho_E = flux^2 + curvature^2 into out_rho (float32 per voxel).
extern "C" __global__ void ew_kernel_compute_rho(const float* flux, const float* curvature, float* out_rho, int n) {
    int i = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    if (i < n) {
        const float f = flux[i];
        const float c = curvature[i];
        out_rho[i] = ew_quantize_f32(((f * f + c * c)), EW_SNAP_CURV_SCALE);
    }
}

// Build radiance channels and a BGRA8 slice.
extern "C" __global__ void ew_kernel_build_radiance_slice(const float* flux, const float* coherence, const float* curvature,
                                                          const float* doppler,
                                                          float* L0, float* L1, float* L2, float* L3,
                                                          uint8_t* out_bgra8,
                                                          int gx, int gy, int gz, int slice_z,
                                                          float mean_abs_time, float mean_rho) {
    int tid = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    const int n_slice = gx * gy;
    if (tid >= n_slice) return;
    const int y = tid / gx;
    const int x = tid - y * gx;
    const int i = idx3(x, y, slice_z, gx, gy);

    const float D4 = flux[i];
    const float D5 = coherence[i];
    const float D6 = curvature[i];
    const float D7 = doppler[i];

    // rho_E = D4^2 + D6^2
    const float rho = D4 * D4 + D6 * D6;
    // gamma_t = 1 / (1 + mean(|D3|))  (time magnitude is global)
    const float gamma_t = 1.0f / (1.0f + mean_abs_time);
    // I_c = D5^2 / (1 + mean(rho))
    const float Ic = (D5 * D5) / (1.0f + mean_rho);
    // k_D = D7 / (1 + |D7|)
    const float kD = D7 / (1.0f + ((D7 >= 0.0f) ? D7 : -D7));
    // L = rho * gamma_t * I_c
    const float L = rho * gamma_t * Ic;

    L0[i] = L;
    L1[i] = kD;
    L2[i] = rho;
    L3[i] = Ic;

    // Map to BGRA8.
    float intensity = L;
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    const uint8_t I = (uint8_t)(intensity * 255.0f);

    // Color from kD in [-1,1] => [0,255]
    float kd01 = (kD * 0.5f + 0.5f);
    if (kd01 < 0.0f) kd01 = 0.0f;
    if (kd01 > 1.0f) kd01 = 1.0f;
    const uint8_t C = (uint8_t)(kd01 * 255.0f);

    // Simple mapping: blue=I, green=C, red=I (emissive + spectral).
    const int o = tid * 4;
    out_bgra8[o + 0] = I;
    out_bgra8[o + 1] = C;
    out_bgra8[o + 2] = I;
    out_bgra8[o + 3] = 255;
}