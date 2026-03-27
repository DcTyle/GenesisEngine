#include <cuda_runtime.h>
#include <stdint.h>

#include "GE_object_local_cuda.hpp"

namespace {

__device__ __forceinline__ uint32_t idx3(uint32_t x, uint32_t y, uint32_t z, uint32_t gx, uint32_t gy) {
    return (z * gx * gy) + (y * gx) + x;
}

__device__ __forceinline__ int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// One kernel does:
//  - local phi update (simple conservative stencil)
//  - deterministic reductions for coupling observables
__global__ void k_object_local_step(const uint8_t* occ,
                                    const int16_t* phi_in,
                                    int16_t* phi_out,
                                    uint32_t gx, uint32_t gy, uint32_t gz,
                                    int16_t bias_e_curr_q15,
                                    int16_t bias_flux_q15,
                                    int16_t bias_coherence_q15,
                                    int16_t bias_curvature_q15,
                                    int16_t bias_doppler_q15,
                                    unsigned long long* occ_sum,
                                    unsigned long long* vox_cnt,
                                    unsigned long long* b_occ_sum,
                                    unsigned long long* b_cnt,
                                    long long* phi_sum,
                                    unsigned long long* phi_cnt,
                                    unsigned long long* b_grad_sum,
                                    unsigned long long* b_grad_cnt) {
    const uint32_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    const uint64_t n = (uint64_t)gx * (uint64_t)gy * (uint64_t)gz;
    if ((uint64_t)tid >= n) return;

    const uint32_t x = tid % gx;
    const uint32_t y = (tid / gx) % gy;
    const uint32_t z = (tid / (gx * gy));

    const uint8_t o = occ[tid];
    atomicAdd(vox_cnt, 1ull);
    atomicAdd(occ_sum, (unsigned long long)o);

    // Unoccupied voxels: phi is forced to 0.
    if (o == 0u) {
        phi_out[tid] = 0;
        return;
    }

    // Read local neighborhood (occupied or not); for out-of-bounds we treat as empty.
    const int32_t c = (int32_t)phi_in[tid];

    int32_t lap = 0;
    int32_t grad_mag = 0;

    // 6-neighbor stencil
    const bool edge = (x == 0 || x + 1 >= gx || y == 0 || y + 1 >= gy || z == 0 || z + 1 >= gz);

    auto neighbor_phi = [&](int32_t nx, int32_t ny, int32_t nz) -> int32_t {
        if (nx < 0 || ny < 0 || nz < 0) return 0;
        if ((uint32_t)nx >= gx || (uint32_t)ny >= gy || (uint32_t)nz >= gz) return 0;
        const uint32_t ni = idx3((uint32_t)nx, (uint32_t)ny, (uint32_t)nz, gx, gy);
        if (occ[ni] == 0u) return 0;
        return (int32_t)phi_in[ni];
    };

    const int32_t xm = neighbor_phi((int32_t)x - 1, (int32_t)y, (int32_t)z);
    const int32_t xp = neighbor_phi((int32_t)x + 1, (int32_t)y, (int32_t)z);
    const int32_t ym = neighbor_phi((int32_t)x, (int32_t)y - 1, (int32_t)z);
    const int32_t yp = neighbor_phi((int32_t)x, (int32_t)y + 1, (int32_t)z);
    const int32_t zm = neighbor_phi((int32_t)x, (int32_t)y, (int32_t)z - 1);
    const int32_t zp = neighbor_phi((int32_t)x, (int32_t)y, (int32_t)z + 1);

    lap = (xm + xp + ym + yp + zm + zp) - 6 * c;

    // Gradient magnitude proxy (L1)
    grad_mag = abs(c - xm) + abs(c - xp) + abs(c - ym) + abs(c - yp) + abs(c - zm) + abs(c - zp);

    // Deterministic update rule:
    //  phi' = phi + (lap >> 3) - (phi >> 7) + boundary_bias
    //  - lap term diffuses/propagates
    //  - damping term prevents runaway and acts as leak
    //  - boundary_bias couples object-local boundary voxels to world lattice mean
    //    energy in the object's neighborhood (bidirectional boundary exchange).
    //  - (lap >> 3) sets the effective propagation rate; ensure this is physically justified.
    // PHASE PROPAGATION SPEED ENFORCEMENT:
    //  The shift value (>> 3) determines the maximum phase propagation speed.
    //  If this is changed, recalculate the effective speed and ensure it does not exceed c.
    int32_t p = c + (lap >> 3) - (c >> 7);
    p = clamp_i32(p, -32768, 32767);
    phi_out[tid] = (int16_t)p;

    // Reductions (occupied only)
    atomicAdd(phi_cnt, 1ull);
    atomicAdd(phi_sum, (long long)p);

    // Boundary definition: occupied voxel with any neighbor empty or out-of-bounds
    bool boundary = edge;
    if (!boundary) {
        const uint32_t xm_i = idx3(x - 1, y, z, gx, gy);
        const uint32_t xp_i = idx3(x + 1, y, z, gx, gy);
        const uint32_t ym_i = idx3(x, y - 1, z, gx, gy);
        const uint32_t yp_i = idx3(x, y + 1, z, gx, gy);
        const uint32_t zm_i = idx3(x, y, z - 1, gx, gy);
        const uint32_t zp_i = idx3(x, y, z + 1, gx, gy);
        boundary = (occ[xm_i] == 0u) || (occ[xp_i] == 0u) || (occ[ym_i] == 0u) || (occ[yp_i] == 0u) || (occ[zm_i] == 0u) || (occ[zp_i] == 0u);
    }

    if (boundary) {
        // Apply a small world-coupling bias only at the boundary.
        // The biases are centered signed Q15-ish in [-32768,32767]. We apply
        // fixed right-shifts per channel to prevent runaway.
        p = p + ((int32_t)bias_e_curr_q15 >> 5);
        p = p + ((int32_t)bias_flux_q15 >> 6);
        p = p + ((int32_t)bias_coherence_q15 >> 7);
        p = p + ((int32_t)bias_curvature_q15 >> 7);
        p = p + ((int32_t)bias_doppler_q15 >> 6);
        p = clamp_i32(p, -32768, 32767);
        phi_out[tid] = (int16_t)p;

        atomicAdd(b_cnt, 1ull);
        atomicAdd(b_occ_sum, (unsigned long long)o);
        atomicAdd(b_grad_cnt, 1ull);
        atomicAdd(b_grad_sum, (unsigned long long)grad_mag);
    }
}

} // namespace

