#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "GE_research_confinement.hpp"
#include "GE_runtime.hpp"
#include "ew_cli_args.hpp"
#include "field_lattice_cpu.hpp"

namespace {

constexpr uint32_t kTicksPerSecond = 360u;
constexpr uint32_t kSecondsPerHour = 3600u;
constexpr uint32_t kDefaultStartHour = 9u;
constexpr uint32_t kDefaultEndHour = 17u;
constexpr uint32_t kDefaultAnchors = 128u;
constexpr uint32_t kDefaultAxisResolution = 6u;
constexpr uint32_t kDefaultSampleIntervalSeconds = 1800u;
constexpr uint32_t kDefaultObservationIntervalSeconds = 300u;
constexpr uint32_t kDefaultRehearsalTicksPerSimSecond = 1u;

struct TimeSeriesPoint {
    std::string label_utf8;
    uint64_t tick_u64 = 0u;
    double desired_f = 0.0;
    double desired_a = 0.0;
    double desired_i = 0.0;
    double desired_v = 0.0;
    double emitted_f = 0.0;
    double emitted_a = 0.0;
    double emitted_i = 0.0;
    double emitted_v = 0.0;
    double readiness_norm = 0.0;
    double certainty_norm = 0.0;
    double residual_norm = 0.0;
    double interference_norm = 0.0;
    double source_vibration_norm = 0.0;
    double lattice_interference_norm = 0.0;
    double lattice_temporal_coupling_norm = 0.0;
    double ai_confidence_norm = 0.0;
    uint32_t pending_external_api_u32 = 0u;
    uint32_t inflight_external_api_u32 = 0u;
    uint32_t live_compute_active_u32 = 0u;
    uint32_t watchdog_fault_u32 = 0u;
};

struct Aggregates {
    double max_residual_norm = 0.0;
    double max_interference_norm = 0.0;
    double max_source_vibration_norm = 0.0;
    double max_controller_authority_norm = 0.0;
    double max_calibration_authority_norm = 0.0;
    double max_lattice_interference_norm = 0.0;
    double max_lattice_temporal_coupling_norm = 0.0;
    double max_prediction_confidence_norm = 0.0;
    double max_readiness_norm = 0.0;
    double min_readiness_norm = 1.0;
    double mean_readiness_norm = 0.0;
    double max_ai_confidence_norm = 0.0;
    uint32_t max_pending_external_api_u32 = 0u;
    uint32_t max_inflight_external_api_u32 = 0u;
    uint32_t max_ai_command_count_u32 = 0u;
    uint32_t max_ai_action_log_count_u32 = 0u;
    uint32_t max_ai_data_pending_external_api_u32 = 0u;
    uint32_t max_ai_data_inflight_external_api_u32 = 0u;
};

static double clamp_norm_01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static double norm_from_abs(double v) {
    const double av = std::abs(v);
    return av / (1.0 + av);
}

static uint16_t pack_norm_u16(double v) {
    const double n = clamp_norm_01(v);
    return static_cast<uint16_t>(std::lround(n * 65535.0));
}

static int32_t pack_freq_code(double v) {
    const double n = clamp_norm_01(v);
    return static_cast<int32_t>(std::lround(n * 4096.0));
}

static uint64_t pack_freq_hz(double v) {
    const uint64_t hz = static_cast<uint64_t>(std::llround(clamp_norm_01(v) * 4096.0));
    return std::max<uint64_t>(hz, 1u);
}

static int64_t q32_32_from_f64(double v) {
    const long double scaled = static_cast<long double>(v) * 4294967296.0L;
    if (scaled >= static_cast<long double>(INT64_MAX)) return INT64_MAX;
    if (scaled <= static_cast<long double>(INT64_MIN)) return INT64_MIN;
    return static_cast<int64_t>(scaled);
}

static uint64_t pack_positive_q32_32(double v) {
    const long double scaled = static_cast<long double>(std::max(0.0, v)) * 4294967296.0L;
    if (scaled >= static_cast<long double>(UINT64_MAX)) return UINT64_MAX;
    return static_cast<uint64_t>(std::llround(static_cast<double>(scaled)));
}

static double unpack_positive_q32_32(uint64_t v) {
    return static_cast<double>(v) / 4294967296.0;
}

static double q15_to_norm(uint16_t v) {
    return static_cast<double>(v) / 65535.0;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8u);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20u) {
                    out += '?';
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

static std::string format_local_time_label(const std::string& date_utf8,
                                           uint32_t start_hour_u32,
                                           uint32_t second_offset_u32) {
    const uint32_t total_seconds = start_hour_u32 * kSecondsPerHour + second_offset_u32;
    const uint32_t hour = (total_seconds / 3600u) % 24u;
    const uint32_t minute = (total_seconds / 60u) % 60u;
    const uint32_t second = total_seconds % 60u;

    std::ostringstream oss;
    oss << date_utf8 << 'T'
        << std::setw(2) << std::setfill('0') << hour << ':'
        << std::setw(2) << std::setfill('0') << minute << ':'
        << std::setw(2) << std::setfill('0') << second
        << " America/Anchorage";
    return oss.str();
}

static bool quartet_is_finite(const genesis::GeResearchPulseQuartet& q) {
    return std::isfinite(q.F) && std::isfinite(q.A) &&
           std::isfinite(q.I) && std::isfinite(q.V);
}

static genesis::GeResearchPulseQuartet clamp_to_window(
    const genesis::GeResearchConfinementArchive& archive,
    genesis::GeResearchPulseQuartet q) {
    q.F = std::clamp(q.F, archive.pulse_window_min_quartet.F, archive.pulse_window_max_quartet.F);
    q.A = std::clamp(q.A, archive.pulse_window_min_quartet.A, archive.pulse_window_max_quartet.A);
    q.I = std::clamp(q.I, archive.pulse_window_min_quartet.I, archive.pulse_window_max_quartet.I);
    q.V = std::clamp(q.V, archive.pulse_window_min_quartet.V, archive.pulse_window_max_quartet.V);
    return q;
}

static genesis::GeResearchPulseQuartet scheduled_desired_quartet(
    const genesis::GeResearchConfinementArchive& archive,
    double day01) {
    constexpr double kTau = 6.28318530717958647692;
    const genesis::GeResearchPulseQuartet& c = archive.run043_center_quartet;
    const genesis::GeResearchPulseQuartet& d = archive.run043_delta_quartet;

    auto wave = [&](double phase_a, double phase_b, double phase_c) {
        return 0.56 * std::sin(kTau * (day01 + phase_a)) +
               0.29 * std::sin(kTau * (2.0 * day01 + phase_b)) +
               0.15 * std::sin(kTau * (5.0 * day01 + phase_c));
    };

    genesis::GeResearchPulseQuartet q{};
    q.F = c.F + d.F * wave(0.03, 0.17, 0.41);
    q.A = c.A + d.A * wave(0.19, 0.08, 0.36);
    q.I = c.I + d.I * wave(0.11, 0.27, 0.49);
    q.V = c.V + d.V * wave(0.07, 0.21, 0.31);

    const double midday = std::sin(kTau * (day01 - 0.25));
    q.F += 0.18 * d.F * midday;
    q.A += 0.24 * d.A * midday;
    q.I += 0.22 * d.I * std::sin(kTau * (day01 - 0.18));
    q.V += 0.12 * d.V * std::sin(kTau * (day01 - 0.33));
    return clamp_to_window(archive, q);
}

static EwEnvelopeSample scheduled_envelope_sample(const genesis::GeResearchPulseQuartet& q,
                                                  double day01) {
    constexpr double kTau = 6.28318530717958647692;
    const double load = clamp_norm_01(
        0.52 +
        0.28 * std::sin(kTau * (day01 - 0.12)) +
        0.12 * std::sin(kTau * (4.0 * day01 + 0.27)) +
        0.08 * clamp_norm_01((q.F + q.A + q.I + q.V) * 0.25));

    EwEnvelopeSample s{};
    s.t_exec_ns_u64 = static_cast<uint64_t>(1000000.0 + 9000000.0 * load);
    s.t_budget_ns_u64 = 12000000u;
    s.bytes_moved_u64 = static_cast<uint64_t>(2.0e6 + 20.0e6 * load);
    s.bytes_budget_u64 = 24u * 1000u * 1000u;
    s.queue_backlog_u32 = static_cast<uint32_t>(std::lround(8.0 + 184.0 * load));
    s.queue_budget_u32 = 256u;
    s.gpu_carrier_hz_q32_32 = q32_32_from_f64(std::max(1.0, q.F * 4096.0));
    return s;
}

static void write_report_json(const std::filesystem::path& out_path,
                              const std::string& date_utf8,
                              uint32_t start_hour_u32,
                              uint32_t end_hour_u32,
                              uint32_t anchors_u32,
                              uint32_t rehearsal_ticks_per_sim_second_u32,
                              uint64_t simulated_ticks_u64,
                              uint32_t simulated_seconds_u32,
                              double wall_elapsed_seconds,
                              uint32_t watchdog_fault_count_u32,
                              uint32_t guidance_build_fail_count_u32,
                              uint32_t guidance_out_of_window_count_u32,
                              uint32_t next_pulse_out_of_window_count_u32,
                              uint32_t live_compute_toggle_count_u32,
                              uint32_t live_compute_active_seconds_u32,
                              uint64_t live_compute_activation_tick_u64,
                              const Aggregates& agg,
                              const genesis::GeResearchPulseQuartet& final_desired,
                              const genesis::GeResearchPulseQuartet& final_emitted,
                              const genesis::GeResearchRuntimeGuidance& final_guidance,
                              const genesis::GeResearchGpuAdaptiveCalibration& final_calibration,
                              const genesis::GeResearchLiveComputePlan& final_live_plan,
                              const std::vector<TimeSeriesPoint>& timeline) {
    std::error_code ec;
    std::filesystem::create_directories(out_path.parent_path(), ec);

    std::ofstream f(out_path, std::ios::out | std::ios::trunc);
    if (!f.good()) return;
    f.setf(std::ios::fixed);
    f.precision(6);

    f << "{\n";
    f << "  \"simulation\": {\n";
    f << "    \"date\": \"" << json_escape(date_utf8) << "\",\n";
    f << "    \"start_local\": \"" << json_escape(format_local_time_label(date_utf8, start_hour_u32, 0u)) << "\",\n";
    f << "    \"end_local\": \"" << json_escape(format_local_time_label(date_utf8, start_hour_u32, simulated_seconds_u32)) << "\",\n";
    f << "    \"configured_end_hour\": " << end_hour_u32 << ",\n";
    f << "    \"canonical_ticks_per_second\": " << kTicksPerSecond << ",\n";
    f << "    \"rehearsal_ticks_per_sim_second\": " << rehearsal_ticks_per_sim_second_u32 << ",\n";
    f << "    \"simulated_seconds\": " << simulated_seconds_u32 << ",\n";
    f << "    \"simulated_ticks\": " << simulated_ticks_u64 << ",\n";
    f << "    \"anchors\": " << anchors_u32 << ",\n";
    f << "    \"wall_elapsed_seconds\": " << wall_elapsed_seconds << "\n";
    f << "  },\n";
    f << "  \"health\": {\n";
    f << "    \"watchdog_fault_count\": " << watchdog_fault_count_u32 << ",\n";
    f << "    \"guidance_build_fail_count\": " << guidance_build_fail_count_u32 << ",\n";
    f << "    \"guidance_out_of_window_count\": " << guidance_out_of_window_count_u32 << ",\n";
    f << "    \"next_pulse_out_of_window_count\": " << next_pulse_out_of_window_count_u32 << ",\n";
    f << "    \"live_compute_toggle_count\": " << live_compute_toggle_count_u32 << ",\n";
    f << "    \"live_compute_active_seconds\": " << live_compute_active_seconds_u32 << ",\n";
    f << "    \"live_compute_active_fraction\": "
      << ((simulated_seconds_u32 != 0u) ? (static_cast<double>(live_compute_active_seconds_u32) / static_cast<double>(simulated_seconds_u32)) : 0.0) << ",\n";
    f << "    \"live_compute_activation_tick\": " << live_compute_activation_tick_u64 << "\n";
    f << "  },\n";
    f << "  \"aggregates\": {\n";
    f << "    \"max_residual_norm\": " << agg.max_residual_norm << ",\n";
    f << "    \"max_interference_norm\": " << agg.max_interference_norm << ",\n";
    f << "    \"max_source_vibration_norm\": " << agg.max_source_vibration_norm << ",\n";
    f << "    \"max_controller_authority_norm\": " << agg.max_controller_authority_norm << ",\n";
    f << "    \"max_calibration_authority_norm\": " << agg.max_calibration_authority_norm << ",\n";
    f << "    \"max_lattice_interference_norm\": " << agg.max_lattice_interference_norm << ",\n";
    f << "    \"max_lattice_temporal_coupling_norm\": " << agg.max_lattice_temporal_coupling_norm << ",\n";
    f << "    \"max_prediction_confidence_norm\": " << agg.max_prediction_confidence_norm << ",\n";
    f << "    \"min_readiness_norm\": " << agg.min_readiness_norm << ",\n";
    f << "    \"max_readiness_norm\": " << agg.max_readiness_norm << ",\n";
    f << "    \"mean_readiness_norm\": " << agg.mean_readiness_norm << ",\n";
    f << "    \"max_ai_confidence_norm\": " << agg.max_ai_confidence_norm << ",\n";
    f << "    \"max_pending_external_api\": " << agg.max_pending_external_api_u32 << ",\n";
    f << "    \"max_inflight_external_api\": " << agg.max_inflight_external_api_u32 << ",\n";
    f << "    \"max_ai_command_count\": " << agg.max_ai_command_count_u32 << ",\n";
    f << "    \"max_ai_action_log_count\": " << agg.max_ai_action_log_count_u32 << ",\n";
    f << "    \"max_ai_data_pending_external_api\": " << agg.max_ai_data_pending_external_api_u32 << ",\n";
    f << "    \"max_ai_data_inflight_external_api\": " << agg.max_ai_data_inflight_external_api_u32 << "\n";
    f << "  },\n";
    f << "  \"final_state\": {\n";
    f << "    \"desired_quartet\": {\"F\": " << final_desired.F << ", \"A\": " << final_desired.A
      << ", \"I\": " << final_desired.I << ", \"V\": " << final_desired.V << "},\n";
    f << "    \"emitted_quartet\": {\"F\": " << final_emitted.F << ", \"A\": " << final_emitted.A
      << ", \"I\": " << final_emitted.I << ", \"V\": " << final_emitted.V << "},\n";
    f << "    \"guidance\": {\n";
    f << "      \"valid\": " << (final_guidance.valid ? 1 : 0) << ",\n";
    f << "      \"certainty_norm\": " << final_guidance.certainty_norm << ",\n";
    f << "      \"exactness_norm\": " << final_guidance.exactness_norm << ",\n";
    f << "      \"temporal_coupling_norm\": " << final_guidance.temporal_coupling_norm << ",\n";
    f << "      \"predicted_silicon_score\": " << final_guidance.predicted_silicon_score << ",\n";
    f << "      \"predicted_coherence\": " << final_guidance.predicted_coherence << "\n";
    f << "    },\n";
    f << "    \"gpu_calibration\": {\n";
    f << "      \"valid\": " << (final_calibration.valid ? 1 : 0) << ",\n";
    f << "      \"prediction_confidence_norm\": " << final_calibration.prediction_confidence_norm << ",\n";
    f << "      \"best_interference_norm\": " << final_calibration.best_interference_norm << ",\n";
    f << "      \"best_lattice_interference_norm\": " << final_calibration.best_lattice_interference_norm << ",\n";
    f << "      \"best_temporal_coupling_norm\": " << final_calibration.best_temporal_coupling_norm << ",\n";
    f << "      \"next_pulse_correction_norm\": " << final_calibration.next_pulse_correction_norm << ",\n";
    f << "      \"best_trajectory_spectral_id\": " << final_calibration.best_trajectory_spectral_id_u64 << "\n";
    f << "    },\n";
    f << "    \"live_compute\": {\n";
    f << "      \"valid\": " << (final_live_plan.valid ? 1 : 0) << ",\n";
    f << "      \"ready\": " << (final_live_plan.ready ? 1 : 0) << ",\n";
    f << "      \"readiness_norm\": " << final_live_plan.readiness_norm << ",\n";
    f << "      \"interference_ledger_norm\": " << final_live_plan.interference_ledger_norm << ",\n";
    f << "      \"encoded_extrapolation_norm\": " << final_live_plan.encoded_extrapolation_norm << ",\n";
    f << "      \"trajectory_spectral_id\": " << final_live_plan.trajectory_spectral_id_u64 << "\n";
    f << "    }\n";
    f << "  },\n";
    f << "  \"timeline\": [\n";
    for (size_t i = 0u; i < timeline.size(); ++i) {
        const TimeSeriesPoint& p = timeline[i];
        f << "    {\n";
        f << "      \"label\": \"" << json_escape(p.label_utf8) << "\",\n";
        f << "      \"tick\": " << p.tick_u64 << ",\n";
        f << "      \"desired\": {\"F\": " << p.desired_f << ", \"A\": " << p.desired_a
          << ", \"I\": " << p.desired_i << ", \"V\": " << p.desired_v << "},\n";
        f << "      \"emitted\": {\"F\": " << p.emitted_f << ", \"A\": " << p.emitted_a
          << ", \"I\": " << p.emitted_i << ", \"V\": " << p.emitted_v << "},\n";
        f << "      \"readiness_norm\": " << p.readiness_norm << ",\n";
        f << "      \"certainty_norm\": " << p.certainty_norm << ",\n";
        f << "      \"residual_norm\": " << p.residual_norm << ",\n";
        f << "      \"interference_norm\": " << p.interference_norm << ",\n";
        f << "      \"source_vibration_norm\": " << p.source_vibration_norm << ",\n";
        f << "      \"lattice_interference_norm\": " << p.lattice_interference_norm << ",\n";
        f << "      \"lattice_temporal_coupling_norm\": " << p.lattice_temporal_coupling_norm << ",\n";
        f << "      \"ai_confidence_norm\": " << p.ai_confidence_norm << ",\n";
        f << "      \"pending_external_api\": " << p.pending_external_api_u32 << ",\n";
        f << "      \"inflight_external_api\": " << p.inflight_external_api_u32 << ",\n";
        f << "      \"live_compute_active\": " << p.live_compute_active_u32 << ",\n";
        f << "      \"watchdog_fault\": " << p.watchdog_fault_u32 << "\n";
        f << "    }" << ((i + 1u < timeline.size()) ? "," : "") << "\n";
    }
    f << "  ]\n";
    f << "}\n";
}

struct RehearsalRunner {
    explicit RehearsalRunner(uint32_t anchors_u32)
        : sm(anchors_u32), lattice(128u, 128u, 128u) {}

    SubstrateManager sm;
    EwFieldLatticeCpu lattice;
    genesis::GeResearchConfinementArchive archive;
    genesis::GeResearchRuntimeGuidance guidance;
    genesis::GeResearchGpuAdaptiveCalibration calibration;
    genesis::GeResearchLiveComputePlan live_plan;
    genesis::GeResearchPulseQuartet desired_quartet;
    genesis::GeResearchPulseQuartet emitted_quartet;

    uint64_t sx_q32_32_u64 = 1ull << 32u;
    uint64_t sy_q32_32_u64 = 1ull << 32u;
    uint64_t sz_q32_32_u64 = 1ull << 32u;
    double lattice_probe_energy_norm = 0.0;
    double lattice_probe_flux_norm = 0.0;
    double lattice_probe_interference_norm = 0.0;
    double lattice_probe_temporal_coupling_norm = 0.0;

    bool live_compute_enabled = true;
    bool live_compute_active = false;
    bool watchdog_fault = false;
    bool has_pulse_history = false;
    uint64_t last_pulse_tick_u64 = 0u;
    uint64_t live_compute_activation_tick_u64 = 0u;
    uint32_t watchdog_fault_count_u32 = 0u;
    uint32_t guidance_build_fail_count_u32 = 0u;
    uint32_t guidance_out_of_window_count_u32 = 0u;
    uint32_t next_pulse_out_of_window_count_u32 = 0u;
    uint32_t live_compute_toggle_count_u32 = 0u;
    uint32_t live_compute_active_seconds_u32 = 0u;

    void init() {
        sm.observe_text_line("BOOT: research live-day rehearsal");
        sm.reservoir = static_cast<int64_t>(sm.anchors.size()) << 32;
        lattice.init(0xE16E5151ULL);
        archive = genesis::ge_load_research_confinement_archive();
        desired_quartet = archive.run043_center_quartet;
        emitted_quartet = archive.run043_center_quartet;
    }

    bool ready(std::string& out_error_utf8) const {
        if (!archive.loaded) {
            out_error_utf8 = archive.load_error_utf8.empty() ? "research archive failed to load" : archive.load_error_utf8;
            return false;
        }
        if (!genesis::ge_has_research_runtime_guidance(archive)) {
            out_error_utf8 = "research archive is missing runtime guidance operators";
            return false;
        }
        out_error_utf8.clear();
        return true;
    }

    void update_axis_scale_state(const genesis::GeResearchPulseQuartet& quartet) {
        const double sx = std::max(0.125, 0.50 + 1.50 * clamp_norm_01(quartet.F));
        const double sy = std::max(0.125, 0.50 + 1.50 * clamp_norm_01(quartet.A));
        const double sz = std::max(0.125, 0.45 + 0.85 * clamp_norm_01(quartet.F) + 0.70 * clamp_norm_01(quartet.A));
        sx_q32_32_u64 = pack_positive_q32_32(sx);
        sy_q32_32_u64 = pack_positive_q32_32(sy);
        sz_q32_32_u64 = pack_positive_q32_32(sz);
    }

    void update_prediction_lattice_probe(const EwProcessSubstrateTelemetry* telemetry_opt,
                                         const genesis::GeResearchPulseQuartet& desired) {
        update_axis_scale_state(desired);

        const double gpu_freq_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->gpu_freq_norm_q15) : desired.F;
        const double gpu_amp_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->gpu_amp_norm_q15) : desired.A;
        const double gpu_curr_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->last_i_code_u16) : desired.I;
        const double gpu_volt_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->gpu_volt_norm_q15) : desired.V;
        const double interference_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->interference_norm_q15) : 0.0;
        const double coherence_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->coherence_norm_q15) : 0.0;
        const double source_vibration_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->source_vibration_q15) : 0.0;

        const double sx = unpack_positive_q32_32(sx_q32_32_u64);
        const double sy = unpack_positive_q32_32(sy_q32_32_u64);
        const double sz = unpack_positive_q32_32(sz_q32_32_u64);
        const double text_drive_n =
            clamp_norm_01(0.62 * clamp_norm_01(desired.F * sx) + 0.20 * gpu_freq_norm + 0.18 * interference_norm);
        const double image_drive_n =
            clamp_norm_01(0.60 * clamp_norm_01(desired.A * sy) + 0.22 * gpu_amp_norm + 0.18 * gpu_volt_norm);
        const double audio_drive_n =
            clamp_norm_01(0.56 * clamp_norm_01(desired.I * sz) + 0.24 * gpu_curr_norm + 0.20 * source_vibration_norm);

        lattice.inject_text_amplitude_q32_32(q32_32_from_f64(text_drive_n));
        lattice.inject_image_amplitude_q32_32(q32_32_from_f64(image_drive_n));
        lattice.inject_audio_amplitude_q32_32(q32_32_from_f64(audio_drive_n));
        lattice.step_one_tick();

        const std::vector<float>& e_curr = lattice.E_curr();
        const std::vector<float>& flux = lattice.flux();
        if (e_curr.empty() || flux.empty() || e_curr.size() != flux.size()) {
            lattice_probe_energy_norm = 0.0;
            lattice_probe_flux_norm = 0.0;
            lattice_probe_interference_norm = 0.0;
            lattice_probe_temporal_coupling_norm = 0.0;
            return;
        }

        constexpr int gx = 128;
        constexpr int gy = 128;
        constexpr int gz = 128;
        const auto idx = [](int x, int y, int z) -> size_t {
            return static_cast<size_t>((z * gy + y) * gx + x);
        };

        const int cx = gx / 2;
        const int cy = gy / 2;
        const int cz = gz / 2;
        double sum_energy = 0.0;
        double sum_flux = 0.0;
        double sum_gradient = 0.0;
        size_t sample_count = 0u;
        for (int z = cz - 1; z <= cz + 1; ++z) {
            for (int y = cy - 1; y <= cy + 1; ++y) {
                for (int x = cx - 1; x <= cx + 1; ++x) {
                    const size_t i = idx(x, y, z);
                    const size_t ixm = idx(x - 1, y, z);
                    const size_t ixp = idx(x + 1, y, z);
                    const size_t iym = idx(x, y - 1, z);
                    const size_t iyp = idx(x, y + 1, z);
                    const size_t izm = idx(x, y, z - 1);
                    const size_t izp = idx(x, y, z + 1);
                    const double e_abs = std::abs(static_cast<double>(e_curr[i]));
                    const double f_abs = std::abs(static_cast<double>(flux[i]));
                    const double gradient =
                        std::abs(static_cast<double>(e_curr[ixp]) - static_cast<double>(e_curr[ixm])) +
                        std::abs(static_cast<double>(e_curr[iyp]) - static_cast<double>(e_curr[iym])) +
                        std::abs(static_cast<double>(e_curr[izp]) - static_cast<double>(e_curr[izm]));
                    sum_energy += e_abs;
                    sum_flux += f_abs;
                    sum_gradient += gradient;
                    ++sample_count;
                }
            }
        }

        const double inv_count = (sample_count != 0u) ? (1.0 / static_cast<double>(sample_count)) : 0.0;
        lattice_probe_energy_norm = norm_from_abs(sum_energy * inv_count);
        lattice_probe_flux_norm = norm_from_abs(sum_flux * inv_count);
        const double gradient_norm = norm_from_abs(sum_gradient * inv_count);
        lattice_probe_interference_norm =
            clamp_norm_01(0.34 * lattice_probe_energy_norm +
                          0.30 * gradient_norm +
                          0.20 * lattice_probe_flux_norm +
                          0.10 * coherence_norm +
                          0.06 * source_vibration_norm);
        lattice_probe_temporal_coupling_norm =
            clamp_norm_01(0.40 * lattice_probe_interference_norm +
                          0.24 * source_vibration_norm +
                          0.18 * lattice_probe_flux_norm +
                          0.10 * coherence_norm +
                          0.08 * interference_norm);
    }

    void update_live_compute_state() {
        const bool active_before = live_compute_active;
        live_plan = genesis::GeResearchLiveComputePlan{};
        if (!live_compute_enabled) {
            live_compute_active = false;
            return;
        }
        if (!genesis::ge_build_research_live_compute_plan(archive, calibration, guidance, live_plan)) {
            live_compute_active = false;
            return;
        }

        const double activate_threshold = 0.72;
        const double release_threshold = 0.58;
        const bool can_activate = live_plan.ready && !watchdog_fault;
        if (!live_compute_active) {
            if (can_activate || live_plan.readiness_norm >= activate_threshold) {
                live_compute_active = true;
                live_compute_activation_tick_u64 = sm.canonical_tick_u64();
            }
        } else if (watchdog_fault || live_plan.readiness_norm < release_threshold) {
            live_compute_active = false;
        }

        if (active_before != live_compute_active) {
            ++live_compute_toggle_count_u32;
        }
    }

    void emit_research_tick(const genesis::GeResearchPulseQuartet& quartet,
                            const EwProcessSubstrateTelemetry* telemetry_opt) {
        sm.submit_gpu_pulse_sample_v2(pack_freq_hz(quartet.F), 4096u,
                                      static_cast<uint32_t>(pack_norm_u16(quartet.A)), 65535u,
                                      static_cast<uint32_t>(pack_norm_u16(quartet.V)), 65535u);

        const uint32_t cadence_u32 = std::max<uint32_t>(1u, guidance.correction_cadence_ticks_u32);
        const uint64_t tick_u64 = sm.canonical_tick_u64();
        const bool emit_pulse =
            !has_pulse_history ||
            (tick_u64 >= last_pulse_tick_u64 + static_cast<uint64_t>(cadence_u32));
        if (!emit_pulse) return;

        Pulse p{};
        p.anchor_id =
            (telemetry_opt != nullptr && telemetry_opt->spectral_anchor_id_u32 != 0u)
                ? telemetry_opt->spectral_anchor_id_u32
                : sm.spectral_field_anchor_id_u32;
        if (p.anchor_id == 0u) return;

        p.f_code = pack_freq_code(quartet.F);
        p.a_code = pack_norm_u16(quartet.A);
        p.i_code = pack_norm_u16(quartet.I);
        p.v_code = pack_norm_u16(quartet.V);
        p.profile_id = EW_PROFILE_CORE_EVOLUTION;
        p.causal_tag = 91u;
        p.tick = tick_u64;
        sm.submit_pulse(p);
        has_pulse_history = true;
        last_pulse_tick_u64 = tick_u64;
    }

    genesis::GeResearchPulseQuartet compute_control_for_second(double day01,
                                                               const EwProcessSubstrateTelemetry* telemetry_opt) {
        desired_quartet = scheduled_desired_quartet(archive, day01);
        guidance = genesis::GeResearchRuntimeGuidance{};
        calibration = genesis::GeResearchGpuAdaptiveCalibration{};

        const double residual_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->residual_norm_q15) : 0.0;
        const double interference_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->interference_norm_q15) : 0.0;
        const double coherence_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->coherence_norm_q15) : 0.0;
        const double source_vibration_norm =
            telemetry_opt ? q15_to_norm(telemetry_opt->source_vibration_q15) : 0.0;

        update_prediction_lattice_probe(telemetry_opt, desired_quartet);

        const double prediction_interference_norm =
            clamp_norm_01(0.58 * interference_norm + 0.42 * lattice_probe_interference_norm);
        const double prediction_coherence_norm =
            clamp_norm_01(0.74 * coherence_norm + 0.26 * lattice_probe_temporal_coupling_norm);
        const double prediction_source_vibration_norm =
            clamp_norm_01(0.68 * source_vibration_norm + 0.32 * lattice_probe_temporal_coupling_norm);

        std::vector<genesis::GeResearchInterferencePredictionCell> predictions;
        (void)genesis::ge_build_research_gpu_interference_predictions(
            archive,
            desired_quartet,
            telemetry_opt ? q15_to_norm(telemetry_opt->gpu_freq_norm_q15) : desired_quartet.F,
            telemetry_opt ? q15_to_norm(telemetry_opt->gpu_amp_norm_q15) : desired_quartet.A,
            telemetry_opt ? q15_to_norm(telemetry_opt->last_i_code_u16) : desired_quartet.I,
            telemetry_opt ? q15_to_norm(telemetry_opt->gpu_volt_norm_q15) : desired_quartet.V,
            prediction_interference_norm,
            prediction_coherence_norm,
            prediction_source_vibration_norm,
            kDefaultAxisResolution,
            calibration,
            &predictions);

        genesis::GeResearchPulseQuartet guidance_input = desired_quartet;
        if (calibration.valid) {
            const double blend =
                clamp_norm_01(0.18 + 0.42 * calibration.prediction_confidence_norm +
                              0.20 * calibration.best_gpu_alignment_norm +
                              0.20 * calibration.best_interference_norm);
            guidance_input.F += (calibration.adapted_quartet.F - guidance_input.F) * blend;
            guidance_input.A += (calibration.adapted_quartet.A - guidance_input.A) * blend;
            guidance_input.I += (calibration.adapted_quartet.I - guidance_input.I) * blend;
            guidance_input.V += (calibration.adapted_quartet.V - guidance_input.V) * blend;
        }

        if (!genesis::ge_build_research_runtime_guidance(archive,
                                                         guidance_input,
                                                         residual_norm,
                                                         interference_norm,
                                                         coherence_norm,
                                                         source_vibration_norm,
                                                         guidance)) {
            ++guidance_build_fail_count_u32;
            watchdog_fault = true;
            ++watchdog_fault_count_u32;
            update_live_compute_state();
            emitted_quartet = archive.run043_center_quartet;
            return emitted_quartet;
        }

        update_live_compute_state();

        const bool metrics_finite =
            std::isfinite(guidance.predicted_silicon_score) &&
            std::isfinite(guidance.predicted_trap_ratio) &&
            std::isfinite(guidance.predicted_coherence) &&
            std::isfinite(guidance.predicted_inertia) &&
            std::isfinite(guidance.predicted_curvature);
        const genesis::GeResearchPulseQuartet corrected = guidance.corrected_quartet;
        const bool corrected_valid =
            guidance.valid &&
            quartet_is_finite(corrected) &&
            corrected.F >= archive.pulse_window_min_quartet.F &&
            corrected.F <= archive.pulse_window_max_quartet.F &&
            corrected.A >= archive.pulse_window_min_quartet.A &&
            corrected.A <= archive.pulse_window_max_quartet.A &&
            corrected.I >= archive.pulse_window_min_quartet.I &&
            corrected.I <= archive.pulse_window_max_quartet.I &&
            corrected.V >= archive.pulse_window_min_quartet.V &&
            corrected.V <= archive.pulse_window_max_quartet.V &&
            metrics_finite;
        if (!corrected_valid) {
            ++guidance_out_of_window_count_u32;
            watchdog_fault = true;
            ++watchdog_fault_count_u32;
            emitted_quartet = archive.run043_center_quartet;
            return emitted_quartet;
        }

        watchdog_fault = false;
        emitted_quartet =
            (live_compute_active && live_plan.valid) ? live_plan.compute_quartet : corrected;
        if (calibration.valid) {
            const double temporal_prediction_blend =
                clamp_norm_01(0.14 +
                              0.28 * guidance.temporal_coupling_norm +
                              0.24 * calibration.best_temporal_coupling_norm +
                              0.22 * calibration.next_pulse_correction_norm +
                              0.12 * lattice_probe_temporal_coupling_norm);
            emitted_quartet.F += (calibration.next_pulse_quartet.F - emitted_quartet.F) * temporal_prediction_blend;
            emitted_quartet.A += (calibration.next_pulse_quartet.A - emitted_quartet.A) * temporal_prediction_blend;
            emitted_quartet.I += (calibration.next_pulse_quartet.I - emitted_quartet.I) * temporal_prediction_blend;
            emitted_quartet.V += (calibration.next_pulse_quartet.V - emitted_quartet.V) * temporal_prediction_blend;
        }

        const bool emitted_valid =
            quartet_is_finite(emitted_quartet) &&
            emitted_quartet.F >= archive.pulse_window_min_quartet.F &&
            emitted_quartet.F <= archive.pulse_window_max_quartet.F &&
            emitted_quartet.A >= archive.pulse_window_min_quartet.A &&
            emitted_quartet.A <= archive.pulse_window_max_quartet.A &&
            emitted_quartet.I >= archive.pulse_window_min_quartet.I &&
            emitted_quartet.I <= archive.pulse_window_max_quartet.I &&
            emitted_quartet.V >= archive.pulse_window_min_quartet.V &&
            emitted_quartet.V <= archive.pulse_window_max_quartet.V;
        if (!emitted_valid) {
            ++next_pulse_out_of_window_count_u32;
            watchdog_fault = true;
            ++watchdog_fault_count_u32;
            emitted_quartet = archive.run043_center_quartet;
        }
        return emitted_quartet;
    }
};

} // namespace

