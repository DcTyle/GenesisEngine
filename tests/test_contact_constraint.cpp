#include "field_lattice_cpu.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

static bool is_finite(float x) {
    return std::isfinite((double)x);
}

int main() {
    // Small deterministic lattice.
    const uint32_t gx = 24, gy = 24, gz = 24;
    EwFieldLatticeCpu lat(gx, gy, gz);

    // Build a deterministic high-density overlap pocket at the center.
    std::vector<uint8_t> dens((size_t)gx * (size_t)gy * (size_t)gz, 0u);
    const int cx = (int)gx / 2;
    const int cy = (int)gy / 2;
    const int cz = (int)gz / 2;
    auto idx3 = [&](int x, int y, int z)->size_t {
        return (size_t)((z * (int)gy + y) * (int)gx + x);
    };
    for (int z = cz - 1; z <= cz + 1; ++z) {
        for (int y = cy - 1; y <= cy + 1; ++y) {
            for (int x = cx - 1; x <= cx + 1; ++x) {
                dens[idx3(x, y, z)] = 255u;
            }
        }
    }
    lat.upload_density_mask_u8(dens.data(), dens.size());

    // Inject deterministic energy in the same pocket to stress the contact term.
    for (int k = 0; k < 6; ++k) {
        lat.inject_text_amplitude_q32_32((int64_t)1 << 32);
        lat.step_one_tick();
    }

    // Run for a bounded number of ticks and ensure we remain finite and stable.
    for (int t = 0; t < 32; ++t) {
        lat.step_one_tick();
    }

    // Validate no NaNs/inf and flux not exploding.
    const auto& E = lat.E_curr();
    const auto& F = lat.flux();
    double sum_abs_E = 0.0;
    double sum_abs_F = 0.0;
    for (size_t i = 0; i < E.size(); ++i) {
        assert(is_finite(E[i]));
        assert(is_finite(F[i]));
        sum_abs_E += std::abs((double)E[i]);
        sum_abs_F += std::abs((double)F[i]);
    }
    // Conservative bounds: we only care about runaway prevention.
    assert(sum_abs_E < 1.0e9);
    assert(sum_abs_F < 1.0e9);
    return 0;
}
