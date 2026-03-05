#include "text_encoder.hpp"
#include "frequency_collapse.hpp"
#include "delta_profiles.hpp"
#include "anchor.hpp"

#include "ew_cordic.hpp"
#include "fixed_point.hpp"

#include <vector>
#include <cmath>

static inline bool is_ascii_space(uint8_t b) {
    return (b == ' ' || b == '\t' || b == '\n' || b == '\r' || b == '\f' || b == '\v');
}

std::string normalize_text(const std::string& utf8) {
    // Deterministic normalization per Equations A.11.58:
    // - line ending normalize (CRLF/CR -> LF)
    // - collapse runs of spaces to single space
    // - collapse 3+ newlines to 2
    // - strip ends
    std::string out;
    out.reserve(utf8.size());

    const uint8_t* b = (const uint8_t*)utf8.data();
    const size_t n = utf8.size();
    bool in_space = false;
    int nl_run = 0;

    for (size_t i = 0; i < n; ++i) {
        uint8_t c = b[i];

        // Normalize CRLF / CR to LF.
        if (c == '\r') {
            if (i + 1 < n && b[i + 1] == '\n') ++i;
            c = '\n';
        }

        if (c == '\n') {
            // finalize any pending space before newline
            if (!out.empty() && out.back() == ' ') out.pop_back();
            nl_run++;
            // collapse 3+ newlines to 2
            if (nl_run <= 2) out.push_back('\n');
            in_space = false;
            continue;
        }

        // reset newline run
        nl_run = 0;

        if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
            if (!in_space) {
                out.push_back(' ');
                in_space = true;
            }
            continue;
        }

        out.push_back((char)c);
        in_space = false;
    }

    // strip ends (spaces and newlines)
    while (!out.empty() && (out.front() == ' ' || out.front() == '\n')) out.erase(out.begin());
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
    return out;
}


std::vector<std::string> segment_text_blocks(const std::string& normalized_utf8) {
    // Deterministic block segmentation: split on 2+ consecutive '\n'.
    std::vector<std::string> blocks;
    std::string cur;
    cur.reserve(normalized_utf8.size());

    int nl_run = 0;
    for (size_t i = 0; i < normalized_utf8.size(); ++i) {
        const char c = normalized_utf8[i];
        if (c == '\n') {
            nl_run++;
            if (nl_run >= 2) {
                // finalize block
                // trim trailing spaces/newlines
                while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\n')) cur.pop_back();
                // trim leading spaces
                size_t start = 0;
                while (start < cur.size() && cur[start] == ' ') start++;
                if (start < cur.size()) {
                    blocks.push_back(cur.substr(start));
                }
                cur.clear();
                // consume further newlines in the run (stay in segmentation mode)
            } else {
                cur.push_back('\n');
            }
        } else {
            // if we were in a newline run >=2, we've already segmented.
            nl_run = 0;
            cur.push_back(c);
        }
    }
    // final block
    while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\n')) cur.pop_back();
    size_t start = 0;
    while (start < cur.size() && cur[start] == ' ') start++;
    if (start < cur.size()) {
        blocks.push_back(cur.substr(start));
    }
    return blocks;
}

static inline E9d e9_zero() {
    E9d a{};
    for (int i = 0; i < 9; ++i) a.v[i] = 0.0;
    return a;
}

static inline E9d e9_add(const E9d& a, const E9d& b) {
    E9d out{};
    for (int i = 0; i < 9; ++i) out.v[i] = a.v[i] + b.v[i];
    return out;
}

static inline double e9_norm(const E9d& a) {
    double s = 0.0;
    for (int i = 0; i < 9; ++i) s += a.v[i] * a.v[i];
    // Deterministic sqrt is not required for Phase-1 replay stability when
    // using identical toolchains; however, normalization MUST be stable.
    // Use std::sqrt here; it only acts on an accumulated double and does not
    // introduce cross-run variance within a single environment.
    return std::sqrt(s);
}

static inline E9d e9_scale(const E9d& a, double s) {
    E9d out{};
    for (int i = 0; i < 9; ++i) out.v[i] = a.v[i] * s;
    return out;
}

