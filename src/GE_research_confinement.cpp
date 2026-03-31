#include "GE_research_confinement.hpp"
#include "VirtualStateDrive.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

namespace genesis {

namespace {

static const GeResearchTaskSummary* ge_find_task_summary(const std::vector<GeResearchTaskSummary>& tasks,
                                                         const std::string& task_utf8);

static std::string ge_trim_ascii(std::string_view in) {
    size_t begin = 0u;
    while (begin < in.size()) {
        const char ch = in[begin];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
        ++begin;
    }
    size_t end = in.size();
    while (end > begin) {
        const char ch = in[end - 1u];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
        --end;
    }
    return std::string(in.substr(begin, end - begin));
}

static bool ge_read_text_file(const std::filesystem::path& path, std::string& out_text) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::ostringstream oss;
    oss << f.rdbuf();
    out_text = oss.str();
    return true;
}

static bool ge_read_binary_file(const std::filesystem::path& path, std::vector<uint8_t>& out_blob) {
    out_blob.clear();
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.good()) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size <= 0) return false;
    f.seekg(0, std::ios::beg);
    out_blob.resize((size_t)size);
    f.read(reinterpret_cast<char*>(out_blob.data()), size);
    return f.good();
}

static float ge_clamp01f_local(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static double ge_clamp01d_local(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static double ge_clampd_local(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static double ge_absd_local(double v) {
    return (v < 0.0) ? -v : v;
}

static double ge_lerpd_local(double a, double b, double t) {
    return a + (b - a) * t;
}

static double ge_softsign_local(double v) {
    const double av = ge_absd_local(v);
    return (av > 0.0) ? (v / (1.0 + av)) : 0.0;
}

static double ge_norm_from_abs_local(double v) {
    const double av = ge_absd_local(v);
    return av / (1.0 + av);
}

static double ge_quartet_distance_norm_local(const GeResearchPulseQuartet& a,
                                             const GeResearchPulseQuartet& b,
                                             const GeResearchPulseQuartet& qmin,
                                             const GeResearchPulseQuartet& qmax) {
    const double spanF = std::max(qmax.F - qmin.F, 1.0e-9);
    const double spanA = std::max(qmax.A - qmin.A, 1.0e-9);
    const double spanI = std::max(qmax.I - qmin.I, 1.0e-9);
    const double spanV = std::max(qmax.V - qmin.V, 1.0e-9);
    const double dF = ge_absd_local(a.F - b.F) / spanF;
    const double dA = ge_absd_local(a.A - b.A) / spanA;
    const double dI = ge_absd_local(a.I - b.I) / spanI;
    const double dV = ge_absd_local(a.V - b.V) / spanV;
    return ge_clamp01d_local((dF + dA + dI + dV) * 0.25);
}

static uint64_t ge_mix_u64_local(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6u) + (h >> 2u);
    return h;
}

static uint32_t ge_clamp_u32_i32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return (uint32_t)lo;
    if (v > hi) return (uint32_t)hi;
    return (uint32_t)v;
}

static bool ge_append_photon_volume_vsd(const std::filesystem::path& path,
                                        GeResearchConfinementArchive& archive) {
    std::vector<uint8_t> blob;
    if (!ge_read_binary_file(path, blob)) return false;
    if (blob.size() < 12u) return false;

    genesis::VirtualStateDrive drive;
    if (!drive.deserialize_binary(blob.data(), blob.size())) return false;

    uint64_t edge_u64 = 64u;
    double scale_f64 = 1.0;
    (void)drive.get_u64("photon/volume/edge", edge_u64);
    (void)drive.get_f64("photon/volume/scale", scale_f64);
    const uint32_t edge_u32 = (edge_u64 > 0u) ? (uint32_t)edge_u64 : 64u;
    const uint32_t max_edge = (edge_u32 > 512u) ? 512u : edge_u32;
    const float scale = (float)scale_f64;

    std::vector<GeVirtualStateRecord> records;
    drive.export_records(records);
    if (records.empty()) return false;

    const size_t max_points = 24000u;
    for (size_t i = 0u; i < records.size(); ++i) {
        if (archive.shader_texture.size() >= max_points) break;
        const GeVirtualStateRecord& rec = records[i];
        if (rec.key_utf8.find("photon/tensor/") != 0u) continue;

        const GeMetaFrequencyTensor& t = rec.tensor;
        const int32_t fx = t.channels.f_code;
        const float a = (float)t.channels.a_code / 65535.0f;
        const float v = (float)t.channels.v_code / 65535.0f;
        const float ic = (float)t.channels.i_code / 65535.0f;
        const float dft1 = (float)t.dft[1].magnitude_q15 / 65535.0f;
        const float dft2 = (float)t.dft[2].magnitude_q15 / 65535.0f;
        const float dft3 = (float)t.dft[3].magnitude_q15 / 65535.0f;
        const uint32_t local_edge = (t.lattice_edge_hint_u32 > max_edge) ? max_edge : t.lattice_edge_hint_u32;

        const double freq_phase = (double)(fx % 360) / 360.0;
        const double seed = (double)(i % 4096u) / 4096.0;
        const double radial = (0.18 + 0.64 * (double)dft2) * (double)scale;
        const double theta0 = 6.28318530718 * (freq_phase + seed);
        const double z_center = 0.50 + 0.22 * std::sin(theta0 * (1.0 + 3.0 * (double)v));
        const uint32_t replicas = 6u + (uint32_t)std::lround(6.0 * (double)dft1);

        for (uint32_t r = 0u; r < replicas; ++r) {
            if (archive.shader_texture.size() >= max_points) break;
            const double tr = theta0 + 6.28318530718 * ((double)r / (double)replicas);
            const double x01 = 0.5 + radial * std::cos(tr);
            const double y01 = 0.5 + radial * std::sin(tr);
            const double z01 = z_center + 0.12 * std::cos(tr * (1.0 + 2.0 * (double)ic));
            const int32_t xi = (int32_t)std::llround(std::max(0.0, std::min(1.0, x01)) * (double)(local_edge - 1u));
            const int32_t yi = (int32_t)std::llround(std::max(0.0, std::min(1.0, y01)) * (double)(local_edge - 1u));
            const int32_t zi = (int32_t)std::llround(std::max(0.0, std::min(1.0, z01)) * (double)(local_edge - 1u));

            GeResearchShaderTextureSample sample{};
            sample.x_u32 = ge_clamp_u32_i32(xi, 0, (int32_t)local_edge - 1);
            sample.y_u32 = ge_clamp_u32_i32(yi, 0, (int32_t)local_edge - 1);
            sample.z_u32 = ge_clamp_u32_i32(zi, 0, (int32_t)local_edge - 1);
            sample.color_r = ge_clamp01f_local(0.20f + 0.75f * dft1);
            sample.color_g = ge_clamp01f_local(0.15f + 0.75f * a);
            sample.color_b = ge_clamp01f_local(0.20f + 0.70f * (0.5f * dft3 + 0.5f * ic));
            archive.shader_texture.push_back(sample);
        }
    }
    return true;
}
static std::string ge_get_env_utf8(const char* name_ascii) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0u;
    if (_dupenv_s(&value, &len, name_ascii) != 0 || value == nullptr) {
        return std::string();
    }
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name_ascii);
    return value ? std::string(value) : std::string();
#endif
}

static bool ge_parse_uint32_ascii(const std::string& text, uint32_t& out_value) {
    try {
        const unsigned long v = std::stoul(text);
        if (v > 0xFFFFFFFFul) return false;
        out_value = static_cast<uint32_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

static bool ge_parse_double_ascii(const std::string& text, double& out_value) {
    try {
        out_value = std::stod(text);
        return true;
    } catch (...) {
        return false;
    }
}

static std::vector<std::string> ge_split_csv_ascii(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : line) {
        if (ch == ',') {
            out.push_back(ge_trim_ascii(cur));
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    out.push_back(ge_trim_ascii(cur));
    return out;
}

static std::string ge_strip_line_comments_ascii(const std::string& text) {
    std::istringstream iss(text);
    std::ostringstream oss;
    std::string line;
    while (std::getline(iss, line)) {
        const std::string trimmed = ge_trim_ascii(line);
        if (trimmed.rfind("//", 0u) == 0u) continue;
        oss << line << '\n';
    }
    return oss.str();
}

static bool ge_extract_json_array_numbers(const std::string& text,
                                          const std::string& key,
                                          std::vector<double>& out_values) {
    out_values.clear();
    const std::string needle = "\"" + key + "\"";
    const size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) return false;
    const size_t bracket_begin = text.find('[', key_pos + needle.size());
    if (bracket_begin == std::string::npos) return false;

    int depth = 0;
    size_t bracket_end = std::string::npos;
    for (size_t i = bracket_begin; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                bracket_end = i;
                break;
            }
        }
    }
    if (bracket_end == std::string::npos || bracket_end <= bracket_begin) return false;

    const std::string body = text.substr(bracket_begin + 1u, bracket_end - bracket_begin - 1u);
    const std::vector<std::string> cols = ge_split_csv_ascii(body);
    for (const std::string& col : cols) {
        if (col.empty()) continue;
        double value = 0.0;
        if (!ge_parse_double_ascii(col, value)) return false;
        out_values.push_back(value);
    }
    return !out_values.empty();
}

static bool ge_extract_top_level_json_objects(const std::string& text,
                                              std::vector<std::string>& out_objects) {
    out_objects.clear();
    const std::string sanitized = ge_strip_line_comments_ascii(text);
    bool in_string = false;
    bool escape = false;
    int depth = 0;
    size_t object_begin = std::string::npos;
    for (size_t i = 0u; i < sanitized.size(); ++i) {
        const char ch = sanitized[i];
        if (escape) {
            escape = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escape = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == '{') {
            if (depth == 0) object_begin = i;
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && object_begin != std::string::npos) {
                out_objects.push_back(sanitized.substr(object_begin, i - object_begin + 1u));
                object_begin = std::string::npos;
            }
        }
    }
    return !out_objects.empty();
}

static bool ge_extract_json_string(const std::string& text,
                                   const std::string& key,
                                   std::string& out_value) {
    const std::string needle = "\"" + key + "\"";
    const size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) return false;
    const size_t colon_pos = text.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) return false;
    const size_t quote_begin = text.find('"', colon_pos + 1u);
    if (quote_begin == std::string::npos) return false;
    const size_t quote_end = text.find('"', quote_begin + 1u);
    if (quote_end == std::string::npos) return false;
    out_value = text.substr(quote_begin + 1u, quote_end - quote_begin - 1u);
    return true;
}

static bool ge_extract_json_number(const std::string& text,
                                   const std::string& key,
                                   double& out_value) {
    const std::string needle = "\"" + key + "\"";
    const size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) return false;
    const size_t colon_pos = text.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) return false;

    size_t num_begin = colon_pos + 1u;
    while (num_begin < text.size()) {
        const char ch = text[num_begin];
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.') break;
        ++num_begin;
    }
    if (num_begin >= text.size()) return false;

    size_t num_end = num_begin;
    while (num_end < text.size()) {
        const char ch = text[num_end];
        const bool numeric = ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' ||
                              ch == '.' || ch == 'e' || ch == 'E');
        if (!numeric) break;
        ++num_end;
    }
    return ge_parse_double_ascii(text.substr(num_begin, num_end - num_begin), out_value);
}

static double ge_extract_json_number_or_default(const std::string& text,
                                                const std::string& key,
                                                double default_value) {
    double out_value = default_value;
    (void)ge_extract_json_number(text, key, out_value);
    return out_value;
}

static bool ge_extract_json_object(const std::string& text,
                                   const std::string& key,
                                   std::string& out_object) {
    const std::string needle = "\"" + key + "\"";
    const size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) return false;
    const size_t brace_begin = text.find('{', key_pos + needle.size());
    if (brace_begin == std::string::npos) return false;

    int depth = 0;
    for (size_t i = brace_begin; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                out_object = text.substr(brace_begin, i - brace_begin + 1u);
                return true;
            }
        }
    }
    return false;
}

static bool ge_parse_pulse_quartet_object(const std::string& text,
                                          GeResearchPulseQuartet& out_quartet) {
    GeResearchPulseQuartet out{};
    const bool ok =
        ge_extract_json_number(text, "F", out.F) &&
        ge_extract_json_number(text, "A", out.A) &&
        ge_extract_json_number(text, "I", out.I) &&
        ge_extract_json_number(text, "V", out.V);
    if (!ok) return false;
    out_quartet = out;
    return true;
}

static bool ge_parse_pulse_window_object(const std::string& text,
                                         GeResearchPulseQuartet& out_min,
                                         GeResearchPulseQuartet& out_max) {
    std::string freq_obj;
    std::string amp_obj;
    std::string ampere_obj;
    std::string volt_obj;
    if (!ge_extract_json_object(text, "frequency", freq_obj) ||
        !ge_extract_json_object(text, "amplitude", amp_obj) ||
        !ge_extract_json_object(text, "amperage", ampere_obj) ||
        !ge_extract_json_object(text, "voltage", volt_obj)) {
        return false;
    }

    GeResearchPulseQuartet qmin{};
    GeResearchPulseQuartet qmax{};
    const bool ok =
        ge_extract_json_number(freq_obj, "min", qmin.F) &&
        ge_extract_json_number(freq_obj, "max", qmax.F) &&
        ge_extract_json_number(amp_obj, "min", qmin.A) &&
        ge_extract_json_number(amp_obj, "max", qmax.A) &&
        ge_extract_json_number(ampere_obj, "min", qmin.I) &&
        ge_extract_json_number(ampere_obj, "max", qmax.I) &&
        ge_extract_json_number(volt_obj, "min", qmin.V) &&
        ge_extract_json_number(volt_obj, "max", qmax.V);
    if (!ok) return false;

    out_min = qmin;
    out_max = qmax;
    return true;
}

static bool ge_parse_metric_model_object(const std::string& text,
                                         const char* key_ascii,
                                         GeResearchQuadraticMetricModel& out_model) {
    std::string model_object;
    if (!ge_extract_json_object(text, key_ascii, model_object)) return false;

    std::string jacobian_obj;
    std::string hessian_diag_obj;
    std::string hessian_cross_obj;
    if (!ge_extract_json_object(model_object, "jacobian", jacobian_obj) ||
        !ge_extract_json_object(model_object, "hessian_diag", hessian_diag_obj) ||
        !ge_extract_json_object(model_object, "hessian_cross", hessian_cross_obj)) {
        return false;
    }

    GeResearchQuadraticMetricModel model{};
    (void)ge_extract_json_string(model_object, "metric", model.metric_utf8);
    (void)ge_extract_json_string(model_object, "deviation_formula", model.deviation_formula_utf8);
    const bool ok =
        ge_extract_json_number(model_object, "center_value", model.center_value) &&
        ge_parse_pulse_quartet_object(jacobian_obj, model.jacobian) &&
        ge_parse_pulse_quartet_object(hessian_diag_obj, model.hessian_diag) &&
        ge_extract_json_number(hessian_cross_obj, "FA", model.hessian_FA) &&
        ge_extract_json_number(hessian_cross_obj, "FI", model.hessian_FI) &&
        ge_extract_json_number(hessian_cross_obj, "FV", model.hessian_FV) &&
        ge_extract_json_number(hessian_cross_obj, "AI", model.hessian_AI) &&
        ge_extract_json_number(hessian_cross_obj, "AV", model.hessian_AV) &&
        ge_extract_json_number(hessian_cross_obj, "IV", model.hessian_IV);
    if (!ok) return false;

    out_model = model;
    return true;
}

