#include "substrate_alu.hpp"

#include "fixed_point.hpp"

#include <vector>

namespace {

static inline int64_t iabs64_local(int64_t v) { return (v < 0) ? -v : v; }

static inline int64_t clamp_i64_local(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Project a Q32.32 scalar into TURN_SCALE units deterministically.
static inline int64_t q32_32_to_turns_q(int64_t x_q32_32) {
    __int128 p = (__int128)x_q32_32 * (__int128)TURN_SCALE;
    return (int64_t)(p >> 32);
}

}

uint64_t ew_alu_carrier_id_u64_from_q32_32_pair(const EwCtx& ctx, int64_t a_q32_32, int64_t b_q32_32) {
    // Map operands into a minimal 2-component carrier collapse.
    // This is NOT a physics claim; it is a deterministic execution substrate
    // representation used to ensure "carrier harmonics" participate in every
    // computation path.

    const int64_t ta = q32_32_to_turns_q(a_q32_32);
    const int64_t tb = q32_32_to_turns_q(b_q32_32);

    // Frequency bins in TURN_SCALE units (wrap in [0, TURN_SCALE)).
    auto wrap_turns_local = [](int64_t t)->int64_t {
        if (TURN_SCALE <= 0) return 0;
        int64_t r = t % (int64_t)TURN_SCALE;
        if (r < 0) r += (int64_t)TURN_SCALE;
        return r;
    };

    std::vector<int64_t> f_bins_turns_q;
    std::vector<int64_t> a_bins_q32_32;
    std::vector<int64_t> phi_bins_turns_q;

    f_bins_turns_q.reserve(2);
    a_bins_q32_32.reserve(2);
    phi_bins_turns_q.reserve(2);

    // Use context pulse limit as a deterministic scaling guide (if present).
    const int64_t scale = (ctx.phase_max_displacement_q32_32 > 0) ? ctx.phase_max_displacement_q32_32 : (1LL << 32);

    const int64_t aa = clamp_i64_local(iabs64_local(a_q32_32), 0, (8LL << 32));
    const int64_t bb = clamp_i64_local(iabs64_local(b_q32_32), 0, (8LL << 32));

    // Convert magnitudes into pseudo-frequency bins.
    const int64_t fa = wrap_turns_local((q32_32_to_turns_q(div_q32_32(aa, scale))));
    const int64_t fb = wrap_turns_local((q32_32_to_turns_q(div_q32_32(bb, scale))));

    f_bins_turns_q.push_back(fa);
    f_bins_turns_q.push_back(fb);

    // Amplitudes use magnitudes directly (ensure > 0 for collapse).
    const int64_t amin = (1LL << 20);
    a_bins_q32_32.push_back((aa > amin) ? aa : amin);
    a_bins_q32_32.push_back((bb > amin) ? bb : amin);

    // Phases are turns (TURN_SCALE units).
    phi_bins_turns_q.push_back(wrap_turns_local(ta));
    phi_bins_turns_q.push_back(wrap_turns_local(tb));

    const EwCarrierParams car = carrier_params(f_bins_turns_q, a_bins_q32_32, phi_bins_turns_q);
    return carrier_phase_u64(car.phi_carrier_turns_q32_32);
}

uint64_t ew_alu_carrier_id_u64_from_turns_pair(const EwCtx& ctx, int64_t a_turns_q, int64_t b_turns_q) {
    // Convert TURN_SCALE-domain turns -> Q32.32 turns.
    const int64_t a_q32_32 = turns_q_to_q32_32(a_turns_q);
    const int64_t b_q32_32 = turns_q_to_q32_32(b_turns_q);
    return ew_alu_carrier_id_u64_from_q32_32_pair(ctx, a_q32_32, b_q32_32);
}
