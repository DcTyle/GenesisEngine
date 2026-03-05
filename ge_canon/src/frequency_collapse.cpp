#include "frequency_collapse.hpp"

#include "canonical_ops.hpp"
#include "fixed_point.hpp"

#include <cstddef>

namespace {

static uint64_t isqrt_u128_to_u64(uint64_t hi, uint64_t lo) {
    // Integer sqrt for a 128-bit value represented by (hi, lo).
    // Deterministic bit-by-bit method.
    __uint128_t n = (static_cast<__uint128_t>(hi) << 64) | static_cast<__uint128_t>(lo);
    __uint128_t x = 0;
    __uint128_t bit = static_cast<__uint128_t>(1) << 126;
    while (bit > n) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (n >= x + bit) {
            n -= x + bit;
            x = (x >> 1) + bit;
        } else {
            x >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<uint64_t>(x);
}

static int64_t wrap_mean_phase_turns_q32_32(const std::vector<EwFreqComponentQ32_32>& comps) {
    // Wrap-aware mean using minimal phase deltas around the first phase.
    const int64_t phi0 = comps[0].phi_turns_q32_32;
    int64_t acc_delta = 0;
    for (std::size_t i = 1; i < comps.size(); ++i) {
        const int64_t d = phase_delta_min_i64(comps[i].phi_turns_q32_32, phi0);
        acc_delta += d;
    }
    const int64_t mean_delta = (comps.size() <= 1) ? 0 : (acc_delta / static_cast<int64_t>(comps.size()));
    return phi0 + mean_delta;
}

}

bool ew_collapse_frequency_components_q32_32(const std::vector<EwFreqComponentQ32_32>& comps,
                                            EwCarrierWaveQ32_32& out) {
    out = EwCarrierWaveQ32_32{};
    if (comps.empty()) {
        return false;
    }

    // Sum weights and weighted frequencies in Q32.32.
    int64_t sum_a = 0;
    __int128 sum_af = 0;
    __int128 sum_a2 = 0;

    for (const auto& c : comps) {
        if (c.a_q32_32 <= 0) {
            return false;
        }
        sum_a += c.a_q32_32;
        sum_af += static_cast<__int128>(c.a_q32_32) * static_cast<__int128>(c.f_turns_q32_32);
        sum_a2 += static_cast<__int128>(c.a_q32_32) * static_cast<__int128>(c.a_q32_32);
    }

    if (sum_a <= 0) {
        return false;
    }

    // f_carrier = sum(a*f) / sum(a), keep Q32.32.
    const int64_t f_carrier = static_cast<int64_t>(sum_af / static_cast<__int128>(sum_a));

    // A = sqrt(sum a^2) in Q32.32.
    // sum_a2 is Q64.64 (since a is Q32.32). We want sqrt -> Q32.32.
    // Take integer sqrt of 128-bit sum_a2.
    const __uint128_t ua2 = static_cast<__uint128_t>(sum_a2);
    const uint64_t hi = static_cast<uint64_t>(ua2 >> 64);
    const uint64_t lo = static_cast<uint64_t>(ua2 & 0xFFFFFFFFFFFFFFFFULL);
    const uint64_t A_u64 = isqrt_u128_to_u64(hi, lo);
    const int64_t A = static_cast<int64_t>(A_u64);

    const int64_t phi_carrier = wrap_mean_phase_turns_q32_32(comps);

    out.f_carrier_turns_q32_32 = f_carrier;
    out.A_carrier_q32_32 = A;
    out.phi_carrier_turns_q32_32 = phi_carrier;
    out.component_count_u32 = static_cast<uint32_t>(comps.size());
    return true;
}

EwCarrierParams carrier_params(const std::vector<int64_t>& f_bins_turns_q,
                               const std::vector<int64_t>& a_bins_q32_32,
                               const std::vector<int64_t>& phi_bins_turns_q) {
    EwCarrierParams out;
    out = EwCarrierParams{};
    const std::size_t n = f_bins_turns_q.size();
    if (n == 0 || a_bins_q32_32.size() != n || phi_bins_turns_q.size() != n) {
        return out;
    }
    std::vector<EwFreqComponentQ32_32> comps;
    comps.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        EwFreqComponentQ32_32 c;
        // Convert TURN_SCALE units into Q32.32 turns.
        c.f_turns_q32_32 = turns_q_to_q32_32(f_bins_turns_q[i]);
        c.a_q32_32 = a_bins_q32_32[i];
        c.phi_turns_q32_32 = turns_q_to_q32_32(phi_bins_turns_q[i]);
        comps.push_back(c);
    }
    EwCarrierWaveQ32_32 tmp;
    if (!ew_collapse_frequency_components_q32_32(comps, tmp)) {
        return out;
    }
    out = tmp;
    return out;
}

uint64_t carrier_phase_u64(int64_t phi_carrier_turns_q32_32) {
    // Stable 64-bit carrier id derived directly from the fixed-point phase.
    // This is used for inspection/logging only.
    const uint64_t x = (uint64_t)phi_carrier_turns_q32_32;
    return x;
}