static bool ge_parse_run043_summary_file(const std::filesystem::path& path,
                                         GeResearchConfinementArchive& archive) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;

    std::string center_obj;
    std::string delta_obj;
    std::string models_obj;
    std::string validation_obj;
    if (!ge_extract_json_object(text, "center_quartet", center_obj) ||
        !ge_extract_json_object(text, "delta_quartet", delta_obj) ||
        !ge_extract_json_object(text, "models", models_obj) ||
        !ge_extract_json_object(text, "validation_error", validation_obj)) {
        return false;
    }

    if (!ge_parse_pulse_quartet_object(center_obj, archive.run043_center_quartet) ||
        !ge_parse_pulse_quartet_object(delta_obj, archive.run043_delta_quartet) ||
        !ge_parse_metric_model_object(models_obj, "silicon_score", archive.run043_silicon_score_model) ||
        !ge_parse_metric_model_object(models_obj, "trap_ratio", archive.run043_trap_ratio_model) ||
        !ge_parse_metric_model_object(models_obj, "coherence", archive.run043_coherence_model) ||
        !ge_parse_metric_model_object(models_obj, "inertia", archive.run043_inertia_model) ||
        !ge_parse_metric_model_object(models_obj, "curvature", archive.run043_curvature_model)) {
        return false;
    }

    if (!ge_extract_json_number(validation_obj, "silicon_score_mae", archive.run043_validation_error.silicon_score_mae) ||
        !ge_extract_json_number(validation_obj, "silicon_score_rmse", archive.run043_validation_error.silicon_score_rmse)) {
        return false;
    }

    archive.run043_loaded = true;
    return true;
}

static bool ge_parse_temporal_coupling_schema_file(const std::filesystem::path& path,
                                                   GeResearchConfinementArchive& archive) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;

    std::string pulse_codes_obj;
    std::string normalized_window_obj;
    std::string collapse_gates_obj;
    if (!ge_extract_json_object(text, "pulse_codes", pulse_codes_obj) ||
        !ge_extract_json_object(pulse_codes_obj, "normalized_window", normalized_window_obj) ||
        !ge_extract_json_object(text, "collapse_gates", collapse_gates_obj)) {
        return false;
    }

    if (!ge_extract_json_number(pulse_codes_obj, "f_code", archive.run043_center_quartet.F) ||
        !ge_extract_json_number(pulse_codes_obj, "a_code", archive.run043_center_quartet.A) ||
        !ge_extract_json_number(pulse_codes_obj, "i_code", archive.run043_center_quartet.I) ||
        !ge_extract_json_number(pulse_codes_obj, "v_code", archive.run043_center_quartet.V)) {
        return false;
    }
    if (!ge_parse_pulse_window_object(normalized_window_obj,
                                      archive.pulse_window_min_quartet,
                                      archive.pulse_window_max_quartet)) {
        return false;
    }
    if (!ge_extract_json_number(collapse_gates_obj, "gate_coherence", archive.temporal_collapse_gates.gate_coherence) ||
        !ge_extract_json_number(collapse_gates_obj, "gate_trap", archive.temporal_collapse_gates.gate_trap) ||
        !ge_extract_json_number(collapse_gates_obj, "gate_score", archive.temporal_collapse_gates.gate_score)) {
        return false;
    }

    return true;
}

static bool ge_parse_task_summary(const std::string& text,
                                  const char* task_name,
                                  GeResearchTaskSummary& out_summary) {
    std::string task_object;
    if (!ge_extract_json_object(text, task_name, task_object)) return false;

    out_summary = GeResearchTaskSummary{};
    out_summary.task_utf8 = task_name;
    const bool ok =
        ge_extract_json_number(task_object, "freq_norm", out_summary.freq_norm) &&
        ge_extract_json_number(task_object, "amp_norm", out_summary.amp_norm) &&
        ge_extract_json_number(task_object, "volt_norm", out_summary.volt_norm) &&
        ge_extract_json_number(task_object, "curr_norm", out_summary.curr_norm) &&
        ge_extract_json_number(task_object, "lattice_lock_fraction", out_summary.lattice_lock_fraction) &&
        ge_extract_json_number(task_object, "composite_score", out_summary.composite_score) &&
        ge_extract_json_number(task_object, "return_distance_a", out_summary.return_distance_a);
    if (!ok) return false;

    out_summary.pulse_period_steps =
        ge_extract_json_number_or_default(task_object, "pulse_period_steps", 0.0);
    out_summary.phase_lock_fraction =
        ge_extract_json_number_or_default(task_object, "phase_lock_fraction", 0.0);
    out_summary.joint_lock_fraction =
        ge_extract_json_number_or_default(task_object, "joint_lock_fraction", 0.0);
    out_summary.speed_tracking_score =
        ge_extract_json_number_or_default(task_object, "speed_tracking_score", 0.0);
    out_summary.curvature_tracking_score =
        ge_extract_json_number_or_default(task_object, "curvature_tracking_score", 0.0);
    out_summary.feedback_to_signal_ratio =
        ge_extract_json_number_or_default(task_object, "feedback_to_signal_ratio", 0.0);
    out_summary.mean_energy_drift =
        ge_extract_json_number_or_default(task_object, "mean_energy_drift", 0.0);
    out_summary.recurrence_alignment =
        ge_extract_json_number_or_default(task_object, "recurrence_alignment", 0.0);
    out_summary.conservation_alignment =
        ge_extract_json_number_or_default(task_object, "conservation_alignment", 0.0);
    return true;
}

static bool ge_parse_summary_file(const std::filesystem::path& path,
                                  std::vector<GeResearchTaskSummary>& out_tasks,
                                  double* out_lattice_constant_m) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;

    out_tasks.clear();
    GeResearchTaskSummary summary{};
    if (ge_parse_task_summary(text, "D_track", summary)) out_tasks.push_back(summary);
    if (ge_parse_task_summary(text, "I_accum", summary)) out_tasks.push_back(summary);
    if (ge_parse_task_summary(text, "L_smooth", summary)) out_tasks.push_back(summary);

    if (out_lattice_constant_m) {
        double lattice_constant_m = 0.0;
        if (ge_extract_json_number(text, "silicon_lattice_constant_m", lattice_constant_m)) {
            *out_lattice_constant_m = lattice_constant_m;
        }
    }
    return !out_tasks.empty();
}

static bool ge_parse_nist_summary_file(const std::filesystem::path& path,
                                       GeResearchNistSummary& out_summary) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;
    out_summary = GeResearchNistSummary{};
    return ge_extract_json_number(text, "surrogate_frequency_scale_MHz_per_step", out_summary.surrogate_frequency_scale_mhz_per_step) &&
           ge_extract_json_number(text, "step_to_linewidth_ratio", out_summary.step_to_linewidth_ratio) &&
           ge_extract_json_number(text, "step_to_abs_uncertainty_ratio", out_summary.step_to_abs_uncertainty_ratio) &&
           ge_extract_json_string(text, "verdict", out_summary.verdict_utf8);
}

static bool ge_parse_nist_silicon_reference_file(const std::filesystem::path& path,
                                                 GeNistSiliconReference& out_reference) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;

    out_reference = GeNistSiliconReference{};
    out_reference.source_path = path;
    return ge_extract_json_number(text, "lattice_spacing_d220_m", out_reference.lattice_spacing_d220_m) &&
           ge_extract_json_number(text, "lattice_constant_m", out_reference.lattice_constant_m) &&
           ge_extract_json_number(text, "density_g_cm3", out_reference.density_g_cm3) &&
           ge_extract_json_number(text, "z_over_a", out_reference.z_over_a) &&
           ge_extract_json_number(text, "atomic_weight_u", out_reference.atomic_weight_u) &&
           ge_extract_json_number(text, "mean_excitation_energy_ev", out_reference.mean_excitation_energy_ev) &&
           ge_extract_json_number(text, "first_ionization_energy_ev", out_reference.first_ionization_energy_ev) &&
           ge_extract_json_number(text, "k_edge_energy_ev", out_reference.k_edge_energy_ev) &&
           ge_extract_json_number(text, "mass_attenuation_10kev_cm2_g", out_reference.mass_attenuation_10kev_cm2_g) &&
           ge_extract_json_number(text, "mass_energy_absorption_10kev_cm2_g", out_reference.mass_energy_absorption_10kev_cm2_g) &&
           ge_extract_json_string(text, "inference_note", out_reference.inference_note_utf8);
}

static bool ge_build_nist_surrogate_fallback(const std::filesystem::path& root,
                                             const std::vector<GeResearchTaskSummary>& run041_tasks,
                                             GeResearchNistSummary& out_summary) {
    GeNistSiliconReference reference{};
    if (!ge_parse_nist_silicon_reference_file(root / "nist_silicon_reference.json", reference)) {
        return false;
    }

    const GeResearchTaskSummary* d_track = ge_find_task_summary(run041_tasks, "D_track");
    const double pulse_period_steps = (d_track && d_track->pulse_period_steps > 0.0)
                                          ? d_track->pulse_period_steps
                                          : 1.0;
    const double mean_energy_drift = (d_track && d_track->mean_energy_drift > 0.0)
                                         ? d_track->mean_energy_drift
                                         : 1.0e-9;

    out_summary = GeResearchNistSummary{};
    out_summary.surrogate_frequency_scale_mhz_per_step =
        (reference.mean_excitation_energy_ev * 241799050.4024) / pulse_period_steps;
    out_summary.step_to_linewidth_ratio =
        reference.mean_excitation_energy_ev / std::max(mean_energy_drift * 1.0e6, 1.0e-9);
    out_summary.step_to_abs_uncertainty_ratio =
        reference.k_edge_energy_ev / std::max(reference.first_ionization_energy_ev, 1.0e-9);
    out_summary.verdict_utf8 = "not_within_nist_uncertainty_scale";
    return true;
}

static bool ge_parse_history_csv(const std::filesystem::path& path,
                                 std::vector<GeResearchTrajectorySample>& out_samples) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    if (!std::getline(f, line)) return false;

    out_samples.clear();
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> cols = ge_split_csv_ascii(line);
        if (cols.size() < 15u) continue;

        GeResearchTrajectorySample sample{};
        sample.task_utf8 = cols[0];
        if (!ge_parse_uint32_ascii(cols[1], sample.step_u32)) continue;
        if (!ge_parse_double_ascii(cols[2], sample.sent_signal)) continue;
        if (!ge_parse_double_ascii(cols[3], sample.noise_feedback)) continue;
        if (!ge_parse_double_ascii(cols[4], sample.phase_error)) continue;
        if (!ge_parse_double_ascii(cols[5], sample.lattice_distance_a)) continue;
        if (!ge_parse_double_ascii(cols[6], sample.x_m)) continue;
        if (!ge_parse_double_ascii(cols[7], sample.y_m)) continue;
        if (!ge_parse_double_ascii(cols[8], sample.z_m)) continue;
        if (!ge_parse_double_ascii(cols[9], sample.vx)) continue;
        if (!ge_parse_double_ascii(cols[10], sample.vy)) continue;
        if (!ge_parse_double_ascii(cols[11], sample.vz)) continue;
        if (!ge_parse_double_ascii(cols[12], sample.speed)) continue;
        if (!ge_parse_double_ascii(cols[13], sample.curvature)) continue;
        if (!ge_parse_double_ascii(cols[14], sample.energy_drift)) continue;
        out_samples.push_back(sample);
    }
    return !out_samples.empty();
}

static bool ge_parse_packet_path_classification_file(
    const std::filesystem::path& path,
    std::vector<GeResearchPacketPathClassification>& out_samples) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;

    std::vector<std::string> objects;
    if (!ge_extract_top_level_json_objects(text, objects)) return false;

    out_samples.clear();
    for (const std::string& object_text : objects) {
        GeResearchPacketPathClassification sample{};
        std::string classification_utf8;
        if (!ge_extract_json_string(object_text, "classification", classification_utf8)) continue;
        double packet_id_d = 0.0;
        double group_id_d = 0.0;
        if (!ge_extract_json_number(object_text, "packet_id", packet_id_d) ||
            !ge_extract_json_number(object_text, "group_id", group_id_d) ||
            !ge_extract_json_number(object_text, "phase_lock_score", sample.phase_lock_score) ||
            !ge_extract_json_number(object_text, "curvature_depth", sample.curvature_depth) ||
            !ge_extract_json_number(object_text, "coherence_score", sample.coherence_score)) {
            continue;
        }
        sample.packet_id_u64 = static_cast<uint64_t>(packet_id_d);
        sample.group_id_u32 = static_cast<uint32_t>(group_id_d);
        sample.shared_path = (classification_utf8 == "shared" || classification_utf8 == "phase_locked");
        out_samples.push_back(sample);
    }
    return !out_samples.empty();
}

static bool ge_parse_tensor6d_file(const std::filesystem::path& path,
                                   std::vector<GeResearchTensor6DCell>& out_samples) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;

    std::vector<std::string> objects;
    if (!ge_extract_top_level_json_objects(text, objects)) return false;

    out_samples.clear();
    for (const std::string& object_text : objects) {
        GeResearchTensor6DCell sample{};
        double x_d = 0.0;
        double y_d = 0.0;
        double z_d = 0.0;
        std::vector<double> spin;
        if (!ge_extract_json_number(object_text, "x", x_d) ||
            !ge_extract_json_number(object_text, "y", y_d) ||
            !ge_extract_json_number(object_text, "z", z_d) ||
            !ge_extract_json_number(object_text, "phase_coherence", sample.phase_coherence) ||
            !ge_extract_json_number(object_text, "curvature", sample.curvature) ||
            !ge_extract_json_number(object_text, "flux", sample.flux) ||
            !ge_extract_json_number(object_text, "inertia", sample.inertia) ||
            !ge_extract_json_number(object_text, "freq_x", sample.freq_x) ||
            !ge_extract_json_number(object_text, "freq_y", sample.freq_y) ||
            !ge_extract_json_number(object_text, "freq_z", sample.freq_z) ||
            !ge_extract_json_number(object_text, "dtheta_dt", sample.dtheta_dt) ||
            !ge_extract_json_number(object_text, "d2theta_dt2", sample.d2theta_dt2) ||
            !ge_extract_json_number(object_text, "oam_twist", sample.oam_twist) ||
            !ge_extract_json_number(object_text, "higgs_inertia", sample.higgs_inertia) ||
            !ge_extract_json_array_numbers(object_text, "spin_vector", spin) ||
            spin.size() < 3u) {
            continue;
        }
        sample.x_u32 = static_cast<uint32_t>(x_d);
        sample.y_u32 = static_cast<uint32_t>(y_d);
        sample.z_u32 = static_cast<uint32_t>(z_d);
        sample.spin_x = spin[0];
        sample.spin_y = spin[1];
        sample.spin_z = spin[2];
        out_samples.push_back(sample);
    }
    return !out_samples.empty();
}

