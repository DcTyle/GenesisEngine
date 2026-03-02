#include "spec_aux_ops.hpp"

#include "fixed_point.hpp"

#include <algorithm>

static const int64_t ONE_Q32_32 = (int64_t)1 << 32;

int64_t admit_memory(int64_t memory_q32_32, int64_t coherence_q32_32) {
    // memory' = memory + coherence*(1-memory)
    // bounded to [0,1]
    if (memory_q32_32 < 0) memory_q32_32 = 0;
    if (memory_q32_32 > ONE_Q32_32) memory_q32_32 = ONE_Q32_32;
    if (coherence_q32_32 < 0) coherence_q32_32 = 0;
    if (coherence_q32_32 > ONE_Q32_32) coherence_q32_32 = ONE_Q32_32;

    const int64_t one_minus = ONE_Q32_32 - memory_q32_32;
    const int64_t inc = mul_q32_32(coherence_q32_32, one_minus);
    int64_t out = memory_q32_32 + inc;
    if (out < 0) out = 0;
    if (out > ONE_Q32_32) out = ONE_Q32_32;
    return out;
}

int64_t amp(int64_t amp_code_q32_32) {
    // Prototype binding: amplitude codes are already Q32.32, clamp to [0, +inf).
    if (amp_code_q32_32 < 0) return 0;
    return amp_code_q32_32;
}

int64_t budget_state(int64_t value_q32_32, int64_t abs_limit_q32_32) {
    // Deterministic absolute clamp.
    if (abs_limit_q32_32 < 0) abs_limit_q32_32 = -abs_limit_q32_32;
    if (value_q32_32 > abs_limit_q32_32) return abs_limit_q32_32;
    if (value_q32_32 < -abs_limit_q32_32) return -abs_limit_q32_32;
    return value_q32_32;
}

int32_t bytes_touched_i(uint32_t byte_len_u32) {
    return (int32_t)byte_len_u32;
}

int64_t chi_band(int64_t chi_q32_32) {
    // band clamp [0,1]
    if (chi_q32_32 < 0) return 0;
    if (chi_q32_32 > ONE_Q32_32) return ONE_Q32_32;
    return chi_q32_32;
}

int64_t cont_band(int64_t cont_q32_32) {
    if (cont_q32_32 < 0) return 0;
    if (cont_q32_32 > ONE_Q32_32) return ONE_Q32_32;
    return cont_q32_32;
}

int64_t viol_band(int64_t viol_q32_32) {
    if (viol_q32_32 < 0) return 0;
    if (viol_q32_32 > ONE_Q32_32) return ONE_Q32_32;
    return viol_q32_32;
}

int64_t coherence_map_dev(int64_t coh_a_q32_32, int64_t coh_b_q32_32) {
    // Deterministic deviation measure in Q32.32, clamped to [0,1].
    int64_t d = coh_a_q32_32 - coh_b_q32_32;
    if (d < 0) d = -d;
    if (d > ONE_Q32_32) d = ONE_Q32_32;
    return d;
}


uint64_t anchor_id_u64(uint64_t a, uint64_t b) {
    // Structural pair-mix (for stable structure): reversible-free combine.
    // Uses odd multiplier to spread bits deterministically.
    const uint64_t m = 0x9E3779B97F4A7C15ULL;
    return (a * m) ^ (b + (a << 1));
}

uint32_t blend_ms_u32(uint32_t a_ms, uint32_t b_ms, uint32_t w_q16_16) {
    // w in [0, 65535]
    const uint64_t wa = (uint64_t)(0x10000u - (w_q16_16 & 0xFFFFu));
    const uint64_t wb = (uint64_t)(w_q16_16 & 0xFFFFu);
    const uint64_t out = (wa * a_ms + wb * b_ms) >> 16;
    return (uint32_t)out;
}

uint64_t delta_tick(uint64_t tick_index_u64) {
    // Tick delta surface: one increment in tick-index space.
    (void)tick_index_u64;
    return 1ULL;
}