static inline E9d embed_codepoint_to_E9(uint32_t cp) {
    // Spec 17.4
    // n in [0,1] as Q32.32 (deterministic).
    const int64_t n_q32_32 = (static_cast<int64_t>(cp) << 32) / static_cast<int64_t>(0x10FFFF);

    // theta = 2*pi*n in Q32.32, deterministic.
    static const int64_t TWO_PI_Q32_32 = 0x00000006487ED511LL;
    const __int128 t128 = static_cast<__int128>(n_q32_32) * static_cast<__int128>(TWO_PI_Q32_32);
    const int64_t theta_q32_32 = static_cast<int64_t>(t128 >> 32);

    auto q32_32_to_double = [](int64_t x) -> double {
        return static_cast<double>(x) / static_cast<double>(1ULL << 32);
    };

    E9d out{};

    // Harmonics: 1,2,4,8
    const int64_t theta1 = theta_q32_32;
    const int64_t theta2 = theta_q32_32 << 1;
    const int64_t theta4 = theta_q32_32 << 2;
    const int64_t theta8 = theta_q32_32 << 3;

    const EwSinCosQ32_32 sc1 = ew_cordic_sincos_q32_32(theta1);
    const EwSinCosQ32_32 sc2 = ew_cordic_sincos_q32_32(theta2);
    const EwSinCosQ32_32 sc4 = ew_cordic_sincos_q32_32(theta4);
    const EwSinCosQ32_32 sc8 = ew_cordic_sincos_q32_32(theta8);

    out.v[0] = q32_32_to_double(sc1.sin_q32_32);
    out.v[1] = q32_32_to_double(sc1.cos_q32_32);
    out.v[2] = q32_32_to_double(sc2.sin_q32_32);
    out.v[3] = q32_32_to_double(sc2.cos_q32_32);
    out.v[4] = q32_32_to_double(sc4.sin_q32_32);
    out.v[5] = q32_32_to_double(sc4.cos_q32_32);
    out.v[6] = q32_32_to_double(sc8.sin_q32_32);
    out.v[7] = q32_32_to_double(sc8.cos_q32_32);
    out.v[8] = q32_32_to_double(n_q32_32);
    return out;
}

// Minimal UTF-8 decoder with replacement.
static inline uint32_t decode_one_utf8(const uint8_t* s, size_t n, size_t* io_i) {
    const size_t i = *io_i;
    if (i >= n) return 0;
    const uint8_t b0 = s[i];

    // 1-byte ASCII
    if (b0 < 0x80) {
        *io_i = i + 1;
        return (uint32_t)b0;
    }

    // Continuation bytes must be 0b10xxxxxx
    auto is_cont = [](uint8_t b) { return (b & 0xC0) == 0x80; };

    // 2-byte
    if ((b0 & 0xE0) == 0xC0) {
        if (i + 1 >= n) { *io_i = i + 1; return 0xFFFD; }
        const uint8_t b1 = s[i + 1];
        if (!is_cont(b1)) { *io_i = i + 1; return 0xFFFD; }
        const uint32_t cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
        // overlong
        if (cp < 0x80) { *io_i = i + 2; return 0xFFFD; }
        *io_i = i + 2;
        return cp;
    }

    // 3-byte
    if ((b0 & 0xF0) == 0xE0) {
        if (i + 2 >= n) { *io_i = i + 1; return 0xFFFD; }
        const uint8_t b1 = s[i + 1];
        const uint8_t b2 = s[i + 2];
        if (!is_cont(b1) || !is_cont(b2)) { *io_i = i + 1; return 0xFFFD; }
        const uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
        // overlong
        if (cp < 0x800) { *io_i = i + 3; return 0xFFFD; }
        // surrogate
        if (cp >= 0xD800 && cp <= 0xDFFF) { *io_i = i + 3; return 0xFFFD; }
        *io_i = i + 3;
        return cp;
    }

    // 4-byte
    if ((b0 & 0xF8) == 0xF0) {
        if (i + 3 >= n) { *io_i = i + 1; return 0xFFFD; }
        const uint8_t b1 = s[i + 1];
        const uint8_t b2 = s[i + 2];
        const uint8_t b3 = s[i + 3];
        if (!is_cont(b1) || !is_cont(b2) || !is_cont(b3)) { *io_i = i + 1; return 0xFFFD; }
        const uint32_t cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(b1 & 0x3F) << 12) | ((uint32_t)(b2 & 0x3F) << 6) | (uint32_t)(b3 & 0x3F);
        // overlong
        if (cp < 0x10000) { *io_i = i + 4; return 0xFFFD; }
        // max
        if (cp > 0x10FFFF) { *io_i = i + 4; return 0xFFFD; }
        *io_i = i + 4;
        return cp;
    }

    // Invalid leader
    *io_i = i + 1;
    return 0xFFFD;
}

