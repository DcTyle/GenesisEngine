#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace genesis {

struct PhonemeSpan;

// Forward-declare to avoid heavy includes.
enum class PauseKind : uint8_t;

// Legacy convenience API.
std::vector<PhonemeSpan> ge_text_to_phones_english(const std::string& text_utf8);


// Preferred API: returns pause kinds + pause strength aligned 1:1 with returned phones.
// Non-"SP" phones will have PauseKind::Unknown and strength 0.
bool ge_text_to_phones_english_with_pause_meta(const std::string& text_utf8,
                                               std::vector<PhonemeSpan>* out_phones,
                                               std::vector<PauseKind>* out_pause_kinds,
                                               std::vector<uint8_t>* out_pause_strength_u8);

// Preferred API: returns pause kinds aligned 1:1 with returned phones.
// Non-"SP" phones will have PauseKind::Unknown.
bool ge_text_to_phones_english_with_pause_kinds(const std::string& text_utf8,
                                               std::vector<PhonemeSpan>* out_phones,
                                               std::vector<PauseKind>* out_pause_kinds);

} // namespace genesis
