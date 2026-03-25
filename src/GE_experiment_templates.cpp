#include "GE_experiment_templates.hpp"
#include "ew_kv_params.hpp"
#include "ew_id9.hpp"
#include "ew_packed_io.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

// Convert an int64 integer to Q32.32 fixed-point safely (with 128-bit intermediate).
static inline int64_t i64_to_q32_32(int64_t v) {
    __int128 t = (__int128)v * (__int128(1) << 32);
    if (t > (__int128)INT64_MAX) return INT64_MAX;
    if (t < (__int128)INT64_MIN) return INT64_MIN;
    return (int64_t)t;
}

namespace EigenWare {

// ew_kv_params.hpp places parsing helpers in namespace ew.
using namespace ew;

static inline std::string ascii_lower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) s[i] = ew_ascii_lower(s[i]);
    return s;
}

static inline bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static inline bool parse_u32(const std::string& s, uint32_t& out) { return ew_parse_u32_ascii(s, out); }
static inline bool parse_i64(const std::string& s, int64_t& out) { return ew_parse_i64_ascii(s, out); }

static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) { return ew_clamp_u32(v, lo, hi); }

static inline uint32_t read_kv_token(const std::string& tok, std::string& k, std::string& v) {
    std::string_view ks, vs;
    if (!ew_split_kv_token_ascii(tok, ks, vs)) return 0u;
    k.assign(ks.data(), ks.size());
    v.assign(vs.data(), vs.size());
    return 1u;
}

