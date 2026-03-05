#pragma once

#include <cstdint>
#include <string>

#include "GE_metric_claim.hpp"
#include "GE_metric_registry.hpp"
#include "GE_ai_config_anchor.hpp"

namespace genesis {

// -----------------------------------------------------------------------------
// Metric Templates
//
// A small registry that maps MetricKind -> deterministic sim/validation template.
// For now this provides:
//  - a recommended /exp line for visualization hooks
//  - declared worst-case work units for per-tick budget enforcement
//  - canonical MetricTask construction from a MetricClaim
// -----------------------------------------------------------------------------

struct MetricTemplateDesc {
    MetricKind kind = MetricKind::Unknown;
    const char* name_ascii = "";

    // Worst-case abstract work cost for this metric kind.
    // Used only for budgeting steps-per-tick (deterministic clamp).
    uint32_t worst_case_work_units_u32 = 64u;

    // Visualization proxy (optional) as an /exp line.
    const char* exp_line_ascii = "";

    // Target vector layout.
    // dim=2: [value_q32_32, unit_code<<32]
    uint32_t target_dim_u32 = 2u;
};

const MetricTemplateDesc* ew_metric_template_for_kind(MetricKind k);

// Build a MetricTask from a MetricClaim using the template registry.
// The output task is ready to enqueue into the MetricRegistry.
bool ew_build_metric_task_from_claim(
    const MetricClaim& claim,
    const EwAiConfigAnchorState* cfg,
    uint64_t source_id_u64,
    uint32_t source_anchor_id_u32,
    uint32_t context_anchor_id_u32,
    MetricTask& out_task
);

// Returns the recommended visualization /exp line for a metric kind.
// Empty string means no visualization.
std::string ew_exp_line_for_metric_kind(MetricKind k);

} // namespace genesis
