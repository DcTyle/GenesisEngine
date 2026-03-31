#include "GE_repo_reader.hpp"

#include "GE_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

static bool ew_is_ascii_printable(char c) {
    const unsigned char uc = (unsigned char)c;
    return (uc == '\n' || uc == '\r' || uc == '\t' || (uc >= 32u && uc <= 126u));
}

static std::string ew_sanitize_ascii(const std::string& in, uint32_t cap_bytes_u32) {
    const size_t cap = (in.size() < (size_t)cap_bytes_u32) ? in.size() : (size_t)cap_bytes_u32;
    std::string out;
    out.reserve(cap);
    for (size_t i = 0; i < cap; ++i) {
        const char ch = in[i];
        out.push_back(ew_is_ascii_printable(ch) ? ch : ' ');
    }
    return out;
}

static bool ew_has_ext_ascii(const std::string& p, const char* ext) {
    const size_t n = p.size();
    const size_t m = std::strlen(ext);
    if (n < m) return false;
    for (size_t i = 0; i < m; ++i) {
        char a = p[n - m + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static fs::path ew_repo_reader_root_path() {
    std::error_code ec;
    fs::path probe = fs::current_path(ec);
    if (ec || probe.empty()) return fs::path();

    while (!probe.empty()) {
        const bool has_cmake = fs::exists(probe / "CMakeLists.txt", ec);
        ec.clear();
        const bool has_include = fs::exists(probe / "include", ec);
        ec.clear();
        const bool has_research = fs::exists(probe / "ResearchConfinement", ec);
        ec.clear();
        if (has_cmake && (has_include || has_research)) return probe;

        const fs::path parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }

    ec.clear();
    return fs::current_path(ec);
}

static bool ew_repo_file_allowed_rel(const std::string& rel_ascii) {
    const auto starts = [&](const char* prefix) -> bool {
        const size_t n = std::strlen(prefix);
        return rel_ascii.size() >= n && rel_ascii.compare(0, n, prefix) == 0;
    };

    const bool in_allowed_scope =
        starts("include/") ||
        starts("src/") ||
        starts("vulkan_app/include/") ||
        starts("vulkan_app/src/") ||
        starts("shaders/") ||
        starts("cmake/") ||
        starts("docs/") ||
        starts("ResearchConfinement/") ||
        starts("ge_canon/include/") ||
        starts("ge_canon/src/") ||
        starts("ge_canon/vulkan_app/include/") ||
        starts("ge_canon/vulkan_app/src/") ||
        starts("ge_canon/docs/spec_uploads/") ||
        starts("ge_canon/ResearchConfinement/") ||
        starts("scripts/");
    if (!in_allowed_scope) return false;

    if (rel_ascii.find("/build/") != std::string::npos) return false;
    if (rel_ascii.find("/out/") != std::string::npos) return false;
    if (rel_ascii.find("/.git/") != std::string::npos) return false;
    if (rel_ascii.find("/external/") != std::string::npos) return false;
    if (rel_ascii.find("/third_party/") != std::string::npos) return false;
    if (rel_ascii.find("/ThirdParty/") != std::string::npos) return false;

    const char* exts[] = {
        ".cpp", ".c", ".hpp", ".h", ".inl", ".mm",
        ".txt", ".md", ".json", ".cmake", ".glsl",
        ".comp", ".vert", ".frag", ".py", ".ewcfg"
    };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
        if (ew_has_ext_ascii(rel_ascii, exts[i])) return true;
    }
    if (rel_ascii.size() >= 13u &&
        rel_ascii.compare(rel_ascii.size() - 13u, 13u, "CMakeLists.txt") == 0) {
        return true;
    }
    return false;
}

static bool ew_read_file_ascii_bounded(const fs::path& abs_path, uint32_t cap_bytes_u32, std::string& out) {
    out.clear();
    std::ifstream f(abs_path, std::ios::binary);
    if (!f.good()) return false;
    out.resize((size_t)cap_bytes_u32);
    f.read(&out[0], (std::streamsize)cap_bytes_u32);
    const std::streamsize got = f.gcount();
    if (got <= 0) {
        out.clear();
        return false;
    }
    out.resize((size_t)got);
    return true;
}

void GE_RepoReader::scan_repo_root() {
    files_rel_ascii.clear();
    repo_root_ascii.clear();
    cursor_u32 = 0;
    next_seq_u64 = 1u;
    scanned = false;

    std::error_code ec;
    fs::path root = ew_repo_reader_root_path();
    if (root.empty()) return;
    repo_root_ascii = root.generic_string();

    std::vector<std::string> rels;
    rels.reserve(4096u);

    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;

        const fs::path abs = it->path();
        fs::path rel = fs::relative(abs, root, ec);
        if (ec) continue;
        const std::string rel_s = rel.generic_string();

        bool ascii_only = true;
        for (char ch : rel_s) {
            if ((unsigned char)ch > 127u) {
                ascii_only = false;
                break;
            }
        }
        if (!ascii_only) continue;
        if (!ew_repo_file_allowed_rel(rel_s)) continue;

        rels.push_back(rel_s);
        if (rels.size() >= 8192u) break;
    }

    std::sort(rels.begin(), rels.end());
    rels.erase(std::unique(rels.begin(), rels.end()), rels.end());
    files_rel_ascii = std::move(rels);
    scanned = true;
}

void GE_RepoReader::tick(SubstrateManager* sm, uint32_t files_per_tick_u32, uint32_t bytes_per_file_u32) {
    if (!sm || !enabled) return;
    if (!scanned) scan_repo_root();
    if (files_rel_ascii.empty() || repo_root_ascii.empty()) return;

    if (files_per_tick_u32 == 0u) files_per_tick_u32 = 1u;
    if (files_per_tick_u32 > 16u) files_per_tick_u32 = 16u;
    if (bytes_per_file_u32 < 512u) bytes_per_file_u32 = 512u;
    if (bytes_per_file_u32 > (64u * 1024u)) bytes_per_file_u32 = (64u * 1024u);

    const fs::path root = fs::path(repo_root_ascii);
    const uint32_t causal_tag_u32 = 0x52455031u; // 'REP1'

    for (uint32_t n = 0u; n < files_per_tick_u32; ++n) {
        if (cursor_u32 >= (uint32_t)files_rel_ascii.size()) cursor_u32 = 0u;
        const std::string& rel = files_rel_ascii[cursor_u32++];
        const fs::path abs = (root / fs::path(rel)).lexically_normal();

        std::string bytes;
        if (!ew_read_file_ascii_bounded(abs, bytes_per_file_u32, bytes)) continue;
        const std::string snippet = ew_sanitize_ascii(bytes, bytes_per_file_u32);

        const uint64_t rid = (sm->canonical_tick_u64() << 32) ^ (next_seq_u64++);
        sm->crawler.enqueue_observation_utf8(
            rid,
            0u,
            0u,
            0u,
            3u,
            0u,
            1u,
            causal_tag_u32,
            std::string("repo"),
            std::string("repo://") + rel,
            snippet);
    }
}

std::string GE_RepoReader::status_line(uint32_t files_per_tick_u32, uint32_t bytes_per_file_u32) const {
    std::string s = "REPO_READER_STATUS enabled=";
    s += enabled ? "1" : "0";
    s += " scanned=";
    s += scanned ? "1" : "0";
    s += " files=" + std::to_string((unsigned long long)files_rel_ascii.size());
    s += " cursor=" + std::to_string((unsigned long long)cursor_u32);
    s += " files_per_tick=" + std::to_string((unsigned long long)files_per_tick_u32);
    s += " bytes_per_file=" + std::to_string((unsigned long long)bytes_per_file_u32);
    if (!repo_root_ascii.empty()) {
        s += " root=" + repo_root_ascii;
    }
    return s;
}
