#include "code_artifact_ops.hpp"

#include "GE_runtime.hpp"
#include "coherence_gate.hpp"

#include <cctype>
#include <cstdio>
#include <algorithm>
#include <cstring>

uint32_t code_artifact_kind_from_rel_path(const std::string& rel_path) {
    if (rel_path.size() >= 4 && rel_path.substr(rel_path.size() - 4) == ".cpp") return (uint32_t)EW_ARTIFACT_CPP;
    if (rel_path.size() >= 4 && rel_path.substr(rel_path.size() - 4) == ".hpp") return (uint32_t)EW_ARTIFACT_HPP;
    if (rel_path.size() >= 14 && rel_path.substr(rel_path.size() - 14) == "CMakeLists.txt") return (uint32_t)EW_ARTIFACT_CMAKE;
    if (rel_path.size() >= 3 && rel_path.substr(rel_path.size() - 3) == ".md") return (uint32_t)EW_ARTIFACT_MD;
    return (uint32_t)EW_ARTIFACT_TEXT;
}

static std::string dirname_from_rel_path(const std::string& rel_path) {
    const size_t p = rel_path.find_last_of('/');
    if (p == std::string::npos) return std::string();
    return rel_path.substr(0, p + 1);
}

static bool file_exists_on_disk_repo(const std::string& rel_path) {
    // Repo-side include allow-list: includes that exist in the shipped source tree.
    // This does not access external resources; it only checks the local repo.
    // Deterministic: pure filesystem existence.
    FILE* f = std::fopen(rel_path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

static bool include_resolution_ok(SubstrateManager* sm, const std::string& rel_path, const std::string& payload) {
    if (!sm) return false;
    const std::string dir = dirname_from_rel_path(rel_path);
    size_t i = 0;
    while (i < payload.size()) {
        // line scan
        size_t j = payload.find('\n', i);
        if (j == std::string::npos) j = payload.size();
        const size_t len = (j > i) ? (j - i) : 0;
        const std::string line = payload.substr(i, len);
        i = (j < payload.size()) ? (j + 1) : j;

        // match: #include "..."
        const char* p = line.c_str();
        while (*p == ' ' || *p == '\t') ++p;
        if (p[0] == '#' && line.find("#include") != std::string::npos) {
            const size_t q1 = line.find('"');
            if (q1 == std::string::npos) continue;
            const size_t q2 = line.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            const std::string inc = line.substr(q1 + 1, q2 - (q1 + 1));
            if (inc.empty()) continue;
            // Resolve as local include: dir + inc
            std::string cand = dir + inc;

            // Inspector store has priority.
            if (sm->inspector_fields.find_by_path(cand)) continue;
            // If not present in inspector, allow shipped headers/sources.
            if (file_exists_on_disk_repo("include/" + inc)) continue;
            if (file_exists_on_disk_repo("src/" + inc)) continue;
            if (file_exists_on_disk_repo(cand)) continue;
            return false;
        }
    }
    return true;
}

static bool cmake_sanity_ok(SubstrateManager* sm, const std::string& rel_path, const std::string& payload) {
    (void)rel_path;
    if (!sm) return false;
    // Conservative: ensure referenced .cpp/.c/.hpp paths exist either in inspector or repo.
    size_t i = 0;
    while (i < payload.size()) {
        size_t j = payload.find('\n', i);
        if (j == std::string::npos) j = payload.size();
        const std::string line = payload.substr(i, j - i);
        i = (j < payload.size()) ? (j + 1) : j;
        auto check_ext = [&](const char* ext)->bool {
            const size_t p = line.find(ext);
            if (p == std::string::npos) return true;
            // extract run around ext
            size_t b = p;
            while (b > 0 && line[b-1] != ' ' && line[b-1] != '\t') --b;
            size_t e = p + std::strlen(ext);
            while (e < line.size() && line[e] != ' ' && line[e] != '\t' && line[e] != ')' ) ++e;
            const std::string tok = line.substr(b, e - b);
            if (tok.empty()) return true;
            // allow relative path as-is
            if (sm->inspector_fields.find_by_path(tok)) return true;
            if (file_exists_on_disk_repo(tok)) return true;
            // also allow within same directory as CMakeLists
            const std::string dir = dirname_from_rel_path(rel_path);
            if (!dir.empty() && sm->inspector_fields.find_by_path(dir + tok)) return true;
            if (!dir.empty() && file_exists_on_disk_repo(dir + tok)) return true;
            return false;
        };
        if (!check_ext(".cpp")) return false;
        if (!check_ext(".c")) return false;
        if (!check_ext(".hpp")) return false;
    }
    return true;
}

static int score_candidate(SubstrateManager* sm,
                           const std::string& rel_path,
                           uint32_t kind_u32,
                           const std::string& base_payload,
                           const std::string& cand_payload,
                           uint16_t coherence_q15,
                           bool commit_ready) {
    (void)sm;
    int score = 0;
    if (commit_ready) score += 50;
    score += (int)(coherence_q15 / 512); // 0..64
    const int diff = (int)std::abs((int)cand_payload.size() - (int)base_payload.size());
    score += std::max(0, 40 - (diff / 32));
    // bonus: format ends with newline
    if (!cand_payload.empty() && cand_payload.back() == '\n') score += 5;
    // extra validation
    if (kind_u32 == (uint32_t)EW_ARTIFACT_CPP || kind_u32 == (uint32_t)EW_ARTIFACT_HPP) {
        // include checks handled elsewhere; add small bonus if ok
        score += 5;
    }
    return score;
}

static bool find_unique_anchor_line(const std::string& payload, const std::string& anchor_name, size_t& out_pos) {
    // Anchor marker set:
    //   // EW_ANCHOR:<name>
    //   #  EW_ANCHOR:<name>
    //   <!-- EW_ANCHOR:<name> -->
    const std::string a1 = "// EW_ANCHOR:" + anchor_name;
    const std::string a2 = "# EW_ANCHOR:" + anchor_name;
    const std::string a3 = "<!-- EW_ANCHOR:" + anchor_name + " -->";
    size_t p = payload.find(a1);
    size_t q = payload.find(a2);
    size_t r = payload.find(a3);
    size_t found = std::string::npos;
    int count = 0;
    auto take = [&](size_t x){ if (x != std::string::npos) { found = (found == std::string::npos) ? x : std::min(found, x); ++count; } };
    take(p); take(q); take(r);
    // Ensure uniqueness across all forms.
    if (count == 0) return false;
    // Also ensure no second occurrence.
    auto second = [&](const std::string& pat)->bool {
        size_t f = payload.find(pat);
        if (f == std::string::npos) return false;
        size_t s = payload.find(pat, f + pat.size());
        return s != std::string::npos;
    };
    if (second(a1) || second(a2) || second(a3)) return false;
    out_pos = found;
    return true;
}

static std::string apply_patch_or_empty(const std::string& base_payload, const EwPatchSpec& spec, bool& ok) {
    ok = false;
    std::string out = base_payload;

    auto ensure_newline_join = [](std::string& s) {
        if (!s.empty() && s.back() != '\n') s.push_back('\n');
    };

    if (spec.mode_u16 == EW_PATCH_APPEND_EOF) {
        ensure_newline_join(out);
        out += spec.text;
        ensure_newline_join(out);
        ok = true;
        return out;
    }

    if (spec.mode_u16 == EW_PATCH_INSERT_AFTER_ANCHOR) {
        size_t pos = std::string::npos;
        if (!find_unique_anchor_line(out, spec.anchor_a, pos)) return out;
        // insert after end-of-line
        size_t eol = out.find('\n', pos);
        if (eol == std::string::npos) eol = out.size();
        else eol += 1;
        std::string insert = spec.text;
        if (!insert.empty() && insert.back() != '\n') insert.push_back('\n');
        out.insert(eol, insert);
        ok = true;
        return out;
    }

    if (spec.mode_u16 == EW_PATCH_REPLACE_BETWEEN_ANCHORS || spec.mode_u16 == EW_PATCH_DELETE_BETWEEN_ANCHORS) {
        size_t a = std::string::npos;
        size_t b = std::string::npos;
        if (!find_unique_anchor_line(out, spec.anchor_a, a)) return out;
        if (!find_unique_anchor_line(out, spec.anchor_b, b)) return out;
        if (b <= a) return out;
        // region is between end-of-line of A and start of line of B
        size_t a_eol = out.find('\n', a);
        if (a_eol == std::string::npos) return out;
        a_eol += 1;
        size_t b_sol = out.rfind('\n', b);
        if (b_sol == std::string::npos) b_sol = 0; else b_sol += 1;
        if (b_sol < a_eol) b_sol = a_eol;

        std::string repl;
        if (spec.mode_u16 == EW_PATCH_REPLACE_BETWEEN_ANCHORS) {
            repl = spec.text;
            if (!repl.empty() && repl.back() != '\n') repl.push_back('\n');
        }
        out.replace(a_eol, b_sol - a_eol, repl);
        ok = true;
        return out;
    }

    return out;
}

static std::string sanitize_module_name(const std::string& s) {
    // Deterministic sanitizer:
    // - Keep ASCII letters/digits/underscore.
    // - Convert '-' and space to '_'.
    // - Drop other bytes.
    // - Ensure non-empty.
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x80) continue;
        if (std::isalnum((int)c) || c == '_') {
            out.push_back((char)c);
            continue;
        }
        if (c == '-' || c == ' ') {
            out.push_back('_');
            continue;
        }
    }
    if (out.empty()) out = "eigenware_module";
    return out;
}

