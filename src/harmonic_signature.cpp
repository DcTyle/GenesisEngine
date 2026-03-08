#include "harmonic_signature.hpp"

#include "fixed_point.hpp"

static inline uint32_t ew_det_k_from_turns(int64_t turns_q) {
    // Deterministic mapping into [1..9].
    // Uses magnitude in TURN_SCALE-domain to avoid dependence on sign.
    uint64_t u = (uint64_t)((turns_q < 0) ? -turns_q : turns_q);
    uint32_t k = (uint32_t)(u % 9ULL);
    return (uint32_t)(k + 1U);
}

AnchorHarmonicSignatureQ63 ew_build_anchor_signature(uint32_t anchor_id, const int64_t coord_turns_q[kDims9]) {
    AnchorHarmonicSignatureQ63 fp{};

    // Base carrier code: stable mapping of anchor id into [0..1] Q63.
    // This does not expose lattice internals; it is an identity proxy only.
    const uint64_t base_mod = (uint64_t)(anchor_id & 0x3FFU); // 10-bit stable band id
    fp.base_freq_code_q63 = (int64_t)(((__int128)base_mod * (__int128)Q63_ONE) / (__int128)1024);

    uint64_t order_sum = 0;
    for (int d = 0; d < kDims9; ++d) {
        const uint32_t k_d = ew_det_k_from_turns(coord_turns_q[d]);
        fp.harmonic_order[d] = k_d;
        fp.harmonic_weight_q63[d] = (int64_t)((uint64_t)Q63_ONE / (uint64_t)k_d);
        order_sum += (uint64_t)k_d;
    }

    // Bandwidth proxy: lower order sum => wider band. Map into [0..1] Q63.
    // order_sum in [9..81].
    const uint64_t order_span = 81ULL - 9ULL;
    const uint64_t inv = (order_sum <= 9ULL) ? order_span : (81ULL - order_sum);
    fp.bandwidth_q63 = (int64_t)(((__int128)inv * (__int128)Q63_ONE) / (__int128)order_span);

    return fp;
}