int main(int argc, char** argv) {
    ew::CliArgsKV cli{};
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, cli)) {
        std::fprintf(stderr, "ew_research_live_day_sim: malformed args\n");
        return 2;
    }

    std::string date_utf8 = "2026-03-31";
    uint32_t start_hour_u32 = kDefaultStartHour;
    uint32_t end_hour_u32 = kDefaultEndHour;
    uint32_t anchors_u32 = kDefaultAnchors;
    uint32_t sample_interval_seconds_u32 = kDefaultSampleIntervalSeconds;
    uint32_t observation_interval_seconds_u32 = kDefaultObservationIntervalSeconds;
    uint32_t rehearsal_ticks_per_sim_second_u32 = kDefaultRehearsalTicksPerSimSecond;
    uint32_t seconds_u32 = (kDefaultEndHour - kDefaultStartHour) * kSecondsPerHour;
    std::string report_utf8;

    (void)ew::ew_cli_get_str(cli, "date", date_utf8);
    (void)ew::ew_cli_get_u32(cli, "start_hour", start_hour_u32);
    (void)ew::ew_cli_get_u32(cli, "end_hour", end_hour_u32);
    (void)ew::ew_cli_get_u32(cli, "anchors", anchors_u32);
    (void)ew::ew_cli_get_u32(cli, "sample_interval_seconds", sample_interval_seconds_u32);
    (void)ew::ew_cli_get_u32(cli, "observation_interval_seconds", observation_interval_seconds_u32);
    (void)ew::ew_cli_get_u32(cli, "rehearsal_ticks_per_sim_second", rehearsal_ticks_per_sim_second_u32);
    (void)ew::ew_cli_get_u32(cli, "seconds", seconds_u32);
    (void)ew::ew_cli_get_str(cli, "report", report_utf8);

    if (end_hour_u32 <= start_hour_u32 && seconds_u32 == 0u) {
        std::fprintf(stderr, "ew_research_live_day_sim: end_hour must be greater than start_hour\n");
        return 2;
    }
    if (seconds_u32 == 0u) {
        seconds_u32 = (end_hour_u32 - start_hour_u32) * kSecondsPerHour;
    }
    if (sample_interval_seconds_u32 == 0u) sample_interval_seconds_u32 = kDefaultSampleIntervalSeconds;
    if (observation_interval_seconds_u32 == 0u) observation_interval_seconds_u32 = kDefaultObservationIntervalSeconds;
    if (rehearsal_ticks_per_sim_second_u32 == 0u) rehearsal_ticks_per_sim_second_u32 = kDefaultRehearsalTicksPerSimSecond;

    std::filesystem::path report_path;
    if (!report_utf8.empty()) {
        report_path = std::filesystem::path(report_utf8);
    } else {
        std::ostringstream name;
        name << "live_day_launch_rehearsal_" << date_utf8
             << "_" << std::setw(2) << std::setfill('0') << start_hour_u32
             << "00_" << std::setw(2) << std::setfill('0') << end_hour_u32
             << "00.json";
        report_path = std::filesystem::current_path() / "ResearchConfinement" / name.str();
    }

    auto runner = std::make_unique<RehearsalRunner>(anchors_u32);
    runner->init();

    std::string error_utf8;
    if (!runner->ready(error_utf8)) {
        std::fprintf(stderr, "ew_research_live_day_sim: %s\n", error_utf8.c_str());
        return 1;
    }

    const std::vector<std::string> observation_cycle = {
        "launch rehearsal calibration checkpoint",
        "gpu kernel pulse envelope update",
        "temporal coupling feedback audit",
        "process substrate api heartbeat",
        "lattice interference prediction refresh",
        "live compute readiness checkpoint",
        "observer effect correction sweep",
        "blueprint alignment continuity check"
    };

    Aggregates agg{};
    std::vector<TimeSeriesPoint> timeline;
    timeline.reserve(static_cast<size_t>(seconds_u32 / std::max<uint32_t>(sample_interval_seconds_u32, 1u) + 2u));

    EwProcessSubstrateTelemetry process_telemetry{};
    EwAiSubstrateTelemetry ai_telemetry{};
    EwAiDataSubstrateTelemetry ai_data_telemetry{};
    bool has_process_telemetry = runner->sm.get_process_substrate_telemetry(&process_telemetry);
    runner->compute_control_for_second(0.0, has_process_telemetry ? &process_telemetry : nullptr);

    const auto wall_start = std::chrono::steady_clock::now();

    for (uint32_t second_u32 = 0u; second_u32 < seconds_u32; ++second_u32) {
        const double day01 =
            (seconds_u32 != 0u) ? (static_cast<double>(second_u32) / static_cast<double>(seconds_u32)) : 0.0;

        runner->compute_control_for_second(day01, has_process_telemetry ? &process_telemetry : nullptr);
        runner->sm.submit_envelope_sample(scheduled_envelope_sample(runner->emitted_quartet, day01));

        if ((second_u32 % observation_interval_seconds_u32) == 0u) {
            const size_t obs_idx = (second_u32 / observation_interval_seconds_u32) % observation_cycle.size();
            runner->sm.observe_text_line(observation_cycle[obs_idx]);
        }

        for (uint32_t tick_step = 0u; tick_step < rehearsal_ticks_per_sim_second_u32; ++tick_step) {
            runner->emit_research_tick(runner->emitted_quartet, has_process_telemetry ? &process_telemetry : nullptr);
            runner->sm.tick();
        }

        has_process_telemetry = runner->sm.get_process_substrate_telemetry(&process_telemetry);
        const bool has_ai_telemetry = runner->sm.get_ai_substrate_telemetry(&ai_telemetry);
        const bool has_ai_data_telemetry = runner->sm.get_ai_data_substrate_telemetry(&ai_data_telemetry);

        const double residual_norm = has_process_telemetry ? q15_to_norm(process_telemetry.residual_norm_q15) : 0.0;
        const double interference_norm = has_process_telemetry ? q15_to_norm(process_telemetry.interference_norm_q15) : 0.0;
        const double source_vibration_norm = has_process_telemetry ? q15_to_norm(process_telemetry.source_vibration_q15) : 0.0;
        const double controller_authority_norm = has_process_telemetry ? q15_to_norm(process_telemetry.controller_authority_q15) : 0.0;
        const double calibration_authority_norm = has_process_telemetry ? q15_to_norm(process_telemetry.calibration_authority_q15) : 0.0;
        const double ai_confidence_norm =
            has_ai_telemetry ? norm_from_abs(static_cast<double>(ai_telemetry.confidence_q32_32) / 4294967296.0) : 0.0;

        agg.max_residual_norm = std::max(agg.max_residual_norm, residual_norm);
        agg.max_interference_norm = std::max(agg.max_interference_norm, interference_norm);
        agg.max_source_vibration_norm = std::max(agg.max_source_vibration_norm, source_vibration_norm);
        agg.max_controller_authority_norm = std::max(agg.max_controller_authority_norm, controller_authority_norm);
        agg.max_calibration_authority_norm = std::max(agg.max_calibration_authority_norm, calibration_authority_norm);
        agg.max_lattice_interference_norm = std::max(agg.max_lattice_interference_norm, runner->lattice_probe_interference_norm);
        agg.max_lattice_temporal_coupling_norm = std::max(agg.max_lattice_temporal_coupling_norm, runner->lattice_probe_temporal_coupling_norm);
        agg.max_prediction_confidence_norm = std::max(agg.max_prediction_confidence_norm, runner->calibration.prediction_confidence_norm);
        agg.max_readiness_norm = std::max(agg.max_readiness_norm, runner->live_plan.readiness_norm);
        agg.min_readiness_norm = std::min(agg.min_readiness_norm, runner->live_plan.readiness_norm);
        agg.mean_readiness_norm += runner->live_plan.readiness_norm;
        agg.max_ai_confidence_norm = std::max(agg.max_ai_confidence_norm, ai_confidence_norm);
        agg.max_pending_external_api_u32 = std::max<uint32_t>(agg.max_pending_external_api_u32, has_ai_telemetry ? ai_telemetry.pending_external_api_u32 : 0u);
        agg.max_inflight_external_api_u32 = std::max<uint32_t>(agg.max_inflight_external_api_u32, has_ai_telemetry ? ai_telemetry.inflight_external_api_u32 : 0u);
        agg.max_ai_command_count_u32 = std::max<uint32_t>(agg.max_ai_command_count_u32, has_ai_telemetry ? ai_telemetry.command_count_u32 : 0u);
        agg.max_ai_action_log_count_u32 = std::max<uint32_t>(agg.max_ai_action_log_count_u32, has_ai_telemetry ? ai_telemetry.action_log_count_u32 : 0u);
        agg.max_ai_data_pending_external_api_u32 = std::max<uint32_t>(agg.max_ai_data_pending_external_api_u32, has_ai_data_telemetry ? ai_data_telemetry.pending_external_api_u32 : 0u);
        agg.max_ai_data_inflight_external_api_u32 = std::max<uint32_t>(agg.max_ai_data_inflight_external_api_u32, has_ai_data_telemetry ? ai_data_telemetry.inflight_external_api_u32 : 0u);

        if (runner->live_compute_active) {
            ++runner->live_compute_active_seconds_u32;
        }

        const bool should_sample =
            (second_u32 == 0u) ||
            (((second_u32 + 1u) % sample_interval_seconds_u32) == 0u) ||
            (second_u32 + 1u == seconds_u32);
        if (should_sample) {
            TimeSeriesPoint p{};
            p.label_utf8 = format_local_time_label(date_utf8, start_hour_u32, second_u32 + 1u);
            p.tick_u64 = runner->sm.canonical_tick_u64();
            p.desired_f = runner->desired_quartet.F;
            p.desired_a = runner->desired_quartet.A;
            p.desired_i = runner->desired_quartet.I;
            p.desired_v = runner->desired_quartet.V;
            p.emitted_f = runner->emitted_quartet.F;
            p.emitted_a = runner->emitted_quartet.A;
            p.emitted_i = runner->emitted_quartet.I;
            p.emitted_v = runner->emitted_quartet.V;
            p.readiness_norm = runner->live_plan.readiness_norm;
            p.certainty_norm = runner->guidance.certainty_norm;
            p.residual_norm = residual_norm;
            p.interference_norm = interference_norm;
            p.source_vibration_norm = source_vibration_norm;
            p.lattice_interference_norm = runner->lattice_probe_interference_norm;
            p.lattice_temporal_coupling_norm = runner->lattice_probe_temporal_coupling_norm;
            p.ai_confidence_norm = ai_confidence_norm;
            p.pending_external_api_u32 = has_ai_telemetry ? ai_telemetry.pending_external_api_u32 : 0u;
            p.inflight_external_api_u32 = has_ai_telemetry ? ai_telemetry.inflight_external_api_u32 : 0u;
            p.live_compute_active_u32 = runner->live_compute_active ? 1u : 0u;
            p.watchdog_fault_u32 = runner->watchdog_fault ? 1u : 0u;
            timeline.push_back(p);
        }

        if (((second_u32 + 1u) % kSecondsPerHour) == 0u) {
            const uint32_t elapsed_hours = (second_u32 + 1u) / kSecondsPerHour;
            std::cout << "PROGRESS hour=" << elapsed_hours
                      << " tick=" << runner->sm.canonical_tick_u64()
                      << " live=" << (runner->live_compute_active ? 1 : 0)
                      << " readiness=" << runner->live_plan.readiness_norm
                      << " residual=" << residual_norm
                      << " interference=" << interference_norm
                      << " watchdog=" << (runner->watchdog_fault ? 1 : 0)
                      << "\n";
        }
    }

    const auto wall_end = std::chrono::steady_clock::now();
    const double wall_elapsed_seconds =
        std::chrono::duration<double>(wall_end - wall_start).count();

    if (seconds_u32 != 0u) {
        agg.mean_readiness_norm /= static_cast<double>(seconds_u32);
    }
    if (agg.min_readiness_norm > agg.max_readiness_norm) {
        agg.min_readiness_norm = agg.max_readiness_norm;
    }

    write_report_json(report_path,
                      date_utf8,
                      start_hour_u32,
                      end_hour_u32,
                      anchors_u32,
                      rehearsal_ticks_per_sim_second_u32,
                      runner->sm.canonical_tick_u64(),
                      seconds_u32,
                      wall_elapsed_seconds,
                      runner->watchdog_fault_count_u32,
                      runner->guidance_build_fail_count_u32,
                      runner->guidance_out_of_window_count_u32,
                      runner->next_pulse_out_of_window_count_u32,
                      runner->live_compute_toggle_count_u32,
                      runner->live_compute_active_seconds_u32,
                      runner->live_compute_activation_tick_u64,
                      agg,
                      runner->desired_quartet,
                      runner->emitted_quartet,
                      runner->guidance,
                      runner->calibration,
                      runner->live_plan,
                      timeline);

    std::cout << "LIVE_DAY_SIM report=" << report_path.string()
              << " date=" << date_utf8
              << " start_hour=" << start_hour_u32
              << " end_hour=" << end_hour_u32
              << " seconds=" << seconds_u32
              << " rehearsal_ticks_per_sim_second=" << rehearsal_ticks_per_sim_second_u32
              << " ticks=" << runner->sm.canonical_tick_u64()
              << " wall_seconds=" << wall_elapsed_seconds
              << " live_active_fraction="
              << ((seconds_u32 != 0u) ? (static_cast<double>(runner->live_compute_active_seconds_u32) / static_cast<double>(seconds_u32)) : 0.0)
              << " watchdog_faults=" << runner->watchdog_fault_count_u32
              << " readiness_mean=" << agg.mean_readiness_norm
              << " readiness_max=" << agg.max_readiness_norm
              << " residual_max=" << agg.max_residual_norm
              << " lattice_max=" << agg.max_lattice_interference_norm
              << "\n";
    return 0;
}
