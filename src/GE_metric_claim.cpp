#include "GE_metric_claim.hpp"
#include "GE_metric_registry.hpp"

#include <cctype>
#include <cstring>

namespace genesis {

static inline char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static inline bool is_digit(char c) { return (c >= '0' && c <= '9'); }

static inline uint64_t pow10_u64(uint32_t e) {
    static const uint64_t k_pow10[20] = {
        1ull,
        10ull,
        100ull,
        1000ull,
        10000ull,
        100000ull,
        1000000ull,
        10000000ull,
        100000000ull,
        1000000000ull,
        10000000000ull,
        100000000000ull,
        1000000000000ull,
        10000000000000ull,
        100000000000000ull,
        1000000000000000ull,
        10000000000000000ull,
        100000000000000000ull,
        1000000000000000000ull,
        10000000000000000000ull
    };
    if (e >= 20u) return 0ull;
    return k_pow10[e];
}

static bool parse_number_to_q32_32(const std::string& s, size_t i0, size_t i1, int64_t& out_q32_32) {
    // Parses ASCII number in s[i0..i1) and writes Q32.32.
    // Supported: [-]?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?
    if (i0 >= i1) return false;

    bool neg = false;
    size_t i = i0;
    if (s[i] == '-') { neg = true; i++; }
    else if (s[i] == '+') { i++; }

    uint64_t int_part = 0ull;
    uint64_t frac_part = 0ull;
    uint32_t frac_digits = 0u;
    bool any = false;

    while (i < i1 && is_digit(s[i])) {
        any = true;
        int_part = int_part * 10ull + (uint64_t)(s[i] - '0');
        i++;
        if (int_part > 9000000000000000000ull) { /* clamp */ }
    }

    if (i < i1 && s[i] == '.') {
        i++;
        while (i < i1 && is_digit(s[i]) && frac_digits < 12u) {
            any = true;
            frac_part = frac_part * 10ull + (uint64_t)(s[i] - '0');
            frac_digits++;
            i++;
        }
        // Skip remaining frac digits but do not record them (bounded precision).
        while (i < i1 && is_digit(s[i])) i++;
    }

    if (!any) return false;

    int32_t exp10 = 0;
    if (i < i1 && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        bool exp_neg = false;
        if (i < i1 && (s[i] == '+' || s[i] == '-')) {
            exp_neg = (s[i] == '-');
            i++;
        }
        int32_t e = 0;
        while (i < i1 && is_digit(s[i]) && e < 40) {
            e = e * 10 + (int32_t)(s[i] - '0');
            i++;
        }
        exp10 = exp_neg ? -e : e;
    }

    // value = (int_part + frac_part / 10^frac_digits) * 10^exp10
    // Convert to Q32.32 using integer math.
    const uint64_t frac_den = (frac_digits == 0u) ? 1ull : pow10_u64(frac_digits);
    if (frac_den == 0ull) return false;

    __int128 num = (__int128)int_part * (__int128)frac_den + (__int128)frac_part;
    __int128 den = (__int128)frac_den;

    // Apply exp10.
    if (exp10 > 0) {
        const uint64_t p = pow10_u64((uint32_t)exp10);
        if (p == 0ull) return false;
        num *= (__int128)p;
    } else if (exp10 < 0) {
        const uint64_t p = pow10_u64((uint32_t)(-exp10));
        if (p == 0ull) return false;
        den *= (__int128)p;
    }

    // Q32.32 = (num << 32) / den
    const __int128 scaled = (num << 32);
    if (den == 0) return false;
    __int128 q = scaled / den;

    // Clamp to int64_t range.
    if (q > (__int128)INT64_MAX) q = (__int128)INT64_MAX;
    if (q < (__int128)INT64_MIN) q = (__int128)INT64_MIN;

    int64_t v = (int64_t)q;
    if (neg) v = -v;
    out_q32_32 = v;
    return true;
}

static std::string make_bounded_ascii_lower(const std::string& utf8, uint32_t cap_bytes_u32) {
    const size_t cap = (utf8.size() < (size_t)cap_bytes_u32) ? utf8.size() : (size_t)cap_bytes_u32;
    std::string s;
    s.reserve(cap);
    for (size_t i = 0; i < cap; ++i) {
        unsigned char ch = (unsigned char)utf8[i];
        if (ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 32 && ch <= 126)) {
            s.push_back(to_lower_ascii((char)ch));
        } else {
            s.push_back(' ');
        }
    }
    return s;
}

