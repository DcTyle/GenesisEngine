#pragma once
#include <string>
#include <vector>

namespace genesis {

struct PhonemeSpan;

std::vector<PhonemeSpan> ge_text_to_phones_english(const std::string& text_utf8);

} // namespace genesis
