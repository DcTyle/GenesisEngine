#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "GE_runtime.hpp"
#include "ew_cli_args.hpp"

// -----------------------------------------------------------------------------
//  Headless AI Runner (no UE)
// -----------------------------------------------------------------------------
// Usage:
//   ew_ai_headless_run --ticks N --anchors M --seed S
// Reads UTF-8 lines from stdin. Each line is submitted as one crawler
// observation artifact. The crawler/encoder run inside the substrate.
//
// Output:
//   - Final AI status line
//   - Action log lines (oldest->newest)

int main(int argc, char** argv) {
    ew::CliArgsKV args;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, args)) {
        std::fprintf(stderr, "ew_ai_headless_run: malformed args\n");
        return 2;
    }
    uint64_t ticks = 512;
    uint32_t anchors = 64;
    uint64_t seed = 1;

    (void)ew::ew_cli_get_u64(args, "ticks", ticks);
    (void)ew::ew_cli_get_u32(args, "anchors", anchors);
    (void)ew::ew_cli_get_u64(args, "seed", seed);

    SubstrateManager sm(anchors);
    sm.set_projection_seed(seed);

    // Ingest stdin lines as substrate observations.
    // NOTE: observe_text_line stores the line in liberty-space state and routes
    // it through the crawler with deterministic labels.
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        sm.observe_text_line(line);
    }

    for (uint64_t t = 0; t < ticks; ++t) {
        sm.tick();
    }

    const EwAiStatus st = sm.ai_status();
    std::cout << "AI_STATUS tick=" << st.tick_u64
              << " class_id=" << st.class_id_u32
              << " confidence_q32_32=" << st.confidence_q32_32
              << " sig9=" << st.sig9_u64
              << "\n";

    std::vector<EwAiActionEvent> events;
    events.resize(SubstrateManager::AI_ACTION_LOG_CAP);
    const uint32_t n = sm.ai_get_action_log(events.data(), (uint32_t)events.size());
    std::cout << "AI_ACTION_LOG count=" << n << "\n";
    for (uint32_t i = 0; i < n; ++i) {
        const EwAiActionEvent& e = events[i];
        if (e.kind_u16 == 0) continue;
        std::cout << "AI_ACTION"
                  << " tick=" << e.tick_u64
                  << " kind=" << e.kind_u16
                  << " class_id=" << e.class_id_u32
                  << " sig9=" << e.sig9_u64
                  << " conf_q32_32=" << e.confidence_q32_32
                  << " strength_q32_32=" << e.attractor_strength_q32_32
                  << " gamma_turns_q=" << e.frame_gamma_turns_q
                  << " profile=" << e.profile_id_u16
                  << " anchor=" << e.target_anchor_id_u32
                  << " f_code=" << e.f_code_i32
                  << " a_code=" << e.a_code_u32
                  << "\n";
    }

    return 0;
}
