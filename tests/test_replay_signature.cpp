#include <cstdint>
#include <vector>
#include <iostream>
#include "GE_runtime.hpp"

// Regression test: identical runs produce identical signature streams.
// This complements test_determinism.cpp (bytewise pulse stream) by asserting
// the higher-level engine signature harness stays stable.

static std::vector<uint64_t> run_signature_trace(uint64_t seed, int ticks, int anchor_count)
{
    SubstrateManager s((size_t)anchor_count);
    s.projection_seed = seed;

    std::vector<uint64_t> signatures;
    signatures.reserve((size_t)ticks);

    for (int t = 0; t < ticks; ++t) {
        s.tick();
        if (!s.check_invariants()) {
            std::cerr << "Invariant failure at tick " << (unsigned long long)s.canonical_tick << "\n";
            std::abort();
        }
        signatures.push_back(s.state_signature_u64);
    }
    return signatures;
}

int main()
{
    const uint64_t seed = 0xC0FFEEULL;
    const int ticks = 500;
    const int anchors = 64;

    std::vector<uint64_t> a = run_signature_trace(seed, ticks, anchors);
    std::vector<uint64_t> b = run_signature_trace(seed, ticks, anchors);

    if (a != b) {
        size_t idx = 0;
        while (idx < a.size() && a[idx] == b[idx]) idx++;
        std::cerr << "FAIL: signature mismatch at tick " << idx
                  << " a=" << (unsigned long long)a[idx]
                  << " b=" << (unsigned long long)b[idx] << "\n";
        return 1;
    }

    std::cout << "PASS: deterministic signature stream (ticks=" << ticks
              << ", anchors=" << anchors << ")\n";
    return 0;
}
