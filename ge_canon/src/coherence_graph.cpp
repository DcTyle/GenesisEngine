#include "coherence_graph.hpp"

#include "inspector_fields.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_map>

static inline bool is_ident_start(char c) {
    return (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static inline bool is_ident_body(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

uint32_t EwCoherenceGraph::fnv1a_u32_(const std::string& s) {
    uint32_t acc = 2166136261u;
    for (size_t i = 0; i < s.size(); ++i) {
        acc ^= (uint8_t)s[i];
        acc *= 16777619u;
    }
    return acc;
}

void EwCoherenceGraph::tokenize_identifiers_ascii_(const std::string& s, std::vector<std::string>& out) {
    out.clear();
    out.reserve(128);
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        unsigned char uc = (unsigned char)s[i];
        if (uc >= 0x80) { ++i; continue; }
        const char c = (char)uc;
        if (!is_ident_start(c)) { ++i; continue; }
        size_t j = i + 1;
        while (j < n) {
            unsigned char uj = (unsigned char)s[j];
            if (uj >= 0x80) break;
            if (!is_ident_body((char)uj)) break;
            ++j;
        }
        std::string tok;
        tok.reserve(j - i);
        for (size_t k = i; k < j; ++k) {
            char cc = s[k];
            if (cc >= 'A' && cc <= 'Z') cc = (char)(cc - 'A' + 'a');
            tok.push_back(cc);
        }
        if (tok.size() >= 3) {
            static const char* stop[] = {"the","and","for","with","from","this","that","true","false","uint","int","size"};
            bool is_stop = false;
            for (size_t si = 0; si < sizeof(stop)/sizeof(stop[0]); ++si) {
                if (tok == stop[si]) { is_stop = true; break; }
            }
            if (!is_stop) out.push_back(tok);
        }
        i = j;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void EwCoherenceGraph::rebuild_from_inspector(const EwInspectorFields& insp) {
    artifacts_.clear();
    sym_refs_.clear();
    unique_syms_u32_ = 0;

    std::vector<EwInspectorArtifact> all;
    insp.snapshot_all(all);
    std::sort(all.begin(), all.end(), [](const EwInspectorArtifact& a, const EwInspectorArtifact& b) {
        if (a.rel_path != b.rel_path) return a.rel_path < b.rel_path;
        return a.kind_u32 < b.kind_u32;
    });

    std::vector<std::string> toks;
    toks.reserve(256);

    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint16_t>>> tmp;
    tmp.reserve(4096);

    for (size_t ai = 0; ai < all.size(); ++ai) {
        const EwInspectorArtifact& a = all[ai];
        const std::string& lp = a.rel_path;
        const bool is_code = (a.kind_u32 == EW_ARTIFACT_CPP) || (a.kind_u32 == EW_ARTIFACT_HPP) || (a.kind_u32 == EW_ARTIFACT_CMAKE);
        const bool is_sym_stream = (lp.find(".code_symbols.txt") != std::string::npos);
        if (!is_code && !is_sym_stream) continue;

        ArtifactInfo info;
        info.rel_path = a.rel_path;
        info.kind_u32 = a.kind_u32;
        const uint32_t art_index = (uint32_t)artifacts_.size();
        artifacts_.push_back(info);

        const std::string& body = a.payload;
        const size_t cap = (body.size() < (size_t)131072) ? body.size() : (size_t)131072;
        tokenize_identifiers_ascii_(body.substr(0, cap), toks);
        if (toks.empty()) continue;

        const uint16_t base_w = is_sym_stream ? 16384 : 8192;
        for (size_t ti = 0; ti < toks.size(); ++ti) {
            const uint32_t h = fnv1a_u32_(toks[ti]);
            tmp[h].push_back({art_index, base_w});
        }
    }

    sym_refs_.reserve(8192);
    for (auto& kv : tmp) {
        const uint32_t sym = kv.first;
        auto& vec = kv.second;
        std::sort(vec.begin(), vec.end(), [](auto& a, auto& b){
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });
        uint32_t last_art = 0xFFFFFFFFu;
        uint32_t acc = 0;
        bool have = false;
        for (size_t i = 0; i < vec.size(); ++i) {
            if (!have) {
                last_art = vec[i].first;
                acc = vec[i].second;
                have = true;
                continue;
            }
            if (vec[i].first == last_art) {
                acc += vec[i].second;
                if (acc > 32768u) acc = 32768u;
            } else {
                SymRef r;
                r.sym_hash_u32 = sym;
                r.art_index_u32 = last_art;
                r.weight_q15 = (uint16_t)acc;
                sym_refs_.push_back(r);
                last_art = vec[i].first;
                acc = vec[i].second;
            }
        }
        if (have) {
            SymRef r;
            r.sym_hash_u32 = sym;
            r.art_index_u32 = last_art;
            r.weight_q15 = (uint16_t)acc;
            sym_refs_.push_back(r);
        }
    }

    std::sort(sym_refs_.begin(), sym_refs_.end(), [](const SymRef& a, const SymRef& b) {
        if (a.sym_hash_u32 != b.sym_hash_u32) return a.sym_hash_u32 < b.sym_hash_u32;
        return a.art_index_u32 < b.art_index_u32;
    });

    uint32_t uniq = 0;
    uint32_t last = 0;
    bool have = false;
    for (const auto& r : sym_refs_) {
        if (!have || r.sym_hash_u32 != last) {
            uniq += 1;
            last = r.sym_hash_u32;
            have = true;
        }
    }
    unique_syms_u32_ = uniq;
}

void EwCoherenceGraph::query_best(const std::string& request_utf8, uint32_t max_out,
                                 std::vector<Match>& out) const {
    out.clear();
    if (artifacts_.empty() || sym_refs_.empty()) return;
    if (max_out < 1u) max_out = 1u;
    if (max_out > 64u) max_out = 64u;

    std::vector<std::string> toks;
    tokenize_identifiers_ascii_(request_utf8, toks);
    if (toks.empty()) return;

    std::vector<uint32_t> score(artifacts_.size(), 0u);
    for (const auto& t : toks) {
        const uint32_t h = fnv1a_u32_(t);
        auto lb = std::lower_bound(sym_refs_.begin(), sym_refs_.end(), h,
            [](const SymRef& r, uint32_t key){ return r.sym_hash_u32 < key; });
        auto ub = std::upper_bound(sym_refs_.begin(), sym_refs_.end(), h,
            [](uint32_t key, const SymRef& r){ return key < r.sym_hash_u32; });
        for (auto it = lb; it != ub; ++it) {
            const uint32_t ai = it->art_index_u32;
            if (ai < score.size()) score[ai] += (uint32_t)it->weight_q15;
        }
    }

    std::vector<uint32_t> idx;
    idx.reserve(artifacts_.size());
    for (uint32_t i = 0; i < (uint32_t)artifacts_.size(); ++i) {
        if (score[i] != 0u) idx.push_back(i);
    }
    std::sort(idx.begin(), idx.end(), [&](uint32_t a, uint32_t b) {
        if (score[a] != score[b]) return score[a] > score[b];
        return artifacts_[a].rel_path < artifacts_[b].rel_path;
    });

    const uint32_t take = ((uint32_t)idx.size() < max_out) ? (uint32_t)idx.size() : max_out;
    out.reserve(take);
    for (uint32_t i = 0; i < take; ++i) {
        Match m;
        m.rel_path = artifacts_[idx[i]].rel_path;
        m.score_u32 = score[idx[i]];
        out.push_back(m);
    }
}

std::string EwCoherenceGraph::debug_stats() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "coherence_graph artifacts=%llu sym_refs=%llu unique_syms=%llu",
                  (unsigned long long)artifacts_.size(),
                  (unsigned long long)sym_refs_.size(),
                  (unsigned long long)unique_syms_u32_);
    return std::string(buf);
}
