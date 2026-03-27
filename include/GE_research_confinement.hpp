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
    GeResearchParticleClass particle_class = GeResearchParticleClass::Photon;
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

uint32_t ge_particle_class_rgba8(GeResearchParticleClass particle_class);

const char* ge_particle_class_name_ascii(GeResearchParticleClass particle_class);

} // namespace genesis