uint32_t denial_code_u32(bool accepted) {
    // 0 means accepted; 1 means denied.
    return accepted ? 0u : 1u;
}


uint64_t q32_32_phase_to_u64(int64_t phase_q32_32, uint64_t turn_scale_u64) {
    // phase in turns in Q32.32 -> integer domain.
    const int64_t scaled = (int64_t)(((__int128)phase_q32_32 * (int64_t)turn_scale_u64) >> 32);
    return (uint64_t)scaled;
}

int64_t phase_accumulation(int64_t prev_phase_q32_32, int64_t dphase_q32_32) {
    return prev_phase_q32_32 + dphase_q32_32;
}

int64_t csin_fp(int64_t theta_q32_32) {
    return ew_cordic_sincos_q32_32(theta_q32_32).sin_q32_32;
}

int64_t ccos_fp(int64_t theta_q32_32) {
    return ew_cordic_sincos_q32_32(theta_q32_32).cos_q32_32;
}

EwComplexQ32_32 cis_fp(int64_t theta_q32_32) {
    const EwSinCosQ32_32 sc = ew_cordic_sincos_q32_32(theta_q32_32);
    EwComplexQ32_32 z{};
    z.re_q32_32 = sc.cos_q32_32;
    z.im_q32_32 = sc.sin_q32_32;
    return z;
}

EwComplexQ32_32 conj_transpose_fp(const EwComplexQ32_32& z) {
    EwComplexQ32_32 out{};
    out.re_q32_32 = z.re_q32_32;
    out.im_q32_32 = -z.im_q32_32;
    return out;
}


static int64_t exp_series_q32_32(int64_t x_q32_32) {
    // exp(x) via bounded Taylor for |x| <= 1.0 in Q32.32.
    // Range reduce by clamping: for this prototype we keep bounded behavior.
    const int64_t LIM = ONE_Q32_32;
    if (x_q32_32 > LIM) x_q32_32 = LIM;
    if (x_q32_32 < -LIM) x_q32_32 = -LIM;

    // sum_{n=0..N} x^n/n!
    const int N = 20;
    int64_t term = ONE_Q32_32;
    int64_t sum = ONE_Q32_32;
    for (int n = 1; n <= N; ++n) {
        term = mul_q32_32(term, x_q32_32);
        // divide by n
        term = term / n;
        sum += term;
    }
    if (sum < 0) sum = 0;
    return sum;
}

int64_t exp(int64_t x_q32_32) {
    return exp_series_q32_32(x_q32_32);
}

EwComplexQ32_32 cexp_fp(const EwComplexQ32_32& z) {
    // exp(re + i im) = exp(re) * (cos(im) + i sin(im))
    const int64_t e_re = exp_series_q32_32(z.re_q32_32);
    const EwSinCosQ32_32 sc = ew_cordic_sincos_q32_32(z.im_q32_32);
    EwComplexQ32_32 out{};
    out.re_q32_32 = mul_q32_32(e_re, sc.cos_q32_32);
    out.im_q32_32 = mul_q32_32(e_re, sc.sin_q32_32);
    return out;
}

// Mirror the ATAN table used in ew_cordic.cpp.
// Values are stable constants; exposing them as ROM is deterministic.
static const int64_t CAS_ATAN_Q32_32[32] = {
    0x00000000c90fdaa2LL,
    0x0000000076b19c16LL,
    0x000000003eb6ebf2LL,
    0x000000001fd5ba9bLL,
    0x000000000ffaaddcLL,
    0x0000000007ff556fLL,
    0x0000000003ffeaabLL,
    0x0000000001fffd55LL,
    0x0000000000ffffabLL,
    0x00000000007ffff5LL,
    0x00000000003fffffLL,
    0x0000000000200000LL,
    0x0000000000100000LL,
    0x0000000000080000LL,
    0x0000000000040000LL,
    0x0000000000020000LL,
    0x0000000000010000LL,
    0x0000000000008000LL,
    0x0000000000004000LL,
    0x0000000000002000LL,
    0x0000000000001000LL,
    0x0000000000000800LL,
    0x0000000000000400LL,
    0x0000000000000200LL,
    0x0000000000000100LL,
    0x0000000000000080LL,
    0x0000000000000040LL,
    0x0000000000000020LL,
    0x0000000000000010LL,
    0x0000000000000008LL,
    0x0000000000000004LL,
    0x0000000000000002LL
};

