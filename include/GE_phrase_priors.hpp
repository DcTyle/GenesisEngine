#pragma once

#include <cstdint>
#include <string>

namespace genesis {

enum class PhraseType : uint8_t {
    Statement = 0,
    Question = 1,
    Exclaim = 2,
};

struct PhrasePriors {
    // End-of-utterance pitch ratio (Q16.16)
    uint32_t end_pitch_ratio_q16_u32_stmt = 60000; // slight fall
    uint32_t end_pitch_ratio_q16_u32_q = 76000;    // rise
    uint32_t end_pitch_ratio_q16_u32_ex = 70000;   // assertive

    // End pause in ms
    uint32_t end_pause_ms_stmt = 120;
    uint32_t end_pause_ms_q = 150;
    uint32_t end_pause_ms_ex = 140;
};

bool ge_phrase_priors_read_ewcfg(const std::string& path, PhrasePriors* out);
bool ge_phrase_priors_write_ewcfg(const std::string& path, const PhrasePriors& pri);

PhraseType ge_phrase_type_from_text(const std::string& text_utf8);

} // namespace genesis
