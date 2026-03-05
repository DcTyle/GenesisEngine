#include "GE_g2p_lexicon.hpp"
#include "GE_g2p_english.hpp"
#include "GE_voice_synth.hpp"

namespace genesis {

std::vector<PhonemeSpan> ge_text_to_phones_english(const std::string& text_utf8) {
    return ge_g2p_english_text_to_phones(text_utf8);
}

} // namespace genesis