static uint32_t unit_code_from_token_ascii(const std::string& u) {
    // Normalize: strip spaces and parentheses and '*' characters.
    std::string t;
    t.reserve(u.size());
    for (char c : u) {
        const char lo = to_lower_ascii(c);
        if (lo == ' ' || lo == '\t' || lo == '(' || lo == ')' || lo == '*' || lo == ',') continue;
        t.push_back(lo);
    }

    if (t == "s" || t == "sec" || t == "secs" || t == "second" || t == "seconds") return (uint32_t)MetricUnitCode::Seconds;

    // Frequency
    if (t == "hz") return (uint32_t)MetricUnitCode::Hertz;

    // Length
    if (t == "m" || t == "meter" || t == "meters") return (uint32_t)MetricUnitCode::Meters;

    // Temperature
    if (t == "k" || t == "kelvin") return (uint32_t)MetricUnitCode::Kelvin;
    if (t == "c" || t == "celsius") return (uint32_t)MetricUnitCode::Celsius;

    // Pressure/modulus
    if (t == "pa") return (uint32_t)MetricUnitCode::Pascal;
    if (t == "kpa") return (uint32_t)MetricUnitCode::Pascal;
    if (t == "mpa") return (uint32_t)MetricUnitCode::Pascal;
    if (t == "gpa") return (uint32_t)MetricUnitCode::Pascal;

    // Conductivity
    if (t == "w/mk" || t == "w/m*k" || t == "w/(mk)" || t == "w/(m*k)") return (uint32_t)MetricUnitCode::W_Per_MK;
    if (t == "s/m" || t == "siemens/m" || t == "siemensperm") return (uint32_t)MetricUnitCode::S_Per_M;

    // Diffusion
    if (t == "m2/s" || t == "m^2/s" || t == "m^2/sec" || t == "m2/sec") return (uint32_t)MetricUnitCode::M2_Per_S;

    return (uint32_t)MetricUnitCode::Unknown;
}

static int64_t apply_simple_unit_scale_q32_32(int64_t v_q32_32, const std::string& unit_token_ascii) {
    // Convert common SI prefixes for supported units into base units.
    // This is intentionally conservative.
    //
    // Currently:
    // - kPa / MPa / GPa -> Pa
    // - kHz / MHz / GHz -> Hz (if token starts with k/m/g + hz)

    std::string t;
    t.reserve(unit_token_ascii.size());
    for (char c : unit_token_ascii) {
        const char lo = to_lower_ascii(c);
        if (lo == ' ' || lo == '\t' || lo == '(' || lo == ')' || lo == '*') continue;
        t.push_back(lo);
    }

    auto mul_pow10 = [&](int32_t e10)->int64_t {
        if (e10 <= 0) return v_q32_32;
        uint64_t p = pow10_u64((uint32_t)e10);
        if (p == 0ull) return v_q32_32;
        __int128 q = (__int128)v_q32_32 * (__int128)p;
        if (q > (__int128)INT64_MAX) q = (__int128)INT64_MAX;
        if (q < (__int128)INT64_MIN) q = (__int128)INT64_MIN;
        return (int64_t)q;
    };

    if (t == "kpa") return mul_pow10(3);
    if (t == "mpa") return mul_pow10(6);
    if (t == "gpa") return mul_pow10(9);

    if (t == "khz") return mul_pow10(3);
    if (t == "mhz") return mul_pow10(6);
    if (t == "ghz") return mul_pow10(9);

    return v_q32_32;
}

