#pragma once
#include <cstdint>
#include <string>

class SubstrateManager;

// Deterministic curriculum manager that drives ingest/training scheduling.
// Does NOT "skip" stages; advances only based on accepted measurable checkpoints.
struct GE_CurriculumManager {
    uint32_t stage_u32 = 0; // 0..5
    uint64_t epoch_u64 = 0;

    // Scheduling knobs (deterministic, can be configured later).
    uint32_t ingest_lane_mask_u32 = 0;
    uint32_t train_lane_mask_u32 = 0;

    // Observability counters.
    uint64_t last_stage_tick_u64 = 0;
    uint64_t stage_advances_u64 = 0;

    void tick(SubstrateManager* sm);

private:
    bool stage0_ready_(SubstrateManager* sm) const;
    void update_lane_masks_(SubstrateManager* sm);
    void maybe_advance_(SubstrateManager* sm);
};
