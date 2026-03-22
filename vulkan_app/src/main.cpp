#include "GE_app.hpp"

#include <shellapi.h>

#include <cstdlib>
#include <string>

namespace {

static std::string wide_to_utf8_local(const std::wstring& s) {
    if (s.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static std::string trim_ascii_copy(std::string s) {
    while (!s.empty() && (unsigned char)s.front() <= 32u) s.erase(s.begin());
    while (!s.empty() && (unsigned char)s.back() <= 32u) s.pop_back();
    return s;
}

static std::string lower_ascii_copy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return s;
}

static bool parse_bool_ascii(const std::string& s, bool fallback) {
    const std::string v = lower_ascii_copy(trim_ascii_copy(s));
    if (v == "1" || v == "true" || v == "yes" || v == "on" || v == "enable" || v == "enabled") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off" || v == "disable" || v == "disabled") return false;
    return fallback;
}

static void apply_arg_kv(ewv::AppConfig& cfg, const std::string& key_in, const std::string& value_in) {
    const std::string key = lower_ascii_copy(trim_ascii_copy(key_in));
    const std::string value = trim_ascii_copy(value_in);
    if (key == "title") cfg.app_title_utf8 = value;
    else if (key == "width") {
        const int v = std::atoi(value.c_str());
        if (v > 0) cfg.initial_width = v;
    } else if (key == "height") {
        const int v = std::atoi(value.c_str());
        if (v > 0) cfg.initial_height = v;
    } else if (key == "cmd" || key == "command" || key == "runtime_cmd") {
        if (!value.empty()) cfg.startup_commands_utf8.push_back(value);
    } else if (key == "bootstrap") {
        if (!value.empty()) cfg.startup_commands_utf8.push_back(std::string("bootstrap:") + value);
    } else if (key == "chat") {
        if (!value.empty()) cfg.startup_commands_utf8.push_back(std::string("chat:") + value);
    } else if (key == "live_mode" || key == "livemode") {
        cfg.start_live_mode = parse_bool_ascii(value, cfg.start_live_mode);
    } else if (key == "resonance" || key == "resonance_view") {
        cfg.start_resonance_view = parse_bool_ascii(value, cfg.start_resonance_view);
    } else if (key == "confinement" || key == "confinement_particles" || key == "particles") {
        cfg.start_confinement_particles = parse_bool_ascii(value, cfg.start_confinement_particles);
    } else if (key == "visualize" || key == "visualization") {
        cfg.start_visualization = parse_bool_ascii(value, cfg.start_visualization);
    } else if (key == "headless") {
        cfg.start_visualization = !parse_bool_ascii(value, false);
    } else if (key == "log" || key == "log_path") {
        cfg.output_log_path_utf8 = value;
    } else if (key == "appid" || key == "app_user_model_id") {
        cfg.app_user_model_id_utf8 = value;
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    ewv::AppConfig cfg;
    cfg.app_title_utf8 = "Genesis Engine";
    cfg.initial_width = 1600;
    cfg.initial_height = 900;
#if defined(GENESIS_RUNTIME_BUILD) && GENESIS_RUNTIME_BUILD
    cfg.app_user_model_id_utf8 = "GenesisEngine.Runtime";
#else
    cfg.app_user_model_id_utf8 = "GenesisEngine.Editor";
#endif

    int argc = 0;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv_w) {
        for (int i = 1; i < argc; ++i) {
            std::string a = wide_to_utf8_local(argv_w[i] ? std::wstring(argv_w[i]) : std::wstring());
            if (a.empty()) continue;
            if (a.rfind("--", 0) == 0 || a.rfind("-", 0) == 0) {
                while (!a.empty() && a.front() == '-') a.erase(a.begin());
                std::string key = a;
                std::string value = "1";
                const size_t eq = key.find('=');
                if (eq != std::string::npos) {
                    value = key.substr(eq + 1u);
                    key.resize(eq);
                } else if ((i + 1) < argc) {
                    const std::wstring next = argv_w[i + 1] ? std::wstring(argv_w[i + 1]) : std::wstring();
                    if (!next.empty() && next[0] != L'-') {
                        value = wide_to_utf8_local(next);
                        ++i;
                    }
                }
                apply_arg_kv(cfg, key, value);
                continue;
            }
            const size_t eq = a.find('=');
            if (eq != std::string::npos) {
                apply_arg_kv(cfg, a.substr(0, eq), a.substr(eq + 1u));
            }
        }
        LocalFree(argv_w);
    }

    ewv::App app(cfg);
    return app.Run(hInst);
}
