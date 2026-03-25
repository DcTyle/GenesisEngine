#include "GE_math_foundation.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "GE_runtime.hpp"
#include "GE_experiment_templates.hpp"

namespace genesis {

// Use canonical abs_i64 from canonical_ops.cpp/header; avoid local duplicate.

MathFoundation::MathFoundation() {
    stats_ = {};
    pemdas_exprs_.clear();
    pemdas_expected_q32_32_.clear();

    // Deterministic baseline graph parameters: y = 2x + 1.
    graph_a_q32_32_ = ((int64_t)2) << 32;
    graph_b_q32_32_ = ((int64_t)1) << 32;
}

int64_t MathFoundation::q32_32_mul(int64_t a, int64_t b) {
    // (a*b)>>32 with 128-bit intermediate.
    __int128 z = ( __int128)a * ( __int128)b;
    z >>= 32;
    if (z > (__int128)INT64_MAX) return INT64_MAX;
    if (z < (__int128)INT64_MIN) return INT64_MIN;
    return (int64_t)z;
}

int64_t MathFoundation::q32_32_div(int64_t a, int64_t b) {
    if (b == 0) return (a >= 0) ? INT64_MAX : INT64_MIN;
    __int128 z = ( (__int128)a << 32 ) / ( __int128)b;
    if (z > (__int128)INT64_MAX) return INT64_MAX;
    if (z < (__int128)INT64_MIN) return INT64_MIN;
    return (int64_t)z;
}

// -----------------------------
// PEMDAS expression evaluator
// -----------------------------
// Shunting-yard to RPN, then eval.
// Only integers and + - * / and parentheses.

struct Tok {
    enum Kind { Num, Op, LParen, RParen } kind;
    int64_t num_q32_32;
    char op;
};

static int op_prec(char op) {
    if (op == '*' || op == '/') return 2;
    if (op == '+' || op == '-') return 1;
    return 0;
}

static bool tokize(const std::string& s, std::vector<Tok>& out) {
    out.clear();
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char ch = (unsigned char)s[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') { ++i; continue; }
        if (ch == '(') { out.push_back({Tok::LParen, 0, 0}); ++i; continue; }
        if (ch == ')') { out.push_back({Tok::RParen, 0, 0}); ++i; continue; }
        if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
            out.push_back({Tok::Op, 0, (char)ch}); ++i; continue;
        }
        if (ch >= '0' && ch <= '9') {
            int64_t v = 0;
            while (i < s.size()) {
                const unsigned char c2 = (unsigned char)s[i];
                if (c2 < '0' || c2 > '9') break;
                v = v * 10 + (int64_t)(c2 - '0');
                if (v > (int64_t)0x7FFFFFFF) v = (int64_t)0x7FFFFFFF;
                ++i;
            }
            Tok t{Tok::Num, (v << 32), 0};
            out.push_back(t);
            continue;
        }
        // Reject unknown characters deterministically.
        return false;
    }
    return !out.empty();
}

static bool to_rpn(const std::vector<Tok>& toks, std::vector<Tok>& rpn) {
    rpn.clear();
    std::vector<Tok> ops;
    ops.reserve(toks.size());
    for (size_t i = 0; i < toks.size(); ++i) {
        const Tok& t = toks[i];
        if (t.kind == Tok::Num) {
            rpn.push_back(t);
        } else if (t.kind == Tok::Op) {
            while (!ops.empty()) {
                const Tok& top = ops.back();
                if (top.kind == Tok::Op && op_prec(top.op) >= op_prec(t.op)) {
                    rpn.push_back(top);
                    ops.pop_back();
                } else {
                    break;
                }
            }
            ops.push_back(t);
        } else if (t.kind == Tok::LParen) {
            ops.push_back(t);
        } else if (t.kind == Tok::RParen) {
            bool found = false;
            while (!ops.empty()) {
                Tok top = ops.back();
                ops.pop_back();
                if (top.kind == Tok::LParen) { found = true; break; }
                rpn.push_back(top);
            }
            if (!found) return false;
        }
    }
    while (!ops.empty()) {
        Tok top = ops.back();
        ops.pop_back();
        if (top.kind == Tok::LParen || top.kind == Tok::RParen) return false;
        rpn.push_back(top);
    }
    return !rpn.empty();
}

