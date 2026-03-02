#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "GE_runtime.hpp"

static std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::string cur;
    for (;;) {
        int c = std::fgetc(f);
        if (c == EOF) break;
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back((char)c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    std::fclose(f);
    return out;
}

static void write_text(const std::string& path, const std::string& s) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
}

static void coord_coord9_u64x9(const SubstrateManager& sim, uint64_t out9[9]) {
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

int main(int argc, char** argv) {
    const char* out_path = (argc >= 2) ? argv[1] : "crawler_feedback.json";
    const char* seed_path = (argc >= 3) ? argv[2] : "../docs/crawler_seed_texts.txt";

    std::vector<std::string> seeds = read_lines(seed_path);
    if (seeds.empty()) {
        // Hard refusal: crawler requires configured deterministic seed list.
        return 2;
    }

    SubstrateManager sim(128);
    sim.projection_seed = 77;
    sim.configure_cosmic_expansion(1, 1);

    // Minimal crawler loop: ingestion -> actuation -> feedback.
    // Ingestion: each seed line is enqueued as a crawler observation.
    // Actuation: crawler runs inside substrate.tick() and admits pulses under budget.
    // Feedback: capture coord-tag and ancilla convergence.
    for (size_t i = 0; i < seeds.size(); ++i) {
        const uint64_t artifact_id_u64 = (uint64_t)(i + 1);
        const uint32_t stream_id_u32 = 1;
        const uint32_t extractor_id_u32 = 1;
        const uint32_t trust_class_u32 = 1;
        const uint32_t causal_tag_u32 = (uint32_t)((i + 1) & 0xFFU);
        sim.crawler_enqueue_observation_utf8(
            artifact_id_u64,
            stream_id_u32,
            extractor_id_u32,
            trust_class_u32,
            causal_tag_u32,
            std::string("local"),
            std::string("seed:") + std::to_string((unsigned long long)(i + 1)),
            seeds[i]
        );
        for (int k = 0; k < 32; ++k) sim.tick();
    }

    uint64_t coord9[9];
    coord_coord9_u64x9(sim, coord9);

    int64_t conv_sum = 0;
    for (size_t i = 0; i < sim.ancilla.size(); ++i) {
        conv_sum += sim.ancilla[i].convergence_metric_q32_32;
    }

    std::string j;
    j += "{\n";
    j += "  \"crawler_steps\": " + std::to_string((unsigned long long)seeds.size()) + ",\n";
    j += "  \"canonical_tick\": " + std::to_string((unsigned long long)sim.canonical_tick) + ",\n";
    j += "  \"anchor_coord_coord9_u64x9\": [";
    for (int i = 0; i < 9; ++i) {
        j += std::to_string((unsigned long long)coord9[i]);
        if (i != 8) j += ",";
    }
    j += "],\n";
    j += "  \"ancilla_convergence_sum_q32_32\": " + std::to_string((long long)conv_sum) + "\n";
    j += "}\n";

    write_text(out_path, j);
    return 0;
}
