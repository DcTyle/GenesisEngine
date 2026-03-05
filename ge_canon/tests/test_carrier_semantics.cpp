#include "anchor.hpp"
#include "GE_causality_clamp.hpp"
#include "GE_budget_helpers.hpp"
#include "GE_vector_budget.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

static void test_amplitude_object_count_monotonic() {
    uint16_t prev = 0;
    for (uint32_t n = 0; n <= 1024; ++n) {
        const uint16_t a = Anchor::encode_object_count(n, 1024);
        if (n == 0) {
            assert(a == 0);
        } else {
            assert(a >= 1);
            assert(a >= prev);
        }
        prev = a;
    }

    // Independence from chi/phase: encoder must not use anchor instance state.
    Anchor a0;
    a0.chi_q = 123;
    Anchor a1;
    a1.chi_q = 999999;
    const uint16_t x0 = Anchor::encode_object_count(77, 1024);
    (void)a0; (void)a1;
    const uint16_t x1 = Anchor::encode_object_count(77, 1024);
    assert(x0 == x1);
}

static void test_voltage_budget_decreases_with_amperage() {
    const uint16_t coh = 30000; // ~0.915 in Q0.15
    uint16_t prev = Anchor::encode_vector_budget(coh, 0);
    for (uint32_t i = 0; i <= 65535; i += 4096) {
        const uint16_t v = Anchor::encode_vector_budget(coh, (uint16_t)i);
        // As i rises, v should not increase (budget reserved by compute density).
        assert(v <= prev);
        prev = v;
    }
}

static void test_causality_clamp_bounds_delta() {
    // Use a deliberately high f_code and confirm delta is clamped.
    const int32_t f = 1000000;
    const int64_t sf_q32_32 = (1LL << 32); // step factor = 1
    const uint16_t w_q15 = 32767;         // weight = 1
    const int64_t bound = (int64_t)TURN_SCALE / 64; // 1/64 turn

    int64_t out_delta = 0;
    const int32_t f2 = GE::clamp_frequency_by_causality(
        f, sf_q32_32, w_q15, bound,
        (int64_t)TURN_SCALE, (int64_t)F_SCALE, &out_delta);

    (void)f2;
    const int64_t mag = (out_delta < 0) ? -out_delta : out_delta;
    assert(mag <= bound);

    // Sign preservation.
    assert((out_delta >= 0) == true);
    const int32_t f_neg = -f;
    int64_t out_delta_neg = 0;
    (void)GE::clamp_frequency_by_causality(
        f_neg, sf_q32_32, w_q15, bound,
        (int64_t)TURN_SCALE, (int64_t)F_SCALE, &out_delta_neg);
    assert(out_delta_neg <= 0);
    const int64_t mag2 = (out_delta_neg < 0) ? -out_delta_neg : out_delta_neg;
    assert(mag2 <= bound);
}

static void test_work_budget_tracks_v_and_i() {
    const uint32_t cap = 512u;
    // Higher voltage should not reduce budget when amperage is fixed.
    uint32_t prev = 0u;
    for (uint32_t v = 0u; v <= 65535u; v += 4096u) {
        const uint32_t b = GE::work_budget_from_vi_u16((uint16_t)v, 0u, cap);
        assert(b >= prev);
        prev = b;
    }
    // Higher amperage should not increase budget when voltage is fixed.
    prev = GE::work_budget_from_vi_u16(65535u, 0u, cap);
    for (uint32_t i = 0u; i <= 65535u; i += 4096u) {
        const uint32_t b = GE::work_budget_from_vi_u16(65535u, (uint16_t)i, cap);
        assert(b <= prev);
        prev = b;
    }
    // Floor is enforced.
    const uint32_t floor_case = GE::work_budget_from_vi_u16(0u, 65535u, cap);
    assert(floor_case >= 1u);
}

static void test_vector_budget_tracks_v_and_i() {
    const uint32_t base = 1024u;
    const uint32_t b_lo = GE::compute_vector_budget(1000u, 60000u, base);
    const uint32_t b_hi = GE::compute_vector_budget(60000u, 1000u, base);
    assert(b_hi > b_lo);

    const uint32_t cap = 4096u;
    const uint32_t n1 = GE::decode_object_count_est(1000u, cap);
    const uint32_t n2 = GE::decode_object_count_est(30000u, cap);
    assert(n2 >= n1);
    assert(n2 <= cap);
}

int main() {
    test_amplitude_object_count_monotonic();
    test_voltage_budget_decreases_with_amperage();
    test_causality_clamp_bounds_delta();
    test_work_budget_tracks_v_and_i();
    test_vector_budget_tracks_v_and_i();
    std::puts("test_carrier_semantics: OK");
    return 0;
}
