#include "GE_g2p_lexicon.hpp"
#include "GE_voice_synth.hpp"
#include "GE_pause_priors.hpp"

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

// Deterministic rule-based OOV G2P (English-first).
static void emit_oov_word(const std::string& upper_word, std::vector<PhonemeSpan>& out) {
    auto push = [&](const std::string& ph, uint32_t ms) {
        PhonemeSpan s; s.phone = ph; s.dur_ms_u32 = ms; out.push_back(s);
    };

    bool stressed = false;
    auto push_vowel = [&](const std::string& base_phone) {
        // Stress the first vowel in the word deterministically.
        std::string ph = base_phone;
        if (!stressed) {
            if (ph.size() >= 2 && (ph.back() == '0' || ph.back() == '1' || ph.back() == '2')) {
                ph.back() = '1';
            } else {
                ph += "1";
            }
            stressed = true;
        } else {
            if (ph.size() >= 2 && (ph.back() == '0' || ph.back() == '1' || ph.back() == '2')) {
                ph.back() = '0';
            } else {
                ph += "0";
            }
        }
        push(ph, 95);
    };

    for (size_t i = 0; i < upper_word.size(); ) {
        char c = upper_word[i];
        if (c < 'A' || c > 'Z') { ++i; continue; }
        char c1 = (i + 1 < upper_word.size()) ? upper_word[i + 1] : 0;
        char c2 = (i + 2 < upper_word.size()) ? upper_word[i + 2] : 0;

        // Trigraphs.
        if (c=='T' && c1=='I' && c2=='O') { push("SH", 70); push_vowel("AH0"); push("N", 70); i += 3; continue; }
        if (c=='S' && c1=='I' && c2=='O') { push("ZH", 70); push_vowel("AH0"); i += 3; continue; }

        // Digraph consonants.
        if (c=='T' && c1=='H') { push("TH", 70); i += 2; continue; }
        if (c=='D' && c1=='H') { push("DH", 70); i += 2; continue; }
        if (c=='S' && c1=='H') { push("SH", 70); i += 2; continue; }
        if (c=='C' && c1=='H') { push("CH", 80); i += 2; continue; }
        if (c=='P' && c1=='H') { push("F", 70); i += 2; continue; }
        if (c=='N' && c1=='G') { push("NG", 70); i += 2; continue; }
        if (c=='Q' && c1=='U') { push("K", 50); push("W", 50); i += 2; continue; }

        // Digraph vowels.
        if (c=='E' && c1=='E') { push_vowel("IY0"); i += 2; continue; }
        if (c=='O' && c1=='O') { push_vowel("UW0"); i += 2; continue; }
        if (c=='E' && c1=='A') { push_vowel("IY0"); i += 2; continue; }
        if (c=='A' && c1=='I') { push_vowel("EY0"); i += 2; continue; }
        if (c=='O' && c1=='A') { push_vowel("OW0"); i += 2; continue; }
        if (c=='O' && c1=='U') { push_vowel("AW0"); i += 2; continue; }
        if (c=='O' && c1=='W') { push_vowel("AW0"); i += 2; continue; }
        if (c=='A' && c1=='Y') { push_vowel("EY0"); i += 2; continue; }
        if (c=='E' && c1=='R') { push_vowel("ER0"); i += 2; continue; }
        if (c=='A' && c1=='R') { push_vowel("AA0"); push("R", 70); i += 2; continue; }
        if (c=='O' && c1=='R') { push_vowel("AO0"); push("R", 70); i += 2; continue; }

        // Single letters with context.
        if (c == 'C') {
            if (c1=='E' || c1=='I' || c1=='Y') push("S", 70);
            else push("K", 70);
            i += 1; continue;
        }
        if (c == 'G') {
            if (c1=='E' || c1=='I' || c1=='Y') push("JH", 80);
            else push("G", 70);
            i += 1; continue;
        }
        if (c == 'X') { push("K", 50); push("S", 50); i += 1; continue; }

        // Vowels.
        if (c == 'A') { push_vowel("AE0"); i += 1; continue; }
        if (c == 'E') { push_vowel("EH0"); i += 1; continue; }
        if (c == 'I') { push_vowel("IH0"); i += 1; continue; }
        if (c == 'O') { push_vowel("AA0"); i += 1; continue; }
        if (c == 'U') { push_vowel("AH0"); i += 1; continue; }
        if (c == 'Y') {
            if (i + 1 >= upper_word.size()) push_vowel("IY0");
            else push("Y", 70);
            i += 1; continue;
        }

        // Consonants.
        switch (c) {
            case 'B': push("B", 70); break;
            case 'D': push("D", 70); break;
            case 'F': push("F", 70); break;
            case 'H': push("HH", 60); break;
            case 'J': push("JH", 80); break;
            case 'K': push("K", 70); break;
            case 'L': push("L", 70); break;
            case 'M': push("M", 70); break;
            case 'N': push("N", 70); break;
            case 'P': push("P", 70); break;
            case 'R': push("R", 70); break;
            case 'S': push("S", 70); break;
            case 'T': push("T", 70); break;
            case 'V': push("V", 70); break;
            case 'W': push("W", 70); break;
            case 'Z': push("Z", 70); break;
            default: break;
        }
        i += 1;
    }

    if (!stressed) push_vowel("AH0");
}