uint32_t path_fold_u32_from_rel_path(const std::string& rel_path) {
    // Deterministic fold. Used only for stable bookkeeping.
    uint32_t acc = 2166136261u;
    for (size_t i = 0; i < rel_path.size(); ++i) {
        acc ^= (uint8_t)rel_path[i];
        acc *= 16777619u;
    }
    return acc;
}

static uint64_t coord_fold9_from_text(const std::string& s) {
    // Deterministic 9D coordinate fold for artifact identity.
    // Fold bytes into a u64 with a simple integer mixing.
    uint64_t acc = 0xD6E8FEB86659FD93ULL;
    for (size_t i = 0; i < s.size(); ++i) {
        acc ^= (uint64_t)(uint8_t)s[i];
        acc = acc * 0x9E3779B97F4A7C15ULL + 0xBF58476D1CE4E5B9ULL;
    }
    return acc;
}

static void upsert_artifact(SubstrateManager* sm,
                            const std::string& rel_path,
                            uint32_t kind_u32,
                            const std::string& payload,
                            uint32_t producer_operator_id_u32) {
    if (!sm) return;

    EwInspectorArtifact a;
    a.rel_path = rel_path;
    a.kind_u32 = kind_u32;
    a.payload = payload;
    a.producer_operator_id_u32 = producer_operator_id_u32;
    a.producer_tick_u64 = sm->canonical_tick;
    a.coord_coord9_u64 = coord_fold9_from_text(rel_path + "\n" + payload);

    const EwCoherenceResult cr = EwCoherenceGate::validate_artifact(rel_path, kind_u32, payload);
    a.coherence_q15 = cr.coherence_q15;
    a.commit_ready = cr.commit_ready;
    a.denial_code_u32 = cr.denial_code_u32;

    sm->inspector_fields.upsert(a);

    EwAiActionEvent ev{};
    ev.tick_u64 = sm->canonical_tick;
    ev.sig9_u64 = sm->neural_ai.status().sig9_u64;
    ev.class_id_u32 = sm->neural_ai.status().class_id_u32;
    ev.kind_u16 = (uint16_t)EW_AI_ACTION_ARTIFACT_WRITE;
    ev.profile_id_u16 = 0;
    ev.target_anchor_id_u32 = 0;
    ev.f_code_i32 = 0;
    ev.a_code_u32 = 0;
    ev.confidence_q32_32 = sm->neural_ai.status().confidence_q32_32;
    ev.attractor_strength_q32_32 = sm->neural_ai.last_attractor_strength_q32_32();
    ev.frame_gamma_turns_q = sm->frame_gamma_turns_q;
    ev.artifact_coord_sig9_u64 = a.coord_coord9_u64;
    ev.artifact_kind_u32 = kind_u32;
    ev.artifact_path_code_u32 = path_fold_u32_from_rel_path(rel_path);
    sm->ai_log_event(ev);
}

