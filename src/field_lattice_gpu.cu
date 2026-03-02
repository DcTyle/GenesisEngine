#include "field_lattice.hpp"

#include "learning_gate_cuda.hpp"

#include <cuda_runtime.h>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" __global__ void ew_kernel_clear_f32(float* a, int n, float v);
extern "C" __global__ void ew_kernel_clear_u8(uint8_t* a, int n, uint8_t v);
extern "C" __global__ void ew_kernel_inject_center(float* E_curr, float* flux, float* doppler,
                                                   float* opA, float* opB,
                                                   const uint8_t* bh_exclude_u8,
                                                   int gx, int gy, int gz,
                                                   float amp_text, float amp_image, float amp_audio,
                                                   uint64_t tick_index);
extern "C" __global__ void ew_kernel_inject_cmds(float* E_curr, float* flux, float* doppler,
                                                 float* opA, float* opB,
                                                 const uint8_t* bh_exclude_u8,
                                                 int gx, int gy, int gz,
                                                 const EwPulseInjectCmd* cmds, int n_cmd,
                                                 uint64_t tick_index);
extern "C" __global__ void ew_kernel_wave_step(const float* E_prev, const float* E_curr, float* E_next,
                                               float* flux, float* coherence, float* curvature,
                                               float* doppler,
                                               float* opA, float* opB,
                                               const uint8_t* density_u8,
                                               const uint8_t* bh_exclude_u8,
                                               int gx, int gy, int gz,
                                               float c2, float beta, float dt);
extern "C" __global__ void ew_kernel_block_sum_abs(const float* a, float* out_block, int n);
extern "C" __global__ void ew_kernel_block_sum(const float* a, float* out_block, int n);
extern "C" __global__ void ew_kernel_compute_rho(const float* flux, const float* curvature, float* out_rho, int n);
extern "C" __global__ void ew_kernel_build_radiance_slice(const float* flux, const float* coherence, const float* curvature,
                                                          const float* doppler,
                                                          float* L0, float* L1, float* L2, float* L3,
                                                          uint8_t* out_bgra8,
                                                          int gx, int gy, int gz, int slice_z,
                                                          float mean_abs_time, float mean_rho);

extern "C" __global__ void ew_kernel_emergent_bloom_add(const float* L0, const float* L1, const float* L3,
                                                        uint8_t* out_bgra8,
                                                        int gx, int gy, int slice_z,
                                                        float bloom_gain);

// Deterministic integer reduction for sampling centered Q15-ish means from
// multiple world fields.
// We clamp each float to [-1,1], convert to centered signed Q15-ish integers
// using a fixed scale, and reduce with atomicAdd on int64.
__global__ void ew_kernel_sample_means_q15_box(const float* E_curr,
                                               const float* flux,
                                               const float* coherence,
                                               const float* curvature,
                                               const float* doppler,
                                               int gx, int gy, int gz,
                                               int cx, int cy, int cz,
                                               int rx, int ry, int rz,
                                               int64_t* out_sums_i64_5,
                                               uint64_t* out_cnt_u64) {
    const int tid = (int)blockIdx.x * (int)blockDim.x + (int)threadIdx.x;
    const int sx0 = cx - rx;
    const int sx1 = cx + rx;
    const int sy0 = cy - ry;
    const int sy1 = cy + ry;
    const int sz0 = cz - rz;
    const int sz1 = cz + rz;
    const int nx = sx1 - sx0 + 1;
    const int ny = sy1 - sy0 + 1;
    const int nz = sz1 - sz0 + 1;
    const int n = nx * ny * nz;
    if (tid >= n) return;
    const int lx = tid % nx;
    const int ly = (tid / nx) % ny;
    const int lz = (tid / (nx * ny));
    int x = sx0 + lx;
    int y = sy0 + ly;
    int z = sz0 + lz;
    if (x < 0) x = 0; if (x >= gx) x = gx - 1;
    if (y < 0) y = 0; if (y >= gy) y = gy - 1;
    if (z < 0) z = 0; if (z >= gz) z = gz - 1;
    const int idx = (z * gy + y) * gx + x;
    auto clamp_q15ish = [](float v) -> int32_t {
        float c = v;
        if (c > 1.0f) c = 1.0f;
        if (c < -1.0f) c = -1.0f;
        return (int32_t)(c * 16384.0f);
    };

    const int32_t q0 = clamp_q15ish(E_curr[idx]);
    const int32_t q1 = clamp_q15ish(flux[idx]);
    const int32_t q2 = clamp_q15ish(coherence[idx]);
    const int32_t q3 = clamp_q15ish(curvature[idx]);
    const int32_t q4 = clamp_q15ish(doppler[idx]);

    atomicAdd(&out_sums_i64_5[0], (int64_t)q0);
    atomicAdd(&out_sums_i64_5[1], (int64_t)q1);
    atomicAdd(&out_sums_i64_5[2], (int64_t)q2);
    atomicAdd(&out_sums_i64_5[3], (int64_t)q3);
    atomicAdd(&out_sums_i64_5[4], (int64_t)q4);
    atomicAdd(out_cnt_u64, (uint64_t)1ull);
}

