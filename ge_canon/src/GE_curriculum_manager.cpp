#include "GE_curriculum_manager.hpp"
#include "GE_runtime.hpp"

static inline std::string u32_to_s(uint32_t v) { return std::to_string((uint32_t)v); }

bool GE_CurriculumManager::stage0_ready_(SubstrateMicroprocessor* sm) const {
    if (!sm) return false;
    const uint64_t req = sm->learning_stage_required_mask_u64[0];
    const uint64_t done = sm->learning_stage_completed_mask_u64[0];
    return (req != 0u) && ((done & req) == req);
}

void GE_CurriculumManager::update_lane_masks_(SubstrateMicroprocessor* sm) {
    // Deterministic lane masks:
    // stage0: lane8 dictionaries/encyclopedia + lane2 math sources (khan, etc)
    // stage1: lane2 physics + lane1 determinism/compilers
    // stage2: lane3 chemistry/materials
    // stage3: lane3 materials + lane6 robotics/control
    // stage4: lane2 cosmology/atmos + lane4 bio reference unlock pre-bio
    // stage5: lane4 bio + lane5 neuro + lane6 control
    // stage6: lane7 game engine bootstrap (render/scene/editor) + lane1 determinism/compilers + lane2 physics/math
    uint32_t m = 0u;
    if (stage_u32 == 0u) m = (1u<<8) | (1u<<2) | (1u<<0);
    else if (stage_u32 == 1u) m = (1u<<2) | (1u<<1);
    else if (stage_u32 == 2u) m = (1u<<3) | (1u<<2);
    else if (stage_u32 == 3u) m = (1u<<3) | (1u<<6);
    else if (stage_u32 == 4u) m = (1u<<2) | (1u<<4);
    else if (stage_u32 == 5u) m = (1u<<4) | (1u<<5) | (1u<<6);
    else m = (1u<<7) | (1u<<1) | (1u<<2);
    ingest_lane_mask_u32 = m;
    train_lane_mask_u32 = m;
    (void)sm;
}

void GE_CurriculumManager::maybe_advance_(SubstrateMicroprocessor* sm) {
    if (!sm) return;
    if (stage_u32 == 0u) {
        if (stage0_ready_(sm)) {
            stage_u32 = 1u;
            stage_advances_u64 += 1;
            last_stage_tick_u64 = sm->canonical_tick;
            sm->emit_ui_line("CURRICULUM_STAGE_ADVANCE:to=1 tick=" + std::to_string(sm->canonical_tick));
        }
        return;
    }
    // Later stages: require that at least one metric task in the bucket has been accepted since last advance.
    // This is conservative and deterministic. It avoids skipping by requiring measurable acceptance.
    const uint64_t accepted_since = sm->learning_gate.registry().accepted_since_tick_u64(last_stage_tick_u64);
    if (accepted_since == 0) return;

    // Hard prerequisites: biology (stage5) locked until stage4 achieved.
    if (stage_u32 < 6u) {
        stage_u32 += 1u;
        stage_advances_u64 += 1;
        last_stage_tick_u64 = sm->canonical_tick;
        sm->emit_ui_line("CURRICULUM_STAGE_ADVANCE:to=" + u32_to_s(stage_u32) + " tick=" + std::to_string(sm->canonical_tick));
    }
}

void GE_CurriculumManager::tick(SubstrateMicroprocessor* sm) {
    if (!sm) return;

    update_lane_masks_(sm);

    // Policy: offline-first corpus ingestion can run when enabled and when stage0 is active or later.
    // Live crawling requires explicit opt-in.
    if (sm->corpus_pipeline_enable_u32 != 0u) {
        // Schedule one training epoch per N ticks deterministically.
        const uint64_t n = (uint64_t)sm->corpus_epoch_period_ticks_u32;
        if (n != 0 && (sm->canonical_tick % n) == 0) {
            epoch_u64 += 1;
            sm->emit_ui_line("CORPUS_EPOCH_SCHEDULED:epoch=" + std::to_string(epoch_u64));
            // The actual ingest/trainer run is driven by tools or adapter; here we only publish the schedule.
        }
    }

    maybe_advance_(sm);
}
