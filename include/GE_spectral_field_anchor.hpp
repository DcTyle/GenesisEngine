#pragma once

#include <cstdint>
#include "GE_coherence_packets.hpp"
#include "GE_temporal_summaries.hpp"
#include "GE_actuation_packet.hpp"

// Spectral field anchor is an authoritative substrate "microprocessor" for a region.
// This is a minimal deterministic bootstrap implementation: a bounded bank of Fourier modes.
//
// Design rules:
//  - fixed-size arrays, fixed-point integers only in authoritative state
//  - explicit caps/budgets
//  - deterministic ordering (stable sorts where needed)

static const uint32_t EW_SPECTRAL_N = 64; // power-of-two
static const uint32_t EW_SPECTRAL_HOOK_INBOX_MAX = 16;
static const uint32_t EW_SPECTRAL_TRAJ_SLOTS = 8;

struct EwSpectralComplexQ32_32 {
    int64_t re_q32_32 = 0;
    int64_t im_q32_32 = 0;
};

struct EwSpectralFieldAnchorState {
    // Grid / profile.
    uint32_t n_u32 = EW_SPECTRAL_N;
    uint32_t log2n_u32 = 6;
    uint32_t twiddle_profile_u32 = 0;
    uint32_t pad0 = 0;

    // Region definition for bounded integration.
    // Center in meters (Q16.16) and radius in meters (Q16.16).
    int32_t region_center_q16_16[3] = {0, 0, 0};
    int32_t region_radius_m_q16_16 = (int32_t)(64 * 65536); // 64m default

    // Spectral state: phi_hat(k).
    EwSpectralComplexQ32_32 phi_hat[EW_SPECTRAL_N];

    // Forcing accumulator bins (cleared each tick by the operator).
    EwSpectralComplexQ32_32 forcing_hat[EW_SPECTRAL_N];

    // Calibration scalars.
    int64_t dt_scale_q32_32 = (1LL << 32);
    int64_t viscosity_bias_q32_32 = 0;
    uint16_t noise_floor_q15 = 0;
    uint16_t min_delta_q15 = 8;
    uint32_t fanout_budget_u32 = 32;

    // Learning coupling (Q0.15) driven by influx (reverse leakage) via coherence bus.
    uint16_t learning_coupling_q15 = 0;
    uint16_t pad_learning_u16 = 0;

    // Temporal coupling operator parameters (small, fixed-size).
    // These define how actuation impulses are interpreted and are updated via
    // collapse-like operator replacement based on temporal residuals.
    uint16_t op_gain_q15 = 32767;       // multiplicative gain on forcing (Q0.15)
    uint16_t op_phase_bias_q15 = 0;     // phase bias (Q0.15 turns)
    uint16_t op_band_w_q15[8] = {32767, 0, 0, 0, 0, 0, 0, 0};


    // Boundary coupling factors from voxel collision boundaries (Q0.15).
    uint16_t boundary_strength_mean_q15 = 0;
    uint16_t permeability_mean_q15 = 0;
    // Dominant interface axis and anisotropy strength from voxel boundary coupling.
    uint8_t boundary_axis_dom_u8 = 0;
    uint8_t pad_boundary_u8 = 0;
    uint16_t boundary_anisotropy_q15 = 0;

    // Calibration state machine.
    // 0=off, 1=running, 2=done (latched).
    uint8_t calibration_mode_u8 = 1;
    uint8_t calibration_profile_u8 = 0;
    uint8_t hold_tick_u8 = 0;
    uint8_t pad1_u8 = 0;

    uint32_t calibration_ticks_remaining_u32 = 240; // ~0.66s at 360Hz
    uint64_t cal_energy_sum_u64 = 0;
    uint64_t cal_leak_abs_sum_u64 = 0;
    uint32_t cal_count_u32 = 0;
    uint32_t pad2_u32 = 0;

    // Leakage measurement (published to coherence bus).
    int64_t leakage_q32_32 = 0;
    uint8_t leakage_band_u8 = 0;
    uint8_t leakage_pending_u8 = 0;
    uint16_t pad3 = 0;
    EwId9 leakage_id9{};

    // Explicit measurement lanes for temporal coupling.
    EwIntentSummary intent_summary;
    EwMeasuredSummary measured_summary;
    EwResidualSummary temporal_residual;
    EwPulseMeasuredSummary pulse_measured;
    // Optional GPU-written sidecar for pulse-measured lane (future).
    EwPulseMeasuredSummary pulse_measured_gpu_sidecar;
    uint8_t pulse_measured_gpu_valid_u8 = 0;
    uint8_t pad_pulse_measured0_u8 = 0;
    uint16_t pad_pulse_measured1_u16 = 0;
    // Pulse intent sidecar (control-plane) + bounded history window.
    EwPulseIntentSummary pulse_intent;
    EwPulseIntentSummary pulse_intent_ring[EW_PULSE_INTENT_RING_W];
    uint32_t pulse_intent_ring_head_u32 = 0;
    uint32_t pulse_intent_ring_count_u32 = 0;
    // Bounded history window for pulse-measured summaries.
    EwPulseMeasuredSummary pulse_measured_ring[EW_PULSE_MEASURED_RING_W];
    uint32_t pulse_measured_ring_head_u32 = 0;
    uint32_t pulse_measured_ring_count_u32 = 0;

    // Bounded trajectory/actuation slots (overwrite per tick).
    EwActuationPacket actuation_slots[EW_SPECTRAL_TRAJ_SLOTS];
    uint32_t actuation_count_u32 = 0;

    // Hook inbox (filled by coherence bus op).
    EwHookPacket hook_inbox[EW_SPECTRAL_HOOK_INBOX_MAX];
    uint32_t hook_inbox_count_u32 = 0;

    // Per-tick summary for visualization.
    uint16_t energy_mean_q15 = 0;
    uint16_t energy_peak_q15 = 0;
    uint16_t leakage_abs_q15 = 0;
    uint16_t pad5_u16 = 0;
    uint32_t last_step_committed_u32 = 0;
};
