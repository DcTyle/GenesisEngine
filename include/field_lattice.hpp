#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// GPU-first 3D lattice that carries co-located manifolds per voxel.
// The 3D domain is the spatial manifold (x,y,z). Extra dimensions (D3..D8)
// are realized as additional per-voxel field layers (flux, coherence, curvature,
// doppler, etc.).
//
// This module provides an end-to-end Expo-ready UE viewport contract:
// UE5 is the 3D projected viewport; UE Tick dispatches step calls and pulls
// the latest committed field slice for display. No file persistence.

struct EwFieldFrameHeader {
    uint64_t frame_seq_begin;
    uint64_t tick_index;
    uint32_t grid_x;
    uint32_t grid_y;
    uint32_t grid_z;
    uint32_t slice_z;
    uint64_t frame_seq_end;
};

// Pulse injection command for the GPU lattice.
// This is the concrete bridge between the substrate carrier pulses and the
// lattice evolution step. Commands are expected to be merged/deduped on the
// host so that each (x,y,z) target is unique within a tick; this avoids
// nondeterministic atomic accumulation on device.
struct EwPulseInjectCmd {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    float amp_text;
    float amp_image;
    float amp_audio;
};

// Centered signed Q15-ish means sampled from the world lattice in a clamped box.
// These values serve as boundary conditions for object-local sublattice evolution.
struct EwBoundarySampleQ15 {
    int16_t e_curr_q15 = 0;
    int16_t flux_q15 = 0;
    // Q0.15 magnitude of the local flux gradient (0..32767). Used for
    // boundary-coupled diffusion/noise and energy dominance rules.
    int16_t flux_grad_q15 = 0;
    int16_t coherence_q15 = 0;
    int16_t curvature_q15 = 0;
    int16_t doppler_q15 = 0;
};

class EwFieldLatticeGpu {
public:
    EwFieldLatticeGpu(uint32_t gx, uint32_t gy, uint32_t gz);
    ~EwFieldLatticeGpu();

    void init(uint64_t seed);

    // Geometry constraint injection: density mask is a voxel grid (0..255).
    void upload_density_mask_u8(const uint8_t* mask_u8, size_t bytes);

    // Canonical modality injection amplitudes (Q32.32). Binding is fixed:
    // TEXT->x, IMAGE->y, AUDIO->z.
    void inject_text_amplitude_q32_32(int64_t amp_q32_32);
    void inject_image_amplitude_q32_32(int64_t amp_q32_32);
    void inject_audio_amplitude_q32_32(int64_t amp_q32_32);

    // Override dt for the wave step (seconds). Must be deterministic.
    void set_dt_seconds(float dt_seconds);

    // Upload merged pulse injection commands for the upcoming tick.
    // The data is copied to device and consumed on the next step_one_tick().
    void upload_pulse_inject_cmds(const EwPulseInjectCmd* cmds, size_t count);

    void step_one_tick();

    // Run N micro-ticks without presenting frames. Intended for learning probe
    // evolution where parameters must literally mold the lattice state via
    // repeated constrained evolution steps.
    //
    // bind_as_probe: if true, binds the resulting device views as the learning
    // probe lattice; otherwise binds as the authoritative world lattice.
    void step_micro_ticks(uint32_t micro_ticks, bool bind_as_probe);

    // ---- Device views (read-only) ----
    // These accessors expose the authoritative device pointers for internal GPU
    // subsystems (learning probe, metric sampling). They are intentionally
    // read-only at the type level; mutation occurs only via lattice kernels.
    const float* device_E_curr_f32() const;
    const float* device_flux_f32() const;
    const float* device_coherence_f32() const;
    const float* device_curvature_f32() const;
    const float* device_doppler_f32() const;
    uint32_t device_gx_u32() const { return gx_; }
    uint32_t device_gy_u32() const { return gy_; }
    uint32_t device_gz_u32() const { return gz_; }

    // ---- Learning probe support ----
    // Seed this lattice from a subregion of a world lattice. This is used for
    // the learning sandbox so parameter molding/evolution does not perturb the
    // world lattice, while initial conditions remain grounded in measurable
    // world observables.
    void seed_from_world_subregion(const EwFieldLatticeGpu& world, uint32_t origin_x, uint32_t origin_y, uint32_t origin_z);

    // Visualization slice output (BGRA8), derived from radiance channels.
    void get_radiance_slice_bgra8(uint32_t slice_z, std::vector<uint8_t>& out_bgra8, EwFieldFrameHeader& out_hdr);

