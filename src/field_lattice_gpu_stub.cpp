#include "field_lattice.hpp"
#include "field_lattice_cpu.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

struct EwFieldLatticeFallbackState {
    EwFieldLatticeCpu cpu;
    std::vector<EwPulseInjectCmd> pending_cmds;
    std::vector<float> coherence_proxy;
    std::vector<float> curvature_proxy;
    std::vector<float> doppler_proxy;

    EwFieldLatticeFallbackState(uint32_t gx, uint32_t gy, uint32_t gz)
        : cpu(gx, gy, gz),
          coherence_proxy((size_t)gx * (size_t)gy * (size_t)gz, 0.0f),
          curvature_proxy((size_t)gx * (size_t)gy * (size_t)gz, 0.0f),
          doppler_proxy((size_t)gx * (size_t)gy * (size_t)gz, 0.0f) {}
};

std::unordered_map<const EwFieldLatticeGpu*, std::unique_ptr<EwFieldLatticeFallbackState>> g_fallback_states;
std::mutex g_fallback_states_mutex;

static EwFieldLatticeFallbackState* ew_fallback_state(const EwFieldLatticeGpu* self) {
    std::lock_guard<std::mutex> lock(g_fallback_states_mutex);
    auto it = g_fallback_states.find(self);
    return (it != g_fallback_states.end()) ? it->second.get() : nullptr;
}

static size_t ew_idx3(uint32_t gx, uint32_t gy, uint32_t x, uint32_t y, uint32_t z) {
    return ((size_t)z * (size_t)gy + (size_t)y) * (size_t)gx + (size_t)x;
}

static int16_t ew_float_to_q15_clamped(double value) {
    double clamped = std::max(-1.0, std::min(1.0, value));
    const long long scaled = llround(clamped * 32767.0);
    if (scaled < -32767ll) return (int16_t)-32767;
    if (scaled > 32767ll) return (int16_t)32767;
    return (int16_t)scaled;
}

static int64_t ew_float_to_q32_32(double value) {
    const double scale = 4294967296.0;
    return (int64_t)llround(value * scale);
}

static void ew_refresh_proxy_fields(EwFieldLatticeFallbackState& state, uint32_t gx, uint32_t gy, uint32_t gz) {
    const std::vector<float>& e_curr = state.cpu.E_curr();
    const std::vector<float>& flux = state.cpu.flux();
    const size_t n = (size_t)gx * (size_t)gy * (size_t)gz;
    if (state.coherence_proxy.size() != n) state.coherence_proxy.assign(n, 0.0f);
    if (state.curvature_proxy.size() != n) state.curvature_proxy.assign(n, 0.0f);
    if (state.doppler_proxy.size() != n) state.doppler_proxy.assign(n, 0.0f);

    for (uint32_t z = 0; z < gz; ++z) {
        const uint32_t zm = (z > 0u) ? (z - 1u) : z;
        const uint32_t zp = (z + 1u < gz) ? (z + 1u) : z;
        for (uint32_t y = 0; y < gy; ++y) {
            const uint32_t ym = (y > 0u) ? (y - 1u) : y;
            const uint32_t yp = (y + 1u < gy) ? (y + 1u) : y;
            for (uint32_t x = 0; x < gx; ++x) {
                const uint32_t xm = (x > 0u) ? (x - 1u) : x;
                const uint32_t xp = (x + 1u < gx) ? (x + 1u) : x;
                const size_t i = ew_idx3(gx, gy, x, y, z);
                const size_t i_xm = ew_idx3(gx, gy, xm, y, z);
                const size_t i_xp = ew_idx3(gx, gy, xp, y, z);
                const size_t i_ym = ew_idx3(gx, gy, x, ym, z);
                const size_t i_yp = ew_idx3(gx, gy, x, yp, z);
                const size_t i_zm = ew_idx3(gx, gy, x, y, zm);
                const size_t i_zp = ew_idx3(gx, gy, x, y, zp);

                const float e = e_curr[i];
                const float lap = e_curr[i_xm] + e_curr[i_xp] + e_curr[i_ym] + e_curr[i_yp] + e_curr[i_zm] + e_curr[i_zp] - 6.0f * e;
                const float abs_e = (e < 0.0f) ? -e : e;
                const float abs_flux = (flux[i] < 0.0f) ? -flux[i] : flux[i];
                const float coh = std::max(0.0f, 1.0f - std::min(1.0f, abs_e));

                state.coherence_proxy[i] = coh;
                state.curvature_proxy[i] = (lap < 0.0f) ? -lap : lap;
                state.doppler_proxy[i] = abs_flux;
            }
        }
    }
}

} // namespace

