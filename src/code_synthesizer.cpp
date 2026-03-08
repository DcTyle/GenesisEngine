#include "code_synthesizer.hpp"

#include "GE_runtime.hpp"
#include "coherence_graph.hpp"
#include "code_artifact_ops.hpp"
#include "coherence_gate.hpp"

#include <algorithm>

static inline std::string lower_ascii_only(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x80) continue;
        char cc = (char)c;
        if (cc >= 'A' && cc <= 'Z') cc = (char)(cc - 'A' + 'a');
        out.push_back(cc);
    }
    return out;
}

static inline void trim_left(std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    if (i) s = s.substr(i);
}

static inline std::string extract_first_ident(const std::string& s) {
    auto is_start = [](char c){ return (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    auto is_body = [&](char c){ return is_start(c) || (c >= '0' && c <= '9'); };
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char uc = (unsigned char)s[i];
        if (uc >= 0x80) continue;
        if (!is_start((char)uc)) continue;
        size_t j = i + 1;
        while (j < s.size()) {
            unsigned char uj = (unsigned char)s[j];
            if (uj >= 0x80) break;
            if (!is_body((char)uj)) break;
            ++j;
        }
        return s.substr(i, j - i);
    }
    return std::string();
}

static inline bool looks_like_rel_path(const std::string& s) {
    return (s.find(".cpp") != std::string::npos) || (s.find(".hpp") != std::string::npos) ||
           (s.find("CMakeLists.txt") != std::string::npos) || (s.find(".cmake") != std::string::npos);
}

static std::string pick_explicit_path_or_empty(const std::string& request) {
    const std::string lc = lower_ascii_only(request);
    const char* keys[] = {"to file ", "file ", "path ", "in "};
    for (size_t ki = 0; ki < sizeof(keys)/sizeof(keys[0]); ++ki) {
        const std::string k(keys[ki]);
        size_t p = lc.find(k);
        if (p == std::string::npos) continue;
        size_t s = p + k.size();
        size_t e = s;
        while (e < request.size() && request[e] != ' ' && request[e] != '\t' && request[e] != '\r' && request[e] != '\n') ++e;
        if (e <= s) continue;
        std::string cand = request.substr(s, e - s);
        if (looks_like_rel_path(cand)) return cand;
    }
    return std::string();
}



static std::string sanitize_slug_ascii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char uc = (unsigned char)s[i];
        if (uc >= 0x80) continue;
        char c = (char)uc;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(c);
        else if (c == '_' || c == '-' || c == ' ') out.push_back('_');
    }
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) out = "patch_view";
    if (out.size() > 48) out.resize(48);
    return out;
}

