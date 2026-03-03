#include "GE_corpus_allowlist.hpp"

#include <algorithm>
#include <fstream>

#include "ew_kv_params.hpp"

static inline std::string ge_trim_ascii(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

static inline bool ge_parse_lane_header(const std::string& line, uint8_t& lane_u8) {
    const std::string prefix = "## Lane ";
    if (line.rfind(prefix, 0) != 0) return false;
    size_t i = prefix.size();
    uint32_t lane_u32 = 0;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
        lane_u32 = lane_u32 * 10u + uint32_t(line[i] - '0');
        i++;
    }
    if (lane_u32 > 9u) return false;
    lane_u8 = uint8_t(lane_u32);
    return true;
}

static inline bool ge_extract_domain_backticks(const std::string& line, std::string& out_domain) {
    const size_t a = line.find('`');
    if (a == std::string::npos) return false;
    const size_t b = line.find('`', a + 1);
    if (b == std::string::npos || b <= a + 1) return false;
    out_domain = ge_trim_ascii(line.substr(a + 1, b - (a + 1)));
    ew::ew_ascii_lower_inplace(out_domain);
    return !out_domain.empty();
}

const GE_CorpusDomainPolicy* GE_CorpusAllowlist::find_by_domain_ascii(const std::string& domain_ascii) const {
    std::string key = domain_ascii;
    ew::ew_ascii_lower_inplace(key);
    for (const auto& d : domains) {
        if (d.domain_ascii == key) return &d;
    }
    return nullptr;
}

const GE_CorpusDomainPolicy* GE_CorpusAllowlist::find_by_domain_id9(const EwId9& id9) const {
    for (const auto& d : domains) {
        if (d.domain_id9 == id9) return &d;
    }
    return nullptr;
}

bool GE_CorpusAllowlist::is_domain_allowed(const std::string& domain_ascii) const {
    return find_by_domain_ascii(domain_ascii) != nullptr;
}

bool GE_load_corpus_allowlist_from_md(const std::string& md_path_utf8, GE_CorpusAllowlist& out) {
    out.domains.clear();
    std::ifstream f(md_path_utf8, std::ios::binary);
    if (!f) return false;
    uint8_t lane_u8 = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = ge_trim_ascii(line);
        if (t.empty()) continue;
        (void)ge_parse_lane_header(t, lane_u8);
        if (t.size() >= 2 && t[0] == '-' && (t[1] == ' ' || t[1] == '\t')) {
            std::string domain;
            if (!ge_extract_domain_backticks(t, domain)) continue;
            GE_CorpusDomainPolicy p;
            p.lane_u8 = lane_u8;
            p.domain_ascii = domain;
            p.domain_id9 = ew_id9_from_string_ascii(domain);
            // Defaults: offline allowed, live disabled.
            p.allow_live_http = false;
            p.allow_offline = true;
            p.offline_only = false;
            p.respect_robots = true;
            p.respect_terms = true;
            p.license_filter_required = true;
            // Conservative deterministic rate defaults.
            p.tokens_per_sec_q16_16 = (1u << 16);
            p.burst_tokens_u32 = 1;
            p.rate_tokens_per_sec_u32 = 1;
            p.rate_burst_u32 = 1;
            out.domains.push_back(p);
        }
    }
    std::sort(out.domains.begin(), out.domains.end(), [](const GE_CorpusDomainPolicy& a, const GE_CorpusDomainPolicy& b) {
        if (a.lane_u8 != b.lane_u8) return a.lane_u8 < b.lane_u8;
        if (a.domain_ascii != b.domain_ascii) return a.domain_ascii < b.domain_ascii;
        return a.domain_id9 < b.domain_id9;
    });
    out.domains.erase(std::unique(out.domains.begin(), out.domains.end(), [](const GE_CorpusDomainPolicy& a, const GE_CorpusDomainPolicy& b) {
        return a.lane_u8 == b.lane_u8 && a.domain_ascii == b.domain_ascii;
    }), out.domains.end());
    return !out.domains.empty();
}

bool GE_load_corpus_allowlist_from_md_text(const std::string& md_text_utf8, GE_CorpusAllowlist& out) {
    out.domains.clear();
    uint8_t lane_u8 = 0;
    std::string cur;
    cur.reserve(256);
    auto flush_line = [&](const std::string& line) {
        std::string t = ge_trim_ascii(line);
        if (t.empty()) return;
        (void)ge_parse_lane_header(t, lane_u8);
        if (t.size() >= 2 && t[0] == '-' && (t[1] == ' ' || t[1] == '\t')) {
            std::string domain;
            if (!ge_extract_domain_backticks(t, domain)) return;
            GE_CorpusDomainPolicy p;
            p.lane_u8 = lane_u8;
            p.domain_ascii = domain;
            p.domain_id9 = ew_id9_from_string_ascii(domain);
            // Defaults: offline allowed, live disabled.
            p.allow_live_http = false;
            p.allow_offline = true;
            p.offline_only = false;
            p.respect_robots = true;
            p.respect_terms = true;
            p.license_filter_required = true;
            // Conservative deterministic rate defaults.
            p.tokens_per_sec_q16_16 = (1u << 16);
            p.burst_tokens_u32 = 1;
            p.rate_tokens_per_sec_u32 = 1;
            p.rate_burst_u32 = 1;
            out.domains.push_back(p);
        }
    };

    for (size_t i = 0; i < md_text_utf8.size(); ++i) {
        const char c = md_text_utf8[i];
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            flush_line(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) flush_line(cur);

    std::sort(out.domains.begin(), out.domains.end(), [](const GE_CorpusDomainPolicy& a, const GE_CorpusDomainPolicy& b) {
        if (a.lane_u8 != b.lane_u8) return a.lane_u8 < b.lane_u8;
        if (a.domain_ascii != b.domain_ascii) return a.domain_ascii < b.domain_ascii;
        return a.domain_id9 < b.domain_id9;
    });
    out.domains.erase(std::unique(out.domains.begin(), out.domains.end(), [](const GE_CorpusDomainPolicy& a, const GE_CorpusDomainPolicy& b) {
        return a.lane_u8 == b.lane_u8 && a.domain_ascii == b.domain_ascii;
    }), out.domains.end());
    return !out.domains.empty();
}
