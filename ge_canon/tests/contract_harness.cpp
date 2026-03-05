#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <cmath>
#include <string>
#include <vector>

#include "GE_runtime.hpp"
#include "GE_operator_registry.hpp"

static EwLedger compute_ledger_from_sim(const SubstrateMicroprocessor& sim);

static inline void write_text_file(const std::string& path, const std::string& content) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
}

static inline uint64_t u64_absdiff(uint64_t a, uint64_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static inline int64_t i64_abs(int64_t x) { return (x < 0) ? -x : x; }

static inline void write_ppm_rgb(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::string header = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    std::fwrite(header.data(), 1, header.size(), f);
    if (!rgb.empty()) std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
}

static inline uint8_t u8_clamp_i64(int64_t x) {
    if (x < 0) return 0;
    if (x > 255) return 255;
    return static_cast<uint8_t>(x);
}

static inline void coord_sig9_u64x9(const SubstrateMicroprocessor& sim, uint64_t out9[9]) {
    for (int i = 0; i < 9; ++i) out9[i] = 0ULL;
    for (size_t k = 0; k < sim.anchors.size(); ++k) {
        const Anchor& a = sim.anchors[k];
        out9[0] += (uint64_t)a.id;
        out9[1] += (uint64_t)a.object_id_u64;
        out9[2] += (uint64_t)(uint64_t)(a.theta_q);
        out9[3] += (uint64_t)(uint64_t)(a.chi_q);
        out9[4] += (uint64_t)(uint64_t)(a.m_q);
        out9[5] += (uint64_t)(uint64_t)(a.tau_turns_q);
        out9[6] += (uint64_t)(uint64_t)(a.curvature_q);
        out9[7] += (uint64_t)(uint64_t)(a.doppler_q);
        out9[8] += (uint64_t)(uint64_t)(a.paf_turns_q);
    }
}

static inline void emit_rvc_artifacts(const std::string& out_dir,
                                      const SubstrateMicroprocessor& sim,
                                      int steps,
                                      uint64_t seed) {
    const size_t n = sim.anchors.size();
    const int w = (int)std::ceil(std::sqrt((double)n));
    const int h = w;

    uint64_t sig9[9];
    coord_sig9_u64x9(sim, sig9);

    // run.log (human-readable)
    {
        std::string log;
        log += "width=" + std::to_string(w) + "\n";
        log += "height=" + std::to_string(h) + "\n";
        log += "steps=" + std::to_string(steps) + "\n";
        log += "seed=" + std::to_string((unsigned long long)seed) + "\n";
        log += "anchor_coord_sig9_u64x9=";
        for (int i = 0; i < 9; ++i) {
            log += std::to_string((unsigned long long)sig9[i]);
            if (i != 8) log += ",";
        }
        log += "\n";
        log += "accepted=1\n";
        log += "sink_reason=none\n";
        write_text_file(out_dir + "/run.log", log);
    }

    // state.json (machine-readable)
    {
        std::map<uint64_t, uint64_t> counts;
        for (size_t i = 0; i < n; ++i) counts[sim.anchors[i].object_id_u64] += 1ULL;

        const EwLedger L = compute_ledger_from_sim(sim);
        std::string j;
        j += "{\n";
        j += "  \"width\": " + std::to_string(w) + ",\n";
        j += "  \"height\": " + std::to_string(h) + ",\n";
        j += "  \"steps\": " + std::to_string(steps) + ",\n";
        j += "  \"seed\": " + std::to_string((unsigned long long)seed) + ",\n";
        j += "  \"anchor_coord_sig9_u64x9\": [";
        for (int i = 0; i < 9; ++i) {
            j += std::to_string((unsigned long long)sig9[i]);
            if (i != 8) j += ",";
        }
        j += "],\n";
        j += "  \"total_mass_plus_res_q\": " + std::to_string((long long)L.total_mass_plus_res_q) + ",\n";
        j += "  \"reservoir_q\": " + std::to_string((long long)L.reservoir_q) + ",\n";
        j += "  \"object_counts\": {";
        bool first = true;
        for (auto it = counts.begin(); it != counts.end(); ++it) {
            if (!first) j += ",";
            first = false;
            j += "\n    \"" + std::to_string((unsigned long long)it->first) + "\": " + std::to_string((unsigned long long)it->second);
        }
        if (!counts.empty()) j += "\n  ";
        j += "}\n";
        j += "}\n";
        write_text_file(out_dir + "/state.json", j);
    }

    // lattice projection image (PPM)
    {
        std::vector<uint8_t> rgb;
        rgb.resize((size_t)w * (size_t)h * 3ULL, 0);
        for (size_t i = 0; i < n; ++i) {
            const Anchor& a = sim.anchors[i];
            const int x = (int)(i % (size_t)w);
            const int y = (int)(i / (size_t)w);
            const size_t p = ((size_t)y * (size_t)w + (size_t)x) * 3ULL;
            const uint32_t t = (uint32_t)(a.theta_q % TURN_SCALE);
            const uint32_t c = (uint32_t)(a.chi_q % TURN_SCALE);
            const uint8_t r = (uint8_t)((t * 255U) / (uint32_t)TURN_SCALE);
            const uint8_t g = (uint8_t)((c * 255U) / (uint32_t)TURN_SCALE);
            const uint8_t b = (uint8_t)(((t ^ c) * 255U) / (uint32_t)TURN_SCALE);
            rgb[p + 0] = r;
            rgb[p + 1] = g;
            rgb[p + 2] = b;
        }
        write_ppm_rgb(out_dir + "/lattice_x0x1.ppm", w, h, rgb);
    }

    // object reference image (PPM)
    {
        std::vector<uint8_t> rgb;
        rgb.resize((size_t)w * (size_t)h * 3ULL, 0);
        for (size_t i = 0; i < n; ++i) {
            const Anchor& a = sim.anchors[i];
            const int x = (int)(i % (size_t)w);
            const int y = (int)(i / (size_t)w);
            const size_t p = ((size_t)y * (size_t)w + (size_t)x) * 3ULL;
            const uint64_t oid = a.object_id_u64;
            rgb[p + 0] = (uint8_t)((oid >> 0) & 0xFF);
            rgb[p + 1] = (uint8_t)((oid >> 8) & 0xFF);
            rgb[p + 2] = (uint8_t)((oid >> 16) & 0xFF);
        }
        write_ppm_rgb(out_dir + "/object_map.ppm", w, h, rgb);
    }
}

static EwLedger compute_ledger_from_sim(const SubstrateMicroprocessor& sim) {
    EwState s;
    s.canonical_tick = sim.canonical_tick;
    s.reservoir = sim.reservoir;
    s.boundary_scale_q32_32 = sim.boundary_scale_q32_32;
    s.anchors = sim.anchors;
    s.lanes = sim.lanes;
    return compute_ledger(s);
}

static bool run_determinism_harness(std::string* repro_out) {
    SubstrateMicroprocessor a(64);
    SubstrateMicroprocessor b(64);
    a.projection_seed = 123;
    b.projection_seed = 123;

    a.configure_cosmic_expansion(1, 1);
    b.configure_cosmic_expansion(1, 1);

    a.sx_q32_32 = (1LL << 32);
    b.sx_q32_32 = (1LL << 32);

    a.inject_text_utf8("determinism");
    b.inject_text_utf8("determinism");

    for (int k = 0; k < 200; ++k) {
        a.tick();
        b.tick();
        if (a.canonical_tick != b.canonical_tick) {
            if (repro_out) *repro_out = "tick_mismatch";
            return false;
        }
        if (a.reservoir != b.reservoir) {
            if (repro_out) *repro_out = "reservoir_mismatch";
            return false;
        }
        for (size_t i = 0; i < a.anchors.size(); ++i) {
            const Anchor& aa = a.anchors[i];
            const Anchor& bb = b.anchors[i];
            if (aa.theta_q != bb.theta_q || aa.chi_q != bb.chi_q || aa.m_q != bb.m_q) {
                if (repro_out) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "anchor_mismatch i=%zu k=%d", i, k);
                    *repro_out = buf;
                }
                return false;
            }
        }
    }
    return true;
}

