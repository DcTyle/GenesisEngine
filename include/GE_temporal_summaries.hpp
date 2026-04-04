#pragma once

#include <cstdint>

// Tiny, fixed-size summaries that formalize the temporal-coupling contract.
// These are designed to be GPU-portable "measurement lanes" later:
//  - IntentSummary: what actuation was injected
//  - MeasuredSummary: what the state evolution produced
//  - ResidualSummary: discrepancy and collapse routing metadata
// Authoritative state remains fixed-point and bounded.

struct EwIntentSummary {
    // Low-band forcing magnitudes (Q0.15) for bins 0..7.
    uint16_t band_mag_q15[8] = {0,0,0,0,0,0,0,0};
    // Mean intent magnitude (Q0.15).
    uint16_t intent_norm_q15 = 0;
    // Last observed carrier authority codes from pulses.
    uint16_t last_v_code_u16 = 0;
    uint16_t last_i_code_u16 = 0;
};

struct EwMeasuredSummary {
    // Energy summary proxies (Q0.15) for visualization/assimilation.
    uint16_t energy_mean_q15 = 0;
    uint16_t energy_peak_q15 = 0;
    uint16_t leakage_abs_q15 = 0;
    uint16_t pad0 = 0;
};

struct EwResidualSummary {
    // Hashes of the summaries (not cryptographic; deterministic mixing).
    uint64_t intent_hash_u64 = 0;
    uint64_t measured_hash_u64 = 0;
    // Scalar residual proxy (Q32.32), and a norm proxy (Q0.15).
    int64_t residual_q32_32 = 0;
    uint16_t residual_norm_q15 = 0;
    uint8_t residual_band_u8 = 0;
    uint8_t residual_pending_u8 = 0;
};

struct EwCalculusSummary {
    // Bounded discrete calculus lanes derived from committed tick-to-tick error.
    int64_t error_q32_32 = 0;
    int64_t error_delta_q32_32 = 0;
    int64_t error_integral_q32_32 = 0;

    uint16_t error_norm_q15 = 0;
    uint16_t error_delta_norm_q15 = 0;
    uint16_t error_integral_norm_q15 = 0;
    uint16_t controller_authority_q15 = 0;

    uint8_t controller_hold_u8 = 0;
    uint8_t last_commit_u8 = 0;
    uint16_t pad0 = 0;
};

struct EwFrequencyInterferenceCalibrationSummary {
    int64_t calibration_error_q32_32 = 0;
    int64_t calibration_delta_q32_32 = 0;
    int64_t calibration_integral_q32_32 = 0;

    uint16_t gpu_freq_norm_q15 = 0;
    uint16_t gpu_amp_norm_q15 = 0;
    uint16_t gpu_volt_norm_q15 = 0;
    uint16_t interference_norm_q15 = 0;

    uint16_t coherence_norm_q15 = 0;
    uint16_t observer_norm_q15 = 0;
    uint16_t source_vibration_q15 = 0;
    uint16_t calibration_authority_q15 = 0;

    uint16_t interference_band_q15[8] = {0,0,0,0,0,0,0,0};
    uint16_t coherence_band_q15[8] = {0,0,0,0,0,0,0,0};
};

struct EwProcessSubstrateTelemetry {
    uint32_t valid_u32 = 0;
    uint32_t spectral_anchor_id_u32 = 0;
    uint32_t coherence_anchor_id_u32 = 0;
    uint32_t fanout_budget_u32 = 0;

    uint64_t tick_u64 = 0;

    int64_t dt_scale_q32_32 = 0;
    int64_t viscosity_bias_q32_32 = 0;
    int64_t error_q32_32 = 0;
    int64_t error_delta_q32_32 = 0;
    int64_t error_integral_q32_32 = 0;
    int64_t calibration_error_q32_32 = 0;
    int64_t calibration_delta_q32_32 = 0;
    int64_t calibration_integral_q32_32 = 0;

    uint16_t intent_norm_q15 = 0;
    uint16_t energy_mean_q15 = 0;
    uint16_t energy_peak_q15 = 0;
    uint16_t leakage_abs_q15 = 0;

    uint16_t residual_norm_q15 = 0;
    uint16_t error_delta_norm_q15 = 0;
    uint16_t error_integral_norm_q15 = 0;
    uint16_t controller_authority_q15 = 0;
    uint16_t gpu_freq_norm_q15 = 0;
    uint16_t gpu_amp_norm_q15 = 0;
    uint16_t gpu_volt_norm_q15 = 0;
    uint16_t interference_norm_q15 = 0;
    uint16_t coherence_norm_q15 = 0;
    uint16_t observer_norm_q15 = 0;
    uint16_t source_vibration_q15 = 0;
    uint16_t calibration_authority_q15 = 0;