static void emit_patch_view_artifact(SubstrateManager* sm,
                                     const std::string& request_utf8,
                                     const std::vector<EwCoherenceGraph::SemanticPatchTarget>& semantic_targets,
                                     const std::vector<EwCoherenceGraph::Match>& fallback_matches,
                                     const std::string& chosen_rel_path,
                                     const std::string& chosen_anchor_a,
                                     const std::string& chosen_anchor_b,
                                     uint16_t chosen_mode_u16,
                                     bool used_semantic_binding) {
    if (!sm) return;
    const std::string slug = sanitize_slug_ascii(extract_first_ident(request_utf8));
    const std::string rel_path = std::string("docs/ai_patch_views/") +
                                 slug + "_" + std::to_string((unsigned long long)sm->canonical_tick) + ".md";
    std::string md;
    md.reserve(4096);
    md += "# Genesis AI Coherent Patch View\n\n";
    md += "- Tick: `" + std::to_string((unsigned long long)sm->canonical_tick) + "`\n";
    md += "- Request: `" + request_utf8 + "`\n";
    md += "- Coherence relation layer: emergent coherence determines what artifacts and anchors are related.\n";
    md += "- Patch scope layer: the coherent patch view narrows those relations into likely logic regions that should change together.\n";
    md += "- Canonical binding layer: the chosen write target resolves the patch view into exact source/export spans.\n\n";
    md += "## Chosen Canonical Binding\n\n";
    md += "- Binding mode: `";
    md += used_semantic_binding ? "semantic-anchor" : "fallback-file";
    md += "`\n";
    md += "- Target file: `" + (chosen_rel_path.empty() ? std::string("<none>") : chosen_rel_path) + "`\n";
    md += "- Patch mode: `" + std::to_string((unsigned)chosen_mode_u16) + "`\n";
    md += "- Anchor A: `" + (chosen_anchor_a.empty() ? std::string("<none>") : chosen_anchor_a) + "`\n";
    md += "- Anchor B: `" + (chosen_anchor_b.empty() ? std::string("<none>") : chosen_anchor_b) + "`\n\n";
    md += "## Semantic Patch Scope Candidates\n\n";
    if (semantic_targets.empty()) {
        md += "No semantic anchor-bounded patch targets were available for this request.\n\n";
    } else {
        for (size_t i = 0; i < semantic_targets.size(); ++i) {
            const auto& t = semantic_targets[i];
            md += std::to_string((unsigned long long)(i + 1)) + ". `" + t.rel_path + "`";
            md += " score=`" + std::to_string((unsigned)t.score_u32) + "`";
            md += " mode=`" + std::to_string((unsigned)t.patch_mode_u16) + "`";
            md += " anchorA=`" + (t.anchor_a.empty() ? std::string("<none>") : t.anchor_a) + "`";
            md += " anchorB=`" + (t.anchor_b.empty() ? std::string("<none>") : t.anchor_b) + "`";
            md += " reason=`" + t.reason_utf8 + "`\n";
        }
        md += "\n";
    }
    md += "## Fallback File Ranking\n\n";
    if (fallback_matches.empty()) {
        md += "No fallback file matches were needed.\n";
    } else {
        for (size_t i = 0; i < fallback_matches.size(); ++i) {
            const auto& m = fallback_matches[i];
            md += std::to_string((unsigned long long)(i + 1)) + ". `" + m.rel_path + "` score=`" + std::to_string((unsigned)m.score_u32) + "`\n";
        }
    }
    md += "\n## Contract Reminder\n\n";
    md += "Coherence tells what is related. The coherent patch view tells what is in scope. Canonical binding tells where to write. The coherence view remains derived and is regenerated after canonical edits.\n";

    EwInspectorArtifact a;
    a.rel_path = rel_path;
    a.kind_u32 = (uint32_t)EW_ARTIFACT_MD;
    a.payload = md;
    a.producer_operator_id_u32 = 0xE312u;
    a.producer_tick_u64 = sm->canonical_tick;
    const EwCoherenceResult cr = EwCoherenceGate::validate_artifact(a.rel_path, a.kind_u32, a.payload);
    a.coherence_q15 = cr.coherence_q15;
    a.commit_ready = cr.commit_ready;
    a.denial_code_u32 = cr.denial_code_u32;
    sm->inspector_fields.upsert(a);
}


static void emit_patch_view_chat_lines(SubstrateManager* sm,
                                       const std::string& request_utf8,
                                       const std::vector<EwCoherenceGraph::SemanticPatchTarget>& semantic_targets,
                                       const std::vector<EwCoherenceGraph::Match>& fallback_matches,
                                       const std::string& chosen_rel_path,
                                       const std::string& chosen_anchor_a,
                                       const std::string& chosen_anchor_b,
                                       uint16_t chosen_mode_u16,
                                       bool used_semantic_binding) {
    if (!sm) return;
    sm->emit_ui_line(std::string("AI_PATCH_VIEW request=") + request_utf8);
    if (used_semantic_binding) {
        sm->emit_ui_line(std::string("AI_PATCH_SCOPE semantic target=") + chosen_rel_path +
                         " anchorA=" + (chosen_anchor_a.empty() ? std::string("<none>") : chosen_anchor_a) +
                         " anchorB=" + (chosen_anchor_b.empty() ? std::string("<none>") : chosen_anchor_b) +
                         " mode=" + std::to_string((unsigned)chosen_mode_u16));
    } else {
        sm->emit_ui_line(std::string("AI_PATCH_SCOPE fallback target=") +
                         (chosen_rel_path.empty() ? std::string("<none>") : chosen_rel_path) +
                         " mode=" + std::to_string((unsigned)chosen_mode_u16));
    }
    if (!semantic_targets.empty()) {
        const auto& t = semantic_targets[0];
        sm->emit_ui_line(std::string("AI_PATCH_REASON primary=") + t.reason_utf8 +
                         " score=" + std::to_string((unsigned)t.score_u32));
        const size_t cap = std::min<size_t>(semantic_targets.size(), 3u);
        for (size_t i = 0; i < cap; ++i) {
            const auto& alt = semantic_targets[i];
            sm->emit_ui_line(std::string("AI_PATCH_CANDIDATE ") + std::to_string((unsigned long long)(i + 1)) +
                             " path=" + alt.rel_path +
                             " score=" + std::to_string((unsigned)alt.score_u32) +
                             " anchorA=" + (alt.anchor_a.empty() ? std::string("<none>") : alt.anchor_a) +
                             " anchorB=" + (alt.anchor_b.empty() ? std::string("<none>") : alt.anchor_b));
        }
    } else if (!fallback_matches.empty()) {
        const size_t cap = std::min<size_t>(fallback_matches.size(), 3u);
        for (size_t i = 0; i < cap; ++i) {
            const auto& alt = fallback_matches[i];
            sm->emit_ui_line(std::string("AI_PATCH_FALLBACK ") + std::to_string((unsigned long long)(i + 1)) +
                             " path=" + alt.rel_path +
                             " score=" + std::to_string((unsigned)alt.score_u32));
        }
    }
    sm->emit_ui_line("AI_PATCH_BIND coherence tells related logic; patch view sets scope; canonical binding sets write target.");
}

