#pragma once

#include <cstdint>

#include "ew_id9.hpp"

// Canonical ancilla particle (Equations A.18).
// Ancilla particles are the only mutable runtime state holders.
// All fields are fixed-point or packed integers.
struct ancilla_particle {
    int64_t current_mA_q32_32;
    int64_t delta_I_mA_q32_32;
    int64_t delta_I_prev_mA_q32_32;
    uint64_t phase_offset_u64;
    int64_t convergence_metric_q32_32;

    // -----------------------------------------------------------------
    // Substrate microprocessor trace (Blueprint 3.x / Equations ΩA intent)
    // -----------------------------------------------------------------
    // Every non-trivial arithmetic dispatch SHOULD be accompanied by a
    // carrier-wave collapse derived from its operands. The carrier id is
    // used for inspection/validation only; it does not affect semantics.
    uint64_t last_carrier_id_u64 = 0;

    // Deterministic counter of ALU-style micro-ops executed for this
    // ancilla in the current lifetime.
    uint32_t microop_count_u32 = 0;

    // -----------------------------------------------------------------
    // Harmonic constraint surface (Directive 115)
    // -----------------------------------------------------------------
    // Each tick is represented as a substrate operation artifact encoded as
    // an inertial frequency. Values carried through the substrate may also
    // be tagged and encoded as inertial-frequency ids.
    EigenWare::EwId9 tick_carrier_id9 = {};
    uint32_t last_artifact_id_u32 = 0;
    uint32_t last_value_tag_u32 = 0;
    uint32_t harmonic_violation_u32 = 0;
    EigenWare::EwId9 last_artifact_id9 = {};
    // Back-compat single-slot view (slot 0 mirrors this).
    EigenWare::EwId9 last_value_id9 = {};

    // Multi-value harmonic surface. Slot 0 is always the key phase theta.
    // Additional slots may carry derived channels (curvature, doppler, etc.).
    uint32_t value_slot_count_u32 = 0;
    uint32_t value_tag_slots_u32[4] = {0, 0, 0, 0};
    EigenWare::EwId9 value_id9_slots[4] = {};
// -----------------------------------------------------------------
// Continuous environment + chemistry fields (Volume 1 Update 160)
// -----------------------------------------------------------------
// Environment fields are continuously updated each tick. These are
// substrate-resident invariants; rendering consumes only projections.
int64_t env_temp_q32_32 = 0;        // Local temperature proxy (Q32.32)
int64_t env_oxygen_q32_32 = 0;      // Local oxidizer proxy (Q32.32, 1.0 = nominal)
int64_t oxidation_q32_32 = 0;       // Surface oxidation level (Q32.32, [0,1])
int64_t reaction_rate_q32_32 = 0;   // Last computed reaction rate (Q32.32)
};
