#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

static std::string read_all(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return std::string(); }
    std::string s; s.resize((size_t)n);
    std::fread(&s[0], 1, (size_t)n, f);
    std::fclose(f);
    return s;
}

struct EwPackHeader {
    char magic[8];
    uint32_t layout_u32;// layout identifier (deterministic)
    uint32_t anchor_count;
    uint64_t canonical_tick;
    uint64_t coord_coord9[9];
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
    int64_t current_mA_q32_32;
    int64_t delta_I_mA_q32_32;
    int64_t delta_I_prev_mA_q32_32;
    uint64_t phase_offset_u64;
    int64_t convergence_metric_q32_32;
};

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string in_bin = argv[1];
    const std::string blob = read_all(in_bin);
    if (blob.size() < sizeof(EwPackHeader)) return 3;

    EwPackHeader hdr;
    std::memcpy(&hdr, blob.data(), sizeof(EwPackHeader));
    if (!(hdr.magic[0] == 'E' && hdr.magic[1] == 'W' && hdr.magic[2] == 'P')) return 4;

    const size_t expected = sizeof(EwPackHeader) + (size_t)hdr.anchor_count * sizeof(EwPackAnchorRow);
    if (blob.size() != expected) return 5;

    // Minimal rehydration check: verify the embedded 9D coord-tag matches recompute from rows.
    uint64_t coord9[9];
    for (int i = 0; i < 9; ++i) coord9[i] = 0ULL;

    for (size_t i = 0; i < (size_t)hdr.anchor_count; ++i) {
        EwPackAnchorRow row{};
        std::memcpy(&row, &blob[sizeof(EwPackHeader) + i * sizeof(EwPackAnchorRow)], sizeof(EwPackAnchorRow));
        coord9[0] += (uint64_t)row.id;
        coord9[1] += (uint64_t)row.object_id_u64;
        coord9[2] += (uint64_t)(uint64_t)row.theta_q;
        coord9[3] += (uint64_t)(uint64_t)row.chi_q;
        coord9[4] += (uint64_t)(uint64_t)row.m_q;
        coord9[5] += (uint64_t)(uint64_t)row.tau_turns_q;
        coord9[6] += (uint64_t)(uint64_t)row.curvature_q;
        coord9[7] += (uint64_t)(uint64_t)row.doppler_q;
        coord9[8] += (uint64_t)(uint64_t)row.paf_turns_q;
    }

    for (int i = 0; i < 9; ++i) {
        if (coord9[i] != hdr.coord_coord9[i]) return 6;
    }

    return 0;
}
