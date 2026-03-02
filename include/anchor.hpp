#pragma once
#include <vector>
#include <cstddef>
#include "fixed_point.hpp"

// Anchor kind tags (deterministic, non-versioned).
static const uint32_t EW_ANCHOR_KIND_DEFAULT = 0;
static const uint32_t EW_ANCHOR_KIND_CONTEXT_ROOT = 1;
static const uint32_t EW_ANCHOR_KIND_SYLLABUS_ROOT = 2;
static const uint32_t EW_ANCHOR_KIND_CRAWLER_ROOT = 3;
static const uint32_t EW_ANCHOR_KIND_DOMAIN_ROOT = 4;
static const uint32_t EW_ANCHOR_KIND_DOC_ROOT = 5;

// Spider carrier bounds (deterministic, non-versioned).
// These are carrier observables used for gating and tensor-gradient coupling.
static const uint32_t V_MAX = 65535;
static const uint32_t I_MAX = 65535;

struct Pulse {
    uint32_t anchor_id;
    int32_t f_code;
    uint16_t a_code;
    uint16_t v_code;
    uint16_t i_code;
    // Delta encoding profile id (Spec 3.6). This is a versioned constant selector
    // that must not vary within a run unless explicitly set by the caller.
    uint8_t profile_id;
    // Deterministic tie-break tag for ordering/merge. Not a coord-tag.
    uint8_t causal_tag;
    // Padding is explicit and MUST be zeroed so that bytewise determinism
    // tests are meaningful and stable.
    uint16_t pad0;
    uint32_t pad1;
    uint64_t tick;
};

struct SpiderCode4 {
    int32_t f_code;
    uint16_t a_code;
    uint16_t v_code;
    uint16_t i_code;
};

struct Basis9 {
    int64_t d[9];
    Basis9() { for (int i = 0; i < 9; ++i) d[i] = 0; }
};

class Anchor {
public:
    uint32_t id;
    // Anchor kind tag (deterministic). This is a runtime semantic label.
    uint32_t kind_u32;
    // Owning context anchor id (0 means global/default).
    uint32_t context_id_u32;
    // Owning crawler anchor id (0 means none).
    uint32_t crawler_id_u32;
    // Object memory reference ID (OMRO). This is an immutable identifier
    // for the referenced object entry. In the Expo prototype it defaults
    // deterministically to the anchor id unless explicitly assigned.
    uint64_t object_id_u64;
    // Deterministic phase key associated with the referenced object.
    // This is copied into the lane/anchor at import time.
    uint64_t object_phase_seed_u64;

    // Anchor-permitted object influence channels (Blueprint C.4).
    // mask9 bits correspond to basis9 dimensions 0..8.
    // scale is Q32.32 multiplier applied to a centered seed turn value.
    uint16_t object_influence_mask9;
    uint16_t object_influence_pad0;
    int64_t object_theta_scale_turns_q32_32;
    int64_t theta_q;
    int64_t chi_q;
    int64_t m_q;
    uint64_t tau_q;

    // Local temporal accumulator expressed in TURN_SCALE units.
    // This enables non-uniform (time-dilated) local evolution while keeping
    // a single global canonical_tick for scheduling.
    int64_t tau_turns_q;

    // Derived channels (TURN_SCALE units) for visualization and operator input.
    int64_t curvature_q;
    int64_t doppler_q;

    // Previous-step phase for deterministic doppler estimation.
    int64_t last_theta_q;

    // Previous-step inbound pulse observables (for tau_delta sampling).
    // These are stored substrate observables that operators may read to
    // deterministically derive phase anchors.
    int32_t  last_f_code;
    uint16_t last_a_code;
    uint16_t last_v_code;
    uint16_t last_i_code;

    // -----------------------------------------------------------------
    // Substrate anchor harmonics (processing artifacts)
    // -----------------------------------------------------------------
    // The substrate microprocessor may store processing artifacts (e.g. text/page
    // encodings, dataset signatures) as harmonic magnitudes on the anchor.
    // These are NOT computed on CPU; they are written by GPU encoders and
    // consumed by substrate operators.
    static const uint32_t HARMONICS_N = 32;
    uint16_t harmonics_q15[HARMONICS_N];
    // Mean harmonic magnitude (Q15), computed on GPU during encoding.
    // This is a convenience observable so CPU orchestration never needs to
    // compute reductions over harmonics bins.
    uint16_t harmonics_mean_q15;
    uint32_t harmonics_epoch_u32;

    // Previous-step ln(A_k/A_ref) (Q32.32) used for delta coupling.
    int64_t last_lnA_q32_32;

    // Previous-step ln(|f_k|/f_ref) (Q32.32) used for ratio-delta coupling.
    int64_t last_lnF_q32_32;

    // Ring semantics (Blueprint 3.3.1): deterministic phase-ring orientation.
    // These are TURN_SCALE-domain turns.
    int64_t theta_start_turns_q;
    int64_t theta_end_turns_q;
    int64_t paf_turns_q;

    // Derived coherent time delta (dt_star) as an OUTPUT only (Q32.32 seconds).
    // This value is computed only when coherence gating passes.
    int64_t dt_star_seconds_q32_32;

