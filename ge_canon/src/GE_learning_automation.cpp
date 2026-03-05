#include "GE_learning_automation.hpp"

#include "GE_runtime.hpp"
#include "coherence_gate.hpp"
#include "inspector_fields.hpp"

#include <algorithm>
#include <cstdio>

namespace genesis {

void AutoArtifactBus::push(const AutoArtifact& a_in) {
    AutoArtifact a = a_in;
    // Deterministic id9 if caller didn't provide one.
    if (a.id9.u32[0] == 0u && a.id9.u32[1] == 0u && a.id9.u32[2] == 0u) {
        // NOTE: This is not cryptographic; it is a stable sequence labeling.
        a.id9.u32[0] = (uint32_t)(seq_u64_ & 0xFFFFFFFFu);
        a.id9.u32[1] = (uint32_t)((seq_u64_ >> 32) & 0xFFFFFFFFu);
        a.id9.u32[2] = (uint32_t)a.kind;
        seq_u64_++;
    }
    // Bounded queue (deterministic drop oldest).
    if (q_.size() >= 2048u) q_.pop_front();
    q_.push_back(a);
}

bool AutoArtifactBus::pop(AutoArtifact* out) {
    if (!out) return false;
    if (q_.empty()) return false;
    *out = q_.front();
    q_.pop_front();
    return true;
}

static inline bool is_commit_safe_small_utf8(const std::string& s) {
    // Reuse coherence gate screening rules (UTF-8 + control bytes) to keep artifacts clean.
    EwCoherenceResult r = EwCoherenceGate::validate_artifact("automation.txt", EW_ARTIFACT_TEXT, s);
    return r.denial_code_u32 == 0u;
}

// -----------------------------------------------------------------------------
// Track mapping
// -----------------------------------------------------------------------------

static LearningTrack track_for_metric(MetricKind k) {
    switch (k) {
        case MetricKind::Lang_Dictionary_LexiconStats:
        case MetricKind::Lang_Thesaurus_RelationStats:
        case MetricKind::Lang_Encyclopedia_ConceptStats:
        case MetricKind::Lang_SpeechCorpus_AlignmentStats:
            return LearningTrack::Vocabulary;

        case MetricKind::Math_Pemdas_PrecedenceStats:
        case MetricKind::Math_Graph_1D_ProbeStats:
        case MetricKind::Math_KhanAcademy_CoverageStats:
            return LearningTrack::Math;

        case MetricKind::Qm_DoubleSlit_Fringes:
        case MetricKind::Qm_ParticleInBox_Levels:
        case MetricKind::Qm_HarmonicOsc_Spacing:
        case MetricKind::Qm_Tunneling_Transmission:
            return LearningTrack::Quantum;

        case MetricKind::Cosmo_Orbit_Period:
        case MetricKind::Cosmo_Radiation_Spectrum:
        case MetricKind::Cosmo_Atmos_PressureProfile:
            return LearningTrack::Cosmology;

        case MetricKind::Atom_Orbital_EnergyRatios:
        case MetricKind::Atom_Orbital_RadialNodes:
        case MetricKind::Bond_Length_Equilibrium:
        case MetricKind::Bond_Vibration_Frequency:
        case MetricKind::Mat_Thermal_Conductivity:
        case MetricKind::Mat_Electrical_Conductivity:
        case MetricKind::Mat_StressStrain_Modulus:
        case MetricKind::Mat_PhaseChange_Threshold:
        case MetricKind::Chem_ReactionRate_Temp:
        case MetricKind::Chem_Equilibrium_Constant:
        case MetricKind::Chem_Diffusion_Coefficient:
            return LearningTrack::Chemistry;

        case MetricKind::Bio_CellDiffusion_Osmosis:
            return LearningTrack::Biology;

        case MetricKind::Game_RenderPipeline_Determinism:
        case MetricKind::Game_SceneGraph_TransformConsistency:
        case MetricKind::Game_EditorHook_CommandSurface:
            return LearningTrack::Game;

        default:
            return LearningTrack::Vocabulary;
    }
}

static uint32_t sandbox_for_track(LearningTrack t) {
    switch (t) {
        case LearningTrack::Vocabulary: return 0u;
        case LearningTrack::Math:       return 0u;
        case LearningTrack::Quantum:    return 1u;
        case LearningTrack::Cosmology:  return 2u;
        case LearningTrack::Chemistry:  return 3u;
        case LearningTrack::Biology:    return 4u;
        case LearningTrack::Game:       return 5u;
        default:                        return 0u;
    }
}

bool LearningAutomation::track_prereqs_satisfied(SubstrateMicroprocessor* sm, LearningTrack t) {
    if (!sm) return false;

    // Curriculum logic:
    // - Vocabulary + Math always eligible.
    // - Quantum eligible once Stage>=1.
    // - Cosmology eligible once Stage>=1 (spheres/orbits alongside early physics).
    // - Chemistry eligible once Stage>=2 AND basic cosmology checkpoints have been accepted.
    // - Biology deferred until Stage>=4 AND chemistry accepted (environment constraints first).
    // - Game bootstrap eligible once Stage>=6 (or stage manager reaches it).
    const uint32_t st = sm->learning_curriculum_stage_u32;
    if (t == LearningTrack::Vocabulary || t == LearningTrack::Math) return true;
    if (t == LearningTrack::Quantum) return (st >= 1u);
    if (t == LearningTrack::Cosmology) return (st >= 1u);
    if (t == LearningTrack::Game) return (st >= 6u);

    if (t == LearningTrack::Chemistry) {
        if (st < 2u) return false;
        // Require at least one accepted cosmology checkpoint as a minimal "spheres/orbits" foundation.
        uint64_t ok = 0;
        const auto& completed = sm->learning_gate.registry().completed();
        for (const auto& c : completed) {
            if (!c.accepted) continue;
            if (track_for_metric(c.target.kind) == LearningTrack::Cosmology) { ok++; break; }
        }
        return ok > 0;
    }

    if (t == LearningTrack::Biology) {
        if (st < 4u) return false;
        uint64_t chem_ok = 0;
        const auto& completed = sm->learning_gate.registry().completed();
        for (const auto& c : completed) {
            if (!c.accepted) continue;
            if (track_for_metric(c.target.kind) == LearningTrack::Chemistry) { chem_ok++; break; }
        }
        return chem_ok > 0;
    }

    return false;
}

// -----------------------------------------------------------------------------
// Automation
// -----------------------------------------------------------------------------

void LearningAutomation::init_once(SubstrateMicroprocessor* sm) {
    if (inited_) return;
    inited_ = true;

    tracks_.clear();
    auto add_track = [&](LearningTrack t) {
        TrackState ts{};
        ts.track = t;
        ts.sandbox_id_u32 = sandbox_for_track(t);
        ts.rr_budget_u32 = 0u;
        tracks_.push_back(ts);
    };

    add_track(LearningTrack::Vocabulary);
    add_track(LearningTrack::Math);
    add_track(LearningTrack::Quantum);
    add_track(LearningTrack::Cosmology);
    add_track(LearningTrack::Chemistry);
    add_track(LearningTrack::Biology);
    add_track(LearningTrack::Game);

    completed_cursor_u32_ = 0;

    // Seed a first plan packet so the system has an explicit "next action" object.
    AutoArtifact a{};
    a.kind = AutoArtifactKind::PlanPacket;
    a.created_tick_u64 = (sm ? sm->canonical_tick_u64() : 0u);
    a.lane_u32 = 0u;
    a.payload_utf8 = "{\"plan\":\"auto_learn\",\"mode\":\"parallel_rr\",\"tol_percent\":6}";
    if (is_commit_safe_small_utf8(a.payload_utf8)) bus_.push(a);
}

void LearningAutomation::emit_eval_results(SubstrateMicroprocessor* sm) {
    if (!sm) return;

    const auto& completed = sm->learning_gate.registry().completed();
    if (completed_cursor_u32_ > (uint32_t)completed.size()) completed_cursor_u32_ = (uint32_t)completed.size();

    for (; completed_cursor_u32_ < (uint32_t)completed.size(); ++completed_cursor_u32_) {
        const auto& t = completed[completed_cursor_u32_];
        AutoArtifact a{};
        a.kind = AutoArtifactKind::EvalResult;
        a.created_tick_u64 = t.completed_tick_u64;
        a.lane_u32 = (uint32_t)track_for_metric(t.target.kind);

        // Compact payload. No floats; deterministic.
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "{\"metric\":%u,\"accepted\":%s,\"err_q32_32\":%lld,\"tick\":%llu}",
            (unsigned)t.target.kind,
            t.accepted ? "true" : "false",
            (long long)t.best_err_q32_32,
            (unsigned long long)t.completed_tick_u64
        );
        a.payload_utf8 = buf;
        if (is_commit_safe_small_utf8(a.payload_utf8)) bus_.push(a);

        // Update track counters.
        const LearningTrack tr = track_for_metric(t.target.kind);
        for (auto& ts : tracks_) {
            if (ts.track == tr) {
                ts.attempted_count_u64++;
                if (t.accepted) {
                    ts.accepted_count_u64++;
                    ts.last_progress_tick_u64 = t.completed_tick_u64;
                }
                break;
            }
        }
    }
}

