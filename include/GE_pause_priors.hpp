#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "GE_voice_synth.hpp"

namespace genesis {

// Pause kind aligned to the phoneme stream. Only meaningful for "SP" phones.
// NOTE: keep values stable for deterministic configs.
enum class PauseKind : uint8_t {
    Unknown = 0,
    Space = 1,
    Comma = 2,
    Clause = 3,
    TerminalPeriod = 4,
    TerminalQuestion = 5,
    TerminalExclaim = 6,
    Newline = 7,
};



struct PausePriorRow {
    PauseKind kind = PauseKind::Unknown;
    uint8_t strength_bin_u8 = 0; // 0..4, quantized from pause strength multiplier
    // Context class derived from the phone stream around the pause.
    // 0 = none/start, 1 = prev_vowel, 2 = prev_consonant.
    uint8_t prev_class_u8 = 0;
    // Next context class derived from the phone stream around the pause.
    // 0 = none/end, 1 = next_vowel, 2 = next_consonant.
    uint8_t next_class_u8 = 0;
    // Q16.16 multiplier applied to the current pause duration.
    uint32_t mean_dur_mul_q16_u32 = 65536u;
    uint32_t count_u32 = 0;
};

struct PausePriors {
    bool ok = false;
    std::string info;
    std::vector<PausePriorRow> rows;
};

// Produce pause kinds aligned to phones. Non-SP phones get Unknown.
std::vector<PauseKind> ge_pause_kinds_from_text_and_phones(const std::string& text_utf8,
                                                          const std::vector<PhonemeSpan>& phones);

// Legacy helper to derive pause strength markers aligned to phones.
// Non-SP phones get strength 0.
bool ge_pause_meta_from_text_and_phones(const std::string& text_utf8,
                                       const std::vector<PhonemeSpan>& phones,
                                       std::vector<PauseKind>* out_kinds,
                                       std::vector<uint8_t>* out_strength_u8);

// Build pause priors from observed per-phone duration multipliers (Q16.16).
// Inputs:
//  - pause_kinds: aligned to phones (same length as phone_str_obs)
//  - phone_str_obs: phone strings aligned to durations (must include "SP")
//  - dur_mul_q16_obs: duration multipliers (Q16.16) per phone
bool ge_pause_priors_build_from_observations(const std::vector<PauseKind>& pause_kinds,
                                            const std::vector<uint8_t>& pause_strength_u8,
                                            const std::vector<std::string>& phone_str_obs,
                                            const std::vector<uint32_t>& dur_mul_q16_obs,
                                            PausePriors* out);

bool ge_pause_priors_write_ewcfg(const std::string& path_utf8, const PausePriors& pri);
bool ge_pause_priors_read_ewcfg(const std::string& path_utf8, PausePriors* out);

// Apply priors to controls: only affects SP phones.
// blend_q16: 0..1 in Q16.16
bool ge_pause_priors_apply_blend(const PausePriors& pri,
                                 const std::vector<PauseKind>& pause_kinds,
                                 const std::vector<uint8_t>& pause_strength_u8,
                                 const std::vector<std::string>& phone_str,
                                 uint32_t blend_q16,
                                 std::vector<struct VoiceProsodyControl>* io);

const char* ge_pause_kind_to_cstr(PauseKind k);
bool ge_pause_kind_from_token(const std::string& tok, PauseKind* out);

uint32_t ge_pause_strength_mul_q16_from_strength(uint8_t strength);
uint8_t ge_pause_strength_bin_from_mul_q16(uint32_t mul_q16);
uint8_t ge_pause_strength_bin_from_strength(uint8_t strength);
const char* ge_pause_bin_to_cstr(uint8_t bin_u8);
bool ge_pause_bin_from_token(const std::string& tok, uint8_t* out_bin_u8);

const char* ge_pause_prev_class_to_cstr(uint8_t prev_class_u8);
bool ge_pause_prev_class_from_token(const std::string& tok, uint8_t* out_prev_class_u8);

const char* ge_pause_next_class_to_cstr(uint8_t next_class_u8);
bool ge_pause_next_class_from_token(const std::string& tok, uint8_t* out_next_class_u8);

} // namespace genesis
