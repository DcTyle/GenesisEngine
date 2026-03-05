#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

// Deterministic ASCII-safe packing for complex vectors.
// API surface matches Spec/Blueprint naming.

inline std::string serialize_vector_with_fields(const std::vector<std::complex<double>>& vec) {
    // Uses hexfloat for stable, locale-independent representation.
    std::ostringstream oss;
    oss << std::hexfloat;
    oss << "n=" << vec.size() << "\n";
    for (size_t i = 0; i < vec.size(); ++i) {
        oss << "re=" << vec[i].real() << ";im=" << vec[i].imag() << "\n";
    }
    return oss.str();
}

inline std::vector<std::complex<double>> deserialize_vector_with_fields(const std::string& blob) {
    std::istringstream iss(blob);
    iss >> std::hexfloat;
    std::string line;

    size_t n = 0;
    if (!std::getline(iss, line)) return {};
    if (line.rfind("n=", 0) != 0) return {};
    {
        std::istringstream h(line.substr(2));
        h >> n;
    }

    std::vector<std::complex<double>> out;
    out.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        if (!std::getline(iss, line)) break;
        // Expect: re=<hexfloat>;im=<hexfloat>
        const auto sep = line.find(";im=");
        if (sep == std::string::npos) break;
        if (line.rfind("re=", 0) != 0) break;
        const std::string re_s = line.substr(3, sep - 3);
        const std::string im_s = line.substr(sep + 4);

        double re = 0.0;
        double im = 0.0;
        {
            std::istringstream rs(re_s);
            rs >> std::hexfloat;
            rs >> re;
        }
        {
            std::istringstream is(im_s);
            is >> std::hexfloat;
            is >> im;
        }
        out.emplace_back(re, im);
    }
    return out;
}

inline std::string serialize_statevector(const std::vector<std::complex<double>>& state_vec) {
    return serialize_vector_with_fields(state_vec);
}

inline std::vector<std::complex<double>> deserialize_statevector(const std::string& blob) {
    return deserialize_vector_with_fields(blob);
}
