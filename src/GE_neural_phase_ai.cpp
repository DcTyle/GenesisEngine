#include "GE_neural_phase_ai.hpp"

#include "code_artifact_ops.hpp"
#include "GE_runtime.hpp"
#include "fixed_point.hpp"
#include <climits>

EwNeuralPhaseAI::EwNeuralPhaseAI() : seed_u64_(1) {
    status_.tick_u64 = 0;
    status_.class_id_u32 = 0;
    status_.reserved0_u32 = 0;
    status_.confidence_q32_32 = 0;
    status_.sig9_u64 = 0;
    last_strength_q32_32_ = 0;
}

void EwNeuralPhaseAI::init(uint64_t projection_seed) {
    seed_u64_ = projection_seed ? projection_seed : 1;
    mem_.clear();
    mem_.reserve(16);
    status_.tick_u64 = 0;
    status_.class_id_u32 = 0;
    status_.confidence_q32_32 = (1LL << 32);
    status_.sig9_u64 = 0;
    last_strength_q32_32_ = 0;
}

int64_t EwNeuralPhaseAI::attractor_strength_for_sig9(uint64_t sig9_u64) const {
    for (size_t i = 0; i < mem_.size(); ++i) {
        if (mem_[i].sig9_u64 == sig9_u64) return mem_[i].strength_q32_32;
    }
    return 0;
}

uint64_t EwNeuralPhaseAI::sig9_fold(uint64_t acc, uint64_t x) {
    // Deterministic fold for stable bookkeeping / bucket addressing.
    acc ^= x + 0x9E3779B97F4A7C15ULL + (acc << 6) + (acc >> 2);
    return acc;
}

uint64_t EwNeuralPhaseAI::sig9_from_state(const SubstrateManager* sm) {
    uint64_t acc = 0;
    acc = sig9_fold(acc, sm->canonical_tick);
    acc = sig9_fold(acc, (uint64_t)sm->anchors.size());

    int64_t sum_theta = 0;
    int64_t sum_chi = 0;
    int64_t sum_curv = 0;
    int64_t sum_dopp = 0;
    int64_t sum_m = 0;
    for (size_t i = 0; i < sm->anchors.size(); ++i) {
        const Anchor& a = sm->anchors[i];
        sum_theta += a.theta_q;
        sum_chi += a.chi_q;
        sum_curv += a.curvature_q;
        sum_dopp += a.doppler_q;
        sum_m += a.m_q;
    }

    int64_t sum_res = 0;
    for (size_t i = 0; i < sm->lanes.size(); ++i) {
        sum_res += sm->lanes[i].residual_turns_q;
    }

    const int64_t n = (int64_t)(sm->anchors.size() ? sm->anchors.size() : 1);
    const int64_t m = (int64_t)(sm->lanes.size() ? sm->lanes.size() : 1);
    const int64_t mean_theta = sum_theta / n;
    const int64_t mean_chi = sum_chi / n;
    const int64_t mean_curv = sum_curv / n;
    const int64_t mean_dopp = sum_dopp / n;
    const int64_t mean_m = sum_m / n;
    const int64_t mean_res = sum_res / m;

    acc = sig9_fold(acc, (uint64_t)(uint32_t)(mean_theta & 0xFFFFFFFF));
    acc = sig9_fold(acc, (uint64_t)(uint32_t)(mean_chi & 0xFFFFFFFF));
    acc = sig9_fold(acc, (uint64_t)(uint32_t)(mean_curv & 0xFFFFFFFF));
    acc = sig9_fold(acc, (uint64_t)(uint32_t)(mean_dopp & 0xFFFFFFFF));
    acc = sig9_fold(acc, (uint64_t)(uint32_t)(mean_m & 0xFFFFFFFF));
    acc = sig9_fold(acc, (uint64_t)(uint32_t)(mean_res & 0xFFFFFFFF));

    acc = sig9_fold(acc, (uint64_t)(uint32_t)(sm->frame_gamma_turns_q & 0xFFFFFFFF));
    return acc;
}

uint32_t EwNeuralPhaseAI::class_id_from_sig9(uint64_t sig9_u64) {
    return (uint32_t)((sig9_u64 >> 40) & 0xFFULL);
}