EwFieldLatticeGpu::EwFieldLatticeGpu(uint32_t gx, uint32_t gy, uint32_t gz) : gx_(gx), gy_(gy), gz_(gz) {
    auto state = std::make_unique<EwFieldLatticeFallbackState>(gx_, gy_, gz_);
    d_E_curr_ = const_cast<float*>(state->cpu.E_curr().data());
    d_flux_ = const_cast<float*>(state->cpu.flux().data());
    d_coherence_ = state->coherence_proxy.data();
    d_curvature_ = state->curvature_proxy.data();
    d_doppler_ = state->doppler_proxy.data();
    std::lock_guard<std::mutex> lock(g_fallback_states_mutex);
    g_fallback_states[this] = std::move(state);
}

EwFieldLatticeGpu::~EwFieldLatticeGpu() {
    std::lock_guard<std::mutex> lock(g_fallback_states_mutex);
    g_fallback_states.erase(this);
    d_E_curr_ = nullptr;
    d_flux_ = nullptr;
    d_coherence_ = nullptr;
    d_curvature_ = nullptr;
    d_doppler_ = nullptr;
}

void EwFieldLatticeGpu::init(uint64_t seed) {
    EwFieldLatticeFallbackState* state = ew_fallback_state(this);
    if (!state) return;
    state->cpu.init(seed);
    state->pending_cmds.clear();
    ew_refresh_proxy_fields(*state, gx_, gy_, gz_);
    d_E_curr_ = const_cast<float*>(state->cpu.E_curr().data());
    d_flux_ = const_cast<float*>(state->cpu.flux().data());
    d_coherence_ = state->coherence_proxy.data();
    d_curvature_ = state->curvature_proxy.data();
    d_doppler_ = state->doppler_proxy.data();
    tick_index_ = state->cpu.tick_index();
}

void EwFieldLatticeGpu::upload_density_mask_u8(const uint8_t* mask_u8, size_t bytes) {
    EwFieldLatticeFallbackState* state = ew_fallback_state(this);
    if (!state) return;
    state->cpu.upload_density_mask_u8(mask_u8, bytes);
}

void EwFieldLatticeGpu::inject_text_amplitude_q32_32(int64_t amp_q32_32) {
    EwFieldLatticeFallbackState* state = ew_fallback_state(this);
    if (!state) return;
    state->cpu.inject_text_amplitude_q32_32(amp_q32_32);
}

void EwFieldLatticeGpu::inject_image_amplitude_q32_32(int64_t amp_q32_32) {
    EwFieldLatticeFallbackState* state = ew_fallback_state(this);
    if (!state) return;
    state->cpu.inject_image_amplitude_q32_32(amp_q32_32);
}

void EwFieldLatticeGpu::inject_audio_amplitude_q32_32(int64_t amp_q32_32) {
    EwFieldLatticeFallbackState* state = ew_fallback_state(this);
    if (!state) return;
    state->cpu.inject_audio_amplitude_q32_32(amp_q32_32);
}

void EwFieldLatticeGpu::set_dt_seconds(float dt_seconds) {
    EwFieldLatticeFallbackState* state = ew_fallback_state(this);
    if (!state) return;
    state->cpu.set_dt_seconds(dt_seconds);
}

void EwFieldLatticeGpu::upload_pulse_inject_cmds(const EwPulseInjectCmd* cmds, size_t count) {
    EwFieldLatticeFallbackState* state = ew_fallback_state(this);
    if (!state) return;
    state->pending_cmds.clear();
    if (!cmds || count == 0u) return;
    state->pending_cmds.assign(cmds, cmds + count);
}

