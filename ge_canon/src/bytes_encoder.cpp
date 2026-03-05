#include "bytes_encoder.hpp"

#include "frequency_collapse.hpp"
#include "delta_profiles.hpp"
#include "canonical_ops.hpp"
#include "anchor.hpp"

#include <vector>

// Map a raw byte into a deterministic phase target in TURN_SCALE units.
// theta_byte = byte / 256 turns.
static inline int64_t theta_byte_turns_q(uint8_t b) {
    // TURN_SCALE is divisible by 256 by design in this codebase.
    return ((int64_t)b * (int64_t)TURN_SCALE) / 256;
}

int32_t ew_bytes_to_frequency_code(const uint8_t* bytes, size_t len, uint8_t profile_id) {
    if (!bytes || len == 0) return 0;

    // Build component bins from bytes using the same shortest-wrap delta
    // accumulation strategy as the UTF-8 mapping, but without text
    // normalization. This makes the mapping stable for any file bytes.
    std::vector<int64_t> f_bins_turns_q;
    std::vector<int64_t> a_bins_q32_32;
    std::vector<int64_t> phi_bins_turns_q;
    f_bins_turns_q.reserve(len);
    a_bins_q32_32.reserve(len);
    phi_bins_turns_q.reserve(len);

    int64_t prev_theta = theta_byte_turns_q(bytes[0]);
    int64_t accum_turns = prev_theta;

    for (size_t i = 0; i < len; ++i) {
        const int64_t theta_i = theta_byte_turns_q(bytes[i]);
        const int64_t d = delta_turns(prev_theta, theta_i);
        accum_turns = wrap_turns(accum_turns + d);
        prev_theta = theta_i;

        // Component frequency bin: use the accumulated phase as the bin.
        // This is a deterministic surrogate for local frequency identity.
        f_bins_turns_q.push_back(accum_turns);

        // Component amplitude: weight by byte magnitude in [0,255].
        // Deterministic Q32.32 weight: (b+1)/256.
        const int64_t a_q32_32 = (((int64_t)bytes[i] + 1) << 32) / 256;
        a_bins_q32_32.push_back(a_q32_32);

        // Component phase: the symbol phase itself.
        phi_bins_turns_q.push_back(theta_i);
    }

    // Collapse into a single carrier wave.
    EwCarrierParams car = carrier_params(f_bins_turns_q, a_bins_q32_32, phi_bins_turns_q);

    // Convert the carrier into a single-axis 9D delta (phase-dominant axis).
    Basis9 d;
    for (int k = 0; k < 9; ++k) d.d[k] = 0;

    // d4 = f_carrier * A_carrier (Q32.32), then converted into TURN_SCALE domain
    // inside the spider compressor path via denom and axis weights.
    const int64_t drive_q32_32 = mul_q32_32(car.f_carrier_turns_q32_32, car.A_carrier_q32_32);
    d.d[4] = (int64_t)(((__int128)drive_q32_32 * (int64_t)TURN_SCALE) >> 32);

    EwDeltaProfile prof;
    ew_get_delta_profile(profile_id, &prof);

    // Spider compressor produces f_code.
    // Use the canonical encoder helper on an ephemeral anchor-like object.
    // We reuse the deterministic encoding math without storing state.
    Anchor tmp(0u);
    tmp.basis9 = d;
    const int32_t f_code = tmp.spider_encode_9d(d, prof.weights_q10, prof.denom_q);
    return f_code;
}