    uint16_t op_gain_q15 = 0;
    uint16_t op_phase_bias_q15 = 0;
    uint16_t learning_coupling_q15 = 0;
    uint16_t phys_coherence_q15 = 0;
    uint16_t learning_coherence_q15 = 0;
    uint16_t temporal_coherence_q15 = 0;

    uint16_t last_v_code_u16 = 0;
    uint16_t last_i_code_u16 = 0;
    uint8_t residual_pending_u8 = 0;
    uint8_t hold_tick_u8 = 0;
    uint8_t last_commit_u8 = 0;
    uint8_t pad1 = 0;
};

static constexpr uint32_t EW_SUBSYSTEM_SUBSTRATE_LANE_CAP = 16u;
static constexpr uint32_t EW_AI_GPU_GRADIENT_DIM_CAP = 10u;

enum EwSubsystemPlaneId : uint32_t {
    EW_SUBSYSTEM_PLANE_NONE = 0u,
    EW_SUBSYSTEM_PLANE_SPECTRAL_PROCESS = 1u,
    EW_SUBSYSTEM_PLANE_CAMERA = 2u,
    EW_SUBSYSTEM_PLANE_RENDER = 3u,
    EW_SUBSYSTEM_PLANE_ASSET_OBJECT = 4u,
    EW_SUBSYSTEM_PLANE_NBODY = 5u,
    EW_SUBSYSTEM_PLANE_CURRICULUM = 6u,
    EW_SUBSYSTEM_PLANE_AUTOMATION = 7u,
    EW_SUBSYSTEM_PLANE_LANGUAGE = 8u,
    EW_SUBSYSTEM_PLANE_MATH = 9u,
    EW_SUBSYSTEM_PLANE_CORPUS = 10u,
    EW_SUBSYSTEM_PLANE_EXTERNAL_API = 11u,
    EW_SUBSYSTEM_PLANE_AI_CORE = 12u,
    EW_SUBSYSTEM_PLANE_AI_DATA = 13u
};

struct EwSubsystemCalculusLane {
    uint32_t subsystem_id_u32 = 0;

    uint16_t intent_norm_q15 = 0;
    uint16_t measured_norm_q15 = 0;
    uint16_t residual_norm_q15 = 0;
    uint16_t spin_norm_q15 = 0;

    uint16_t coupling_norm_q15 = 0;
    uint16_t error_delta_norm_q15 = 0;
    uint16_t error_integral_norm_q15 = 0;
    uint16_t controller_authority_q15 = 0;

    int64_t error_q32_32 = 0;
    int64_t error_delta_q32_32 = 0;
    int64_t error_integral_q32_32 = 0;
};

struct EwSubsystemSubstrateTelemetry {
    uint32_t valid_u32 = 0;
    uint32_t subsystem_count_u32 = 0;
    uint32_t active_subsystem_count_u32 = 0;
    uint32_t dominant_subsystem_id_u32 = 0;

    uint64_t tick_u64 = 0;

    int64_t aggregate_error_q32_32 = 0;
    int64_t aggregate_error_delta_q32_32 = 0;
    int64_t aggregate_error_integral_q32_32 = 0;

    uint16_t aggregate_intent_norm_q15 = 0;
    uint16_t aggregate_measured_norm_q15 = 0;
    uint16_t aggregate_residual_norm_q15 = 0;
    uint16_t aggregate_spin_norm_q15 = 0;

    uint16_t aggregate_coupling_norm_q15 = 0;
    uint16_t aggregate_error_delta_norm_q15 = 0;
    uint16_t aggregate_error_integral_norm_q15 = 0;
    uint16_t aggregate_controller_authority_q15 = 0;

    uint16_t dominant_spin_norm_q15 = 0;
    uint16_t dominant_coupling_norm_q15 = 0;
    uint16_t dominant_residual_norm_q15 = 0;
    uint16_t pad0 = 0;

    EwSubsystemCalculusLane lanes[EW_SUBSYSTEM_SUBSTRATE_LANE_CAP];
};

struct EwAiSubstrateTelemetry {
    uint32_t valid_u32 = 0;
    uint32_t class_id_u32 = 0;
    uint32_t command_count_u32 = 0;
    uint32_t action_log_count_u32 = 0;

    uint64_t tick_u64 = 0;
    uint64_t sig9_u64 = 0;
    uint64_t observation_seq_u64 = 0;
    uint64_t dispatched_observation_seq_u64 = 0;

    int64_t confidence_q32_32 = 0;
    int64_t attractor_strength_q32_32 = 0;
    int64_t carrier_pulse_q63 = 0;
    int64_t carrier_total_weight_q63 = 0;

    uint32_t pending_external_api_u32 = 0;
    uint32_t inflight_external_api_u32 = 0;
    uint32_t fanout_budget_u32 = 0;
    uint32_t ui_chat_count_u32 = 0;