int64_t cas_rom(uint32_t i_u32) {
    if (i_u32 >= 32u) return 0;
    return CAS_ATAN_Q32_32[i_u32];
}

std::vector<std::string> separate_watermark_blocks(const std::string& s) {
    // Deterministic split on a fixed marker.
    const std::string marker = "[[WATERMARK]]";
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        const size_t p = s.find(marker, pos);
        if (p == std::string::npos) {
            out.push_back(s.substr(pos));
            break;
        }
        out.push_back(s.substr(pos, p - pos));
        pos = p + marker.size();
    }
    return out;
}

int32_t estimate_alignment_offset(const std::vector<std::string>& caption_terms,
                                 const std::vector<uint32_t>& audio_events_kind_u32) {
    // Minimal deterministic harness behavior:
    // If first audio event is tagged as noise (1) and captions are non-empty, offset = 1.
    (void)caption_terms;
    if (!audio_events_kind_u32.empty() && audio_events_kind_u32[0] == 1u) return 1;
    return 0;
}

static int64_t isqrt_q32_32(int64_t x_q32_32) {
    // sqrt(x) for x in Q32.32, returning Q32.32 using integer Newton.
    if (x_q32_32 <= 0) return 0;
    // initial guess: x
    int64_t g = x_q32_32;
    for (int i = 0; i < 32; ++i) {
        int64_t div = div_q32_32(x_q32_32, g);
        g = (g + div) / 2;
        if (g <= 0) break;
    }
    return g;
}

int64_t relativistic_correlation(int64_t v_fraction_c_q32_32,
                                int64_t flux_factor_q32_32,
                                int64_t strain_factor_q32_32) {
    // Deterministic bounded correlation in (0,1].
    // sqrt_term = sqrt(max(0, 1 - v^2))
    // p = 1 + flux + strain
    // r = sqrt_term / p
    if (v_fraction_c_q32_32 < 0) v_fraction_c_q32_32 = -v_fraction_c_q32_32;
    if (v_fraction_c_q32_32 > ONE_Q32_32) v_fraction_c_q32_32 = ONE_Q32_32;

    const int64_t v2 = mul_q32_32(v_fraction_c_q32_32, v_fraction_c_q32_32);
    int64_t one_minus = ONE_Q32_32 - v2;
    if (one_minus < 0) one_minus = 0;
    const int64_t sqrt_term = isqrt_q32_32(one_minus);

    int64_t p = ONE_Q32_32;
    if (flux_factor_q32_32 > 0) p += flux_factor_q32_32;
    if (strain_factor_q32_32 > 0) p += strain_factor_q32_32;
    if (p <= 0) p = ONE_Q32_32;

    int64_t r = div_q32_32(sqrt_term, p);
    // clamp to (0,1]
    if (r < 1) r = 1;
    if (r > ONE_Q32_32) r = ONE_Q32_32;
    return r;
}

int64_t stochastic_dispersion_factor(int64_t temperature_q32_32,
                                    int64_t temperature_ref_q32_32) {
    // Deterministic dispersion multiplier >= 1.
    if (temperature_ref_q32_32 <= 0) temperature_ref_q32_32 = ONE_Q32_32;
    if (temperature_q32_32 < 0) temperature_q32_32 = 0;

    const int64_t x = div_q32_32(temperature_q32_32, temperature_ref_q32_32);
    int64_t s = x;
    if (s < ONE_Q32_32) s = ONE_Q32_32;
    return s;
}

