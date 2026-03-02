#include <cuda_runtime.h>
#include <stdint.h>

static __device__ __forceinline__ int64_t ew_turn_scale() { return 1000000000000000000LL; }

struct EwCarrierTriple {
    uint32_t x_u32;
    uint32_t y_u32;
    uint32_t z_u32;
};

static __device__ __forceinline__ int32_t ew_clamp_i32_dev(int64_t v, int32_t lo, int32_t hi) {
    if (v < (int64_t)lo) return lo;
    if (v > (int64_t)hi) return hi;
    return (int32_t)v;
}

static __device__ __forceinline__ int32_t ew_doppler_k_q16_16_from_turns_dev(int64_t doppler_turns_q) {
    const int64_t absd = (doppler_turns_q < 0) ? -doppler_turns_q : doppler_turns_q;
    const int64_t denom = ew_turn_scale() + absd;
    if (denom <= 0) return 0;
    const int64_t num = (doppler_turns_q << 16);
    return ew_clamp_i32_dev(num / denom, -(int32_t)65536, (int32_t)65536);
}

static __device__ __forceinline__ int32_t ew_leak_density_q16_16_from_mass_dev(int64_t mass_turns_q) {
    int64_t d = ew_turn_scale() - mass_turns_q;
    if (d < 0) d = 0;
    const int64_t num = (d << 16);
    return ew_clamp_i32_dev(num / ew_turn_scale(), 0, (int32_t)65536);
}

__global__ void ew_kernel_compute_carrier_triples(
    const int64_t* __restrict__ doppler_q,
    const int64_t* __restrict__ m_q,
    const uint16_t* __restrict__ harmonics_mean_q15,
    uint32_t anchors_n,
    const uint32_t* __restrict__ anchor_ids,
    uint32_t ids_n,
    EwCarrierTriple* __restrict__ out_triples
) {
    const uint32_t idx = (uint32_t)(blockIdx.x * blockDim.x + threadIdx.x);
    if (idx >= ids_n) return;
    const uint32_t a0 = anchor_ids[idx];
    if (a0 >= anchors_n || anchors_n == 0u) {
        out_triples[idx] = EwCarrierTriple{0u, 0u, 0u};
        return;
    }
    const uint32_t a1 = (a0 + 1u) % anchors_n;
    const uint32_t a2 = (a0 + 2u) % anchors_n;

    const int32_t d0 = ew_doppler_k_q16_16_from_turns_dev(doppler_q[a0]);
    const int32_t d1 = ew_doppler_k_q16_16_from_turns_dev(doppler_q[a1]);
    const int32_t d2 = ew_doppler_k_q16_16_from_turns_dev(doppler_q[a2]);
    const int64_t ds = (int64_t)d0 + (int64_t)d1 + (int64_t)d2;
    const int32_t doppler_bundled = ew_clamp_i32_dev(ds / 3, -(int32_t)65536, (int32_t)65536);

    const int32_t l0 = ew_leak_density_q16_16_from_mass_dev(m_q[a0]);
    const int32_t l1 = ew_leak_density_q16_16_from_mass_dev(m_q[a1]);
    const int32_t l2 = ew_leak_density_q16_16_from_mass_dev(m_q[a2]);
    const int64_t ls = (int64_t)l0 + (int64_t)l1 + (int64_t)l2;
    const int32_t leak_bundled = ew_clamp_i32_dev(ls / 3, 0, (int32_t)65536);

    uint32_t h0 = (uint32_t)harmonics_mean_q15[a0];
    uint32_t h1 = (uint32_t)harmonics_mean_q15[a1];
    uint32_t h2 = (uint32_t)harmonics_mean_q15[a2];
    uint32_t h = ((h0 + h1 + h2) / 3u) & 65535u;

    out_triples[idx] = EwCarrierTriple{(uint32_t)leak_bundled, (uint32_t)doppler_bundled, h};
}

extern "C" bool ew_cuda_compute_carrier_triples_impl(
    const int64_t* doppler_q_turns,
    const int64_t* m_q_turns,
    const uint16_t* harmonics_mean_q15,
    uint32_t anchors_n,
    const uint32_t* anchor_ids,
    uint32_t ids_n,
    EwCarrierTriple* out_triples
) {
    if (!doppler_q_turns || !m_q_turns || !harmonics_mean_q15 || !anchor_ids || !out_triples) return false;
    if (anchors_n == 0u || ids_n == 0u) return true;

    int64_t* d_doppler = nullptr;
    int64_t* d_m = nullptr;
    uint16_t* d_hm = nullptr;
    uint32_t* d_ids = nullptr;
    EwCarrierTriple* d_out = nullptr;

    cudaError_t st = cudaSuccess;
    st = cudaMalloc((void**)&d_doppler, sizeof(int64_t) * (size_t)anchors_n);
    if (st != cudaSuccess) return false;
    st = cudaMalloc((void**)&d_m, sizeof(int64_t) * (size_t)anchors_n);
    if (st != cudaSuccess) { cudaFree(d_doppler); return false; }
    st = cudaMalloc((void**)&d_hm, sizeof(uint16_t) * (size_t)anchors_n);
    if (st != cudaSuccess) { cudaFree(d_doppler); cudaFree(d_m); return false; }
    st = cudaMalloc((void**)&d_ids, sizeof(uint32_t) * (size_t)ids_n);
    if (st != cudaSuccess) { cudaFree(d_doppler); cudaFree(d_m); cudaFree(d_hm); return false; }
    st = cudaMalloc((void**)&d_out, sizeof(EwCarrierTriple) * (size_t)ids_n);
    if (st != cudaSuccess) { cudaFree(d_doppler); cudaFree(d_m); cudaFree(d_hm); cudaFree(d_ids); return false; }

    st = cudaMemcpy(d_doppler, doppler_q_turns, sizeof(int64_t) * (size_t)anchors_n, cudaMemcpyHostToDevice);
    if (st != cudaSuccess) goto fail;
    st = cudaMemcpy(d_m, m_q_turns, sizeof(int64_t) * (size_t)anchors_n, cudaMemcpyHostToDevice);
    if (st != cudaSuccess) goto fail;
    st = cudaMemcpy(d_hm, harmonics_mean_q15, sizeof(uint16_t) * (size_t)anchors_n, cudaMemcpyHostToDevice);
    if (st != cudaSuccess) goto fail;
    st = cudaMemcpy(d_ids, anchor_ids, sizeof(uint32_t) * (size_t)ids_n, cudaMemcpyHostToDevice);
    if (st != cudaSuccess) goto fail;

    const uint32_t block = 256u;
    const uint32_t grid = (ids_n + block - 1u) / block;
    ew_kernel_compute_carrier_triples<<<grid, block>>>(d_doppler, d_m, d_hm, anchors_n, d_ids, ids_n, d_out);
    st = cudaGetLastError();
    if (st != cudaSuccess) goto fail;
    st = cudaMemcpy(out_triples, d_out, sizeof(EwCarrierTriple) * (size_t)ids_n, cudaMemcpyDeviceToHost);
    if (st != cudaSuccess) goto fail;

    cudaFree(d_doppler);
    cudaFree(d_m);
    cudaFree(d_hm);
    cudaFree(d_ids);
    cudaFree(d_out);
    return true;

fail:
    cudaFree(d_doppler);
    cudaFree(d_m);
    cudaFree(d_hm);
    cudaFree(d_ids);
    cudaFree(d_out);
    return false;
}
