#pragma once

#include <string>
#include <vector>

#include "GE_voice_synth.hpp"

namespace genesis {

// Deterministic English grapheme-to-phoneme:
// - No external dictionary
// - Emits "WB" between words and "SP" pauses for punctuation
// - Uses a compact ARPABET-like inventory
std::vector<PhonemeSpan> ge_g2p_english_text_to_phones(const std::string& text_utf8);

bool ge_phone_is_pause(const std::string& phone_upper);
bool ge_phone_is_vowel(const std::string& phone_upper);
bool ge_phone_is_voiced(const std::string& phone_upper);

} // namespace genesis
