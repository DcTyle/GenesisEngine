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
    SubstrateManager substrate(8);
    substrate.projection_seed = 12345;    // Configure boundary expansion from baseline references.
    // Genesis tick cadence: 360 Hz (deterministic simulation micro-tick).
    const double dt_s = 1.0 / 360.0;
    const int64_t dt_q32_32 = (int64_t)(dt_s * 4294967296.0);
    const int64_t h0_ref_q32_32 = hubble_h0_ref_default_q32_32();
    substrate.configure_cosmic_expansion(h0_ref_q32_32, dt_q32_32);
    Sandbox sandbox;

    for (int i = 0; i < 10; ++i) {
        substrate.tick();
        if (!substrate.check_invariants()) {
            std::cerr << "Invariant failure at tick " << substrate.canonical_tick << "\n";
            return 2;
        }
        sandbox.consume(substrate.outbound);
        substrate.outbound.clear();
    }
    return 0;
}