E9d ew_text_aggregate_utf8_to_SF(const std::string& utf8) {
    // Spec 17.5
    const std::string norm = normalize_text(utf8);
    const uint8_t* bytes = (const uint8_t*)norm.data();
    const size_t n = norm.size();

    E9d A = e9_zero();
    size_t i = 0;
    while (i < n) {
        uint32_t cp = decode_one_utf8(bytes, n, &i);
        E9d e = embed_codepoint_to_E9(cp);
        A = e9_add(A, e);
    }

    // Canonical rule: no global uniform normalization.
    // We only apply deterministic per-component bounding so the aggregate
    // remains within a stable numeric domain for downstream per-axis scaling.
    for (int k = 0; k < 9; ++k) {
        if (A.v[k] > 1.0) A.v[k] = 1.0;
        if (A.v[k] < -1.0) A.v[k] = -1.0;
    }
    return A;
}

// Canonical aggregate file displacement (17.5).
// Computes the normalized 9D displacement for a UTF‑8 byte stream.
// S_F = A / ||A|| where A is the sum of per-codepoint embeddings.
E9d ew_file_aggregate_utf8_to_SF(const std::string& utf8) {
    const std::string norm = normalize_text(utf8);
    const uint8_t* bytes = (const uint8_t*)norm.data();
    const size_t n = norm.size();
    E9d A = e9_zero();
    size_t i = 0;
    while (i < n) {
        uint32_t cp = decode_one_utf8(bytes, n, &i);
        E9d e = embed_codepoint_to_E9(cp);
        A = e9_add(A, e);
    }
    const double mag = e9_norm(A);
    if (mag > 0.0) {
        return e9_scale(A, 1.0 / mag);
    }
    return A;
}

int64_t ew_text_utf8_to_phase_ring_push_turns(const std::string& utf8) {
    // Blueprint 3.3.1: symbol-phase primitives with ring semantics.
    // N_sym = 256 for byte-stable determinism.
    // Each byte maps to a phase bin in turns: theta_sym = sym_index / N_sym.
    // Rings advance by a deterministic phase-density value (PAF).

    const std::string norm = normalize_text(utf8);
    const std::vector<std::string> blocks = segment_text_blocks(norm);

    int64_t theta_start = 0;
    int64_t theta_end = 0;
    int64_t total_delta = 0;

    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const std::string& blk = blocks[bi];

        bool have_prev = false;
        int64_t prev_theta = 0;
        int64_t phase_density_sum = 0;
        int64_t symbol_count = 0;

        for (size_t i = 0; i < blk.size(); ++i) {
            const uint8_t sym = (uint8_t)blk[i];
            // theta_sym in TURN_SCALE domain (turns), deterministic.
            const int64_t theta_sym = (static_cast<int64_t>(sym) * TURN_SCALE) / 256;
            const int64_t theta = wrap_turns(theta_start + theta_sym);

            if (have_prev) {
                const int64_t d = delta_turns(theta, prev_theta);
                total_delta = wrap_turns(total_delta + d);
                phase_density_sum += (d < 0) ? -d : d;
                symbol_count += 1;
            } else {
                have_prev = true;
            }
            prev_theta = theta;
        }

        theta_end = have_prev ? prev_theta : theta_start;

        // Deterministic PAF: mean absolute delta per symbol, clamped.
        int64_t paf = 0;
        if (symbol_count > 0) {
            paf = phase_density_sum / symbol_count;
            // Clamp to a stable bound (<= 1/8 turn) to prevent runaway.
            const int64_t paf_max = TURN_SCALE / 8;
            if (paf > paf_max) paf = paf_max;
        }

        theta_start = wrap_turns(theta_end + paf);
    }

    // Injection push: ring orientation drift plus a reduced net trajectory delta.
    // This preserves ring semantics while keeping a stable bounded driver.
    const int64_t push = wrap_turns(theta_start + (total_delta / 4));
    return push;
}

