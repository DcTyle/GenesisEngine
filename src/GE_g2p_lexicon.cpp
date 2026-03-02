#include "GE_g2p_lexicon.hpp"
#include "GE_voice_synth.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace genesis {

static std::string ge_upper_ascii(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (unsigned char uc : s) o.push_back((char)std::toupper(uc));
    return o;
}

struct LexEntry { const char* w; const char* phones; };

// Engine-owned minimal lexicon (sorted for binary search).
static const LexEntry kLex[] = {
    {"A", "AH0"},
    {"AI", "EY1 AY1"},
    {"AND", "AE1 N D"},
    {"ARE", "AA1 R"},
    {"ENGINE", "EH1 N JH AH0 N"},
    {"FOR", "F AO1 R"},
    {"GAME", "G EY1 M"},
    {"HELLO", "HH AH0 L OW1"},
    {"I", "AY1"},
    {"IS", "IH1 Z"},
    {"IT", "IH1 T"},
    {"OF", "AH1 V"},
    {"PHYSICS", "F IH1 Z IH0 K S"},
    {"SUBSTRATE", "S AH1 B S T R EY2 T"},
    {"THE", "DH AH0"},
    {"THIS", "DH IH1 S"},
    {"TO", "T UW1"},
    {"YOU", "Y UW1"},
};

static bool lex_lookup(const std::string& upper_word, const char** out_phones) {
    const size_t n = sizeof(kLex) / sizeof(kLex[0]);
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = (lo + hi) >> 1;
        const int cmp = std::strcmp(upper_word.c_str(), kLex[mid].w);
        if (cmp == 0) { *out_phones = kLex[mid].phones; return true; }
        if (cmp < 0) hi = mid; else lo = mid + 1;
    }
    return false;
}

static void emit_phones_string(const char* phones, std::vector<PhonemeSpan>& out) {
    auto push = [&](const std::string& ph, uint32_t ms) {
        PhonemeSpan s; s.phone = ph; s.dur_ms_u32 = ms; out.push_back(s);
    };

    std::string cur;
    for (const char* p = phones; ; ++p) {
        const char c = *p;
        if (c == 0 || c == ' ') {
            if (!cur.empty()) {
                const bool vowel = (cur.size() >= 2 && (cur[0]=='A'||cur[0]=='E'||cur[0]=='I'||cur[0]=='O'||cur[0]=='U'));
                push(cur, vowel ? 95u : 75u);
                cur.clear();
            }
            if (c == 0) break;
        } else {
            cur.push_back(c);
        }
    }
}

// Canonical deterministic OOV grapheme path.
static void emit_oov_word(const std::string& upper_word, std::vector<PhonemeSpan>& out) {
    auto push = [&](const std::string& ph, uint32_t ms) {
        PhonemeSpan s; s.phone = ph; s.dur_ms_u32 = ms; out.push_back(s);
    };
    for (size_t i = 0; i < upper_word.size(); ++i) {
        const char c = upper_word[i];
        if (c < 'A' || c > 'Z') continue;
        switch (c) {
            case 'A': push("AH0", 90); break;
            case 'B': push("B", 70); break;
            case 'C': push("K", 70); break;
            case 'D': push("D", 70); break;
            case 'E': push("EH0", 90); break;
            case 'F': push("F", 70); break;
            case 'G': push("G", 70); break;
            case 'H': push("HH", 60); break;
            case 'I': push("IH0", 90); break;
            case 'J': push("JH", 80); break;
            case 'K': push("K", 70); break;
            case 'L': push("L", 70); break;
            case 'M': push("M", 70); break;
            case 'N': push("N", 70); break;
            case 'O': push("AO0", 90); break;
            case 'P': push("P", 70); break;
            case 'Q': push("K", 50); push("W", 50); break;
            case 'R': push("R", 70); break;
            case 'S': push("S", 70); break;
            case 'T': push("T", 70); break;
            case 'U': push("UH0", 90); break;
            case 'V': push("V", 70); break;
            case 'W': push("W", 70); break;
            case 'X': push("K", 50); push("S", 50); break;
            case 'Y': push("Y", 70); break;
            case 'Z': push("Z", 70); break;
            default: break;
        }
    }
}

std::vector<PhonemeSpan> ge_text_to_phones_english(const std::string& text_utf8) {
    std::vector<PhonemeSpan> out;
    out.reserve(text_utf8.size() / 2);

    auto push_sp = [&](uint32_t ms) {
        PhonemeSpan s; s.phone = "SP"; s.dur_ms_u32 = ms; out.push_back(s);
    };

    std::string word;
    for (size_t i = 0; i <= text_utf8.size(); ++i) {
        const char c = (i < text_utf8.size()) ? text_utf8[i] : 0;
        const bool is_letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (is_letter) { word.push_back(c); continue; }

        if (!word.empty()) {
            const std::string up = ge_upper_ascii(word);
            const char* phones = nullptr;
            if (lex_lookup(up, &phones)) emit_phones_string(phones, out);
            else emit_oov_word(up, out);
            word.clear();
        }

        if (c == 0) break;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') push_sp(90);
        else if (c == '.' || c == '!' || c == '?') push_sp(160);
        else if (c == ',' || c == ';' || c == ':') push_sp(120);
    }

    if (out.empty()) push_sp(200);
    return out;
}

} // namespace genesis