static inline void ew_cuda_check(cudaError_t e, const char* what) {
    if (e != cudaSuccess) {
        throw std::runtime_error(std::string("CUDA error: ") + what + ": " + cudaGetErrorString(e));
    }
}

// Device-to-device copy of a subregion of a world lattice into a probe lattice.
// Used by the learning sandbox so learning can inject/evolve without perturbing
// the world lattice, while remaining grounded in measurable world observables.
__global__ void ew_kernel_copy_subregion_f32(
    const float* src,
    int src_gx, int src_gy, int src_gz,
    float* dst,
    int dst_gx, int dst_gy, int dst_gz,
    int ox, int oy, int oz
) {
    const int x = (int)blockIdx.x * (int)blockDim.x + (int)threadIdx.x;
    const int y = (int)blockIdx.y * (int)blockDim.y + (int)threadIdx.y;
    const int z = (int)blockIdx.z * (int)blockDim.z + (int)threadIdx.z;
    if (x >= dst_gx || y >= dst_gy || z >= dst_gz) return;
    const int sx = ox + x;
    const int sy = oy + y;
    const int sz = oz + z;
    const int csx = (sx < 0) ? 0 : (sx >= src_gx ? (src_gx - 1) : sx);
    const int csy = (sy < 0) ? 0 : (sy >= src_gy ? (src_gy - 1) : sy);
    const int csz = (sz < 0) ? 0 : (sz >= src_gz ? (src_gz - 1) : sz);
    const int sidx = (csz * src_gy + csy) * src_gx + csx;
    const int didx = (z * dst_gy + y) * dst_gx + x;
    dst[didx] = src[sidx];
}

static inline float q32_32_to_float(int64_t v_q32_32) {
    return (float)((double)v_q32_32 / 4294967296.0);
}

EwFieldLatticeGpu::EwFieldLatticeGpu(uint32_t gx, uint32_t gy, uint32_t gz) : gx_(gx), gy_(gy), gz_(gz) {
    alloc_buffers_();
}

EwFieldLatticeGpu::~EwFieldLatticeGpu() {
    free_buffers_();
}

void EwFieldLatticeGpu::alloc_buffers_() {
    const size_t n = (size_t)gx_ * (size_t)gy_ * (size_t)gz_;
    const size_t bytes_f = n * sizeof(float);
    const size_t bytes_u8 = n * sizeof(uint8_t);
    ew_cuda_check(cudaMalloc(&d_E_prev_, bytes_f), "malloc E_prev");
    ew_cuda_check(cudaMalloc(&d_E_curr_, bytes_f), "malloc E_curr");
    ew_cuda_check(cudaMalloc(&d_E_next_, bytes_f), "malloc E_next");
    ew_cuda_check(cudaMalloc(&d_flux_, bytes_f), "malloc flux");
    ew_cuda_check(cudaMalloc(&d_coherence_, bytes_f), "malloc coherence");
    ew_cuda_check(cudaMalloc(&d_curvature_, bytes_f), "malloc curvature");
    ew_cuda_check(cudaMalloc(&d_doppler_, bytes_f), "malloc doppler");
    ew_cuda_check(cudaMalloc(&d_density_u8_, bytes_u8), "malloc density");
    ew_cuda_check(cudaMalloc(&d_bh_exclude_u8_, bytes_u8), "malloc bh_exclude");

    ew_cuda_check(cudaMalloc(&d_opA_, bytes_f), "malloc opA");
    ew_cuda_check(cudaMalloc(&d_opB_, bytes_f), "malloc opB");

    ew_cuda_check(cudaMalloc(&d_L0_, bytes_f), "malloc L0");
    ew_cuda_check(cudaMalloc(&d_L1_, bytes_f), "malloc L1");
    ew_cuda_check(cudaMalloc(&d_L2_, bytes_f), "malloc L2");
    ew_cuda_check(cudaMalloc(&d_L3_, bytes_f), "malloc L3");

    const size_t n_slice = (size_t)gx_ * (size_t)gy_;
    ew_cuda_check(cudaMalloc(&d_slice_bgra8_, n_slice * 4), "malloc slice");
    h_slice_bgra8_.resize(n_slice * 4);

    // Reduction scratch: start with one float per block.
    // Upper bound blocks = ceil(n/1024)
    const uint32_t blocks0 = (uint32_t)((n + 1023) / 1024);
    reduce_scratch_floats_ = blocks0;
    ew_cuda_check(cudaMalloc(&d_reduce_scratch_, (size_t)reduce_scratch_floats_ * sizeof(float)), "malloc reduce scratch");

    // Pulse injection command buffer (host-merged, deterministic).
    // Allocate a bounded capacity; host will clamp uploads.
    const size_t max_cmd = 65536u;
    ew_cuda_check(cudaMalloc(&d_pulse_cmds_, max_cmd * sizeof(EwPulseInjectCmd)), "malloc pulse cmds");
    pulse_cmd_cap_ = (uint32_t)max_cmd;
    pulse_cmd_count_ = 0u;

    // Persistent sampling scratch for boundary exchange (int64 sums + u64 count).
    ew_cuda_check(cudaMalloc(&d_sample_sums_i64_, sizeof(int64_t) * 5u), "malloc sample sums");
    ew_cuda_check(cudaMalloc(&d_sample_cnt_u64_, sizeof(uint64_t)), "malloc sample cnt");
}

