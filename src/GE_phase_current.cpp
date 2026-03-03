#include "GE_phase_current.hpp"
#include <cstring>
#include <algorithm>

namespace genesis {

static inline uint16_t q15_mul(uint16_t a, uint16_t b) {
    return (uint16_t)(((uint32_t)a * (uint32_t)b) >> 15);
}

uint32_t EwPhaseCurrent::det_mix_u32(uint32_t x) {
    // Deterministic avalanche mixer (not crypto; just diffusion).
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

uint16_t EwPhaseCurrent::clamp_q15(int32_t v) {
    if (v < 0) return 0;
    if (v > 32767) return 32767;
    return (uint16_t)v;
}

EwPhaseCurrent::EwPhaseCurrent() { reset(); }

void EwPhaseCurrent::reset() {
    std::memset(regions_, 0, sizeof(regions_));
    for (uint32_t i = 0; i < REGION_CAP; ++i) {
        regions_[i].leak_q15 = (uint16_t)(32767 / 64); // ~1.56% per tick default
        regions_[i].sat_q15  = 32767;
        regions_[i].phase_u16 = 0;
    }
}

void EwPhaseCurrent::ring_push_(RegionState& r, const EwActivationFootprint& fp) {
    const uint16_t slot = (uint16_t)(r.ring_head_u16 & 7u);
    for (uint32_t i = 0; i < 8; ++i) {
        r.ring_bins_u16[slot * 8 + i] = fp.top_bins_u16[i];
        r.ring_amp_q15[slot * 8 + i] = fp.bin_amp_q15[i];
    }
    r.ring_tick_u64[slot] = fp.tick_u64;
    r.ring_head_u16 = (uint16_t)((r.ring_head_u16 + 1u) & 7u);
    if (r.ring_count_u16 < 8u) r.ring_count_u16++;
}

uint16_t EwPhaseCurrent::resonance_overlap_(const RegionState& r, const EwActivationFootprint& fp, uint64_t max_dt) const {
    // Compare against last footprints within a dt window.
    uint16_t best = 0;
    const uint16_t n = r.ring_count_u16;
    for (uint16_t j = 0; j < n; ++j) {
        const uint16_t slot = (uint16_t)((r.ring_head_u16 + 8u - 1u - j) & 7u);
        const uint64_t t = r.ring_tick_u64[slot];
        if (fp.tick_u64 >= t && (fp.tick_u64 - t) > max_dt) break;

        uint16_t overlap = 0;
        for (uint32_t a = 0; a < 8; ++a) {
            const uint16_t ba = fp.top_bins_u16[a];
            if (ba == 0) continue;
            for (uint32_t b = 0; b < 8; ++b) {
                const uint16_t bb = r.ring_bins_u16[slot * 8 + b];
                if (bb == ba) { overlap++; break; }
            }
        }
        if (overlap > best) best = overlap;
    }
    return best;
}

void EwPhaseCurrent::inject_impulse_(RegionState& r, uint16_t impulse_q15, uint16_t phase_hint_u16, uint64_t tick_u64) {
    // Saturating add with clamp.
    const uint16_t sat = (r.sat_q15 == 0) ? 32767 : r.sat_q15;
    const uint32_t a0 = r.amp_q15;
    const uint32_t a1 = a0 + impulse_q15;
    r.amp_q15 = (uint16_t)((a1 > sat) ? sat : a1);

    // Charge accumulates slower (readiness).
    const uint16_t dq = (uint16_t)(impulse_q15 >> 2);
    const uint32_t q1 = (uint32_t)r.charge_q15 + dq;
    r.charge_q15 = (uint16_t)((q1 > 32767u) ? 32767u : q1);

    // Phase hint only nudges.
    r.phase_u16 = (uint16_t)((r.phase_u16 + phase_hint_u16) & 0xFFFFu);
    r.last_tick_u64 = tick_u64;
}

void EwPhaseCurrent::on_activation(const EwActivationFootprint& fp) {
    RegionState& r = regions_[det_region_index(fp.region_key_u32)];

    // Leakage to current tick for determinism: decay proportional to dt.
    if (r.last_tick_u64 != 0 && fp.tick_u64 > r.last_tick_u64) {
        const uint64_t dt = fp.tick_u64 - r.last_tick_u64;
        // Apply leak dt times, but bounded.
        const uint64_t steps = (dt > 32u) ? 32u : dt;
        for (uint64_t i = 0; i < steps; ++i) {
            const uint16_t leak = r.leak_q15;
            const uint16_t dec = q15_mul(r.amp_q15, leak);
            r.amp_q15 = (dec > r.amp_q15) ? 0u : (uint16_t)(r.amp_q15 - dec);
        }
    }

    // Resonance overlap with recent footprints.
    const uint16_t overlap = resonance_overlap_(r, fp, 24u);
    ring_push_(r, fp);

    // Base impulse from summed bin amps.
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 8; ++i) sum += fp.bin_amp_q15[i];
    if (sum > 32767u) sum = 32767u;

    // If overlap >= 3, treat as a ping. Multiply impulse by overlap/8.
    uint16_t impulse = (uint16_t)sum;
    if (overlap >= 3u) {
        impulse = (uint16_t)((sum * (uint32_t)overlap) / 8u);
        // phase hint encodes cycle step: crawl->experiment->eval->store
        const uint16_t phase_hint = (uint16_t)(overlap << 12);
        inject_impulse_(r, impulse, phase_hint, fp.tick_u64);
    } else {
        // Low overlap still contributes a small trickle.
        inject_impulse_(r, (uint16_t)(sum >> 5), 0u, fp.tick_u64);
    }
}

uint32_t EwPhaseCurrent::top_regions(uint32_t max_k,
                                     uint32_t* out_region_key_u32,
                                     uint16_t* out_amp_q15) const {
    if (max_k == 0) return 0;
    if (max_k > 32) max_k = 32;

    // Selection by partial insertion (deterministic).
    uint32_t k = 0;
    for (uint32_t i = 0; i < REGION_CAP; ++i) {
        const uint16_t a = regions_[i].amp_q15;
        if (a == 0) continue;
        // insert into top-k arrays
        uint32_t pos = k;
        if (k < max_k) {
            out_region_key_u32[k] = i;
            out_amp_q15[k] = a;
            k++;
            pos = k - 1;
        } else if (a <= out_amp_q15[max_k - 1]) {
            continue;
        } else {
            out_region_key_u32[max_k - 1] = i;
            out_amp_q15[max_k - 1] = a;
            pos = max_k - 1;
        }
        // bubble up
        while (pos > 0 && out_amp_q15[pos] > out_amp_q15[pos - 1]) {
            std::swap(out_amp_q15[pos], out_amp_q15[pos - 1]);
            std::swap(out_region_key_u32[pos], out_region_key_u32[pos - 1]);
            pos--;
        }
    }
    return k;
}

void EwPhaseCurrent::discharge(uint32_t region_key_u32, uint16_t amount_q15) {
    RegionState& r = regions_[det_region_index(region_key_u32)];
    r.amp_q15 = (amount_q15 >= r.amp_q15) ? 0u : (uint16_t)(r.amp_q15 - amount_q15);
    r.charge_q15 = (amount_q15 >= r.charge_q15) ? 0u : (uint16_t)(r.charge_q15 - amount_q15);
}

EwActivationFootprint EwPhaseCurrent::footprint_from_text(uint64_t tick_u64, uint64_t artifact_id_u64) {
    EwActivationFootprint fp{};
    fp.tick_u64 = tick_u64;
    fp.source_kind_u8 = 1;
    // region key: mixed from artifact_id halves
    const uint32_t lo = (uint32_t)(artifact_id_u64 & 0xFFFFFFFFu);
    const uint32_t hi = (uint32_t)(artifact_id_u64 >> 32);
    fp.region_key_u32 = det_mix_u32(lo ^ (hi * 0x9E3779B9u));
    // bins: derive 8 bins from mixed words (non-zero)
    uint32_t x = fp.region_key_u32 ^ 0xA5A5A5A5u;
    for (uint32_t i = 0; i < 8; ++i) {
        x = det_mix_u32(x + (uint32_t)i * 0x27D4EB2Du);
        fp.top_bins_u16[i] = (uint16_t)((x % 4095u) + 1u);
        fp.bin_amp_q15[i] = (uint16_t)(32767u / 8u);
    }
    return fp;
}

EwActivationFootprint EwPhaseCurrent::footprint_from_metric(uint64_t tick_u64, uint32_t metric_kind_u32) {
    EwActivationFootprint fp{};
    fp.tick_u64 = tick_u64;
    fp.source_kind_u8 = 4;
    fp.region_key_u32 = det_mix_u32(metric_kind_u32 ^ 0x13579BDFu);
    uint32_t x = fp.region_key_u32;
    for (uint32_t i = 0; i < 8; ++i) {
        x = det_mix_u32(x + 0x9E3779B9u);
        fp.top_bins_u16[i] = (uint16_t)((x % 2047u) + 1u);
        fp.bin_amp_q15[i] = (uint16_t)(32767u / 16u);
    }
    fp.track_id_u32 = metric_kind_u32;
    return fp;
}

} // namespace genesis