    Basis9 basis9;
    std::vector<uint32_t> neighbors;

    Anchor(uint32_t id_)
        : id(id_), kind_u32(0), context_id_u32(0), crawler_id_u32(0), object_id_u64(static_cast<uint64_t>(id_)), object_phase_seed_u64(0),
          object_influence_mask9(0), object_influence_pad0(0),
          object_theta_scale_turns_q32_32(0),
          theta_q(0), chi_q(TURN_SCALE / 10),
          m_q(TURN_SCALE), tau_q(0), tau_turns_q(0),
          curvature_q(0), doppler_q(0), last_theta_q(0) {
        last_f_code = 0;
        last_a_code = 0;
        last_v_code = 0;
        last_i_code = 0;
        last_lnA_q32_32 = 0;
        last_lnF_q32_32 = 0;
        dt_star_seconds_q32_32 = 0;

        for (uint32_t i = 0; i < HARMONICS_N; ++i) harmonics_q15[i] = 0;
        harmonics_mean_q15 = 0;
        harmonics_epoch_u32 = 0;

        theta_start_turns_q = 0;
        theta_end_turns_q = 0;
        paf_turns_q = 0;
        sync_basis9_from_core();
    }

    inline void sync_basis9_from_core() {
        basis9.d[3] = tau_turns_q;                              // temporal
        basis9.d[4] = theta_q;                                  // d5
        basis9.d[5] = chi_q;                                    // d6
        basis9.d[6] = curvature_q;                               // curvature
        basis9.d[7] = doppler_q;                                 // doppler
        basis9.d[8] = m_q;                                      // d9
    }

    inline void update_derived_terms(int64_t curvature_in_q, int64_t doppler_in_q) {
        curvature_q = curvature_in_q;
        doppler_q = doppler_in_q;
        basis9.d[6] = curvature_q;
        basis9.d[7] = doppler_q;
    }

    inline void compute_delta9(const Basis9& projected, int64_t out_delta9[9]) const {
        for (int i = 0; i < 9; ++i) out_delta9[i] = 0;
        out_delta9[0] = projected.d[0] - basis9.d[0];
        out_delta9[1] = projected.d[1] - basis9.d[1];
        out_delta9[2] = projected.d[2] - basis9.d[2];
        out_delta9[3] = projected.d[3] - basis9.d[3];
        out_delta9[4] = delta_turns(projected.d[4], basis9.d[4]);
        out_delta9[5] = projected.d[5] - basis9.d[5];
        out_delta9[6] = projected.d[6] - basis9.d[6];
        out_delta9[7] = projected.d[7] - basis9.d[7];
        out_delta9[8] = projected.d[8] - basis9.d[8];
    }

    inline int32_t norm_q10(int64_t delta, int64_t denom) const {
        if (denom <= 0) denom = 1;
        int64_t scaled = delta * 1024;
        // Deterministic quantization: truncation toward zero.
        // We do NOT use banker's rounding, and we do not rely on floating math.
        int64_t xi = scaled / denom;
        if (xi < -1024) xi = -1024;
        if (xi >  1024) xi =  1024;
        return static_cast<int32_t>(xi);
    }

    inline int32_t spider_encode_9d(const Basis9& projected,
                                   const int32_t weights_q10[9],
                                   const int64_t denom_q[9]) const {
        int64_t delta9[9];
        compute_delta9(projected, delta9);

        int64_t s_q20 = 0;
        for (int i = 0; i < 9; ++i) {
            int32_t xi_q10 = norm_q10(delta9[i], denom_q[i]);
            s_q20 += static_cast<int64_t>(weights_q10[i]) * static_cast<int64_t>(xi_q10);
        }

        // Deterministic quantization: truncation toward zero.
        int64_t scaled = (s_q20 * static_cast<int64_t>(F_SCALE)) / (1024LL * 1024LL);
        return clamp_i32(static_cast<int32_t>(scaled));
    }

    inline SpiderCode4 spider_encode_9d_4(const Basis9& projected,
                                  const int32_t weights_q10[9],
                                  const int64_t denom_q[9]) const {
        const int32_t f_code = spider_encode_9d(projected, weights_q10, denom_q);
        const uint16_t a_code = amplitude_encode();
        const uint16_t v_code = voltage_encode(f_code, a_code);
        const uint16_t i_code = amperage_encode(f_code, a_code);
        SpiderCode4 out{f_code, a_code, v_code, i_code};
        return out;
    }

    inline void apply_frequency_weighted(int32_t f_code,
                                         int64_t step_factor_q32_32,
                                         uint16_t weight_q15) {
        // Weighted transport step for harmonic expansion.
        // weight_q15 is Q0.15 in [0, 1].
        const int64_t delta = static_cast<int64_t>(f_code) * (TURN_SCALE / F_SCALE);
        __int128 p = (__int128)delta;
        p = p * (__int128)step_factor_q32_32;
        p = p * (__int128)weight_q15;
        // >> 32 for q32_32, >> 15 for q15.
        const int64_t delta_scaled = (int64_t)(p >> (32 + 15));
        theta_q = wrap_turns(theta_q + delta_scaled);
        basis9.d[4] = theta_q;
    }

