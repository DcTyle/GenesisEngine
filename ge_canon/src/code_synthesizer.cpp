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

static bool emit_function_stub_patch(SubstrateManager* sm, const std::string& rel_path, const std::string& func_name) {
    if (!sm) return false;
    if (rel_path.empty() || func_name.empty()) return false;

    EwPatchSpec ps;
    ps.mode_u16 = EW_PATCH_APPEND_EOF;
    ps.text = "\n// EW_ANCHOR:EW_SYNTH_STUB_BEGIN\n";
    ps.text += "// Synthesized stub (deterministic). Fill in logic under coherence gate.\n";
    ps.text += "static int ";
    ps.text += func_name;
    ps.text += "(int argc, char** argv) {\n";
    ps.text += "    (void)argc; (void)argv;\n";
    ps.text += "    return 0;\n";
    ps.text += "}\n";
    ps.text += "// EW_ANCHOR:EW_SYNTH_STUB_END\n";

    return code_apply_patch_coherence_gated(sm, rel_path, kind_from_path(rel_path), ps, 0xE310u);
}

static bool emit_cli_tool(SubstrateManager* sm, const std::string& tool_name) {
    if (!sm) return false;
    if (tool_name.empty()) return false;

    const std::string base_dir = "Draft Container/GenesisEngine/src/tools/";
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
    const std::string cmake_path = "Draft Container/GenesisEngine/CMakeLists.txt";
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
            const bool ok = emit_function_stub_patch(sm, explicit_path, func);
            sm->emit_ui_line(std::string("SYNTHCODE_PATCH ") + explicit_path + " ok=" + (ok ? "1" : "0"));
            return ok;
        }

        std::vector<EwCoherenceGraph::Match> ms;
        cg.query_best(req, 8u, ms);
        if (!ms.empty()) {
            const std::string target = ms[0].rel_path;
            const bool ok = emit_function_stub_patch(sm, target, func);
            sm->emit_ui_line(std::string("SYNTHCODE_PATCH ") + target + " ok=" + (ok ? "1" : "0"));
            return ok;
        }
    }

    // Fallback.
    std::string name = extract_first_ident(req);
    if (name.empty()) name = "eigenware_module";
    code_emit_minimal_cpp_module(sm, name);
    sm->emit_ui_line(std::string("SYNTHCODE_FALLBACK_MODULE ") + name);
    return true;
}