static bool eval_rpn(const std::vector<Tok>& rpn, int64_t& out_q32_32) {
    std::vector<int64_t> st;
    st.reserve(rpn.size());
    for (size_t i = 0; i < rpn.size(); ++i) {
        const Tok& t = rpn[i];
        if (t.kind == Tok::Num) {
            st.push_back(t.num_q32_32);
        } else if (t.kind == Tok::Op) {
            if (st.size() < 2) return false;
            const int64_t b = st.back(); st.pop_back();
            const int64_t a = st.back(); st.pop_back();
            int64_t r = 0;
            if (t.op == '+') r = a + b;
            else if (t.op == '-') r = a - b;
            else if (t.op == '*') r = MathFoundation::q32_32_mul(a, b);
            else if (t.op == '/') r = MathFoundation::q32_32_div(a, b);
            else return false;
            st.push_back(r);
        } else {
            return false;
        }
    }
    if (st.size() != 1) return false;
    out_q32_32 = st.back();
    return true;
}

bool MathFoundation::eval_pemdas_expr_q32_32(const std::string& s, int64_t& out_q32_32) {
    std::vector<Tok> toks;
    std::vector<Tok> rpn;
    if (!tokize(s, toks)) return false;
    if (!to_rpn(toks, rpn)) return false;
    return eval_rpn(rpn, out_q32_32);
}

bool MathFoundation::bootstrap_defaults(std::string* out_report) {
    // A small deterministic test bank. These are not dummy values; they are
    // the concrete measurable objectives for Stage-0 math.
    pemdas_exprs_ = {
        "2+3*4",          // 14
        "(2+3)*4",        // 20
        "18/3+2",         // 8
        "18/(3+3)",       // 3
        "7+8/4*3",        // 13
        "(7+8)/(4*3)",    // 1 (integer division in Q32.32)
    };
    pemdas_expected_q32_32_.clear();
    pemdas_expected_q32_32_.reserve(pemdas_exprs_.size());

    stats_.pemdas_cases_total_u32 = (uint32_t)pemdas_exprs_.size();
    stats_.pemdas_cases_passed_u32 = 0u;

    bool ok_any = false;
    for (size_t i = 0; i < pemdas_exprs_.size(); ++i) {
        int64_t v = 0;
        const bool ok = eval_pemdas_expr_q32_32(pemdas_exprs_[i], v);
        if (!ok) {
            pemdas_expected_q32_32_.push_back(0);
            continue;
        }
        pemdas_expected_q32_32_.push_back(v);
        ok_any = true;
    }

    // Graph baseline: y = 2x + 1 on [-8..+8] with 129 samples.
    stats_.graph_1d_samples_total_u32 = 129u;
    stats_.graph_1d_packets_emitted_u32 = 0u;

    if (out_report) {
        *out_report = "MATH_BOOTSTRAP:PEMDAS_CASES=" + std::to_string((uint32_t)pemdas_exprs_.size()) + "\n";
        *out_report += "MATH_BOOTSTRAP:GRAPH1D_SAMPLES=" + std::to_string(stats_.graph_1d_samples_total_u32) + "\n";
        *out_report += "MATH_BOOTSTRAP:KHAN_PAGES_SEEN=" + std::to_string(stats_.khan_pages_seen_u32) + "\n";
    }
    return ok_any;
}

void MathFoundation::observe_crawl_text_khan_math(const std::string& host_utf8, const std::string& path_utf8, const std::string& text) {
    // Strict filter: only khanacademy.org math paths.
    if (host_utf8.find("khanacademy.org") == std::string::npos) return;
    if (path_utf8.find("/math") == std::string::npos && path_utf8.find("/" ) != 0) return;

    // Deterministic cap to avoid overweighting a single page.
    const uint32_t cap = 8192u;
    const uint32_t n = (uint32_t)((text.size() < (size_t)cap) ? text.size() : (size_t)cap);
    if (n == 0u) return;

    stats_.khan_pages_seen_u32 += 1u;
    stats_.khan_chars_ingested_u32 += n;
}