    inline uint16_t amplitude_encode() const {
        uint32_t u = static_cast<uint32_t>(chi_q % TURN_SCALE);
        return clamp_u16(u % A_MAX);
    }

    inline uint16_t voltage_encode(int32_t f_code, uint16_t a_code) const {
        // Voltage is treated as the available potential to do work during this pulse.
        // Deterministic proxy derived from phase (chi) and instantaneous frequency magnitude.
        // NOTE: v_code is a bounded carrier observable; it is not a physical voltage.
        const uint32_t base = static_cast<uint32_t>((chi_q >= 0 ? chi_q : -chi_q) % TURN_SCALE);
        const uint32_t fm = static_cast<uint32_t>(f_code >= 0 ? (uint32_t)f_code : (uint32_t)(-f_code));
        const uint32_t am = static_cast<uint32_t>(a_code);

        // Derived scaling (no magic shifts): spread |f| and A across V_MAX.
        // fm_scale is chosen to map roughly one full-scale frequency span into V_MAX.
        const uint32_t fm_scale = (F_SCALE == 0) ? 1u : (V_MAX / (uint32_t)F_SCALE);
        const uint32_t am_scale = (A_MAX == 0) ? 1u : (V_MAX / (uint32_t)A_MAX);
        const uint32_t v = base + (fm * fm_scale) + (am * am_scale);
        return clamp_u16(v % V_MAX);
    }

    inline uint16_t amperage_encode(int32_t f_code, uint16_t a_code) const {
        // Amperage is treated as the permitted transfer rate / load for this pulse.
        // Deterministic proxy derived from curvature/doppler channels and amplitude.
        // NOTE: i_code is a bounded carrier observable; it is not a physical current.
        const uint32_t c = static_cast<uint32_t>((curvature_q >= 0 ? curvature_q : -curvature_q) % TURN_SCALE);
        const uint32_t d = static_cast<uint32_t>((doppler_q >= 0 ? doppler_q : -doppler_q) % TURN_SCALE);
        const uint32_t am = static_cast<uint32_t>(a_code);

        // Derived scaling (no magic shifts): map curvature/doppler and amplitude
        // contributions across I_MAX.
        const uint32_t cd_scale = (TURN_SCALE == 0) ? 1u : (I_MAX / (uint32_t)TURN_SCALE);
        const uint32_t am_scale = (A_MAX == 0) ? 1u : (I_MAX / (uint32_t)A_MAX);
        const uint32_t i = (c * cd_scale) + (d * cd_scale) + (am * am_scale);
        return clamp_u16(i % I_MAX);
    }

    inline void harmonic_mode(uint16_t a_code, uint16_t mode_bucket,
                              uint16_t* out_k, uint16_t* out_strength_q15) const {
        if (mode_bucket == 0) mode_bucket = 1;
        uint16_t k = static_cast<uint16_t>(a_code / mode_bucket);
        uint16_t rem = static_cast<uint16_t>(a_code % mode_bucket);
        uint32_t strength = (static_cast<uint32_t>(rem) << 15) / static_cast<uint32_t>(mode_bucket);
        *out_k = k;
        *out_strength_q15 = static_cast<uint16_t>(strength);
    }

    inline void apply_frequency(int32_t f_code) {
        int64_t delta = static_cast<int64_t>(f_code) * (TURN_SCALE / F_SCALE);
        theta_q = wrap_turns(theta_q + delta);
        basis9.d[4] = theta_q;
    }

    inline void apply_frequency_transport_scaled(int32_t f_code, int64_t step_factor_q32_32) {
        // step_factor_q32_32 is a per-anchor time-dilation factor.
        // Delta is scaled deterministically by truncation.
        const int64_t delta = static_cast<int64_t>(f_code) * (TURN_SCALE / F_SCALE);
        __int128 p = (__int128)delta * (__int128)step_factor_q32_32;
        const int64_t delta_scaled = (int64_t)(p >> 32);
        theta_q = wrap_turns(theta_q + delta_scaled);
        basis9.d[4] = theta_q;
    }

    inline void apply_amplitude(uint16_t a_code) {
        chi_q += static_cast<int64_t>(a_code) * 1000;
        if (chi_q < 0) chi_q = 0;
        basis9.d[5] = chi_q;
    }

    inline void decay(int64_t lambda_scaled) {
        chi_q = chi_q / (1 + lambda_scaled);
        if (chi_q < 0) chi_q = 0;
        basis9.d[5] = chi_q;
    }

    inline int64_t leak_mass(int64_t lambda_scaled) {
        int64_t leak = (m_q * lambda_scaled) / TURN_SCALE;
        m_q -= leak;
        if (m_q < 0) m_q = 0;
        basis9.d[8] = m_q;
        return leak;
    }

    inline int64_t alignment_energy(const Anchor& other) const {
        int64_t d = delta_turns(theta_q, other.theta_q);
        int64_t ad = (d < 0) ? -d : d;
        return (TURN_SCALE / 2) - ad;
    }
};
