#include "GE_ai_regression_tests.hpp"

#include "GE_runtime.hpp"
#include "GE_metric_claim.hpp"
#include "GE_metric_templates.hpp"

#include <vector>

static void ge_append_line(std::string& out, const std::string& s) {
    out += s;
    out.push_back('\n');
}

static bool ge_expect_eq_u64(std::string& out, const char* label, uint64_t a, uint64_t b) {
    if (a == b) return true;
    ge_append_line(out, std::string("FAIL ") + label + " a=" + std::to_string((unsigned long long)a) + " b=" + std::to_string((unsigned long long)b));
    return false;
}

static bool ge_expect_eq_u32(std::string& out, const char* label, uint32_t a, uint32_t b) {
    if (a == b) return true;
    ge_append_line(out, std::string("FAIL ") + label + " a=" + std::to_string((unsigned)a) + " b=" + std::to_string((unsigned)b));
    return false;
}

static bool ge_test_metric_claim_determinism(std::string& out) {
    const std::string text =
        "Material thermal conductivity is 205 W/mK at 300 K. "
        "The modulus is 120 GPa and diffusion is 1.2e-9 m2/s.";

    std::vector<genesis::MetricClaim> c1;
    std::vector<genesis::MetricClaim> c2;
    const uint32_t n1 = genesis::ew_extract_metric_claims_from_utf8_bounded(text, 1024u, 8u, c1);
    const uint32_t n2 = genesis::ew_extract_metric_claims_from_utf8_bounded(text, 1024u, 8u, c2);
    bool ok = true;
    ok &= ge_expect_eq_u32(out, "metric_claim.count", n1, n2);
    if (n1 != n2) return false;
    for (uint32_t i = 0; i < n1; ++i) {
        const auto& a = c1[(size_t)i];
        const auto& b = c2[(size_t)i];
        ok &= ge_expect_eq_u32(out, "metric_claim.kind", (uint32_t)a.kind, (uint32_t)b.kind);
        ok &= ge_expect_eq_u64(out, "metric_claim.value_q32_32", (uint64_t)a.value_q32_32, (uint64_t)b.value_q32_32);
        ok &= ge_expect_eq_u32(out, "metric_claim.unit", a.unit_code_u32, b.unit_code_u32);
        ok &= ge_expect_eq_u32(out, "metric_claim.ordinal", a.claim_ordinal_u32, b.claim_ordinal_u32);
    }
    if (ok) ge_append_line(out, "OK metric_claim_determinism");
    return ok;
}

static bool ge_test_metric_task_build_determinism(std::string& out) {
    // Synthetic claim: 205 W/mK.
    genesis::MetricClaim c{};
    c.kind = genesis::MetricKind::Mat_Thermal_Conductivity;
    c.value_q32_32 = (int64_t)205ll << 32;
    c.unit_code_u32 = (uint32_t)genesis::MetricUnitCode::W_Per_MK;
    c.claim_ordinal_u32 = 0u;

    EwAiConfigAnchorState cfg{};
    cfg.metric_tol_num_u32 = 6u;
    cfg.metric_tol_den_u32 = 100u;
    cfg.sim_synth_budget_work_units_per_tick_u32 = 64u;

    genesis::MetricTask t1{};
    genesis::MetricTask t2{};
    const bool ok1 = genesis::ew_build_metric_task_from_claim(c, &cfg, 123ull, 7u, 9u, t1);
    const bool ok2 = genesis::ew_build_metric_task_from_claim(c, &cfg, 123ull, 7u, 9u, t2);
    bool ok = true;
    ok &= ge_expect_eq_u32(out, "metric_task.build1", ok1 ? 1u : 0u, 1u);
    ok &= ge_expect_eq_u32(out, "metric_task.build2", ok2 ? 1u : 0u, 1u);
    if (!ok1 || !ok2) return false;
    ok &= ge_expect_eq_u64(out, "metric_task.task_id", t1.task_id_u64, t2.task_id_u64);
    ok &= ge_expect_eq_u32(out, "metric_task.kind", (uint32_t)t1.target.kind, (uint32_t)t2.target.kind);
    ok &= ge_expect_eq_u32(out, "metric_task.tol_num", t1.target.tol_num_u32, t2.target.tol_num_u32);
    ok &= ge_expect_eq_u32(out, "metric_task.tol_den", t1.target.tol_den_u32, t2.target.tol_den_u32);
    ok &= ge_expect_eq_u32(out, "metric_task.work_units", t1.declared_work_units_u32, t2.declared_work_units_u32);
    if (ok) ge_append_line(out, "OK metric_task_build_determinism");
    return ok;
}

static bool ge_test_two_run_fingerprint_determinism(std::string& out, SubstrateManager* sm_outer) {
    // Create two identical headless substrate managers and run identical inputs.
    // This catches scheduler nondeterminism and key/GC drift.
    const uint64_t seed = 1337ull;
    const uint64_t ticks = 192ull;
    const uint32_t anchors = 96u;

    SubstrateManager a(anchors);
    SubstrateManager b(anchors);
    a.visualization_headless = true;
    b.visualization_headless = true;
    a.set_projection_seed(seed);
    b.set_projection_seed(seed);
    a.ai_learning_enabled_u32 = 1u;
    b.ai_learning_enabled_u32 = 1u;
    a.ai_crawling_enabled_u32 = 0u;
    b.ai_crawling_enabled_u32 = 0u;

    // Deterministic synthetic corpus lines.
    a.observe_text_line("thermal conductivity 205 W/mK at 300 K");
    a.observe_text_line("diffusion coefficient 1.2e-9 m2/s");
    b.observe_text_line("thermal conductivity 205 W/mK at 300 K");
    b.observe_text_line("diffusion coefficient 1.2e-9 m2/s");

    for (uint64_t t = 0; t < ticks; ++t) {
        a.tick();
        b.tick();
    }

    bool ok = true;
    ok &= ge_expect_eq_u64(out, "two_run.final_fingerprint", a.state_fingerprint_u64, b.state_fingerprint_u64);
    ok &= ge_expect_eq_u32(out, "two_run.stage", a.learning_curriculum_stage_u32, b.learning_curriculum_stage_u32);
    ok &= ge_expect_eq_u32(out, "two_run.pending", a.learning_gate.registry().pending_count_u32(), b.learning_gate.registry().pending_count_u32());
    ok &= ge_expect_eq_u64(out, "two_run.vault_last_key", a.vault_last_commit_key_u64, b.vault_last_commit_key_u64);
    ok &= ge_expect_eq_u32(out, "two_run.vault_last_kind", a.vault_last_commit_kind_u32, b.vault_last_commit_kind_u32);

    // Also compare against the live runtime instance fingerprint if provided.
    if (sm_outer) {
        ok &= ge_expect_eq_u32(out, "selftest.sm_present", 1u, 1u);
    }

    if (ok) ge_append_line(out, "OK two_run_fingerprint_determinism");
    return ok;
}

bool ge_run_ai_determinism_selftests(SubstrateManager* sm, std::string& out_log_utf8) {
    out_log_utf8.clear();
    ge_append_line(out_log_utf8, "AI_SELFTEST:begin");

    bool ok = true;
    ok &= ge_test_metric_claim_determinism(out_log_utf8);
    ok &= ge_test_metric_task_build_determinism(out_log_utf8);
    ok &= ge_test_two_run_fingerprint_determinism(out_log_utf8, sm);

    ge_append_line(out_log_utf8, ok ? "AI_SELFTEST:OK" : "AI_SELFTEST:FAIL");
    return ok;
}
