#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "GE_runtime.hpp"
#include "ew_cli_args.hpp"

// Headless local hydration runner.
// Mechanism:
//   - Build substrate in-process.
//   - Admit observations (optional, stdin lines).
//   - Tick deterministically for N ticks.
//   - Hydrate commit-ready inspector artifacts into a workspace root_dir.
//
// This keeps computation inside the substrate (software phase-dynamics).
// The hydrator is a deterministic projection step.

int main(int argc, char** argv) {
    ew::CliArgsKV args;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, args)) {
        std::fprintf(stderr, "ew_hydrate_workspace: malformed args\n");
        return 2;
    }
    std::string root_dir;
    uint64_t ticks = 0;
    uint64_t seed = 1;

    (void)ew::ew_cli_get_str(args, "root_dir", root_dir);
    if (root_dir.empty()) (void)ew::ew_cli_get_str(args, "root-dir", root_dir);
    (void)ew::ew_cli_get_u64(args, "ticks", ticks);
    (void)ew::ew_cli_get_u64(args, "projection_seed", seed);
    if (seed == 1) (void)ew::ew_cli_get_u64(args, "seed", seed);

    if (root_dir.empty() || ticks == 0) {
        std::fprintf(stderr, "usage: ew_hydrate_workspace root_dir=<dir> ticks=<n> [projection_seed=<u64>]\n");
        return 2;
    }

    SubstrateManager sm(256);
    sm.set_projection_seed(seed);

    // Optional stdin injection: each line is treated as a substrate observation.
    // This stays inside the substrate via observe_text_line.
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), stdin)) {
        std::string s(buf);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty()) continue;
        sm.observe_text_line(s);
    }

    for (uint64_t i = 0; i < ticks; ++i) {
        sm.tick();
    }

    const bool ok = sm.hydrate_workspace_to(root_dir);
    if (!ok) {
        std::fprintf(stderr, "hydration failed, error_code=%u\n", sm.last_hydration_error_code_u32);
        return 5;
    }

    // Print a minimal receipt summary.
    std::printf("hydration_tick_u64=%llu\n", (unsigned long long)sm.last_hydration_receipt.hydration_tick_u64);
    std::printf("rows=%zu\n", sm.last_hydration_receipt.rows.size());
    for (const auto& r : sm.last_hydration_receipt.rows) {
        std::printf("%s %u\n", r.rel_path.c_str(), r.bytes_written_u32);
    }

    return 0;
}
