#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace genesis {

enum class GeResearchParticleClass : uint8_t {
    Photon = 0u,
    Weighted = 1u,
    Flavor = 2u,
    Charged = 3u,
};

struct GeResearchTaskSummary {
    std::string task_utf8;
    double freq_norm = 0.0;
    double amp_norm = 0.0;
    double volt_norm = 0.0;
    double curr_norm = 0.0;
    double pulse_period_steps = 0.0;
    double phase_lock_fraction = 0.0;
    double lattice_lock_fraction = 0.0;
    double joint_lock_fraction = 0.0;
    double speed_tracking_score = 0.0;
    double curvature_tracking_score = 0.0;
    double feedback_to_signal_ratio = 0.0;
    double mean_energy_drift = 0.0;
    double composite_score = 0.0;
    double return_distance_a = 0.0;
    double recurrence_alignment = 0.0;
    double conservation_alignment = 0.0;
};

struct GeResearchNistSummary {
    double surrogate_frequency_scale_mhz_per_step = 0.0;
    double step_to_linewidth_ratio = 0.0;
    double step_to_abs_uncertainty_ratio = 0.0;
    std::string verdict_utf8;
};

struct GeResearchPulseQuartet {
    double F = 0.0;
    double A = 0.0;
    double I = 0.0;
    double V = 0.0;
};

struct GeResearchQuadraticMetricModel {
    std::string metric_utf8;
    double center_value = 0.0;
    GeResearchPulseQuartet jacobian;
    GeResearchPulseQuartet hessian_diag;
    double hessian_FA = 0.0;
    double hessian_FI = 0.0;
    double hessian_FV = 0.0;
    double hessian_AI = 0.0;
    double hessian_AV = 0.0;
    double hessian_IV = 0.0;
    std::string deviation_formula_utf8;
};

struct GeResearchValidationError {
    double silicon_score_mae = 0.0;
    double silicon_score_rmse = 0.0;
};

struct GeResearchCollapseGates {
    double gate_coherence = 0.0;
    double gate_trap = 0.0;
    double gate_score = 0.0;
};

struct GeResearchRuntimeGuidance {
    bool valid = false;
    uint32_t correction_cadence_ticks_u32 = 1u;

    double certainty_norm = 0.0;
    double exactness_norm = 0.0;
    double recurrence_norm = 0.0;
    double tensor_gradient_norm = 0.0;
    double packet_coherence_norm = 0.0;
    double observer_coupling_norm = 0.0;
    double temporal_coupling_norm = 0.0;
    double source_memory_norm = 0.0;
    double correction_authority_norm = 0.0;

    double predicted_silicon_score = 0.0;
    double predicted_trap_ratio = 0.0;
    double predicted_coherence = 0.0;
    double predicted_inertia = 0.0;
    double predicted_curvature = 0.0;

    GeResearchPulseQuartet desired_quartet;
    GeResearchPulseQuartet corrected_quartet;
    GeResearchPulseQuartet gradient_quartet;
};

struct GeResearchInterferencePredictionCell {
    GeResearchPulseQuartet quartet;
    double predicted_interference_norm = 0.0;
    double predicted_score = 0.0;
    double predicted_trap_ratio = 0.0;
    double predicted_coherence = 0.0;
    double predicted_inertia = 0.0;
    double predicted_curvature = 0.0;
    double gpu_alignment_norm = 0.0;
    double vector_coupling_norm = 0.0;
    double subsystem_feedback_norm = 0.0;
    double lattice_interference_norm = 0.0;
    double lattice_temporal_coupling_norm = 0.0;
    double certainty_norm = 0.0;
    uint32_t lattice_x_u32 = 0u;
    uint32_t lattice_y_u32 = 0u;
    uint32_t lattice_z_u32 = 0u;
    uint64_t trajectory_spectral_id_u64 = 0u;
};

struct GeResearchGpuAdaptiveCalibration {
    bool valid = false;
    uint32_t axis_resolution_u32 = 0u;
    uint32_t prediction_count_u32 = 0u;

