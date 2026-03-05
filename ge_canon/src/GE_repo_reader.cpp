#include "GE_repo_reader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

#include "GE_runtime.hpp"

namespace fs = std::filesystem;

static bool ew_is_ascii_printable(char c) {
    unsigned char uc = (unsigned char)c;
    return (uc == '\n' || uc == '\r' || uc == '\t' || (uc >= 32 && uc <= 126));
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

static bool ew_repo_file_allowed_rel(const std::string& rel_ascii) {
    // Strictly bounded include list to avoid ingesting build artifacts or large binaries.
    // Determinism first: only known source-ish paths.
    const auto starts = [&](const char* prefix)->bool{
        const size_t n = std::strlen(prefix);
        return rel_ascii.size() >= n && rel_ascii.compare(0, n, prefix) == 0;
    };
    if (!(starts("include/") || starts("src/") || starts("shaders/") || starts("cmake/"))) return false;

    // Exclude obvious non-source.
    if (rel_ascii.find("/build/") != std::string::npos) return false;
    if (rel_ascii.find("/.git/") != std::string::npos) return false;
    if (rel_ascii.find("/external/") != std::string::npos) return false;
    if (rel_ascii.find("/third_party/") != std::string::npos) return false;

    // Allowed extensions (ASCII-only filter; repo reader is for text).
    const char* exts[] = {
        ".cpp", ".c", ".hpp", ".h", ".inl", ".mm",
        ".txt", ".md", ".cmake", ".glsl", ".hlsl",
        "CMakeLists.txt"
    };
    for (size_t i = 0; i < sizeof(exts)/sizeof(exts[0]); ++i) {
        const char* e = exts[i];
        if (std::strcmp(e, "CMakeLists.txt") == 0) {
            // Exact tail match.
            if (rel_ascii.size() >= 13 && rel_ascii.compare(rel_ascii.size() - 13, 13, "CMakeLists.txt") == 0) return true;
        } else if (ew_has_ext_ascii(rel_ascii, e)) return true;
    }
    return false;
}

void GE_RepoReader::scan_repo_root() {
    files_rel_ascii.clear();
    cursor_u32 = 0;
    next_seq_u64 = 1;
    scanned = false;

    std::error_code ec;
    fs::path root = fs::current_path(ec);
    if (ec || root.empty()) return;

    // Collect candidate rel paths, then sort.
    std::vector<std::string> rels;
    rels.reserve(2048);

    // Deterministic walk: filesystem iteration order is unspecified, so we collect then sort.
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::path p = it->path();
        if (!it->is_regular_file(ec)) continue;

        fs::path rel = fs::relative(p, root, ec);
        if (ec) continue;
        std::string rel_s = rel.generic_string();

        // Force ASCII-only rel path (fail-closed by skipping if non-ascii).
        bool ok = true;
        for (char ch : rel_s) { if ((unsigned char)ch > 127u) { ok = false; break; } }
        if (!ok) continue;

        if (!ew_repo_file_allowed_rel(rel_s)) continue;
        rels.push_back(rel_s);
        if (rels.size() >= 4096u) break; // hard cap
    }

    std::sort(rels.begin(), rels.end());
    // Deduplicate stable.
    rels.erase(std::unique(rels.begin(), rels.end()), rels.end());
    files_rel_ascii = std::move(rels);
    scanned = true;
}

static bool ew_read_file_ascii_bounded(const fs::path& abs_path, uint32_t cap_bytes_u32, std::string& out) {
    out.clear();
    std::ifstream f(abs_path, std::ios::binary);
    if (!f.good()) return false;
    out.resize((size_t)cap_bytes_u32);
    f.read(&out[0], (std::streamsize)cap_bytes_u32);
    const std::streamsize got = f.gcount();
    if (got <= 0) { out.clear(); return false; }
    out.resize((size_t)got);
    return true;
}

void GE_RepoReader::tick(SubstrateManager* sm, uint32_t files_per_tick_u32, uint32_t bytes_per_file_u32) {
    if (sm == nullptr) return;
    if (!enabled) return;
    if (!scanned) scan_repo_root();
    if (files_rel_ascii.empty()) return;
    if (files_per_tick_u32 == 0u) files_per_tick_u32 = 1u;
    if (files_per_tick_u32 > 16u) files_per_tick_u32 = 16u;
    if (bytes_per_file_u32 < 512u) bytes_per_file_u32 = 512u;
    if (bytes_per_file_u32 > (64u * 1024u)) bytes_per_file_u32 = (64u * 1024u);

    std::error_code ec;
    fs::path root = fs::current_path(ec);
    if (ec || root.empty()) return;

    const uint32_t EW_CAUSAL_TAG_REPO_READER = 0x52455031U; // 'REP1'

    for (uint32_t n = 0; n < files_per_tick_u32; ++n) {
        if (cursor_u32 >= (uint32_t)files_rel_ascii.size()) cursor_u32 = 0;
        const std::string& rel = files_rel_ascii[cursor_u32++];
        fs::path abs = (root / fs::path(rel)).lexically_normal();

        // Read a bounded chunk and sanitize.
        std::string bytes;
        if (!ew_read_file_ascii_bounded(abs, bytes_per_file_u32, bytes)) continue;
        std::string snippet = ew_sanitize_ascii(bytes, bytes_per_file_u32);

        // Observation identity is deterministic across replay:
        // request_id uses tick and deterministic seq.
        const uint64_t rid = (sm->canonical_tick_u64() << 32) ^ (next_seq_u64++);
        const std::string domain_ascii = "repo";
        const std::string url_ascii = std::string("repo://") + rel;

        sm->crawler.enqueue_observation_utf8(
            rid,
            0u, // domain_anchor_id_u32 (internal)
            0u, // crawler_anchor_id_u32 (internal)
            0u, // context_anchor_id_u32
            3u, // segment_u32 (repo)
            0u,
            1u,
            EW_CAUSAL_TAG_REPO_READER,
            domain_ascii,
            url_ascii,
            snippet
        );
    }
}

std::string GE_RepoReader::status_line() const {
    std::string s = "repo_reader:";
    s += enabled ? "on" : "off";
    s += " scanned=";
    s += scanned ? "1" : "0";
    s += " files=" + std::to_string((unsigned long long)files_rel_ascii.size());
    s += " cursor=" + std::to_string((unsigned long long)cursor_u32);
    return s;
}
