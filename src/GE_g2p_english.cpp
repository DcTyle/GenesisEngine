#include "GE_g2p_english.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace genesis {

static std::string ge_upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}

bool ge_phone_is_pause(const std::string& phone_upper) { return phone_upper == "SP"; }

bool ge_phone_is_vowel(const std::string& p) {
    const char* vowels[] = {"AA","AE","AH","AO","AW","AY","EH","ER","EY","IH","IY","OW","OY","UH","UW"};
    for (const char* v : vowels) if (p.rfind(v, 0) == 0) return true;
    return false;
}

bool ge_phone_is_voiced(const std::string& p) {
    if (p == "SP" || p == "WB") return false;
    if (ge_phone_is_vowel(p)) return true;
    return (p=="M"||p=="N"||p=="NG"||p=="L"||p=="R"||p=="W"||p=="Y"||p=="Z"||p=="ZH"||p=="V"||p=="DH"||p=="D"||p=="B"||p=="G"||p=="JH");
}

// Small deterministic letter->phone rules.
// This is intentionally compact; lexicons can be learned later.
static void ge_emit_phone(std::vector<PhonemeSpan>* out, const std::string& ph, uint32_t dur_ms) {
    if (!out) return;
    PhonemeSpan s;
    s.phone = ph;
    s.dur_ms_u32 = dur_ms;
    out->push_back(std::move(s));
}

static bool ge_is_vowel_char(char c) {
    c = (char)std::tolower((unsigned char)c);
    return c=='a'||c=='e'||c=='i'||c=='o'||c=='u'||c=='y';
}