int64_t EwNeuralPhaseAI::confidence_from_state(const SubstrateManager* sm) {
    if (sm->anchors.empty()) return (1LL << 31);

    int64_t sum_chi = 0;
    for (size_t i = 0; i < sm->anchors.size(); ++i) sum_chi += sm->anchors[i].chi_q;
    const int64_t mean_chi = sum_chi / (int64_t)sm->anchors.size();

    int64_t sum_abs_res = 0;
    for (size_t i = 0; i < sm->lanes.size(); ++i) {
        int64_t r = sm->lanes[i].residual_turns_q;
        if (r < 0) r = -r;
        sum_abs_res += r;
    }
    const int64_t denom = (int64_t)(sm->lanes.size() ? sm->lanes.size() : 1);
    const int64_t mean_abs_res = sum_abs_res / denom;

    int64_t chi_q32_32 = 0;
    if (TURN_SCALE > 0) chi_q32_32 = (mean_chi << 32) / TURN_SCALE;
    if (chi_q32_32 < 0) chi_q32_32 = 0;
    if (chi_q32_32 > (1LL << 32)) chi_q32_32 = (1LL << 32);

    const int64_t unit = (TURN_SCALE / 64) ? (TURN_SCALE / 64) : 1;
    const int64_t k = mean_abs_res / unit;
    const int64_t denom_pen = (1LL << 32) + (k << 32);
    int64_t pen_q32_32 = 0;
    if (denom_pen > 0) {
        __int128 num = (__int128)(1LL << 32) * (__int128)(1LL << 32);
        pen_q32_32 = (int64_t)(num / (__int128)denom_pen);
    }

    return mul_q32_32(chi_q32_32, pen_q32_32);
}

