#include "GE_metric_templates.hpp"

namespace genesis {

static const MetricTemplateDesc k_templates[] = {
    { MetricKind::Math_Pemdas_PrecedenceStats, "pemdas", 8u, "/exp pemdas_objects micro=256 expr=(2+3)*4 tag=1", 2u },
    { MetricKind::Math_Graph_1D_ProbeStats, "graph_1d", 8u, "/exp graph_1d micro=256 a=2 b=1 xmin=-8 xmax=8 samples=129 tag=1", 2u },

    { MetricKind::Qm_ParticleInBox_Levels, "qm_well", 16u, "/exp qm_well micro=512 tag=1", 2u },
    { MetricKind::Qm_HarmonicOsc_Spacing, "qm_well", 16u, "/exp qm_well micro=512 tag=1", 2u },
    { MetricKind::Qm_Tunneling_Transmission, "qm_well", 16u, "/exp qm_well micro=512 tag=1", 2u },
    { MetricKind::Qm_DoubleSlit_Fringes, "double_slit", 16u, "/exp probe_isolate micro=512 pattern=ring radius=12 tag=1", 2u },

    { MetricKind::Chem_Diffusion_Coefficient, "diffuse", 16u, "/exp diffuse micro=512 tag=1", 2u },
    { MetricKind::Chem_ReactionRate_Temp, "field_relax", 16u, "/exp field_relax micro=512 tag=1", 2u },
    { MetricKind::Chem_Equilibrium_Constant, "field_relax", 16u, "/exp field_relax micro=512 tag=1", 2u },

    { MetricKind::Mat_Thermal_Conductivity, "mat_k", 32u, "/exp probe_isolate micro=512 pattern=cross radius=10 tag=1", 2u },
    { MetricKind::Mat_Electrical_Conductivity, "mat_sigma", 32u, "/exp probe_isolate micro=512 pattern=cross radius=10 tag=1", 2u },
    { MetricKind::Mat_StressStrain_Modulus, "mat_modulus", 32u, "/exp probe_isolate micro=512 pattern=cross radius=10 tag=1", 2u },
    { MetricKind::Mat_PhaseChange_Threshold, "mat_phase", 32u, "/exp probe_isolate micro=512 pattern=cross radius=10 tag=1", 2u },

    { MetricKind::Cosmo_Orbit_Period, "orbit", 16u, "/exp field_relax micro=512 tag=1", 2u },
    { MetricKind::Cosmo_Radiation_Spectrum, "spectrum", 16u, "/exp field_relax micro=512 tag=1", 2u },
    { MetricKind::Cosmo_Atmos_PressureProfile, "pressure", 16u, "/exp field_relax micro=512 tag=1", 2u },

    { MetricKind::Bio_CellDiffusion_Osmosis, "osmosis", 16u, "/exp diffuse micro=512 tag=1", 2u },

    { MetricKind::Game_RenderPipeline_Determinism, "game_det", 8u, "", 2u },
    { MetricKind::Game_SceneGraph_TransformConsistency, "scene_xform", 8u, "", 2u },
    { MetricKind::Game_EditorHook_CommandSurface, "editor_cmd", 8u, "", 2u },
};

const MetricTemplateDesc* ew_metric_template_for_kind(MetricKind k) {
    for (size_t i = 0; i < sizeof(k_templates) / sizeof(k_templates[0]); ++i) {
        if (k_templates[i].kind == k) return &k_templates[i];
    }
    return nullptr;
}

bool ew_build_metric_task_from_claim(
    const MetricClaim& claim,
    const EwAiConfigAnchorState* cfg,
    uint64_t source_id_u64,
    uint32_t source_anchor_id_u32,
    uint32_t context_anchor_id_u32,
    MetricTask& out_task
) {
    const MetricTemplateDesc* t = ew_metric_template_for_kind(claim.kind);
    if (!t) return false;

    MetricTask task{};
    task.source_id_u64 = source_id_u64;
    task.source_anchor_id_u32 = source_anchor_id_u32;
    task.context_anchor_id_u32 = context_anchor_id_u32;

    task.target.kind = claim.kind;
    task.target.target.dim_u32 = t->target_dim_u32;
    task.target.target.v_q32_32[0] = claim.value_q32_32;

    // Unit code stored as integer<<32 in Q32.32.
    task.target.target.v_q32_32[1] = (int64_t)((uint64_t)claim.unit_code_u32 << 32);

    task.target.tol_num_u32 = cfg ? cfg->metric_tol_num_u32 : 6u;
    task.target.tol_den_u32 = cfg ? cfg->metric_tol_den_u32 : 100u;

    task.has_claim_u32 = 1u;
    task.claim = claim;
    task.declared_work_units_u32 = t->worst_case_work_units_u32;

    out_task = task;
    return true;
}

std::string ew_exp_line_for_metric_kind(MetricKind k) {
    const MetricTemplateDesc* t = ew_metric_template_for_kind(k);
    if (!t) return std::string();
    if (!t->exp_line_ascii) return std::string();
    return std::string(t->exp_line_ascii);
}

} // namespace genesis
