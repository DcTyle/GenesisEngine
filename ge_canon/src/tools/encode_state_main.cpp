#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

#include "GE_runtime.hpp"

static void write_all(const std::string& path, const void* p, size_t n) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) std::exit(2);
    if (n) std::fwrite(p, 1, n, f);
    std::fclose(f);
}

static void write_text(const std::string& path, const std::string& s) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) std::exit(2);
    std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
}

static void coord_sig9_u64x9(const SubstrateMicroprocessor& sim, uint64_t out9[9]) {
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

struct EwPackHeader {
    char magic[8];
    uint32_t anchor_count;
    uint64_t canonical_tick;
    uint64_t coord_sig9[9];
};

struct EwPackAnchorRow {
    uint32_t id;
    uint32_t pad0;
    uint64_t object_id_u64;
    uint64_t object_phase_seed_u64;
    uint16_t object_influence_mask9;
    uint16_t pad1;
    int64_t object_theta_scale_turns_q32_32;
    int64_t theta_q;
    int64_t chi_q;
    int64_t m_q;
    int64_t tau_turns_q;
    int64_t curvature_q;
    int64_t doppler_q;
    int64_t paf_turns_q;
    // ancilla
    int64_t current_mA_q32_32;
    int64_t delta_I_mA_q32_32;
    int64_t delta_I_prev_mA_q32_32;
    uint64_t phase_offset_u64;
    int64_t convergence_metric_q32_32;
};

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string out_bin = argv[1];
    const std::string out_meta = argv[2];

    SubstrateMicroprocessor sim(128);
    sim.projection_seed = 99;
    sim.configure_cosmic_expansion(1, 1);
    sim.inject_text_utf8("encode_state");
    for (int k = 0; k < 64; ++k) sim.tick();

    EwPackHeader hdr;
    hdr.magic[0] = 'E'; hdr.magic[1] = 'W'; hdr.magic[2] = 'P'; hdr.magic[3] = 'A';
    hdr.magic[4] = 'C'; hdr.magic[5] = 'K'; hdr.magic[6] = ' '; hdr.magic[7] = ' ';
    hdr.anchor_count = (uint32_t)sim.anchors.size();
    hdr.canonical_tick = sim.canonical_tick;
    coord_sig9_u64x9(sim, hdr.coord_sig9);

    std::string blob;
    blob.resize(sizeof(EwPackHeader) + sizeof(EwPackAnchorRow) * sim.anchors.size());
    std::memcpy(&blob[0], &hdr, sizeof(EwPackHeader));

    for (size_t i = 0; i < sim.anchors.size(); ++i) {
        EwPackAnchorRow row{};
        const Anchor& a = sim.anchors[i];
        const ancilla_particle& an = sim.ancilla[i];

        row.id = a.id;
        row.object_id_u64 = a.object_id_u64;
        row.object_phase_seed_u64 = a.object_phase_seed_u64;
        row.object_influence_mask9 = a.object_influence_mask9;
        row.object_theta_scale_turns_q32_32 = a.object_theta_scale_turns_q32_32;
        row.theta_q = a.theta_q;
        row.chi_q = a.chi_q;
        row.m_q = a.m_q;
        row.tau_turns_q = a.tau_turns_q;
        row.curvature_q = a.curvature_q;
        row.doppler_q = a.doppler_q;
        row.paf_turns_q = a.paf_turns_q;
        row.current_mA_q32_32 = an.current_mA_q32_32;
        row.delta_I_mA_q32_32 = an.delta_I_mA_q32_32;
        row.delta_I_prev_mA_q32_32 = an.delta_I_prev_mA_q32_32;
        row.phase_offset_u64 = an.phase_offset_u64;
        row.convergence_metric_q32_32 = an.convergence_metric_q32_32;

        std::memcpy(&blob[sizeof(EwPackHeader) + i * sizeof(EwPackAnchorRow)], &row, sizeof(EwPackAnchorRow));
    }

    write_all(out_bin, blob.data(), blob.size());

    std::string m;
    m += "artifact = ew_state\n";
    m += "format = EWPACK\n";
    m += "anchor_count_u32 = " + std::to_string((unsigned long long)hdr.anchor_count) + "\n";
    m += "canonical_tick_u64 = " + std::to_string((unsigned long long)hdr.canonical_tick) + "\n";
    m += "coord_sig9_u64x9 = ";
    for (int i = 0; i < 9; ++i) {
        m += std::to_string((unsigned long long)hdr.coord_sig9[i]);
        if (i != 8) m += ",";
    }
    m += "\n";
    write_text(out_meta, m);
    return 0;
}
