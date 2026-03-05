#pragma once

#include <cstdint>
#include <climits>
#include <string>
#include <vector>

#include "ew_cordic.hpp"
#include "fixed_point.hpp"

// Spec/Equations auxiliary operators that are referenced by the canonical
// registries but were not previously exposed as callable surfaces.
//
// Design rule for this repo: these operators are deterministic, bounded,
// and do not perform external I/O. External system integration is handled
// only by UE/adapter modules.

struct EwComplexQ32_32 {
    int64_t re_q32_32;
    int64_t im_q32_32;
};


static inline int64_t sat_add_i64(int64_t a, int64_t b) {
    __int128 s = (__int128)a + (__int128)b;
    if (s > (__int128)INT64_MAX) return INT64_MAX;
    if (s < (__int128)INT64_MIN) return INT64_MIN;
    return (int64_t)s;
}


// --- Memory / amplitude / budgeting helpers ---
int64_t admit_memory(int64_t memory_q32_32, int64_t coherence_q32_32);
int64_t amp(int64_t amp_code_q32_32);
int64_t budget_state(int64_t value_q32_32, int64_t abs_limit_q32_32);
int32_t bytes_touched_i(uint32_t byte_len_u32);

// --- Bands (Q32.32 in [0,1] unless otherwise stated) ---
int64_t chi_band(int64_t chi_q32_32);
int64_t cont_band(int64_t cont_q32_32);
int64_t viol_band(int64_t viol_q32_32);
int64_t coherence_map_dev(int64_t coh_a_q32_32, int64_t coh_b_q32_32);

// --- Phase / tick helpers ---
uint64_t anchor_id_u64(uint64_t a, uint64_t b);
uint32_t blend_ms_u32(uint32_t a_ms, uint32_t b_ms, uint32_t w_q16_16);
uint64_t delta_tick(uint64_t tick_index_u64);
uint64_t q32_32_phase_to_u64(int64_t phase_q32_32, uint64_t turn_scale_u64);
int64_t phase_accumulation(int64_t prev_phase_q32_32, int64_t dphase_q32_32);

// --- Trig/exp fixed-point (deterministic) ---
int64_t csin_fp(int64_t theta_q32_32);
int64_t ccos_fp(int64_t theta_q32_32);
EwComplexQ32_32 cis_fp(int64_t theta_q32_32);
int64_t exp(int64_t x_q32_32);
EwComplexQ32_32 cexp_fp(const EwComplexQ32_32& z);
EwComplexQ32_32 conj_transpose_fp(const EwComplexQ32_32& z);

// Expose the CORDIC atan table used by ew_cordic (ROM semantics).
int64_t cas_rom(uint32_t i_u32);

// --- Simple structural helpers used by harnesses ---
std::vector<std::string> separate_watermark_blocks(const std::string& s);
int32_t estimate_alignment_offset(const std::vector<std::string>& caption_terms,
                                 const std::vector<uint32_t>& audio_events_kind_u32);

// --- Constraint pipeline (effective constants) ---
struct EwRefConstantsQ32_32 {
    // Baseline reference constants (Q32.32). These are references only and
    // MUST NOT be used directly in evolution; apply effective_constants()
    // to obtain *_eff values for the current tick.
    int64_t c_ref_q32_32;
    int64_t h_ref_q32_32;
    int64_t kB_ref_q32_32;
    int64_t hubble_h0_ref_q32_32;
    int64_t temperature_ref_q32_32;
};

struct EwEffectiveConstantsQ32_32 {
    // Per-tick effective constants (Q32.32). These are the ONLY constant
    // values permitted to influence operator evolution in the substrate.
    int64_t c_eff_q32_32;
    int64_t h_eff_q32_32;
    int64_t kB_eff_q32_32;
    int64_t hubble_h0_eff_q32_32;
};

EwRefConstantsQ32_32 ref_constants_default();
int64_t timespace_doppler_factor(int64_t v_fraction_c_q32_32);
EwEffectiveConstantsQ32_32 effective_constants(const EwRefConstantsQ32_32& refs,
                                              int64_t v_fraction_c_q32_32,
                                              int64_t doppler_factor_q32_32,
                                              int64_t flux_factor_q32_32,
                                              int64_t strain_factor_q32_32,
                                              int64_t temperature_q32_32);

// Convenience: reference H0 for callers that need a baseline without
// embedding numeric literals in adapter or harness code.
int64_t hubble_h0_ref_default_q32_32();

int64_t relativistic_correlation(int64_t v_fraction_c_q32_32,
                                int64_t flux_factor_q32_32,
                                int64_t strain_factor_q32_32);
int64_t stochastic_dispersion_factor(int64_t temperature_q32_32,
                                    int64_t temperature_ref_q32_32);

// --- Small typed value helpers (explicit casts) ---
uint32_t imm_u32(uint32_t v);
// NOTE: avoid name collision with Unreal's int64 typedef.
int64_t ew_int64_identity(int64_t v);
uint8_t opcode_u8(uint8_t v);
uint32_t lab_intent_kind_u32(uint32_t v);
uint32_t projection_mode_u32(uint32_t v);

// --- Misc canonical surfaces (minimal deterministic bindings) ---
// These are intentionally small in this prototype; they expose the symbol
// so other modules can bind without relying on hidden locals.
uint64_t eq_pagesig9_u64x9(const uint64_t page_sig9_u64[9]);
uint64_t eigenware_runtime(uint64_t seed_u64);
uint64_t ew_bind_operator_page_phase_transport(uint64_t op_page_u64);

// Field / drive helpers (bounded) 
int64_t drive(int64_t f_code_q32_32, int64_t amp_q32_32);
int64_t f(int64_t f_code_q32_32);
int64_t f_env(int64_t f_code_q32_32, int64_t env_q32_32);
int64_t delta_env(int64_t env_prev_q32_32, int64_t env_next_q32_32);
uint32_t denial_code_u32(bool accepted);

// Simple cyclic wrap for signed values.
int64_t cyclic(int64_t x, int64_t period);

// Placeholders for spec-named math blocks that are currently structural in this prototype.
// They are deterministic and bounded, and can be refined without changing names.
uint64_t d5(const uint64_t v_u64[5]);
uint64_t d9(const uint64_t v_u64[9]);
uint64_t ell(uint64_t v);
uint64_t eta(uint64_t v);
uint64_t phi_i(uint64_t v);

// Deterministic sample helper.
int64_t sample(int64_t signal_q32_32, uint32_t k_u32);

// Reservoir / RW stand-ins are forbidden; implement deterministic bounded forms.
int64_t reservoir_q63(int64_t prev_q63, int64_t in_q63, uint32_t leak_u32);
int64_t rps_rw(int64_t prev_q63, int64_t delta_q63);

// Debug/dev packet tag builder.
uint64_t pulse_packet_dev(uint64_t tick_u64, uint32_t lane_u32, uint32_t tag_u32);

// Signature rules surface (returns 0 on pass, nonzero on fail).
int32_t sig9_rules(const uint64_t sig9_u64[9]);