static void emit_stub_refusal_lines(SubstrateManager* sm,
                                   const std::string& req,
                                   const std::string& rel_path,
                                   const std::string& func_name,
                                   const std::string& anchor_a,
                                   const std::string& anchor_b,
                                   uint16_t patch_mode_u16,
                                   bool used_semantic_binding,
                                   const std::string& reason_utf8) {
    if (!sm) return;
    sm->emit_ui_line(std::string("SYNTHCODE_STUB_REFUSED path=") +
                     (rel_path.empty() ? std::string("<none>") : rel_path) +
                     " function=" + (func_name.empty() ? std::string("<none>") : func_name));
    if (!reason_utf8.empty()) {
        sm->emit_ui_line(std::string("SYNTHCODE_STUB_REFUSED_REASON ") + reason_utf8);
    }

    std::vector<EwCoherenceGraph::SemanticPatchTarget> ts;
    std::vector<EwCoherenceGraph::Match> ms;
    if (used_semantic_binding) {
        EwCoherenceGraph::SemanticPatchTarget t{};
        t.rel_path = rel_path;
        t.anchor_a = anchor_a;
        t.anchor_b = anchor_b;
        t.patch_mode_u16 = patch_mode_u16;
        t.reason_utf8 = reason_utf8;
        ts.push_back(t);
    } else if (!rel_path.empty()) {
        EwCoherenceGraph::Match m{};
        m.rel_path = rel_path;
        ms.push_back(m);
    }

    emit_patch_view_artifact(sm, req, ts, ms, rel_path, anchor_a, anchor_b, patch_mode_u16, used_semantic_binding);
    emit_patch_view_chat_lines(sm, req, ts, ms, rel_path, anchor_a, anchor_b, patch_mode_u16, used_semantic_binding);
    sm->emit_ui_line("SYNTHCODE_NEXT produce a complete patch diff or module/tool artifact instead of a synthesized function stub.");
}

static bool emit_cli_tool(SubstrateManager* sm, const std::string& tool_name) {
    if (!sm) return false;
    if (tool_name.empty()) return false;

    const std::string base_dir = "src/tools/";
    const std::string cpp_path = base_dir + tool_name + "_main.cpp";

    std::string cpp;
    cpp.reserve(2048);
    cpp += "#include \"GE_runtime.hpp\"\n\n";
    cpp += "#include <cstdio>\n";
    cpp += "#include <string>\n\n";
    cpp += "static std::string join_args(int argc, char** argv) {\n";
    cpp += "    std::string out;\n";
    cpp += "    for (int i = 1; i < argc; ++i) {\n";
    cpp += "        if (i > 1) out.push_back(' ');\n";
    cpp += "        out += argv[i];\n";
    cpp += "    }\n";
    cpp += "    return out;\n";
    cpp += "}\n\n";
    cpp += "int main(int argc, char** argv) {\n";
    cpp += "    if (argc < 2) {\n";
    cpp += "        std::fprintf(stderr, \"usage: ";
    cpp += tool_name;
    cpp += " <request>\\n\");\n";
    cpp += "        return 2;\n";
    cpp += "    }\n";
    cpp += "    SubstrateManager sm;\n";
    cpp += "    sm.set_projection_seed(1u);\n";
    cpp += "    const std::string req = join_args(argc, argv);\n";
    cpp += "    sm.ui_submit_user_text_line(std::string(\"SYNTHCODE:\") + req);\n";
    cpp += "    for (int i = 0; i < 12; ++i) sm.tick();\n";
    cpp += "    for (;;) {\n";
    cpp += "        std::string out;\n";
    cpp += "        if (!sm.ui_pop_output_text(out)) break;\n";
    cpp += "        std::printf(\"%s\\n\", out.c_str());\n";
    cpp += "    }\n";
    cpp += "    return 0;\n";
    cpp += "}\n";

    EwInspectorArtifact a;
    a.rel_path = cpp_path;
    a.kind_u32 = (uint32_t)EW_ARTIFACT_CPP;
    a.payload = cpp;
    a.producer_operator_id_u32 = 0xE320u;
    a.producer_tick_u64 = sm->canonical_tick;
    const EwCoherenceResult cr = EwCoherenceGate::validate_artifact(a.rel_path, a.kind_u32, a.payload);
    a.coherence_q15 = cr.coherence_q15;
    a.commit_ready = cr.commit_ready;
    a.denial_code_u32 = cr.denial_code_u32;
    sm->inspector_fields.upsert(a);

    // Patch top-level CMakeLists to add this executable.
    const std::string cmake_path = "CMakeLists.txt";
    EwPatchSpec ps;
    ps.mode_u16 = EW_PATCH_APPEND_EOF;
    ps.text = "\n# EW_ANCHOR:EW_SYNTH_TOOLS\n";
    ps.text += "add_executable(";
    ps.text += tool_name;
    ps.text += " src/tools/";
    ps.text += tool_name;
    ps.text += "_main.cpp)\n";
    ps.text += "target_link_libraries(";
    ps.text += tool_name;
    ps.text += " PRIVATE EigenWareCore)\n";
    (void)code_apply_patch_coherence_gated(sm, cmake_path, (uint32_t)EW_ARTIFACT_CMAKE, ps, 0xE321u);

    return true;
}