EwBoundarySampleQ15 EwFieldLatticeGpu::sample_boundary_means_q15_box(uint32_t center_x, uint32_t center_y, uint32_t center_z,
                                                                     uint32_t radius_x, uint32_t radius_y, uint32_t radius_z) const {
    EwBoundarySampleQ15 out{};
    if (!d_E_curr_ || !d_flux_ || !d_coherence_ || !d_curvature_ || !d_doppler_) return out;
    if (!d_sample_sums_i64_ || !d_sample_cnt_u64_) return out;

    const int cx = (int)center_x;
    const int cy = (int)center_y;
    const int cz = (int)center_z;
    const int rx = (int)radius_x;
    const int ry = (int)radius_y;
    const int rz = (int)radius_z;
    const int nx = rx * 2 + 1;
    const int ny = ry * 2 + 1;
    const int nz = rz * 2 + 1;
    const int n = nx * ny * nz;

    int64_t h_sums[5] = {0,0,0,0,0};
    uint64_t h_cnt = 0;
    ew_cuda_check(cudaMemcpy(d_sample_sums_i64_, h_sums, sizeof(h_sums), cudaMemcpyHostToDevice), "init sample sums");
    ew_cuda_check(cudaMemcpy(d_sample_cnt_u64_, &h_cnt, sizeof(h_cnt), cudaMemcpyHostToDevice), "init sample cnt");

    const int block = 256;
    const int grid = (n + block - 1) / block;
    ew_kernel_sample_means_q15_box<<<grid, block>>>(reinterpret_cast<const float*>(d_E_curr_),
                                                    reinterpret_cast<const float*>(d_flux_),
                                                    reinterpret_cast<const float*>(d_coherence_),
                                                    reinterpret_cast<const float*>(d_curvature_),
                                                    reinterpret_cast<const float*>(d_doppler_),
                                                    (int)gx_, (int)gy_, (int)gz_,
                                                    cx, cy, cz, rx, ry, rz,
                                                    reinterpret_cast<int64_t*>(d_sample_sums_i64_),
                                                    reinterpret_cast<uint64_t*>(d_sample_cnt_u64_));
    ew_cuda_check(cudaDeviceSynchronize(), "sync sample means");
    ew_cuda_check(cudaMemcpy(h_sums, d_sample_sums_i64_, sizeof(h_sums), cudaMemcpyDeviceToHost), "copy sample sums");
    ew_cuda_check(cudaMemcpy(&h_cnt, d_sample_cnt_u64_, sizeof(h_cnt), cudaMemcpyDeviceToHost), "copy sample cnt");
    if (h_cnt == 0) return out;

    auto clamp_i16 = [](int64_t v) -> int16_t {
        if (v < -32768) v = -32768;
        if (v > 32767) v = 32767;
        return (int16_t)v;
    };
    out.e_curr_q15 = clamp_i16(h_sums[0] / (int64_t)h_cnt);
    out.flux_q15 = clamp_i16(h_sums[1] / (int64_t)h_cnt);
    out.coherence_q15 = clamp_i16(h_sums[2] / (int64_t)h_cnt);
    out.curvature_q15 = clamp_i16(h_sums[3] / (int64_t)h_cnt);
    out.doppler_q15 = clamp_i16(h_sums[4] / (int64_t)h_cnt);
    return out;
}

