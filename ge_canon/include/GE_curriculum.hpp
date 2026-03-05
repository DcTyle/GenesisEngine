#pragma once

#include <cstdint>

#include "GE_metric_registry.hpp"

namespace genesis {

// -----------------------------------------------------------------------------
// Curriculum Stage Table + Exact 128-bit MetricKind Mask
//
// This file is the canonical source for:
//  - GENESIS_CURRICULUM_STAGE_COUNT
//  - Stage required MetricKind lists
//  - Exact required/accepted masks (no id&63 collisions)
//
// Determinism rules:
//  - Stage definitions are compile-time constants.
//  - Mask operations are stable across platforms.
// -----------------------------------------------------------------------------

static constexpr uint32_t GENESIS_CURRICULUM_STAGE_COUNT = 7u;

struct EwMask128 {
    uint64_t lo_u64 = 0ull;
    uint64_t hi_u64 = 0ull;
};

static inline void ew_mask128_set_bit(EwMask128* m, uint32_t bit_u32) {
    if (!m) return;
    if (bit_u32 < 64u) m->lo_u64 |= (1ull << bit_u32);
    else if (bit_u32 < 128u) m->hi_u64 |= (1ull << (bit_u32 - 64u));
}

static inline void ew_mask128_set_metric(EwMask128* m, MetricKind k) {
    ew_mask128_set_bit(m, (uint32_t)k);
}

static inline bool ew_mask128_test_bit(EwMask128 m, uint32_t bit_u32) {
    if (bit_u32 < 64u) return (m.lo_u64 & (1ull << bit_u32)) != 0ull;
    if (bit_u32 < 128u) return (m.hi_u64 & (1ull << (bit_u32 - 64u))) != 0ull;
    return false;
}

static inline EwMask128 ew_mask128_and(EwMask128 a, EwMask128 b) {
    EwMask128 o{};
    o.lo_u64 = a.lo_u64 & b.lo_u64;
    o.hi_u64 = a.hi_u64 & b.hi_u64;
    return o;
}

static inline EwMask128 ew_mask128_and_not(EwMask128 a, EwMask128 b) {
    EwMask128 o{};
    o.lo_u64 = a.lo_u64 & (~b.lo_u64);
    o.hi_u64 = a.hi_u64 & (~b.hi_u64);
    return o;
}

static inline bool ew_mask128_eq(EwMask128 a, EwMask128 b) {
    return (a.lo_u64 == b.lo_u64) && (a.hi_u64 == b.hi_u64);
}

static inline uint32_t ew_popcount_u64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_popcountll((unsigned long long)x);
#else
    // Portable popcount (deterministic).
    uint32_t c = 0u;
    while (x) { x &= (x - 1ull); ++c; }
    return c;
#endif
}

static inline uint32_t ew_mask128_popcount(EwMask128 m) {
    return ew_popcount_u64(m.lo_u64) + ew_popcount_u64(m.hi_u64);
}

struct EwCurriculumStageDef {
    uint32_t stage_id_u32 = 0u;
    const char* name_ascii = "";

    // Parallel exploration cap for allowlist lanes at this stage.
    uint32_t allowlist_lane_max_u32 = 1u;

    // Required measurable checkpoints to advance.
    MetricKind required_kinds[16] = { MetricKind::Unknown };
    uint32_t required_count_u32 = 0u;
};

