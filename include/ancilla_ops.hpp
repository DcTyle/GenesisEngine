#pragma once

#include <cstdint>
#include "fixed_point.hpp"
#include "ancilla_particle.hpp"

// Canonical quantization operator (Equations A.18).
// quantize_q32_32(x, quantum) = round_half_even(x / quantum) * quantum
int64_t quantize_q32_32_round_half_even(int64_t x_q32_32, int64_t quantum_q32_32);

// Phase -> current projection (Equations A.18).
// delta_I = k_phase_current * delta_phi
int64_t phase_to_current_mA_q32_32(int64_t k_phase_current_q32_32, int64_t delta_phi_q32_32);

// Universal operator execution template (Equations A.18).
// Applies a proposed phi_prime under dispatcher headroom constraints.
// Returns true if committed, false if refused.
bool ancilla_apply_phi_prime(
    ancilla_particle* a,
    int64_t* io_phi_q32_32,
    int64_t phi_prime_q32_32,
    int64_t phase_quantum_q32_32,
    int64_t k_phase_current_q32_32,
    int64_t remaining_tension_headroom_mA_q32_32,
    int64_t gradient_headroom_mA_q32_32);