void EwFieldLatticeGpu::free_buffers_() {
    auto fre = [](void* p) { if (p) cudaFree(p); };
    fre(d_E_prev_);
    fre(d_E_curr_);
    fre(d_E_next_);
    fre(d_flux_);
    fre(d_coherence_);
    fre(d_curvature_);
    fre(d_doppler_);
    fre(d_density_u8_);
    fre(d_bh_exclude_u8_);
    fre(d_opA_);
    fre(d_opB_);
    fre(d_L0_);
    fre(d_L1_);
    fre(d_L2_);
    fre(d_L3_);
    fre(d_slice_bgra8_);
    fre(d_reduce_scratch_);
    fre(d_pulse_cmds_);
    fre(d_sample_sums_i64_);
    fre(d_sample_cnt_u64_);
    d_E_prev_ = d_E_curr_ = d_E_next_ = nullptr;
    d_flux_ = d_coherence_ = d_curvature_ = d_doppler_ = nullptr;
    d_density_u8_ = nullptr;
    d_bh_exclude_u8_ = nullptr;
    d_opA_ = d_opB_ = nullptr;
    d_L0_ = d_L1_ = d_L2_ = d_L3_ = nullptr;
    d_slice_bgra8_ = nullptr;
    d_reduce_scratch_ = nullptr;
    d_pulse_cmds_ = nullptr;
    d_sample_sums_i64_ = nullptr;
    d_sample_cnt_u64_ = nullptr;
    pulse_cmd_cap_ = 0u;
    pulse_cmd_count_ = 0u;
}

void EwFieldLatticeGpu::init(uint64_t seed) {
    (void)seed; // seed reserved for future deterministic phase offsets; not used to introduce randomness.
    const int n = (int)((size_t)gx_ * (size_t)gy_ * (size_t)gz_);
    const int blocks = (n + 255) / 256;
    ew_kernel_clear_f32<<<blocks, 256>>>((float*)d_E_prev_, n, 0.0f);
    ew_kernel_clear_f32<<<blocks, 256>>>((float*)d_E_curr_, n, 0.0f);
    ew_kernel_clear_f32<<<blocks, 256>>>((float*)d_E_next_, n, 0.0f);
    ew_kernel_clear_f32<<<blocks, 256>>>((float*)d_flux_, n, 0.0f);
    ew_kernel_clear_f32<<<blocks, 256>>>((float*)d_coherence_, n, 1.0f);
    ew_kernel_clear_f32<<<blocks, 256>>>((float*)d_curvature_, n, 0.0f);
    ew_kernel_clear_f32<<<blocks, 256>>>((float*)d_doppler_, n, 0.0f);
    ew_kernel_clear_u8<<<blocks, 256>>>((uint8_t*)d_density_u8_, n, 0u);
    ew_kernel_clear_u8<<<blocks, 256>>>((uint8_t*)d_bh_exclude_u8_, n, 0u);
    ew_kernel_clear_f32<<<blocks, 256>>>((float*)d_opA_, n, 0.0f);
    ew_kernel_clear_f32<<<blocks, 256>>>((float*)d_opB_, n, 0.0f);
    ew_cuda_check(cudaDeviceSynchronize(), "init sync");
    tick_index_ = 0;
    frame_seq_ = 0;
    pending_text_amp_q32_32_ = 0;
    pending_image_amp_q32_32_ = 0;
    pending_audio_amp_q32_32_ = 0;

    // Initial bind so learning metrics can immediately read measurable fields.
    (void)genesis::ew_learning_bind_world_lattice_cuda(
        (const float*)d_E_curr_,
        (const float*)d_flux_,
        (const float*)d_coherence_,
        (const float*)d_curvature_,
        (const float*)d_doppler_,
        (int)gx_, (int)gy_, (int)gz_
    );
}

