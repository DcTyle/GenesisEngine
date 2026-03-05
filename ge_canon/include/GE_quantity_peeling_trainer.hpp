#pragma once

#include <cstdint>
#include <vector>

#include "GE_corpus_anchor_store.hpp"
#include "GE_overlap_router.hpp"
#include "GE_coherence_graph_store.hpp"
#include "GE_lane_threshold_schedule.hpp"
#include "GE_learning_checkpoint_gate.hpp"
#include "GE_safety_governor.hpp"

struct GE_TrainerParams {
    uint32_t topk_u32 = 16;
    uint32_t max_degree_u32 = 64;

    uint32_t epoch_u32 = 0;

    bool use_cuda_scoring = true;

    int64_t step_q32_32 = (int64_t(1) << 28); // 1/16

    // If thresholds are provided, per-lane rel_err_max is taken from schedule for epoch.
    // Otherwise accept_rel_err_max_q32_32 is used.
    int64_t accept_rel_err_max_q32_32 = (int64_t(1) << 30); // 0.25
    const GE_AllLaneThresholds* opt_thresholds = nullptr;

    // Optional safety caps (fan-out, materialization budgets).
    GE_SafetyCaps safety_caps = GE_default_safety_caps();
};

struct GE_TrainerStats {
    uint64_t proposals_u64 = 0;
    uint64_t accepted_u64 = 0;
    uint64_t edge_writes_u64 = 0;
    int64_t last_rel_err_q32_32 = 0;
};

bool GE_trainer_epoch(GE_CorpusAnchorStore& store,
                      GE_CoherenceGraphStore& graph,
                      genesis::LearningCheckpointGate& gate,
                      const GE_TrainerParams& params,
                      GE_TrainerStats& io_stats);
