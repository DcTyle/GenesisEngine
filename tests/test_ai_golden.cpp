#include <cstdint>
#include <vector>

#include "GE_runtime.hpp"

static int fail(const char* msg) {
    // Minimal diagnostic to keep the test harness transparent.
    std::fprintf(stderr, "FAIL: %s\n", msg ? msg : "(null)");
    return 1;
}

int main() {
    // Deterministic golden run: fixed seed, fixed observation stream.
    SubstrateManager sm(64);
    sm.set_projection_seed(1337);

    sm.crawler_enqueue_observation_utf8(1, 1, 1, 1, 1, "local", "test:1", "hello genesis");
    sm.crawler_enqueue_observation_utf8(2, 1, 1, 1, 1, "local", "test:2", "phase dynamics");

    for (uint64_t t = 0; t < 256; ++t) sm.tick();

    const EwAiStatus st = sm.ai_status();
    if (st.tick_u64 == 0) return fail("status tick");
    // Confidence may vary with lane residuals; do not over-constrain here.

    std::vector<EwAiActionEvent> ev;
    ev.resize(SubstrateManager::AI_ACTION_LOG_CAP);
    const uint32_t n = sm.ai_get_action_log(ev.data(), (uint32_t)ev.size());
    if (n == 0) return fail("no actions");

    // At least one pulse emission should occur.
    bool saw_emit = false;
    for (uint32_t i = 0; i < n; ++i) {
        if (ev[i].kind_u16 == (uint16_t)EW_AI_ACTION_PULSE_EMIT) {
            saw_emit = true;
            // Basic invariants.
            if (ev[i].a_code_u32 == 0) return fail("a_code zero");
            if (ev[i].f_code_i32 == 0) return fail("f_code zero");
            break;
        }
    }
    if (!saw_emit) return fail("no pulse emit");

    return 0;
}