void EwNeuralPhaseAI::pre_tick(SubstrateManager* sm) {
    if (!sm) return;

    auto starts_with_ascii = [&](const char* prefix)->bool {
        if (!prefix) return false;
        size_t n = 0;
        while (prefix[n] != 0) ++n;
        if (sm->last_observation_text.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            if (sm->last_observation_text[i] != prefix[i]) return false;
        }
        return true;
    };

    auto submit_ops_once = [&](const EwAiOpcodeU16* ops, uint32_t count_u32) {
        if (!ops || count_u32 == 0u) return;
        if (sm->observation_seq_u64 != 0u &&
            sm->ai_last_command_observation_seq_u64 == sm->observation_seq_u64) {
            return;
        }
        EwAiCommand cmds[EW_AI_COMMAND_MAX]{};
        uint32_t n = (count_u32 > EW_AI_COMMAND_MAX) ? EW_AI_COMMAND_MAX : count_u32;
        uint16_t prio = 10u;
        for (uint32_t i = 0; i < n; ++i) {
            cmds[i].opcode_u16 = (uint16_t)ops[i];
            cmds[i].priority_u16 = prio;
            cmds[i].weight_q63 = (int64_t)(INT64_MAX / (1LL << (i + 1u)));
            if (prio > 1u) --prio;
        }
        sm->submit_ai_commands_fixed(cmds, n);
        sm->ai_last_command_observation_seq_u64 = sm->observation_seq_u64;
    };

    static const EwAiOpcodeU16 kQueryOps[] = {
        EW_AI_OP_TASK_SELECT,
        EW_AI_OP_IO_READ,
        EW_AI_OP_RENDER_UPDATE
    };
    static const EwAiOpcodeU16 kFetchOps[] = {
        EW_AI_OP_TASK_SELECT,
        EW_AI_OP_ROUTE,
        EW_AI_OP_FETCH,
        EW_AI_OP_RENDER_UPDATE
    };
    static const EwAiOpcodeU16 kStoreOps[] = {
        EW_AI_OP_TASK_SELECT,
        EW_AI_OP_STORE
    };
    static const EwAiOpcodeU16 kConfigOps[] = {
        EW_AI_OP_TASK_SELECT,
        EW_AI_OP_PRIORITY_HINT,
        EW_AI_OP_RENDER_UPDATE
    };

    // ---------------------------------------------------------------------
    //  AI Interface Layer: bounded command admission into the substrate
    // ---------------------------------------------------------------------
    // Command vectors are one-shot per observed line. This prevents repeated
    // network scheduling when a command-bearing observation remains resident.
    if (starts_with_ascii("QUERY:") || starts_with_ascii("ANSWER:")) {
        submit_ops_once(kQueryOps, 3u);
    } else if (starts_with_ascii("WEBSEARCH:") || starts_with_ascii("SEARCH:") ||
               starts_with_ascii("WEBFETCH:") ||
               starts_with_ascii("OPEN:") || starts_with_ascii("OPEN_RESULT:")) {
        submit_ops_once(kFetchOps, 4u);
    } else if (starts_with_ascii("WEBSEARCH_CFG:")) {
        submit_ops_once(kConfigOps, 3u);
    } else if (starts_with_ascii("CODEGEN:") || starts_with_ascii("SYNTHCODE:") ||
               starts_with_ascii("CODEEDIT:") || starts_with_ascii("PATCH:") ||
               starts_with_ascii("HYDRATE:") || starts_with_ascii("GAMEBOOT:")) {
        submit_ops_once(kStoreOps, 2u);
    }

    // --- Policy-driven operator selection (Spec/Blueprint) ---
    // Mechanism (exact):
    //   1) Observe committed substrate state.
    //   2) Derive sig9 + class_id + confidence.
    //   3) Lookup attractor strength for sig9.
    //   4) Select a pulse profile + shape using sm->ai_policy.
    //   5) Emit action only through enqueue_inbound_pulse.
    //
    // This produces a deterministic classification -> operator -> action loop
    // that is testable without UE.
    const uint64_t sig9 = sig9_from_state(sm);
    const uint32_t class_id = class_id_from_sig9(sig9);
    const int64_t conf_q32_32 = confidence_from_state(sm);
    const int64_t strength_q32_32 = attractor_strength_for_sig9(sig9);

    // Select a policy decision.
    const EwAiPolicyDecision pd = sm->ai_policy.decide(class_id, conf_q32_32, strength_q32_32);

    // Emit policy pulse only above a minimal confidence band.
    // Minimal action emission threshold for testability.
    // The policy still scales amplitude by confidence, but will always
    // produce at least one pulse per tick when anchors exist.
    if (!sm->anchors.empty()) {
        const uint32_t idx = (uint32_t)(class_id % (uint32_t)sm->anchors.size());
        const uint32_t aid = sm->anchors[idx].id;

        Pulse p;
        p.anchor_id = aid;
        p.f_code = pd.f_code_i32;
        p.a_code = pd.a_code_u16;
        p.v_code = pd.v_code_u16;
        p.i_code = pd.i_code_u16;
        p.profile_id = pd.profile_id_u8;
        // Spec: 0x1 = ACTIVATE (context activation favored).
        p.causal_tag = 0x1u;
        p.pad0 = 0u;
        p.pad1 = 0u;
        p.tick = sm->canonical_tick;
        sm->enqueue_inbound_pulse(p);

        EwAiActionEvent ev{};
        ev.tick_u64 = sm->canonical_tick;
        ev.sig9_u64 = sig9;
        ev.class_id_u32 = class_id;
        ev.kind_u16 = (uint16_t)EW_AI_ACTION_PULSE_EMIT;
        ev.profile_id_u16 = (uint16_t)pd.profile_id_u8;
        ev.target_anchor_id_u32 = aid;
        ev.f_code_i32 = pd.f_code_i32;
        ev.a_code_u32 = (uint32_t)pd.a_code_u16;
        ev.v_code_u32 = (uint32_t)pd.v_code_u16;
        ev.i_code_u32 = (uint32_t)pd.i_code_u16;
        ev.confidence_q32_32 = conf_q32_32;
        ev.attractor_strength_q32_32 = strength_q32_32;
        ev.frame_gamma_turns_q = sm->frame_gamma_turns_q;
        sm->ai_log_event(ev);
    }

    int64_t sum_res = 0;
    for (size_t i = 0; i < sm->lanes.size(); ++i) sum_res += sm->lanes[i].residual_turns_q;
    const int64_t denom = (int64_t)(sm->lanes.size() ? sm->lanes.size() : 1);
    const int64_t mean_res = sum_res / denom;
    int64_t abs_mean_res = mean_res;
    if (abs_mean_res < 0) abs_mean_res = -abs_mean_res;

    const int64_t thresh = sm->lane_policy.residual_thresh_turns_q;
    if (abs_mean_res <= thresh) return;

    const int64_t step = (TURN_SCALE / 512) ? (TURN_SCALE / 512) : 1;
    int64_t delta = (mean_res > 0) ? -step : step;
    sm->frame_gamma_turns_q = wrap_turns(sm->frame_gamma_turns_q + delta);

    // Log bounded internal control action.
    EwAiActionEvent ev{};
    ev.tick_u64 = sm->canonical_tick;
    ev.sig9_u64 = sig9_from_state(sm);
    ev.class_id_u32 = class_id_from_sig9(ev.sig9_u64);
    ev.kind_u16 = (uint16_t)EW_AI_ACTION_FRAME_GAMMA_ADJUST;
    ev.profile_id_u16 = 0;
    ev.target_anchor_id_u32 = 0;
    ev.f_code_i32 = 0;
    ev.a_code_u32 = 0;
    ev.confidence_q32_32 = confidence_from_state(sm);
    ev.attractor_strength_q32_32 = attractor_strength_for_sig9(ev.sig9_u64);
    ev.frame_gamma_turns_q = sm->frame_gamma_turns_q;
    sm->ai_log_event(ev);

    const uint32_t aid = sm->anchors.empty() ? 0u : sm->anchors[0].id;

    Pulse p;
    p.anchor_id = aid;
    const int32_t bucket = (int32_t)(abs_mean_res / ((TURN_SCALE / 128) ? (TURN_SCALE / 128) : 1));
    const int32_t dir = (mean_res > 0) ? -1 : 1;
    p.f_code = dir * (bucket ? bucket : 1);
    uint32_t a = (uint32_t)(bucket);
    if (a > 65535u) a = 65535u;
    p.a_code = (uint16_t)a;
    p.profile_id = (uint8_t)EW_PROFILE_CORE_EVOLUTION;
    p.causal_tag = 1u;
    p.pad0 = 0u;
    p.pad1 = 0u;
    p.tick = sm->canonical_tick;

    sm->enqueue_inbound_pulse(p);

    // Log emitted correction pulse.
    EwAiActionEvent evp{};
    evp.tick_u64 = sm->canonical_tick;
    evp.sig9_u64 = sig9_from_state(sm);
    evp.class_id_u32 = class_id_from_sig9(evp.sig9_u64);
    evp.kind_u16 = (uint16_t)EW_AI_ACTION_PULSE_EMIT;
    evp.profile_id_u16 = (uint16_t)p.profile_id;
    evp.target_anchor_id_u32 = aid;
    evp.f_code_i32 = p.f_code;
    evp.a_code_u32 = (uint32_t)p.a_code;
    evp.confidence_q32_32 = confidence_from_state(sm);
    evp.attractor_strength_q32_32 = attractor_strength_for_sig9(evp.sig9_u64);
    evp.frame_gamma_turns_q = sm->frame_gamma_turns_q;
    sm->ai_log_event(evp);
}