    uint16_t anticipation_flags_u16 = 0;
    uint16_t controller_authority_q15 = 0;
    uint16_t op_gain_q15 = 0;
    uint16_t op_phase_bias_q15 = 0;
    uint16_t memory_norm_q15 = 0;
    uint16_t reasoning_norm_q15 = 0;
    uint16_t planning_norm_q15 = 0;
    uint16_t creativity_norm_q15 = 0;
    uint16_t perception_norm_q15 = 0;
    uint16_t temporal_binding_norm_q15 = 0;
    uint16_t crawler_drift_norm_q15 = 0;
    uint16_t network_coherence_norm_q15 = 0;

    uint16_t carrier_band_q15[8] = {0,0,0,0,0,0,0,0};

    uint16_t last_action_kind_u16 = 0;
    uint16_t last_action_profile_u16 = 0;
    uint32_t last_target_anchor_id_u32 = 0;
    int32_t last_f_code_i32 = 0;
    uint16_t last_a_code_u16 = 0;
    uint16_t last_v_code_u16 = 0;
    uint16_t last_i_code_u16 = 0;
};

struct EwAiDataSubstrateTelemetry {
    uint32_t valid_u32 = 0;
    uint64_t tick_u64 = 0;

    uint32_t corpus_artifact_count_u32 = 0;
    uint32_t corpus_text_artifact_count_u32 = 0;
    uint32_t manifest_record_count_u32 = 0;
    uint32_t pending_metric_count_u32 = 0;
    uint32_t completed_metric_count_u32 = 0;
    uint32_t accepted_metric_count_u32 = 0;

    uint32_t language_word_count_u32 = 0;
    uint32_t language_pron_count_u32 = 0;
    uint32_t language_relation_count_u32 = 0;
    uint32_t language_concept_count_u32 = 0;
    uint32_t language_speech_utt_count_u32 = 0;
    uint32_t language_speech_word_tokens_u32 = 0;

    uint32_t math_pemdas_cases_total_u32 = 0;
    uint32_t math_pemdas_cases_passed_u32 = 0;
    uint32_t math_graph_packets_emitted_u32 = 0;
    uint32_t math_khan_pages_seen_u32 = 0;
    uint32_t math_khan_chars_ingested_u32 = 0;

    uint32_t pending_external_api_u32 = 0;
    uint32_t inflight_external_api_u32 = 0;
    uint32_t crawler_pending_obs_u32 = 0;
    uint32_t crawler_last_utf8_bytes_u32 = 0;

    uint64_t crawler_enqueued_obs_u64 = 0;
    uint64_t crawler_admitted_pulses_u64 = 0;
    uint64_t crawler_dropped_obs_u64 = 0;
    uint64_t crawler_truncated_segments_u64 = 0;
    uint64_t api_decode_request_id_u64 = 0;
    uint64_t gpu_spider_carrier_phase_u64 = 0;

    int64_t error_q32_32 = 0;
    int64_t error_delta_q32_32 = 0;
    int64_t error_integral_q32_32 = 0;
    int64_t temporal_prediction_q32_32 = 0;
    int32_t gpu_spider_f_code_i32 = 0;
    int32_t api_decode_f_code_i32 = 0;
    int32_t api_decode_http_status_s32 = -1;

    uint16_t intent_norm_q15 = 0;
    uint16_t measured_norm_q15 = 0;
    uint16_t residual_norm_q15 = 0;
    uint16_t error_delta_norm_q15 = 0;
    uint16_t error_integral_norm_q15 = 0;
    uint16_t controller_authority_q15 = 0;
    uint16_t crawler_flow_norm_q15 = 0;
    uint16_t crawler_drift_norm_q15 = 0;
    uint16_t crawler_interference_norm_q15 = 0;
    uint16_t crawler_coherence_norm_q15 = 0;
    uint16_t temporal_memory_norm_q15 = 0;
    uint16_t temporal_prediction_norm_q15 = 0;
    uint16_t gpu_spider_a_code_u16 = 0;
    uint16_t gpu_spider_v_code_u16 = 0;
    uint16_t gpu_spider_i_code_u16 = 0;
    uint16_t gpu_gradient_energy_norm_q15 = 0;
    uint16_t api_decode_count_u16 = 0;
    uint16_t api_decode_body_bytes_u16 = 0;
    uint16_t api_decode_a_code_u16 = 0;
    uint16_t api_decode_v_code_u16 = 0;
    uint16_t api_decode_i_code_u16 = 0;
    uint16_t api_decode_norm_q15 = 0;
    uint16_t api_decode_http_status_q15 = 0;

    uint16_t gpu_gradient_dim_q15[EW_AI_GPU_GRADIENT_DIM_CAP] = {0,0,0,0,0,0,0,0,0,0};

    uint16_t intent_band_q15[8] = {0,0,0,0,0,0,0,0};
    uint16_t measured_band_q15[8] = {0,0,0,0,0,0,0,0};
};
