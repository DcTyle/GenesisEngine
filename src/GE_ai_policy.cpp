#include "GE_ai_policy.hpp"

// Deterministic LCG step (matches EwLcg64 constants).
static inline uint64_t ew_lcg_step(uint64_t s) {
    return s * 6364136223846793005ULL + 1442695040888963407ULL;
}

EwAiPolicyTable::EwAiPolicyTable() : seed_u64_(1) {
    for (int i = 0; i < 256; ++i) class_profile_u8_[i] = 0;
}

void EwAiPolicyTable::init(uint64_t projection_seed) {
    seed_u64_ = projection_seed ? projection_seed : 1;
    uint64_t s = seed_u64_ ^ 0xC0FFEE1234ULL;
    for (int i = 0; i < 256; ++i) {
        s = ew_lcg_step(s);
        // Bias toward CORE with occasional LANGUAGE/CRAWLER preferences.
        const uint8_t r = (uint8_t)((s >> 24) & 0xFFU);
        uint8_t p = 0;
        if (r < 32) p = 1;
        else if (r < 48) p = 2;
        class_profile_u8_[i] = p;
    }
}

EwAiPolicyDecision EwAiPolicyTable::decide(uint32_t class_id_u32,
                                          int64_t confidence_q32_32,
                                          int64_t attractor_strength_q32_32) const {
    EwAiPolicyDecision d{};
    const uint8_t base = class_profile_u8_[(uint8_t)class_id_u32];

    // Confidence bands.
    const int64_t c_hi = (int64_t)((3LL << 30));   // 0.75
    const int64_t c_mid = (int64_t)((1LL << 31));  // 0.5

    // Strength bands: 1.0 and 2.0 in Q32.32.
    const int64_t s_mid = (1LL << 32);
    const int64_t s_hi = (2LL << 32);

    uint8_t profile = 0;
    if (confidence_q32_32 >= c_hi && attractor_strength_q32_32 >= s_mid) {
        // High confidence + reinforced attractor: allow broader coupling.
        profile = (base == 0) ? 1 : base;
    } else if (confidence_q32_32 >= c_mid && attractor_strength_q32_32 >= s_hi) {
        profile = (base == 2) ? 2 : base;
    } else {
        profile = 0;
    }

    // Pulse shape: bucket a_code scales with confidence and strength.
    // a_code in [1..65535].
    int64_t w = confidence_q32_32;
    if (w < 0) w = 0;
    if (w > (1LL << 32)) w = (1LL << 32);

    int64_t s = attractor_strength_q32_32;
    if (s < 0) s = 0;
    if (s > (8LL << 32)) s = (8LL << 32);

    // Map to 0..1024 bucket.
    const int64_t bucket_c = (w * 1024) >> 32;
    const int64_t bucket_s = (s * 256) >> 32;
    int64_t bucket = bucket_c + bucket_s;
    if (bucket < 1) bucket = 1;
    if (bucket > 65535) bucket = 65535;

    d.profile_id_u8 = profile;
    d.f_code_i32 = (int32_t)((int32_t)((class_id_u32 & 0x7F)) - 63); // [-63..64]
    if (d.f_code_i32 == 0) d.f_code_i32 = 1;
    d.a_code_u16 = (uint16_t)bucket;

    // Voltage/amperage carrier codes: deterministic proxies derived from
    // attractor strength (v) and confidence (i). These are bounded observables
    // used for gating and tensor-gradient coupling.
    int64_t v_bucket = bucket_s; // 0..2048 approx
    if (v_bucket < 1) v_bucket = 1;
    if (v_bucket > 65535) v_bucket = 65535;
    int64_t i_bucket = bucket_c; // 0..1024
    if (i_bucket < 1) i_bucket = 1;
    // Scale i_bucket into 0..65535 range for uniform carrier width.
    int64_t i_scaled = (i_bucket * 64);
    if (i_scaled > 65535) i_scaled = 65535;

    d.v_code_u16 = (uint16_t)v_bucket;
    d.i_code_u16 = (uint16_t)i_scaled;
    d.reserved0_u16 = 0;
    return d;
}
