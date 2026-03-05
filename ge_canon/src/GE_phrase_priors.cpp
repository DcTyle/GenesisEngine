#include "GE_phrase_priors.hpp"

#include <cctype>
#include <cstdio>

namespace genesis {

static bool ge_read_all(const std::string& path, std::string* out) {
    if (!out) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); return false; }
    out->assign((size_t)sz, '\0');
    if (sz > 0) {
        if (std::fread(out->data(), 1, (size_t)sz, f) != (size_t)sz) { std::fclose(f); return false; }
    }
    std::fclose(f);
    return true;
}

static bool ge_write_all(const std::string& path, const std::string& s) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    if (!s.empty()) {
        if (std::fwrite(s.data(), 1, s.size(), f) != s.size()) { std::fclose(f); return false; }
    }
    std::fclose(f);
    return true;
}

static bool ge_parse_u32_line(const std::string& txt, const char* key, uint32_t* out) {
    if (!out) return false;
    const std::string k = key;
    size_t p = txt.find(k);
    if (p == std::string::npos) return false;
    size_t i = p + k.size();
    while (i < txt.size() && (txt[i] == ' ' || txt[i] == '\t')) i++;
    size_t j = i;
    while (j < txt.size() && std::isdigit((unsigned char)txt[j])) j++;
    if (j <= i) return false;
    *out = (uint32_t)std::strtoul(txt.substr(i, j - i).c_str(), nullptr, 10);
    return true;
}

bool ge_phrase_priors_read_ewcfg(const std::string& path, PhrasePriors* out) {
    if (!out) return false;
    std::string txt;
    if (!ge_read_all(path, &txt)) return false;
    PhrasePriors pri = *out;
    (void)ge_parse_u32_line(txt, "end_pitch_ratio_q16_stmt", &pri.end_pitch_ratio_q16_u32_stmt);
    (void)ge_parse_u32_line(txt, "end_pitch_ratio_q16_question", &pri.end_pitch_ratio_q16_u32_q);
    (void)ge_parse_u32_line(txt, "end_pitch_ratio_q16_exclaim", &pri.end_pitch_ratio_q16_u32_ex);
    (void)ge_parse_u32_line(txt, "end_pause_ms_stmt", &pri.end_pause_ms_stmt);
    (void)ge_parse_u32_line(txt, "end_pause_ms_question", &pri.end_pause_ms_q);
    (void)ge_parse_u32_line(txt, "end_pause_ms_exclaim", &pri.end_pause_ms_ex);
    *out = pri;
    return true;
}

bool ge_phrase_priors_write_ewcfg(const std::string& path, const PhrasePriors& pri) {
    std::string out;
    out += "# phrase priors (deterministic)\n";
    out += "end_pitch_ratio_q16_stmt " + std::to_string(pri.end_pitch_ratio_q16_u32_stmt) + "\n";
    out += "end_pitch_ratio_q16_question " + std::to_string(pri.end_pitch_ratio_q16_u32_q) + "\n";
    out += "end_pitch_ratio_q16_exclaim " + std::to_string(pri.end_pitch_ratio_q16_u32_ex) + "\n";
    out += "end_pause_ms_stmt " + std::to_string(pri.end_pause_ms_stmt) + "\n";
    out += "end_pause_ms_question " + std::to_string(pri.end_pause_ms_q) + "\n";
    out += "end_pause_ms_exclaim " + std::to_string(pri.end_pause_ms_ex) + "\n";
    return ge_write_all(path, out);
}

PhraseType ge_phrase_type_from_text(const std::string& text_utf8) {
    for (size_t i = text_utf8.size(); i-- > 0;) {
        unsigned char c = (unsigned char)text_utf8[i];
        if (c==' '||c=='\t'||c=='\n'||c=='\r') continue;
        if (c=='?') return PhraseType::Question;
        if (c=='!') return PhraseType::Exclaim;
        return PhraseType::Statement;
    }
    return PhraseType::Statement;
}

} // namespace genesis