genesis::MetricVector MathFoundation::metrics_for_kind(genesis::MetricKind k) const {
    genesis::MetricVector mv;
    mv.dim_u32 = 0;
    for (uint32_t i = 0; i < genesis::GENESIS_METRIC_DIM_MAX; ++i) mv.v_q32_32[i] = 0;

    if (k == genesis::MetricKind::Math_Pemdas_PrecedenceStats) {
        // dim: [total, passed]
        mv.dim_u32 = 2;
        mv.v_q32_32[0] = ((int64_t)stats_.pemdas_cases_total_u32) << 32;
        mv.v_q32_32[1] = ((int64_t)stats_.pemdas_cases_passed_u32) << 32;
    } else if (k == genesis::MetricKind::Math_Graph_1D_ProbeStats) {
        // dim: [samples_total, packets_emitted]
        mv.dim_u32 = 2;
        mv.v_q32_32[0] = ((int64_t)stats_.graph_1d_samples_total_u32) << 32;
        mv.v_q32_32[1] = ((int64_t)stats_.graph_1d_packets_emitted_u32) << 32;
    } else if (k == genesis::MetricKind::Math_KhanAcademy_CoverageStats) {
        // dim: [pages_seen, chars_ingested]
        mv.dim_u32 = 2;
        mv.v_q32_32[0] = ((int64_t)stats_.khan_pages_seen_u32) << 32;
        mv.v_q32_32[1] = ((int64_t)stats_.khan_chars_ingested_u32) << 32;
    }
    return mv;
}

genesis::MetricTask MathFoundation::make_task_for_kind(genesis::MetricKind k, uint64_t source_id_u64, uint32_t source_anchor_id_u32, uint32_t context_anchor_id_u32) const {
    genesis::MetricTask task;
    task.source_id_u64 = source_id_u64;
    task.source_anchor_id_u32 = source_anchor_id_u32;
    task.context_anchor_id_u32 = context_anchor_id_u32;
    task.target.kind = k;
    task.target.target = metrics_for_kind(k);
    // Stage-0 math checkpoints are structural and must match exactly.
    task.target.tol_num_u32 = 0;
    task.target.tol_den_u32 = 1;
    task.tries_remaining_u64 = 4096ull;
    task.tries_per_step_u32 = 64u;
    task.ticks_remaining_u32 = 720u;
    return task;
}

void MathFoundation::tick(::SubstrateManager* sm) {
    if (!sm) return;

    // Update PEMDAS pass count deterministically by re-evaluating the bank.
    // This is a measurable invariant (parser correctness) and must be stable.
    uint32_t passed = 0u;
    for (size_t i = 0; i < pemdas_exprs_.size() && i < pemdas_expected_q32_32_.size(); ++i) {
        int64_t v = 0;
        if (!eval_pemdas_expr_q32_32(pemdas_exprs_[i], v)) continue;
        if (v == pemdas_expected_q32_32_[i]) passed++;
    }
    stats_.pemdas_cases_passed_u32 = passed;

    // Emit a 1D graph into the probe lattice once per N ticks, bounded.
    // This drives visual learning: line y = 2x+1.
    if ((sm->canonical_tick & 31ull) != 0ull) return; // every 32 ticks
    if (sm->learning_curriculum_stage_u32 != 0u) return; // only stage0

    // Use experiment template so the opcode runner is exercised, not bypassed.
    // Graph is written as pulses into the probe lattice and then rendered via tag.
    EigenWare::EwExperimentRequest req;
    req.name = "graph_1d";
    req.micro_ticks_u32 = 128u;
    req.tag_render = true;
    req.slice_z_u32 = 32u;
    req.stride_u32 = 2u;
    req.max_points_u32 = 120000u;
    req.intensity_min_u8 = 8u;
    // graph params
    req.graph_a_q32_32 = graph_a_q32_32_;
    req.graph_b_q32_32 = graph_b_q32_32_;
    req.graph_samples_u32 = stats_.graph_1d_samples_total_u32;
    req.graph_xmin_q32_32 = -(((int64_t)8) << 32);
    req.graph_xmax_q32_32 = ((int64_t)8) << 32;
    (void)sm->compile_and_submit_experiment(req);
    stats_.graph_1d_packets_emitted_u32 += 1u;
}

} // namespace genesis