static bool ge_parse_vector_excitation_file(
    const std::filesystem::path& path,
    std::vector<GeResearchVectorExcitationSample>& out_samples) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;

    std::vector<std::string> objects;
    if (!ge_extract_top_level_json_objects(text, objects)) return false;

    out_samples.clear();
    for (const std::string& object_text : objects) {
        GeResearchVectorExcitationSample sample{};
        std::vector<double> spin;
        if (!ge_extract_json_number(object_text, "x", sample.x) ||
            !ge_extract_json_number(object_text, "y", sample.y) ||
            !ge_extract_json_number(object_text, "z", sample.z) ||
            !ge_extract_json_number(object_text, "vec_x", sample.vec_x) ||
            !ge_extract_json_number(object_text, "vec_y", sample.vec_y) ||
            !ge_extract_json_number(object_text, "vec_z", sample.vec_z) ||
            !ge_extract_json_number(object_text, "oam_twist", sample.oam_twist) ||
            !ge_extract_json_array_numbers(object_text, "spin", spin) ||
            spin.size() < 3u) {
            continue;
        }
        sample.spin_x = spin[0];
        sample.spin_y = spin[1];
        sample.spin_z = spin[2];
        out_samples.push_back(sample);
    }
    return !out_samples.empty();
}

static bool ge_parse_tensor_glyph_file(const std::filesystem::path& path,
                                       std::vector<GeResearchTensorGlyphSample>& out_samples) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;

    std::vector<std::string> objects;
    if (!ge_extract_top_level_json_objects(text, objects)) return false;

    out_samples.clear();
    for (const std::string& object_text : objects) {
        GeResearchTensorGlyphSample sample{};
        std::vector<double> tensor_rows;
        std::vector<double> color;
        if (!ge_extract_json_number(object_text, "x", sample.x) ||
            !ge_extract_json_number(object_text, "y", sample.y) ||
            !ge_extract_json_number(object_text, "z", sample.z) ||
            !ge_extract_json_array_numbers(object_text, "color", color) ||
            color.size() < 3u) {
            continue;
        }
        std::string tensor_text;
        const std::string needle = "\"tensor\"";
        const size_t key_pos = object_text.find(needle);
        if (key_pos == std::string::npos) continue;
        const size_t array_begin = object_text.find('[', key_pos + needle.size());
        if (array_begin == std::string::npos) continue;
        int depth = 0;
        size_t array_end = std::string::npos;
        for (size_t i = array_begin; i < object_text.size(); ++i) {
            if (object_text[i] == '[') ++depth;
            else if (object_text[i] == ']') {
                --depth;
                if (depth == 0) {
                    array_end = i;
                    break;
                }
            }
        }
        if (array_end == std::string::npos) continue;
        tensor_text = object_text.substr(array_begin, array_end - array_begin + 1u);
        for (char& ch : tensor_text) {
            if (ch == '[' || ch == ']') ch = ' ';
        }
        const std::vector<std::string> cols = ge_split_csv_ascii(tensor_text);
        for (const std::string& col : cols) {
            if (col.empty()) continue;
            double value = 0.0;
            if (!ge_parse_double_ascii(col, value)) {
                tensor_rows.clear();
                break;
            }
            tensor_rows.push_back(value);
        }
        if (tensor_rows.size() < 9u) continue;
        sample.tensor_00 = tensor_rows[0];
        sample.tensor_01 = tensor_rows[1];
        sample.tensor_02 = tensor_rows[2];
        sample.tensor_10 = tensor_rows[3];
        sample.tensor_11 = tensor_rows[4];
        sample.tensor_12 = tensor_rows[5];
        sample.tensor_20 = tensor_rows[6];
        sample.tensor_21 = tensor_rows[7];
        sample.tensor_22 = tensor_rows[8];
        sample.color_r = color[0];
        sample.color_g = color[1];
        sample.color_b = color[2];
        out_samples.push_back(sample);
    }
    return !out_samples.empty();
}

static bool ge_parse_shader_texture_file(
    const std::filesystem::path& path,
    std::vector<GeResearchShaderTextureSample>& out_samples) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;

    std::vector<std::string> objects;
    if (!ge_extract_top_level_json_objects(text, objects)) return false;

    out_samples.clear();
    for (const std::string& object_text : objects) {
        GeResearchShaderTextureSample sample{};
        double x_d = 0.0;
        double y_d = 0.0;
        double z_d = 0.0;
        std::vector<double> rgb;
        if (!ge_extract_json_number(object_text, "x", x_d) ||
            !ge_extract_json_number(object_text, "y", y_d) ||
            !ge_extract_json_number(object_text, "z", z_d) ||
            !ge_extract_json_array_numbers(object_text, "rgb", rgb) ||
            rgb.size() < 3u) {
            continue;
        }
        sample.x_u32 = static_cast<uint32_t>(x_d);
        sample.y_u32 = static_cast<uint32_t>(y_d);
        sample.z_u32 = static_cast<uint32_t>(z_d);
        sample.color_r = rgb[0];
        sample.color_g = rgb[1];
        sample.color_b = rgb[2];
        out_samples.push_back(sample);
    }
    return !out_samples.empty();
}

static bool ge_parse_audio_waveform_file(
    const std::filesystem::path& path,
    std::vector<GeResearchAudioWaveSample>& out_samples) {
    std::string text;
    if (!ge_read_text_file(path, text)) return false;

    std::vector<std::string> objects;
    if (!ge_extract_top_level_json_objects(text, objects)) return false;

    out_samples.clear();
    for (const std::string& object_text : objects) {
        GeResearchAudioWaveSample sample{};
        std::vector<double> channels;
        if (!ge_extract_json_number(object_text, "time", sample.time_s) ||
            !ge_extract_json_array_numbers(object_text, "channels", channels) ||
            channels.size() < 4u) {
            continue;
        }
        sample.channel_0 = (float)channels[0];
        sample.channel_1 = (float)channels[1];
        sample.channel_2 = (float)channels[2];
        sample.channel_3 = (float)channels[3];
        out_samples.push_back(sample);
    }
    return !out_samples.empty();
}

static const GeResearchTaskSummary* ge_find_task_summary(const std::vector<GeResearchTaskSummary>& tasks,
                                                         const std::string& task_utf8) {
    for (const GeResearchTaskSummary& task : tasks) {
        if (task.task_utf8 == task_utf8) return &task;
    }
    return nullptr;
}

static float ge_clamp01f(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static uint32_t ge_pack_rgba8(float r, float g, float b, float a) {
    const uint32_t rr = (uint32_t)std::lround((double)(ge_clamp01f(r) * 255.0f));
    const uint32_t gg = (uint32_t)std::lround((double)(ge_clamp01f(g) * 255.0f));
    const uint32_t bb = (uint32_t)std::lround((double)(ge_clamp01f(b) * 255.0f));
    const uint32_t aa = (uint32_t)std::lround((double)(ge_clamp01f(a) * 255.0f));
    return rr | (gg << 8u) | (bb << 16u) | (aa << 24u);
}

static float ge_lerp_f32(float a, float b, float t) {
    return a + (b - a) * t;
}

static void ge_normalize_vec3(float& x, float& y, float& z) {
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len > 1.0e-6f) {
        x /= len;
        y /= len;
        z /= len;
    } else {
        x = 0.0f;
        y = 0.0f;
        z = 1.0f;
    }
}

struct GeTrackStats {
    double max_speed = 1.0;
    double max_curvature = 1.0;
    double max_energy_drift = 1.0;
    double max_lattice_distance = 1.0;
};

static GeTrackStats ge_collect_track_stats(const GeResearchConfinementArchive& archive) {
    GeTrackStats stats{};
    auto visit = [&](const std::vector<GeResearchTrajectorySample>& samples) {
        for (const GeResearchTrajectorySample& sample : samples) {
            stats.max_speed = std::max(stats.max_speed, std::fabs(sample.speed));
            stats.max_curvature = std::max(stats.max_curvature, std::fabs(sample.curvature));
            stats.max_energy_drift = std::max(stats.max_energy_drift, std::fabs(sample.energy_drift));
            stats.max_lattice_distance = std::max(stats.max_lattice_distance, std::fabs(sample.lattice_distance_a));
        }
    };
    visit(archive.run041_d_track);
    visit(archive.run041_i_accum);
    visit(archive.run041_l_smooth);
    return stats;
}

struct GeTrackHeadPoint {
    GeResearchParticleVizPoint point;
    float coupling = 0.0f;
};

static bool ge_make_track_head(const std::vector<GeResearchTrajectorySample>& samples,
                               const std::vector<GeResearchTaskSummary>& summaries,
                               const GeTrackStats& stats,
                               uint64_t tick_u64,
                               float frame_phase_01,
                               float temporal_coupling_01,
                               double phase_offset,
                               GeResearchParticleClass particle_class,
                               GeTrackHeadPoint& out_head) {
    if (samples.size() < 2u) return false;

    const float frame_phase = ge_clamp01f(frame_phase_01);
    const float temporal_coupling = ge_clamp01f(temporal_coupling_01);
    const double sample_cursor =
        ((double)tick_u64 + (double)(frame_phase * (0.35f + 0.65f * temporal_coupling))) / 6.0 +
        phase_offset;
    const double sample_floor = std::floor(sample_cursor);
    const size_t base_index = (size_t)((uint64_t)sample_floor % (uint64_t)samples.size());
    const size_t next_index = (base_index + 1u) % samples.size();
    const float t = (float)(sample_cursor - sample_floor);

    const GeResearchTrajectorySample& a = samples[base_index];
    const GeResearchTrajectorySample& b = samples[next_index];
    const GeResearchTaskSummary* task_summary = ge_find_task_summary(summaries, a.task_utf8);
    const float coupling = ge_clamp01f(task_summary ? (float)task_summary->lattice_lock_fraction : 0.55f);

    GeResearchParticleVizPoint point{};
    point.x = ge_lerp_f32((float)a.x_m, (float)b.x_m, t);
    point.y = ge_lerp_f32((float)a.y_m, (float)b.y_m, t);
    point.z = ge_lerp_f32((float)a.z_m, (float)b.z_m, t);
    point.vx = ge_lerp_f32((float)a.vx, (float)b.vx, t);
    point.vy = ge_lerp_f32((float)a.vy, (float)b.vy, t);
    point.vz = ge_lerp_f32((float)a.vz, (float)b.vz, t);

    if (std::fabs(point.vx) < 1.0e-9f &&
        std::fabs(point.vy) < 1.0e-9f &&
        std::fabs(point.vz) < 1.0e-9f) {
        point.vx = (float)(b.x_m - a.x_m);
        point.vy = (float)(b.y_m - a.y_m);
        point.vz = (float)(b.z_m - a.z_m);
    }
    ge_normalize_vec3(point.vx, point.vy, point.vz);

    const float speed_n = ge_clamp01f((float)(std::fabs(ge_lerp_f32((float)a.speed, (float)b.speed, t)) / stats.max_speed));
    const float curvature_n = ge_clamp01f((float)(std::fabs(ge_lerp_f32((float)a.curvature, (float)b.curvature, t)) / stats.max_curvature));
    const float energy_n = ge_clamp01f((float)(std::fabs(ge_lerp_f32((float)a.energy_drift, (float)b.energy_drift, t)) / stats.max_energy_drift));
    const float lattice_n = ge_clamp01f((float)(std::fabs(ge_lerp_f32((float)a.lattice_distance_a, (float)b.lattice_distance_a, t)) / stats.max_lattice_distance));
    const float phase_error = ge_clamp01f(ge_lerp_f32((float)a.phase_error, (float)b.phase_error, t));
    const float coherence = ge_clamp01f(1.0f - phase_error);
    const float signal_delta = ge_lerp_f32((float)(a.noise_feedback - a.sent_signal),
                                           (float)(b.noise_feedback - b.sent_signal),
                                           t);

    point.density = ge_clamp01f(0.26f + 0.30f * coherence + 0.22f * speed_n + 0.22f * coupling);
    point.specularity = ge_clamp01f(0.18f + 0.34f * curvature_n + 0.18f * coherence + 0.30f * coupling);
    point.roughness = ge_clamp01f(0.14f + 0.52f * (1.0f - coherence) + 0.18f * lattice_n);
    point.occlusion = ge_clamp01f(0.08f + 0.50f * energy_n + 0.22f * (1.0f - coupling));
    point.phase_bias = std::max(-1.0f, std::min(1.0f, signal_delta * 32.0f));
    point.amplitude = ge_clamp01f(0.32f + 0.40f * coupling + 0.18f * speed_n + 0.10f * coherence);
    point.temporal_coupling = ge_clamp01f(0.45f * coupling + 0.35f * temporal_coupling + 0.20f * coherence);
    point.curvature_n = curvature_n;
    point.observer_shift = std::max(-1.0f, std::min(1.0f, point.phase_bias * (0.40f + 0.60f * point.temporal_coupling)));
    point.trail_age = 0.0f;
    point.radius_m = 0.075f;
    point.emissive = 1.9f + 0.7f * point.temporal_coupling;
    point.rgba8 = ge_particle_class_rgba8(particle_class);
    point.render_kind_u32 = 3u;
    point.particle_class = particle_class;

    out_head.point = point;
    out_head.coupling = coupling;
    return true;
}

static void ge_push_point_bounded(const GeResearchParticleVizPoint& point,
                                  size_t max_points,
                                  std::vector<GeResearchParticleVizPoint>& out_points) {
    if (out_points.size() >= max_points) return;
    out_points.push_back(point);
}

