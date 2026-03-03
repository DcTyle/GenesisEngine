#pragma once
#include <cstdint>

namespace genesis {

struct EwActivationFootprint {
    uint32_t region_key_u32 = 0;
    uint16_t top_bins_u16[8] = {};
    uint16_t bin_amp_q15[8] = {};
    uint8_t  source_kind_u8 = 0; // 1=text,2=crawl,3=experiment,4=metric
    uint32_t track_id_u32 = 0;
    uint64_t tick_u64 = 0;
};

// Fixed-size current field over quantized 9D regions.
// This is the deterministic "phase-amplitude current" rail that accumulates,
// leaks, saturates, and discharges into bounded actions.
class EwPhaseCurrent {
public:
    static constexpr uint32_t REGION_CAP = 4096;
    struct RegionState {
        uint16_t amp_q15 = 0;
        uint16_t charge_q15 = 0;
        uint16_t leak_q15 = 0;
        uint16_t sat_q15 = 0;
        uint16_t phase_u16 = 0;
        uint64_t last_tick_u64 = 0;

        // Recent footprint ring for resonance detection (bins only).
        uint16_t ring_bins_u16[8 * 8] = {}; // 8 footprints * 8 bins
        uint16_t ring_amp_q15[8 * 8] = {};
        uint16_t ring_head_u16 = 0;
        uint16_t ring_count_u16 = 0;
        uint64_t ring_tick_u64[8] = {};
    };

    EwPhaseCurrent();

    void reset();

    // Feed a new activation footprint.
    void on_activation(const EwActivationFootprint& fp);

    // Tick update: apply leakage and compute top regions (bounded).
    // Returns number of regions written to out arrays.
    uint32_t top_regions(uint32_t max_k,
                         uint32_t* out_region_key_u32,
                         uint16_t* out_amp_q15) const;

    // Discharge a region by a requested amount (q15), saturating at zero.
    void discharge(uint32_t region_key_u32, uint16_t amount_q15);

    // Utility helpers to build footprints deterministically from scalar artifacts.
    static EwActivationFootprint footprint_from_text(uint64_t tick_u64, uint64_t artifact_id_u64);
    static EwActivationFootprint footprint_from_metric(uint64_t tick_u64, uint32_t metric_kind_u32);

    const RegionState& region(uint32_t region_key_u32) const { return regions_[region_key_u32 % REGION_CAP]; }

private:
    RegionState regions_[REGION_CAP];

    static inline uint32_t det_region_index(uint32_t region_key_u32) { return region_key_u32 % REGION_CAP; }
    static uint32_t det_mix_u32(uint32_t x);
    static uint16_t clamp_q15(int32_t v);

    void inject_impulse_(RegionState& r, uint16_t impulse_q15, uint16_t phase_hint_u16, uint64_t tick_u64);
    void ring_push_(RegionState& r, const EwActivationFootprint& fp);
    uint16_t resonance_overlap_(const RegionState& r, const EwActivationFootprint& fp, uint64_t max_dt) const;
};

} // namespace genesis