    double observed_gpu_freq_norm = 0.0;
    double observed_gpu_amp_norm = 0.0;
    double observed_gpu_curr_norm = 0.0;
    double observed_gpu_volt_norm = 0.0;
    double observed_interference_norm = 0.0;
    double observed_coherence_norm = 0.0;
    double observed_source_vibration_norm = 0.0;
    double observed_subsystem_residual_norm = 0.0;
    double observed_subsystem_spin_norm = 0.0;
    double observed_subsystem_coupling_norm = 0.0;
    double observed_subsystem_controller_norm = 0.0;

    double tensor_gradient_norm = 0.0;
    double packet_coherence_norm = 0.0;
    double observer_coupling_norm = 0.0;
    double recurrence_norm = 0.0;

    double prediction_confidence_norm = 0.0;
    double best_gpu_alignment_norm = 0.0;
    double best_vector_coupling_norm = 0.0;
    double best_subsystem_feedback_norm = 0.0;
    double best_interference_norm = 0.0;
    double best_lattice_interference_norm = 0.0;
    double best_temporal_coupling_norm = 0.0;
    double next_pulse_correction_norm = 0.0;
    double best_silicon_score = 0.0;
    double best_trap_ratio = 0.0;
    double best_coherence = 0.0;
    double best_inertia = 0.0;
    double best_curvature = 0.0;

    GeResearchPulseQuartet gpu_observed_quartet;
    GeResearchPulseQuartet best_quartet;
    GeResearchPulseQuartet adapted_quartet;
    GeResearchPulseQuartet next_pulse_quartet;
    uint32_t best_lattice_x_u32 = 0u;
    uint32_t best_lattice_y_u32 = 0u;
    uint32_t best_lattice_z_u32 = 0u;
    uint64_t best_trajectory_spectral_id_u64 = 0u;
};

struct GeResearchLiveComputePlan {
    bool valid = false;
    bool ready = false;
    double readiness_norm = 0.0;
    double interference_ledger_norm = 0.0;
    double encoded_extrapolation_norm = 0.0;
    double score_alignment_norm = 0.0;
    double trap_alignment_norm = 0.0;
    double coherence_alignment_norm = 0.0;
    GeResearchPulseQuartet compute_quartet;
    uint64_t trajectory_spectral_id_u64 = 0u;
};

struct GeResearchTrajectorySample {
    std::string task_utf8;
    uint32_t step_u32 = 0u;
    double sent_signal = 0.0;
    double noise_feedback = 0.0;
    double phase_error = 0.0;
    double lattice_distance_a = 0.0;
    double x_m = 0.0;
    double y_m = 0.0;
    double z_m = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    double speed = 0.0;
    double curvature = 0.0;
    double energy_drift = 0.0;
};

struct GeResearchParticleVizPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 1.0f;
    float density = 0.0f;
    float specularity = 0.0f;
    float roughness = 0.0f;
    float occlusion = 0.0f;
    float phase_bias = 0.0f;
    float amplitude = 0.0f;
    float temporal_coupling = 0.0f;
    float curvature_n = 0.0f;
    float observer_shift = 0.0f;
    float trail_age = 0.0f;
    float radius_m = 0.08f;
    float emissive = 1.0f;
    uint32_t rgba8 = 0xFFFFFFFFu;
    uint32_t render_kind_u32 = 3u;
    GeResearchParticleClass particle_class = GeResearchParticleClass::Photon;
};

struct GeResearchPacketPathClassification {
    uint64_t packet_id_u64 = 0u;
    bool shared_path = false;
    uint32_t group_id_u32 = 0u;
    double phase_lock_score = 0.0;
    double curvature_depth = 0.0;
    double coherence_score = 0.0;
};