static bool run_conservation_harness(uint64_t* max_resid_out, std::string* repro_out) {
    SubstrateMicroprocessor sim(128);
    sim.projection_seed = 7;

    const EwLedger L0 = compute_ledger_from_sim(sim);
    const uint64_t m0 = (L0.total_mass_plus_res_q < 0) ? 0ULL : (uint64_t)L0.total_mass_plus_res_q;
    uint64_t max_resid = 0;
    int max_k = 0;

    for (int k = 0; k < 400; ++k) {
        sim.tick();
        const EwLedger Lk = compute_ledger_from_sim(sim);
        const uint64_t mk = (Lk.total_mass_plus_res_q < 0) ? 0ULL : (uint64_t)Lk.total_mass_plus_res_q;
        const uint64_t resid = u64_absdiff(mk, m0);
        if (resid > max_resid) {
            max_resid = resid;
            max_k = k;
        }
    }

    if (max_resid_out) *max_resid_out = max_resid;
    // For this prototype, total_mass must be exactly conserved because reservoir
    // is incremented by the exact leak removed from m_q.
    if (max_resid != 0) {
        if (repro_out) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "mass_residual=%llu at k=%d", (unsigned long long)max_resid, max_k);
            *repro_out = buf;
        }
        return false;
    }
    return true;
}

