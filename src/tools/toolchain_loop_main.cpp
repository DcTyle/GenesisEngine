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


static bool write_text_file_exact(const fs::path &abs_path, const std::string &payload) {
    if (!ensure_parent_dir(abs_path)) return false;
    std::ofstream f(abs_path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(payload.data(), (std::streamsize)payload.size());
    return true;
}

static bool read_text_file_exact(const fs::path& abs_path, std::string& out) {
    out.clear();
    std::ifstream f(abs_path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool find_unique_anchor_line(const std::string& payload, const std::string& anchor_name, size_t& out_pos) {
    const std::string a1 = "// EW_ANCHOR:" + anchor_name;
    const std::string a2 = "# EW_ANCHOR:" + anchor_name;
    const std::string a3 = "<!-- EW_ANCHOR:" + anchor_name + " -->";
    size_t found = std::string::npos;
    int count = 0;
    auto take = [&](const std::string& pat) {
        size_t p = payload.find(pat);
        if (p != std::string::npos) { found = (found == std::string::npos) ? p : std::min(found, p); ++count; }
        if (p != std::string::npos && payload.find(pat, p + pat.size()) != std::string::npos) count = 99;
    };
    take(a1); take(a2); take(a3);
    if (count != 1) return false;
    out_pos = found;
    return true;
}

static bool find_unique_exact_substr(const std::string& payload, const std::string& needle, size_t& out_pos) {
    if (needle.empty()) return false;
    size_t p = payload.find(needle);
    if (p == std::string::npos) return false;
    if (payload.find(needle, p + needle.size()) != std::string::npos) return false;
    out_pos = p;
    return true;
}

static bool apply_patch_in_memory(std::string& out, const std::string& op, const std::string& arg_a, const std::string& arg_b, const std::string& text) {
    auto ensure_nl = [](std::string& s){ if (!s.empty() && s.back() != '\n') s.push_back('\n'); };
    if (op == "APPEND") {
        ensure_nl(out);
        out += text;
        ensure_nl(out);
        return true;
    }
    if (op == "INSERT_AFTER") {
        size_t pos = std::string::npos;
        if (!find_unique_anchor_line(out, arg_a, pos)) return false;
        size_t eol = out.find('\n', pos);
        if (eol == std::string::npos) eol = out.size(); else eol += 1;
        std::string insert = text;
        if (!insert.empty() && insert.back() != '\n') insert.push_back('\n');
        out.insert(eol, insert);
        return true;
    }
    if (op == "REPLACE_BETWEEN" || op == "DELETE_BETWEEN") {
        size_t a = std::string::npos, b = std::string::npos;
        if (!find_unique_anchor_line(out, arg_a, a)) return false;
        if (!find_unique_anchor_line(out, arg_b, b)) return false;
        if (b <= a) return false;
        size_t a_eol = out.find('\n', a);
        if (a_eol == std::string::npos) return false;
        a_eol += 1;
        size_t b_sol = out.rfind('\n', b);
        if (b_sol == std::string::npos) b_sol = 0; else b_sol += 1;
        if (b_sol < a_eol) b_sol = a_eol;
        std::string repl;
        if (op == "REPLACE_BETWEEN") {
            repl = text;
            if (!repl.empty() && repl.back() != '\n') repl.push_back('\n');
        }
        out.replace(a_eol, b_sol - a_eol, repl);
        return true;
    }
    if (op == "REPLACE_EXACT" || op == "DELETE_EXACT" || op == "INSERT_BEFORE_EXACT" || op == "INSERT_AFTER_EXACT") {
        size_t pos = std::string::npos;
        if (!find_unique_exact_substr(out, arg_a, pos)) return false;
        if (op == "REPLACE_EXACT") { out.replace(pos, arg_a.size(), text); return true; }
        if (op == "DELETE_EXACT") { out.erase(pos, arg_a.size()); return true; }
        std::string insert = text;
        if (!insert.empty() && insert.back() != '\n' && !arg_a.empty() && arg_a.front() != '\n') insert.push_back('\n');
        if (op == "INSERT_BEFORE_EXACT") out.insert(pos, insert);
        else out.insert(pos + arg_a.size(), insert);
        return true;
    }
    return false;
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
    std::cout << "  PATCH:<rel_file>:INSERT_AFTER:<anchor>:<text>\n";
    std::cout << "  PATCH:<rel_file>:REPLACE_BETWEEN:<anchor_a>:<anchor_b>:<text>\n";
    std::cout << "  PATCH:<rel_file>:DELETE_BETWEEN:<anchor_a>:<anchor_b>\n";
    std::cout << "  PATCH:<rel_file>:REPLACE_EXACT:<needle>:WITH:<text>\n";
    std::cout << "  PATCH:<rel_file>:DELETE_EXACT:<needle>\n";
    std::cout << "  PATCH:<rel_file>:INSERT_BEFORE_EXACT:<needle>:TEXT:<text>\n";
    std::cout << "  PATCH:<rel_file>:INSERT_AFTER_EXACT:<needle>:TEXT:<text>\n";
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
            const std::string rest = line.substr(std::strlen("PATCH:"));
            auto parts = split(rest, ':');
            if (parts.size() < 2) {
                std::cout << "TOOL: PATCH rejected (bad format)\n";
                continue;
            }

            const std::string rel_file = parts[0];
            const std::string op = parts[1];
            std::string arg_a, arg_b, payload;
            if (op == "APPEND") {
                for (size_t i = 2; i < parts.size(); ++i) { if (i > 2) payload.push_back(':'); payload += parts[i]; }
            } else if (op == "INSERT_AFTER") {
                if (parts.size() < 4) { std::cout << "TOOL: PATCH rejected (bad format)\n"; continue; }
                arg_a = parts[2];
                for (size_t i = 3; i < parts.size(); ++i) { if (i > 3) payload.push_back(':'); payload += parts[i]; }
            } else if (op == "REPLACE_BETWEEN") {
                if (parts.size() < 5) { std::cout << "TOOL: PATCH rejected (bad format)\n"; continue; }
                arg_a = parts[2]; arg_b = parts[3];
                for (size_t i = 4; i < parts.size(); ++i) { if (i > 4) payload.push_back(':'); payload += parts[i]; }
            } else if (op == "DELETE_BETWEEN") {
                if (parts.size() != 4) { std::cout << "TOOL: PATCH rejected (bad format)\n"; continue; }
                arg_a = parts[2]; arg_b = parts[3];
            } else if (op == "REPLACE_EXACT") {
                const size_t p = rest.find(":REPLACE_EXACT:");
                const size_t m = rest.find(":WITH:");
                if (p == std::string::npos || m == std::string::npos || m <= p + std::strlen(":REPLACE_EXACT:")) { std::cout << "TOOL: PATCH rejected (bad format)\n"; continue; }
                arg_a = rest.substr(p + std::strlen(":REPLACE_EXACT:"), m - (p + std::strlen(":REPLACE_EXACT:")));
                payload = rest.substr(m + std::strlen(":WITH:"));
            } else if (op == "DELETE_EXACT") {
                const size_t p = rest.find(":DELETE_EXACT:");
                if (p == std::string::npos) { std::cout << "TOOL: PATCH rejected (bad format)\n"; continue; }
                arg_a = rest.substr(p + std::strlen(":DELETE_EXACT:"));
            } else if (op == "INSERT_BEFORE_EXACT") {
                const size_t p = rest.find(":INSERT_BEFORE_EXACT:");
                const size_t m = rest.find(":TEXT:");
                if (p == std::string::npos || m == std::string::npos || m <= p + std::strlen(":INSERT_BEFORE_EXACT:")) { std::cout << "TOOL: PATCH rejected (bad format)\n"; continue; }
                arg_a = rest.substr(p + std::strlen(":INSERT_BEFORE_EXACT:"), m - (p + std::strlen(":INSERT_BEFORE_EXACT:")));
                payload = rest.substr(m + std::strlen(":TEXT:"));
            } else if (op == "INSERT_AFTER_EXACT") {
                const size_t p = rest.find(":INSERT_AFTER_EXACT:");
                const size_t m = rest.find(":TEXT:");
                if (p == std::string::npos || m == std::string::npos || m <= p + std::strlen(":INSERT_AFTER_EXACT:")) { std::cout << "TOOL: PATCH rejected (bad format)\n"; continue; }
                arg_a = rest.substr(p + std::strlen(":INSERT_AFTER_EXACT:"), m - (p + std::strlen(":INSERT_AFTER_EXACT:")));
                payload = rest.substr(m + std::strlen(":TEXT:"));
            } else {
                std::cout << "TOOL: PATCH rejected (unsupported op)\n";
                continue;
            }

            fs::path abs_file = repo_root / rel_file;
            abs_file = fs::weakly_canonical(abs_file, ec);
            if (ec) { ec.clear(); abs_file = (repo_root / rel_file).lexically_normal(); }
            const std::string abs_str = abs_file.string();
            const std::string root_str = repo_root.string();
            if (abs_str.rfind(root_str, 0) != 0) {
                std::cout << "TOOL: PATCH rejected (path escapes repo_root)\n";
                continue;
            }

            std::string file_payload;
            if (!read_text_file_exact(abs_file, file_payload) && op != "APPEND") {
                std::cout << "TOOL: PATCH rejected (target missing)\n";
                continue;
            }
            if (op == "APPEND") {
                if (!append_text_file(abs_file, payload)) std::cout << "TOOL: PATCH failed\n";
                else std::cout << "TOOL: PATCH ok " << rel_file << "\n";
                continue;
            }
            if (!apply_patch_in_memory(file_payload, op, arg_a, arg_b, payload)) {
                std::cout << "TOOL: PATCH rejected (no unique target)\n";
                continue;
            }
            if (!write_text_file_exact(abs_file, file_payload)) std::cout << "TOOL: PATCH failed\n";
            else std::cout << "TOOL: PATCH ok " << rel_file << "\n";
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
