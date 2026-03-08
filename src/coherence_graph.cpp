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


static inline bool ew_is_anchor_line_(const std::string& line, std::string& out_name) {
    out_name.clear();
    const char* pats[] = {"// EW_ANCHOR:", "# EW_ANCHOR:", "<!-- EW_ANCHOR:"};
    for (size_t pi = 0; pi < sizeof(pats)/sizeof(pats[0]); ++pi) {
        const std::string pat(pats[pi]);
        const size_t p = line.find(pat);
        if (p == std::string::npos) continue;
        size_t s = p + pat.size();
        size_t e = s;
        while (e < line.size()) {
            const char c = line[e];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-' || c == '>') break;
            ++e;
        }
        if (e <= s) return false;
        out_name = line.substr(s, e - s);
        return true;
    }
    return false;
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
    anchor_regions_.clear();
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
        info.payload = a.payload;
        const uint32_t art_index = (uint32_t)artifacts_.size();
        artifacts_.push_back(info);

        // Build bounded anchor regions as the semantic locator surface.
        std::vector<std::pair<std::string, size_t>> anchors;
        anchors.reserve(64);
        size_t ls = 0;
        while (ls <= a.payload.size()) {
            size_t le = a.payload.find('\n', ls);
            if (le == std::string::npos) le = a.payload.size();
            std::string line = a.payload.substr(ls, le - ls);
            std::string an;
            if (ew_is_anchor_line_(line, an)) anchors.push_back({an, ls});
            if (le >= a.payload.size()) break;
            ls = le + 1;
        }
        for (size_t ai2 = 0; ai2 + 1 < anchors.size(); ++ai2) {
            AnchorRegion rg;
            rg.art_index_u32 = art_index;
            rg.anchor_a = anchors[ai2].first;
            rg.anchor_b = anchors[ai2 + 1].first;
            rg.insertion_anchor = anchors[ai2].first;
            size_t rs = a.payload.find('\n', anchors[ai2].second);
            if (rs == std::string::npos) rs = anchors[ai2].second; else rs += 1;
            size_t re = anchors[ai2 + 1].second;
            if (re > rs) rg.region_text = a.payload.substr(rs, re - rs);
            anchor_regions_.push_back(std::move(rg));
        }
        for (size_t ai2 = 0; ai2 < anchors.size(); ++ai2) {
            AnchorRegion rg;
            rg.art_index_u32 = art_index;
            rg.insertion_anchor = anchors[ai2].first;
            size_t rs = a.payload.find('\n', anchors[ai2].second);
            if (rs == std::string::npos) rs = anchors[ai2].second; else rs += 1;
            size_t re = a.payload.find('\n', rs);
            if (re == std::string::npos) re = a.payload.size();
            if (re > rs) rg.region_text = a.payload.substr(rs, re - rs);
            anchor_regions_.push_back(std::move(rg));
        }

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

bool EwCoherenceGraph::selftest(std::string& report_utf8) const {
    report_utf8.clear();
    // Determinism checks should be cheap and must never throw or allocate unboundedly.
    // NOTE: this is not a correctness proof; it's a production smoke test.
    std::vector<std::string> t0;
    std::vector<std::string> t1;
    tokenize_identifiers_ascii_("Anchor sync_basis9_from_core EW_ARTIFACT_CPP", t0);
    tokenize_identifiers_ascii_("Anchor sync_basis9_from_core EW_ARTIFACT_CPP", t1);
    if (t0 != t1) {
        report_utf8 = "fail:tokenize_nondeterministic";
        return false;
    }

    // Bounds clamp check.
    std::vector<Match> ms;
    query_best("Anchor sync_basis9_from_core", 9999u, ms);
    if (ms.size() > 64u) {
        report_utf8 = "fail:query_max_out_not_clamped";
        return false;
    }

    // Stable ordering check (non-increasing score, then rel_path).
    for (size_t i = 1; i < ms.size(); ++i) {
        if (ms[i-1].score_u32 < ms[i].score_u32) {
            report_utf8 = "fail:query_sort_score";
            return false;
        }
        if (ms[i-1].score_u32 == ms[i].score_u32 && ms[i-1].rel_path > ms[i].rel_path) {
            report_utf8 = "fail:query_sort_path";
            return false;
        }
    }

    std::vector<SemanticPatchTarget> pt;
    query_semantic_patch_targets("anchor update route scheduler", 9999u, pt);
    if (pt.size() > 64u) {
        report_utf8 = "fail:semantic_patch_max_out_not_clamped";
        return false;
    }

    // Rename plan clamps as well (should never exceed 64 internally).
    std::vector<Match> hits;
    plan_rename_ascii("anchor", "anchor2", 9999u, hits);
    if (hits.size() > 64u) {
        report_utf8 = "fail:rename_plan_max_out_not_clamped";
        return false;
    }

    report_utf8 = "ok";
    return true;
}


static inline bool ew_norm_token_ascii_(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char uc = (unsigned char)in[i];
        if (uc >= 0x80) return false;
        char c = (char)uc;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out.push_back(c);
    }
    if (out.size() < 3) return false;
    if (!is_ident_start(out[0])) return false;
    for (size_t i = 1; i < out.size(); ++i) {
        if (!is_ident_body(out[i])) return false;
    }
    static const char* stop[] = {"the","and","for","with","from","this","that","true","false","uint","int","size"};
    for (size_t si = 0; si < sizeof(stop)/sizeof(stop[0]); ++si) {
        if (out == stop[si]) return false;
    }
    return true;
}