bool code_apply_patch_coherence_gated(
    SubstrateManager* sm,
    const std::string& rel_path,
    uint32_t kind_u32,
    const EwPatchSpec& spec,
    uint32_t producer_operator_id_u32
) {
    if (!sm) return false;

    const EwInspectorArtifact* prev = sm->inspector_fields.find_by_path(rel_path);
    const std::string base_payload = prev ? prev->payload : std::string();
    const uint16_t base_coh = prev ? prev->coherence_q15 : 0;

    // Candidate generation: fixed N=3, deterministic variants.
    static constexpr int N = 3;
    struct Cand { std::string payload; uint16_t coh; bool commit; uint32_t denial; int score; bool ok; };
    Cand cands[N];
    for (int i = 0; i < N; ++i) { cands[i].coh = 0; cands[i].commit = false; cands[i].denial = 0; cands[i].score = -9999; cands[i].ok = false; }

    for (int i = 0; i < N; ++i) {
        EwPatchSpec s = spec;
        // Deterministic minor variants to help convergence.
        if (i == 1) {
            // Ensure a leading newline for inserts/replacements.
            if (!s.text.empty() && s.text.front() != '\n') s.text = "\n" + s.text;
        } else if (i == 2) {
            // Ensure both leading and trailing newline.
            if (!s.text.empty() && s.text.front() != '\n') s.text = "\n" + s.text;
            if (!s.text.empty() && s.text.back() != '\n') s.text.push_back('\n');
        }

        bool patch_ok = false;
        std::string out_payload = apply_patch_or_empty(base_payload, s, patch_ok);
        if (!patch_ok) { cands[i].ok = false; continue; }

        // Coherence + conservative validations.
        const EwCoherenceResult cr = EwCoherenceGate::validate_artifact(rel_path, kind_u32, out_payload);
        if (kind_u32 == (uint32_t)EW_ARTIFACT_CPP || kind_u32 == (uint32_t)EW_ARTIFACT_HPP) {
            if (!include_resolution_ok(sm, rel_path, out_payload)) {
                cands[i].ok = false;
                continue;
            }
        }
        if (kind_u32 == (uint32_t)EW_ARTIFACT_CMAKE) {
            if (!cmake_sanity_ok(sm, rel_path, out_payload)) {
                cands[i].ok = false;
                continue;
            }
        }

        cands[i].payload = out_payload;
        cands[i].coh = cr.coherence_q15;
        cands[i].commit = cr.commit_ready;
        cands[i].denial = cr.denial_code_u32;
        cands[i].ok = true;
        cands[i].score = score_candidate(sm, rel_path, kind_u32, base_payload, out_payload, cr.coherence_q15, cr.commit_ready);
    }

    // Select best.
    int best = -1;
    for (int i = 0; i < N; ++i) {
        if (!cands[i].ok) continue;
        if (best < 0) { best = i; continue; }
        if (cands[i].score > cands[best].score) { best = i; continue; }
        if (cands[i].score == cands[best].score) {
            const int di = (int)std::abs((int)cands[i].payload.size() - (int)base_payload.size());
            const int db = (int)std::abs((int)cands[best].payload.size() - (int)base_payload.size());
            if (di < db) { best = i; continue; }
            if (di == db && cands[i].payload < cands[best].payload) { best = i; continue; }
        }
    }
    if (best < 0) {
        // Route into dark excitation: deterministic small increment.
        const uint64_t inc = (uint64_t)(((__int128)INT64_MAX) / 2048);
        sm->dark_mass_q63_u64 += inc;
        return false;
    }

    // Coherence gate: must improve by minimum delta and be commit-ready.
    static constexpr uint16_t delta_min_q15 = 256; // ~0.0078
    if (!cands[best].commit || (uint16_t)(cands[best].coh - base_coh) < delta_min_q15) {
        const uint64_t inc = (uint64_t)(((__int128)INT64_MAX) / 1024);
        sm->dark_mass_q63_u64 += inc;
        return false;
    }

    upsert_artifact(sm, rel_path, kind_u32, cands[best].payload, producer_operator_id_u32);
    return true;
}

