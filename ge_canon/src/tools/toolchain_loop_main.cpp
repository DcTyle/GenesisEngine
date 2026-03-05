#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ew_cli_args.hpp"

namespace fs = std::filesystem;

static std::string trim(const std::string &s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    size_t b = s.size();
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) b--;
    return s.substr(a, b - a);
}

static std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

static bool ensure_parent_dir(const fs::path &p) {
    std::error_code ec;
    fs::path parent = p.parent_path();
    if (parent.empty()) return false;
    if (fs::exists(parent, ec)) return true;
    return fs::create_directories(parent, ec);
}

static bool append_text_file(const fs::path &abs_path, const std::string &payload) {
    if (!ensure_parent_dir(abs_path)) return false;
    std::ofstream f(abs_path, std::ios::binary | std::ios::app);
    if (!f) return false;
    f.write(payload.data(), (std::streamsize)payload.size());
    // Ensure final newline for deterministic diffs.
    if (payload.empty() || payload.back() != '\n') {
        f.put('\n');
    }
    return true;
}

static int run_command_capture_to_file(const std::string &cmd, const fs::path &log_path) {
    ensure_parent_dir(log_path);
#if defined(_WIN32)
    // Use cmd.exe on Windows.
    std::string full = "cmd.exe /c \"" + cmd + " > \"" + log_path.string() + "\" 2>&1\"";
#else
    std::string full = cmd + " > \"" + log_path.string() + "\" 2>&1";
#endif
    return std::system(full.c_str());
}

static void print_usage() {
    std::cout << "ew_toolchain_loop --repo_root <path>\n";
    std::cout << "Reads directives from stdin:\n";
    std::cout << "  PATCH:<rel_file>:APPEND:<text>\n";
    std::cout << "  BUILD:<build_dir_rel_or_abs>:<config>:<target>\n";
    std::cout << "  TEST:<build_dir_rel_or_abs>:<config>\n";
}

int main(int argc, char **argv) {
    fs::path repo_root;

    ew::CliArgsKV args;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, args)) {
        std::cerr << "ERROR: malformed args\n";
        return 2;
    }
    std::string rr;
    if (ew::ew_cli_get_str(args, "repo_root", rr)) repo_root = fs::path(rr);
    if (repo_root.empty()) {
        if (ew::ew_cli_get_str(args, "repo-root", rr)) repo_root = fs::path(rr);
    }

    bool help = false;
    (void)ew::ew_cli_get_bool(args, "help", help);
    if (help) {
        print_usage();
        return 0;
    }

    if (repo_root.empty()) {
        std::cerr << "ERROR: --repo_root is required\n";
        print_usage();
        return 2;
    }

    std::error_code ec;
    repo_root = fs::weakly_canonical(repo_root, ec);
    if (ec || repo_root.empty()) {
        std::cerr << "ERROR: invalid repo_root\n";
        return 2;
    }

    const fs::path logs_dir = repo_root / "build" / "toolchain_logs";
    std::cout << "TOOL: repo_root=" << repo_root.string() << "\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        line = trim(line);
        if (line.empty()) continue;

        if (line.rfind("PATCH:", 0) == 0) {
            // PATCH:<rel_file>:APPEND:<text>
            const std::string rest = line.substr(std::strlen("PATCH:"));
            auto parts = split(rest, ':');
            if (parts.size() < 3) {
                std::cout << "TOOL: PATCH rejected (bad format)\n";
                continue;
            }

            const std::string rel_file = parts[0];
            const std::string op = parts[1];

            // Re-join payload (may contain ':')
            std::string payload;
            for (size_t i = 2; i < parts.size(); ++i) {
                if (i > 2) payload.push_back(':');
                payload += parts[i];
            }

            if (op != "APPEND") {
                std::cout << "TOOL: PATCH rejected (only APPEND supported)\n";
                continue;
            }

            fs::path abs_file = repo_root / rel_file;
            abs_file = fs::weakly_canonical(abs_file, ec);
            if (ec) {
                // If file does not exist yet, weakly_canonical can fail; fall back to lexically_normal.
                ec.clear();
                abs_file = (repo_root / rel_file).lexically_normal();
            }

            // Fail-closed if patch tries to escape repo root.
            const std::string abs_str = abs_file.string();
            const std::string root_str = repo_root.string();
            if (abs_str.rfind(root_str, 0) != 0) {
                std::cout << "TOOL: PATCH rejected (path escapes repo_root)\n";
                continue;
            }

            if (!append_text_file(abs_file, payload)) {
                std::cout << "TOOL: PATCH failed\n";
            } else {
                std::cout << "TOOL: PATCH ok " << rel_file << "\n";
            }
            continue;
        }

        if (line.rfind("BUILD:", 0) == 0) {
            // BUILD:<build_dir>:<config>:<target>
            const std::string rest = line.substr(std::strlen("BUILD:"));
            auto parts = split(rest, ':');
            if (parts.size() != 3) {
                std::cout << "TOOL: BUILD rejected (bad format)\n";
                continue;
            }
            const std::string build_dir_in = parts[0];
            const std::string config = parts[1];
            const std::string target = parts[2];

            fs::path build_dir = fs::path(build_dir_in);
            if (build_dir.is_relative()) build_dir = repo_root / build_dir;
            build_dir = build_dir.lexically_normal();

            const fs::path log_path = logs_dir / ("build_" + config + "_" + target + ".log");

            std::ostringstream cmd;
            cmd << "cmake --build \"" << build_dir.string() << "\" --config \"" << config << "\" --target \"" << target << "\"";
            std::cout << "TOOL: BUILD start\n";
            const int rc = run_command_capture_to_file(cmd.str(), log_path);
            std::cout << "TOOL: BUILD rc=" << rc << " log=" << log_path.string() << "\n";
            continue;
        }

        if (line.rfind("TEST:", 0) == 0) {
            // TEST:<build_dir>:<config>
            const std::string rest = line.substr(std::strlen("TEST:"));
            auto parts = split(rest, ':');
            if (parts.size() != 2) {
                std::cout << "TOOL: TEST rejected (bad format)\n";
                continue;
            }
            const std::string build_dir_in = parts[0];
            const std::string config = parts[1];

            fs::path build_dir = fs::path(build_dir_in);
            if (build_dir.is_relative()) build_dir = repo_root / build_dir;
            build_dir = build_dir.lexically_normal();

            const fs::path log_path = logs_dir / ("test_" + config + ".log");

            std::ostringstream cmd;
            cmd << "cd \"" << build_dir.string() << "\" && ctest -C \"" << config << "\" --output-on-failure";
            std::cout << "TOOL: TEST start\n";
            const int rc = run_command_capture_to_file(cmd.str(), log_path);
            std::cout << "TOOL: TEST rc=" << rc << " log=" << log_path.string() << "\n";
            continue;
        }

        std::cout << "TOOL: unknown directive\n";
    }

    std::cout << "TOOL: done\n";
    return 0;
}
