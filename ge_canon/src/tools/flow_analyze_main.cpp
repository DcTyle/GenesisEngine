#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "statevector_serialization.hpp"
#include "ew_cli_args.hpp"

static void print_usage() {
    std::printf("ew_flow_analyze --input <file> [--help]\n");
    std::printf("Reads a serialized statevector (hexfloat lines) and emits deterministic stats.\n");
}

static bool read_all(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string s;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0, std::ios::beg);
    s.resize((size_t)n);
    if (!s.empty()) f.read(&s[0], n);
    out.swap(s);
    return true;
}

int main(int argc, char** argv) {
    ew::CliArgsKV args;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, args)) {
        std::printf("ERROR: malformed args\n");
        return 2;
    }
    std::string input;
    (void)ew::ew_cli_get_str(args, "input", input);
    bool help = false;
    (void)ew::ew_cli_get_bool(args, "help", help);
    if (help) { print_usage(); return 0; }

    if (input.empty()) {
        print_usage();
        return 2;
    }

    std::string blob;
    if (!read_all(input, blob)) {
        std::printf("ERROR: could not read input file\n");
        return 3;
    }

    const auto v = deserialize_statevector(blob);
    if (v.empty()) {
        std::printf("ERROR: could not parse statevector\n");
        return 4;
    }

    long double l2 = 0.0L;
    long double max_mag = 0.0L;
    size_t max_i = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        const long double re = (long double)v[i].real();
        const long double im = (long double)v[i].imag();
        const long double m2 = re*re + im*im;
        l2 += m2;
        const long double m = std::sqrt((double)m2);
        if (m > max_mag) { max_mag = m; max_i = i; }
    }

    const long double norm = std::sqrt((double)l2);
    std::printf("n=%zu\n", v.size());
    std::printf("l2_norm=%0.17g\n", (double)norm);
    std::printf("max_mag=%0.17g\n", (double)max_mag);
    std::printf("max_index=%zu\n", max_i);
    return 0;
}
