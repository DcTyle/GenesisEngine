#pragma once
#include <cstdint>
#include <vector>

class SubstrateManager;

// Deterministic 9D state signature harness.
// This is NOT a security mechanism; it is a reproducibility signature used
// to detect determinism regressions.

// Compute a 64-bit signature from CPU-visible authoritative state.
// The goal is high sensitivity to divergence.
uint64_t ge_compute_state_signature_9d(const SubstrateManager* sm);

// Optional: load a reference signature trace from a text file.
// One hex or decimal u64 per line. Returns true if at least one value loaded.
bool ge_load_signature_reference(const char* path_utf8, std::vector<uint64_t>& out);