struct GeResearchTensor6DCell {
    uint32_t x_u32 = 0u;
    uint32_t y_u32 = 0u;
    uint32_t z_u32 = 0u;
    double phase_coherence = 0.0;
    double curvature = 0.0;
    double flux = 0.0;
    double inertia = 0.0;
    double freq_x = 0.0;
    double freq_y = 0.0;
    double freq_z = 0.0;
    double dtheta_dt = 0.0;
    double d2theta_dt2 = 0.0;
    double oam_twist = 0.0;
    double spin_x = 0.0;
    double spin_y = 0.0;
    double spin_z = 0.0;
    double higgs_inertia = 0.0;
};

struct GeResearchVectorExcitationSample {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double vec_x = 0.0;
    double vec_y = 0.0;
    double vec_z = 0.0;
    double spin_x = 0.0;
    double spin_y = 0.0;
    double spin_z = 0.0;
    double oam_twist = 0.0;
};

struct GeResearchTensorGlyphSample {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double tensor_00 = 0.0;
    double tensor_01 = 0.0;
    double tensor_02 = 0.0;
    double tensor_10 = 0.0;
    double tensor_11 = 0.0;
    double tensor_12 = 0.0;
    double tensor_20 = 0.0;
    double tensor_21 = 0.0;
    double tensor_22 = 0.0;
    double color_r = 0.0;
    double color_g = 0.0;
    double color_b = 0.0;
};

struct GeResearchShaderTextureSample {
    uint32_t x_u32 = 0u;
    uint32_t y_u32 = 0u;
    uint32_t z_u32 = 0u;
    double color_r = 0.0;
    double color_g = 0.0;
    double color_b = 0.0;
};

struct GeResearchAudioWaveSample {
    double time_s = 0.0;
    float channel_0 = 0.0f;
    float channel_1 = 0.0f;
    float channel_2 = 0.0f;
    float channel_3 = 0.0f;
};

struct GeResearchAudioFrame {
    uint32_t sample_rate_hz_u32 = 48000u;
    uint32_t channel_count_u32 = 0u;
    float mean_abs_amplitude = 0.0f;
    float peak_abs_amplitude = 0.0f;
    std::vector<int16_t> interleaved_pcm16;
};

struct GeResearchConfinementArchive {
    bool loaded = false;
    std::filesystem::path root_dir;
    std::string load_error_utf8;
    double silicon_lattice_constant_m = 0.0;
    GeResearchNistSummary nist_surrogate;
    std::vector<GeResearchTaskSummary> run040_tasks;
    std::vector<GeResearchTaskSummary> run041_tasks;
    std::vector<GeResearchTrajectorySample> run041_d_track;
    std::vector<GeResearchTrajectorySample> run041_i_accum;
    std::vector<GeResearchTrajectorySample> run041_l_smooth;
    std::vector<GeResearchPacketPathClassification> packet_path_classes;
    std::vector<GeResearchTensor6DCell> tensor6d_cells;
    std::vector<GeResearchVectorExcitationSample> vector_excitations;
    std::vector<GeResearchTensorGlyphSample> tensor_glyphs;
    std::vector<GeResearchShaderTextureSample> shader_texture;
    std::vector<GeResearchAudioWaveSample> audio_waveform;

    bool run043_loaded = false;
    GeResearchPulseQuartet run043_center_quartet;
    GeResearchPulseQuartet run043_delta_quartet;
    GeResearchPulseQuartet pulse_window_min_quartet;
    GeResearchPulseQuartet pulse_window_max_quartet;
    GeResearchQuadraticMetricModel run043_silicon_score_model;
    GeResearchQuadraticMetricModel run043_trap_ratio_model;
    GeResearchQuadraticMetricModel run043_coherence_model;
    GeResearchQuadraticMetricModel run043_inertia_model;
    GeResearchQuadraticMetricModel run043_curvature_model;
    GeResearchValidationError run043_validation_error;
    GeResearchCollapseGates temporal_collapse_gates;
};

