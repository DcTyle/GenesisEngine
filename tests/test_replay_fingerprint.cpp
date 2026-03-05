#include <cstdint>
#include <vector>
#include <iostream>
#include "GE_runtime.hpp"

// Regression test: identical runs produce identical fingerprint streams.
// This complements test_determinism.cpp (bytewise pulse stream) by asserting
// the higher-level engine fingerprint harness stays stable.

static std::vector<uint64_t> run_fp_trace(uint64_t seed, int ticks, int anchor_count)
{
    SubstrateManager s((size_t)anchor_count);
    s.projection_seed = seed;

    std::vector<uint64_t> fp;
    fp.reserve((size_t)ticks);

    for (int t = 0; t < ticks; ++t) {
        s.tick();
        if (!s.check_invariants()) {
            std::cerr << "Invariant failure at tick " << (unsigned long long)s.canonical_tick << "\n";
            std::abort();
        }
        fp.push_back(s.state_fingerprint_u64);
    }
    return fp;
}

int main()
{
    const uint64_t seed = 0xC0FFEEULL;
    const int ticks = 500;
    const int anchors = 64;

    std::vector<uint64_t> a = run_fp_trace(seed, ticks, anchors);
    std::vector<uint64_t> b = run_fp_trace(seed, ticks, anchors);

    if (a != b) {
        size_t idx = 0;
        while (idx < a.size() && a[idx] == b[idx]) idx++;
        std::cerr << "FAIL: fingerprint mismatch at tick " << idx
                  << " a=" << (unsigned long long)a[idx]
                  << " b=" << (unsigned long long)b[idx] << "\n";
        return 1;
    }

    std::cout << "PASS: deterministic fingerprint stream (ticks=" << ticks
              << ", anchors=" << anchors << ")\n";
    return 0;
}
