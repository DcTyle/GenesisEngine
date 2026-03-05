#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ew {

// ASCII-only helpers. No locale transforms.

static inline bool ew_is_space_ascii(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static inline char ew_ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static inline void ew_ascii_lower_inplace(std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) s[i] = ew_ascii_lower(s[i]);
}

static inline std::string_view ew_trim_ascii(std::string_view sv) {
    size_t a = 0;
    size_t b = sv.size();
    while (a < b && ew_is_space_ascii(sv[a])) ++a;
    while (b > a && ew_is_space_ascii(sv[b - 1])) --b;
    return sv.substr(a, b - a);
}

static inline bool ew_split_kv_token_ascii(std::string_view tok, std::string_view& out_k, std::string_view& out_v) {
    const size_t eq = tok.find('=');
    if (eq == std::string_view::npos) return false;
    out_k = ew_trim_ascii(tok.substr(0, eq));
    out_v = ew_trim_ascii(tok.substr(eq + 1));
    return !out_k.empty();
}

static inline bool ew_parse_u32_ascii(std::string_view sv, uint32_t& out) {
    sv = ew_trim_ascii(sv);
    if (sv.empty()) return false;
    uint64_t v = 0;
    for (char c : sv) {
        if (c < '0' || c > '9') return false;
        v = v * 10u + (uint64_t)(c - '0');
        if (v > 0xFFFFFFFFull) return false;
    }
    out = (uint32_t)v;
    return true;
}

static inline bool ew_parse_u64_ascii(std::string_view sv, uint64_t& out) {
    sv = ew_trim_ascii(sv);
    if (sv.empty()) return false;
    uint64_t v = 0;
    for (char c : sv) {
        if (c < '0' || c > '9') return false;
        const uint64_t nv = v * 10u + (uint64_t)(c - '0');
        if (nv < v) return false;
        v = nv;
    }
    out = v;
    return true;
}

static inline bool ew_parse_i64_ascii(std::string_view sv, int64_t& out) {
    sv = ew_trim_ascii(sv);
    if (sv.empty()) return false;
    bool neg = false;
    size_t i = 0;
    if (sv[0] == '-') { neg = true; i = 1; }
    uint64_t mag = 0;
    for (; i < sv.size(); ++i) {
        char c = sv[i];
        if (c < '0' || c > '9') return false;
        const uint64_t nv = mag * 10u + (uint64_t)(c - '0');
        if (nv < mag) return false;
        mag = nv;
    }
    if (!neg) {
        if (mag > (uint64_t)INT64_MAX) return false;
        out = (int64_t)mag;
        return true;
    }
    // allow INT64_MIN magnitude
    if (mag > (uint64_t)INT64_MAX + 1ull) return false;
    if (mag == (uint64_t)INT64_MAX + 1ull) out = INT64_MIN;
    else out = -(int64_t)mag;
    return true;
}

static inline bool ew_parse_bool_ascii(std::string_view sv, bool& out) {
    sv = ew_trim_ascii(sv);
    if (sv.empty()) return false;
    std::string tmp(sv);
    ew_ascii_lower_inplace(tmp);
    if (tmp == "1" || tmp == "true" || tmp == "on" || tmp == "yes") { out = true; return true; }
    if (tmp == "0" || tmp == "false" || tmp == "off" || tmp == "no") { out = false; return true; }
    return false;
}

static inline uint32_t ew_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// Shell-like splitter: supports double quotes and inline comments (#) outside quotes.
static inline void ew_split_shell_ascii(const std::string& line_utf8, std::vector<std::string>& out_tokens) {
    out_tokens.clear();
    std::string cur;
    bool in_q = false;

    for (size_t i = 0; i < line_utf8.size(); ++i) {
        const char c = line_utf8[i];
        if (!in_q && c == '#') break;
        if (c == '"') { in_q = !in_q; continue; }

        if (!in_q && ew_is_space_ascii(c)) {
            if (!cur.empty()) { out_tokens.push_back(cur); cur.clear(); }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out_tokens.push_back(cur);
}

} // namespace ew
