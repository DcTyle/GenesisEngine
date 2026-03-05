#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "GE_voice_predictive_model.hpp"

namespace genesis {

// Forward-declare to avoid heavier headers.
enum class PauseKind : uint8_t;

// Deterministic prosody planner.
// Produces per-phone controls using simple rules (punctuation + position).
std::vector<VoiceProsodyControl> ge_voice_plan_prosody(
    const std::vector<PhonemeSpan>& phones,
    const VoiceSynthConfig& cfg,
    const std::string& text_utf8);

// Preferred overload: accepts pause kinds + pause strength aligned 1:1 with phones.
// For non-"SP" phones, pause_kind should be Unknown and strength 0.
std::vector<VoiceProsodyControl> ge_voice_plan_prosody_with_pause_meta(
    const std::vector<PhonemeSpan>& phones,
    const VoiceSynthConfig& cfg,
    const std::vector<PauseKind>& pause_kinds,
    const std::vector<uint8_t>& pause_strength_u8,
    const std::string& text_utf8);

// Back-compat: pause kinds only (strength defaults to 1 for SP).
std::vector<VoiceProsodyControl> ge_voice_plan_prosody_with_pause_kinds(
    const std::vector<PhonemeSpan>& phones,
    const VoiceSynthConfig& cfg,
    const std::vector<PauseKind>& pause_kinds,
    const std::string& text_utf8);

} // namespace genesis
