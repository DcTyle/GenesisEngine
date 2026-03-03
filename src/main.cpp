#include <iostream>
#include "GE_runtime.hpp"
#include "spec_aux_ops.hpp"

class Sandbox {
public:
    void consume(const std::vector<Pulse>& pulses) {
        for (size_t i = 0; i < pulses.size(); ++i) {
            const Pulse& p = pulses[i];
            std::cout << "Sandbox pulse | Anchor: " << p.anchor_id
                      << " f_code: " << p.f_code
                      << " a_code: " << p.a_code
                      << " v_code: " << p.v_code
                      << " i_code: " << p.i_code
                      << " tick: " << p.tick << "\n";
        }
    }
};

int main() {
    SubstrateMicroprocessor substrate_microprocessor(8);
    substrate_microprocessor.projection_seed = 12345;    // Configure boundary expansion from baseline references.
    // Genesis tick cadence: 360 Hz (deterministic simulation micro-tick).
    const double dt_s = 1.0 / 360.0;
    const int64_t dt_q32_32 = (int64_t)(dt_s * 4294967296.0);
    const int64_t h0_ref_q32_32 = hubble_h0_ref_default_q32_32();
    substrate_microprocessor.configure_cosmic_expansion(h0_ref_q32_32, dt_q32_32);
    Sandbox sandbox;

    for (int i = 0; i < 10; ++i) {
        substrate_microprocessor.tick();
        if (!substrate_microprocessor.check_invariants()) {
            std::cerr << "Invariant failure at tick " << substrate_microprocessor.canonical_tick << "\n";
            return 2;
        }
        sandbox.consume(substrate_microprocessor.outbound);
        substrate_microprocessor.outbound.clear();
    }
    return 0;
}
