#pragma once

#include <cstdint>
#include <string>

class SubstrateManager;

// -----------------------------------------------------------------------------
// AI Determinism Regression Tests
//
// These tests are intended to fail loudly if deterministic behavior regresses
// across merges. They are NOT exhaustive; they are a reproducibility harness.
//
// Contract:
//  - No network required.
//  - Bounded runtime.
//  - Uses only deterministic inputs.
// -----------------------------------------------------------------------------

// Runs a bounded deterministic self-test suite.
// Returns true if all tests pass. On failure, out_log_utf8 contains details.
bool ge_run_ai_determinism_selftests(SubstrateManager* sm, std::string& out_log_utf8);
