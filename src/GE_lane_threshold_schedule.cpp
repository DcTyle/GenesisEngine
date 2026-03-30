#include "GE_lane_threshold_schedule.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "ew_kv_params.hpp"

static int64_t ge_parse_q32_32_from_ascii_float(std::string_view s) {
    // Deterministic decimal parse: sign + integer + optional fraction up to 9 digits.
    bool neg=false;
    size_t i=0;
    while (i<s.size() && (s[i]==' '||s[i]=='\t')) i++;
    if (i<s.size() && (s[i]=='+'||s[i]=='-')) { neg = (s[i]=='-'); i++; }
    uint64_t int_part=0;
    while (i<s.size() && s[i]>='0' && s[i]<='9') { int_part = int_part*10 + (uint64_t)(s[i]-'0'); i++; }
    uint64_t frac_part=0;
    uint64_t frac_scale=1;
    if (i<s.size() && s[i]=='.') {
        i++;
        int digits=0;
        while (i<s.size() && s[i]>='0' && s[i]<='9' && digits<9) {
            frac_part = frac_part*10 + (uint64_t)(s[i]-'0');
            frac_scale *= 10;
            i++; digits++;
        }
        // ignore remaining digits deterministically
    }
    __int128 q = (__int128)int_part<<32;
    if (frac_part) {
        q += (__int128)frac_part * ((__int128)1<<32) / (__int128)frac_scale;
    }
    int64_t out = (int64_t)q;
    if (neg) out = -out;
    return out;
}

int64_t GE_LaneThresholdSchedule::rel_err_limit_for_epoch_q32_32(uint32_t current_epoch_u32, int64_t default_limit_q32_32) const {
    if (points.empty()) return default_limit_q32_32;
    const GE_LaneThresholdPoint* best = &points[0];
    for (const auto& p : points) {
        if (p.epoch_u32 <= current_epoch_u32) best = &p;
    }
    return best->rel_err_max_q32_32;
}

const GE_LaneThresholdSchedule* GE_AllLaneThresholds::find_lane(uint8_t lane_u8) const {
    for (const auto& l : lanes) if (l.lane_u8 == lane_u8) return &l;
    return nullptr;
}

bool GE_load_lane_thresholds_from_file(const std::string& path_utf8, GE_AllLaneThresholds* out) {
    out->lanes.clear();
    FILE* f = std::fopen(path_utf8.c_str(), "rb");
    if (!f) return false;
    char line[4096];
    GE_LaneThresholdSchedule cur;
    bool has_cur=false;
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        // strip CRLF
        while (!s.empty() && (s.back()=='\n'||s.back()=='\r')) s.pop_back();
        // trim
        size_t j=0;
        while (j<s.size() && (s[j]==' '||s[j]=='\t')) j++;
        if (j==s.size()) continue;
        if (s[j]=='#') continue;
        s = s.substr(j);

        // tokenization by spaces into k=v
        std::vector<std::string> toks;
        std::string curtok;
        for (char c: s) {
            if (c==' '||c=='\t') { if(!curtok.empty()){ toks.push_back(curtok); curtok.clear(); } }
            else curtok.push_back(c);
        }
        if(!curtok.empty()) toks.push_back(curtok);

        // detect lane line
        for (const auto& t : toks) {
            std::string_view k, v;
            if (!ew::ew_split_kv_token_ascii(t, k, v)) continue;
            if (k=="lane") {
                if (has_cur) { out->lanes.push_back(cur); cur = GE_LaneThresholdSchedule(); }
                has_cur=true;
                uint32_t u=0;
                if (!ew::ew_parse_u32_ascii(v, u)) { std::fclose(f); return false; }
                cur.lane_u8 = (uint8_t)u;
            }
        }

        if (!has_cur) continue;

        uint32_t epoch=0;
        bool has_epoch=false;
        int64_t rel=0;
        bool has_rel=false;
        for (const auto& t: toks) {
            std::string_view k, v;
            if (!ew::ew_split_kv_token_ascii(t, k, v)) continue;
            if (k=="epoch") { uint32_t u=0; if(!ew::ew_parse_u32_ascii(v, u)) { std::fclose(f); return false; } epoch=u; has_epoch=true; }
            if (k=="rel_err_max") { rel = ge_parse_q32_32_from_ascii_float(v); has_rel=true; }
        }
        if (has_epoch && has_rel) {
            cur.points.push_back({epoch, rel});
        }
    }
    if (has_cur) out->lanes.push_back(cur);
    std::fclose(f);
    // sort points deterministically
    for (auto& l: out->lanes) {
        std::stable_sort(l.points.begin(), l.points.end(), [](auto&a,auto&b){ return a.epoch_u32 < b.epoch_u32; });
    }
    std::stable_sort(out->lanes.begin(), out->lanes.end(), [](auto&a,auto&b){ return a.lane_u8 < b.lane_u8; });
    return true;
}
