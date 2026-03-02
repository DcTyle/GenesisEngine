#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "GE_voice_synth.hpp"

namespace genesis {

// Predictive voice model (deterministic, self-hosted).
//
// Goal: learn a simple, auditable mapping from (text/phone features, trajectory context)
// to prosody controls (duration, pitch delta, energy) without pulling in external TTS engines.
//
// This model is intentionally small and deterministic:
// - Linear features
// - Ridge regularization
// - Fixed-size matrices
// - Deterministic solver order

struct VoiceProsodyControl {
    uint32_t dur_ms_u32 = 80;
    uint32_t f0_hz_q16_u32 = 0;   // 0 -> use cfg.f0
    int16_t  amp_q15_i16 = 0;     // 0 -> use cfg.amp
};

struct VoicePredictiveModel {
    bool ok = false;
    std::string info;

    // Feature dimension (fixed)
    uint32_t feat_dim_u32 = 0;

    // Linear weights for [dur_ms, f0_delta_ratio, amp_ratio]
    // Each is a vector of size feat_dim.
    std::vector<double> w_dur;
    std::vector<double> w_f0_ratio;
    std::vector<double> w_amp_ratio;

    // Baselines
    double base_dur_ms = 80.0;
    double base_f0_ratio = 1.0;
    double base_amp_ratio = 1.0;
};

// Serialize to a small JSON-like text format (no external deps).
bool ge_voice_model_save(const std::string& path, const VoicePredictiveModel& m);
bool ge_voice_model_load(const std::string& path, VoicePredictiveModel* out);

// Build deterministic feature vector for a phone span within a sequence.
// Features are intentionally simple and stable:
//  [1,
//   is_vowel,
//   is_silence,
//   is_voiced_consonant,
//   pos_norm,
//   prev_punct_pause,
//   next_punct_pause]
std::vector<double> ge_voice_phone_features(const std::vector<PhonemeSpan>& seq, uint32_t i);

// Apply model to a phone sequence to generate per-phone prosody controls.
std::vector<VoiceProsodyControl> ge_voice_apply_model(
    const std::vector<PhonemeSpan>& phones,
    const VoicePredictiveModel& model,
    const VoiceSynthConfig& cfg);

// Controlled synthesis: uses per-phone controls if provided.
TtsResult ge_synthesize_phones_to_wav_controlled(
    const std::vector<PhonemeSpan>& phones,
    const std::vector<VoiceProsodyControl>& controls,
    const VoiceSynthConfig& cfg);

} // namespace genesis
