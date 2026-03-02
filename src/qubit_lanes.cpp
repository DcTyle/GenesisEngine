#include "qubit_lanes.hpp"
#include "anchor.hpp"

static inline int64_t i64_abs_local(int64_t x) { return (x < 0) ? -x : x; }

static inline int64_t clamp_q32_32_local(int64_t x, int64_t lo, int64_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline int64_t q32_32_mul_local(int64_t a_q32_32, int64_t b_q32_32) {
    __int128 p = (__int128)a_q32_32 * (__int128)b_q32_32;
    return (int64_t)(p >> 32);
}

static inline uint32_t clamp_u32(uint32_t x, uint32_t lo, uint32_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

uint32_t ew_compute_lane_count(const std::vector<Pulse>& inbound, const EwLanePolicy& pol) {
    uint64_t amp_sum = 0;
    uint8_t max_profile = 0;
    uint8_t max_tag = 0;
    for (size_t i = 0; i < inbound.size(); ++i) {
        const Pulse& p = inbound[i];
        amp_sum += (uint64_t)p.a_code;
        if (p.profile_id > max_profile) max_profile = p.profile_id;
        if (p.causal_tag > max_tag) max_tag = p.causal_tag;
    }

    const uint32_t amp_bucket = (uint32_t)(amp_sum / (uint64_t)(A_MAX / 4 + 1));
    const uint32_t tier = (uint32_t)(max_profile & 0x3u);
    const uint32_t band = (uint32_t)(max_tag & 0x3u);

    uint32_t lanes = pol.min_lanes;
    lanes += amp_bucket;
    lanes += tier * 4u;
    lanes += band * 2u;
    return clamp_u32(lanes, pol.min_lanes, pol.max_lanes);
}

void ew_update_qubit_lanes(std::vector<EwQubitLane>& lanes,
                           uint64_t /*canonical_tick*/,
                           const std::vector<Anchor>& anchors,
                           const std::vector<Pulse>& inbound,
                           const EwLanePolicy& pol) {
    const uint32_t lane_count = ew_compute_lane_count(inbound, pol);
    if (lane_count == 0) return;

    if (lanes.size() != lane_count) {
        lanes.assign(lane_count, EwQubitLane{});
    }

    std::vector<int64_t> sum(lane_count, 0);
    std::vector<uint32_t> cnt(lane_count, 0);
    for (size_t i = 0; i < anchors.size(); ++i) {
        const uint32_t li = (uint32_t)(i % lane_count);
        sum[li] += anchors[i].doppler_q;
        cnt[li] += 1;
    }

    for (uint32_t li = 0; li < lane_count; ++li) {
        EwQubitLane& L = lanes[li];
        const int64_t obs = (cnt[li] == 0) ? 0 : (sum[li] / (int64_t)cnt[li]);
        L.drift_obs_turns_q = obs;

        // pred <- pred + gain*(obs - pred)
        const int64_t err = obs - L.delta_phi_pred_turns_q;
        __int128 pe = (__int128)err * (__int128)pol.pred_gain_q32_32;
        const int64_t corr = (int64_t)(pe >> 32);
        L.delta_phi_pred_turns_q += corr;

        // U = exp(-i*delta_phi_pred*sigma_z/2) -> expo phase correction
        L.phase_turns_q = wrap_turns(L.phase_turns_q - (L.delta_phi_pred_turns_q / 2));

        // residual and confidence
        L.residual_turns_q = obs - L.delta_phi_pred_turns_q;
        const int64_t abs_r = i64_abs_local(L.residual_turns_q);

        int64_t conf = L.confidence_q32_32;
        if (abs_r <= pol.residual_thresh_turns_q) {
            const int64_t one = (1LL << 32);
            const int64_t gap = one - conf;
            conf += q32_32_mul_local(gap, pol.conf_gain_up_q32_32);
        } else {
            conf -= q32_32_mul_local(conf, pol.conf_gain_down_q32_32);
        }
        conf = clamp_q32_32_local(conf, 0, (1LL << 32));
        L.confidence_q32_32 = conf;
        L.weight_q32_32 = conf;
    }

    __int128 wsum = 0;
    for (uint32_t li = 0; li < lane_count; ++li) wsum += (uint64_t)lanes[li].weight_q32_32;
    if (wsum > 0) {
        for (uint32_t li = 0; li < lane_count; ++li) {
            __int128 num = (__int128)lanes[li].weight_q32_32 << 32;
            lanes[li].weight_q32_32 = (int64_t)(num / wsum);
        }
    } else {
        const int64_t uni = (int64_t)((1ULL << 32) / lane_count);
        for (uint32_t li = 0; li < lane_count; ++li) lanes[li].weight_q32_32 = uni;
    }
}