static void ge_g2p_word(const std::string& word, std::vector<PhonemeSpan>* out) {
    // Simple digraphs + vowel mapping.
    std::string w;
    for (char c : word) {
        if (std::isalpha((unsigned char)c) || c=='\'') w.push_back((char)std::tolower((unsigned char)c));
    }
    if (w.empty()) return;

    // A few high-frequency irregulars.
    static const std::unordered_map<std::string, std::vector<std::string>> irregular = {
        {"the", {"DH","AH"}},
        {"a", {"AH"}},
        {"i", {"AY"}},
        {"you", {"Y","UW"}},
        {"to", {"T","UW"}},
        {"of", {"AH","V"}},
        {"and", {"AE","N","D"}},
        {"in", {"IH","N"}},
        {"is", {"IH","Z"}},
        {"it", {"IH","T"}},
    };
    auto it = irregular.find(w);
    if (it != irregular.end()) {
        for (auto& ph : it->second) ge_emit_phone(out, ph, ge_phone_is_vowel(ph) ? 95u : 55u);
        return;
    }

    size_t i = 0;
    while (i < w.size()) {
        // Digraphs.
        if (i + 1 < w.size()) {
            std::string dg = w.substr(i, 2);
            if (dg == "ch") { ge_emit_phone(out, "CH", 55); i += 2; continue; }
            if (dg == "sh") { ge_emit_phone(out, "SH", 60); i += 2; continue; }
            if (dg == "th") { ge_emit_phone(out, "TH", 55); i += 2; continue; }
            if (dg == "dh") { ge_emit_phone(out, "DH", 55); i += 2; continue; }
            if (dg == "ng") { ge_emit_phone(out, "NG", 60); i += 2; continue; }
            if (dg == "ph") { ge_emit_phone(out, "F", 55); i += 2; continue; }
            if (dg == "wh") { ge_emit_phone(out, "W", 50); i += 2; continue; }
            if (dg == "qu") { ge_emit_phone(out, "K", 45); ge_emit_phone(out, "W", 45); i += 2; continue; }
            if (dg == "ee") { ge_emit_phone(out, "IY", 100); i += 2; continue; }
            if (dg == "oo") { ge_emit_phone(out, "UW", 100); i += 2; continue; }
            if (dg == "ai" || dg == "ay") { ge_emit_phone(out, "EY", 95); i += 2; continue; }
            if (dg == "ea") { ge_emit_phone(out, "IY", 95); i += 2; continue; }
            if (dg == "ou" || dg == "ow") { ge_emit_phone(out, "AW", 95); i += 2; continue; }
            if (dg == "oi" || dg == "oy") { ge_emit_phone(out, "OY", 95); i += 2; continue; }
        }

        char c = w[i];
        // Vowels.
        if (ge_is_vowel_char(c)) {
            // Contextual y.
            if (c == 'y') {
                if (i == 0) { ge_emit_phone(out, "Y", 40); }
                else { ge_emit_phone(out, "IY", 85); }
                i += 1;
                continue;
            }
            switch (c) {
                case 'a': ge_emit_phone(out, "AE", 95); break;
                case 'e': ge_emit_phone(out, "EH", 90); break;
                case 'i': ge_emit_phone(out, "IH", 85); break;
                case 'o': ge_emit_phone(out, "AA", 95); break;
                case 'u': ge_emit_phone(out, "AH", 85); break;
                default: ge_emit_phone(out, "AH", 85); break;
            }
            i += 1;
            continue;
        }

        // Consonants.
        switch (c) {
            case 'b': ge_emit_phone(out, "B", 45); break;
            case 'c': {
                // soft c before e/i/y
                if (i + 1 < w.size() && (w[i+1]=='e'||w[i+1]=='i'||w[i+1]=='y')) ge_emit_phone(out, "S", 45);
                else ge_emit_phone(out, "K", 45);
                break;
            }
            case 'd': ge_emit_phone(out, "D", 45); break;
            case 'f': ge_emit_phone(out, "F", 50); break;
            case 'g': {
                if (i + 1 < w.size() && (w[i+1]=='e'||w[i+1]=='i'||w[i+1]=='y')) ge_emit_phone(out, "JH", 55);
                else ge_emit_phone(out, "G", 45);
                break;
            }
            case 'h': ge_emit_phone(out, "HH", 45); break;
            case 'j': ge_emit_phone(out, "JH", 55); break;
            case 'k': ge_emit_phone(out, "K", 45); break;
            case 'l': ge_emit_phone(out, "L", 55); break;
            case 'm': ge_emit_phone(out, "M", 55); break;
            case 'n': ge_emit_phone(out, "N", 55); break;
            case 'p': ge_emit_phone(out, "P", 45); break;
            case 'q': ge_emit_phone(out, "K", 45); break;
            case 'r': ge_emit_phone(out, "R", 55); break;
            case 's': ge_emit_phone(out, "S", 50); break;
            case 't': ge_emit_phone(out, "T", 45); break;
            case 'v': ge_emit_phone(out, "V", 50); break;
            case 'w': ge_emit_phone(out, "W", 50); break;
            case 'x': ge_emit_phone(out, "K", 35); ge_emit_phone(out, "S", 35); break;
            case 'z': ge_emit_phone(out, "Z", 50); break;
            default: break;
        }
        i += 1;
    }
}

std::vector<PhonemeSpan> ge_g2p_english_text_to_phones(const std::string& text_utf8) {
    std::vector<PhonemeSpan> out;
    std::string cur;
    auto flush_word = [&](){
        if (cur.empty()) return;
        ge_g2p_word(cur, &out);
        cur.clear();
        // word boundary
        ge_emit_phone(&out, "WB", 0);
    };

    for (size_t i = 0; i < text_utf8.size(); ++i) {
        unsigned char uc = (unsigned char)text_utf8[i];
        char c = (char)uc;
        if (std::isalpha((unsigned char)c) || c=='\'') {
            cur.push_back(c);
            continue;
        }
        flush_word();
        // pauses for punctuation
        if (c == '.' || c == '!' || c == '?' || c == ';' || c == ':') {
            ge_emit_phone(&out, "SP", 220);
        } else if (c == ',' ) {
            ge_emit_phone(&out, "SP", 140);
        }
    }
    flush_word();

    // Remove trailing WB.
    while (!out.empty() && out.back().phone == "WB") out.pop_back();
    return out;
}

} // namespace genesis
