#include "ancilla_ops.hpp"

static inline int64_t iabs64(int64_t x) { return (x < 0) ? -x : x; }

int64_t quantize_q32_32_round_half_even(int64_t x_q32_32, int64_t quantum_q32_32) {
    if (quantum_q32_32 <= 0) return x_q32_32;
    // Compute q = x / quantum with remainder r.
    const int64_t q = x_q32_32 / quantum_q32_32;
    const int64_t r = x_q32_32 - q * quantum_q32_32;

    // Half-even rounding: if |r| < |quantum|/2 -> q
    // if |r| > |quantum|/2 -> q +/- 1
    // if |r| == |quantum|/2 -> q if q even else q +/- 1
    const int64_t half = quantum_q32_32 / 2;
    const int64_t ar = iabs64(r);
    const int64_t ahalf = iabs64(half);

    int64_t q_rounded = q;
    if (ar < ahalf) {
        // keep q
    } else if (ar > ahalf) {
        q_rounded = (x_q32_32 >= 0) ? (q + 1) : (q - 1);
    } else {
        // exactly half
        const bool q_even = ((q & 1LL) == 0);
        if (!q_even) {
            q_rounded = (x_q32_32 >= 0) ? (q + 1) : (q - 1);
        }
    }
    return q_rounded * quantum_q32_32;
}

int64_t phase_to_current_mA_q32_32(int64_t k_phase_current_q32_32, int64_t delta_phi_q32_32) {
    return mul_q32_32(k_phase_current_q32_32, delta_phi_q32_32);
}

bool ancilla_apply_phi_prime(
    ancilla_particle* a,
    int64_t* io_phi_q32_32,
    int64_t phi_prime_q32_32,
    int64_t phase_quantum_q32_32,
    int64_t k_phase_current_q32_32,
    int64_t remaining_tension_headroom_mA_q32_32,
    int64_t gradient_headroom_mA_q32_32) {
    const int64_t phi_q32_32 = *io_phi_q32_32;
    const int64_t delta_phi_q32_32 = phi_prime_q32_32 - phi_q32_32;
    const int64_t delta_phi_quant_q32_32 = quantize_q32_32_round_half_even(delta_phi_q32_32, phase_quantum_q32_32);
    const int64_t delta_I_mA_q32_32 = phase_to_current_mA_q32_32(k_phase_current_q32_32, delta_phi_quant_q32_32);

    a->delta_I_mA_q32_32 = delta_I_mA_q32_32;

    const int64_t grad = delta_I_mA_q32_32 - a->delta_I_prev_mA_q32_32;

    const bool ok_tension = (iabs64(delta_I_mA_q32_32) <= remaining_tension_headroom_mA_q32_32);
    const bool ok_grad = (iabs64(grad) <= gradient_headroom_mA_q32_32);

    if (ok_tension && ok_grad) {
        *io_phi_q32_32 = phi_q32_32 + delta_phi_quant_q32_32;
        a->delta_I_prev_mA_q32_32 = delta_I_mA_q32_32;
        a->current_mA_q32_32 += delta_I_mA_q32_32;
        a->convergence_metric_q32_32 = iabs64(grad);
        return true;
    }

    // Refused: hold phi; convergence metric grows.
    a->convergence_metric_q32_32 += (1LL << 16);
    return false;
}