// Canonical stage table.
static inline const EwCurriculumStageDef* ew_curriculum_table() {
    // NOTE: The array lives in static storage.
    static const EwCurriculumStageDef k_table[GENESIS_CURRICULUM_STAGE_COUNT] = {
        // Stage 0: Foundations (Language + SpeechBoot + Math)
        {0u, "Foundations", 1u,
            {
                MetricKind::Lang_Dictionary_LexiconStats,
                MetricKind::Lang_Thesaurus_RelationStats,
                MetricKind::Lang_Encyclopedia_ConceptStats,
                MetricKind::Lang_SpeechCorpus_AlignmentStats,

                MetricKind::Lang_SpeechBoot_VocabSize,
                MetricKind::Lang_SpeechBoot_SplitStability,
                MetricKind::Lang_SpeechBoot_IntentParsePass,

                MetricKind::Math_Pemdas_PrecedenceStats,
                MetricKind::Math_Graph_1D_ProbeStats,
                MetricKind::Math_KhanAcademy_CoverageStats,

                MetricKind::Unknown
            },
            10u},

        // Stage 1: Physics / Quantum
        {1u, "Physics", 2u,
            {
                MetricKind::Qm_DoubleSlit_Fringes,
                MetricKind::Qm_ParticleInBox_Levels,
                MetricKind::Qm_HarmonicOsc_Spacing,
                MetricKind::Qm_Tunneling_Transmission,
                MetricKind::Unknown
            },
            4u},

        // Stage 2: Atoms + Chemistry
        {2u, "AtomsChem", 2u,
            {
                MetricKind::Atom_Orbital_EnergyRatios,
                MetricKind::Atom_Orbital_RadialNodes,
                MetricKind::Bond_Length_Equilibrium,
                MetricKind::Bond_Vibration_Frequency,
                MetricKind::Chem_ReactionRate_Temp,
                MetricKind::Chem_Equilibrium_Constant,
                MetricKind::Chem_Diffusion_Coefficient,
                MetricKind::Unknown
            },
            7u},

        // Stage 3: Materials
        {3u, "Materials", 3u,
            {
                MetricKind::Mat_Thermal_Conductivity,
                MetricKind::Mat_Electrical_Conductivity,
                MetricKind::Mat_StressStrain_Modulus,
                MetricKind::Mat_PhaseChange_Threshold,
                MetricKind::Unknown
            },
            4u},

        // Stage 4: Cosmology / Atmospheres
        {4u, "Cosmology", 2u,
            {
                MetricKind::Cosmo_Orbit_Period,
                MetricKind::Cosmo_Radiation_Spectrum,
                MetricKind::Cosmo_Atmos_PressureProfile,
                MetricKind::Unknown
            },
            3u},

        // Stage 5: Biology (deferred)
        {5u, "Biology", 1u,
            {
                MetricKind::Bio_CellDiffusion_Osmosis,
                MetricKind::Unknown
            },
            1u},

        // Stage 6: Game engine bootstrap
        {6u, "Game", 1u,
            {
                MetricKind::Game_RenderPipeline_Determinism,
                MetricKind::Game_SceneGraph_TransformConsistency,
                MetricKind::Game_EditorHook_CommandSurface,
                MetricKind::Unknown
            },
            3u},
    };
    return k_table;
}

static inline const EwCurriculumStageDef* ew_curriculum_stage_def(uint32_t stage_u32) {
    const EwCurriculumStageDef* t = ew_curriculum_table();
    if (!t) return nullptr;
    if (stage_u32 >= GENESIS_CURRICULUM_STAGE_COUNT) return &t[GENESIS_CURRICULUM_STAGE_COUNT - 1u];
    return &t[stage_u32];
}

static inline const char* ew_curriculum_stage_name_ascii(uint32_t stage_u32) {
    const EwCurriculumStageDef* d = ew_curriculum_stage_def(stage_u32);
    return d ? d->name_ascii : "";
}

static inline uint32_t ew_curriculum_stage_allowlist_lane_max_u32(uint32_t stage_u32) {
    const EwCurriculumStageDef* d = ew_curriculum_stage_def(stage_u32);
    return d ? d->allowlist_lane_max_u32 : 1u;
}

static inline uint32_t ew_curriculum_stage_required_count_u32(uint32_t stage_u32) {
    const EwCurriculumStageDef* d = ew_curriculum_stage_def(stage_u32);
    return d ? d->required_count_u32 : 0u;
}

static inline EwMask128 ew_curriculum_stage_required_mask128(uint32_t stage_u32) {
    EwMask128 m{};
    const EwCurriculumStageDef* d = ew_curriculum_stage_def(stage_u32);
    if (!d) return m;
    for (uint32_t i = 0u; i < d->required_count_u32 && i < 16u; ++i) {
        const uint32_t kid = (uint32_t)d->required_kinds[i];
        if (kid < 128u) ew_mask128_set_bit(&m, kid);
    }
    return m;
}