    uint64_t tick_index() const { return tick_index_; }
    uint32_t grid_x() const { return gx_; }
    uint32_t grid_y() const { return gy_; }
    uint32_t grid_z() const { return gz_; }

    // Deterministic sampling support for object↔world boundary exchange.
    // Returns centered signed Q15-ish means of multiple world fields over a clamped box.
    // These values are intended to bias object-local boundary evolution.
    EwBoundarySampleQ15 sample_boundary_means_q15_box(uint32_t center_x, uint32_t center_y, uint32_t center_z,
                                                      uint32_t radius_x, uint32_t radius_y, uint32_t radius_z) const;

    // ------------------------------------------------------------------
    // Object→world bounded writeback
    //
    // To preserve determinism, object writeback accumulates into integer
    // imprint buffers (atomicAdd on int32 is order-independent). The imprint
    // is then applied once per cell into the floating world fields.
    // ------------------------------------------------------------------
    void clear_object_imprint();
    // Accumulate a bounded object-local imprint into world-sized integer buffers.
    // The imprint is applied later in a single per-cell pass to preserve determinism.
    // Scales are per-field multipliers converting centered Q15-ish sums into world float units.
    bool accumulate_object_imprint5_q15(const uint8_t* occ_u8, const int16_t* phi_q15_s16,
                                        uint32_t ogx, uint32_t ogy, uint32_t ogz,
                                        uint32_t world_center_x, uint32_t world_center_y, uint32_t world_center_z,
                                        float e_scale, float coherence_scale,
                                        float flux_scale, float curvature_scale, float doppler_scale);
    void apply_object_imprint_to_fields();

private:
    uint32_t gx_ = 0, gy_ = 0, gz_ = 0;
    uint64_t tick_index_ = 0;
    uint64_t frame_seq_ = 0;

    float dt_ = 1.0f / 60.0f;
    float c2_ = 1.0f;
    float beta_ = 0.015f;

    int64_t pending_text_amp_q32_32_ = 0;
    int64_t pending_image_amp_q32_32_ = 0;
    int64_t pending_audio_amp_q32_32_ = 0;

    void* d_E_prev_ = nullptr;
    void* d_E_curr_ = nullptr;
    void* d_E_next_ = nullptr;
    void* d_flux_ = nullptr;
    void* d_coherence_ = nullptr;
    void* d_curvature_ = nullptr;
    void* d_doppler_ = nullptr;
    void* d_density_u8_ = nullptr;

    // Black hole event-horizon exclusion mask (derived from density).
    // Voxels with bh_exclude != 0 are not evolved by the wave step.
    void* d_bh_exclude_u8_ = nullptr;

    // Operator vector fields (phase-dynamics encoded compute controls).
    // These are not UE-side parameters; they live inside the substrate lattice.
    // opA/opB modulate transport/damping deterministically per-voxel.
    void* d_opA_ = nullptr;
    void* d_opB_ = nullptr;

    void* d_L0_ = nullptr;
    void* d_L1_ = nullptr;
    void* d_L2_ = nullptr;
    void* d_L3_ = nullptr;

    void* d_slice_bgra8_ = nullptr;
    std::vector<uint8_t> h_slice_bgra8_;

    void* d_reduce_scratch_ = nullptr;
    uint32_t reduce_scratch_floats_ = 0;

    // Host-merged pulse injection command buffer.
    void* d_pulse_cmds_ = nullptr;
    uint32_t pulse_cmd_cap_ = 0;
    uint32_t pulse_cmd_count_ = 0;

    // Persistent device-side sampling scratch (integer sums + count).
    void* d_sample_sums_i64_ = nullptr; // int64_t[5]
    void* d_sample_cnt_u64_ = nullptr;  // uint64_t

    // Persistent device-side object imprint scratch (integer sums per cell).
    void* d_obj_imprint_e_i32_ = nullptr;   // int32_t[gx*gy*gz]
    void* d_obj_imprint_coh_i32_ = nullptr; // int32_t[gx*gy*gz]
    void* d_obj_imprint_flux_i32_ = nullptr; // int32_t[gx*gy*gz]
    void* d_obj_imprint_curv_i32_ = nullptr; // int32_t[gx*gy*gz]
    void* d_obj_imprint_dopp_i32_ = nullptr; // int32_t[gx*gy*gz]

    void alloc_buffers_();
    void free_buffers_();
};