void code_emit_hydration_hint(SubstrateManager* sm, const std::string& root_dir_rel) {
    if (!sm) return;
    const std::string rel_path = "Draft Container/AI/hydration_hint.txt";
    const std::string payload = "HYDRATE_ROOT=" + root_dir_rel + "\n";
    upsert_artifact(sm, rel_path, (uint32_t)EW_ARTIFACT_TEXT, payload, 0xE180u);
}

void code_emit_minimal_cpp_module(SubstrateManager* sm, const std::string& module_name_utf8) {
    if (!sm) return;
    const std::string name = sanitize_module_name(module_name_utf8);

    const std::string base_dir = "Draft Container/Generated/";
    const std::string hpp_path = base_dir + name + ".hpp";
    const std::string cpp_path = base_dir + name + ".cpp";
    const std::string cmake_path = "Draft Container/Generated/CMakeLists.txt";

    const std::string hpp =
        "#pragma once\n"
        "\n"
        "#include <cstdint>\n"
        "#include <string>\n"
        "\n"
        "// Generated by EigenWare substrate inspector operator.\n"
        "// This artifact is committed only when coherence gate passes.\n"
        "\n"
        "namespace eigenware_generated {\n"
        "\n"
        "// Returns a stable identifier string for this module.\n"
        "const char* " + name + "_id();\n"
        "\n"
        "// Deterministic utility: packs a string into a u64 coordinate coord-tag\n"
        "// for bookkeeping inside liberty-space vector fields.\n"
        "uint64_t " + name + "_coord_coord9_from_utf8(const std::string& s);\n"
        "\n"
        "} // namespace eigenware_generated\n";

    const std::string cpp =
        "#include \"" + name + ".hpp\"\n"
        "#include \"text_encoder.hpp\"\n"
        "#include \"delta_profiles.hpp\"\n"
        "\n"
        "namespace eigenware_generated {\n"
        "\n"
        "const char* " + name + "_id() {\n"
        "    return \"" + name + "\";\n"
        "}\n"
        "\n"
        "uint64_t " + name + "_coord_coord9_from_utf8(const std::string& s) {\n"
        "    // Structural coord coord-tag (not a coord-tag): pack (byte_len, f_code).\n"
        "    const int32_t f = ew_text_utf8_to_frequency_code(s, (uint8_t)EW_PROFILE_LANGUAGE_INJECTION);\n"
        "    const uint32_t n = (uint32_t)s.size();\n"
        "    return (static_cast<uint64_t>(n) << 32) | static_cast<uint64_t>((uint32_t)f);\n"
        "}\n"
        "\n"
        "} // namespace eigenware_generated\n";

    const std::string cmake =
        "# Generated by EigenWare substrate inspector operator.\n"
        "cmake_minimum_required(VERSION 3.16)\n"
        "\n"
        "add_library(eigenware_generated STATIC\n"
        "    " + name + ".cpp\n"
        ")\n"
        "target_include_directories(eigenware_generated PUBLIC ${CMAKE_CURRENT_LIST_DIR})\n";

    // Producer operator id namespace: 0xE1xx reserved for code artifact ops.
    upsert_artifact(sm, hpp_path, (uint32_t)EW_ARTIFACT_HPP, hpp, 0xE101u);
    upsert_artifact(sm, cpp_path, (uint32_t)EW_ARTIFACT_CPP, cpp, 0xE102u);
    upsert_artifact(sm, cmake_path, (uint32_t)EW_ARTIFACT_CMAKE, cmake, 0xE103u);
}
