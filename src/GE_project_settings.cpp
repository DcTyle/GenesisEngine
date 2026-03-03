#include "GE_project_settings.hpp"

#include <fstream>
#include <sstream>

// Deterministic key=value parser. Strict: unknown keys are ignored, malformed
// lines fail the load.

static bool _parse_i32(const std::string& v, int32_t& out) {
    char* end = nullptr;
    long long x = std::strtoll(v.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    if (x < INT32_MIN || x > INT32_MAX) return false;
    out = (int32_t)x;
    return true;
}

static bool _parse_u16(const std::string& v, uint16_t& out) {
    char* end = nullptr;
    long long x = std::strtoll(v.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    if (x < 0 || x > 65535) return false;
    out = (uint16_t)x;
    return true;
}

bool ge_project_settings_load(EwProjectSettings& out, const std::string& path_utf8, std::string& out_err) {
    std::ifstream f(path_utf8, std::ios::in | std::ios::binary);
    if (!f.good()) {
        out_err = "Project settings file not found: " + path_utf8;
        return false;
    }
    std::string line;
    uint32_t lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            out_err = "Malformed settings line (missing '=') at " + std::to_string(lineno);
            return false;
        }
        const std::string k = line.substr(0, eq);
        const std::string v = line.substr(eq + 1);

        if (k == "rendering.dnoise_gain_q16_16") {
            if (!_parse_i32(v, out.rendering.dnoise_gain_q16_16)) return false;
        } else if (k == "rendering.dnoise_bias_q16_16") {
            if (!_parse_i32(v, out.rendering.dnoise_bias_q16_16)) return false;
        } else if (k == "physics.contact_pressure_gain_q16_16") {
            if (!_parse_i32(v, out.physics.contact_pressure_gain_q16_16)) return false;
        } else if (k == "camera.default_focal_length_mm_q16_16") {
            if (!_parse_i32(v, out.camera.default_focal_length_mm_q16_16)) return false;
        } else if (k == "camera.default_aperture_f_q16_16") {
            if (!_parse_i32(v, out.camera.default_aperture_f_q16_16)) return false;
        } else if (k == "camera.default_exposure_ev_q16_16") {
            if (!_parse_i32(v, out.camera.default_exposure_ev_q16_16)) return false;
        } else if (k == "camera.move_speed_mps_q16_16") {
            if (!_parse_i32(v, out.camera.move_speed_mps_q16_16)) return false;
        } else if (k == "camera.move_step_m_q16_16") {
            if (!_parse_i32(v, out.camera.move_step_m_q16_16)) return false;
        } else if (k == "camera.look_sens_rad_per_unit_q16_16") {
            if (!_parse_i32(v, out.camera.look_sens_rad_per_unit_q16_16)) return false;
        } else if (k == "camera.zoom_step_m_q16_16") {
            if (!_parse_i32(v, out.camera.zoom_step_m_q16_16)) return false;
        } else if (k == "input.bindings_path_utf8") {
            out.input.bindings_path_utf8 = v;
        } else if (k == "ai.global_coherence_gate_q15") {
            if (!_parse_u16(v, out.ai.global_coherence_gate_q15)) return false;
        } else if (k == "simulation.fixed_dt_ms_s32") {
            if (!_parse_i32(v, out.simulation.fixed_dt_ms_s32)) return false;
        } else {
            // unknown key ignored for forward compatibility.
        }
    }
    return true;
}

bool ge_project_settings_save(const EwProjectSettings& s, const std::string& path_utf8, std::string& out_err) {
    std::ofstream f(path_utf8, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.good()) {
        out_err = "Failed to open for write: " + path_utf8;
        return false;
    }
    f << "# GenesisEngine Project Settings (deterministic)\n";
    f << "rendering.dnoise_gain_q16_16=" << s.rendering.dnoise_gain_q16_16 << "\n";
    f << "rendering.dnoise_bias_q16_16=" << s.rendering.dnoise_bias_q16_16 << "\n";
    f << "physics.contact_pressure_gain_q16_16=" << s.physics.contact_pressure_gain_q16_16 << "\n";
    f << "camera.default_focal_length_mm_q16_16=" << s.camera.default_focal_length_mm_q16_16 << "\n";
    f << "camera.default_aperture_f_q16_16=" << s.camera.default_aperture_f_q16_16 << "\n";
    f << "camera.default_exposure_ev_q16_16=" << s.camera.default_exposure_ev_q16_16 << "\n";
    f << "camera.move_speed_mps_q16_16=" << s.camera.move_speed_mps_q16_16 << "\n";
    f << "camera.move_step_m_q16_16=" << s.camera.move_step_m_q16_16 << "\n";
    f << "camera.look_sens_rad_per_unit_q16_16=" << s.camera.look_sens_rad_per_unit_q16_16 << "\n";
    f << "camera.zoom_step_m_q16_16=" << s.camera.zoom_step_m_q16_16 << "\n";
    f << "input.bindings_path_utf8=" << s.input.bindings_path_utf8 << "\n";
    f << "ai.global_coherence_gate_q15=" << (uint32_t)s.ai.global_coherence_gate_q15 << "\n";
    f << "simulation.fixed_dt_ms_s32=" << s.simulation.fixed_dt_ms_s32 << "\n";
    return true;
}
