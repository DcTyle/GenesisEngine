#pragma once

#include <string>
#include <map>
#include <vector>

#include "ew_kv_params.hpp"

namespace ew {

struct CliArgsKV {
    // normalized key -> raw value (UTF-8 as provided; key is ASCII-lowered).
    // Deterministic ordered map. Avoids hash-based containers.
    std::map<std::string, std::string> kv;
};

// Parses args supporting both:
//  - --key value
//  - key=value
// Keys are normalized by stripping leading '-' and ASCII-lowering.
// Returns false only on malformed forms (e.g., '--key' missing value).
inline bool ew_cli_parse_kv_ascii(int argc, char** argv, CliArgsKV& out) {
    out.kv.clear();
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i] ? std::string(argv[i]) : std::string();
        if (a.empty()) continue;

        if (a.rfind("--", 0) == 0 || a.rfind("-", 0) == 0) {
            // --key value form
            while (!a.empty() && a[0] == '-') a.erase(a.begin());
            ew_ascii_lower_inplace(a);
            if (a.empty()) continue;
            if (i + 1 >= argc) return false;
            std::string v = argv[i + 1] ? std::string(argv[i + 1]) : std::string();
            out.kv[a] = v;
            ++i;
            continue;
        }

        // key=value form
        std::string_view k, v;
        if (!ew_split_kv_token_ascii(a, k, v)) continue;
        std::string kk(k);
        ew_ascii_lower_inplace(kk);
        out.kv[kk] = std::string(v);
    }
    return true;
}

inline bool ew_cli_get_str(const CliArgsKV& kv, const char* key_ascii, std::string& out) {
    if (!key_ascii) return false;
    std::string k(key_ascii);
    ew_ascii_lower_inplace(k);
    auto it = kv.kv.find(k);
    if (it == kv.kv.end()) return false;
    out = it->second;
    return true;
}

inline bool ew_cli_get_u32(const CliArgsKV& kv, const char* key_ascii, uint32_t& out) {
    std::string s;
    if (!ew_cli_get_str(kv, key_ascii, s)) return false;
    return ew_parse_u32_ascii(s, out);
}

inline bool ew_cli_get_u64(const CliArgsKV& kv, const char* key_ascii, uint64_t& out) {
    std::string s;
    if (!ew_cli_get_str(kv, key_ascii, s)) return false;
    return ew_parse_u64_ascii(s, out);
}

inline bool ew_cli_get_i64(const CliArgsKV& kv, const char* key_ascii, int64_t& out) {
    std::string s;
    if (!ew_cli_get_str(kv, key_ascii, s)) return false;
    return ew_parse_i64_ascii(s, out);
}

inline bool ew_cli_get_bool(const CliArgsKV& kv, const char* key_ascii, bool& out) {
    std::string s;
    if (!ew_cli_get_str(kv, key_ascii, s)) return false;
    return ew_parse_bool_ascii(s, out);
}

} // namespace ew
