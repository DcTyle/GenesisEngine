#include "coherence_gate.hpp"

#include <cstddef>

static constexpr uint16_t Q15_ONE = 32768;

// UTF-8 gate: accept well-formed UTF-8 and allow common whitespace.
// Deny C0 control characters (except \n, \r, \t) and DEL to prevent invisible
// control payloads from entering artifacts.
static bool ew_is_allowed_control_byte(unsigned char b) {
    return (b == (unsigned char)'\n' || b == (unsigned char)'\r' || b == (unsigned char)'\t');
}

static bool ew_utf8_validate_and_screen(const std::string& s) {
    const unsigned char* p = (const unsigned char*)s.data();
    const size_t n = s.size();
    size_t i = 0;

    while (i < n) {
        unsigned char b0 = p[i];

        // Fast-path ASCII.
        if (b0 < 0x80) {
            if (b0 < 0x20 && !ew_is_allowed_control_byte(b0)) return false;
            if (b0 == 0x7F) return false; // DEL
            i += 1;
            continue;
        }

        // Multi-byte sequences. Validate structure and avoid overlongs/surrogates/out-of-range.
        if ((b0 & 0xE0) == 0xC0) {
            if (i + 1 >= n) return false;
            unsigned char b1 = p[i + 1];
            if ((b1 & 0xC0) != 0x80) return false;
            if (b0 < 0xC2) return false; // overlong
            i += 2;
            continue;
        }

        if ((b0 & 0xF0) == 0xE0) {
            if (i + 2 >= n) return false;
            unsigned char b1 = p[i + 1];
            unsigned char b2 = p[i + 2];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return false;
            if (b0 == 0xE0 && b1 < 0xA0) return false;  // overlong
            if (b0 == 0xED && b1 >= 0xA0) return false; // surrogate range
            i += 3;
            continue;
        }

        if ((b0 & 0xF8) == 0xF0) {
            if (i + 3 >= n) return false;
            unsigned char b1 = p[i + 1];
            unsigned char b2 = p[i + 2];
            unsigned char b3 = p[i + 3];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return false;
            if (b0 == 0xF0 && b1 < 0x90) return false;  // overlong
            if (b0 == 0xF4 && b1 > 0x8F) return false;  // > U+10FFFF
            if (b0 > 0xF4) return false;
            i += 4;
            continue;
        }

        // Invalid leading byte.
        return false;
    }

    return true;
}

bool EwCoherenceGate::rel_path_is_safe(const std::string& rel_path) {
    if (rel_path.empty()) return false;
    if (rel_path[0] == '/' || rel_path[0] == '\\') return false;
    if (rel_path.size() >= 2) {
        const char c1 = rel_path[0];
        const char c2 = rel_path[1];
        if (((c1 >= 'A' && c1 <= 'Z') || (c1 >= 'a' && c1 <= 'z')) && c2 == ':') return false;
    }
    for (size_t i = 0; i + 1 < rel_path.size(); ++i) {
        if (rel_path[i] == '.' && rel_path[i + 1] == '.') return false;
    }
    return true;
}

bool EwCoherenceGate::braces_balanced_cpp_like(const std::string& s) {
    int32_t braces = 0;
    int32_t parens = 0;
    int32_t brackets = 0;
    for (char c : s) {
        if (c == '{') braces++;
        else if (c == '}') braces--;
        else if (c == '(') parens++;
        else if (c == ')') parens--;
        else if (c == '[') brackets++;
        else if (c == ']') brackets--;
        if (braces < 0 || parens < 0 || brackets < 0) return false;
    }
    return braces == 0 && parens == 0 && brackets == 0;
}

EwCoherenceResult EwCoherenceGate::validate_artifact(
    const std::string& rel_path,
    uint32_t kind_u32,
    const std::string& payload
) {
    EwCoherenceResult r{};

    if (!rel_path_is_safe(rel_path)) {
        r.denial_code_u32 = 1001;
        r.coherence_q15 = 0;
        r.commit_ready = false;
        return r;
    }

    if (!ew_utf8_validate_and_screen(payload)) {
        r.denial_code_u32 = 1002;
        r.coherence_q15 = 0;
        r.commit_ready = false;
        return r;
    }

    if (kind_u32 == 2u || kind_u32 == 3u) {
        if (!braces_balanced_cpp_like(payload)) {
            r.denial_code_u32 = 1101;
            r.coherence_q15 = 0;
            r.commit_ready = false;
            return r;
        }
    }

    r.coherence_q15 = Q15_ONE;
    r.commit_ready = true;
    r.denial_code_u32 = 0;
    return r;
}