void EwCoherenceGraph::plan_rename_ascii(const std::string& old_token_ascii, const std::string& new_token_ascii,
                                        uint32_t max_out, std::vector<Match>& out) const {
    out.clear();
    if (artifacts_.empty() || sym_refs_.empty()) return;
    if (max_out < 1u) max_out = 1u;
    if (max_out > 64u) max_out = 64u;

    std::string old_norm, new_norm;
    if (!ew_norm_token_ascii_(old_token_ascii, old_norm)) return;
    // new token is only used for collision-awareness (hook point); validate but do not require.
    const bool new_ok = ew_norm_token_ascii_(new_token_ascii, new_norm);

    const uint32_t old_h = fnv1a_u32_(old_norm);
    const uint32_t new_h = new_ok ? fnv1a_u32_(new_norm) : 0u;

    auto lb = std::lower_bound(sym_refs_.begin(), sym_refs_.end(), old_h,
        [](const SymRef& a, uint32_t h){ return a.sym_hash_u32 < h; });
    auto ub = std::upper_bound(sym_refs_.begin(), sym_refs_.end(), old_h,
        [](uint32_t h, const SymRef& a){ return h < a.sym_hash_u32; });
    if (lb == ub) return;

    // Score artifacts by old-symbol weight. Deterministic because sym_refs_ is sorted.
    std::vector<uint32_t> score(artifacts_.size(), 0u);
    for (auto it = lb; it != ub; ++it) {
        const uint32_t ai = it->art_index_u32;
        if (ai < score.size()) {
            uint32_t s = score[ai] + (uint32_t)it->weight_q15;
            score[ai] = (s > 0x7FFFFFFFu) ? 0x7FFFFFFFu : s;
        }
    }

    // Optional collision hint: if new token exists, down-rank artifacts already heavy on new token.
    if (new_ok) {
        auto lb2 = std::lower_bound(sym_refs_.begin(), sym_refs_.end(), new_h,
            [](const SymRef& a, uint32_t h){ return a.sym_hash_u32 < h; });
        auto ub2 = std::upper_bound(sym_refs_.begin(), sym_refs_.end(), new_h,
            [](uint32_t h, const SymRef& a){ return h < a.sym_hash_u32; });
        for (auto it = lb2; it != ub2; ++it) {
            const uint32_t ai = it->art_index_u32;
            if (ai < score.size()) {
                // collision penalty (hook point): subtract a fraction deterministically.
                const uint32_t penalty = (uint32_t)it->weight_q15 >> 2;
                score[ai] = (score[ai] > penalty) ? (score[ai] - penalty) : 0u;
            }
        }
    }

    struct Tmp { uint32_t ai; uint32_t s; };
    std::vector<Tmp> tmp;
    tmp.reserve(256);
    for (uint32_t ai = 0; ai < (uint32_t)score.size(); ++ai) {
        if (score[ai] == 0u) continue;
        tmp.push_back({ai, score[ai]});
    }
    std::sort(tmp.begin(), tmp.end(), [&](const Tmp& a, const Tmp& b){
        if (a.s != b.s) return a.s > b.s;
        return artifacts_[a.ai].rel_path < artifacts_[b.ai].rel_path;
    });

    const uint32_t out_n = (uint32_t)((tmp.size() < (size_t)max_out) ? tmp.size() : (size_t)max_out);
    out.reserve(out_n);
    for (uint32_t i = 0; i < out_n; ++i) {
        Match m;
        m.rel_path = artifacts_[tmp[i].ai].rel_path;
        m.score_u32 = tmp[i].s;
        out.push_back(std::move(m));
    }
}


