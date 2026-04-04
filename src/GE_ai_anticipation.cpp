#include "GE_ai_anticipation.hpp"
#include "GE_runtime.hpp" // SubstrateManager + corpus_query_best_score

#include <cctype>

static inline void ge_trim_left(std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    if (i) s.erase(0, i);
}

static inline void ge_trim_right(std::string& s) {
    size_t n = s.size();
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') n--;
        else break;
    }
    if (n != s.size()) s.resize(n);
}

static inline bool ge_starts_with_ascii(const std::string& s, const char* pfx) {
    if (!pfx) return false;
    size_t n = 0;
    while (pfx[n] != 0) n++;
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) if (s[i] != pfx[i]) return false;
    return true;
}

static inline bool ge_contains_first_token_colon(const std::string& s) {
    // If the first token contains ':', treat as an explicit command.
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
        if (c == ':') return true;
    }
    return false;
}

static inline bool ge_looks_like_url(const std::string& s) {
    if (ge_starts_with_ascii(s, "http://") || ge_starts_with_ascii(s, "https://")) return true;
    // Common shorthand: www.
    if (ge_starts_with_ascii(s, "www.")) return true;
    // If it contains a dot in the first token and no spaces, treat as URL-ish.
    bool dot = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
        if (c == '.') dot = true;
        // Stop early if token becomes too long; still deterministic.
        if (i > 256) break;
    }
    return dot;
}

static inline bool ge_parse_open_index(const std::string& s, uint32_t& idx_out) {
    // Accept: "open 3", "open:3", "OPEN 3".
    idx_out = 0u;
    std::string t = s;
    ge_trim_left(t);
    ge_trim_right(t);
    if (t.size() < 4) return false;
    // Lowercase first 4 chars ASCII-only.
    char o0 = (char)std::tolower((unsigned char)t[0]);
    char o1 = (char)std::tolower((unsigned char)t[1]);
    char o2 = (char)std::tolower((unsigned char)t[2]);
    char o3 = (char)std::tolower((unsigned char)t[3]);
    if (!(o0=='o'&&o1=='p'&&o2=='e'&&o3=='n')) return false;
    size_t p = 4;
    while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == ':')) p++;
    if (p >= t.size()) return false;
    uint32_t v = 0u;
    size_t k = 0;
    while (p < t.size() && t[p] >= '0' && t[p] <= '9') {
        v = v * 10u + (uint32_t)(t[p] - '0');
        p++; k++;
        if (v > 100000u) break;
    }
    if (k == 0) return false;
    if (v == 0u) return false;
    idx_out = v;
    return true;
}

static inline bool ge_looks_like_question(const std::string& s) {
    // Cheap deterministic heuristic.
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '?') return true;
    }
    // Leading interrogatives (ASCII only), bounded.
    std::string t = s;
    ge_trim_left(t);
    ge_trim_right(t);
    auto starts_word = [&](const char* w)->bool {
        size_t n = 0; while (w[n]) n++;
        if (t.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = (char)std::tolower((unsigned char)t[i]);
            if (a != w[i]) return false;
        }
        if (t.size() == n) return true;
        char c = t[n];
        return (c == ' ' || c == '\t');
    };
    return starts_word("what") || starts_word("why") || starts_word("how") || starts_word("when") || starts_word("where") || starts_word("who");
}

