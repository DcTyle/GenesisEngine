#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "ew_id9.hpp"
#include "GE_metric_registry.hpp"

// Forward declaration.
class SubstrateManager;

namespace genesis {

// -----------------------------------------------------------------------------
// Learning automation artifacts (deterministic, substrate-native)
//
// This implements an event-driven automation loop that:
//  - respects curriculum progression and prerequisite ordering,
//  - runs topics in parallel (round-robin across tracks),
//  - emits crawl requests when data is missing (crawler must be enabled explicitly),
//  - emits evaluation-result artifacts when tasks complete.
//
// It does NOT perform filesystem writes. Hydration remains coherence-gated.
// -----------------------------------------------------------------------------

enum class AutoArtifactKind : uint32_t {
    Unknown      = 0,
    PlanPacket   = 1,
    AnchorSpec   = 2,
    CrawlRequest = 3,
    EvalRequest  = 4,
    EvalResult   = 5
};

struct AutoArtifact {
    EwId9 id9{};
    AutoArtifactKind kind = AutoArtifactKind::Unknown;
    uint64_t created_tick_u64 = 0;
    uint32_t lane_u32 = 0;
    std::string payload_utf8; // structured, small UTF-8 (JSON-ish), validated by coherence gate rules.
};

// A tiny, bounded bus that preserves deterministic ordering.
class AutoArtifactBus {
public:
    void push(const AutoArtifact& a);
    bool pop(AutoArtifact* out);
    uint32_t size_u32() const { return (uint32_t)q_.size(); }

private:
    std::deque<AutoArtifact> q_;
    uint64_t seq_u64_ = 1;
};

// Curriculum-aligned tracks. Multiple tracks may be active in parallel.
enum class LearningTrack : uint32_t {
    Vocabulary = 0,
    Math = 1,
    Quantum = 2,
    Cosmology = 3,
    Chemistry = 4,
    Biology = 5,
    Game = 6
};

struct TrackState {
    LearningTrack track = LearningTrack::Vocabulary;
    uint32_t sandbox_id_u32 = 0;
    uint32_t rr_budget_u32 = 0;
    uint64_t last_progress_tick_u64 = 0;
    uint64_t accepted_count_u64 = 0;
    uint64_t attempted_count_u64 = 0;
};

class LearningAutomation {
public:
    void init_once(SubstrateManager* sm);
    void tick(SubstrateManager* sm);

    void set_enabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }

    AutoArtifactBus& bus() { return bus_; }

public:

private:
    bool inited_ = false;
    bool enabled_ = true;

    AutoArtifactBus bus_;
    std::vector<TrackState> tracks_;

    // Cursor into MetricRegistry::completed() to emit EvalResult artifacts.
    uint32_t completed_cursor_u32_ = 0;

    void emit_eval_results(SubstrateManager* sm);
    void maybe_seed_plans(SubstrateManager* sm);
    void process_one_artifact(SubstrateManager* sm, const AutoArtifact& a);

    // Plan expansion
    void plan_expand_for_stage(SubstrateManager* sm, uint32_t stage_u32);

    // Scheduling helpers
    static bool track_prereqs_satisfied(SubstrateManager* sm, LearningTrack t);

    void schedule_parallel_tasks(SubstrateManager* sm, uint32_t stage_u32);
};

} // namespace genesis