bool ew_parse_experiment_request(const std::string& utf8_line, EwExperimentRequest& out) {
    out = EwExperimentRequest{};

    // Trim leading whitespace.
    size_t i = 0;
    while (i < utf8_line.size() && (utf8_line[i] == ' ' || utf8_line[i] == '\t' || utf8_line[i] == '\n' || utf8_line[i] == '\r')) i++;
    if (i >= utf8_line.size()) return false;

    bool is_exp = false;
    if (utf8_line.compare(i, 5, "/exp ") == 0) {
        i += 5;
        is_exp = true;
    } else if (utf8_line.compare(i, 4, "exp:") == 0) {
        i += 4;
        is_exp = true;
    }
    if (!is_exp) return false;

    while (i < utf8_line.size() && (utf8_line[i] == ' ' || utf8_line[i] == '\t')) i++;
    if (i >= utf8_line.size()) return false;

    // Read template name.
    size_t j = i;
    while (j < utf8_line.size() && is_name_char(utf8_line[j])) j++;
    if (j == i) return false;
    out.name = ascii_lower(utf8_line.substr(i, j - i));
    i = j;

    // Deterministic amplitude from full-line 9D ID (vectorized encoding; no hashing).
    const EwId9 id9 = ew_id9_from_ascii(utf8_line.data(), utf8_line.size());
    // Map into a bounded Q32.32 amplitude: [0.05 .. 0.25] in float space.
    const uint32_t lo_q = (uint32_t)(0.05 * 4294967296.0);
    const uint32_t span_q = (uint32_t)(0.20 * 4294967296.0);
    // Use a stable lane mix derived directly from packed bytes.
    const uint32_t frac = (uint32_t)(id9.u32[0] ^ id9.u32[3] ^ id9.u32[6] ^ id9.u32[8]);
    const uint64_t amp_q = (uint64_t)lo_q + ((uint64_t)span_q * (uint64_t)frac >> 32);
    out.amp_q32_32 = (int64_t)amp_q;

    // Defaults (shared across templates).
    out.micro_ticks_u32 = 256;
    out.tag_render = true;
    out.slice_z_u32 = 64;
    out.stride_u32 = 2;
    out.max_points_u32 = 50000;
    out.intensity_min_u8 = 8;
    out.x_u32 = 64;
    out.y_u32 = 64;
    out.z_u32 = 64;
    out.pattern_kind_u32 = 0;
    out.pattern_radius_u32 = 0;

    // World-origin used when seeding a probe isolate. Kept separate from x/y/z
    // (which refer to the target lattice coordinates of the pulse injection).
    out.origin_x_u32 = 0;
    out.origin_y_u32 = 0;
    out.origin_z_u32 = 0;

    // Graph defaults: y = 2x + 1 on [-8..+8] sampled at 129.
    out.graph_a_q32_32 = i64_to_q32_32(2);
    out.graph_b_q32_32 = i64_to_q32_32(1);
    out.graph_xmin_q32_32 = i64_to_q32_32(-8);
    out.graph_xmax_q32_32 = i64_to_q32_32(8);
    out.graph_samples_u32 = 129u;

    // Graph_2d defaults: f(x,y) = x + y on [-8..+8] x [-8..+8] at 65x65.
    out.graph2_ax_q32_32 = i64_to_q32_32(1);
    out.graph2_by_q32_32 = i64_to_q32_32(1);
    out.graph2_c_q32_32 = 0;
    out.graph2_xmin_q32_32 = i64_to_q32_32(-8);
    out.graph2_xmax_q32_32 = i64_to_q32_32(8);
    out.graph2_ymin_q32_32 = i64_to_q32_32(-8);
    out.graph2_ymax_q32_32 = i64_to_q32_32(8);
    out.graph2_samples_x_u32 = 65u;
    out.graph2_samples_y_u32 = 65u;

    out.expr_ascii.clear();

    // Parse key=value tokens.
    while (i < utf8_line.size()) {
        while (i < utf8_line.size() && (utf8_line[i] == ' ' || utf8_line[i] == '\t')) i++;
        if (i >= utf8_line.size()) break;
        size_t k0 = i;
        while (i < utf8_line.size() && utf8_line[i] != ' ' && utf8_line[i] != '\t' && utf8_line[i] != '\n' && utf8_line[i] != '\r') i++;
        const std::string tok = utf8_line.substr(k0, i - k0);
        std::string k, v;
        if (!read_kv_token(tok, k, v)) continue;
        uint32_t u = 0;
        if (k == "micro" || k == "micro_ticks" || k == "ticks") {
            if (parse_u32(v, u)) out.micro_ticks_u32 = clamp_u32(u, 1u, 1u << 20);
        } else if (k == "slice" || k == "slice_z") {
            if (parse_u32(v, u)) out.slice_z_u32 = u;
        } else if (k == "stride") {
            if (parse_u32(v, u)) out.stride_u32 = clamp_u32(u, 1u, 64u);
        } else if (k == "max_points") {
            if (parse_u32(v, u)) out.max_points_u32 = clamp_u32(u, 1u, 200000u);
        } else if (k == "min" || k == "intensity_min") {
            if (parse_u32(v, u)) out.intensity_min_u8 = (uint8_t)clamp_u32(u, 0u, 255u);
        } else if (k == "x") {
            if (parse_u32(v, u)) out.x_u32 = u;
        } else if (k == "y") {
            if (parse_u32(v, u)) out.y_u32 = u;
        } else if (k == "z") {
            if (parse_u32(v, u)) out.z_u32 = u;
        } else if (k == "ox" || k == "origin_x") {
            if (parse_u32(v, u)) out.origin_x_u32 = u;
        } else if (k == "oy" || k == "origin_y") {
            if (parse_u32(v, u)) out.origin_y_u32 = u;
        } else if (k == "oz" || k == "origin_z") {
            if (parse_u32(v, u)) out.origin_z_u32 = u;
        } else if (k == "pattern") {
            if (v == "center") out.pattern_kind_u32 = 0;
            else if (v == "ring") out.pattern_kind_u32 = 1;
            else if (v == "cross") out.pattern_kind_u32 = 2;
        } else if (k == "radius") {
            if (parse_u32(v, u)) out.pattern_radius_u32 = clamp_u32(u, 0u, 512u);
        } else if (k == "tag") {
            bool b = false;
            if (ew_parse_bool_ascii(v, b)) out.tag_render = b;
        } else if (k == "a") {
            int64_t iv = 0;
            if (parse_i64(v, iv)) out.graph_a_q32_32 = i64_to_q32_32(iv);
        } else if (k == "b") {
            int64_t iv = 0;
            if (parse_i64(v, iv)) out.graph_b_q32_32 = i64_to_q32_32(iv);
        } else if (k == "xmin") {
            int64_t iv = 0;
            if (parse_i64(v, iv)) out.graph_xmin_q32_32 = i64_to_q32_32(iv);
        } else if (k == "xmax") {
            int64_t iv = 0;
            if (parse_i64(v, iv)) out.graph_xmax_q32_32 = i64_to_q32_32(iv);
        } else if (k == "samples") {
            if (parse_u32(v, u)) out.graph_samples_u32 = clamp_u32(u, 8u, 4096u);
        } else if (k == "expr") {
            // Expression is stored verbatim after ASCII-lower normalization (operators and digits unchanged).
            out.expr_ascii = v;
        } else if (k == "ax") {
            int64_t iv = 0;
            if (parse_i64(v, iv)) out.graph2_ax_q32_32 = (iv << 32);
        } else if (k == "by") {
            int64_t iv = 0;
            if (parse_i64(v, iv)) out.graph2_by_q32_32 = (iv << 32);
        } else if (k == "c") {
            int64_t iv = 0;
            if (parse_i64(v, iv)) out.graph2_c_q32_32 = (iv << 32);
        } else if (k == "ymin") {
            int64_t iv = 0;
            if (parse_i64(v, iv)) out.graph2_ymin_q32_32 = (iv << 32);
        } else if (k == "ymax") {
            int64_t iv = 0;
            if (parse_i64(v, iv)) out.graph2_ymax_q32_32 = (iv << 32);
        } else if (k == "sx" || k == "samples_x") {
            if (parse_u32(v, u)) out.graph2_samples_x_u32 = clamp_u32(u, 8u, 2048u);
        } else if (k == "sy" || k == "samples_y") {
            if (parse_u32(v, u)) out.graph2_samples_y_u32 = clamp_u32(u, 8u, 2048u);
        }
    }

    // Validate known templates.
    if (out.name != "field_relax" && out.name != "diffuse" && out.name != "qm_well" &&
        out.name != "probe_isolate" && out.name != "graph_1d" && out.name != "graph_2d" &&
        out.name != "pemdas_objects") return false;

    if (out.name == "pemdas_objects") {
        // Provide a safe default expression.
        if (out.expr_ascii.empty()) out.expr_ascii = "(2+3)*4";
    }
    return true;
}

