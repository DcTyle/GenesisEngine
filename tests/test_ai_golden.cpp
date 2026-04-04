#include <cstdint>
#include <memory>
#include <vector>

#include "GE_runtime.hpp"

static int fail(const char* msg) {
    // Minimal diagnostic to keep the test harness transparent.
    std::fprintf(stderr, "FAIL: %s\n", msg ? msg : "(null)");
    return 1;
}

int main() {
    // Deterministic golden run: fixed seed, fixed observation stream.
    auto sm = std::make_unique<SubstrateManager>(64);
    sm->set_projection_seed(1337);

    sm->submit_gpu_pulse_sample_v2(2048u, 4096u, 768u, 1024u, 320u, 512u);
    sm->observe_text_line("QUERY:what is phase dynamics");
    sm->crawler_enqueue_observation_utf8(1, 1, 1, 1, 1, "local", "test:1", "hello genesis");
    sm->crawler_enqueue_observation_utf8(2, 1, 1, 1, 1, "local", "test:2", "phase dynamics");
    EwExternalApiResponse api_resp{};
    api_resp.request_id_u64 = 77u;
    api_resp.tick_u64 = 0u;
    api_resp.http_status_s32 = 200;
    const char kApiResp[] = "phase dynamics api decode surface";
    api_resp.body_bytes.assign(kApiResp, kApiResp + sizeof(kApiResp) - 1u);
    sm->submit_external_api_response(api_resp);

    for (uint64_t t = 0; t < 256; ++t) sm->tick();

    const EwAiStatus st = sm->ai_status();
    if (st.tick_u64 == 0) return fail("status tick");
    // Confidence may vary with lane residuals; do not over-constrain here.

    std::vector<EwAiActionEvent> ev;
    ev.resize(SubstrateManager::AI_ACTION_LOG_CAP);
    const uint32_t n = sm->ai_get_action_log(ev.data(), (uint32_t)ev.size());
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

    EwAiSubstrateTelemetry ai{};
    if (!sm->get_ai_substrate_telemetry(&ai)) return fail("no ai substrate telemetry");
    if (ai.memory_norm_q15 == 0u) return fail("ai memory lane zero");
    if (ai.reasoning_norm_q15 == 0u) return fail("ai reasoning lane zero");
    if (ai.planning_norm_q15 == 0u) return fail("ai planning lane zero");
    if (ai.creativity_norm_q15 == 0u) return fail("ai creativity lane zero");
    if (ai.perception_norm_q15 == 0u) return fail("ai perception lane zero");
    if (ai.temporal_binding_norm_q15 == 0u) return fail("ai temporal binding lane zero");
    if (ai.network_coherence_norm_q15 == 0u) return fail("ai network coherence lane zero");

    EwAiDataSubstrateTelemetry ai_data{};
    if (!sm->get_ai_data_substrate_telemetry(&ai_data)) return fail("no ai data telemetry");
    if (ai_data.crawler_enqueued_obs_u64 == 0u) return fail("crawler ledger empty");
    if (ai_data.crawler_flow_norm_q15 == 0u) return fail("crawler flow lane zero");
    if (ai_data.crawler_coherence_norm_q15 == 0u) return fail("crawler coherence lane zero");
    if (ai_data.temporal_memory_norm_q15 == 0u) return fail("temporal memory lane zero");
    if (ai_data.temporal_prediction_norm_q15 == 0u) return fail("temporal prediction lane zero");
    if (ai_data.gpu_gradient_energy_norm_q15 == 0u) return fail("gpu gradient lane zero");
    if (ai_data.gpu_spider_a_code_u16 == 0u) return fail("gpu spider a_code zero");
    if (ai_data.gpu_spider_v_code_u16 == 0u) return fail("gpu spider v_code zero");
    if (ai_data.gpu_spider_i_code_u16 == 0u) return fail("gpu spider i_code zero");
    if (ai_data.api_decode_count_u16 == 0u) return fail("api decode count zero");
    if (ai_data.api_decode_norm_q15 == 0u) return fail("api decode norm zero");
    if (ai_data.gpu_gradient_dim_q15[4] == 0u &&
        ai_data.gpu_gradient_dim_q15[5] == 0u &&
        ai_data.gpu_gradient_dim_q15[6] == 0u) {
        return fail("gpu pairwise gradients zero");
    }

    return 0;
}