void EwFieldLatticeGpu::upload_density_mask_u8(const uint8_t* mask_u8, size_t bytes) {
    const size_t n = (size_t)gx_ * (size_t)gy_ * (size_t)gz_;
    const size_t need = n * sizeof(uint8_t);
    if (!mask_u8 || bytes != need) throw std::runtime_error("density mask size mismatch");
    ew_cuda_check(cudaMemcpy(d_density_u8_, mask_u8, need, cudaMemcpyHostToDevice), "upload density");

    // Derive black hole exclusion mask on host deterministically and upload.
    // Matches the CPU lattice rule.
    constexpr uint8_t BH_CORE_DENSITY_U8 = 250u;
    constexpr int BH_RADIUS_MIN = 2;
    constexpr int BH_RADIUS_MAX = 12;
    std::vector<uint8_t> bh;
    bh.assign(n, 0u);

    auto idx3h = [this](int x, int y, int z) -> size_t {
        return (size_t)((z * (int)gy_ + y) * (int)gx_ + x);
    };

    for (uint32_t z = 0; z < gz_; ++z) {
        for (uint32_t y = 0; y < gy_; ++y) {
            for (uint32_t x = 0; x < gx_; ++x) {
                const uint8_t d = mask_u8[idx3h((int)x, (int)y, (int)z)];
                if (d < BH_CORE_DENSITY_U8) continue;
                int r = BH_RADIUS_MIN + (int)(d - BH_CORE_DENSITY_U8) * 2;
                if (r > BH_RADIUS_MAX) r = BH_RADIUS_MAX;
                const int r2 = r * r;
                const int x0 = (int)x;
                const int y0 = (int)y;
                const int z0 = (int)z;
                const int xmin = (x0 - r < 0) ? 0 : (x0 - r);
                const int xmax = (x0 + r >= (int)gx_) ? ((int)gx_ - 1) : (x0 + r);
                const int ymin = (y0 - r < 0) ? 0 : (y0 - r);
                const int ymax = (y0 + r >= (int)gy_) ? ((int)gy_ - 1) : (y0 + r);
                const int zmin = (z0 - r < 0) ? 0 : (z0 - r);
                const int zmax = (z0 + r >= (int)gz_) ? ((int)gz_ - 1) : (z0 + r);
                for (int zz = zmin; zz <= zmax; ++zz) {
                    const int dz = zz - z0;
                    for (int yy = ymin; yy <= ymax; ++yy) {
                        const int dy = yy - y0;
                        for (int xx = xmin; xx <= xmax; ++xx) {
                            const int dx = xx - x0;
                            const int dist2 = dx * dx + dy * dy + dz * dz;
                            if (dist2 <= r2) bh[idx3h(xx, yy, zz)] = 1u;
                        }
                    }
                }
            }
        }
    }
    ew_cuda_check(cudaMemcpy(d_bh_exclude_u8_, bh.data(), need, cudaMemcpyHostToDevice), "upload bh_exclude");
}

void EwFieldLatticeGpu::inject_text_amplitude_q32_32(int64_t amp_q32_32) {
    pending_text_amp_q32_32_ += amp_q32_32;
}

void EwFieldLatticeGpu::inject_image_amplitude_q32_32(int64_t amp_q32_32) {
    pending_image_amp_q32_32_ += amp_q32_32;
}

void EwFieldLatticeGpu::inject_audio_amplitude_q32_32(int64_t amp_q32_32) {
    pending_audio_amp_q32_32_ += amp_q32_32;
}

void EwFieldLatticeGpu::set_dt_seconds(float dt_seconds) {
    // Deterministic clamp to a safe range.
    if (!(dt_seconds > 0.0f)) return;
    if (dt_seconds < 1.0f / 4096.0f) dt_seconds = 1.0f / 4096.0f;
    if (dt_seconds > 1.0f / 10.0f) dt_seconds = 1.0f / 10.0f;
    dt_ = dt_seconds;
}

void EwFieldLatticeGpu::upload_pulse_inject_cmds(const EwPulseInjectCmd* cmds, size_t count) {
    if (!cmds || count == 0) {
        pulse_cmd_count_ = 0u;
        return;
    }
    if (!d_pulse_cmds_ || pulse_cmd_cap_ == 0u) {
        throw std::runtime_error("pulse cmd buffer not allocated");
    }
    const size_t take = (count > (size_t)pulse_cmd_cap_) ? (size_t)pulse_cmd_cap_ : count;
    ew_cuda_check(cudaMemcpy(d_pulse_cmds_, cmds, take * sizeof(EwPulseInjectCmd), cudaMemcpyHostToDevice), "upload pulse cmds");
    pulse_cmd_count_ = (uint32_t)take;
}

