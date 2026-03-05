#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Metric Registry + Checkpoint Tasks
//
// Deterministic registry for measurable metric targets and queued validation
// tasks. Used by the learning loop to form-fit simulation parameters against
// measurable targets (honesty gate).
//
// Canonical rules:
// - Only simulate/advance for objects/experiments/events with measurable metrics.
// - Default acceptance tolerance is 6% relative error per metric.
// - DO NOT early-exit on reaching tolerance: keep searching until the task's
//   compute budget (tries) is exhausted for the request window.
// - Deterministic ordering: FIFO task order; deterministic candidate sampling.
// -----------------------------------------------------------------------------

namespace genesis {

static constexpr uint32_t GENESIS_METRIC_DIM_MAX = 8;

enum class MetricKind : uint32_t {
    Unknown = 0,

    // ---------------------------------------------------------------------
    // Curriculum-measurable experiment/phenomenon IDs.
    // These are used by the checkpoint gate to enforce parallel-sequential
    // learning. Each kind MUST correspond to measurable metrics that can be
    // validated in the sandbox within the 6% tolerance gate.
    // ---------------------------------------------------------------------

    // ---------------------------------------------------------------------
    // Language foundations measurable set (Stage 0).
    // These are structural correctness/coverage checkpoints derived from
    // authoritative corpora (dictionary/thesaurus/encyclopedia/speech).
    // ---------------------------------------------------------------------
    Lang_Dictionary_LexiconStats = 100,
    Lang_Thesaurus_RelationStats = 101,
    Lang_Encyclopedia_ConceptStats = 102,
    Lang_SpeechCorpus_AlignmentStats = 103,

    // ---------------------------------------------------------------------
    // Math foundations measurable set (Stage 0, parallel with language).
    // These checkpoints are validated in the sandbox lattice (graphs, operator
    // precedence, and basic algebra consistency) and via curriculum-scoped
    // crawler coverage for Khan Academy math.
    // ---------------------------------------------------------------------
    Math_Pemdas_PrecedenceStats = 104,
    Math_Graph_1D_ProbeStats = 105,
    Math_KhanAcademy_CoverageStats = 106,

    // Quantum mechanics measurable set (Stage 0).
    Qm_DoubleSlit_Fringes = 10,
    Qm_ParticleInBox_Levels = 11,
    Qm_HarmonicOsc_Spacing = 12,
    Qm_Tunneling_Transmission = 13,

    // Orbitals/atoms/bonds measurable set (Stage 1 bridge).
    Atom_Orbital_EnergyRatios = 20,
    Atom_Orbital_RadialNodes = 21,
    Bond_Length_Equilibrium = 22,
    Bond_Vibration_Frequency = 23,

    // Chemistry measurable set (Stage 1/2).
    Chem_ReactionRate_Temp = 30,
    Chem_Equilibrium_Constant = 31,
    Chem_Diffusion_Coefficient = 32,

    // Materials / physical science measurable set (Stage 2/3).
    Mat_Thermal_Conductivity = 40,
    Mat_Electrical_Conductivity = 41,
    Mat_StressStrain_Modulus = 42,
    Mat_PhaseChange_Threshold = 43,

    // Cosmology / atmospheres measurable set (Stage 3/4).
    Cosmo_Orbit_Period = 50,
    Cosmo_Radiation_Spectrum = 51,
    Cosmo_Atmos_PressureProfile = 52,

// Game engine bootstrap measurable set (Stage 6).
Game_RenderPipeline_Determinism = 70,
Game_SceneGraph_TransformConsistency = 71,
Game_EditorHook_CommandSurface = 72,

    // Biology measurable set (Stage 5, deferred).
    Bio_CellDiffusion_Osmosis = 60,
};

struct MetricVector {
    // Q32.32 fixed-point values.
    int64_t v_q32_32[GENESIS_METRIC_DIM_MAX] = {0};
    uint32_t dim_u32 = 0;
};

struct MetricTarget {
    MetricKind kind = MetricKind::Unknown;
    MetricVector target;
    // Default 6% tolerance unless overridden.
    uint32_t tol_num_u32 = 6;
    uint32_t tol_den_u32 = 100;
};

struct MetricTask {
    uint64_t task_id_u64 = 0;

    // Provenance keys (minimal identity; not cryptographic).
    uint64_t source_id_u64 = 0;
    uint32_t source_anchor_id_u32 = 0;
    uint32_t context_anchor_id_u32 = 0;

    MetricTarget target;

    // Search budget expressed as conceptual "tries" (object updates).
    uint64_t tries_remaining_u64 = 0;
    uint32_t tries_per_step_u32 = 0;
    // One request window in ticks (e.g. 360 ticks ~= 1 sec at 360 Hz).
    uint32_t ticks_remaining_u32 = 0;

    // Best-so-far tracking.
    int64_t best_err_q32_32 = (1LL << 32);
    MetricVector best_sim;
    MetricVector best_params;

    // Deterministic control flags.
    // If false, the learning gate performs an initial "reverse-calc" attempt
    // (parameter-driven lattice molding) before stochastic exploration.
    bool first_try_done = false;

    bool completed = false;
    bool accepted = false;

    // Tick when the task was completed (accepted or rejected). 0 indicates unset.
    // Determinism rule: recorded from SubstrateMicroprocessor::canonical_tick.
    uint64_t completed_tick_u64 = 0;
};

class MetricRegistry {
public:
    MetricRegistry();

    uint64_t enqueue_task(const MetricTask& t);

    bool has_pending() const { return !q_.empty(); }
    uint32_t pending_count_u32() const { return (uint32_t)q_.size(); }
    uint32_t completed_count_u32() const { return (uint32_t)completed_.size(); }
    MetricTask* front();
    void pop_front();

    void record_completed(const MetricTask& t);
    const std::vector<MetricTask>& completed() const { return completed_; }

    // Returns the count of accepted tasks whose completed_tick >= tick_min.
    // Used for deterministic curriculum stage advancement.
    uint64_t accepted_since_tick_u64(uint64_t tick_min_u64) const;

private:
    std::deque<MetricTask> q_;
    std::vector<MetricTask> completed_;
    uint64_t seq_u64_ = 1;
};

} // namespace genesis