void EwCoherenceGraph::query_semantic_patch_targets(const std::string& request_utf8, uint32_t max_out,
                                                   std::vector<SemanticPatchTarget>& out) const {
    out.clear();
    if (anchor_regions_.empty() || artifacts_.empty()) return;
    if (max_out < 1u) max_out = 1u;
    if (max_out > 64u) max_out = 64u;

    std::vector<std::string> req_toks;
    tokenize_identifiers_ascii_(request_utf8, req_toks);
    if (req_toks.empty()) return;
    const std::string req_lc = [&]() {
        std::string s; s.reserve(request_utf8.size());
        for (char c : request_utf8) {
            unsigned char uc = (unsigned char)c;
            if (uc >= 0x80) continue;
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            s.push_back(c);
        }
        return s;
    }();
    const bool prefers_insert = (req_lc.find("add") != std::string::npos) ||
                                (req_lc.find("insert") != std::string::npos) ||
                                (req_lc.find("append") != std::string::npos) ||
                                (req_lc.find("new ") != std::string::npos);

    struct Tmp { SemanticPatchTarget t; };
    std::vector<Tmp> tmp;
    tmp.reserve(anchor_regions_.size());
    std::vector<std::string> region_toks;
    for (const auto& rg : anchor_regions_) {
        if (rg.art_index_u32 >= artifacts_.size()) continue;
        tokenize_identifiers_ascii_(rg.region_text, region_toks);
        uint32_t score = 0;
        for (const auto& rt : req_toks) {
            for (const auto& gt : region_toks) {
                if (rt == gt) { score += 1024u; break; }
            }
            if (artifacts_[rg.art_index_u32].rel_path.find(rt) != std::string::npos) score += 192u;
            if (!rg.anchor_a.empty() && rg.anchor_a.find(rt) != std::string::npos) score += 96u;
            if (!rg.anchor_b.empty() && rg.anchor_b.find(rt) != std::string::npos) score += 64u;
            if (!rg.insertion_anchor.empty() && rg.insertion_anchor.find(rt) != std::string::npos) score += 48u;
        }
        if (score == 0u) continue;
        SemanticPatchTarget t;
        t.rel_path = artifacts_[rg.art_index_u32].rel_path;
        if (!rg.anchor_a.empty() && !rg.anchor_b.empty() && !prefers_insert) {
            t.patch_mode_u16 = 3u; // EW_PATCH_REPLACE_BETWEEN_ANCHORS
            t.anchor_a = rg.anchor_a;
            t.anchor_b = rg.anchor_b;
            t.reason_utf8 = std::string("coherence region ") + rg.anchor_a + ".." + rg.anchor_b;
            t.score_u32 = score + 256u;
        } else if (!rg.insertion_anchor.empty()) {
            t.patch_mode_u16 = 2u; // EW_PATCH_INSERT_AFTER_ANCHOR
            t.anchor_a = rg.insertion_anchor;
            t.reason_utf8 = std::string("coherence anchor ") + rg.insertion_anchor;
            t.score_u32 = score + (prefers_insert ? 320u : 64u);
        } else {
            continue;
        }
        tmp.push_back({std::move(t)});
    }
    std::sort(tmp.begin(), tmp.end(), [](const Tmp& a, const Tmp& b) {
        if (a.t.score_u32 != b.t.score_u32) return a.t.score_u32 > b.t.score_u32;
        if (a.t.rel_path != b.t.rel_path) return a.t.rel_path < b.t.rel_path;
        if (a.t.anchor_a != b.t.anchor_a) return a.t.anchor_a < b.t.anchor_a;
        return a.t.anchor_b < b.t.anchor_b;
    });
    for (const auto& e : tmp) {
        if (out.size() >= max_out) break;
        bool dup = false;
        for (const auto& o : out) {
            if (o.rel_path == e.t.rel_path && o.patch_mode_u16 == e.t.patch_mode_u16 && o.anchor_a == e.t.anchor_a && o.anchor_b == e.t.anchor_b) { dup = true; break; }
        }
        if (!dup) out.push_back(e.t);
    }
}