static void ge_append_realtime_output_viz(const GeResearchConfinementArchive& archive,
                                          uint64_t tick_u64,
                                          size_t max_points,
                                          float frame_phase_01,
                                          float temporal_coupling_01,
                                          std::vector<GeResearchParticleVizPoint>& out_points) {
    if (out_points.size() >= max_points) return;

    const float frame_phase = ge_clamp01f(frame_phase_01);
    const float temporal_coupling = ge_clamp01f(temporal_coupling_01);
    const float tick_phase = (float)((double)(tick_u64 % 360u) / 360.0);

    for (size_t i = 0u; i < archive.vector_excitations.size(); ++i) {
        if (out_points.size() >= max_points) break;
        const GeResearchVectorExcitationSample& sample = archive.vector_excitations[i];
        GeResearchParticleVizPoint point{};
        const float phase = frame_phase + tick_phase + 0.11f * (float)i;
        const float swing = std::sin(phase * 6.2831853f);
        point.x = (float)(sample.x + sample.vec_x * (0.35 + 0.45 * temporal_coupling) + sample.spin_y * 0.18 * swing);
        point.y = (float)(sample.y + sample.vec_y * (0.35 + 0.45 * temporal_coupling) + sample.spin_z * 0.18 * swing);
        point.z = (float)(sample.z + sample.vec_z * (0.35 + 0.45 * temporal_coupling) + sample.spin_x * 0.18 * swing);
        point.vx = (float)sample.vec_x;
        point.vy = (float)sample.vec_y;
        point.vz = (float)sample.vec_z;
        ge_normalize_vec3(point.vx, point.vy, point.vz);
        point.density = ge_clamp01f(0.35f + 0.25f * (float)std::fabs(sample.oam_twist) * 128.0f);
        point.specularity = ge_clamp01f(0.40f + 0.35f * (float)std::fabs(sample.spin_z));
        point.roughness = 0.10f;
        point.occlusion = 0.05f;
        point.phase_bias = ge_clamp01f((float)sample.oam_twist * 200.0f);
        point.phase_bias = point.phase_bias * 2.0f - 1.0f;
        point.amplitude = ge_clamp01f(0.35f + 3.0f * (float)std::sqrt(sample.vec_x * sample.vec_x +
                                                                       sample.vec_y * sample.vec_y +
                                                                       sample.vec_z * sample.vec_z));
        point.temporal_coupling = ge_clamp01f(0.45f + 0.35f * temporal_coupling);
        point.curvature_n = ge_clamp01f((float)std::fabs(sample.oam_twist) * 200.0f);
        point.observer_shift = swing * 0.35f;
        point.trail_age = ge_clamp01f(0.15f + 0.10f * (float)i);
        point.radius_m = 0.06f;
        point.emissive = 2.6f;
        point.rgba8 = ge_pack_rgba8(0.45f + 0.45f * (float)std::fabs(sample.spin_x),
                                    0.25f + 0.65f * (float)std::fabs(sample.spin_y),
                                    0.35f + 0.60f * (float)std::fabs(sample.spin_z),
                                    1.0f);
        point.render_kind_u32 = 4u;
        point.particle_class = GeResearchParticleClass::Flavor;
        ge_push_point_bounded(point, max_points, out_points);
    }

    for (size_t i = 0u; i < archive.tensor_glyphs.size(); ++i) {
        if (out_points.size() >= max_points) break;
        const GeResearchTensorGlyphSample& sample = archive.tensor_glyphs[i];
        GeResearchParticleVizPoint point{};
        const float phase = frame_phase + tick_phase + 0.07f * (float)i;
        const float curl = std::cos(phase * 6.2831853f);
        point.x = (float)(sample.x + sample.tensor_01 * 0.20 + sample.tensor_02 * 0.10 * curl);
        point.y = (float)(sample.y + sample.tensor_10 * 0.20 + sample.tensor_12 * 0.10 * curl);
        point.z = (float)(sample.z + sample.tensor_21 * 0.20 + sample.tensor_20 * 0.10 * curl);
        point.vx = (float)(sample.tensor_02 - sample.tensor_20);
        point.vy = (float)(sample.tensor_10 - sample.tensor_01);
        point.vz = (float)(sample.tensor_21 - sample.tensor_12);
        ge_normalize_vec3(point.vx, point.vy, point.vz);
        const float tensor_energy = ge_clamp01f((float)((std::fabs(sample.tensor_00) +
                                                         std::fabs(sample.tensor_11) +
                                                         std::fabs(sample.tensor_22)) / 3.0));
        point.density = ge_clamp01f(0.28f + 0.45f * tensor_energy);
        point.specularity = ge_clamp01f(0.22f + 0.50f * ge_clamp01f((float)(std::fabs(sample.tensor_01) +
                                                                                std::fabs(sample.tensor_12) +
                                                                                std::fabs(sample.tensor_20))));
        point.roughness = 0.18f;
        point.occlusion = 0.08f;
        point.phase_bias = curl * 0.55f;
        point.amplitude = ge_clamp01f(0.35f + 0.45f * tensor_energy);
        point.temporal_coupling = ge_clamp01f(0.35f + 0.40f * temporal_coupling);
        point.curvature_n = ge_clamp01f((float)(std::fabs(sample.tensor_01) +
                                                std::fabs(sample.tensor_12) +
                                                std::fabs(sample.tensor_20)));
        point.observer_shift = -curl * 0.25f;
        point.trail_age = 0.10f;
        point.radius_m = 0.08f;
        point.emissive = 2.0f;
        point.rgba8 = ge_pack_rgba8((float)sample.color_r, (float)sample.color_g, (float)sample.color_b, 1.0f);
        point.render_kind_u32 = 5u;
        point.particle_class = GeResearchParticleClass::Charged;
        ge_push_point_bounded(point, max_points, out_points);
    }

    for (size_t i = 0u; i < archive.tensor6d_cells.size(); ++i) {
        if (out_points.size() >= max_points) break;
        const GeResearchTensor6DCell& sample = archive.tensor6d_cells[i];
        GeResearchParticleVizPoint point{};
        const float phase = frame_phase + tick_phase + 0.09f * (float)i;
        const float wobble = std::sin(phase * 6.2831853f);
        point.x = (float)sample.x_u32 + (float)sample.freq_x * 0.20f + wobble * 0.05f;
        point.y = (float)sample.y_u32 + (float)sample.freq_y * 0.20f;
        point.z = (float)sample.z_u32 + (float)sample.freq_z * 0.20f - wobble * 0.05f;
        point.vx = (float)sample.spin_x;
        point.vy = (float)sample.spin_y;
        point.vz = (float)sample.spin_z;
        ge_normalize_vec3(point.vx, point.vy, point.vz);
        point.density = ge_clamp01f(0.20f + 0.55f * (float)sample.phase_coherence);
        point.specularity = ge_clamp01f(0.20f + 0.40f * (float)sample.inertia + 0.20f * (float)sample.higgs_inertia);
        point.roughness = 0.14f;
        point.occlusion = ge_clamp01f(0.05f + 0.30f * (float)sample.flux);
        point.phase_bias = ge_clamp01f((float)sample.dtheta_dt * 16.0f) * 2.0f - 1.0f;
        point.amplitude = ge_clamp01f(0.20f + 0.40f * (float)sample.phase_coherence + 0.20f * (float)sample.oam_twist * 128.0f);
        point.temporal_coupling = ge_clamp01f(0.30f + 0.35f * temporal_coupling + 0.20f * (float)sample.phase_coherence);
        point.curvature_n = ge_clamp01f(0.20f + 0.45f * (float)sample.curvature);
        point.observer_shift = wobble * 0.18f;
        point.trail_age = 0.05f;
        point.radius_m = 0.055f;
        point.emissive = 1.8f + 1.4f * (float)sample.higgs_inertia;
        point.rgba8 = ge_pack_rgba8((float)(0.15 + 0.70 * sample.phase_coherence),
                                    (float)(0.20 + 0.60 * sample.flux),
                                    (float)(0.25 + 0.60 * sample.inertia + 0.30 * sample.higgs_inertia),
                                    1.0f);
        point.render_kind_u32 = 5u;
        point.particle_class = GeResearchParticleClass::Charged;
        ge_push_point_bounded(point, max_points, out_points);
    }

    for (size_t i = 0u; i < archive.shader_texture.size(); ++i) {
        if (out_points.size() >= max_points) break;
        const GeResearchShaderTextureSample& sample = archive.shader_texture[i];
        GeResearchParticleVizPoint point{};
        const float phase = frame_phase + tick_phase + 0.05f * (float)i;
        const float veil = std::sin(phase * 6.2831853f) * (0.10f + 0.20f * temporal_coupling);
        point.x = (float)sample.x_u32;
        point.y = (float)sample.y_u32;
        point.z = (float)sample.z_u32 + veil;
        point.vx = 0.0f;
        point.vy = 0.0f;
        point.vz = 1.0f;
        point.density = ge_clamp01f(0.25f + 0.60f * (float)((sample.color_r + sample.color_g + sample.color_b) / 3.0));
        point.specularity = 0.75f;
        point.roughness = 0.05f;
        point.occlusion = 0.02f;
        point.phase_bias = veil;
        point.amplitude = ge_clamp01f(0.30f + 0.55f * point.density);
        point.temporal_coupling = ge_clamp01f(0.30f + 0.40f * temporal_coupling);
        point.curvature_n = ge_clamp01f(0.20f + 0.25f * (float)sample.z_u32);
        point.observer_shift = veil;
        point.trail_age = 0.0f;
        point.radius_m = 0.05f;
        point.emissive = 2.4f;
        point.rgba8 = ge_pack_rgba8((float)sample.color_r, (float)sample.color_g, (float)sample.color_b, 1.0f);
        point.render_kind_u32 = 6u;
        point.particle_class = GeResearchParticleClass::Photon;
        ge_push_point_bounded(point, max_points, out_points);
    }
}

} // namespace