static float ew_reduce_sum_deterministic(float* d_scratch, const float* d_in, int n, bool abs_mode) {
    // First pass: reduce input into scratch.
    const int threads = 1024;
    int blocks = (n + threads - 1) / threads;
    if (blocks <= 0) return 0.0f;
    if (abs_mode) {
        ew_kernel_block_sum_abs<<<blocks, threads>>>(d_in, d_scratch, n);
    } else {
        ew_kernel_block_sum<<<blocks, threads>>>(d_in, d_scratch, n);
    }
    // Reduce scratch in-place until one value remains.
    int cur_n = blocks;
    while (cur_n > 1) {
        int cur_blocks = (cur_n + threads - 1) / threads;
        ew_kernel_block_sum<<<cur_blocks, threads>>>(d_scratch, d_scratch, cur_n);
        cur_n = cur_blocks;
    }
    float out = 0.0f;
    cudaMemcpy(&out, d_scratch, sizeof(float), cudaMemcpyDeviceToHost);
    return out;
}

void EwFieldLatticeGpu::step_one_tick() {
    tick_index_++;

    // Apply deterministic source injection at tick start.
    const float amp_text = q32_32_to_float(pending_text_amp_q32_32_);
    const float amp_image = q32_32_to_float(pending_image_amp_q32_32_);
    const float amp_audio = q32_32_to_float(pending_audio_amp_q32_32_);
    pending_text_amp_q32_32_ = 0;
    pending_image_amp_q32_32_ = 0;
    pending_audio_amp_q32_32_ = 0;

    // Optional merged pulse injection (authoritative carrier → lattice bridge).
    if (pulse_cmd_count_ != 0u) {
        const int threads = 256;
        const int blocks = (int)((pulse_cmd_count_ + (uint32_t)threads - 1u) / (uint32_t)threads);
        ew_kernel_inject_cmds<<<blocks, threads>>>((float*)d_E_curr_, (float*)d_flux_, (float*)d_doppler_,
                                                  (float*)d_opA_, (float*)d_opB_,
                                                  (const uint8_t*)d_bh_exclude_u8_,
                                                  (int)gx_, (int)gy_, (int)gz_,
                                                  (const EwPulseInjectCmd*)d_pulse_cmds_, (int)pulse_cmd_count_,
                                                  tick_index_);
        pulse_cmd_count_ = 0u;
    }

    // Center injection for modality amplitudes (deterministic).
    if (amp_text != 0.0f || amp_image != 0.0f || amp_audio != 0.0f) {
        ew_kernel_inject_center<<<1, 1>>>((float*)d_E_curr_, (float*)d_flux_, (float*)d_doppler_,
                                          (float*)d_opA_, (float*)d_opB_,
                                          (const uint8_t*)d_bh_exclude_u8_,
                                          (int)gx_, (int)gy_, (int)gz_, amp_text, amp_image, amp_audio, tick_index_);
    }

    const int n = (int)((size_t)gx_ * (size_t)gy_ * (size_t)gz_);
    const int blocks = (n + 255) / 256;
    ew_kernel_wave_step<<<blocks, 256>>>((const float*)d_E_prev_, (const float*)d_E_curr_, (float*)d_E_next_,
                                        (float*)d_flux_, (float*)d_coherence_, (float*)d_curvature_, (float*)d_doppler_,
                                        (float*)d_opA_, (float*)d_opB_,
                                        (const uint8_t*)d_density_u8_,
                                        (const uint8_t*)d_bh_exclude_u8_,
                                        (int)gx_, (int)gy_, (int)gz_, c2_, beta_, dt_);

    // Rotate buffers: E_prev <- E_curr, E_curr <- E_next
    void* tmp = d_E_prev_;
    d_E_prev_ = d_E_curr_;
    d_E_curr_ = d_E_next_;
    d_E_next_ = tmp;

    ew_cuda_check(cudaDeviceSynchronize(), "step sync");

    // Bind updated lattice views for GPU-only learning metrics.
    // This keeps the learning gate measurable against the latest committed
    // lattice fields without any CPU-side simulation.
    (void)genesis::ew_learning_bind_world_lattice_cuda(
        (const float*)d_E_curr_,
        (const float*)d_flux_,
        (const float*)d_coherence_,
        (const float*)d_curvature_,
        (const float*)d_doppler_,
        (int)gx_, (int)gy_, (int)gz_
    );
}

