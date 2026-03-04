\
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
#include <iostream>
#include "GE_runtime.hpp"

static void append_bytes(std::vector<uint8_t>& out, const void* ptr, size_t n) {
    const uint8_t* p = (const uint8_t*)ptr;
    out.insert(out.end(), p, p + n);
}

static std::vector<uint8_t> run_trace(uint64_t seed, int ticks, int anchor_count) {
    SubstrateManager s((size_t)anchor_count);
    s.projection_seed = seed;

    std::vector<uint8_t> bytes;
    bytes.reserve((size_t)ticks * (size_t)anchor_count * sizeof(Pulse));

    for (int t = 0; t < ticks; ++t) {
        s.tick();
        if (!s.check_invariants()) {
            std::cerr << "Invariant failure at tick " << (unsigned long long)s.canonical_tick << "\n";
            std::abort();
        }
        for (size_t i = 0; i < s.outbound.size(); ++i) {
            const Pulse& p = s.outbound[i];
            append_bytes(bytes, &p, sizeof(Pulse));
        }
        s.outbound.clear();
    }
    return bytes;
}

int main() {
    const uint64_t seed = 0xC0FFEEULL;
    const int ticks = 200;
    const int anchors = 32;

    std::vector<uint8_t> a = run_trace(seed, ticks, anchors);
    std::vector<uint8_t> b = run_trace(seed, ticks, anchors);

    if (a.size() != b.size()) {
        std::cerr << "FAIL: size mismatch " << a.size() << " vs " << b.size() << "\n";
        return 1;
    }
    if (std::memcmp(a.data(), b.data(), a.size()) != 0) {
        size_t idx = 0;
        while (idx < a.size() && a[idx] == b[idx]) idx++;
        std::cerr << "FAIL: byte mismatch at index " << idx << "\n";
        return 1;
    }

    std::FILE* fp = std::fopen("pulse_trace.bin", "wb");
    if (fp) {
        std::fwrite(a.data(), 1, a.size(), fp);
        std::fclose(fp);
    }

    std::cout << "PASS: deterministic pulse stream (ticks=" << ticks << ", anchors=" << anchors << ")\n";
    return 0;
}