bool ge_find_research_confinement_root(std::filesystem::path& out_root) {
    std::error_code ec;

    const std::string env_root = ge_get_env_utf8("GENESIS_RESEARCH_ROOT");
    if (!env_root.empty()) {
        const std::filesystem::path root = std::filesystem::path(env_root);
        const std::filesystem::path marker = root / "OPEN_ME_FIRST.txt";
        if (std::filesystem::exists(marker, ec) && std::filesystem::is_regular_file(marker, ec)) {
            out_root = root;
            return true;
        }
    }

    std::filesystem::path probe = std::filesystem::current_path(ec);
    if (ec || probe.empty()) return false;

    while (!probe.empty()) {
        const std::filesystem::path root = probe / "ResearchConfinement";
        const std::filesystem::path marker = root / "OPEN_ME_FIRST.txt";
        ec.clear();
        if (std::filesystem::exists(marker, ec) && std::filesystem::is_regular_file(marker, ec)) {
            out_root = root;
            return true;
        }
        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    return false;
}

GeResearchConfinementArchive ge_load_research_confinement_archive() {
    GeResearchConfinementArchive archive{};

    std::filesystem::path root;
    if (!ge_find_research_confinement_root(root)) {
        archive.load_error_utf8 = "research confinement root not found";
        return archive;
    }

    archive.root_dir = root;

    if (!ge_parse_summary_file(root / "Run40" / "run_040_summary.json",
                               archive.run040_tasks,
                               &archive.silicon_lattice_constant_m)) {
        archive.load_error_utf8 = "unable to parse Run40 silicon summary";
        return archive;
    }

    double run041_lattice_constant_m = archive.silicon_lattice_constant_m;
    if (!ge_parse_summary_file(root / "Run41" / "run_041b_summary.json",
                               archive.run041_tasks,
                               &run041_lattice_constant_m)) {
        archive.load_error_utf8 = "unable to parse Run41 silicon summary";
        return archive;
    }
    if (run041_lattice_constant_m > 0.0) {
        archive.silicon_lattice_constant_m = run041_lattice_constant_m;
    }

    if (!ge_parse_nist_summary_file(root / "new_runs_v4" / "run_030" / "nist_surrogate_summary.json",
                                    archive.nist_surrogate) &&
        !ge_build_nist_surrogate_fallback(root, archive.run041_tasks, archive.nist_surrogate)) {
        archive.load_error_utf8 = "unable to derive NIST surrogate summary";
        return archive;
    }

    if (!ge_parse_run043_summary_file(root / "Run43" / "run_043_derivation_summary.json", archive)) {
        archive.load_error_utf8 = "unable to parse Run43 operator fit summary";
        return archive;
    }

    if (!ge_parse_temporal_coupling_schema_file(root / "temporal_coupling_encoding_schema_2060.json", archive)) {
        archive.load_error_utf8 = "unable to parse temporal coupling encoding schema";
        return archive;
    }

    if (!ge_parse_history_csv(root / "Run41" / "D_track_best_history_sampled.csv", archive.run041_d_track) ||
        !ge_parse_history_csv(root / "Run41" / "I_accum_best_history_sampled.csv", archive.run041_i_accum) ||
        !ge_parse_history_csv(root / "Run41" / "L_smooth_best_history_sampled.csv", archive.run041_l_smooth)) {
        archive.load_error_utf8 = "unable to parse Run41 sampled trajectory histories";
        return archive;
    }

    if (!ge_parse_packet_path_classification_file(root / "photon_packet_path_classification_sample.json",
                                                  archive.packet_path_classes) ||
        !ge_parse_tensor6d_file(root / "photon_lattice_tensor6d_sample.json",
                                archive.tensor6d_cells) ||
        !ge_parse_vector_excitation_file(root / "photon_vector_excitation_sample.json",
                                         archive.vector_excitations) ||
        !ge_parse_tensor_glyph_file(root / "photon_tensor_gradient_glyph_sample.json",
                                    archive.tensor_glyphs) ||
        !ge_parse_shader_texture_file(root / "photon_shader_texture_sample.json",
                                      archive.shader_texture) ||
        !ge_parse_audio_waveform_file(root / "photon_audio_waveform_sample.json",
                                      archive.audio_waveform)) {
        archive.load_error_utf8 = "unable to parse photon-native realtime output samples";
        return archive;
    }

    (void)ge_append_photon_volume_vsd(root / "photon_volume_expansion.gevsd", archive);

    archive.loaded = true;
    archive.load_error_utf8.clear();
    return archive;
}

GeNistSiliconReference ge_load_nist_silicon_reference() {
    GeNistSiliconReference reference{};

    std::filesystem::path root;
    if (!ge_find_research_confinement_root(root)) {
        reference.load_error_utf8 = "research confinement root not found";
        return reference;
    }

    const std::filesystem::path path = root / "nist_silicon_reference.json";
    if (!ge_parse_nist_silicon_reference_file(path, reference)) {
        reference.load_error_utf8 = "unable to parse NIST silicon reference";
        reference.source_path = path;
        return reference;
    }

    reference.loaded = true;
    reference.load_error_utf8.clear();
    return reference;
}

bool ge_validate_research_confinement_archive(const GeResearchConfinementArchive& archive,
                                              std::string& out_message_utf8) {
    if (!archive.loaded) {
        out_message_utf8 = archive.load_error_utf8.empty() ? "research confinement archive is not loaded"
                                                           : archive.load_error_utf8;
        return false;
    }
    if (archive.silicon_lattice_constant_m <= 0.0) {
        out_message_utf8 = "silicon lattice constant missing from archive";
        return false;
    }
    if (archive.nist_surrogate.verdict_utf8 != "not_within_nist_uncertainty_scale") {
        out_message_utf8 = "unexpected NIST surrogate verdict";
        return false;
    }

    const GeResearchTaskSummary* run040_d = ge_find_task_summary(archive.run040_tasks, "D_track");
    const GeResearchTaskSummary* run041_d = ge_find_task_summary(archive.run041_tasks, "D_track");
    if (!run040_d || !run041_d) {
        out_message_utf8 = "missing D_track summary in research archive";
        return false;
    }
    if (run041_d->composite_score <= run040_d->composite_score) {
        out_message_utf8 = "Run41 D_track composite score did not improve over Run40";
        return false;
    }
    if (run041_d->return_distance_a >= run040_d->return_distance_a) {
        out_message_utf8 = "Run41 D_track return distance did not tighten over Run40";
        return false;
    }

    if (archive.run041_d_track.empty() || archive.run041_i_accum.empty() || archive.run041_l_smooth.empty()) {
        out_message_utf8 = "missing sampled Run41 trajectories";
        return false;
    }
    if (archive.packet_path_classes.empty() || archive.tensor6d_cells.empty() ||
        archive.vector_excitations.empty() || archive.tensor_glyphs.empty() ||
        archive.shader_texture.empty() || archive.audio_waveform.empty()) {
        out_message_utf8 = "missing photon-native realtime output samples";
        return false;
    }
    if (!ge_has_research_runtime_guidance(archive)) {
        out_message_utf8 = "missing Run43 temporal guidance operators";
        return false;
    }
    if (!(archive.pulse_window_min_quartet.F < archive.pulse_window_max_quartet.F) ||
        !(archive.pulse_window_min_quartet.A < archive.pulse_window_max_quartet.A) ||
        !(archive.pulse_window_min_quartet.I < archive.pulse_window_max_quartet.I) ||
        !(archive.pulse_window_min_quartet.V < archive.pulse_window_max_quartet.V)) {
        out_message_utf8 = "invalid temporal coupling pulse window";
        return false;
    }
    if (archive.run043_validation_error.silicon_score_rmse <= 0.0 ||
        archive.run043_validation_error.silicon_score_mae <= 0.0) {
        out_message_utf8 = "Run43 validation error was not loaded";
        return false;
    }

    out_message_utf8 = "research confinement archive loaded and validated";
    return true;
}

void ge_build_research_particle_viz(const GeResearchConfinementArchive& archive,
                                    uint64_t tick_u64,
                                    size_t max_points,
                                    float frame_phase_01,
                                    float temporal_coupling_01,
                                    std::vector<GeResearchParticleVizPoint>& out_points) {
    out_points.clear();
    if (!archive.loaded || max_points == 0u) return;

    const GeTrackStats stats = ge_collect_track_stats(archive);
    std::vector<GeTrackHeadPoint> heads;
    heads.reserve(3u);

    GeTrackHeadPoint head{};
    if (ge_make_track_head(archive.run041_d_track, archive.run041_tasks, stats, tick_u64, frame_phase_01, temporal_coupling_01, 0.0, GeResearchParticleClass::Weighted, head)) {
        heads.push_back(head);
    }
    if (ge_make_track_head(archive.run041_i_accum, archive.run041_tasks, stats, tick_u64, frame_phase_01, temporal_coupling_01, 1.7, GeResearchParticleClass::Flavor, head)) {
        heads.push_back(head);
    }
    if (ge_make_track_head(archive.run041_l_smooth, archive.run041_tasks, stats, tick_u64, frame_phase_01, temporal_coupling_01, 3.4, GeResearchParticleClass::Charged, head)) {
        heads.push_back(head);
    }

    if (heads.empty()) return;

    for (const GeTrackHeadPoint& h : heads) {
        ge_push_point_bounded(h.point, max_points, out_points);
    }

    const std::vector<const std::vector<GeResearchTrajectorySample>*> tracks = {
        &archive.run041_d_track, &archive.run041_i_accum, &archive.run041_l_smooth
    };
    for (size_t track_index = 0; track_index < tracks.size() && track_index < heads.size(); ++track_index) {
        const auto& samples = *tracks[track_index];
        if (samples.size() < 2u) continue;

        const double cursor =
            ((double)tick_u64 + (double)(ge_clamp01f(frame_phase_01) * (0.35f + 0.65f * ge_clamp01f(temporal_coupling_01)))) / 6.0 +
            (double)track_index * 1.7;
        const double cursor_floor = std::floor(cursor);
        const size_t base_index = (size_t)((uint64_t)cursor_floor % (uint64_t)samples.size());
        for (size_t trail_index = 1u; trail_index <= 3u; ++trail_index) {
            const size_t sample_index = (base_index + samples.size() - std::min(samples.size() - 1u, trail_index * 2u)) % samples.size();
            const size_t next_index = (sample_index + 1u) % samples.size();
            const GeResearchTrajectorySample& a = samples[sample_index];
            const GeResearchTrajectorySample& b = samples[next_index];
            const float trail_t = 0.35f;
            const float decay = std::pow(0.72f, (float)trail_index);

            GeResearchParticleVizPoint trail = heads[track_index].point;
            trail.x = ge_lerp_f32((float)a.x_m, (float)b.x_m, trail_t);
            trail.y = ge_lerp_f32((float)a.y_m, (float)b.y_m, trail_t);
            trail.z = ge_lerp_f32((float)a.z_m, (float)b.z_m, trail_t);
            trail.vx = ge_lerp_f32((float)a.vx, (float)b.vx, trail_t);
            trail.vy = ge_lerp_f32((float)a.vy, (float)b.vy, trail_t);
            trail.vz = ge_lerp_f32((float)a.vz, (float)b.vz, trail_t);
            ge_normalize_vec3(trail.vx, trail.vy, trail.vz);
            trail.density = ge_clamp01f(trail.density * decay);
            trail.specularity = ge_clamp01f(trail.specularity * (0.92f - 0.07f * (float)trail_index));
            trail.roughness = ge_clamp01f(trail.roughness + 0.06f * (float)trail_index);
            trail.occlusion = ge_clamp01f(trail.occlusion + 0.05f * (float)trail_index);
            trail.phase_bias *= (0.85f - 0.10f * (float)(trail_index - 1u));
            trail.amplitude = ge_clamp01f(trail.amplitude * decay);
            trail.temporal_coupling = ge_clamp01f(trail.temporal_coupling * (0.92f - 0.12f * (float)(trail_index - 1u)));
            trail.curvature_n = ge_clamp01f(trail.curvature_n + 0.08f * (float)trail_index);
            trail.observer_shift *= (0.95f + 0.10f * (float)trail_index);
            trail.trail_age = ge_clamp01f(0.25f * (float)trail_index);
            ge_push_point_bounded(trail, max_points, out_points);
        }
    }

    float centroid_x = 0.0f;
    float centroid_y = 0.0f;
    float centroid_z = 0.0f;
    float centroid_vx = 0.0f;
    float centroid_vy = 0.0f;
    float centroid_vz = 0.0f;
    float centroid_density = 0.0f;
    float centroid_specularity = 0.0f;
    float centroid_coupling = 0.0f;
    for (const GeTrackHeadPoint& h : heads) {
        centroid_x += h.point.x;
        centroid_y += h.point.y;
        centroid_z += h.point.z;
        centroid_vx += h.point.vx;
        centroid_vy += h.point.vy;
        centroid_vz += h.point.vz;
        centroid_density += h.point.density;
        centroid_specularity += h.point.specularity;
        centroid_coupling += h.coupling;
    }
    const float inv_count = 1.0f / (float)heads.size();
    centroid_x *= inv_count;
    centroid_y *= inv_count;
    centroid_z *= inv_count;
    centroid_vx *= inv_count;
    centroid_vy *= inv_count;
    centroid_vz *= inv_count;
    ge_normalize_vec3(centroid_vx, centroid_vy, centroid_vz);

    GeResearchParticleVizPoint photon{};
    photon.x = centroid_x;
    photon.y = centroid_y;
    photon.z = centroid_z;
    photon.vx = centroid_vx;
    photon.vy = centroid_vy;
    photon.vz = centroid_vz;
    photon.density = ge_clamp01f(0.35f + 0.55f * centroid_density * inv_count);
    photon.specularity = ge_clamp01f(0.30f + 0.60f * centroid_specularity * inv_count);
    photon.roughness = 0.08f;
    photon.occlusion = 0.08f;
    photon.phase_bias = 0.0f;
    photon.amplitude = ge_clamp01f(0.55f + 0.35f * centroid_coupling * inv_count);
    photon.temporal_coupling = ge_clamp01f(0.58f + 0.30f * ge_clamp01f(temporal_coupling_01) + 0.12f * centroid_coupling * inv_count);
    photon.curvature_n = 0.12f;
    photon.observer_shift = 0.0f;
    photon.trail_age = 0.0f;
    photon.radius_m = 0.10f;
    photon.emissive = 3.1f;
    photon.rgba8 = ge_particle_class_rgba8(GeResearchParticleClass::Photon);
    photon.render_kind_u32 = 3u;
    photon.particle_class = GeResearchParticleClass::Photon;
    ge_push_point_bounded(photon, max_points, out_points);

    for (const GeTrackHeadPoint& h : heads) {
        GeResearchParticleVizPoint ghost = h.point;
        const float mix_t = 0.28f + 0.20f * h.coupling;
        ghost.x = ge_lerp_f32(h.point.x, photon.x, mix_t);
        ghost.y = ge_lerp_f32(h.point.y, photon.y, mix_t);
        ghost.z = ge_lerp_f32(h.point.z, photon.z, mix_t);
        ghost.vx = ge_lerp_f32(h.point.vx, photon.vx, mix_t);
        ghost.vy = ge_lerp_f32(h.point.vy, photon.vy, mix_t);
        ghost.vz = ge_lerp_f32(h.point.vz, photon.vz, mix_t);
        ge_normalize_vec3(ghost.vx, ghost.vy, ghost.vz);
        ghost.density = ge_clamp01f(ghost.density * 0.58f);
        ghost.specularity = ge_clamp01f(ghost.specularity * 0.82f);
        ghost.roughness = ge_clamp01f(ghost.roughness + 0.10f);
        ghost.occlusion = ge_clamp01f(ghost.occlusion + 0.12f);
        ghost.phase_bias *= 0.65f;
        ghost.amplitude = ge_clamp01f(ghost.amplitude * 0.52f);
        ghost.temporal_coupling = ge_clamp01f(ghost.temporal_coupling * 0.72f);
        ghost.curvature_n = ge_clamp01f(ghost.curvature_n + 0.10f);
        ghost.observer_shift *= 0.85f;
        ghost.trail_age = 0.75f;
        ghost.radius_m = 0.05f;
        ghost.emissive = 1.4f;
        ghost.rgba8 = ge_particle_class_rgba8(ghost.particle_class);
        ghost.render_kind_u32 = 3u;
        ge_push_point_bounded(ghost, max_points, out_points);
    }

    ge_append_realtime_output_viz(archive, tick_u64, max_points, frame_phase_01, temporal_coupling_01, out_points);
}

bool ge_has_research_realtime_outputs(const GeResearchConfinementArchive& archive) {
    return archive.loaded &&
           !archive.vector_excitations.empty() &&
           !archive.tensor_glyphs.empty() &&
           !archive.shader_texture.empty() &&
           !archive.audio_waveform.empty();
}

void ge_build_research_audio_frame(const GeResearchConfinementArchive& archive,
                                   uint64_t tick_u64,
                                   size_t frame_count,
                                   GeResearchAudioFrame& out_frame) {
    out_frame = GeResearchAudioFrame{};
    if (!archive.loaded || archive.audio_waveform.empty() || frame_count == 0u) return;

    out_frame.channel_count_u32 = 4u;
    out_frame.sample_rate_hz_u32 = 48000u;
    out_frame.interleaved_pcm16.reserve(frame_count * (size_t)out_frame.channel_count_u32);

    const size_t base_index = (size_t)(tick_u64 % (uint64_t)archive.audio_waveform.size());
    float accum_abs = 0.0f;
    float peak_abs = 0.0f;
    for (size_t frame_index = 0u; frame_index < frame_count; ++frame_index) {
        const GeResearchAudioWaveSample& a =
            archive.audio_waveform[(base_index + frame_index) % archive.audio_waveform.size()];
        const GeResearchAudioWaveSample& b =
            archive.audio_waveform[(base_index + frame_index + 1u) % archive.audio_waveform.size()];
        const float t = (float)frame_index / (float)std::max<size_t>(frame_count - 1u, 1u);
        const float c0 = ge_lerp_f32(a.channel_0, b.channel_0, t);
        const float c1 = ge_lerp_f32(a.channel_1, b.channel_1, t);
        const float c2 = ge_lerp_f32(a.channel_2, b.channel_2, t);
        const float c3 = ge_lerp_f32(a.channel_3, b.channel_3, t);
        const float channels[4] = {c0, c1, c2, c3};
        for (float sample : channels) {
            const float clamped = std::max(-1.0f, std::min(1.0f, sample));
            const float abs_sample = std::fabs(clamped);
            accum_abs += abs_sample;
            peak_abs = std::max(peak_abs, abs_sample);
            out_frame.interleaved_pcm16.push_back((int16_t)std::lround((double)(clamped * 32767.0f)));
        }
    }
    if (!out_frame.interleaved_pcm16.empty()) {
        out_frame.mean_abs_amplitude = accum_abs / (float)out_frame.interleaved_pcm16.size();
        out_frame.peak_abs_amplitude = peak_abs;
    }
}

bool ge_has_research_runtime_guidance(const GeResearchConfinementArchive& archive) {
    return archive.loaded &&
           archive.run043_loaded &&
           archive.run043_validation_error.silicon_score_rmse > 0.0 &&
           archive.temporal_collapse_gates.gate_coherence > 0.0 &&
           archive.temporal_collapse_gates.gate_trap > 0.0 &&
           archive.temporal_collapse_gates.gate_score > 0.0 &&
           archive.pulse_window_min_quartet.F < archive.pulse_window_max_quartet.F &&
           archive.pulse_window_min_quartet.A < archive.pulse_window_max_quartet.A &&
           archive.pulse_window_min_quartet.I < archive.pulse_window_max_quartet.I &&
           archive.pulse_window_min_quartet.V < archive.pulse_window_max_quartet.V;
}

double ge_compute_research_tensor_gradient_norm(const GeResearchConfinementArchive& archive) {
    if (archive.tensor6d_cells.empty()) return 0.0;

    double accum = 0.0;
    for (const GeResearchTensor6DCell& cell : archive.tensor6d_cells) {
        const double freq_sum =
            ge_absd_local(cell.freq_x) + ge_absd_local(cell.freq_y) + ge_absd_local(cell.freq_z);
        const double spin_sum =
            ge_absd_local(cell.spin_x) + ge_absd_local(cell.spin_y) + ge_absd_local(cell.spin_z);
        const double dynamics_sum =
            ge_absd_local(cell.dtheta_dt) + 0.5 * ge_absd_local(cell.d2theta_dt2);
        const double gradient_n =
            0.24 * ge_clamp01d_local(cell.phase_coherence) +
            0.18 * ge_norm_from_abs_local(cell.curvature) +
            0.16 * ge_norm_from_abs_local(cell.flux) +
            0.14 * ge_norm_from_abs_local(cell.inertia + cell.higgs_inertia) +
            0.14 * ge_norm_from_abs_local(freq_sum) +
            0.14 * ge_norm_from_abs_local(dynamics_sum + spin_sum + ge_absd_local(cell.oam_twist));
        accum += ge_clamp01d_local(gradient_n);
    }

    return ge_clamp01d_local(accum / (double)archive.tensor6d_cells.size());
}

double ge_compute_research_packet_coherence_norm(const GeResearchConfinementArchive& archive) {
    if (archive.packet_path_classes.empty()) return 0.0;

    double accum = 0.0;
    for (const GeResearchPacketPathClassification& sample : archive.packet_path_classes) {
        const double packet_n =
            0.42 * ge_clamp01d_local(sample.coherence_score) +
            0.33 * ge_clamp01d_local(sample.phase_lock_score) +
            0.15 * (sample.shared_path ? 1.0 : 0.0) +
            0.10 * ge_norm_from_abs_local(sample.curvature_depth);
        accum += ge_clamp01d_local(packet_n);
    }

    return ge_clamp01d_local(accum / (double)archive.packet_path_classes.size());
}

double ge_compute_research_observer_coupling_norm(const GeResearchConfinementArchive& archive) {
    const auto accumulate_track = [](const std::vector<GeResearchTrajectorySample>& samples,
                                     double& accum,
                                     size_t& count) {
        for (const GeResearchTrajectorySample& sample : samples) {
            const double feedback_n =
                ge_norm_from_abs_local(sample.noise_feedback - sample.sent_signal);
            const double phase_n = ge_clamp01d_local(ge_absd_local(sample.phase_error));
            const double lattice_n = ge_norm_from_abs_local(sample.lattice_distance_a);
            const double energy_n = ge_norm_from_abs_local(sample.energy_drift);
            const double observer_n =
                0.35 * feedback_n +
                0.30 * (1.0 - phase_n) +
                0.20 * (1.0 - lattice_n) +
                0.15 * (1.0 - energy_n);
            accum += ge_clamp01d_local(observer_n);
            ++count;
        }
    };

    double accum = 0.0;
    size_t count = 0u;
    accumulate_track(archive.run041_d_track, accum, count);
    accumulate_track(archive.run041_i_accum, accum, count);
    accumulate_track(archive.run041_l_smooth, accum, count);
    if (count == 0u) return 0.0;
    return ge_clamp01d_local(accum / (double)count);
}

double ge_compute_research_recurrence_norm(const GeResearchConfinementArchive& archive) {
    if (archive.run041_tasks.empty()) return 0.0;

    double accum = 0.0;
    size_t count = 0u;
    for (const GeResearchTaskSummary& task : archive.run041_tasks) {
        const GeResearchTaskSummary* baseline = ge_find_task_summary(archive.run040_tasks, task.task_utf8);
        const double composite_gain_n =
            baseline
                ? ge_clamp01d_local((task.composite_score - baseline->composite_score) /
                                    std::max(ge_absd_local(baseline->composite_score), 0.05))
                : ge_clamp01d_local(task.composite_score);
        const double task_n =
            0.25 * ge_clamp01d_local(task.recurrence_alignment) +
            0.20 * ge_clamp01d_local(task.conservation_alignment) +
            0.20 * ge_clamp01d_local(task.phase_lock_fraction) +
            0.15 * ge_clamp01d_local(task.joint_lock_fraction) +
            0.10 * ge_clamp01d_local(task.lattice_lock_fraction) +
            0.10 * composite_gain_n;
        accum += ge_clamp01d_local(task_n);
        ++count;
    }

    return (count > 0u) ? ge_clamp01d_local(accum / (double)count) : 0.0;
}

double ge_evaluate_research_metric_model(const GeResearchQuadraticMetricModel& model,
                                         const GeResearchPulseQuartet& center_quartet,
                                         const GeResearchPulseQuartet& quartet) {
    const double dF = quartet.F - center_quartet.F;
    const double dA = quartet.A - center_quartet.A;
    const double dI = quartet.I - center_quartet.I;
    const double dV = quartet.V - center_quartet.V;

    return model.center_value +
           model.jacobian.F * dF +
           model.jacobian.A * dA +
           model.jacobian.I * dI +
           model.jacobian.V * dV +
           0.5 * model.hessian_diag.F * dF * dF +
           0.5 * model.hessian_diag.A * dA * dA +
           0.5 * model.hessian_diag.I * dI * dI +
           0.5 * model.hessian_diag.V * dV * dV +
           model.hessian_FA * dF * dA +
           model.hessian_FI * dF * dI +
           model.hessian_FV * dF * dV +
           model.hessian_AI * dA * dI +
           model.hessian_AV * dA * dV +
           model.hessian_IV * dI * dV;
}

bool ge_compute_research_metric_gradient(const GeResearchQuadraticMetricModel& model,
                                         const GeResearchPulseQuartet& center_quartet,
                                         const GeResearchPulseQuartet& quartet,
                                         GeResearchPulseQuartet& out_gradient) {
    const double dF = quartet.F - center_quartet.F;
    const double dA = quartet.A - center_quartet.A;
    const double dI = quartet.I - center_quartet.I;
    const double dV = quartet.V - center_quartet.V;

    out_gradient = GeResearchPulseQuartet{};
    out_gradient.F = model.jacobian.F +
                     model.hessian_diag.F * dF +
                     model.hessian_FA * dA +
                     model.hessian_FI * dI +
                     model.hessian_FV * dV;
    out_gradient.A = model.jacobian.A +
                     model.hessian_diag.A * dA +
                     model.hessian_FA * dF +
                     model.hessian_AI * dI +
                     model.hessian_AV * dV;
    out_gradient.I = model.jacobian.I +
                     model.hessian_diag.I * dI +
                     model.hessian_FI * dF +
                     model.hessian_AI * dA +
                     model.hessian_IV * dV;
    out_gradient.V = model.jacobian.V +
                     model.hessian_diag.V * dV +
                     model.hessian_FV * dF +
                     model.hessian_AV * dA +
                     model.hessian_IV * dI;
    return true;
}

bool ge_build_research_runtime_guidance(const GeResearchConfinementArchive& archive,
                                        const GeResearchPulseQuartet& desired_quartet,
                                        double residual_norm_01,
                                        double interference_norm_01,
                                        double substrate_coherence_norm_01,
                                        double source_vibration_norm_01,
                                        GeResearchRuntimeGuidance& out_guidance) {
    out_guidance = GeResearchRuntimeGuidance{};
    if (!ge_has_research_runtime_guidance(archive)) return false;

    const GeResearchPulseQuartet center = archive.run043_center_quartet;
    const GeResearchPulseQuartet delta = archive.run043_delta_quartet;

    GeResearchPulseQuartet clamped_desired = desired_quartet;
    clamped_desired.F = ge_clampd_local(clamped_desired.F,
                                        archive.pulse_window_min_quartet.F,
                                        archive.pulse_window_max_quartet.F);
    clamped_desired.A = ge_clampd_local(clamped_desired.A,
                                        archive.pulse_window_min_quartet.A,
                                        archive.pulse_window_max_quartet.A);
    clamped_desired.I = ge_clampd_local(clamped_desired.I,
                                        archive.pulse_window_min_quartet.I,
                                        archive.pulse_window_max_quartet.I);
    clamped_desired.V = ge_clampd_local(clamped_desired.V,
                                        archive.pulse_window_min_quartet.V,
                                        archive.pulse_window_max_quartet.V);

    const double tensor_norm = ge_compute_research_tensor_gradient_norm(archive);
    const double packet_norm = ge_compute_research_packet_coherence_norm(archive);
    const double observer_archive_norm = ge_compute_research_observer_coupling_norm(archive);
    const double recurrence_norm = ge_compute_research_recurrence_norm(archive);
    const double residual_n = ge_clamp01d_local(residual_norm_01);
    const double interference_n = ge_clamp01d_local(interference_norm_01);
    const double substrate_coherence_n = ge_clamp01d_local(substrate_coherence_norm_01);
    const double source_vibration_n = ge_clamp01d_local(source_vibration_norm_01);

    const double observer_input_n =
        ge_clamp01d_local(0.35 * residual_n +
                          0.35 * interference_n +
                          0.15 * (1.0 - substrate_coherence_n) +
                          0.15 * source_vibration_n);
    const double source_memory_n =
        ge_clamp01d_local(0.60 * recurrence_norm + 0.40 * source_vibration_n);
    const double temporal_coupling_n =
        ge_clamp01d_local(0.20 * tensor_norm +
                          0.20 * packet_norm +
                          0.18 * observer_archive_norm +
                          0.14 * recurrence_norm +
                          0.14 * substrate_coherence_n +
                          0.14 * source_memory_n);

    const double memory_blend =
        ge_clamp01d_local(0.08 + 0.32 * source_memory_n + 0.18 * observer_input_n);

    GeResearchPulseQuartet base_quartet{};
    base_quartet.F = ge_lerpd_local(clamped_desired.F, center.F, memory_blend);
    base_quartet.A = ge_lerpd_local(clamped_desired.A, center.A, memory_blend);
    base_quartet.I = ge_lerpd_local(clamped_desired.I, center.I, memory_blend);
    base_quartet.V = ge_lerpd_local(clamped_desired.V, center.V, memory_blend);

    const double base_score =
        ge_evaluate_research_metric_model(archive.run043_silicon_score_model, center, base_quartet);
    const double base_trap =
        ge_evaluate_research_metric_model(archive.run043_trap_ratio_model, center, base_quartet);
    const double base_coherence =
        ge_evaluate_research_metric_model(archive.run043_coherence_model, center, base_quartet);

    const double score_gap =
        std::max(0.0, archive.temporal_collapse_gates.gate_score - base_score);
    const double coherence_gap =
        std::max(0.0, archive.temporal_collapse_gates.gate_coherence - base_coherence);
    const double trap_gap =
        std::max(0.0, archive.temporal_collapse_gates.gate_trap - base_trap);

    GeResearchPulseQuartet score_grad{};
    GeResearchPulseQuartet trap_grad{};
    GeResearchPulseQuartet coherence_grad{};
    (void)ge_compute_research_metric_gradient(archive.run043_silicon_score_model, center, base_quartet, score_grad);
    (void)ge_compute_research_metric_gradient(archive.run043_trap_ratio_model, center, base_quartet, trap_grad);
    (void)ge_compute_research_metric_gradient(archive.run043_coherence_model, center, base_quartet, coherence_grad);

    const double score_weight = 0.65 + 1.50 * score_gap;
    const double coherence_weight = 0.45 + 1.50 * coherence_gap;
    const double trap_weight = 0.35 + 1.25 * trap_gap;

    GeResearchPulseQuartet utility_grad{};
    utility_grad.F = score_weight * score_grad.F +
                     coherence_weight * coherence_grad.F +
                     trap_weight * trap_grad.F;
    utility_grad.A = score_weight * score_grad.A +
                     coherence_weight * coherence_grad.A +
                     trap_weight * trap_grad.A;
    utility_grad.I = score_weight * score_grad.I +
                     coherence_weight * coherence_grad.I +
                     trap_weight * trap_grad.I;
    utility_grad.V = score_weight * score_grad.V +
                     coherence_weight * coherence_grad.V +
                     trap_weight * trap_grad.V;

    const double correction_authority_n =
        ge_clamp01d_local(0.12 +
                          0.44 * observer_input_n +
                          0.22 * ge_clamp01d_local((score_gap + coherence_gap + trap_gap) * 2.0) +
                          0.22 * (1.0 - temporal_coupling_n));

    GeResearchPulseQuartet corrected = base_quartet;
    corrected.F += delta.F * correction_authority_n * ge_softsign_local(utility_grad.F * 24.0);
    corrected.A += delta.A * correction_authority_n * ge_softsign_local(utility_grad.A * 18.0);
    corrected.I += delta.I * correction_authority_n * ge_softsign_local(utility_grad.I * 16.0);
    corrected.V += delta.V * correction_authority_n * ge_softsign_local(utility_grad.V * 16.0);

    corrected.F = ge_clampd_local(corrected.F,
                                  archive.pulse_window_min_quartet.F,
                                  archive.pulse_window_max_quartet.F);
    corrected.A = ge_clampd_local(corrected.A,
                                  archive.pulse_window_min_quartet.A,
                                  archive.pulse_window_max_quartet.A);
    corrected.I = ge_clampd_local(corrected.I,
                                  archive.pulse_window_min_quartet.I,
                                  archive.pulse_window_max_quartet.I);
    corrected.V = ge_clampd_local(corrected.V,
                                  archive.pulse_window_min_quartet.V,
                                  archive.pulse_window_max_quartet.V);

    const double predicted_score =
        ge_evaluate_research_metric_model(archive.run043_silicon_score_model, center, corrected);
    const double predicted_trap =
        ge_evaluate_research_metric_model(archive.run043_trap_ratio_model, center, corrected);
    const double predicted_coherence =
        ge_evaluate_research_metric_model(archive.run043_coherence_model, center, corrected);
    const double predicted_inertia =
        ge_evaluate_research_metric_model(archive.run043_inertia_model, center, corrected);
    const double predicted_curvature =
        ge_evaluate_research_metric_model(archive.run043_curvature_model, center, corrected);

    const double score_alignment =
        ge_clamp01d_local(predicted_score / std::max(archive.temporal_collapse_gates.gate_score, 1.0e-9));
    const double trap_alignment =
        ge_clamp01d_local(predicted_trap / std::max(archive.temporal_collapse_gates.gate_trap, 1.0e-9));
    const double coherence_alignment =
        ge_clamp01d_local(predicted_coherence / std::max(archive.temporal_collapse_gates.gate_coherence, 1.0e-9));
    const double collapse_alignment =
        ge_clamp01d_local((score_alignment + trap_alignment + coherence_alignment) / 3.0);

    const double rmse_penalty =
        ge_clamp01d_local(archive.run043_validation_error.silicon_score_rmse * 5000.0);
    const double exactness_n =
        ge_clamp01d_local(1.0 -
                          ge_clamp01d_local(0.55 * rmse_penalty +
                                            0.25 * observer_input_n +
                                            0.20 * (1.0 - collapse_alignment)));
    const double certainty_n =
        ge_clamp01d_local(0.34 * exactness_n +
                          0.24 * collapse_alignment +
                          0.22 * temporal_coupling_n +
                          0.20 * (1.0 - residual_n));

    const uint32_t cadence_ticks_u32 =
        (uint32_t)std::max<int32_t>(1,
                                    std::min<int32_t>(
                                        24,
                                        (int32_t)std::lround(1.0 +
                                                             (1.0 - correction_authority_n) * 12.0 +
                                                             certainty_n * 6.0 +
                                                             temporal_coupling_n * 6.0)));

    out_guidance.valid = true;
    out_guidance.correction_cadence_ticks_u32 = cadence_ticks_u32;
    out_guidance.certainty_norm = certainty_n;
    out_guidance.exactness_norm = exactness_n;
    out_guidance.recurrence_norm = recurrence_norm;
    out_guidance.tensor_gradient_norm = tensor_norm;
    out_guidance.packet_coherence_norm = packet_norm;
    out_guidance.observer_coupling_norm =
        ge_clamp01d_local(0.65 * observer_archive_norm + 0.35 * observer_input_n);
    out_guidance.temporal_coupling_norm = temporal_coupling_n;
    out_guidance.source_memory_norm = source_memory_n;
    out_guidance.correction_authority_norm = correction_authority_n;
    out_guidance.predicted_silicon_score = predicted_score;
    out_guidance.predicted_trap_ratio = predicted_trap;
    out_guidance.predicted_coherence = predicted_coherence;
    out_guidance.predicted_inertia = predicted_inertia;
    out_guidance.predicted_curvature = predicted_curvature;
    out_guidance.desired_quartet = clamped_desired;
    out_guidance.corrected_quartet = corrected;
    out_guidance.gradient_quartet = utility_grad;
    return true;
}

struct GeResearchPredictionLatticeSample {
    double interference_norm = 0.0;
    double temporal_coupling_norm = 0.0;
    double coherence_norm = 0.0;
    double curvature_penalty_norm = 0.0;
    double inertia_penalty_norm = 0.0;
    uint32_t lattice_x_u32 = 0u;
    uint32_t lattice_y_u32 = 0u;
    uint32_t lattice_z_u32 = 0u;
};

static GeResearchPredictionLatticeSample ge_sample_research_prediction_lattice(
    const GeResearchConfinementArchive& archive,
    const GeResearchPulseQuartet& qmin,
    const GeResearchPulseQuartet& qmax,
    const GeResearchPulseQuartet& gpu_target,
    const GeResearchPulseQuartet& quartet,
    double substrate_coherence_n,
    double source_vibration_n) {
    GeResearchPredictionLatticeSample out{};
    if (archive.tensor6d_cells.empty()) {
        out.interference_norm = ge_clamp01d_local(0.45 * substrate_coherence_n + 0.55 * source_vibration_n);
        out.temporal_coupling_norm = ge_clamp01d_local(0.50 * out.interference_norm + 0.50 * source_vibration_n);
        out.coherence_norm = ge_clamp01d_local(substrate_coherence_n);
        return out;
    }

    uint32_t max_x_u32 = 0u;
    uint32_t max_y_u32 = 0u;
    uint32_t max_z_u32 = 0u;
    for (const GeResearchTensor6DCell& cell : archive.tensor6d_cells) {
        if (cell.x_u32 > max_x_u32) max_x_u32 = cell.x_u32;
        if (cell.y_u32 > max_y_u32) max_y_u32 = cell.y_u32;
        if (cell.z_u32 > max_z_u32) max_z_u32 = cell.z_u32;
    }

    const double spanF = std::max(qmax.F - qmin.F, 1.0e-9);
    const double spanA = std::max(qmax.A - qmin.A, 1.0e-9);
    const double spanI = std::max(qmax.I - qmin.I, 1.0e-9);
    const double spanV = std::max(qmax.V - qmin.V, 1.0e-9);

    const double qF_n = ge_clamp01d_local((quartet.F - qmin.F) / spanF);
    const double qA_n = ge_clamp01d_local((quartet.A - qmin.A) / spanA);
    const double qI_n = ge_clamp01d_local((quartet.I - qmin.I) / spanI);
    const double qV_n = ge_clamp01d_local((quartet.V - qmin.V) / spanV);
    const double gF_n = ge_clamp01d_local((gpu_target.F - qmin.F) / spanF);
    const double gA_n = ge_clamp01d_local((gpu_target.A - qmin.A) / spanA);
    const double gI_n = ge_clamp01d_local((gpu_target.I - qmin.I) / spanI);
    const double gV_n = ge_clamp01d_local((gpu_target.V - qmin.V) / spanV);

    const double tx_n = ge_clamp01d_local(0.52 * qF_n + 0.24 * qA_n + 0.14 * gF_n + 0.10 * gA_n);
    const double ty_n = ge_clamp01d_local(0.52 * qI_n + 0.24 * qV_n + 0.14 * gI_n + 0.10 * gV_n);
    const double tz_n =
        ge_clamp01d_local(0.20 * qF_n +
                          0.18 * qA_n +
                          0.18 * qI_n +
                          0.18 * qV_n +
                          0.14 * substrate_coherence_n +
                          0.12 * source_vibration_n);

    const double max_x = std::max(1.0, (double)max_x_u32);
    const double max_y = std::max(1.0, (double)max_y_u32);
    const double max_z = std::max(1.0, (double)max_z_u32);
    const double target_x = tx_n * max_x;
    const double target_y = ty_n * max_y;
    const double target_z = tz_n * max_z;

    out.lattice_x_u32 = (uint32_t)std::llround(target_x);
    out.lattice_y_u32 = (uint32_t)std::llround(target_y);
    out.lattice_z_u32 = (uint32_t)std::llround(target_z);

    const size_t sample_stride = std::max<size_t>(1u, archive.tensor6d_cells.size() / 768u);
    double accum_phase = 0.0;
    double accum_flux = 0.0;
    double accum_curvature = 0.0;
    double accum_inertia = 0.0;
    double accum_dynamics = 0.0;
    double accum_spin = 0.0;
    double total_weight = 0.0;

    for (size_t i = 0u; i < archive.tensor6d_cells.size(); i += sample_stride) {
        const GeResearchTensor6DCell& cell = archive.tensor6d_cells[i];
        const double dx = ((double)cell.x_u32 - target_x) / (max_x + 1.0);
        const double dy = ((double)cell.y_u32 - target_y) / (max_y + 1.0);
        const double dz = ((double)cell.z_u32 - target_z) / (max_z + 1.0);
        const double dist2 = dx * dx + dy * dy + dz * dz;
        const double spatial_weight = 1.0 / (1.0 + 96.0 * dist2);
        if (spatial_weight <= 0.01) continue;

        const double phase_n = ge_clamp01d_local(cell.phase_coherence);
        const double dynamics_n =
            ge_norm_from_abs_local(ge_absd_local(cell.dtheta_dt) + 0.5 * ge_absd_local(cell.d2theta_dt2));
        const double spin_n =
            ge_norm_from_abs_local(ge_absd_local(cell.spin_x) +
                                   ge_absd_local(cell.spin_y) +
                                   ge_absd_local(cell.spin_z) +
                                   ge_absd_local(cell.oam_twist));
        const double weight = spatial_weight * (0.35 + 0.25 * phase_n + 0.20 * dynamics_n + 0.20 * spin_n);

        accum_phase += weight * phase_n;
        accum_flux += weight * ge_absd_local(cell.flux);
        accum_curvature += weight * ge_absd_local(cell.curvature);
        accum_inertia += weight * ge_absd_local(cell.inertia + cell.higgs_inertia);
        accum_dynamics += weight * (ge_absd_local(cell.dtheta_dt) + 0.5 * ge_absd_local(cell.d2theta_dt2));
        accum_spin += weight * (ge_absd_local(cell.spin_x) +
                                ge_absd_local(cell.spin_y) +
                                ge_absd_local(cell.spin_z) +
                                ge_absd_local(cell.oam_twist));
        total_weight += weight;
    }

    if (total_weight <= 1.0e-12) {
        out.interference_norm = ge_clamp01d_local(0.45 * substrate_coherence_n + 0.55 * source_vibration_n);
        out.temporal_coupling_norm = ge_clamp01d_local(0.50 * out.interference_norm + 0.50 * source_vibration_n);
        out.coherence_norm = ge_clamp01d_local(substrate_coherence_n);
        return out;
    }

    const double phase_n = ge_clamp01d_local(accum_phase / total_weight);
    const double flux_n = ge_norm_from_abs_local(accum_flux / total_weight);
    const double curvature_n = ge_norm_from_abs_local(accum_curvature / total_weight);
    const double inertia_n = ge_norm_from_abs_local(accum_inertia / total_weight);
    const double dynamics_n = ge_norm_from_abs_local(accum_dynamics / total_weight);
    const double spin_n = ge_norm_from_abs_local(accum_spin / total_weight);

    out.coherence_norm = phase_n;
    out.curvature_penalty_norm = curvature_n;
    out.inertia_penalty_norm = inertia_n;
    out.interference_norm =
        ge_clamp01d_local(0.26 * phase_n +
                          0.22 * flux_n +
                          0.18 * dynamics_n +
                          0.12 * spin_n +
                          0.12 * (1.0 - curvature_n) +
                          0.10 * substrate_coherence_n);
    out.temporal_coupling_norm =
        ge_clamp01d_local(0.30 * out.interference_norm +
                          0.22 * dynamics_n +
                          0.16 * phase_n +
                          0.12 * flux_n +
                          0.10 * source_vibration_n +
                          0.10 * substrate_coherence_n);
    return out;
}

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
    std::vector<GeResearchInterferencePredictionCell>* out_predictions) {
    out_calibration = GeResearchGpuAdaptiveCalibration{};
    if (out_predictions) out_predictions->clear();
    if (!ge_has_research_runtime_guidance(archive)) return false;

    const uint32_t axis_resolution = std::max<uint32_t>(3u, std::min<uint32_t>(axis_resolution_u32, 9u));
    const GeResearchPulseQuartet qmin = archive.pulse_window_min_quartet;
    const GeResearchPulseQuartet qmax = archive.pulse_window_max_quartet;
    const GeResearchPulseQuartet center = archive.run043_center_quartet;

    GeResearchPulseQuartet desired = desired_quartet;
    desired.F = ge_clampd_local(desired.F, qmin.F, qmax.F);
    desired.A = ge_clampd_local(desired.A, qmin.A, qmax.A);
    desired.I = ge_clampd_local(desired.I, qmin.I, qmax.I);
    desired.V = ge_clampd_local(desired.V, qmin.V, qmax.V);

    const double gpu_freq_n = ge_clamp01d_local(gpu_freq_norm_01);
    const double gpu_amp_n = ge_clamp01d_local(gpu_amp_norm_01);
    const double gpu_curr_n = ge_clamp01d_local(gpu_curr_norm_01);
    const double gpu_volt_n = ge_clamp01d_local(gpu_volt_norm_01);
    const double interference_n = ge_clamp01d_local(interference_norm_01);
    const double substrate_coherence_n = ge_clamp01d_local(substrate_coherence_norm_01);
    const double source_vibration_n = ge_clamp01d_local(source_vibration_norm_01);
    const double subsystem_residual_n = ge_clamp01d_local(subsystem_residual_norm_01);
    const double subsystem_spin_n = ge_clamp01d_local(subsystem_spin_norm_01);
    const double subsystem_coupling_n = ge_clamp01d_local(subsystem_coupling_norm_01);
    const double subsystem_controller_n = ge_clamp01d_local(subsystem_controller_norm_01);

    const double tensor_norm = ge_compute_research_tensor_gradient_norm(archive);
    const double packet_norm = ge_compute_research_packet_coherence_norm(archive);
    const double observer_norm = ge_compute_research_observer_coupling_norm(archive);
    const double recurrence_norm = ge_compute_research_recurrence_norm(archive);

    GeResearchPulseQuartet gpu_observed{};
    gpu_observed.F = ge_lerpd_local(center.F, gpu_freq_n, 0.70);
    gpu_observed.A = ge_lerpd_local(center.A, gpu_amp_n, 0.70);
    gpu_observed.I = ge_lerpd_local(center.I, gpu_curr_n, 0.70);
    gpu_observed.V = ge_lerpd_local(center.V, gpu_volt_n, 0.70);
    gpu_observed.F = ge_clampd_local(gpu_observed.F, qmin.F, qmax.F);
    gpu_observed.A = ge_clampd_local(gpu_observed.A, qmin.A, qmax.A);
    gpu_observed.I = ge_clampd_local(gpu_observed.I, qmin.I, qmax.I);
    gpu_observed.V = ge_clampd_local(gpu_observed.V, qmin.V, qmax.V);

    const double observed_bias =
        ge_clamp01d_local(0.24 +
                          0.24 * interference_n +
                          0.16 * source_vibration_n +
                          0.12 * (1.0 - substrate_coherence_n) +
                          0.12 * subsystem_controller_n +
                          0.12 * subsystem_residual_n);
    GeResearchPulseQuartet gpu_target{};
    gpu_target.F = ge_lerpd_local(desired.F, gpu_observed.F, observed_bias);
    gpu_target.A = ge_lerpd_local(desired.A, gpu_observed.A, observed_bias);
    gpu_target.I = ge_lerpd_local(desired.I, gpu_observed.I, observed_bias);
    gpu_target.V = ge_lerpd_local(desired.V, gpu_observed.V, observed_bias);

    if (out_predictions) {
        const size_t reserve_count =
            (size_t)axis_resolution * (size_t)axis_resolution * (size_t)axis_resolution * (size_t)axis_resolution;
        out_predictions->reserve(reserve_count);
    }

    double best_objective = -1.0;
    GeResearchInterferencePredictionCell best_cell{};
    bool best_cell_valid = false;

    const auto value_for_axis = [&](double lo, double hi, uint32_t idx) -> double {
        if (axis_resolution <= 1u) return lo;
        const double t = (double)idx / (double)(axis_resolution - 1u);
        return ge_lerpd_local(lo, hi, t);
    };

    for (uint32_t fi = 0u; fi < axis_resolution; ++fi) {
        for (uint32_t ai = 0u; ai < axis_resolution; ++ai) {
            for (uint32_t ii = 0u; ii < axis_resolution; ++ii) {
                for (uint32_t vi = 0u; vi < axis_resolution; ++vi) {
                    GeResearchInterferencePredictionCell cell{};
                    cell.quartet.F = value_for_axis(qmin.F, qmax.F, fi);
                    cell.quartet.A = value_for_axis(qmin.A, qmax.A, ai);
                    cell.quartet.I = value_for_axis(qmin.I, qmax.I, ii);
                    cell.quartet.V = value_for_axis(qmin.V, qmax.V, vi);

                    const GeResearchPredictionLatticeSample lattice_sample =
                        ge_sample_research_prediction_lattice(archive,
                                                              qmin,
                                                              qmax,
                                                              gpu_target,
                                                              cell.quartet,
                                                              substrate_coherence_n,
                                                              source_vibration_n);

                    cell.predicted_score =
                        ge_evaluate_research_metric_model(archive.run043_silicon_score_model, center, cell.quartet);
                    cell.predicted_trap_ratio =
                        ge_evaluate_research_metric_model(archive.run043_trap_ratio_model, center, cell.quartet);
                    cell.predicted_coherence =
                        ge_evaluate_research_metric_model(archive.run043_coherence_model, center, cell.quartet);
                    cell.predicted_inertia =
                        ge_evaluate_research_metric_model(archive.run043_inertia_model, center, cell.quartet);
                    cell.predicted_curvature =
                        ge_evaluate_research_metric_model(archive.run043_curvature_model, center, cell.quartet);

                    const double score_align =
                        ge_clamp01d_local(cell.predicted_score /
                                          std::max(archive.temporal_collapse_gates.gate_score, 1.0e-9));
                    const double trap_align =
                        ge_clamp01d_local(cell.predicted_trap_ratio /
                                          std::max(archive.temporal_collapse_gates.gate_trap, 1.0e-9));
                    const double coherence_align =
                        ge_clamp01d_local(cell.predicted_coherence /
                                          std::max(archive.temporal_collapse_gates.gate_coherence, 1.0e-9));
                    const double curvature_penalty =
                        ge_clamp01d_local(0.55 * ge_norm_from_abs_local(cell.predicted_curvature -
                                                                        archive.run043_curvature_model.center_value) +
                                          0.45 * lattice_sample.curvature_penalty_norm);
                    const double inertia_penalty =
                        ge_clamp01d_local(0.55 * ge_norm_from_abs_local(cell.predicted_inertia -
                                                                        archive.run043_inertia_model.center_value) +
                                          0.45 * lattice_sample.inertia_penalty_norm);

                    const double gpu_dist = ge_quartet_distance_norm_local(cell.quartet, gpu_target, qmin, qmax);
                    const double desired_dist = ge_quartet_distance_norm_local(cell.quartet, desired, qmin, qmax);
                    cell.gpu_alignment_norm =
                        ge_clamp01d_local(1.0 - (0.65 * gpu_dist + 0.35 * desired_dist));

                    cell.lattice_interference_norm = lattice_sample.interference_norm;
                    cell.lattice_temporal_coupling_norm = lattice_sample.temporal_coupling_norm;
                    cell.lattice_x_u32 = lattice_sample.lattice_x_u32;
                    cell.lattice_y_u32 = lattice_sample.lattice_y_u32;
                    cell.lattice_z_u32 = lattice_sample.lattice_z_u32;

                    cell.vector_coupling_norm =
                        ge_clamp01d_local(0.14 * tensor_norm +
                                          0.14 * packet_norm +
                                          0.12 * observer_norm +
                                          0.10 * recurrence_norm +
                                          0.12 * cell.lattice_temporal_coupling_norm +
                                          0.08 * cell.lattice_interference_norm +
                                          0.08 * lattice_sample.coherence_norm +
                                          0.08 * ge_clamp01d_local(0.40 * subsystem_coupling_n +
                                                                   0.24 * subsystem_spin_n +
                                                                   0.20 * subsystem_controller_n +
                                                                   0.16 * subsystem_residual_n) +
                                          0.07 * subsystem_coupling_n +
                                          0.04 * substrate_coherence_n +
                                          0.03 * source_vibration_n);
                    cell.subsystem_feedback_norm =
                        ge_clamp01d_local(0.40 * subsystem_coupling_n +
                                          0.24 * subsystem_spin_n +
                                          0.20 * subsystem_controller_n +
                                          0.16 * subsystem_residual_n);

                    cell.predicted_interference_norm =
                        ge_clamp01d_local(0.18 * cell.gpu_alignment_norm +
                                          0.15 * coherence_align +
                                          0.12 * score_align +
                                          0.10 * trap_align +
                                          0.14 * cell.vector_coupling_norm +
                                          0.09 * cell.subsystem_feedback_norm +
                                          0.07 * interference_n +
                                          0.06 * cell.lattice_interference_norm +
                                          0.05 * cell.lattice_temporal_coupling_norm +
                                          0.04 * subsystem_controller_n -
                                          0.08 * curvature_penalty -
                                          0.06 * inertia_penalty);
                    cell.certainty_norm =
                        ge_clamp01d_local(0.24 * cell.predicted_interference_norm +
                                          0.18 * cell.gpu_alignment_norm +
                                          0.16 * cell.vector_coupling_norm +
                                          0.14 * coherence_align +
                                          0.14 * cell.lattice_temporal_coupling_norm +
                                          0.14 * cell.subsystem_feedback_norm);

                    uint64_t spectral_id = 0xC6A4A7935BD1E995ull;
                    spectral_id = ge_mix_u64_local(spectral_id, (uint64_t)(fi + 1u));
                    spectral_id = ge_mix_u64_local(spectral_id, (uint64_t)(ai + 1u) << 8u);
                    spectral_id = ge_mix_u64_local(spectral_id, (uint64_t)(ii + 1u) << 16u);
                    spectral_id = ge_mix_u64_local(spectral_id, (uint64_t)(vi + 1u) << 24u);
                    spectral_id = ge_mix_u64_local(spectral_id, (uint64_t)cell.lattice_x_u32 << 32u);
                    spectral_id = ge_mix_u64_local(spectral_id, (uint64_t)cell.lattice_y_u32 << 16u);
                    spectral_id = ge_mix_u64_local(spectral_id, (uint64_t)cell.lattice_z_u32);
                    spectral_id = ge_mix_u64_local(spectral_id,
                                                   (uint64_t)std::llround(cell.predicted_interference_norm * 1000000.0));
                    spectral_id = ge_mix_u64_local(spectral_id,
                                                   (uint64_t)std::llround(cell.vector_coupling_norm * 1000000.0));
                    spectral_id = ge_mix_u64_local(spectral_id,
                                                   (uint64_t)std::llround(cell.subsystem_feedback_norm * 1000000.0));
                    cell.trajectory_spectral_id_u64 = spectral_id;

                    const double objective =
                        0.30 * cell.predicted_interference_norm +
                        0.18 * cell.gpu_alignment_norm +
                        0.16 * cell.vector_coupling_norm +
                        0.14 * cell.certainty_norm +
                        0.08 * cell.subsystem_feedback_norm +
                        0.07 * cell.lattice_interference_norm +
                        0.07 * cell.lattice_temporal_coupling_norm;

                    if (!best_cell_valid || objective > best_objective) {
                        best_objective = objective;
                        best_cell = cell;
                        best_cell_valid = true;
                    }

                    if (out_predictions) out_predictions->push_back(cell);
                }
            }
        }
    }

    if (!best_cell_valid) return false;

    const double adaptation_blend =
        ge_clamp01d_local(0.12 +
                          0.24 * best_cell.certainty_norm +
                          0.18 * best_cell.lattice_temporal_coupling_norm +
                          0.16 * interference_n +
                          0.14 * source_vibration_n +
                          0.16 * best_cell.subsystem_feedback_norm);
    GeResearchPulseQuartet adapted{};
    adapted.F = ge_lerpd_local(desired.F, best_cell.quartet.F, adaptation_blend);
    adapted.A = ge_lerpd_local(desired.A, best_cell.quartet.A, adaptation_blend);
    adapted.I = ge_lerpd_local(desired.I, best_cell.quartet.I, adaptation_blend);
    adapted.V = ge_lerpd_local(desired.V, best_cell.quartet.V, adaptation_blend);
    adapted.F = ge_clampd_local(adapted.F, qmin.F, qmax.F);
    adapted.A = ge_clampd_local(adapted.A, qmin.A, qmax.A);
    adapted.I = ge_clampd_local(adapted.I, qmin.I, qmax.I);
    adapted.V = ge_clampd_local(adapted.V, qmin.V, qmax.V);

    const double next_pulse_correction_n =
        ge_clamp01d_local(0.16 +
                          0.24 * best_cell.predicted_interference_norm +
                          0.18 * best_cell.lattice_temporal_coupling_norm +
                          0.14 * interference_n +
                          0.12 * source_vibration_n +
                          0.16 * best_cell.subsystem_feedback_norm);
    GeResearchPulseQuartet next_pulse{};
    next_pulse.F = ge_clampd_local(
        ge_lerpd_local(desired.F,
                       0.54 * best_cell.quartet.F + 0.28 * gpu_target.F + 0.18 * center.F,
                       next_pulse_correction_n),
        qmin.F, qmax.F);
    next_pulse.A = ge_clampd_local(
        ge_lerpd_local(desired.A,
                       0.54 * best_cell.quartet.A + 0.28 * gpu_target.A + 0.18 * center.A,
                       next_pulse_correction_n),
        qmin.A, qmax.A);
    next_pulse.I = ge_clampd_local(
        ge_lerpd_local(desired.I,
                       0.54 * best_cell.quartet.I + 0.28 * gpu_target.I + 0.18 * center.I,
                       next_pulse_correction_n),
        qmin.I, qmax.I);
    next_pulse.V = ge_clampd_local(
        ge_lerpd_local(desired.V,
                       0.54 * best_cell.quartet.V + 0.28 * gpu_target.V + 0.18 * center.V,
                       next_pulse_correction_n),
        qmin.V, qmax.V);

    out_calibration.valid = true;
    out_calibration.axis_resolution_u32 = axis_resolution;
    out_calibration.prediction_count_u32 =
        axis_resolution * axis_resolution * axis_resolution * axis_resolution;
    out_calibration.observed_gpu_freq_norm = gpu_freq_n;
    out_calibration.observed_gpu_amp_norm = gpu_amp_n;
    out_calibration.observed_gpu_curr_norm = gpu_curr_n;
    out_calibration.observed_gpu_volt_norm = gpu_volt_n;
    out_calibration.observed_interference_norm = interference_n;
    out_calibration.observed_coherence_norm = substrate_coherence_n;
    out_calibration.observed_source_vibration_norm = source_vibration_n;
    out_calibration.observed_subsystem_residual_norm = subsystem_residual_n;
    out_calibration.observed_subsystem_spin_norm = subsystem_spin_n;
    out_calibration.observed_subsystem_coupling_norm = subsystem_coupling_n;
    out_calibration.observed_subsystem_controller_norm = subsystem_controller_n;
    out_calibration.tensor_gradient_norm = tensor_norm;
    out_calibration.packet_coherence_norm = packet_norm;
    out_calibration.observer_coupling_norm = observer_norm;
    out_calibration.recurrence_norm = recurrence_norm;
    out_calibration.prediction_confidence_norm = best_cell.certainty_norm;
    out_calibration.best_gpu_alignment_norm = best_cell.gpu_alignment_norm;
    out_calibration.best_vector_coupling_norm = best_cell.vector_coupling_norm;
    out_calibration.best_subsystem_feedback_norm = best_cell.subsystem_feedback_norm;
    out_calibration.best_interference_norm = best_cell.predicted_interference_norm;
    out_calibration.best_lattice_interference_norm = best_cell.lattice_interference_norm;
    out_calibration.best_temporal_coupling_norm = best_cell.lattice_temporal_coupling_norm;
    out_calibration.next_pulse_correction_norm = next_pulse_correction_n;
    out_calibration.best_silicon_score = best_cell.predicted_score;
    out_calibration.best_trap_ratio = best_cell.predicted_trap_ratio;
    out_calibration.best_coherence = best_cell.predicted_coherence;
    out_calibration.best_inertia = best_cell.predicted_inertia;
    out_calibration.best_curvature = best_cell.predicted_curvature;
    out_calibration.gpu_observed_quartet = gpu_observed;
    out_calibration.best_quartet = best_cell.quartet;
    out_calibration.adapted_quartet = adapted;
    out_calibration.next_pulse_quartet = next_pulse;
    out_calibration.best_lattice_x_u32 = best_cell.lattice_x_u32;
    out_calibration.best_lattice_y_u32 = best_cell.lattice_y_u32;
    out_calibration.best_lattice_z_u32 = best_cell.lattice_z_u32;
    out_calibration.best_trajectory_spectral_id_u64 = best_cell.trajectory_spectral_id_u64;
    return true;
}