void LearningAutomation::maybe_seed_plans(SubstrateMicroprocessor* sm) {
    if (!sm) return;
    // Ensure allowlist is loaded and crawl sessions exist (but do not execute network unless enabled).
    if (!sm->corpus_allowlist_loaded) {
        sm->corpus_crawl_start_neuralis_corpus_default();
    }
}

void LearningAutomation::plan_expand_for_stage(SubstrateMicroprocessor* sm, uint32_t stage_u32) {
    if (!sm) return;

    // Expand to CrawlRequests for eligible tracks, in a bounded round-robin style.
    // This is the "artifact drives next command" mechanism.
    (void)stage_u32;

    // Compute backlog pressure (only schedule when backlog is low).
    const uint32_t backlog = sm->learning_gate.registry().pending_count_u32();
    const uint32_t lim = sm->derived_learning_backlog_limit_u32();
    if (backlog >= lim) return;

    schedule_parallel_tasks(sm, stage_u32);
}

void LearningAutomation::schedule_parallel_tasks(SubstrateMicroprocessor* sm, uint32_t stage_u32) {
    if (!sm) return;
    (void)stage_u32;

    // Adjust allowlist lane cap dynamically (parallel breadth) with stage.
    // This lets the crawler cover vocabulary/math + physics branches in parallel.
    // lane meanings come from your allowlist policy: lower lanes are foundational.
    uint32_t max_lane = 1u;
    if (sm->learning_curriculum_stage_u32 >= 1u) max_lane = 2u; // add QM + cosmo
    if (sm->learning_curriculum_stage_u32 >= 2u) max_lane = 3u; // add atoms/materials
    if (sm->learning_curriculum_stage_u32 >= 3u) max_lane = 4u; // add chemistry
    if (sm->learning_curriculum_stage_u32 >= 4u) max_lane = 5u; // add bio (still prereq gated)
    if (sm->learning_curriculum_stage_u32 >= 6u) max_lane = 7u; // game bootstrap lane
    sm->crawl_allowlist_lane_max_u32 = max_lane;

    // Round-robin across tracks: emit CrawlRequest artifacts for tracks that are eligible
    // and currently lagging (no recent progress).
    const uint64_t now = sm->canonical_tick_u64();
    for (auto& ts : tracks_) {
        if (!track_prereqs_satisfied(sm, ts.track)) continue;

        const uint64_t quiet_ticks = (ts.last_progress_tick_u64 == 0u) ? now : (now - ts.last_progress_tick_u64);
        const bool needs_nudge = (quiet_ticks > 3600u); // ~10s at 360Hz, deterministic.
        const bool early_boot = (ts.attempted_count_u64 == 0u);

        if (!(needs_nudge || early_boot)) continue;

        AutoArtifact a{};
        a.kind = AutoArtifactKind::CrawlRequest;
        a.created_tick_u64 = now;
        a.lane_u32 = (uint32_t)ts.track;

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "{\"track\":%u,\"sandbox\":%u,\"lane_max\":%u,\"reason\":\"%s\"}",
            (unsigned)ts.track,
            (unsigned)ts.sandbox_id_u32,
            (unsigned)max_lane,
            early_boot ? "boot" : "no_progress"
        );
        a.payload_utf8 = buf;
        if (is_commit_safe_small_utf8(a.payload_utf8)) bus_.push(a);

        // Nudge only one per tick to preserve determinism and avoid spamming requests.
        break;
    }
}

