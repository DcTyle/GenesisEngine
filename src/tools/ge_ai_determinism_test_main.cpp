#include <cstdio>
#include <cstdlib>
#include <string>

#include "GE_runtime.hpp"
#include "GE_ai_regression_tests.hpp"

// -----------------------------------------------------------------------------
// AI Determinism Test Runner
//
// No network. Bounded runtime. Returns non-zero on failure.
// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    SubstrateManager sm(96u);
    sm.visualization_headless = true;
    sm.set_projection_seed(1337ull);
    sm.ai_learning_enabled_u32 = 1u;
    sm.ai_crawling_enabled_u32 = 0u;

    std::string log;
    const bool ok = ge_run_ai_determinism_selftests(&sm, log);

    std::printf("%s", log.c_str());
    return ok ? 0 : 10;
}