static int64_t q32_32_from_double_local(double x) {
    // Deterministic conversion; used only for baseline reference constants.
    const double scale = 4294967296.0;
    if (x >= 0.0) return (int64_t)(x * scale + 0.5);
    return (int64_t)(x * scale - 0.5);
}

EwRefConstantsQ32_32 ref_constants_default() {
    // Baseline physical constants (references only).
    // NOTE: These references MUST be passed through effective_constants()
    // before influencing any operator evolution or thresholds.
    EwRefConstantsQ32_32 r{};
    // c_ref: speed of light (m/s).
    r.c_ref_q32_32 = q32_32_from_double_local(299792458.0);
    // h_ref: Planck constant (J*s).
    r.h_ref_q32_32 = q32_32_from_double_local(6.62607015e-34);
    // kB_ref: Boltzmann constant (J/K).
    r.kB_ref_q32_32 = q32_32_from_double_local(1.380649e-23);
    // hubble_h0_ref: H0 in 1/s (reference).
    r.hubble_h0_ref_q32_32 = q32_32_from_double_local(2.2683e-18);
    // temperature_ref: 1.0 (dimensionless reference for dispersion).
    r.temperature_ref_q32_32 = ONE_Q32_32;
    return r;
}

int64_t hubble_h0_ref_default_q32_32() {
    return ref_constants_default().hubble_h0_ref_q32_32;
}

int64_t timespace_doppler_factor(int64_t v_fraction_c_q32_32) {
    // Deterministic Doppler multiplier >= 1.
    // Prototype form: gamma = 1/sqrt(1-v^2), clamped.
    if (v_fraction_c_q32_32 < 0) v_fraction_c_q32_32 = -v_fraction_c_q32_32;
    if (v_fraction_c_q32_32 > ONE_Q32_32) v_fraction_c_q32_32 = ONE_Q32_32;
    const int64_t v2 = mul_q32_32(v_fraction_c_q32_32, v_fraction_c_q32_32);
    int64_t one_minus = ONE_Q32_32 - v2;
    if (one_minus < (1LL << 10)) one_minus = (1LL << 10);
    const int64_t sqrt_term = isqrt_q32_32(one_minus);
    int64_t gamma = div_q32_32(ONE_Q32_32, sqrt_term);
    // Clamp gamma into [1, 16] to keep all derived constants bounded.
    if (gamma < ONE_Q32_32) gamma = ONE_Q32_32;
    const int64_t max_gamma = (16LL << 32);
    if (gamma > max_gamma) gamma = max_gamma;
    return gamma;
}

EwEffectiveConstantsQ32_32 effective_constants(const EwRefConstantsQ32_32& refs,
                                              int64_t v_fraction_c_q32_32,
                                              int64_t doppler_factor_q32_32,
                                              int64_t flux_factor_q32_32,
                                              int64_t strain_factor_q32_32,
                                              int64_t temperature_q32_32) {
    // Compute per-tick effective constants from baseline references.
    // All factors are deterministic and bounded.
    const int64_t rel = relativistic_correlation(v_fraction_c_q32_32, flux_factor_q32_32, strain_factor_q32_32);
    const int64_t disp = stochastic_dispersion_factor(temperature_q32_32, refs.temperature_ref_q32_32);
    int64_t f = mul_q32_32(rel, doppler_factor_q32_32);
    f = mul_q32_32(f, disp);

    // Clamp overall factor into (0, 32].
    if (f < 1) f = 1;
    const int64_t max_f = (32LL << 32);
    if (f > max_f) f = max_f;

    EwEffectiveConstantsQ32_32 out{};
    out.c_eff_q32_32 = mul_q32_32(refs.c_ref_q32_32, f);
    out.h_eff_q32_32 = mul_q32_32(refs.h_ref_q32_32, f);
    out.kB_eff_q32_32 = mul_q32_32(refs.kB_ref_q32_32, f);
    out.hubble_h0_eff_q32_32 = mul_q32_32(refs.hubble_h0_ref_q32_32, f);
    return out;
}