bool EwAiAnticipator::route(SubstrateManager* sm,
                           const std::string& in_line,
                           std::string& out_line,
                           std::string& ui_tag) const {
    out_line = in_line;
    ui_tag.clear();
    if (!sm) return false;

    std::string s = in_line;
    ge_trim_left(s);
    ge_trim_right(s);
    if (s.empty()) return false;

    // Do not rewrite explicit commands.
    if (s[0] == '/') return false;
    if (ge_contains_first_token_colon(s)) return false;
    if (ge_starts_with_ascii(s, "QUERY:") || ge_starts_with_ascii(s, "ANSWER:") ||
        ge_starts_with_ascii(s, "WEBSEARCH:") || ge_starts_with_ascii(s, "SEARCH:") ||
        ge_starts_with_ascii(s, "WEBFETCH:") || ge_starts_with_ascii(s, "OPEN:") ||
        ge_starts_with_ascii(s, "PATCH:") || ge_starts_with_ascii(s, "CODEGEN:") ||
        ge_starts_with_ascii(s, "CODEEDIT:")) {
        return false;
    }

    std::string s_lower = s;
    for (char& ch : s_lower) ch = (char)std::tolower((unsigned char)ch);
    auto starts_phrase = [&](const char* phrase_ascii)->bool {
        if (!phrase_ascii) return false;
        size_t n = 0u;
        while (phrase_ascii[n] != 0) ++n;
        if (s_lower.size() < n) return false;
        for (size_t i = 0u; i < n; ++i) {
            if (s_lower[i] != phrase_ascii[i]) return false;
        }
        return true;
    };
    auto tail_after_phrase = [&](const char* phrase_ascii)->std::string {
        size_t n = 0u;
        while (phrase_ascii[n] != 0) ++n;
        if (s.size() <= n) return std::string();
        std::string out = s.substr(n);
        ge_trim_left(out);
        ge_trim_right(out);
        return out;
    };

    if (starts_phrase("list assets") || starts_phrase("list asset library") ||
        starts_phrase("show asset library")) {
        out_line = "ASSET_LIBRARY_LIST:";
        ui_tag = "AI_ANTICIPATE route=ASSET_LIBRARY_LIST";
        return true;
    }
    if (starts_phrase("sandbox status")) {
        out_line = "SANDBOX_STATUS:";
        ui_tag = "AI_ANTICIPATE route=SANDBOX_STATUS";
        return true;
    }
    if (starts_phrase("open scene ")) {
        const std::string target = tail_after_phrase("open scene ");
        if (!target.empty()) {
            out_line = std::string("SCENE_OPEN:") + target;
            ui_tag = "AI_ANTICIPATE route=SCENE_OPEN";
            return true;
        }
    }
    if (starts_phrase("open simulation ")) {
        const std::string target = tail_after_phrase("open simulation ");
        if (!target.empty()) {
            out_line = std::string("SIM_OPEN:") + target;
            ui_tag = "AI_ANTICIPATE route=SIM_OPEN";
            return true;
        }
    }
    if (starts_phrase("open sandbox ")) {
        const std::string target = tail_after_phrase("open sandbox ");
        if (!target.empty()) {
            out_line = std::string("SANDBOX_OPEN: scene=") + target + " replace=1";
            ui_tag = "AI_ANTICIPATE route=SANDBOX_OPEN";
            return true;
        }
    }

    // OPEN routing.
    uint32_t open_idx = 0u;
    if (ge_parse_open_index(s, open_idx)) {
        out_line = std::string("OPEN:") + std::to_string((unsigned long long)open_idx);
        ui_tag = std::string("AI_ANTICIPATE route=OPEN idx=") + std::to_string((unsigned long long)open_idx);
        return true;
    }

    // WEBFETCH routing.
    if (ge_looks_like_url(s)) {
        std::string url = s;
        if (ge_starts_with_ascii(url, "www.")) url = std::string("https://") + url;
        out_line = std::string("WEBFETCH:") + url;
        ui_tag = "AI_ANTICIPATE route=WEBFETCH";
        return true;
    }

    // Prefer local corpus when strongly grounded.
    const uint32_t local_best = sm->corpus_query_best_score(s);
    const EwAiDataSubstrateTelemetry& ai_data = sm->ai_data_substrate_telemetry;
    const uint16_t crawler_drift_q15 = ai_data.crawler_drift_norm_q15;
    const uint16_t crawler_coherence_q15 = ai_data.crawler_coherence_norm_q15;

    // Deterministic scoring.
    // local_best is already 0..N; promote it.
    uint32_t score_query = 1u + local_best;
    uint32_t score_web = ge_looks_like_question(s) ? 2u : 0u;
    // If local grounding is weak and it looks like a question, web gets a bump.
    if (local_best < 3u && ge_looks_like_question(s)) score_web += 3u;
    if (crawler_drift_q15 > crawler_coherence_q15) {
        score_query += (uint32_t)((crawler_drift_q15 - crawler_coherence_q15) / 4096u);
    } else if (crawler_coherence_q15 > crawler_drift_q15) {
        score_web += (uint32_t)((crawler_coherence_q15 - crawler_drift_q15) / 4096u);
    }

    const bool choose_web = (score_web > score_query);
    if (choose_web) {
        out_line = std::string("WEBSEARCH:") + s;
        ui_tag = std::string("AI_ANTICIPATE route=WEBSEARCH score=") +
                 std::to_string((unsigned long long)score_web) +
                 " local_best=" + std::to_string((unsigned long long)local_best);
        return true;
    }

    out_line = std::string("QUERY:") + s;
    ui_tag = std::string("AI_ANTICIPATE route=QUERY score=") +
             std::to_string((unsigned long long)score_query) +
             " local_best=" + std::to_string((unsigned long long)local_best);
    return true;
}
