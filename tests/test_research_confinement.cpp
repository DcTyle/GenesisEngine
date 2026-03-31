#include "GE_research_confinement.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

int main() {
    const genesis::GeResearchConfinementArchive archive =
        genesis::ge_load_research_confinement_archive();
    const genesis::GeNistSiliconReference nist =
        genesis::ge_load_nist_silicon_reference();

    auto fail = [](const std::string& message) -> int {
        std::cerr << "FAIL: " << message << "\n";
        return 1;
    };

    std::string validation_message;
    if (!genesis::ge_validate_research_confinement_archive(archive, validation_message)) {
        return fail(validation_message);
    }

    if (!archive.loaded) return fail("archive did not report loaded=true");
    if (!nist.loaded) return fail("NIST silicon reference did not load");
    if (!(nist.lattice_constant_m > 5.0e-10 && nist.lattice_constant_m < 5.9e-10)) {
        return fail("NIST silicon lattice constant is outside the expected range");
    }
    if (!(nist.mean_excitation_energy_ev > 100.0 && nist.mean_excitation_energy_ev < 250.0)) {
        return fail("NIST silicon mean excitation energy is outside the expected range");
    }
    if (!(archive.silicon_lattice_constant_m > 5.0e-10 && archive.silicon_lattice_constant_m < 5.9e-10)) {
        return fail("silicon lattice constant is outside the expected range");
    }
    if (archive.nist_surrogate.verdict_utf8 != "not_within_nist_uncertainty_scale") {
        return fail("unexpected NIST surrogate verdict");
    }
    if (archive.nist_surrogate.step_to_linewidth_ratio <= 100.0) {
        return fail("NIST step-to-linewidth ratio is weaker than expected");
    }
    if (archive.run041_d_track.size() < 6u || archive.run041_i_accum.size() < 6u || archive.run041_l_smooth.size() < 6u) {
        return fail("sampled Run41 trajectories are too short");
    }
    if (archive.packet_path_classes.empty()) return fail("packet path classifications did not load");
    if (archive.tensor6d_cells.empty()) return fail("tensor6d cells did not load");
    if (archive.vector_excitations.empty()) return fail("vector excitation samples did not load");
    if (archive.tensor_glyphs.empty()) return fail("tensor glyph samples did not load");
    if (archive.shader_texture.empty()) return fail("shader texture samples did not load");
    if (archive.audio_waveform.empty()) return fail("audio waveform samples did not load");
    if (!archive.run043_loaded) return fail("Run43 runtime guidance summary did not load");
    if (!(archive.temporal_collapse_gates.gate_coherence > 0.0 &&
          archive.temporal_collapse_gates.gate_trap > 0.0 &&
          archive.temporal_collapse_gates.gate_score > 0.0)) {
        return fail("temporal collapse gates did not load");
    }
    if (!(archive.run043_validation_error.silicon_score_rmse > 0.0 &&
          archive.run043_validation_error.silicon_score_mae > 0.0)) {
        return fail("Run43 validation error did not load");
    }
    if (!genesis::ge_has_research_runtime_guidance(archive)) {
        return fail("runtime guidance helper reported missing operators");
    }
    if (!genesis::ge_has_research_realtime_outputs(archive)) {
        return fail("realtime output helper reported missing data");
    }

    genesis::GeResearchRuntimeGuidance guidance{};
    genesis::GeResearchPulseQuartet desired{};
    desired.F = 0.248;
    desired.A = 0.19;
    desired.I = 0.35;
    desired.V = 0.34;
    if (!genesis::ge_build_research_runtime_guidance(archive,
                                                     desired,
                                                     0.18,
                                                     0.22,
                                                     0.81,
                                                     0.27,
                                                     guidance)) {
        return fail("runtime guidance construction failed");
    }
    if (!guidance.valid) return fail("runtime guidance was not marked valid");
    if (guidance.correction_cadence_ticks_u32 < 1u) {
        return fail("runtime guidance cadence is invalid");
    }
    if (!(guidance.corrected_quartet.F >= archive.pulse_window_min_quartet.F &&
          guidance.corrected_quartet.F <= archive.pulse_window_max_quartet.F &&
          guidance.corrected_quartet.A >= archive.pulse_window_min_quartet.A &&
          guidance.corrected_quartet.A <= archive.pulse_window_max_quartet.A &&
          guidance.corrected_quartet.I >= archive.pulse_window_min_quartet.I &&
          guidance.corrected_quartet.I <= archive.pulse_window_max_quartet.I &&
          guidance.corrected_quartet.V >= archive.pulse_window_min_quartet.V &&
          guidance.corrected_quartet.V <= archive.pulse_window_max_quartet.V)) {
        return fail("runtime guidance quartet escaped the calibrated window");
    }
    if (!std::isfinite(guidance.predicted_silicon_score) ||
        !std::isfinite(guidance.predicted_coherence) ||
        !std::isfinite(guidance.predicted_trap_ratio) ||
        !std::isfinite(guidance.predicted_curvature)) {
        return fail("runtime guidance predicted non-finite metrics");
    }

    genesis::GeResearchGpuAdaptiveCalibration gpu_calibration{};
    std::vector<genesis::GeResearchInterferencePredictionCell> gpu_predictions;
    if (!genesis::ge_build_research_gpu_interference_predictions(archive,
                                                                 desired,
                                                                 0.24,
                                                                 0.19,
                                                                 0.34,
                                                                 0.33,
                                                                 0.21,
                                                                 0.82,
                                                                 0.28,
                                                                 0.19,
                                                                 0.24,
                                                                 0.31,
                                                                 0.27,
                                                                 5u,
                                                                 gpu_calibration,
                                                                 &gpu_predictions)) {
        return fail("gpu adaptive interference prediction sweep failed");
    }
    if (!gpu_calibration.valid) return fail("gpu adaptive calibration was not marked valid");
    if (gpu_predictions.size() != 625u) {
        return fail("gpu adaptive prediction sweep did not cover the expected full-range grid");
    }
    if (!(gpu_calibration.best_trajectory_spectral_id_u64 != 0u)) {
        return fail("gpu adaptive calibration did not produce a trajectory spectral id");
    }
    if (!(gpu_calibration.best_quartet.F >= archive.pulse_window_min_quartet.F &&
          gpu_calibration.best_quartet.F <= archive.pulse_window_max_quartet.F &&
          gpu_calibration.best_quartet.A >= archive.pulse_window_min_quartet.A &&
          gpu_calibration.best_quartet.A <= archive.pulse_window_max_quartet.A &&
          gpu_calibration.best_quartet.I >= archive.pulse_window_min_quartet.I &&
          gpu_calibration.best_quartet.I <= archive.pulse_window_max_quartet.I &&
          gpu_calibration.best_quartet.V >= archive.pulse_window_min_quartet.V &&
          gpu_calibration.best_quartet.V <= archive.pulse_window_max_quartet.V)) {
        return fail("gpu adaptive best quartet escaped the calibrated window");
    }
    if (!(gpu_calibration.next_pulse_quartet.F >= archive.pulse_window_min_quartet.F &&
          gpu_calibration.next_pulse_quartet.F <= archive.pulse_window_max_quartet.F &&
          gpu_calibration.next_pulse_quartet.A >= archive.pulse_window_min_quartet.A &&
          gpu_calibration.next_pulse_quartet.A <= archive.pulse_window_max_quartet.A &&
          gpu_calibration.next_pulse_quartet.I >= archive.pulse_window_min_quartet.I &&
          gpu_calibration.next_pulse_quartet.I <= archive.pulse_window_max_quartet.I &&
          gpu_calibration.next_pulse_quartet.V >= archive.pulse_window_min_quartet.V &&
          gpu_calibration.next_pulse_quartet.V <= archive.pulse_window_max_quartet.V)) {
        return fail("gpu adaptive next pulse quartet escaped the calibrated window");
    }
    if (!std::isfinite(gpu_calibration.best_interference_norm) ||
        !std::isfinite(gpu_calibration.best_lattice_interference_norm) ||
        !std::isfinite(gpu_calibration.best_temporal_coupling_norm) ||
        !std::isfinite(gpu_calibration.best_subsystem_feedback_norm) ||
        !std::isfinite(gpu_calibration.next_pulse_correction_norm) ||
        !std::isfinite(gpu_calibration.best_silicon_score) ||
        !std::isfinite(gpu_calibration.best_coherence) ||
        !std::isfinite(gpu_calibration.best_curvature)) {
        return fail("gpu adaptive calibration predicted non-finite metrics");
    }
    if (gpu_predictions.empty()) {
        return fail("gpu adaptive prediction sweep emitted no prediction cells");
    }
    if (!std::isfinite(gpu_predictions.front().lattice_interference_norm) ||
        !std::isfinite(gpu_predictions.front().lattice_temporal_coupling_norm)) {
        return fail("gpu adaptive prediction cells did not emit lattice-backed fields");
    }

    genesis::GeResearchLiveComputePlan live_plan{};
    if (!genesis::ge_build_research_live_compute_plan(archive, gpu_calibration, guidance, live_plan)) {
        return fail("live compute plan construction failed");
    }
    if (!live_plan.valid) return fail("live compute plan was not marked valid");
    if (!(live_plan.trajectory_spectral_id_u64 != 0u)) {
        return fail("live compute plan did not carry a spectral trajectory id");
    }
    if (!(live_plan.compute_quartet.F >= archive.pulse_window_min_quartet.F &&
          live_plan.compute_quartet.F <= archive.pulse_window_max_quartet.F &&
          live_plan.compute_quartet.A >= archive.pulse_window_min_quartet.A &&
          live_plan.compute_quartet.A <= archive.pulse_window_max_quartet.A &&
          live_plan.compute_quartet.I >= archive.pulse_window_min_quartet.I &&
          live_plan.compute_quartet.I <= archive.pulse_window_max_quartet.I &&
          live_plan.compute_quartet.V >= archive.pulse_window_min_quartet.V &&
          live_plan.compute_quartet.V <= archive.pulse_window_max_quartet.V)) {
        return fail("live compute quartet escaped the calibrated window");
    }
    if (!std::isfinite(live_plan.readiness_norm) ||
        !std::isfinite(live_plan.interference_ledger_norm) ||
        !std::isfinite(live_plan.encoded_extrapolation_norm)) {
        return fail("live compute plan predicted non-finite readiness");
    }

    std::vector<genesis::GeResearchParticleVizPoint> points;
    genesis::ge_build_research_particle_viz(archive, 24u, 128u, 0.5f, 0.75f, points);
    if (points.empty()) return fail("research replay emitted no points");

    bool saw_photon = false;
    bool saw_weighted = false;
    bool saw_flavor = false;
    bool saw_charged = false;
    for (const genesis::GeResearchParticleVizPoint& point : points) {
        if (!std::isfinite((double)point.x) ||
            !std::isfinite((double)point.y) ||
            !std::isfinite((double)point.z)) {
            return fail("non-finite research point position");
        }
        if (!(point.density >= 0.0f && point.density <= 1.0f)) return fail("point density out of range");
        if (!(point.specularity >= 0.0f && point.specularity <= 1.0f)) return fail("point specularity out of range");
        if (!(point.roughness >= 0.0f && point.roughness <= 1.0f)) return fail("point roughness out of range");
        if (!(point.occlusion >= 0.0f && point.occlusion <= 1.0f)) return fail("point occlusion out of range");
        if (!(point.phase_bias >= -1.0f && point.phase_bias <= 1.0f)) return fail("point phase bias out of range");
        if (!(point.amplitude >= 0.0f && point.amplitude <= 1.0f)) return fail("point amplitude out of range");
        if (!(point.radius_m > 0.0f)) return fail("point radius is not positive");
        if (!(point.emissive >= 0.0f)) return fail("point emissive is negative");
        switch (point.particle_class) {
            case genesis::GeResearchParticleClass::Photon:   saw_photon = true; break;
            case genesis::GeResearchParticleClass::Weighted: saw_weighted = true; break;
            case genesis::GeResearchParticleClass::Flavor:   saw_flavor = true; break;
            case genesis::GeResearchParticleClass::Charged:  saw_charged = true; break;
        }
    }

    if (!saw_photon || !saw_weighted || !saw_flavor || !saw_charged) {
        return fail("replay did not emit all particle classes");
    }

    genesis::GeResearchAudioFrame audio_frame{};
    genesis::ge_build_research_audio_frame(archive, 12u, 16u, audio_frame);
    if (audio_frame.channel_count_u32 != 4u) return fail("unexpected research audio channel count");
    if (audio_frame.interleaved_pcm16.size() != 64u) return fail("unexpected research audio sample count");
    if (!(audio_frame.peak_abs_amplitude > 0.0f)) return fail("research audio peak amplitude is zero");

    std::cout << "PASS: research confinement archive parsed and replayed\n";
    return 0;
}
