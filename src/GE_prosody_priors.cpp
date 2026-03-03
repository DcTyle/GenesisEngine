#include "GE_prosody_priors.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_map>

namespace genesis {

static std::string ge_upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}

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

bool ge_prosody_priors_read_ewcfg(const std::string& path, ProsodyPriors* out) {
    if (!out) return false;
    std::string txt;
    if (!ge_read_all(path, &txt)) return false;
    ProsodyPriors pri;
    pri.ok = false;
    pri.info.clear();
    pri.rows.clear();

    size_t i = 0;
    auto skip_ws = [&]() {
        while (i < txt.size() && (txt[i] == ' ' || txt[i] == '\t' || txt[i] == '\r' || txt[i] == '\n')) i++;
    };
    auto read_tok = [&](std::string* t) {
        t->clear();
        skip_ws();
        while (i < txt.size() && !(txt[i] == ' ' || txt[i] == '\t' || txt[i] == '\r' || txt[i] == '\n')) {
            t->push_back(txt[i++]);
        }
    };
    while (i < txt.size()) {
        skip_ws();
        if (i >= txt.size()) break;
        if (txt[i] == '#') { while (i < txt.size() && txt[i] != '\n') i++; continue; }
        std::string tok; read_tok(&tok);
        if (tok != "phone") { while (i < txt.size() && txt[i] != '\n') i++; continue; }
        ProsodyPriorRow r;
        std::string a,b,c,d,e;
        read_tok(&a); read_tok(&b); read_tok(&c); read_tok(&d); read_tok(&e);
        if (a.empty() || b.empty() || c.empty() || d.empty() || e.empty()) { while (i < txt.size() && txt[i] != '\n') i++; continue; }
        r.phone = ge_upper(a);
        r.mean_dur_ms_q16_u32 = (uint32_t)std::strtoul(b.c_str(), nullptr, 10);
        r.mean_f0_ratio_q16_u32 = (uint32_t)std::strtoul(c.c_str(), nullptr, 10);
        r.mean_amp_ratio_q16_u32 = (uint32_t)std::strtoul(d.c_str(), nullptr, 10);
        r.count_u32 = (uint32_t)std::strtoul(e.c_str(), nullptr, 10);
        pri.rows.push_back(std::move(r));
        while (i < txt.size() && txt[i] != '\n') i++;
    }
    std::sort(pri.rows.begin(), pri.rows.end(), [](const ProsodyPriorRow& x, const ProsodyPriorRow& y){ return x.phone < y.phone; });
    pri.ok = !pri.rows.empty();
    pri.info = pri.ok ? "ok" : "no_rows";
    *out = std::move(pri);
    return true;
}

bool ge_prosody_priors_write_ewcfg(const std::string& path, const ProsodyPriors& pri) {
    std::string out;
    out += "# prosody priors (deterministic)\n";
    for (const auto& r : pri.rows) {
        out += "phone ";
        out += ge_upper(r.phone);
        out += " ";
        out += std::to_string(r.mean_dur_ms_q16_u32);
        out += " ";
        out += std::to_string(r.mean_f0_ratio_q16_u32);
        out += " ";
        out += std::to_string(r.mean_amp_ratio_q16_u32);
        out += " ";
        out += std::to_string(r.count_u32);
        out += "\n";
    }
    return ge_write_all(path, out);
}

static uint32_t ge_lerp_u32(uint32_t a, uint32_t b, uint32_t t_q16) {
    uint64_t inv = 65536ull - (uint64_t)t_q16;
    return (uint32_t)(((uint64_t)a * inv + (uint64_t)b * (uint64_t)t_q16) >> 16);
}

void ge_prosody_apply_priors(
    const ProsodyPriors& pri,
    const std::vector<std::string>& phones_upper,
    uint32_t blend_q16,
    std::vector<VoiceProsodyControl>* inout_controls)
{
    if (!inout_controls) return;
    if (phones_upper.size() != inout_controls->size()) return;
    if (blend_q16 > 65536u) blend_q16 = 65536u;

    std::unordered_map<std::string, const ProsodyPriorRow*> map;
    map.reserve(pri.rows.size());
    for (const auto& r : pri.rows) map[ge_upper(r.phone)] = &r;

    for (size_t i = 0; i < inout_controls->size(); ++i) {
        const auto it = map.find(ge_upper(phones_upper[i]));
        if (it == map.end()) continue;
        const ProsodyPriorRow* r = it->second;
        if (!r) continue;
        VoiceProsodyControl& c = (*inout_controls)[i];

        // Duration: pri is ms in Q16.16.
        uint32_t pri_ms = (r->mean_dur_ms_q16_u32 + 0x8000u) >> 16;
        c.dur_ms_u32 = ge_lerp_u32(c.dur_ms_u32, pri_ms, blend_q16);

        // Pitch ratio: only blend if already ratio-coded.
        if ((c.f0_hz_q16_u32 & 0x80000000u) != 0) {
            uint32_t cur = (c.f0_hz_q16_u32 & 0x7FFFFFFFu);
            uint32_t blended = ge_lerp_u32(cur, r->mean_f0_ratio_q16_u32, blend_q16);
            c.f0_hz_q16_u32 = 0x80000000u | (blended & 0x7FFFFFFFu);
        }
        // Amp ratio: only blend if ratio-coded (negative packed).
        if (c.amp_q15_i16 < 0) {
            uint32_t cur_q15 = (uint32_t)(-c.amp_q15_i16);
            uint32_t pri_q15 = (uint32_t)((((uint64_t)r->mean_amp_ratio_q16_u32) * 32768ull) >> 16);
            if (pri_q15 > 65535u) pri_q15 = 65535u;
            uint32_t blended = ge_lerp_u32(cur_q15, pri_q15, blend_q16);
            if (blended > 65535u) blended = 65535u;
            c.amp_q15_i16 = (int16_t)-(int32_t)blended;
        }
    }
}

} // namespace genesis