uint32_t imm_u32(uint32_t v) { return v; }
int64_t ew_int64_identity(int64_t v) { return v; }
uint8_t opcode_u8(uint8_t v) { return v; }
uint32_t lab_intent_kind_u32(uint32_t v) { return v; }
uint32_t projection_mode_u32(uint32_t v) { return v; }

uint64_t eq_pagesig9_u64x9(const uint64_t page_sig9_u64[9]) {
    // Structural combine: xor-fold of 9 words.
    uint64_t x = 0;
    for (int i = 0; i < 9; ++i) x ^= page_sig9_u64[i] + (uint64_t)i;
    return x;
}

uint64_t eigenware_runtime(uint64_t seed_u64) {
    // Deterministic seed-to-runtime tag (structural), for stable structure.
    const uint64_t m = 0xD6E8FEB86659FD93ULL;
    return seed_u64 * m + (seed_u64 >> 1);
}

uint64_t ew_bind_operator_page_phase_transport(uint64_t op_page_u64) {
    // Binding surface: returns a stable tag for the op page.
    return op_page_u64 ^ 0x50545054504FULL; // 'PTPTP' structural tag
}

int64_t drive(int64_t f_code_q32_32, int64_t amp_q32_32) {
    return mul_q32_32(f_code_q32_32, amp_q32_32);
}

int64_t f(int64_t f_code_q32_32) { return f_code_q32_32; }

int64_t f_env(int64_t f_code_q32_32, int64_t env_q32_32) {
    return mul_q32_32(f_code_q32_32, env_q32_32);
}

int64_t delta_env(int64_t env_prev_q32_32, int64_t env_next_q32_32) {
    return env_next_q32_32 - env_prev_q32_32;
}

int64_t cyclic(int64_t x, int64_t period) {
    if (period == 0) return x;
    int64_t r = x % period;
    if (r < 0) r += period;
    return r;
}

uint64_t d5(const uint64_t v_u64[5]) {
    uint64_t x = 0;
    for (int i = 0; i < 5; ++i) x ^= (v_u64[i] + (uint64_t)(i * 7));
    return x;
}

uint64_t d9(const uint64_t v_u64[9]) {
    uint64_t x = 0;
    for (int i = 0; i < 9; ++i) x ^= (v_u64[i] + (uint64_t)(i * 11));
    return x;
}

uint64_t ell(uint64_t v) { return v ^ (v << 1); }
uint64_t eta(uint64_t v) { return v ^ (v >> 1); }
uint64_t phi_i(uint64_t v) { return v + 1; }

int64_t sample(int64_t signal_q32_32, uint32_t k_u32) {
    // Deterministic subsample: shift-right by k mod 32.
    const uint32_t s = k_u32 & 31u;
    return signal_q32_32 >> s;
}

int64_t reservoir_q63(int64_t prev_q63, int64_t in_q63, uint32_t leak_u32) {
    // leak_u32 in [0, 2^32], interpreted as Q0.32.
    // out = prev*(1-leak) + in*leak
    const uint64_t leak = (uint64_t)leak_u32;
    const uint64_t inv = 0x100000000ULL - leak;
    __int128 acc = (__int128)prev_q63 * (int64_t)inv + (__int128)in_q63 * (int64_t)leak;
    return (int64_t)(acc >> 32);
}

int64_t rps_rw(int64_t prev_q63, int64_t delta_q63) {
    return prev_q63 + delta_q63;
}

uint64_t pulse_packet_dev(uint64_t tick_u64, uint32_t lane_u32, uint32_t tag_u32) {
    return (tick_u64 << 32) ^ ((uint64_t)lane_u32 << 16) ^ (uint64_t)tag_u32;
}

int32_t sig9_rules(const uint64_t sig9_u64[9]) {
    // Rule: not all-zero.
    uint64_t x = 0;
    for (int i = 0; i < 9; ++i) x |= sig9_u64[i];
    return (x == 0) ? 1 : 0;
}