static bool run_decoherence_harness(uint64_t* gamma_effect_out, std::string* repro_out) {
    SubstrateMicroprocessor s0(32);
    SubstrateMicroprocessor s1(32);
    s0.projection_seed = 99;
    s1.projection_seed = 99;

    // Same initial state; only frame gamma differs.
    s0.frame_gamma_turns_q = 0;
    s1.frame_gamma_turns_q = TURN_SCALE / 8;

    s0.tick();
    s1.tick();

    // Measure deterministic divergence in projected phase axis (basis d[4])
    // without any energy coupling claims.
    uint64_t effect = 0;
    for (size_t i = 0; i < s0.anchors.size(); ++i) {
        const int64_t a = s0.anchors[i].basis9.d[4];
        const int64_t b = s1.anchors[i].basis9.d[4];
        effect += (uint64_t)i64_abs(delta_turns(a, b));
    }

    if (gamma_effect_out) *gamma_effect_out = effect;
    if (effect == 0) {
        if (repro_out) *repro_out = "gamma_no_effect";
        return false;
    }
    return true;
}

static bool run_identity_harness(std::string* repro_out) {
    SubstrateMicroprocessor sim(64);
    sim.projection_seed = 5;

    std::vector<uint32_t> ids;
    ids.reserve(sim.anchors.size());
    for (size_t i = 0; i < sim.anchors.size(); ++i) ids.push_back(sim.anchors[i].id);

    for (int k = 0; k < 256; ++k) {
        sim.tick();
        if (!sim.check_invariants()) {
            if (repro_out) *repro_out = "invariant_failed";
            return false;
        }
        for (size_t i = 0; i < sim.anchors.size(); ++i) {
            if (sim.anchors[i].id != ids[i]) {
                if (repro_out) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "id_changed i=%zu k=%d", i, k);
                    *repro_out = buf;
                }
                return false;
            }
        }
    }
    return true;
}

static std::string arg_value(int argc, char** argv, const char* key, const char* default_value) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], key) == 0) return std::string(argv[i + 1]);
    }
    return std::string(default_value ? default_value : "");
}

