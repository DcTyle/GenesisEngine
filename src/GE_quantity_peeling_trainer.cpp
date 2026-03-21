#include "GE_quantity_peeling_trainer.hpp"

#include <algorithm>

static inline int32_t ge_q16_16_from_q32_32(int64_t x_q32_32) {
    // clamp to [0, 1.0] in Q16.16
    if (x_q32_32 < 0) x_q32_32 = 0;
    const int64_t one = (int64_t(1) << 32);
    if (x_q32_32 > one) x_q32_32 = one;
    return (int32_t)((x_q32_32 + (int64_t(1) << 15)) >> 16);
}

bool GE_trainer_epoch(GE_CorpusAnchorStore& store,
                      GE_CoherenceGraphStore& graph,
                      genesis::LearningCheckpointGate& gate,
                      const GE_TrainerParams& params,
                      GE_TrainerStats& io_stats) {
    // The gate is the acceptance oracle. No hidden alternate paths.
    if (store.records.empty()) return false;

    if (!params.opt_thresholds) {
        // Training requires an explicit threshold schedule.
        return false;
    }

    if (!params.use_cuda_scoring) {
        // No CPU scoring path in the corpus training loop.
        return false;
    }

    const size_t n = store.records.size();
    const size_t window = (n < 256) ? n : 256;
    const size_t start = n - window;

    std::vector<int64_t> target_w;
    std::vector<int64_t> proposed_w;
    uint32_t topk_eff = params.topk_u32;
    if (topk_eff == 0u) topk_eff = 1u;
    if (topk_eff > params.safety_caps.max_fanout_u32) topk_eff = params.safety_caps.max_fanout_u32;

    target_w.reserve(topk_eff);
    proposed_w.reserve(topk_eff);

    for (size_t qi = start; qi < n; ++qi) {
        const auto& q = store.records[qi];

        // Determine per-lane threshold for this epoch (quantity peeling schedule).
        int64_t accept_rel = params.accept_rel_err_max_q32_32;
        const auto* lane = params.opt_thresholds->find_lane(q.lane_u8);
        if (lane) {
            accept_rel = lane->rel_err_limit_for_epoch_q32_32(params.epoch_u32, accept_rel);
        }

        const auto hits = GE_retrieve_topk_gpu(store, q, q.lane_u8, &q.domain_id9, topk_eff);

        if (hits.empty()) continue;

        target_w.clear();
        proposed_w.clear();
        for (size_t hi = 0; hi < hits.size(); ++hi) {
            int64_t t = (int64_t(1) << 32) - int64_t(hi) * (int64_t(1) << 28);
            if (t < 0) t = 0;
            target_w.push_back(t);
            proposed_w.push_back(hits[hi].score_q32_32);
        }

        const int64_t rel0 = genesis::LearningCheckpointGate::rel_err_q32_32_vec(
            proposed_w, target_w, (int64_t(1) << 20));

        std::vector<int64_t> next_w = proposed_w;
        for (size_t i = 0; i < next_w.size(); ++i) {
            const int64_t diff = target_w[i] - next_w[i];
            const int64_t step = (diff * params.step_q32_32) >> 32;
            next_w[i] += step;
            if (next_w[i] < 0) next_w[i] = 0;
            if (next_w[i] > (int64_t(1) << 32)) next_w[i] = (int64_t(1) << 32);
        }

        const int64_t rel1 = genesis::LearningCheckpointGate::rel_err_q32_32_vec(
            next_w, target_w, (int64_t(1) << 20));

        io_stats.proposals_u64++;
        io_stats.last_rel_err_q32_32 = rel1;

        // Gate: accept only if the update improves error and meets the lane threshold.
        // We route through the gate object to keep the acceptance policy centralized.
        const bool gate_ok = gate.accept_rel_err(rel0, rel1, accept_rel);
        if (gate_ok) {
            // Safety cap: do not exceed explicit materialization budgets.
            if (io_stats.edge_writes_u64 >= params.safety_caps.max_edge_writes_u64) {
                continue;
            }

            for (size_t hi = 0; hi < hits.size(); ++hi) {
                const auto& dst = store.records[hits[hi].record_index];
                const int32_t w_q16_16 = ge_q16_16_from_q32_32(next_w[hi]);
                if (io_stats.edge_writes_u64 < params.safety_caps.max_edge_writes_u64) {
                    graph.upsert_edge_undirected(q.anchor_id9, dst.anchor_id9, w_q16_16, params.max_degree_u32);
                    io_stats.edge_writes_u64++;
                }
            }
            io_stats.accepted_u64++;
        }
    }

    graph.sort_and_compact(params.max_degree_u32);
    return true;
}