static bool window_has(const std::string& s, size_t center, const char* needle) {
    if (!needle) return false;
    const size_t n = std::strlen(needle);
    if (n == 0) return false;

    const size_t win = 96;
    const size_t a = (center > win) ? (center - win) : 0;
    const size_t b = ((center + win) < s.size()) ? (center + win) : s.size();

    // Bounded naive scan.
    for (size_t i = a; i + n <= b; ++i) {
        bool ok = true;
        for (size_t j = 0; j < n; ++j) {
            if (s[i + j] != needle[j]) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

static MetricKind classify_claim_kind(const std::string& text_lower, size_t at, uint32_t unit_code_u32) {
    // Unit-first classification, keyword-confirmed.
    const MetricUnitCode u = (MetricUnitCode)unit_code_u32;

    if (u == MetricUnitCode::W_Per_MK) {
        if (window_has(text_lower, at, "thermal") || window_has(text_lower, at, "conductiv")) return MetricKind::Mat_Thermal_Conductivity;
    }
    if (u == MetricUnitCode::S_Per_M) {
        if (window_has(text_lower, at, "elect") || window_has(text_lower, at, "conductiv")) return MetricKind::Mat_Electrical_Conductivity;
    }
    if (u == MetricUnitCode::Pascal) {
        if (window_has(text_lower, at, "young") || window_has(text_lower, at, "modulus") || window_has(text_lower, at, "elastic")) return MetricKind::Mat_StressStrain_Modulus;
        if (window_has(text_lower, at, "pressure") || window_has(text_lower, at, "atmos")) return MetricKind::Cosmo_Atmos_PressureProfile;
    }
    if (u == MetricUnitCode::M2_Per_S) {
        if (window_has(text_lower, at, "diffus") || window_has(text_lower, at, "diffusion")) return MetricKind::Chem_Diffusion_Coefficient;
        if (window_has(text_lower, at, "osmos")) return MetricKind::Bio_CellDiffusion_Osmosis;
    }
    if (u == MetricUnitCode::Seconds) {
        if (window_has(text_lower, at, "period") || window_has(text_lower, at, "orbit")) return MetricKind::Cosmo_Orbit_Period;
    }
    if (u == MetricUnitCode::Hertz) {
        if (window_has(text_lower, at, "spectrum") || window_has(text_lower, at, "frequency") || window_has(text_lower, at, "radiation")) return MetricKind::Cosmo_Radiation_Spectrum;
    }

    // Fallback: keyword-only for a few kinds.
    if (window_has(text_lower, at, "phase change") || window_has(text_lower, at, "melting") || window_has(text_lower, at, "boiling")) {
        return MetricKind::Mat_PhaseChange_Threshold;
    }

    return MetricKind::Unknown;
}

uint32_t ew_extract_metric_claims_from_utf8_bounded(
    const std::string& utf8,
    uint32_t text_cap_bytes_u32,
    uint32_t max_claims_u32,
    std::vector<MetricClaim>& out_claims
) {
    if (max_claims_u32 == 0u) return 0u;

    const size_t start_n = out_claims.size();

    const std::string text = make_bounded_ascii_lower(utf8, text_cap_bytes_u32);

    uint32_t ordinal = 0u;
    for (size_t i = 0; i < text.size() && ordinal < max_claims_u32; ++i) {
        // Find a number start.
        const char c = text[i];
        if (!(is_digit(c) || c == '-' || c == '+')) continue;

        // Basic sanity: a sign must be followed by a digit.
        if ((c == '-' || c == '+') && (i + 1 >= text.size() || !is_digit(text[i + 1]))) continue;

        size_t j = i;
        if (text[j] == '-' || text[j] == '+') j++;
        bool saw_digit = false;
        while (j < text.size() && is_digit(text[j])) { saw_digit = true; j++; }
        if (j < text.size() && text[j] == '.') {
            j++;
            while (j < text.size() && is_digit(text[j])) { saw_digit = true; j++; }
        }
        // Optional exponent
        if (j + 1 < text.size() && (text[j] == 'e')) {
            size_t k = j + 1;
            if (k < text.size() && (text[k] == '+' || text[k] == '-')) k++;
            if (k < text.size() && is_digit(text[k])) {
                k++;
                while (k < text.size() && is_digit(text[k])) k++;
                j = k;
            }
        }
        if (!saw_digit) continue;

        // Parse number.
        int64_t v_q32_32 = 0;
        if (!parse_number_to_q32_32(text, i, j, v_q32_32)) continue;

        // Skip whitespace.
        size_t k = j;
        while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) k++;

        // Capture unit token.
        std::string unit;
        unit.reserve(16);
        size_t u_end = k;
        while (u_end < text.size() && unit.size() < 16u) {
            const char uc = text[u_end];
            const bool ok = (uc >= 'a' && uc <= 'z') || (uc >= '0' && uc <= '9') || uc == '/' || uc == '^' || uc == '*' || uc == '(' || uc == ')';
            if (!ok) break;
            unit.push_back(uc);
            u_end++;
        }

        const uint32_t unit_code = unit_code_from_token_ascii(unit);
        if (unit_code == (uint32_t)MetricUnitCode::Unknown) {
            // No recognized unit: skip.
            continue;
        }

        // Apply simple scaling (kPa/MPa/GPa, kHz/MHz/GHz).
        v_q32_32 = apply_simple_unit_scale_q32_32(v_q32_32, unit);

        const MetricKind kind = classify_claim_kind(text, i, unit_code);
        if (kind == MetricKind::Unknown) {
            continue;
        }

        MetricClaim c_out{};
        c_out.kind = kind;
        c_out.value_q32_32 = v_q32_32;
        c_out.unit_code_u32 = unit_code;
        c_out.context_flags_u32 = 0u;
        c_out.claim_ordinal_u32 = ordinal;
        out_claims.push_back(c_out);
        ordinal++;

        // Continue scanning after the unit token to avoid re-reading digits.
        i = (u_end > i) ? (u_end - 1) : i;
    }

    const size_t end_n = out_claims.size();
    return (uint32_t)(end_n - start_n);
}

} // namespace genesis
