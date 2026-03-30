#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ew_cli_args.hpp"

static bool ew_append_runtime_commands(const std::filesystem::path& research_root,
                                       const std::vector<std::string>& commands,
                                       std::string* out_error) {
    if (commands.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(research_root, ec);
    if (ec) {
        if (out_error) *out_error = "unable to create research root directory";
        return false;
    }
    const std::filesystem::path command_path = research_root / "runtime_commands.txt";
    std::ofstream file(command_path, std::ios::out | std::ios::app | std::ios::binary);
    if (!file.is_open()) {
        if (out_error) *out_error = "unable to open runtime command file";
        return false;
    }
    for (const std::string& cmd : commands) {
        file << cmd << "\n";
    }
    if (!file.good()) {
        if (out_error) *out_error = "unable to append runtime commands";
        return false;
    }
    return true;
}

static std::string ew_lower_ascii(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return v;
}

int main(int argc, char** argv) {
    ew::CliArgsKV args;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, args)) {
        std::fprintf(stderr, "ERR: invalid CLI args; expected --key value or key=value forms\n");
        return 2;
    }

    std::string research_root = "ResearchConfinement";
    (void)ew::ew_cli_get_str(args, "research_root", research_root);
    (void)ew::ew_cli_get_str(args, "research-root", research_root);

    std::vector<std::string> runtime_cmds;

    std::string custom_command;
    if (ew::ew_cli_get_str(args, "runtime_command", custom_command) ||
        ew::ew_cli_get_str(args, "runtime-command", custom_command) ||
        ew::ew_cli_get_str(args, "cmd", custom_command)) {
        if (!custom_command.empty()) {
            runtime_cmds.push_back(custom_command);
        }
    }

    bool status = false;
    if (ew::ew_cli_get_bool(args, "research_status", status) ||
        ew::ew_cli_get_bool(args, "research-status", status)) {
        if (status) runtime_cmds.push_back("/research_status");
    }

    bool reload = false;
    if (ew::ew_cli_get_bool(args, "research_reload", reload) ||
        ew::ew_cli_get_bool(args, "research-reload", reload)) {
        if (reload) runtime_cmds.push_back("/research_reload");
    }

    std::string sim_mode;
    if (ew::ew_cli_get_str(args, "sim_mode", sim_mode) ||
        ew::ew_cli_get_str(args, "sim-mode", sim_mode)) {
        const std::string mode_lc = ew_lower_ascii(sim_mode);
        runtime_cmds.push_back((mode_lc == "vector") ? "/sim_mode vector" : "/sim_mode frequency");
    }

    bool stov_mode = false;
    if (ew::ew_cli_get_bool(args, "stov_mode", stov_mode) ||
        ew::ew_cli_get_bool(args, "stov-mode", stov_mode)) {
        runtime_cmds.push_back(stov_mode ? "/stov_mode=1" : "/stov_mode=0");
    }

    uint32_t stream_hz = 0;
    if (ew::ew_cli_get_u32(args, "sim_stream_hz", stream_hz) ||
        ew::ew_cli_get_u32(args, "sim-stream-hz", stream_hz)) {
        runtime_cmds.push_back("/sim_stream_hz=" + std::to_string(stream_hz));
    }

    uint32_t lattice_edge = 0;
    if (ew::ew_cli_get_u32(args, "sim_lattice_edge", lattice_edge) ||
        ew::ew_cli_get_u32(args, "sim-lattice-edge", lattice_edge)) {
        runtime_cmds.push_back("/sim_lattice_edge=" + std::to_string(lattice_edge));
    }

    std::string sim_param;
    if (ew::ew_cli_get_str(args, "sim_param", sim_param) ||
        ew::ew_cli_get_str(args, "sim-param", sim_param)) {
        if (!sim_param.empty()) {
            runtime_cmds.push_back("/sim_param=" + sim_param);
        }
    } else {
        bool has_a = false, has_f = false, has_i = false, has_v = false;
        std::string a, f, i, v;
        has_a = ew::ew_cli_get_str(args, "sim_a", a) || ew::ew_cli_get_str(args, "sim-a", a);
        has_f = ew::ew_cli_get_str(args, "sim_f", f) || ew::ew_cli_get_str(args, "sim-f", f);
        has_i = ew::ew_cli_get_str(args, "sim_i", i) || ew::ew_cli_get_str(args, "sim-i", i);
        has_v = ew::ew_cli_get_str(args, "sim_v", v) || ew::ew_cli_get_str(args, "sim-v", v);
        if (has_a || has_f || has_i || has_v) {
            std::ostringstream oss;
            oss << "/sim_param=";
            bool first = true;
            auto append = [&](const char* k, const std::string& val) {
                if (!first) oss << " ";
                first = false;
                oss << k << ":" << val;
            };
            if (has_a) append("A", a);
            if (has_f) append("F", f);
            if (has_i) append("I", i);
            if (has_v) append("V", v);
            runtime_cmds.push_back(oss.str());
        }
    }

    if (!runtime_cmds.empty()) {
        std::string err;
        if (!ew_append_runtime_commands(std::filesystem::path(research_root), runtime_cmds, &err)) {
            std::fprintf(stderr, "ERR: %s\n", err.c_str());
            return 3;
        }
        std::printf("runtime_cmd_file=%s\n",
                    (std::filesystem::path(research_root) / "runtime_commands.txt").string().c_str());
        for (const std::string& cmd : runtime_cmds) {
            std::printf("queued_cmd=%s\n", cmd.c_str());
        }
        return 0;
    }

    std::printf("No runtime command arguments provided.\n");
    std::printf("Use --sim-mode, --stov-mode, --sim-stream-hz, --sim-lattice-edge, --sim-param, or --runtime-command.\n");
    return 0;
}
