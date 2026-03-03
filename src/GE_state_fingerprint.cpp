#include "GE_state_fingerprint.hpp"

#include "GE_runtime.hpp"
#include <cstdio>
#include <cstdlib>

static inline uint64_t mix_u64(uint64_t x) {
    // SplitMix64-style mixing (deterministic, fast). This is for fingerprints,
    // not security.
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x = x ^ (x >> 31);
    return x;
}

static inline void fold(uint64_t& h, uint64_t v) {
    h ^= mix_u64(v + 0xD6E8FEB86659FD93ull);
    h = (h << 17) | (h >> (64 - 17));
    h *= 0x9E3779B185EBCA87ull;
}

uint64_t ge_compute_state_fingerprint_9d(const SubstrateManager* sm) {
    if (!sm) return 0;
    uint64_t h = 0xC3A5C85C97CB3127ull;

    // Canonical tick.
    fold(h, sm->canonical_tick);

    // Anchor count and a few key observables.
    fold(h, (uint64_t)sm->anchors.size());
    const size_t n = sm->anchors.size();
    // Sample up to 32 anchors deterministically (first, middle, last).
    const size_t sample_n = (n < 32) ? n : 32;
    for (size_t i = 0; i < sample_n; ++i) {
        const size_t idx = (n <= sample_n) ? i : (i * (n - 1) / (sample_n - 1));
        const Anchor& a = sm->anchors[idx];
        fold(h, ((uint64_t)a.id) | ((uint64_t)a.kind_u32 << 32));
        fold(h, (uint64_t)a.object_id_u64);
        fold(h, (uint64_t)a.theta_q);
        fold(h, (uint64_t)a.tau_turns_q);
        fold(h, (uint64_t)a.world_flux_grad_mean_q15);
        fold(h, (uint64_t)a.harmonics_mean_q15);
        if (a.kind_u32 == EW_ANCHOR_KIND_CAMERA) {
            fold(h, (uint64_t)a.camera_state.focus_distance_m_q32_32);
            fold(h, (uint64_t)a.camera_state.focus_mode_u8);
        }
        if (a.kind_u32 == EW_ANCHOR_KIND_OBJECT) {
            fold(h, (uint64_t)(uint32_t)a.object_state.pos_q16_16[0]);
            fold(h, (uint64_t)(uint32_t)a.object_state.pos_q16_16[1]);
            fold(h, (uint64_t)(uint32_t)a.object_state.pos_q16_16[2]);
        }
        if (a.kind_u32 == EW_ANCHOR_KIND_PLANET) {
            fold(h, (uint64_t)(uint32_t)a.planet_state.pos_q16_16[0]);
            fold(h, (uint64_t)(uint32_t)a.planet_state.pos_q16_16[1]);
            fold(h, (uint64_t)(uint32_t)a.planet_state.pos_q16_16[2]);
            fold(h, (uint64_t)a.planet_state.voxel_resonance_q15);
        }
    }

    // Control inbox size (control surface activity).
    fold(h, (uint64_t)sm->control_inbox_count_u32);

    // Render packet ticks.
    fold(h, sm->render_camera_packet_tick_u64);
    fold(h, sm->render_assist_packet_tick_u64);
    fold(h, sm->render_object_packets_tick_u64);

    return h;
}

bool ge_load_fingerprint_reference(const char* path_utf8, std::vector<uint64_t>& out) {
    out.clear();
    if (!path_utf8 || !path_utf8[0]) return false;
    FILE* f = std::fopen(path_utf8, "rb");
    if (!f) return false;

    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        char* endp = nullptr;
        // Accept hex with 0x prefix or decimal.
        uint64_t v = std::strtoull(line, &endp, 0);
        if (endp == line) continue;
        out.push_back(v);
    }
    std::fclose(f);
    return !out.empty();
}
