#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "ew_cli_args.hpp"
#include "GE_corpus_anchor_store.hpp"
#include "GE_overlap_router.hpp"
#include "GE_corpus_canonicalizer.hpp"
#include "crawler_encode_cuda.hpp"
#include "frequency_collapse.hpp"

// Build a minimal query record from ASCII text deterministically.
static bool ge_build_query_from_text(const std::string& s_utf8, uint8_t lane_u8, GE_CorpusAnchorRecord& out_q) {
    std::string canon;
    if (!GE_canonicalize_utf8_strict(s_utf8, canon)) return false;

    SpiderCode4 sc{};
    if (!ew_encode_spidercode4_from_bytes_chunked_cuda(reinterpret_cast<const uint8_t*>(canon.data()),
                                                       canon.size(), 4096u, &sc)) {
        return false;
    }

    std::vector<EwFreqComponentQ32_32> comps;
    comps.reserve(4);
    auto push_comp = [&](int32_t f_code, int32_t a_code, int32_t phi_code) {
        EwFreqComponentQ32_32 c;
        c.f_turns_q32_32 = int64_t(f_code) << 16;
        c.a_q32_32 = (int64_t(a_code) << 16);
        c.phi_turns_q32_32 = int64_t(phi_code) << 16;
        comps.push_back(c);
    };
    push_comp(sc.f_code, sc.a_code, sc.v_code);
    push_comp(sc.a_code, sc.v_code, sc.i_code);
    push_comp(sc.v_code, sc.i_code, sc.f_code);
    push_comp(sc.i_code, sc.f_code, sc.a_code);

    EwCarrierWaveQ32_32 carrier{};
    if (!ew_collapse_frequency_components_q32_32(comps, carrier)) return false;

    out_q = GE_CorpusAnchorRecord{};
    out_q.lane_u8 = lane_u8;
    out_q.sc4 = sc;
    out_q.carrier = carrier;
    // Provenance IDs are zero for queries.
    out_q.domain_id9 = EwId9{};
    out_q.source_id9 = EwId9{};
    out_q.anchor_id9 = EwId9{};
    return true;
}

int main(int argc, char** argv) {
    ew::CliArgsKV args;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, args)) {
        std::fprintf(stderr, "ge_vocab_recall_test: malformed args\n");
        return 2;
    }

    auto get_str_def = [&](const char* k, const char* defv)->std::string {
        std::string out;
        if (ew::ew_cli_get_str(args, k, out)) return out;
        return std::string(defv ? defv : "");
    };
    auto get_u32_def = [&](const char* k, uint32_t defv)->uint32_t {
        uint32_t out = defv;
        (void)ew::ew_cli_get_u32(args, k, out);
        return out;
    };

    const std::string store_path = get_str_def("store", "GenesisEngineState/Corpus/anchors.gecas");
    const std::string token_path = get_str_def("tokens", "");
    const uint32_t topk = get_u32_def("topk", 8);
    const uint32_t lane = get_u32_def("lane", 0);
    const int64_t accept_min_q32_32 = (int64_t(get_u32_def("min_score_q16_16", 8192)) << 16); // default ~0.125

    if (token_path.empty()) {
        std::fprintf(stderr, "ge_vocab_recall_test: tokens=<file> required\n");
        return 2;
    }

    GE_CorpusAnchorStore store;
    if (!store.load_from_file(store_path)) {
        std::fprintf(stderr, "ge_vocab_recall_test: failed load store=%s\n", store_path.c_str());
        return 2;
    }

    std::ifstream in(token_path);
    if (!in) {
        std::fprintf(stderr, "ge_vocab_recall_test: failed open tokens=%s\n", token_path.c_str());
        return 2;
    }

    uint64_t total = 0, hit = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        total++;

        GE_CorpusAnchorRecord q{};
        if (!ge_build_query_from_text(line, (uint8_t)lane, q)) continue;

        const auto hits = GE_retrieve_topk_gpu(store, q, (uint8_t)lane, nullptr, topk);
        if (!hits.empty() && hits[0].score_q32_32 >= accept_min_q32_32) {
            hit++;
        }
    }

    const double rate = (total == 0) ? 0.0 : double(hit) / double(total);
    std::printf("GE_VOCAB_RECALL:total=%llu hit=%llu rate=%.6f\n",
                (unsigned long long)total,
                (unsigned long long)hit,
                rate);
    return 0;
}