void EwCoherenceGraph::resolve_semantic_patch_target(const std::string& request_utf8, uint32_t max_out,
                                                     SemanticPatchDecision& out) const {
    out = SemanticPatchDecision{};
    query_semantic_patch_targets(request_utf8, max_out, out.ranked_candidates);
    if (out.ranked_candidates.empty()) return;
    out.resolved_u8 = 1u;
    out.winner = out.ranked_candidates[0];
    uint32_t tie_break_count = 0u;
    if (out.ranked_candidates.size() > 1u) {
        const uint32_t best_score = out.winner.score_u32;
        const uint32_t second_score = out.ranked_candidates[1].score_u32;
        if (second_score == best_score) out.ambiguity_level_u8 = 3u;
        else if (second_score + 64u >= best_score) out.ambiguity_level_u8 = 2u;
        else if (second_score + 256u >= best_score) out.ambiguity_level_u8 = 1u;
        for (size_t i = 1; i < out.ranked_candidates.size(); ++i) {
            if (out.ranked_candidates[i].score_u32 == best_score) ++tie_break_count;
        }
        out.human_review_prudent_u8 = (out.ambiguity_level_u8 >= 2u || tie_break_count > 0u) ? 1u : 0u;
    }
    out.winner_reason_utf8 = std::string("winner path=") + out.winner.rel_path +
                             " score=" + std::to_string((unsigned)out.winner.score_u32) +
                             " mode=" + std::to_string((unsigned)out.winner.patch_mode_u16) +
                             " reason=" + out.winner.reason_utf8;
    if (tie_break_count > 0u) {
        out.winner_reason_utf8 += " tie_break=lexicographic_path_anchor";
    } else {
        out.winner_reason_utf8 += " tie_break=not_needed";
    }
    if (out.ranked_candidates.size() > 1u) {
        const size_t cap = std::min<size_t>(out.ranked_candidates.size(), 4u);
        for (size_t i = 1; i < cap; ++i) {
            const auto& c = out.ranked_candidates[i];
            if (!out.rejected_candidates_utf8.empty()) out.rejected_candidates_utf8 += " | ";
            out.rejected_candidates_utf8 += c.rel_path;
            out.rejected_candidates_utf8 += " score=" + std::to_string((unsigned)c.score_u32);
            out.rejected_candidates_utf8 += " reason=" + c.reason_utf8;
            if (c.score_u32 == out.winner.score_u32) out.rejected_candidates_utf8 += " rejected_by=lexicographic_tie_break";
            else out.rejected_candidates_utf8 += " rejected_by=lower_semantic_score";
        }
    }
}

std::string EwCoherenceGraph::debug_stats() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "coherence_graph artifacts=%llu anchor_regions=%llu sym_refs=%llu unique_syms=%llu",
                  (unsigned long long)artifacts_.size(),
                  (unsigned long long)anchor_regions_.size(),
                  (unsigned long long)sym_refs_.size(),
                  (unsigned long long)unique_syms_u32_);
    return std::string(buf);
}