static inline void write_u32_le(uint8_t* p, uint32_t v) { ew_write_u32_le(p, v); }
static inline void write_i64_le(uint8_t* p, int64_t v) { ew_write_i64_le(p, v); }
static inline void write_f64_le(uint8_t* p, double dv) { ew_write_f64_le(p, dv); }

static std::vector<uint8_t> make_pkt(uint32_t op_kind, uint32_t exec_order, const uint8_t* payload, uint32_t payload_bytes) {
    // AnchorOpPacked_v1 is fixed 1500 bytes. We set op_id to an ASCII-stable lane id.
    // Layout:
    //  0..71   f64[9] op_id_e9
    //  72..75  u32 op_kind
    //  76..79  u32 exec_order
    //  80..83  u32 n_in (0)
    //  84..659 f64[72] in lanes (zero)
    //  660..663 u32 n_out (0)
    //  664..1239 f64[72] out lanes (zero)
    //  1240..1243 u32 payload_bytes
    //  1244..1499 u8[256] payload
    std::vector<uint8_t> b;
    b.assign(1500u, 0u);

    // op_id: encode kind+order into the first two slots, rest zero.
    write_f64_le(b.data() + 0, (double)op_kind);
    write_f64_le(b.data() + 8, (double)exec_order);

    write_u32_le(b.data() + 72, op_kind);
    write_u32_le(b.data() + 76, exec_order);

    write_u32_le(b.data() + 80, 0u);
    write_u32_le(b.data() + 660, 0u);
    write_u32_le(b.data() + 1240, payload_bytes);
    if (payload_bytes != 0u && payload != nullptr) {
        std::memcpy(b.data() + 1244, payload, payload_bytes);
    }
    return b;
}

