#include <cstdio>
#include <cstdlib>
#include <string>

#include "ew_cli_args.hpp"

static int run_cmd(const std::string& cmd) {
#if defined(_WIN32)
    return std::system(cmd.c_str());
#else
    return std::system(cmd.c_str());
#endif
}

int main(int argc, char** argv) {
    ew::CliArgsKV args;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, args)) {
        std::fprintf(stderr, "ge_regression_suite: malformed args\n");
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

    const std::string corpus_dir = get_str_def("corpus_dir", "");
    const std::string out_dir = get_str_def("out_dir", "GenesisEngineState/Corpus");
    const std::string tokens = get_str_def("tokens", "");
    const uint32_t lane = get_u32_def("lane", 0);
    const uint32_t topk = get_u32_def("topk", 16);
    const uint32_t epochs = get_u32_def("epochs", 2);
    const std::string thresholds = get_str_def("thresholds", "docs/ge_lane_thresholds_v1.txt");

    if (corpus_dir.empty()) {
        std::fprintf(stderr, "ge_regression_suite: corpus_dir=<dir> required\n");
        return 2;
    }

    std::printf("GE_REGRESSION:begin corpus_dir=%s out_dir=%s lane=%u epochs=%u\n",
                corpus_dir.c_str(), out_dir.c_str(), lane, epochs);

    {
        char buf[4096];
        std::snprintf(buf, sizeof(buf),
                      "ge_corpus_ingest corpus_dir=\"%s\" lane=%u out_dir=\"%s\" chunk_bytes=65536 topk=%u thresholds=\"%s\" epochs=%u cuda=true",
                      corpus_dir.c_str(), lane, out_dir.c_str(), topk, thresholds.c_str(), epochs);
        const int rc = run_cmd(buf);
        if (rc != 0) { std::fprintf(stderr, "GE_REGRESSION:ingest_fail rc=%d\n", rc); return 3; }
    }

    {
        char buf[4096];
        std::snprintf(buf, sizeof(buf),
                      "ge_corpus_replay state_dir=\"%s\" corpus_root=\"%s\" thresholds=\"%s\" epochs=%u cuda=true",
                      out_dir.c_str(), out_dir.c_str(), thresholds.c_str(), epochs);
        const int rc = run_cmd(buf);
        if (rc != 0) { std::fprintf(stderr, "GE_REGRESSION:replay_fail rc=%d\n", rc); return 4; }
    }

    if (!tokens.empty()) {
        char buf[4096];
        std::snprintf(buf, sizeof(buf),
                      "ge_vocab_recall_test store=\"%s/anchors.gecas\" tokens=\"%s\" lane=%u topk=%u",
                      out_dir.c_str(), tokens.c_str(), lane, topk);
        const int rc = run_cmd(buf);
        if (rc != 0) { std::fprintf(stderr, "GE_REGRESSION:vocab_fail rc=%d\n", rc); return 5; }
    }

    std::printf("GE_REGRESSION:ok\n");
    return 0;
}
