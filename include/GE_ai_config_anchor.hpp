#pragma once

#include <cstdint>

// -----------------------------------------------------------------------------
// AI Config Anchor
//
// Canonical, deterministic knobs for AI gating and budgets.
//
// NOTE: These are not “tweak live by poking globals.” Changes must be applied
// via control packets so they can be replayed and included in the state
// fingerprint.
// -----------------------------------------------------------------------------

// Field IDs for AiConfigSet control packet (stable, non-versioned).
static const uint32_t EW_AI_CFG_FIELD_RESONANCE_GATE_Q15 = 1u;
static const uint32_t EW_AI_CFG_FIELD_METRIC_TOL_NUM_U32 = 2u;
static const uint32_t EW_AI_CFG_FIELD_METRIC_TOL_DEN_U32 = 3u;
static const uint32_t EW_AI_CFG_FIELD_MAX_METRIC_TASKS_PER_TICK_U32 = 4u;
static const uint32_t EW_AI_CFG_FIELD_EPHEMERAL_TTL_TICKS_U64 = 5u;
static const uint32_t EW_AI_CFG_FIELD_EPHEMERAL_GC_STRIDE_TICKS_U32 = 6u;
static const uint32_t EW_AI_CFG_FIELD_MAX_EPHEMERAL_COUNT_U32 = 7u;
static const uint32_t EW_AI_CFG_FIELD_CRAWL_BUDGET_BYTES_PER_TICK_U32 = 8u;
static const uint32_t EW_AI_CFG_FIELD_CRAWLER_MAX_PULSES_PER_TICK_U32 = 9u;
static const uint32_t EW_AI_CFG_FIELD_SIM_SYNTH_BUDGET_WORK_UNITS_PER_TICK_U32 = 10u;
static const uint32_t EW_AI_CFG_FIELD_MAX_METRIC_CLAIMS_PER_PAGE_U32 = 11u;
static const uint32_t EW_AI_CFG_FIELD_METRIC_CLAIM_TEXT_CAP_BYTES_U32 = 12u;
static const uint32_t EW_AI_CFG_FIELD_REPO_READER_ENABLED_U32 = 13u;
static const uint32_t EW_AI_CFG_FIELD_REPO_READER_FILES_PER_TICK_U32 = 14u;
static const uint32_t EW_AI_CFG_FIELD_REPO_READER_BYTES_PER_FILE_U32 = 15u;
static const uint32_t EW_AI_CFG_FIELD_AI_EVENT_LOG_ENABLED_U32 = 16u;


struct EwAiConfigAnchorState {
    // Topic-mask resonance gate for committing exploratory pages.
    // Q0.15, where 1.0 is 32768.
    uint16_t resonance_gate_q15 = 31457u; // ~0.96
    uint16_t pad0_u16 = 0u;

    // Metric acceptance tolerance as a fraction: tol_num/tol_den.
    uint32_t metric_tol_num_u32 = 6u;
    uint32_t metric_tol_den_u32 = 100u;

    // Total number of MetricTask completions processed per substrate tick.
    uint32_t max_metric_tasks_per_tick_u32 = 2u;

    // Ephemeral vault GC settings.
    uint64_t ephemeral_ttl_ticks_u64 = 21600ull; // ~60s at 360 Hz
    uint32_t ephemeral_gc_stride_ticks_u32 = 360u;
    uint32_t max_ephemeral_count_u32 = 256u;

    // Crawl/ingest budget.
    uint32_t crawl_budget_bytes_per_tick_u32 = (256u * 1024u);
    uint32_t crawler_max_pulses_per_tick_u32 = 64u;
    // Simulation synthesis budget (abstract work units per substrate tick).
    uint32_t sim_synth_budget_work_units_per_tick_u32 = 4096u;

    // Metric claim extraction bounds for crawler pages.
    uint32_t max_metric_claims_per_page_u32 = 4u;
    uint32_t metric_claim_text_cap_bytes_u32 = 16384u;


// RepoReaderAdapter (opt-in self-reading; stage-gated).
uint32_t repo_reader_enabled_u32 = 0u;
uint32_t repo_reader_files_per_tick_u32 = 2u;
uint32_t repo_reader_bytes_per_file_u32 = 8192u;

    // Structured AI event logs (single-line). 1=enabled, 0=disabled.
    uint32_t ai_event_log_enabled_u32 = 1u;

};