bool ew_compile_experiment_to_operator_packets(const EwExperimentRequest& req,
                                               std::vector<std::vector<uint8_t>>& out_packets) {
    out_packets.clear();

    // New operator kinds (v1 extension):
    // 0x00000200 OPK_LATTICE_PULSE_PATTERN
    // 0x00000201 OPK_LATTICE_MICROSTEP
    // 0x00000202 OPK_LATTICE_TAG_SLICE
    // 0x00000203 OPK_LATTICE_MEASURE_SLICE
    // 0x00000204 OPK_PROBE_ISOLATE_RUN
    // 0x00000205 OPK_PROBE_GRAPH_1D_RUN
    // 0x00000206 OPK_PROBE_GRAPH_2D_RUN
    // 0x00000207 OPK_PROBE_PEMDAS_OBJECTS_RUN

    uint32_t order = 1;

    if (req.name == "probe_isolate") {
        // Single opcode program: seed probe from world origin, inject pattern into probe, evolve, optionally tag+measure.
        uint8_t p_iso[64]{};
        write_u32_le(p_iso + 0, req.origin_x_u32);
        write_u32_le(p_iso + 4, req.origin_y_u32);
        write_u32_le(p_iso + 8, req.origin_z_u32);
        write_u32_le(p_iso + 12, req.x_u32);
        write_u32_le(p_iso + 16, req.y_u32);
        write_u32_le(p_iso + 20, req.z_u32);
        write_u32_le(p_iso + 24, req.pattern_kind_u32);
        write_u32_le(p_iso + 28, req.pattern_radius_u32);
        write_i64_le(p_iso + 32, req.amp_q32_32);
        write_u32_le(p_iso + 40, req.micro_ticks_u32);
        write_u32_le(p_iso + 44, req.slice_z_u32);
        write_u32_le(p_iso + 48, req.stride_u32);
        write_u32_le(p_iso + 52, req.max_points_u32);
        write_u32_le(p_iso + 56, (uint32_t)req.intensity_min_u8);
        write_u32_le(p_iso + 60, req.tag_render ? 1u : 0u);
        out_packets.push_back(make_pkt(0x00000204u, order++, p_iso, 64u));
        return true;
    }

    if (req.name == "graph_1d") {
        // Single opcode: seed probe from world origin (origin_*), inject graph points, evolve, optionally tag+measure.
        uint8_t p_g[80]{};
        write_u32_le(p_g + 0, req.origin_x_u32);
        write_u32_le(p_g + 4, req.origin_y_u32);
        write_u32_le(p_g + 8, req.origin_z_u32);
        write_i64_le(p_g + 12, req.graph_a_q32_32);
        write_i64_le(p_g + 20, req.graph_b_q32_32);
        write_i64_le(p_g + 28, req.graph_xmin_q32_32);
        write_i64_le(p_g + 36, req.graph_xmax_q32_32);
        write_u32_le(p_g + 44, req.graph_samples_u32);
        write_i64_le(p_g + 48, req.amp_q32_32);
        write_u32_le(p_g + 56, req.micro_ticks_u32);
        write_u32_le(p_g + 60, req.slice_z_u32);
        write_u32_le(p_g + 64, req.stride_u32);
        write_u32_le(p_g + 68, req.max_points_u32);
        write_u32_le(p_g + 72, (uint32_t)req.intensity_min_u8);
        write_u32_le(p_g + 76, req.tag_render ? 1u : 0u);
        out_packets.push_back(make_pkt(0x00000205u, order++, p_g, 80u));
        return true;
    }

    if (req.name == "graph_2d") {
        // Single opcode: seed probe from world origin, inject 2D field points (heatmap), evolve, optionally tag+measure.
        uint8_t p2[128]{};
        write_u32_le(p2 + 0, req.origin_x_u32);
        write_u32_le(p2 + 4, req.origin_y_u32);
        write_u32_le(p2 + 8, req.origin_z_u32);
        write_i64_le(p2 + 12, req.graph2_ax_q32_32);
        write_i64_le(p2 + 20, req.graph2_by_q32_32);
        write_i64_le(p2 + 28, req.graph2_c_q32_32);
        write_i64_le(p2 + 36, req.graph2_xmin_q32_32);
        write_i64_le(p2 + 44, req.graph2_xmax_q32_32);
        write_i64_le(p2 + 52, req.graph2_ymin_q32_32);
        write_i64_le(p2 + 60, req.graph2_ymax_q32_32);
        write_u32_le(p2 + 68, req.graph2_samples_x_u32);
        write_u32_le(p2 + 72, req.graph2_samples_y_u32);
        write_i64_le(p2 + 76, req.amp_q32_32);
        write_u32_le(p2 + 84, req.micro_ticks_u32);
        write_u32_le(p2 + 88, req.slice_z_u32);
        write_u32_le(p2 + 92, req.stride_u32);
        write_u32_le(p2 + 96, req.max_points_u32);
        write_u32_le(p2 + 100, (uint32_t)req.intensity_min_u8);
        write_u32_le(p2 + 104, req.tag_render ? 1u : 0u);
        out_packets.push_back(make_pkt(0x00000206u, order++, p2, 128u));
        return true;
    }

    if (req.name == "pemdas_objects") {
        // Single opcode: render token objects with precedence-coded amplitudes and emit a deterministic evaluation.
        // Payload: origin (world), micro, slice, stride, max_points, min, tag, then expr bytes.
        uint8_t p3[256]{};
        write_u32_le(p3 + 0, req.origin_x_u32);
        write_u32_le(p3 + 4, req.origin_y_u32);
        write_u32_le(p3 + 8, req.origin_z_u32);
        write_i64_le(p3 + 12, req.amp_q32_32);
        write_u32_le(p3 + 20, req.micro_ticks_u32);
        write_u32_le(p3 + 24, req.slice_z_u32);
        write_u32_le(p3 + 28, req.stride_u32);
        write_u32_le(p3 + 32, req.max_points_u32);
        write_u32_le(p3 + 36, (uint32_t)req.intensity_min_u8);
        write_u32_le(p3 + 40, req.tag_render ? 1u : 0u);
        // Expr starts at 44, max 200 bytes.
        const size_t max_expr = 200u;
        size_t n = req.expr_ascii.size();
        if (n > max_expr) n = max_expr;
        write_u32_le(p3 + 44, (uint32_t)n);
        if (n != 0u) {
            std::memcpy(p3 + 48, req.expr_ascii.data(), n);
        }
        out_packets.push_back(make_pkt(0x00000207u, order++, p3, 256u));
        return true;
    }

    // Pulse pattern.
    {
        uint8_t p[40]{};
        // u32 lattice_sel (0=world,1=probe) -> we use world for patterns and evolve probe bound.
        write_u32_le(p + 0, 0u);
        write_u32_le(p + 4, req.pattern_kind_u32);
        write_u32_le(p + 8, req.x_u32);
        write_u32_le(p + 12, req.y_u32);
        write_u32_le(p + 16, req.z_u32);
        write_u32_le(p + 20, req.pattern_radius_u32);
        write_i64_le(p + 24, req.amp_q32_32);
        // remaining bytes are zero (reserved)
        out_packets.push_back(make_pkt(0x00000200u, order++, p, 40u));
    }

    // Microsteps.
    {
        uint8_t p[12]{};
        write_u32_le(p + 0, 0u); // lattice_sel
        write_u32_le(p + 4, req.micro_ticks_u32);
        write_u32_le(p + 8, 0u); // bind_as_probe (0=world)
        out_packets.push_back(make_pkt(0x00000201u, order++, p, 12u));
    }

    if (req.tag_render) {
        uint8_t p[16]{};
        write_u32_le(p + 0, req.slice_z_u32);
        write_u32_le(p + 4, req.stride_u32);
        write_u32_le(p + 8, req.max_points_u32);
        write_u32_le(p + 12, (uint32_t)req.intensity_min_u8);
        out_packets.push_back(make_pkt(0x00000202u, order++, p, 16u));

        uint8_t m[12]{};
        write_u32_le(m + 0, req.slice_z_u32);
        write_u32_le(m + 4, req.stride_u32);
        write_u32_le(m + 8, (uint32_t)req.intensity_min_u8);
        out_packets.push_back(make_pkt(0x00000203u, order++, m, 12u));
    }

    // Template-specific deterministic adjustments via additional packets.
    if (req.name == "diffuse") {
        // A second microstep chunk to increase smoothing. Still deterministic.
        uint8_t p2[12]{};
        write_u32_le(p2 + 0, 0u);
        write_u32_le(p2 + 4, req.micro_ticks_u32 / 2u + 1u);
        write_u32_le(p2 + 8, 0u);
        out_packets.push_back(make_pkt(0x00000201u, order++, p2, 12u));
    } else if (req.name == "qm_well") {
        // A ring/cross pulse reinforcement by reusing pulse pattern with a different kind.
        uint8_t p3[40]{};
        write_u32_le(p3 + 0, 0u);
        write_u32_le(p3 + 4, 2u); // cross
        write_u32_le(p3 + 8, req.x_u32);
        write_u32_le(p3 + 12, req.y_u32);
        write_u32_le(p3 + 16, req.z_u32);
        write_u32_le(p3 + 20, req.pattern_radius_u32 == 0 ? 6u : req.pattern_radius_u32);
        // Slightly higher amplitude (bounded).
        int64_t amp2 = req.amp_q32_32 + (req.amp_q32_32 >> 2);
        if (amp2 < 0) amp2 = 0;
        write_i64_le(p3 + 24, amp2);
        out_packets.push_back(make_pkt(0x00000200u, order++, p3, 40u));
    }

    return !out_packets.empty();
}

} // namespace EigenWare