// MetricKind -> stable ASCII name for UI/logging.
static inline const char* ew_metric_kind_name_ascii(MetricKind k) {
    switch (k) {
        case MetricKind::Lang_Dictionary_LexiconStats: return "Lang_Dictionary_LexiconStats";
        case MetricKind::Lang_Thesaurus_RelationStats: return "Lang_Thesaurus_RelationStats";
        case MetricKind::Lang_Encyclopedia_ConceptStats: return "Lang_Encyclopedia_ConceptStats";
        case MetricKind::Lang_SpeechCorpus_AlignmentStats: return "Lang_SpeechCorpus_AlignmentStats";
        case MetricKind::Lang_SpeechBoot_VocabSize: return "Lang_SpeechBoot_VocabSize";
        case MetricKind::Lang_SpeechBoot_SplitStability: return "Lang_SpeechBoot_SplitStability";
        case MetricKind::Lang_SpeechBoot_IntentParsePass: return "Lang_SpeechBoot_IntentParsePass";

        case MetricKind::Math_Pemdas_PrecedenceStats: return "Math_Pemdas_PrecedenceStats";
        case MetricKind::Math_Graph_1D_ProbeStats: return "Math_Graph_1D_ProbeStats";
        case MetricKind::Math_KhanAcademy_CoverageStats: return "Math_KhanAcademy_CoverageStats";

        case MetricKind::Qm_DoubleSlit_Fringes: return "Qm_DoubleSlit_Fringes";
        case MetricKind::Qm_ParticleInBox_Levels: return "Qm_ParticleInBox_Levels";
        case MetricKind::Qm_HarmonicOsc_Spacing: return "Qm_HarmonicOsc_Spacing";
        case MetricKind::Qm_Tunneling_Transmission: return "Qm_Tunneling_Transmission";

        case MetricKind::Atom_Orbital_EnergyRatios: return "Atom_Orbital_EnergyRatios";
        case MetricKind::Atom_Orbital_RadialNodes: return "Atom_Orbital_RadialNodes";
        case MetricKind::Bond_Length_Equilibrium: return "Bond_Length_Equilibrium";
        case MetricKind::Bond_Vibration_Frequency: return "Bond_Vibration_Frequency";

        case MetricKind::Chem_ReactionRate_Temp: return "Chem_ReactionRate_Temp";
        case MetricKind::Chem_Equilibrium_Constant: return "Chem_Equilibrium_Constant";
        case MetricKind::Chem_Diffusion_Coefficient: return "Chem_Diffusion_Coefficient";

        case MetricKind::Mat_Thermal_Conductivity: return "Mat_Thermal_Conductivity";
        case MetricKind::Mat_Electrical_Conductivity: return "Mat_Electrical_Conductivity";
        case MetricKind::Mat_StressStrain_Modulus: return "Mat_StressStrain_Modulus";
        case MetricKind::Mat_PhaseChange_Threshold: return "Mat_PhaseChange_Threshold";

        case MetricKind::Cosmo_Orbit_Period: return "Cosmo_Orbit_Period";
        case MetricKind::Cosmo_Radiation_Spectrum: return "Cosmo_Radiation_Spectrum";
        case MetricKind::Cosmo_Atmos_PressureProfile: return "Cosmo_Atmos_PressureProfile";

        case MetricKind::Bio_CellDiffusion_Osmosis: return "Bio_CellDiffusion_Osmosis";

        case MetricKind::Game_RenderPipeline_Determinism: return "Game_RenderPipeline_Determinism";
        case MetricKind::Game_SceneGraph_TransformConsistency: return "Game_SceneGraph_TransformConsistency";
        case MetricKind::Game_EditorHook_CommandSurface: return "Game_EditorHook_CommandSurface";

        default: return "Unknown";
    }
}

} // namespace genesis
