#include "field_lattice.hpp"
#include "learning_gate_cuda.hpp"

#if !defined(EW_ENABLE_CUDA) || (EW_ENABLE_CUDA==0)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

EwFieldLatticeGpu::EwFieldLatticeGpu(uint32_t gx, uint32_t gy, uint32_t gz)
    : gx_(gx), gy_(gy), gz_(gz) {}

EwFieldLatticeGpu::~EwFieldLatticeGpu() = default;

void EwFieldLatticeGpu::alloc_buffers_() {}
void EwFieldLatticeGpu::free_buffers_() {}

void EwFieldLatticeGpu::init(uint64_t seed) {
    tick_index_ = 0;
    frame_seq_ = seed;
}

void EwFieldLatticeGpu::upload_density_mask_u8(const uint8_t* /*mask_u8*/, size_t /*bytes*/) {}
void EwFieldLatticeGpu::inject_text_amplitude_q32_32(int64_t amp_q32_32) { pending_text_amp_q32_32_ = amp_q32_32; }
void EwFieldLatticeGpu::inject_image_amplitude_q32_32(int64_t amp_q32_32) { pending_image_amp_q32_32_ = amp_q32_32; }
void EwFieldLatticeGpu::inject_audio_amplitude_q32_32(int64_t amp_q32_32) { pending_audio_amp_q32_32_ = amp_q32_32; }
void EwFieldLatticeGpu::set_dt_seconds(float dt_seconds) { dt_ = dt_seconds; }
void EwFieldLatticeGpu::upload_pulse_inject_cmds(const EwPulseInjectCmd* /*cmds*/, size_t count) { pulse_cmd_count_ = (uint32_t)count; }

void EwFieldLatticeGpu::step_one_tick() {
    ++tick_index_;
    ++frame_seq_;
}

void EwFieldLatticeGpu::step_micro_ticks(uint32_t micro_ticks, bool /*bind_as_probe*/) {
    tick_index_ += (uint64_t)micro_ticks;
    frame_seq_ += (uint64_t)micro_ticks;
}

const float* EwFieldLatticeGpu::device_E_curr_f32() const { return nullptr; }
const float* EwFieldLatticeGpu::device_flux_f32() const { return nullptr; }
const float* EwFieldLatticeGpu::device_coherence_f32() const { return nullptr; }
const float* EwFieldLatticeGpu::device_curvature_f32() const { return nullptr; }
const float* EwFieldLatticeGpu::device_doppler_f32() const { return nullptr; }

void EwFieldLatticeGpu::seed_from_world_subregion(const EwFieldLatticeGpu& /*world*/,
                                                   uint32_t /*origin_x*/,
                                                   uint32_t /*origin_y*/,
                                                   uint32_t /*origin_z*/) {}

void EwFieldLatticeGpu::get_radiance_slice_bgra8(uint32_t slice_z,
                                                 std::vector<uint8_t>& out_bgra8,
                                                 EwFieldFrameHeader& out_hdr) {
    const uint32_t gx = (gx_ == 0u) ? 1u : gx_;
    const uint32_t gy = (gy_ == 0u) ? 1u : gy_;
    const size_t pixels = (size_t)gx * (size_t)gy;
    out_bgra8.assign(pixels * 4u, 0u);
    out_hdr.frame_seq_begin = frame_seq_;
    out_hdr.tick_index = tick_index_;
    out_hdr.grid_x = gx_;
    out_hdr.grid_y = gy_;
    out_hdr.grid_z = gz_;
    out_hdr.slice_z = (gz_ == 0u) ? 0u : std::min(slice_z, gz_ - 1u);
    out_hdr.frame_seq_end = frame_seq_;
}

EwBoundarySampleQ15 EwFieldLatticeGpu::sample_boundary_means_q15_box(uint32_t /*center_x*/,
                                                                      uint32_t /*center_y*/,
                                                                      uint32_t /*center_z*/,
                                                                      uint32_t /*radius_x*/,
                                                                      uint32_t /*radius_y*/,
                                                                      uint32_t /*radius_z*/) const {
    return EwBoundarySampleQ15{};
}

void EwFieldLatticeGpu::clear_object_imprint() {}

bool EwFieldLatticeGpu::accumulate_object_imprint5_q15(const uint8_t* /*occ_u8*/,
                                                        const int16_t* /*phi_q15_s16*/,
                                                        uint32_t /*ogx*/,
                                                        uint32_t /*ogy*/,
                                                        uint32_t /*ogz*/,
                                                        uint32_t /*world_center_x*/,
                                                        uint32_t /*world_center_y*/,
                                                        uint32_t /*world_center_z*/,
                                                        float /*e_scale*/,
                                                        float /*coherence_scale*/,
                                                        float /*flux_scale*/,
                                                        float /*curvature_scale*/,
                                                        float /*doppler_scale*/) {
    return false;
}

void EwFieldLatticeGpu::apply_object_imprint_to_fields() {}

namespace genesis {

bool ew_learning_gate_tick_cuda(MetricTask* /*task_host*/,
                                uint64_t /*canonical_tick_u64*/,
                                uint64_t /*tries_this_tick_u64*/,
                                uint32_t /*steps_this_tick_u32*/) {
    return false;
}

bool ew_learning_bind_world_lattice_cuda(const float* /*d_E_curr*/,
                                         const float* /*d_flux*/,
                                         const float* /*d_coherence*/,
                                         const float* /*d_curvature*/,
                                         const float* /*d_doppler*/,
                                         int /*gx*/,
                                         int /*gy*/,
                                         int /*gz*/) {
    return false;
}

bool ew_learning_bind_probe_lattice_cuda(const float* /*d_E_curr*/,
                                         const float* /*d_flux*/,
                                         const float* /*d_coherence*/,
                                         const float* /*d_curvature*/,
                                         const float* /*d_doppler*/,
                                         int /*gx*/,
                                         int /*gy*/,
                                         int /*gz*/) {
    return false;
}

} // namespace genesis

#endif
