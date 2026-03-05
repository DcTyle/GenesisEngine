#include <cstdint>
#include <vector>
#include <iostream>
#include <cstdlib>

#include "GE_runtime.hpp"
#include "GE_state_fingerprint.hpp"
#include "anchor.hpp"

static uint32_t find_first_spectral_anchor(const SubstrateManager& sm) {
    for (uint32_t i = 0u; i < (uint32_t)sm.anchors.size(); ++i) {
        if (sm.anchors[i].kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) return i;
    }
    return 0u;
}

static int16_t get_last_op_res_i16(const SubstrateManager& sm, uint32_t anchor_id_u32) {
    if (anchor_id_u32 >= sm.anchors.size()) return 0;
    const Anchor& a = sm.anchors[anchor_id_u32];
    if (a.kind_u32 != EW_ANCHOR_KIND_SPECTRAL_FIELD) return 0;
    const EwSpectralFieldAnchorState& ss = a.spectral_field_state;
    return (int16_t)ss.pad5_u16;
}

static std::vector<uint64_t> run_trace(uint64_t seed, std::vector<int16_t>* out_res_at_ticks) {
    SubstrateManager sm(64);
    sm.projection_seed = seed;

    const uint32_t spectral_id = find_first_spectral_anchor(sm);
    if (spectral_id == 0u) {
        std::cerr << "FAIL: no spectral anchor found\n";
        std::abort();
    }

    std::vector<uint64_t> trace;
    trace.reserve(40);

    if (out_res_at_ticks) out_res_at_ticks->clear();

    for (uint32_t t = 0u; t < 40u; ++t) {
        if (t == 3u)  sm.ui_submit_user_text_line("OP:ADD 5 6");
        if (t == 10u) sm.ui_submit_user_text_line("OP:MUL -2 9");
        if (t == 17u) sm.ui_submit_user_text_line("OP:CLAMP 50 -10 10");

        sm.tick();

        const uint64_t fp = ge_compute_state_fingerprint_9d(&sm);
        trace.push_back(fp);

        if (out_res_at_ticks) {
            if (t == 3u || t == 10u || t == 17u) {
                out_res_at_ticks->push_back(get_last_op_res_i16(sm, spectral_id));
            }
        }
    }
    return trace;
}

int main() {
    const uint64_t seed = 0x12345678ULL;

    std::vector<int16_t> ra, rb;
    std::vector<uint64_t> a = run_trace(seed, &ra);
    std::vector<uint64_t> b = run_trace(seed, &rb);

    if (a.size() != b.size()) {
        std::cerr << "FAIL: fingerprint length mismatch\n";
        return 1;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            std::cerr << "FAIL: fingerprint mismatch at tick " << i << "\n";
            return 1;
        }
    }

    if (ra.size() != 3 || rb.size() != 3) {
        std::cerr << "FAIL: op result capture size mismatch\n";
        return 1;
    }
    // NOTE: calibration / other systems may also emit math ops in the future; this test
    // only checks the known commands at the known ticks.
    if (ra[0] != 11 || ra[1] != -18 || ra[2] != 10) {
        std::cerr << "FAIL: unexpected op results: " << ra[0] << ", " << ra[1] << ", " << ra[2] << "\n";
        return 1;
    }
    if (rb[0] != 11 || rb[1] != -18 || rb[2] != 10) {
        std::cerr << "FAIL: unexpected op results on second run\n";
        return 1;
    }

    std::cout << "PASS: deterministic fingerprint stream + OP results\n";
    return 0;
}
