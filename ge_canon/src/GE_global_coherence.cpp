#include "GE_global_coherence.hpp"

namespace genesis {

static inline uint16_t clamp_q15_u16(int32_t v) {
    if (v < 0) v = 0;
    if (v > 32768) v = 32768;
    return (uint16_t)v;
}

void GE_GlobalCoherence::update(uint64_t tick_u64,
                               uint16_t lang_in_q15,
                               uint16_t phys_in_q15,
                               uint16_t crawl_in_q15,
                               uint16_t exp_in_q15) {
    last_tick_u64 = tick_u64;
    lang_q15 = lang_in_q15;
    phys_q15 = phys_in_q15;
    crawl_q15 = crawl_in_q15;
    exp_q15 = exp_in_q15;

    // Deterministic weighted sum, weights sum to 32768.
    const int32_t w_lang = 12288;  // 0.375
    const int32_t w_phys = 10240;  // 0.3125
    const int32_t w_crawl = 5120;  // 0.15625
    const int32_t w_exp = 5120;    // 0.15625

    const int32_t acc = (int32_t)((w_lang * (int32_t)lang_q15 +
                                  w_phys * (int32_t)phys_q15 +
                                  w_crawl * (int32_t)crawl_q15 +
                                  w_exp * (int32_t)exp_q15) >> 15);

    // Merge with prior global coherence using a deterministic low-pass.
    // global = 3/4 old + 1/4 new
    const int32_t g = (int32_t)global_q15;
    const int32_t merged = (3 * g + acc) / 4;
    global_q15 = clamp_q15_u16(merged);
}

void GE_GlobalCoherence::leak(uint64_t tick_u64, uint16_t leak_rate_q15) {
    (void)tick_u64;
    // leak_rate_q15 is a small Q15 fraction; we do global -= global*leak
    const int32_t g = (int32_t)global_q15;
    const int32_t dec = (g * (int32_t)leak_rate_q15) >> 15;
    global_q15 = clamp_q15_u16(g - dec);
}

void GE_GlobalCoherence::spike_from_user_input(uint64_t tick_u64, uint32_t byte_len_u32, uint16_t strength_q15) {
    last_tick_u64 = tick_u64;
    // Deterministic spike factor based on message size (bounded).
    // Factor in [0.25..1.0] Q15.
    uint32_t n = byte_len_u32;
    if (n > 4096u) n = 4096u;
    // Map length to factor: 0 -> 0.25, 4096 -> 1.0
    const uint32_t f = 8192u + (n * 24576u) / 4096u;
    const int32_t bump = (int32_t)((((uint32_t)strength_q15) * f) >> 15);
    global_q15 = clamp_q15_u16((int32_t)global_q15 + bump);
}

} // namespace genesis