int main(int argc, char** argv) {
    const std::string out_dir = arg_value(argc, argv, "--out_dir", ".");
    const int steps = std::atoi(arg_value(argc, argv, "--steps", "400").c_str());
    const uint64_t seed = (uint64_t)std::strtoull(arg_value(argc, argv, "--seed", "7").c_str(), nullptr, 10);

    std::string repro;
    bool ok_det = run_determinism_harness(&repro);
    std::string repro_det = repro;

    repro.clear();
    uint64_t max_mass_resid = 0;
    bool ok_cons = run_conservation_harness(&max_mass_resid, &repro);
    std::string repro_cons = repro;

    repro.clear();
    uint64_t gamma_effect = 0;
    bool ok_decoh = run_decoherence_harness(&gamma_effect, &repro);
    std::string repro_decoh = repro;

    repro.clear();
    bool ok_id = run_identity_harness(&repro);
    std::string repro_id = repro;

    const bool ok_all = ok_det && ok_cons && ok_decoh && ok_id;

    // RVC required artifacts (per run)
    {
        SubstrateMicroprocessor sim(128);
        sim.projection_seed = seed;
        sim.configure_cosmic_expansion(1, 1);
        for (int k = 0; k < steps; ++k) sim.tick();
        emit_rvc_artifacts(out_dir, sim, steps, seed);
    }

    // contract_metrics.json
    {
        std::string j;
        j += "{\n";
        j += "  \"determinism\": {\"pass\": " + std::string(ok_det ? "true" : "false") + ", \"repro\": \"" + repro_det + "\"},\n";
        j += "  \"conservation\": {\"pass\": " + std::string(ok_cons ? "true" : "false") + ", \"max_mass_residual\": " + std::to_string((unsigned long long)max_mass_resid) + ", \"repro\": \"" + repro_cons + "\"},\n";
        j += "  \"decoherence\": {\"pass\": " + std::string(ok_decoh ? "true" : "false") + ", \"gamma_effect_sum\": " + std::to_string((unsigned long long)gamma_effect) + ", \"repro\": \"" + repro_decoh + "\"},\n";
        j += "  \"identity\": {\"pass\": " + std::string(ok_id ? "true" : "false") + ", \"repro\": \"" + repro_id + "\"}\n";
        j += "}\n";
        write_text_file(out_dir + "/contract_metrics.json", j);
    }

    // failure_repro.txt
    if (!ok_all) {
        std::string r;
        if (!ok_det)  r += "determinism: " + repro_det + "\n";
        if (!ok_cons) r += "conservation: " + repro_cons + "\n";
        if (!ok_decoh) r += "decoherence: " + repro_decoh + "\n";
        if (!ok_id)   r += "identity: " + repro_id + "\n";
        write_text_file(out_dir + "/failure_repro.txt", r);
    } else {
        write_text_file(out_dir + "/failure_repro.txt", "pass\n");
    }

    // contract_report.md
    {
        std::string md;
        md += "# EigenWare Contract Harness Report\n\n";
        md += std::string("Result: ") + (ok_all ? "PASS" : "FAIL") + "\n\n";
        md += "## Determinism\n";
        md += std::string("- pass: ") + (ok_det ? "true" : "false") + "\n";
        md += std::string("- repro: ") + repro_det + "\n\n";
        md += "## Conservation\n";
        md += std::string("- pass: ") + (ok_cons ? "true" : "false") + "\n";
        md += "- max_mass_residual: " + std::to_string((unsigned long long)max_mass_resid) + "\n";
        md += std::string("- repro: ") + repro_cons + "\n\n";
        md += "## Decoherence\n";
        md += std::string("- pass: ") + (ok_decoh ? "true" : "false") + "\n";
        md += "- gamma_effect_sum: " + std::to_string((unsigned long long)gamma_effect) + "\n";
        md += std::string("- repro: ") + repro_decoh + "\n\n";
        md += "## Identity\n";
        md += std::string("- pass: ") + (ok_id ? "true" : "false") + "\n";
        md += std::string("- repro: ") + repro_id + "\n";
        write_text_file(out_dir + "/contract_report.md", md);
    }

    return ok_all ? 0 : 2;
}