bool ge_build_research_live_compute_plan(
    const GeResearchConfinementArchive& archive,
    const GeResearchGpuAdaptiveCalibration& calibration,
    const GeResearchRuntimeGuidance& guidance,
    GeResearchLiveComputePlan& out_plan) {
    out_plan = GeResearchLiveComputePlan{};
    if (!ge_has_research_runtime_guidance(archive)) return false;
    if (!calibration.valid || !guidance.valid) return false;

    const double score_align =
        ge_clamp01d_local(calibration.best_silicon_score /
                          std::max(archive.temporal_collapse_gates.gate_score, 1.0e-9));
    const double trap_align =
        ge_clamp01d_local(calibration.best_trap_ratio /
                          std::max(archive.temporal_collapse_gates.gate_trap, 1.0e-9));
    const double coherence_align =
        ge_clamp01d_local(calibration.best_coherence /
                          std::max(archive.temporal_collapse_gates.gate_coherence, 1.0e-9));
    const double readiness =
        ge_clamp01d_local(0.20 * calibration.prediction_confidence_norm +
                          0.16 * calibration.best_gpu_alignment_norm +
                          0.12 * calibration.best_interference_norm +
                          0.12 * guidance.certainty_norm +
                          0.09 * guidance.exactness_norm +
                          0.07 * score_align +
                          0.06 * trap_align +
                          0.06 * coherence_align +
                          0.12 * calibration.best_subsystem_feedback_norm);

    const double extrapolation_norm =
        ge_clamp01d_local(0.34 * calibration.best_vector_coupling_norm +
                          0.20 * guidance.recurrence_norm +
                          0.16 * guidance.tensor_gradient_norm +
                          0.14 * guidance.source_memory_norm +
                          0.16 * calibration.best_subsystem_feedback_norm);
    const double ledger_norm =
        ge_clamp01d_local(0.42 * calibration.best_interference_norm +
                          0.26 * guidance.temporal_coupling_norm +
                          0.16 * guidance.observer_coupling_norm +
                          0.16 * calibration.best_subsystem_feedback_norm);

    const double compute_blend =
        ge_clamp01d_local(0.24 + 0.46 * readiness + 0.30 * extrapolation_norm);
    GeResearchPulseQuartet compute{};
    compute.F = ge_lerpd_local(guidance.corrected_quartet.F, calibration.adapted_quartet.F, compute_blend);
    compute.A = ge_lerpd_local(guidance.corrected_quartet.A, calibration.adapted_quartet.A, compute_blend);
    compute.I = ge_lerpd_local(guidance.corrected_quartet.I, calibration.adapted_quartet.I, compute_blend);
    compute.V = ge_lerpd_local(guidance.corrected_quartet.V, calibration.adapted_quartet.V, compute_blend);
    compute.F = ge_clampd_local(compute.F, archive.pulse_window_min_quartet.F, archive.pulse_window_max_quartet.F);
    compute.A = ge_clampd_local(compute.A, archive.pulse_window_min_quartet.A, archive.pulse_window_max_quartet.A);
    compute.I = ge_clampd_local(compute.I, archive.pulse_window_min_quartet.I, archive.pulse_window_max_quartet.I);
    compute.V = ge_clampd_local(compute.V, archive.pulse_window_min_quartet.V, archive.pulse_window_max_quartet.V);

    out_plan.valid = true;
    out_plan.ready = (readiness >= 0.72);
    out_plan.readiness_norm = readiness;
    out_plan.interference_ledger_norm = ledger_norm;
    out_plan.encoded_extrapolation_norm = extrapolation_norm;
    out_plan.score_alignment_norm = score_align;
    out_plan.trap_alignment_norm = trap_align;
    out_plan.coherence_alignment_norm = coherence_align;
    out_plan.compute_quartet = compute;
    out_plan.trajectory_spectral_id_u64 = calibration.best_trajectory_spectral_id_u64;
    return true;
}

uint32_t ge_particle_class_rgba8(GeResearchParticleClass particle_class) {
    switch (particle_class) {
        case GeResearchParticleClass::Photon:   return 0xFFFFFFFFu;
        case GeResearchParticleClass::Weighted: return 0xFF2A2AFFu;
        case GeResearchParticleClass::Flavor:   return 0xFF2AE8FFu;
        case GeResearchParticleClass::Charged:  return 0xFFFF9C2Au;
        default:                                return 0xFFFFFFFFu;
    }
}

const char* ge_particle_class_name_ascii(GeResearchParticleClass particle_class) {
    switch (particle_class) {
        case GeResearchParticleClass::Photon:   return "photon";
        case GeResearchParticleClass::Weighted: return "weighted";
        case GeResearchParticleClass::Flavor:   return "flavor";
        case GeResearchParticleClass::Charged:  return "charged";
        default:                                return "unknown";
    }
}

} // namespace genesis