void EwFieldLatticeGpu::step_micro_ticks(uint32_t micro_ticks, bool bind_as_probe) {
    if (micro_ticks == 0u) return;

    // Use the currently pending modality amplitudes as a persistent injection
    // across the micro-tick window. This is the "parameters mold the lattice"
    // mechanism for the learning probe.
    const float amp_text = q32_32_to_float(pending_text_amp_q32_32_);
    const float amp_image = q32_32_to_float(pending_image_amp_q32_32_);
    const float amp_audio = q32_32_to_float(pending_audio_amp_q32_32_);
    pending_text_amp_q32_32_ = 0;
    pending_image_amp_q32_32_ = 0;
    pending_audio_amp_q32_32_ = 0;

    const int n = (int)((size_t)gx_ * (size_t)gy_ * (size_t)gz_);
    const int blocks = (n + 255) / 256;

    for (uint32_t k = 0u; k < micro_ticks; ++k) {
        tick_index_++;

        // Optional merged pulse injection.
        if (pulse_cmd_count_ != 0u) {
            const int threads = 256;
            const int b = (int)((pulse_cmd_count_ + (uint32_t)threads - 1u) / (uint32_t)threads);
            ew_kernel_inject_cmds<<<b, threads>>>(
                (float*)d_E_curr_, (float*)d_flux_, (float*)d_doppler_,
                (float*)d_opA_, (float*)d_opB_,
                (const uint8_t*)d_bh_exclude_u8_,
                (int)gx_, (int)gy_, (int)gz_,
                (const EwPulseInjectCmd*)d_pulse_cmds_, (int)pulse_cmd_count_,
                tick_index_
            );
            pulse_cmd_count_ = 0u;
        }

        // Persistent modality injection for learning probe molding.
        if (amp_text != 0.0f || amp_image != 0.0f || amp_audio != 0.0f) {
            ew_kernel_inject_center<<<1, 1>>>(
                (float*)d_E_curr_, (float*)d_flux_, (float*)d_doppler_,
                (float*)d_opA_, (float*)d_opB_,
                (const uint8_t*)d_bh_exclude_u8_,
                (int)gx_, (int)gy_, (int)gz_,
                amp_text, amp_image, amp_audio,
                tick_index_
            );
        }

        ew_kernel_wave_step<<<blocks, 256>>>(
            (const float*)d_E_prev_, (const float*)d_E_curr_, (float*)d_E_next_,
            (float*)d_flux_, (float*)d_coherence_, (float*)d_curvature_, (float*)d_doppler_,
            (float*)d_opA_, (float*)d_opB_,
            (const uint8_t*)d_density_u8_,
            (const uint8_t*)d_bh_exclude_u8_,
            (int)gx_, (int)gy_, (int)gz_, c2_, beta_, dt_
        );

        // Rotate buffers: E_prev <- E_curr, E_curr <- E_next
        void* tmp = d_E_prev_;
        d_E_prev_ = d_E_curr_;
        d_E_curr_ = d_E_next_;
        d_E_next_ = tmp;
    }

    ew_cuda_check(cudaDeviceSynchronize(), "micro step sync");

    if (bind_as_probe) {
        (void)genesis::ew_learning_bind_probe_lattice_cuda(
            (const float*)d_E_curr_,
            (const float*)d_flux_,
            (const float*)d_coherence_,
            (const float*)d_curvature_,
            (const float*)d_doppler_,
            (int)gx_, (int)gy_, (int)gz_
        );
    } else {
        (void)genesis::ew_learning_bind_world_lattice_cuda(
            (const float*)d_E_curr_,
            (const float*)d_flux_,
            (const float*)d_coherence_,
            (const float*)d_curvature_,
            (const float*)d_doppler_,
            (int)gx_, (int)gy_, (int)gz_
        );
    }
}

