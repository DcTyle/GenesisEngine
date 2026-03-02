#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ew_id9.hpp"
#include "GE_metric_registry.hpp"

// SubstrateManager lives in the global namespace (engine runtime).
class SubstrateManager;

namespace genesis {

// MathFoundation
// -------------
// Stage-0 math foundations learned in parallel with language.
//
// Canonical constraints:
// - Deterministic (no RNG, no hashing/crypto ids).
// - Use the sandbox lattice for understanding: build graphs and test results
//   by injecting deterministic pulses into the probe lattice.
// - Crawl Khan Academy math in parallel with language, and track measurable
//   coverage (pages observed, tokens matched to lexicon).

struct MathStats {
    uint32_t pemdas_cases_total_u32 = 0u;
    uint32_t pemdas_cases_passed_u32 = 0u;

    uint32_t graph_1d_samples_total_u32 = 0u;
    uint32_t graph_1d_packets_emitted_u32 = 0u;

    uint32_t khan_pages_seen_u32 = 0u;
    uint32_t khan_chars_ingested_u32 = 0u;
};

struct MathMetricVector {
    int64_t v_q32_32[genesis::GENESIS_METRIC_DIM_MAX] = {0};
    uint32_t dim_u32 = 0u;
};

class MathFoundation {
public:
    MathFoundation();

    // Initialize deterministic internal test cases and baseline goals.
    // Returns true if any tasks can be formed.
    bool bootstrap_defaults(std::string* out_report);

    // Observe ingested crawl text (already sanitized to ASCII-ish) and update
    // coverage stats for Khan Academy math.
    void observe_crawl_text_khan_math(const std::string& host_utf8, const std::string& path_utf8, const std::string& text);

    // Emit sandbox experiments that visualize + validate math (graphs, pemdas)
    // using the probe lattice.
    void tick(::SubstrateManager* sm);

    // Produce metric vector for a given math checkpoint kind.
    genesis::MetricVector metrics_for_kind(genesis::MetricKind k) const;

    // Build a registry task for a kind.
    genesis::MetricTask make_task_for_kind(genesis::MetricKind k, uint64_t source_id_u64, uint32_t source_anchor_id_u32, uint32_t context_anchor_id_u32) const;

    const MathStats& stats() const { return stats_; }

    // Q32.32 helpers exposed for deterministic parsing/evaluation utilities.
    static int64_t q32_32_mul(int64_t a, int64_t b);
    static int64_t q32_32_div(int64_t a, int64_t b);

private:
    MathStats stats_;

    // Deterministic PEMDAS test bank expressed as ASCII expressions.
    std::vector<std::string> pemdas_exprs_;
    std::vector<int64_t> pemdas_expected_q32_32_;

    // Probe graph plan (1D). Uses y = ax + b as a baseline sanity case.
    // Expression is fixed (no runtime parsing required for checkpoint).
    int64_t graph_a_q32_32_ = 0;
    int64_t graph_b_q32_32_ = 0;

    // Throttle state so math experiments do not monopolize ticks.
    uint32_t rr_u32_ = 0u;

    static int64_t q32_32_from_i32(int32_t x) { return ((int64_t)x) << 32; }

    // Evaluate a restricted PEMDAS expression deterministically.
    // Grammar: integers, + - * /, parentheses, spaces.
    static bool eval_pemdas_expr_q32_32(const std::string& s, int64_t& out_q32_32);
};

} // namespace genesis