void LearningAutomation::process_one_artifact(SubstrateMicroprocessor* sm, const AutoArtifact& a) {
    if (!sm) return;

    if (a.kind == AutoArtifactKind::CrawlRequest) {
        // Respect consent: never cause network I/O unless explicitly enabled and consent flag cleared.
        if (sm->crawler_enable_live_u32 == 0u || sm->crawler_live_consent_required_u32 != 0u) {
            // Still keep the artifact; just log the fact for the UI.
            sm->emit_ui_line("AUTO_CRAWL_BLOCKED consent_or_disabled=1 payload=" + a.payload_utf8);
            return;
        }

        // Ensure crawl sessions exist.
        if (!sm->corpus_allowlist_loaded) sm->corpus_crawl_start_neuralis_corpus_default();

        // Deterministic action: start (or keep) allowlist crawl running.
        // The existing crawl scheduler + topic masks will feed the learning gate.
        // We do not pick an arbitrary URL here: the allowlist scheduler already defines a canonical ordering.
        if (!sm->crawl_sessions[0].active) {
            sm->corpus_crawl_start_neuralis_corpus_default();
        }

        sm->emit_ui_line("AUTO_CRAWL_NUDGE ok=1 payload=" + a.payload_utf8);
        return;
    }

    if (a.kind == AutoArtifactKind::PlanPacket) {
        plan_expand_for_stage(sm, sm->learning_curriculum_stage_u32);
        return;
    }

    if (a.kind == AutoArtifactKind::EvalResult) {
        // On successful acceptance, we immediately enqueue a new plan packet
        // so progress naturally chains into the next action.
        if (a.payload_utf8.find("\"accepted\":true") != std::string::npos) {
            AutoArtifact p{};
            p.kind = AutoArtifactKind::PlanPacket;
            p.created_tick_u64 = sm->canonical_tick_u64();
            p.lane_u32 = a.lane_u32;
            p.payload_utf8 = "{\"plan\":\"next\",\"mode\":\"parallel_rr\",\"cause\":\"eval_accept\"}";
            if (is_commit_safe_small_utf8(p.payload_utf8)) bus_.push(p);
        }
        return;
    }
}

void LearningAutomation::tick(SubstrateMicroprocessor* sm) {
    if (!sm) return;
    if (!enabled_) return;

    init_once(sm);
    maybe_seed_plans(sm);
    emit_eval_results(sm);

    // Consume at most a few artifacts per tick to keep behavior bounded.
    for (uint32_t i = 0; i < 8u; ++i) {
        AutoArtifact a{};
        if (!bus_.pop(&a)) break;
        process_one_artifact(sm, a);
    }
}

} // namespace genesis
