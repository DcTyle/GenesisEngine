#pragma once

#include <cstdint>

#include "ancilla_particle.hpp"
#include "ew_id9.hpp"

// -----------------------------------------------------------------------------
// Substrate Harmonic Constraint Encoding
// -----------------------------------------------------------------------------
// Purpose:
//  - Encode operations (artifacts) and carried values as deterministic
//    inertial-frequency ids that are constrained by a per-tick carrier id.
//  - Provide an enforcement check that can route mismatches into the
//    non-projecting (sink/dark) path via accept_state.
//
// No hashing/crypto: IDs are 9D vector encodings (EwId9).

// Canonical artifact ids (non-versioned). These are stable operator labels.
static const uint32_t EW_ARTIFACT_ID_ANCILLA_TICK_DISPATCH = 0xE001u;

// Canonical value tags (non-versioned).
// 'V001' in ASCII bytes.
static const uint32_t EW_VALUE_TAG_PHASE_THETA_Q32_32 = 0x56303031u;
// 'V006' curvature (TURN_SCALE -> Q32.32 turns)
static const uint32_t EW_VALUE_TAG_CURVATURE_Q32_32   = 0x56303036u;
// 'V007' doppler (TURN_SCALE -> Q32.32 turns)
static const uint32_t EW_VALUE_TAG_DOPPLER_Q32_32     = 0x56303037u;

// Deterministic inertial-frequency id for an executed artifact/op.
EigenWare::EwId9 ew_id9_for_artifact(uint32_t anchor_id,
                                           const int64_t coord9[9],
                                           uint32_t artifact_id_u32,
                                           const EigenWare::EwId9& tick_carrier_id9);

// Deterministic inertial-frequency id for a carried scalar value.
EigenWare::EwId9 ew_id9_for_value(const EigenWare::EwId9& tick_carrier_id9,
                                        int64_t value_q32_32,
                                        uint32_t value_tag_u32);

// Harmonic constraint enforcement.
// Returns true if the ancilla encodings match what the substrate would
// deterministically compute from the supplied inputs.
bool ew_harmonic_constraints_ok(ancilla_particle* a,
                               uint32_t anchor_id,
                               const int64_t coord9[9],
                               uint32_t expected_artifact_id_u32,
                               int64_t key_value_q32_32,
                               uint32_t value_tag_u32);

// Multi-value harmonic constraint enforcement.
// Slot 0 must match (key_value, key_tag). Optional slots are verified if present.
bool ew_harmonic_constraints_ok_multi(ancilla_particle* a,
                                     uint32_t anchor_id,
                                     const int64_t coord9[9],
                                     uint32_t expected_artifact_id_u32,
                                     int64_t key_value_q32_32,
                                     uint32_t key_tag_u32,
                                     const int64_t opt_values_q32_32[3],
                                     const uint32_t opt_tags_u32[3]);
