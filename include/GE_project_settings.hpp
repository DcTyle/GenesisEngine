#pragma once

#include <cstdint>
#include <string>

// Strongly-typed project settings (engine-level). Loaded at boot, accessible
// to the AI substrate via anchors/packets, and included in deterministic snapshots.

struct PsRendering {
    // D-noise scaling derived from harmonics.
    // gain_q16_16 scales harmonic_mean into a [0..1] dominance factor.
    int32_t dnoise_gain_q16_16 = (int32_t)(1.0f * 65536.0f);
    int32_t dnoise_bias_q16_16 = 0;
};

struct PsPhysics {
    // Placeholder for physics knobs that are already defined in the codebase.
    // Must not be empty; keep minimal stable fields.
    int32_t contact_pressure_gain_q16_16 = (int32_t)(1.0f * 65536.0f);
};

struct PsCamera {
    int32_t default_focal_length_mm_q16_16 = (int32_t)(50 * 65536);
    int32_t default_aperture_f_q16_16 = (int32_t)(28 * 65536 / 10);
    int32_t default_exposure_ev_q16_16 = 0;

    // Substrate-side camera control parameters (used by input mapping op).
    // These are NOT applied directly by UI/input code; the substrate maps
    // raw input events into camera anchor changes using these settings.
    int32_t move_speed_mps_q16_16 = (int32_t)(2 * 65536);
    int32_t move_step_m_q16_16 = (int32_t)(25 * 65536 / 100); // 0.25m
    int32_t look_sens_rad_per_unit_q16_16 = (int32_t)(0.002f * 65536.0f);
    int32_t zoom_step_m_q16_16 = (int32_t)(10 * 65536 / 100); // 0.10m
};

struct PsInput {
    // Input bindings file path (relative).
    std::string bindings_path_utf8 = "Draft Container/ProjectSettings/input_bindings.ewcfg";
};

struct PsAiSubstrate {
    // Global coherence gate threshold (Q0.15).
    uint16_t global_coherence_gate_q15 = 12000;
};

struct PsSimulation {
    int32_t fixed_dt_ms_s32 = 16;
};

struct PsPackaging {
    uint8_t reserved_u8 = 0;
};

struct EwProjectSettings {
    PsRendering rendering;
    PsPhysics physics;
    PsCamera camera;
    PsInput input;
    PsAiSubstrate ai;
    PsSimulation simulation;
    PsPackaging packaging;
};

bool ge_project_settings_load(EwProjectSettings& out, const std::string& path_utf8, std::string& out_err);
bool ge_project_settings_save(const EwProjectSettings& s, const std::string& path_utf8, std::string& out_err);