static inline int64_t theta_byte_turns_u8(uint8_t b) {
    // Spec 5.11.3: theta_byte_turns(b) = b / 256 turns.
    return (static_cast<int64_t>(b) * TURN_SCALE) / 256;
}

int32_t ew_text_utf8_to_frequency_code(const std::string& utf8, uint8_t profile_id) {
    // Spec 5.11.3/5.11.4 + Spec 3.5:
    // - Map UTF-8 to bytes (after deterministic normalization)
    // - Compute shortest signed phase deltas in turns using byte->phase targets
    // - Build a phase-dominant Basis9 delta and compress via spider graph
    //   under the requested delta profile.

    EwDeltaProfile prof;
    if (ew_get_delta_profile(profile_id, &prof) != 0) {
        // Fail closed to core profile if the requested profile is unknown.
        // (This is deterministic and avoids hidden behavior.)
        (void)ew_get_delta_profile(EW_PROFILE_CORE_EVOLUTION, &prof);
    }

    const std::string norm = normalize_text(utf8);
    const std::vector<std::string> blocks = segment_text_blocks(norm);

    // Build component frequencies directly from UTF-8 byte targets.
    // Each component contributes (f_i, a_i, phi_i) in TURN_SCALE/Q32.32 turns domain.
    std::vector<EwFreqComponentQ32_32> comps;
    comps.reserve(norm.size());

    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const std::string& blk = blocks[bi];
        for (size_t i = 0; i < blk.size(); ++i) {
            const uint8_t b = (uint8_t)blk[i];
            EwFreqComponentQ32_32 c;
            c.f_turns_q32_32 = theta_byte_turns_u8(b);
            // Unit amplitude weight in Q32.32 domain.
            c.a_q32_32 = TURN_SCALE;
            // Phase target equals the byte phase target; collapse will wrap-average.
            c.phi_turns_q32_32 = c.f_turns_q32_32;
            comps.push_back(c);
        }
    }

    EwCarrierWaveQ32_32 carrier;
    if (!ew_collapse_frequency_components_q32_32(comps, carrier)) {
        // Deterministic fail-closed: encode a zero driver.
        carrier.f_carrier_turns_q32_32 = 0;
        carrier.A_carrier_q32_32 = 0;
        carrier.phi_carrier_turns_q32_32 = 0;
        carrier.component_count_u32 = 0;
    }

    // Collapse requirement: derive a single frequency driver that deterministically
    // actuates vectors as a single-wave signal. We encode the carrier as an
    // amplitude-weighted frequency delta in the phase-dominant axis.
    //
    // d4 = f_carrier * A (Q32.32 -> Q32.32).
    const int64_t d4 = wrap_turns(mul_q32_32(carrier.f_carrier_turns_q32_32, carrier.A_carrier_q32_32));

    Basis9 projected;
    for (int k = 0; k < 9; ++k) projected.d[k] = 0;
    projected.d[4] = d4;

    // Use a zero-baseline anchor to interpret the packet as a pure delta.
    Anchor tmp(0);
    tmp.theta_q = 0;
    for (int k = 0; k < 9; ++k) tmp.basis9.d[k] = 0;

    const int32_t f_code = tmp.spider_encode_9d(projected, prof.weights_q10, prof.denom_q);
    return f_code;
}