bool EwCodeSynthesizer::synthesize(SubstrateManager* sm, const std::string& request_utf8) {
    if (!sm) return false;
    std::string req = request_utf8;
    trim_left(req);
    if (req.empty()) return false;

    EwCoherenceGraph cg;
    cg.rebuild_from_inspector(sm->inspector_fields);
    sm->emit_ui_line(std::string("SYNTHCODE_INDEX ") + cg.debug_stats());

    const std::string lc = lower_ascii_only(req);

    if (lc.find("create module") != std::string::npos || lc.find("new module") != std::string::npos) {
        std::string name = extract_first_ident(req);
        if (name.empty()) name = "eigenware_module";
        code_emit_minimal_cpp_module(sm, name);
        sm->emit_ui_line(std::string("SYNTHCODE_CREATED_MODULE ") + name);
        return true;
    }

    if (lc.find("cli") != std::string::npos || lc.find("tool") != std::string::npos) {
        std::string name = extract_first_ident(req);
        if (name.empty()) name = "ew_synth_tool";
        std::string nn;
        for (size_t i = 0; i < name.size(); ++i) {
            char c = name[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') nn.push_back(c);
        }
        if (nn.empty()) nn = "ew_synth_tool";
        emit_cli_tool(sm, nn);
        sm->emit_ui_line(std::string("SYNTHCODE_CREATED_TOOL ") + nn);
        return true;
    }

    // Add stub.
    std::string explicit_path = pick_explicit_path_or_empty(req);
    std::string func;
    {
        const std::string key = "function ";
        size_t p = lc.find(key);
        if (p != std::string::npos) {
            std::string tail = req.substr(p + key.size());
            func = extract_first_ident(tail);
        }
        if (func.empty()) func = extract_first_ident(req);
    }

    if (!func.empty()) {
        if (!explicit_path.empty()) {
            emit_stub_refusal_lines(sm, req, explicit_path, func, std::string(), std::string(),
                                    (uint16_t)EW_PATCH_APPEND_EOF, false,
                                    "stub/function synthesis is disabled on the canonical path; emit a complete diff or artifact instead");
            return false;
        }

        std::vector<EwCoherenceGraph::Match> ms;
        cg.query_best(req, 8u, ms);
        std::vector<EwCoherenceGraph::SemanticPatchTarget> ts;
        cg.query_semantic_patch_targets(req, 8u, ts);
        if (!ts.empty()) {
            emit_stub_refusal_lines(sm, req, ts[0].rel_path, func, ts[0].anchor_a, ts[0].anchor_b,
                                    ts[0].patch_mode_u16, true,
                                    ts[0].reason_utf8.empty()
                                        ? std::string("stub/function synthesis is disabled on the canonical path; emit a complete diff or artifact instead")
                                        : std::string("stub/function synthesis is disabled on the canonical path; candidate binding reason=") + ts[0].reason_utf8);
        }
        if (!ms.empty()) {
            const std::string target = ms[0].rel_path;
            emit_stub_refusal_lines(sm, req, target, func, std::string(), std::string(),
                                    (uint16_t)EW_PATCH_APPEND_EOF, false,
                                    "stub/function synthesis is disabled on the canonical path; emit a complete diff or artifact instead");
            return false;
        }
    }

    // Fallback.
    std::string name = extract_first_ident(req);
    if (name.empty()) name = "eigenware_module";
    code_emit_minimal_cpp_module(sm, name);
    sm->emit_ui_line(std::string("SYNTHCODE_FALLBACK_MODULE ") + name);
    return true;
}