void EwFieldLatticeGpu::step_one_tick() {
    EwFieldLatticeFallbackState* state = ew_fallback_state(this);
    if (!state) return;

    double text_sum = 0.0;
    double image_sum = 0.0;
    double audio_sum = 0.0;
    for (const EwPulseInjectCmd& cmd : state->pending_cmds) {
        text_sum += (double)cmd.amp_text;
        image_sum += (double)cmd.amp_image;
        audio_sum += (double)cmd.amp_audio;
    }
    state->pending_cmds.clear();

    if (text_sum != 0.0) state->cpu.inject_text_amplitude_q32_32(ew_float_to_q32_32(text_sum));
    if (image_sum != 0.0) state->cpu.inject_image_amplitude_q32_32(ew_float_to_q32_32(image_sum));
    if (audio_sum != 0.0) state->cpu.inject_audio_amplitude_q32_32(ew_float_to_q32_32(audio_sum));

    state->cpu.step_one_tick();
    ew_refresh_proxy_fields(*state, gx_, gy_, gz_);
    tick_index_ = state->cpu.tick_index();
}

void EwFieldLatticeGpu::step_micro_ticks(uint32_t micro_ticks, bool bind_as_probe) {
    (void)bind_as_probe;
    for (uint32_t i = 0u; i < micro_ticks; ++i) {
        step_one_tick();
    }
}

const float* EwFieldLatticeGpu::device_E_curr_f32() const { return static_cast<const float*>(d_E_curr_); }
const float* EwFieldLatticeGpu::device_flux_f32() const { return static_cast<const float*>(d_flux_); }
const float* EwFieldLatticeGpu::device_coherence_f32() const { return static_cast<const float*>(d_coherence_); }
const float* EwFieldLatticeGpu::device_curvature_f32() const { return static_cast<const float*>(d_curvature_); }
const float* EwFieldLatticeGpu::device_doppler_f32() const { return static_cast<const float*>(d_doppler_); }

void EwFieldLatticeGpu::seed_from_world_subregion(const EwFieldLatticeGpu& world, uint32_t origin_x, uint32_t origin_y, uint32_t origin_z) {
    (void)origin_x;
    (void)origin_y;
    (void)origin_z;
    EwFieldLatticeFallbackState* dst = ew_fallback_state(this);
    EwFieldLatticeFallbackState* src = ew_fallback_state(&world);
    if (!dst) return;
    dst->cpu.init(world.tick_index());
    if (!src) return;

    const std::vector<float>& src_e = src->cpu.E_curr();
    const size_t n = src_e.size();
    if (n == 0u) return;
    double avg_abs = 0.0;
    for (float v : src_e) avg_abs += (v < 0.0f) ? -(double)v : (double)v;
    avg_abs /= (double)n;
    if (avg_abs > 0.0) {
        dst->cpu.inject_text_amplitude_q32_32(ew_float_to_q32_32(avg_abs));
        dst->cpu.step_one_tick();
        ew_refresh_proxy_fields(*dst, gx_, gy_, gz_);
    }
}

void EwFieldLatticeGpu::get_radiance_slice_bgra8(uint32_t slice_z, std::vector<uint8_t>& out_bgra8, EwFieldFrameHeader& out_hdr) {
    EwFieldLatticeFallbackState* state = ew_fallback_state(this);
    if (!state) {
        out_bgra8.clear();
        out_hdr = EwFieldFrameHeader{};
        return;
    }
    state->cpu.get_radiance_slice_bgra8(slice_z, out_bgra8, out_hdr);
}