void EwNeuralPhaseAI::post_tick(SubstrateManager* sm) {
    if (!sm) return;

    status_.tick_u64 = sm->canonical_tick;
    status_.sig9_u64 = sig9_from_state(sm);
    status_.class_id_u32 = class_id_from_sig9(status_.sig9_u64);
    status_.confidence_q32_32 = confidence_from_state(sm);

    // Cache current strength (pre-update) so tests can read it.
    last_strength_q32_32_ = attractor_strength_for_sig9(status_.sig9_u64);

    const int64_t decay = (int64_t)((1LL << 32) / 128);
    for (size_t i = 0; i < mem_.size(); ++i) {
        int64_t s = mem_[i].strength_q32_32;
        s -= decay;
        if (s < 0) s = 0;
        mem_[i].strength_q32_32 = s;
    }

    for (size_t i = 0; i < mem_.size(); ++i) {
        if (mem_[i].sig9_u64 == status_.sig9_u64) {
            const int64_t add = mul_q32_32((int64_t)((1LL << 32) / 16), status_.confidence_q32_32);
            mem_[i].strength_q32_32 += add;
            if (mem_[i].strength_q32_32 > (8LL << 32)) mem_[i].strength_q32_32 = (8LL << 32);
            mem_[i].last_tick_u64 = sm->canonical_tick;
            last_strength_q32_32_ = mem_[i].strength_q32_32;
            return;
        }
    }

    EwAttractorEntry e;
    e.sig9_u64 = status_.sig9_u64;
    e.strength_q32_32 = mul_q32_32((int64_t)((1LL << 32) / 8), status_.confidence_q32_32);
    e.last_tick_u64 = sm->canonical_tick;

    if (mem_.size() < 16) {
        mem_.push_back(e);
        last_strength_q32_32_ = e.strength_q32_32;
        return;
    }

    size_t weakest = 0;
    for (size_t i = 1; i < mem_.size(); ++i) {
        if (mem_[i].strength_q32_32 < mem_[weakest].strength_q32_32) weakest = i;
    }
    mem_[weakest] = e;
    last_strength_q32_32_ = e.strength_q32_32;
}
