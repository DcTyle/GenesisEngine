#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "field_lattice.hpp"

// CPU implementation of the same deterministic field lattice contract.
// This exists so the UE plugin can compile without requiring the build system
// to compile CUDA sources inside the UE module.
//
// The field dynamics calculations remain inside the substrate implementation.

struct Ew9DState {
    float d[9] = {0}; // 9D state: [x, y, z, temporal, coherence, flux, phantom, aether, nexus]
};

class EwFieldLatticeCpu {
public:
    EwFieldLatticeCpu(uint32_t gx, uint32_t gy, uint32_t gz);

    void init(uint64_t seed);
    void upload_density_mask_u8(const uint8_t* mask_u8, size_t bytes);
    void inject_text_amplitude_q32_32(int64_t amp_q32_32);
    void inject_image_amplitude_q32_32(int64_t amp_q32_32);
    void inject_audio_amplitude_q32_32(int64_t amp_q32_32);
    void set_dt_seconds(float dt_seconds) { dt_ = dt_seconds; }
    void step_one_tick();
    void get_radiance_slice_bgra8(uint32_t slice_z, std::vector<uint8_t>& out_bgra8, EwFieldFrameHeader& out_hdr);

    // Observability hooks for deterministic test harnesses.
    const std::vector<Ew9DState>& state_curr() const { return state_curr_; }

    uint64_t tick_index() const { return tick_index_; }

private:
    uint32_t gx_ = 0, gy_ = 0, gz_ = 0;
    uint64_t tick_index_ = 0;
    uint64_t frame_seq_ = 0;

    float dt_ = 1.0f / 60.0f;

    int64_t pending_text_amp_q32_32_ = 0;
    int64_t pending_image_amp_q32_32_ = 0;
    int64_t pending_audio_amp_q32_32_ = 0;

    std::vector<Ew9DState> state_prev_;
    std::vector<Ew9DState> state_curr_;
    std::vector<Ew9DState> state_next_;
    std::vector<uint8_t> density_u8_;
    std::vector<uint8_t> bh_exclude_u8_;

    int idx3_(int x, int y, int z) const;
};
