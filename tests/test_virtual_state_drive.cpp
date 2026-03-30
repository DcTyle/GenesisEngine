#include "VirtualStateDrive.hpp"

#include <cmath>
#include <complex>
#include <iostream>
#include <string>
#include <vector>

int main() {
    auto fail = [](const std::string& msg) -> int {
        std::cerr << "FAIL: " << msg << "\n";
        return 1;
    };

    genesis::VirtualStateDrive drive;
    if (!drive.put_text("vars/amplitude", "domain=photon\naxis=x", "phase locked amplitude sample")) {
        return fail("put_text failed");
    }
    if (!drive.put_i64("products/f_code_mul_a_code", "domain=fourier\nkind=product", 123456789ll)) {
        return fail("put_i64 failed");
    }

    std::vector<std::complex<double>> statevector = {
        {1.0, 0.0},
        {0.25, -0.5},
        {0.75, 0.125},
    };
    if (!drive.put_statevector("states/psi", "domain=9d\nkind=statevector", statevector)) {
        return fail("put_statevector failed");
    }

    genesis::GeVirtualStateRecord record{};
    if (!drive.get_record("vars/amplitude", record)) {
        return fail("get_record failed");
    }
    if (record.tensor.channels.a_code == 0u) {
        return fail("a_code was not populated");
    }
    if (record.tensor.lattice_edge_hint_u32 < 64u || record.tensor.lattice_edge_hint_u32 > 512u) {
        return fail("lattice edge hint out of range");
    }
    if (record.tensor.dft[1].magnitude_q15 == 0u && record.tensor.dft[2].magnitude_q15 == 0u) {
        return fail("dft magnitudes were not populated");
    }

    int64_t product_i64 = 0;
    if (!drive.get_i64("products/f_code_mul_a_code", product_i64) || product_i64 != 123456789ll) {
        return fail("int64 round trip failed");
    }

    std::vector<std::complex<double>> restored_statevector;
    if (!drive.get_statevector("states/psi", restored_statevector)) {
        return fail("statevector decode failed");
    }
    if (restored_statevector.size() != statevector.size()) {
        return fail("statevector size mismatch");
    }
    for (size_t i = 0; i < statevector.size(); ++i) {
        if (std::abs(restored_statevector[i].real() - statevector[i].real()) > 1e-12 ||
            std::abs(restored_statevector[i].imag() - statevector[i].imag()) > 1e-12) {
            return fail("statevector value mismatch");
        }
    }

    std::vector<uint8_t> blob;
    if (!drive.serialize_binary(blob) || blob.size() < 12u) {
        return fail("serialize_binary failed");
    }

    genesis::VirtualStateDrive restored_drive;
    if (!restored_drive.deserialize_binary(blob.data(), blob.size())) {
        return fail("deserialize_binary failed");
    }
    if (restored_drive.size() != drive.size()) {
        return fail("restored size mismatch");
    }

    std::string restored_text;
    if (!restored_drive.get_text("vars/amplitude", restored_text)) {
        return fail("restored get_text failed");
    }
    if (restored_text != "phase locked amplitude sample") {
        return fail("restored text mismatch");
    }

    std::cout << "PASS: virtual state drive encoded and restored\n";
    return 0;
}