void EwFieldLatticeGpu::get_radiance_slice_bgra8(uint32_t slice_z, std::vector<uint8_t>& out_bgra8, EwFieldFrameHeader& out_hdr) {
    if (slice_z >= gz_) slice_z = gz_ / 2;
    const int n = (int)((size_t)gx_ * (size_t)gy_ * (size_t)gz_);

    // Deterministic global means.
    // mean_abs_time uses tick magnitude directly (D3 proxy) as per docs.
    const float mean_abs_time = (float)((double)tick_index_ * dt_);
    // mean_rho = mean(rho_E) where rho_E = D4^2 + D6^2.
    // Compute rho into L2 (used as scratch) and reduce with a deterministic tree.
    const int blocks = (n + 255) / 256;
    ew_kernel_compute_rho<<<blocks, 256>>>((const float*)d_flux_, (const float*)d_curvature_, (float*)d_L2_, n);
    const float sum_rho = ew_reduce_sum_deterministic((float*)d_reduce_scratch_, (const float*)d_L2_, n, false);
    const float mean_rho = sum_rho / (float)n;

    // Build slice.
    const int n_slice = (int)((size_t)gx_ * (size_t)gy_);
    const int blocks2 = (n_slice + 255) / 256;
    ew_kernel_build_radiance_slice<<<blocks2, 256>>>((const float*)d_flux_, (const float*)d_coherence_, (const float*)d_curvature_,
                                                    (const float*)d_doppler_,
                                                    (float*)d_L0_, (float*)d_L1_, (float*)d_L2_, (float*)d_L3_,
                                                    (uint8_t*)d_slice_bgra8_,
                                                    (int)gx_, (int)gy_, (int)gz_, (int)slice_z,
                                                    mean_abs_time, mean_rho);

    // Emergent bloom/interference: local wave summation over L0/L1/L3.
    // This is not a blur; it is a deterministic neighborhood interaction.
    const float bloom_gain = 0.50f;
    ew_kernel_emergent_bloom_add<<<blocks2, 256>>>((const float*)d_L0_, (const float*)d_L1_, (const float*)d_L3_,
                                                  (uint8_t*)d_slice_bgra8_,
                                                  (int)gx_, (int)gy_, (int)slice_z,
                                                  bloom_gain);
    ew_cuda_check(cudaDeviceSynchronize(), "slice sync");
    ew_cuda_check(cudaMemcpy(h_slice_bgra8_.data(), d_slice_bgra8_, (size_t)n_slice * 4, cudaMemcpyDeviceToHost), "slice copy");

    out_bgra8 = h_slice_bgra8_;

    frame_seq_++;
    out_hdr.frame_seq_begin = frame_seq_;
    out_hdr.tick_index = tick_index_;
    out_hdr.grid_x = gx_;
    out_hdr.grid_y = gy_;
    out_hdr.grid_z = gz_;
    out_hdr.slice_z = slice_z;
    out_hdr.frame_seq_end = frame_seq_;
}

// ---- Device views (read-only) ----
const float* EwFieldLatticeGpu::device_E_curr_f32() const { return (const float*)d_E_curr_; }
const float* EwFieldLatticeGpu::device_flux_f32() const { return (const float*)d_flux_; }
const float* EwFieldLatticeGpu::device_coherence_f32() const { return (const float*)d_coherence_; }
const float* EwFieldLatticeGpu::device_curvature_f32() const { return (const float*)d_curvature_; }
const float* EwFieldLatticeGpu::device_doppler_f32() const { return (const float*)d_doppler_; }

void EwFieldLatticeGpu::seed_from_world_subregion(const EwFieldLatticeGpu& world, uint32_t origin_x, uint32_t origin_y, uint32_t origin_z) {
    const dim3 block(8, 8, 8);
    const dim3 grid(
        (gx_ + block.x - 1) / block.x,
        (gy_ + block.y - 1) / block.y,
        (gz_ + block.z - 1) / block.z
    );

    ew_kernel_copy_subregion_f32<<<grid, block>>>(
        world.device_E_curr_f32(), (int)world.device_gx_u32(), (int)world.device_gy_u32(), (int)world.device_gz_u32(),
        (float*)d_E_curr_, (int)gx_, (int)gy_, (int)gz_,
        (int)origin_x, (int)origin_y, (int)origin_z
    );
    ew_kernel_copy_subregion_f32<<<grid, block>>>(
        world.device_flux_f32(), (int)world.device_gx_u32(), (int)world.device_gy_u32(), (int)world.device_gz_u32(),
        (float*)d_flux_, (int)gx_, (int)gy_, (int)gz_,
        (int)origin_x, (int)origin_y, (int)origin_z
    );
    ew_kernel_copy_subregion_f32<<<grid, block>>>(
        world.device_coherence_f32(), (int)world.device_gx_u32(), (int)world.device_gy_u32(), (int)world.device_gz_u32(),
        (float*)d_coherence_, (int)gx_, (int)gy_, (int)gz_,
        (int)origin_x, (int)origin_y, (int)origin_z
    );
    ew_kernel_copy_subregion_f32<<<grid, block>>>(
        world.device_curvature_f32(), (int)world.device_gx_u32(), (int)world.device_gy_u32(), (int)world.device_gz_u32(),
        (float*)d_curvature_, (int)gx_, (int)gy_, (int)gz_,
        (int)origin_x, (int)origin_y, (int)origin_z
    );
    ew_kernel_copy_subregion_f32<<<grid, block>>>(
        world.device_doppler_f32(), (int)world.device_gx_u32(), (int)world.device_gy_u32(), (int)world.device_gz_u32(),
        (float*)d_doppler_, (int)gx_, (int)gy_, (int)gz_,
        (int)origin_x, (int)origin_y, (int)origin_z
    );
    ew_cuda_check(cudaDeviceSynchronize(), "seed_from_world_subregion sync");
}