struct GeNistSiliconReference {
    bool loaded = false;
    std::filesystem::path source_path;
    std::string load_error_utf8;
    std::string inference_note_utf8;
    double lattice_spacing_d220_m = 0.0;
    double lattice_constant_m = 0.0;
    double density_g_cm3 = 0.0;
    double z_over_a = 0.0;
    double atomic_weight_u = 0.0;
    double mean_excitation_energy_ev = 0.0;
    double first_ionization_energy_ev = 0.0;
    double k_edge_energy_ev = 0.0;
    double mass_attenuation_10kev_cm2_g = 0.0;
    double mass_energy_absorption_10kev_cm2_g = 0.0;
};

bool ge_find_research_confinement_root(std::filesystem::path& out_root);

GeResearchConfinementArchive ge_load_research_confinement_archive();

GeNistSiliconReference ge_load_nist_silicon_reference();

bool ge_validate_research_confinement_archive(const GeResearchConfinementArchive& archive,
                                              std::string& out_message_utf8);

void ge_build_research_particle_viz(const GeResearchConfinementArchive& archive,
                                    uint64_t tick_u64,
                                    size_t max_points,
                                    float frame_phase_01,
                                    float temporal_coupling_01,
                                    std::vector<GeResearchParticleVizPoint>& out_points);

bool ge_has_research_realtime_outputs(const GeResearchConfinementArchive& archive);

void ge_build_research_audio_frame(const GeResearchConfinementArchive& archive,
                                   uint64_t tick_u64,
                                   size_t frame_count,
                                   GeResearchAudioFrame& out_frame);

bool ge_has_research_runtime_guidance(const GeResearchConfinementArchive& archive);

double ge_compute_research_tensor_gradient_norm(const GeResearchConfinementArchive& archive);

double ge_compute_research_packet_coherence_norm(const GeResearchConfinementArchive& archive);

double ge_compute_research_observer_coupling_norm(const GeResearchConfinementArchive& archive);

double ge_compute_research_recurrence_norm(const GeResearchConfinementArchive& archive);

double ge_evaluate_research_metric_model(const GeResearchQuadraticMetricModel& model,
                                         const GeResearchPulseQuartet& center_quartet,
                                         const GeResearchPulseQuartet& quartet);

bool ge_compute_research_metric_gradient(const GeResearchQuadraticMetricModel& model,
                                         const GeResearchPulseQuartet& center_quartet,
                                         const GeResearchPulseQuartet& quartet,
                                         GeResearchPulseQuartet& out_gradient);

bool ge_build_research_runtime_guidance(const GeResearchConfinementArchive& archive,
                                        const GeResearchPulseQuartet& desired_quartet,
                                        double residual_norm_01,
                                        double interference_norm_01,
                                        double substrate_coherence_norm_01,
                                        double source_vibration_norm_01,
                                        GeResearchRuntimeGuidance& out_guidance);

bool ge_build_research_gpu_interference_predictions(
    const GeResearchConfinementArchive& archive,
    const GeResearchPulseQuartet& desired_quartet,
    double gpu_freq_norm_01,
    double gpu_amp_norm_01,
    double gpu_curr_norm_01,
    double gpu_volt_norm_01,
    double interference_norm_01,
    double substrate_coherence_norm_01,
    double source_vibration_norm_01,
    double subsystem_residual_norm_01,
    double subsystem_spin_norm_01,
    double subsystem_coupling_norm_01,
    double subsystem_controller_norm_01,
    uint32_t axis_resolution_u32,
    GeResearchGpuAdaptiveCalibration& out_calibration,
    std::vector<GeResearchInterferencePredictionCell>* out_predictions);

bool ge_build_research_live_compute_plan(
    const GeResearchConfinementArchive& archive,
    const GeResearchGpuAdaptiveCalibration& calibration,
    const GeResearchRuntimeGuidance& guidance,
    GeResearchLiveComputePlan& out_plan);

uint32_t ge_particle_class_rgba8(GeResearchParticleClass particle_class);

const char* ge_particle_class_name_ascii(GeResearchParticleClass particle_class);

} // namespace genesis