extern "C" bool ge_object_local_step_q15_cuda(const uint8_t* host_occ_u8,
                                              const int16_t* host_phi_in_q15_s16,
                                              uint32_t gx, uint32_t gy, uint32_t gz,
                                              int16_t bias_e_curr_q15,
                                              int16_t bias_flux_q15,
                                              int16_t bias_coherence_q15,
                                              int16_t bias_curvature_q15,
                                              int16_t bias_doppler_q15,
                                              int16_t* host_phi_out_q15_s16,
                                              genesis::EwObjectLocalStepStats* out_host) {
    if (!host_occ_u8 || !host_phi_in_q15_s16 || !host_phi_out_q15_s16 || !out_host) return false;
    if (gx == 0 || gy == 0 || gz == 0) return false;

    const uint64_t n = (uint64_t)gx * (uint64_t)gy * (uint64_t)gz;
    const size_t occ_bytes = (size_t)n;
    const size_t phi_bytes = (size_t)(n * 2ull);

    uint8_t* d_occ = nullptr;
    int16_t* d_phi_in = nullptr;
    int16_t* d_phi_out = nullptr;

    unsigned long long *d_occ_sum=nullptr,*d_vox_cnt=nullptr,*d_b_occ_sum=nullptr,*d_b_cnt=nullptr;
    long long* d_phi_sum=nullptr;
    unsigned long long *d_phi_cnt=nullptr,*d_b_grad_sum=nullptr,*d_b_grad_cnt=nullptr;

    auto fail = [&]() {
        if (d_b_grad_cnt) cudaFree(d_b_grad_cnt);
        if (d_b_grad_sum) cudaFree(d_b_grad_sum);
        if (d_phi_cnt) cudaFree(d_phi_cnt);
        if (d_phi_sum) cudaFree(d_phi_sum);
        if (d_b_cnt) cudaFree(d_b_cnt);
        if (d_b_occ_sum) cudaFree(d_b_occ_sum);
        if (d_vox_cnt) cudaFree(d_vox_cnt);
        if (d_occ_sum) cudaFree(d_occ_sum);
        if (d_phi_out) cudaFree(d_phi_out);
        if (d_phi_in) cudaFree(d_phi_in);
        if (d_occ) cudaFree(d_occ);
        return false;
    };

    if (cudaMalloc((void**)&d_occ, occ_bytes) != cudaSuccess) return false;
    if (cudaMalloc((void**)&d_phi_in, phi_bytes) != cudaSuccess) return fail();
    if (cudaMalloc((void**)&d_phi_out, phi_bytes) != cudaSuccess) return fail();

    if (cudaMalloc((void**)&d_occ_sum, sizeof(unsigned long long)) != cudaSuccess) return fail();
    if (cudaMalloc((void**)&d_vox_cnt, sizeof(unsigned long long)) != cudaSuccess) return fail();
    if (cudaMalloc((void**)&d_b_occ_sum, sizeof(unsigned long long)) != cudaSuccess) return fail();
    if (cudaMalloc((void**)&d_b_cnt, sizeof(unsigned long long)) != cudaSuccess) return fail();
    if (cudaMalloc((void**)&d_phi_sum, sizeof(long long)) != cudaSuccess) return fail();
    if (cudaMalloc((void**)&d_phi_cnt, sizeof(unsigned long long)) != cudaSuccess) return fail();
    if (cudaMalloc((void**)&d_b_grad_sum, sizeof(unsigned long long)) != cudaSuccess) return fail();
    if (cudaMalloc((void**)&d_b_grad_cnt, sizeof(unsigned long long)) != cudaSuccess) return fail();

    unsigned long long z0u = 0ull;
    long long z0s = 0ll;
    if (cudaMemcpy(d_occ, host_occ_u8, occ_bytes, cudaMemcpyHostToDevice) != cudaSuccess) return fail();
    if (cudaMemcpy(d_phi_in, host_phi_in_q15_s16, phi_bytes, cudaMemcpyHostToDevice) != cudaSuccess) return fail();

    cudaMemcpy(d_occ_sum, &z0u, sizeof(z0u), cudaMemcpyHostToDevice);
    cudaMemcpy(d_vox_cnt, &z0u, sizeof(z0u), cudaMemcpyHostToDevice);
    cudaMemcpy(d_b_occ_sum, &z0u, sizeof(z0u), cudaMemcpyHostToDevice);
    cudaMemcpy(d_b_cnt, &z0u, sizeof(z0u), cudaMemcpyHostToDevice);
    cudaMemcpy(d_phi_sum, &z0s, sizeof(z0s), cudaMemcpyHostToDevice);
    cudaMemcpy(d_phi_cnt, &z0u, sizeof(z0u), cudaMemcpyHostToDevice);
    cudaMemcpy(d_b_grad_sum, &z0u, sizeof(z0u), cudaMemcpyHostToDevice);
    cudaMemcpy(d_b_grad_cnt, &z0u, sizeof(z0u), cudaMemcpyHostToDevice);

    const uint32_t block = 256u;
    const uint32_t grid = (uint32_t)((n + (uint64_t)block - 1ull) / (uint64_t)block);
    k_object_local_step<<<grid, block>>>(d_occ, d_phi_in, d_phi_out, gx, gy, gz,
                                         bias_e_curr_q15,
                                         bias_flux_q15,
                                         bias_coherence_q15,
                                         bias_curvature_q15,
                                         bias_doppler_q15,
                                         d_occ_sum, d_vox_cnt, d_b_occ_sum, d_b_cnt,
                                         d_phi_sum, d_phi_cnt, d_b_grad_sum, d_b_grad_cnt);
    if (cudaDeviceSynchronize() != cudaSuccess) return fail();

    if (cudaMemcpy(host_phi_out_q15_s16, d_phi_out, phi_bytes, cudaMemcpyDeviceToHost) != cudaSuccess) return fail();

    unsigned long long h_occ_sum=0,h_vox_cnt=0,h_b_occ_sum=0,h_b_cnt=0,h_phi_cnt=0,h_b_grad_sum=0,h_b_grad_cnt=0;
    long long h_phi_sum=0;

    cudaMemcpy(&h_occ_sum, d_occ_sum, sizeof(h_occ_sum), cudaMemcpyDeviceToHost);
    cudaMemcpy(&h_vox_cnt, d_vox_cnt, sizeof(h_vox_cnt), cudaMemcpyDeviceToHost);
    cudaMemcpy(&h_b_occ_sum, d_b_occ_sum, sizeof(h_b_occ_sum), cudaMemcpyDeviceToHost);
    cudaMemcpy(&h_b_cnt, d_b_cnt, sizeof(h_b_cnt), cudaMemcpyDeviceToHost);
    cudaMemcpy(&h_phi_sum, d_phi_sum, sizeof(h_phi_sum), cudaMemcpyDeviceToHost);
    cudaMemcpy(&h_phi_cnt, d_phi_cnt, sizeof(h_phi_cnt), cudaMemcpyDeviceToHost);
    cudaMemcpy(&h_b_grad_sum, d_b_grad_sum, sizeof(h_b_grad_sum), cudaMemcpyDeviceToHost);
    cudaMemcpy(&h_b_grad_cnt, d_b_grad_cnt, sizeof(h_b_grad_cnt), cudaMemcpyDeviceToHost);

    // Free
    fail(); // uses cudaFree on non-null; returns false but ignore

    out_host->occ_sum_u64 = (uint64_t)h_occ_sum;
    out_host->vox_count_u64 = (uint64_t)h_vox_cnt;
    out_host->boundary_occ_sum_u64 = (uint64_t)h_b_occ_sum;
    out_host->boundary_count_u64 = (uint64_t)h_b_cnt;
    out_host->phi_sum_i64 = (int64_t)h_phi_sum;
    out_host->phi_count_u64 = (uint64_t)h_phi_cnt;
    out_host->boundary_grad_sum_u64 = (uint64_t)h_b_grad_sum;
    out_host->boundary_grad_count_u64 = (uint64_t)h_b_grad_cnt;
    return true;
}
