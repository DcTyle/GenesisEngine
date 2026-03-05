#include "substrate_harmonics.hpp"

#include <cstring>

using EigenWare::EwId9;

static inline uint32_t ew_clamp_i64_to_u32(int64_t v) {
    if (v < (int64_t)0) return 0u;
    if (v > (int64_t)0xFFFFFFFFLL) return 0xFFFFFFFFu;
    return (uint32_t)v;
}

// Vectorized (not hashed) id for an executed artifact/op.
EwId9 ew_id9_for_artifact(uint32_t anchor_id,
                          const int64_t coord9[9],
                          uint32_t artifact_id_u32,
                          const EwId9& tick_carrier_id9) {
    EwId9 out;
    out.u32[0] = anchor_id;
    out.u32[1] = artifact_id_u32;
    // Fold tick carrier lanes into slots 2..3 (direct lanes, not mixed).
    out.u32[2] = tick_carrier_id9.u32[0];
    out.u32[3] = tick_carrier_id9.u32[1];
    // Coordinate projection into remaining lanes.
    out.u32[4] = ew_clamp_i64_to_u32(coord9[0]);
    out.u32[5] = ew_clamp_i64_to_u32(coord9[1]);
    out.u32[6] = ew_clamp_i64_to_u32(coord9[2]);
    out.u32[7] = ew_clamp_i64_to_u32(coord9[3]);
    out.u32[8] = ew_clamp_i64_to_u32(coord9[4]);
    return out;
}

// Vectorized (not hashed) id for a carried scalar value.
EwId9 ew_id9_for_value(const EwId9& tick_carrier_id9,
                       int64_t value_q32_32,
                       uint32_t value_tag_u32) {
    EwId9 out;
    out.u32[0] = value_tag_u32;
    out.u32[1] = tick_carrier_id9.u32[0];
    out.u32[2] = tick_carrier_id9.u32[1];
    out.u32[3] = (uint32_t)(value_q32_32 & 0xFFFFFFFFLL);
    out.u32[4] = (uint32_t)((uint64_t)value_q32_32 >> 32);
    // Leave remaining lanes 0 for extensibility.
    out.u32[8] = 0u;
    return out;
}

bool ew_harmonic_constraints_ok(ancilla_particle* a,
                               uint32_t anchor_id,
                               const int64_t coord9[9],
                               uint32_t expected_artifact_id_u32,
                               int64_t key_value_q32_32,
                               uint32_t value_tag_u32) {
    const EwId9 expect_art = ew_id9_for_artifact(anchor_id, coord9, expected_artifact_id_u32, a->tick_carrier_id9);
    if (a->last_artifact_id9 != expect_art) {
        a->harmonic_violation_u32 |= 1u;
        return false;
    }
    const EwId9 expect_val = ew_id9_for_value(a->tick_carrier_id9, key_value_q32_32, value_tag_u32);
    if (a->last_value_id9 != expect_val) {
        a->harmonic_violation_u32 |= 2u;
        return false;
    }
    return true;
}

bool ew_harmonic_constraints_ok_multi(ancilla_particle* a,
                                     uint32_t anchor_id,
                                     const int64_t coord9[9],
                                     uint32_t expected_artifact_id_u32,
                                     int64_t key_value_q32_32,
                                     uint32_t key_tag_u32,
                                     const int64_t opt_values_q32_32[3],
                                     const uint32_t opt_tags_u32[3]) {
    const EwId9 expect_art = ew_id9_for_artifact(anchor_id, coord9, expected_artifact_id_u32, a->tick_carrier_id9);
    if (a->last_artifact_id9 != expect_art) {
        a->harmonic_violation_u32 |= 1u;
        return false;
    }

    if (a->value_slot_count_u32 == 0u) {
        a->harmonic_violation_u32 |= 4u;
        return false;
    }

    // Slot 0 key
    const EwId9 key_expect = ew_id9_for_value(a->tick_carrier_id9, key_value_q32_32, key_tag_u32);
    if (a->value_tag_slots_u32[0] != key_tag_u32 || a->value_id9_slots[0] != key_expect || a->last_value_id9 != key_expect) {
        a->harmonic_violation_u32 |= 8u;
        return false;
    }

    // Optional slots.
    const uint32_t slots = a->value_slot_count_u32;
    for (uint32_t s = 1u; s < slots && s < 4u; ++s) {
        const uint32_t tag = a->value_tag_slots_u32[s];
        const EwId9 freq = a->value_id9_slots[s];
        bool matched = false;
        for (uint32_t k = 0u; k < 3u; ++k) {
            if (opt_tags_u32[k] == 0u) continue;
            if (tag == opt_tags_u32[k]) {
                const EwId9 expect = ew_id9_for_value(a->tick_carrier_id9, opt_values_q32_32[k], tag);
                if (freq != expect) {
                    a->harmonic_violation_u32 |= 16u;
                    return false;
                }
                matched = true;
                break;
            }
        }
        if (!matched) {
            // Unknown tag present.
            a->harmonic_violation_u32 |= 32u;
            return false;
        }
    }

    return true;
}