EwBoundarySampleQ15 EwFieldLatticeGpu::sample_boundary_means_q15_box(uint32_t center_x, uint32_t center_y, uint32_t center_z,
                                                                     uint32_t radius_x, uint32_t radius_y, uint32_t radius_z) const {
    EwBoundarySampleQ15 out{};
    EwFieldLatticeFallbackState* state = ew_fallback_state(this);
    if (!state || gx_ == 0u || gy_ == 0u || gz_ == 0u) return out;

    const std::vector<float>& e_curr = state->cpu.E_curr();
    const std::vector<float>& flux = state->cpu.flux();
    if (e_curr.empty() || flux.empty()) return out;

    const uint32_t x0 = (center_x > radius_x) ? (center_x - radius_x) : 0u;
    const uint32_t y0 = (center_y > radius_y) ? (center_y - radius_y) : 0u;
    const uint32_t z0 = (center_z > radius_z) ? (center_z - radius_z) : 0u;
    const uint32_t x1 = std::min(gx_ - 1u, center_x + radius_x);
    const uint32_t y1 = std::min(gy_ - 1u, center_y + radius_y);
    const uint32_t z1 = std::min(gz_ - 1u, center_z + radius_z);

    double sum_e = 0.0;
    double sum_flux = 0.0;
    double sum_flux_grad = 0.0;
    double sum_coh = 0.0;
    double sum_curv = 0.0;
    double sum_dopp = 0.0;
    uint64_t count = 0u;

    for (uint32_t z = z0; z <= z1; ++z) {
        const uint32_t zm = (z > 0u) ? (z - 1u) : z;
        const uint32_t zp = (z + 1u < gz_) ? (z + 1u) : z;
        for (uint32_t y = y0; y <= y1; ++y) {
            const uint32_t ym = (y > 0u) ? (y - 1u) : y;
            const uint32_t yp = (y + 1u < gy_) ? (y + 1u) : y;
            for (uint32_t x = x0; x <= x1; ++x) {
                const uint32_t xm = (x > 0u) ? (x - 1u) : x;
                const uint32_t xp = (x + 1u < gx_) ? (x + 1u) : x;
                const size_t i = ew_idx3(gx_, gy_, x, y, z);
                const size_t i_xm = ew_idx3(gx_, gy_, xm, y, z);
                const size_t i_xp = ew_idx3(gx_, gy_, xp, y, z);
                const size_t i_ym = ew_idx3(gx_, gy_, x, ym, z);
                const size_t i_yp = ew_idx3(gx_, gy_, x, yp, z);
                const size_t i_zm = ew_idx3(gx_, gy_, x, y, zm);
                const size_t i_zp = ew_idx3(gx_, gy_, x, y, zp);

                const double e = (double)e_curr[i];
                const double f = (double)flux[i];
                const double lap = (double)e_curr[i_xm] + (double)e_curr[i_xp] +
                                   (double)e_curr[i_ym] + (double)e_curr[i_yp] +
                                   (double)e_curr[i_zm] + (double)e_curr[i_zp] - 6.0 * e;
                const double grad = (std::abs((double)flux[i_xp] - (double)flux[i_xm]) +
                                     std::abs((double)flux[i_yp] - (double)flux[i_ym]) +
                                     std::abs((double)flux[i_zp] - (double)flux[i_zm])) / 3.0;
                const double coh = std::max(0.0, 1.0 - std::min(1.0, std::abs(e)));

                sum_e += e;
                sum_flux += f;
                sum_flux_grad += grad;
                sum_coh += coh;
                sum_curv += std::abs(lap);
                sum_dopp += std::abs(f);
                ++count;
            }
        }
    }

    if (count == 0u) return out;
    const double inv = 1.0 / (double)count;
    out.e_curr_q15 = ew_float_to_q15_clamped(sum_e * inv);
    out.flux_q15 = ew_float_to_q15_clamped(sum_flux * inv);
    out.flux_grad_q15 = ew_float_to_q15_clamped(sum_flux_grad * inv);
    out.coherence_q15 = ew_float_to_q15_clamped(sum_coh * inv);
    out.curvature_q15 = ew_float_to_q15_clamped(sum_curv * inv);
    out.doppler_q15 = ew_float_to_q15_clamped(sum_dopp * inv);
    return out;
}

void EwFieldLatticeGpu::clear_object_imprint() {}

bool EwFieldLatticeGpu::accumulate_object_imprint5_q15(const uint8_t* occ_u8, const int16_t* phi_q15_s16,
                                                       uint32_t ogx, uint32_t ogy, uint32_t ogz,
                                                       uint32_t world_center_x, uint32_t world_center_y, uint32_t world_center_z,
                                                       float e_scale, float coherence_scale,
                                                       float flux_scale, float curvature_scale, float doppler_scale) {
    (void)occ_u8;
    (void)phi_q15_s16;
    (void)ogx;
    (void)ogy;
    (void)ogz;
    (void)world_center_x;
    (void)world_center_y;
    (void)world_center_z;
    (void)e_scale;
    (void)coherence_scale;
    (void)flux_scale;
    (void)curvature_scale;
    (void)doppler_scale;
    return false;
}

void EwFieldLatticeGpu::apply_object_imprint_to_fields() {}
