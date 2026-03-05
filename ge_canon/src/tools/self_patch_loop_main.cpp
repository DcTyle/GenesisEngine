#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ew_cli_args.hpp"

static std::string shell_join_ascii(const std::vector<std::string>& toks) {
    std::string out;
    for (size_t i = 0; i < toks.size(); ++i) {
        if (i) out.push_back(' ');
        const std::string& t = toks[i];
        bool need_q = false;
        for (char c : t) {
            if (c == ' ' || c == '\t' || c == '"') { need_q = true; break; }
        }
        if (!need_q) {
            out += t;
        } else {
            out.push_back('"');
            for (char c : t) {
                if (c == '"' || c == '\\') out.push_back('\\');
                out.push_back(c);
            }
            out.push_back('"');
        }
    }
    return out;
}

namespace fs = std::filesystem;

static bool read_file_bytes(const fs::path& p, std::vector<uint8_t>& out, size_t cap_bytes) {
    out.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    if ((size_t)n > cap_bytes) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    if (!out.empty()) f.read((char*)out.data(), (std::streamsize)out.size());
    return true;
}

static bool contains_ascii_case_insensitive(const std::vector<uint8_t>& b, const char* needle) {
    const size_t n = std::strlen(needle);
    if (n == 0 || b.size() < n) return false;
    for (size_t i = 0; i + n <= b.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < n; ++j) {
            unsigned char c = b[i + j];
            unsigned char t = (unsigned char)needle[j];
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
            if (t >= 'A' && t <= 'Z') t = (unsigned char)(t - 'A' + 'a');
            if (c != t) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

struct GateFail {
    std::string path;
    std::string reason;
};

static void scan_static_gates(const fs::path& proposal_dir, std::vector<GateFail>& fails) {
    fails.clear();
    std::vector<uint8_t> bytes;

    const char* banned[] = {
        "todo",
        "tbd",
        "fixme",
        "not implemented",
        "return false; //",
        "return 0; //",
        "win32",
    };

    for (auto it = fs::recursive_directory_iterator(proposal_dir); it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) continue;
        const fs::path p = it->path();

        // Skip huge binaries; policy gates apply to source-like files.
        const auto sz = it->file_size();
        if (sz > (1u << 21)) continue; // 2MB

        if (!read_file_bytes(p, bytes, (size_t)(1u << 21))) continue;

        // Fail closed on explicit banned terms.
        for (const char* w : banned) {
            if (contains_ascii_case_insensitive(bytes, w)) {
                GateFail f;
                f.path = p.u8string();
                f.reason = std::string("static_gate_banned_token:") + w;
                fails.push_back(f);
                break;
            }
        }
    }

    // Deterministic sort.
    std::sort(fails.begin(), fails.end(), [](const GateFail& a, const GateFail& b) {
        if (a.path != b.path) return a.path < b.path;
        return a.reason < b.reason;
    });
}

static bool read_lines(const fs::path& p, std::vector<std::string>& out_lines) {
    out_lines.clear();
    std::ifstream f(p);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        // Trim CR.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Use shared dialect rules: shell-split with inline # comments.
        std::vector<std::string> toks;
        ew::ew_split_shell_ascii(line, toks);
        if (toks.empty()) continue;

        // Expand k=v into --k v for consistency.
        std::vector<std::string> norm;
        norm.reserve(toks.size() * 2);
        norm.push_back(toks[0]);
        for (size_t ti = 1; ti < toks.size(); ++ti) {
            std::string k, v;
            if (!toks[ti].empty() && toks[ti][0] != '-' && ew::ew_split_kv_token_ascii(toks[ti], k, v)) {
                const std::string nk = ew::ew_cli_normalize_key_ascii(k);
                norm.push_back(std::string("--") + nk);
                norm.push_back(v);
            } else {
                norm.push_back(toks[ti]);
            }
        }

        out_lines.push_back(shell_join_ascii(norm));
    }
    return true;
}

static int run_smoke_commands(const fs::path& proposal_dir, std::vector<std::string>& log) {
    log.clear();
    const fs::path list = proposal_dir / "smoke_commands.txt";
    std::vector<std::string> cmds;
    if (!read_lines(list, cmds)) {
        log.push_back("smoke:no_smoke_commands_file");
        return 2;
    }

    int rc = 0;
    for (size_t i = 0; i < cmds.size(); ++i) {
        const std::string& c = cmds[i];
        log.push_back(std::string("smoke:run:") + c);
        const int r = std::system(c.c_str());
        if (r != 0) {
            log.push_back(std::string("smoke:fail:code=") + std::to_string(r));
            rc = 3;
            break;
        }
        log.push_back("smoke:ok");
    }
    return rc;
}

static bool write_text_file(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write(s.data(), (std::streamsize)s.size());
    return true;
}

int main(int argc, char** argv) {
    fs::path proposal_dir;
    fs::path out_dir;

    ew::CliArgsKV args;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, args)) {
        std::cerr << "Usage: ew_self_patch_loop --proposal <dir> --out <dir>\n";
        return 1;
    }
    std::string s;
    if (ew::ew_cli_get_str(args, "proposal", s)) proposal_dir = fs::path(s);
    if (ew::ew_cli_get_str(args, "out", s)) out_dir = fs::path(s);

    if (proposal_dir.empty() || out_dir.empty()) {
        std::cerr << "Usage: ew_self_patch_loop --proposal <dir> --out <dir>\n";
        return 1;
    }

    fs::create_directories(out_dir);

    std::vector<GateFail> fails;
    scan_static_gates(proposal_dir, fails);
    std::vector<std::string> smoke_log;
    int smoke_rc = 0;

    std::string report;
    report += "SELF_PATCH_REPORT\n";
    report += "proposal=" + proposal_dir.u8string() + "\n";

    if (!fails.empty()) {
        report += "static_gate=FAIL\n";
        for (const auto& f : fails) {
            report += "fail_file=" + f.path + " reason=" + f.reason + "\n";
        }
        write_text_file(out_dir / "self_patch_report.txt", report);
        return 10;
    }
    report += "static_gate=OK\n";

    smoke_rc = run_smoke_commands(proposal_dir, smoke_log);
    if (smoke_rc != 0) {
        report += "smoke_gate=FAIL\n";
        for (const auto& l : smoke_log) report += l + "\n";
        write_text_file(out_dir / "self_patch_report.txt", report);
        return 11;
    }
    report += "smoke_gate=OK\n";
    for (const auto& l : smoke_log) report += l + "\n";

    // Bless the proposal: create a single marker file.
    const fs::path bless = out_dir / "PATCH_BLESSED.txt";
    report += "commit=OK\n";
    write_text_file(bless, "PATCH_BLESSED\n");
    write_text_file(out_dir / "self_patch_report.txt", report);

    std::cout << "PATCH_BLESSED\n";
    return 0;
}
