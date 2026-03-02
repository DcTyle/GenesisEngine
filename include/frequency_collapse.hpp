#pragma once

#include <cstdint>
#include <vector>

// Deterministic collapse of a set of encoding-derived component frequencies into a
// single carrier frequency + amplitude suitable for driving a single-wave signal.
//
// All quantities use Q32.32 fixed-point in "turns" domain unless noted.
//
// This lives in the substrate microprocessor layer (core), not in external adapters.

struct EwFreqComponentQ32_32 {
    int64_t f_turns_q32_32 = 0;   // component frequency (turns)
    int64_t a_q32_32 = 0;         // component amplitude weight
    int64_t phi_turns_q32_32 = 0; // component phase (turns)
};

struct EwCarrierWaveQ32_32 {
    int64_t f_carrier_turns_q32_32 = 0;
    int64_t A_carrier_q32_32 = 0;
    int64_t phi_carrier_turns_q32_32 = 0;
    uint32_t component_count_u32 = 0;
};

// Canonical alias used by the spec registry.
using EwCarrierParams = EwCarrierWaveQ32_32;

// Canonical helper: collapse component bins expressed as parallel arrays.
// Inputs:
//  - f_bins_turns_q: component frequency bins in TURN_SCALE units
//  - a_bins_q32_32: component amplitude weights in Q32.32
//  - phi_bins_turns_q: component phases in TURN_SCALE units
// Output carrier parameters are Q32.32 in turns-domain.
EwCarrierParams carrier_params(const std::vector<int64_t>& f_bins_turns_q,
                               const std::vector<int64_t>& a_bins_q32_32,
                               const std::vector<int64_t>& phi_bins_turns_q);

// Canonical helper: produce a deterministic 64-bit phase carrier id for logging
// and stable inspection (deterministic).
uint64_t carrier_phase_u64(int64_t phi_carrier_turns_q32_32);

// Collapse rule:
//  f_carrier = (sum a_i * f_i) / (sum a_i)
//  A = sqrt(sum a_i^2)  (energy-equivalent)
//  phi_carrier = wrap-aware mean over phi_i using minimal phase deltas
//
// Returns false if inputs are empty or invalid.
bool ew_collapse_frequency_components_q32_32(const std::vector<EwFreqComponentQ32_32>& comps,
                                            EwCarrierWaveQ32_32& out);