static PauseKind ge_classify_pause_char(char c) {
    if (c == ' ' || c == '\t') return PauseKind::Space;
    if (c == '\n' || c == '\r') return PauseKind::Newline;
    if (c == ',') return PauseKind::Comma;
    if (c == ':' || c == ';') return PauseKind::Clause;
    if (c == '.') return PauseKind::TerminalPeriod;
    if (c == '?') return PauseKind::TerminalQuestion;
    if (c == '!') return PauseKind::TerminalExclaim;
    return PauseKind::Unknown;
}


bool ge_text_to_phones_english_with_pause_meta(const std::string& text_utf8,
                                              std::vector<PhonemeSpan>* out_phones,
                                              std::vector<PauseKind>* out_pause_kinds,
                                              std::vector<uint8_t>* out_pause_strength_u8) {
    if (!out_phones || !out_pause_kinds || !out_pause_strength_u8) return false;
    out_phones->clear();
    out_pause_kinds->clear();
    out_pause_strength_u8->clear();
    out_phones->reserve(text_utf8.size() / 2);
    out_pause_kinds->reserve(text_utf8.size() / 2);
    out_pause_strength_u8->reserve(text_utf8.size() / 2);

    auto push_phone = [&](const std::string& ph, uint32_t ms) {
        PhonemeSpan s;
        s.phone = ph;
        s.dur_ms_u32 = ms;
        out_phones->push_back(s);
        out_pause_kinds->push_back(PauseKind::Unknown);
        out_pause_strength_u8->push_back(0u);
    };
    auto push_sp = [&](uint32_t ms, PauseKind k, uint8_t strength) {
        PhonemeSpan s;
        s.phone = "SP";
        s.dur_ms_u32 = ms;
        out_phones->push_back(s);
        out_pause_kinds->push_back(k);
        out_pause_strength_u8->push_back(strength);
    };

    std::string word;
    for (size_t i = 0; i <= text_utf8.size(); ++i) {
        const char c0 = (i < text_utf8.size()) ? text_utf8[i] : 0;
        const bool is_letter = (c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z');
        if (is_letter) { word.push_back(c0); continue; }

        if (!word.empty()) {
            const std::string up = ge_upper_ascii(word);
            const char* phones = nullptr;
            if (lex_lookup(up, &phones)) {
                std::string cur;
                for (const char* p = phones; ; ++p) {
                    const char cc = *p;
                    if (cc == 0 || cc == ' ') {
                        if (!cur.empty()) {
                            const bool vowel = (cur.size() >= 2 && (cur[0]=='A'||cur[0]=='E'||cur[0]=='I'||cur[0]=='O'||cur[0]=='U'));
                            push_phone(cur, vowel ? 95u : 75u);
                            cur.clear();
                        }
                        if (cc == 0) break;
                    } else {
                        cur.push_back(cc);
                    }
                }
            } else {
                const size_t before = out_phones->size();
                emit_oov_word(up, *out_phones);
                const size_t after = out_phones->size();
                for (size_t k = before; k < after; ++k) {
                    out_pause_kinds->push_back(PauseKind::Unknown);
                    out_pause_strength_u8->push_back(0u);
                }
            }
            word.clear();
        }

        if (c0 == 0) break;

        const PauseKind pk0 = ge_classify_pause_char(c0);
        if (pk0 == PauseKind::Unknown) continue;

        // Collapse repeated pause chars into a single SP with a strength marker.
        size_t j = i;
        uint32_t strength = 1u;
        for (; j + 1 < text_utf8.size(); ++j) {
            const char cn = text_utf8[j + 1];
            const PauseKind pkn = ge_classify_pause_char(cn);
            if (pkn != pk0) break;
            strength++;
            if (strength >= 255u) break;
        }
        i = j; // skip run

        // Base durations; refined later by planner/priors.
        uint32_t ms = 90;
        if (pk0 == PauseKind::Space) ms = 60;
        else if (pk0 == PauseKind::Comma) ms = 120;
        else if (pk0 == PauseKind::Clause) ms = 160;
        else if (pk0 == PauseKind::TerminalPeriod) ms = 220;
        else if (pk0 == PauseKind::TerminalQuestion) ms = 240;
        else if (pk0 == PauseKind::TerminalExclaim) ms = 200;
        else if (pk0 == PauseKind::Newline) ms = 260;

        push_sp(ms, pk0, static_cast<uint8_t>(strength));
    }

    if (out_phones->empty()) {
        push_sp(200, PauseKind::Space, 1u);
    }
    return true;
}

bool ge_text_to_phones_english_with_pause_kinds(const std::string& text_utf8,
                                               std::vector<PhonemeSpan>* out_phones,
                                               std::vector<PauseKind>* out_pause_kinds) {
    if (!out_phones || !out_pause_kinds) return false;
    std::vector<uint8_t> strength;
    return ge_text_to_phones_english_with_pause_meta(text_utf8, out_phones, out_pause_kinds, &strength);
}


std::vector<PhonemeSpan> ge_text_to_phones_english(const std::string& text_utf8) {
    std::vector<PhonemeSpan> phones;
    std::vector<PauseKind> pause_kinds;
    (void)ge_text_to_phones_english_with_pause_kinds(text_utf8, &phones, &pause_kinds);
    return phones;
}

} // namespace genesis
