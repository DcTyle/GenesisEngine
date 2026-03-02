#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ew_cli_args.hpp"

#include "GE_corpus_anchor_store.hpp"
#include "GE_corpus_pulse_log.hpp"
#include "GE_coherence_graph_store.hpp"
#include "GE_lane_threshold_schedule.hpp"
#include "GE_quantity_peeling_trainer.hpp"

// Deterministic replay:
// - Load pulses.gepl
// - Verify each pulse by re-encoding payload bytes from blobs.txt
// - Rebuild anchor store records from pulses
// - Run trainer epochs with same thresholds
// - Save rebuilt outputs to temp paths and compare byte-for-byte with expected paths

static bool ge_file_read_all(const std::string& path, std::vector<uint8_t>* out) {
    out->clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    if (n < 0) { std::fclose(f); return false; }
    std::fseek(f, 0, SEEK_SET);
    out->resize((size_t)n);
    if (n > 0) {
        if (std::fread(out->data(), 1, (size_t)n, f) != (size_t)n) { std::fclose(f); return false; }
    }
    std::fclose(f);
    return true;
}

static bool ge_files_equal(const std::string& a, const std::string& b) {
    std::vector<uint8_t> A,B;
    if (!ge_file_read_all(a,&A)) return false;
    if (!ge_file_read_all(b,&B)) return false;
    return A == B;
}

int main(int argc, char** argv) {
    ew::CliArgsKV cli;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, cli)) {
        std::fprintf(stderr, "ge_corpus_replay: malformed args\n");
        return 2;
    }

    auto get_str_def = [&](const char* k, const char* defv)->std::string {
        std::string out;
        if (ew::ew_cli_get_str(cli, k, out)) return out;
        return std::string(defv ? defv : "");
    };
    auto get_u32_def = [&](const char* k, uint32_t defv)->uint32_t {
        uint32_t out = defv;
        (void)ew::ew_cli_get_u32(cli, k, out);
        return out;
    };
    auto get_bool_def = [&](const char* k, bool defv)->bool {
        bool out = defv;
        (void)ew::ew_cli_get_bool(cli, k, out);
        return out;
    };

    const std::string corpus_state_dir = get_str_def("state_dir", "GenesisEngineState/Corpus");
    const std::string corpus_root = get_str_def("corpus_root", "GenesisEngineState/Corpus");
    const std::string thresholds_path = get_str_def("thresholds", "docs/ge_lane_thresholds_v1.txt");
    const uint32_t epochs_u32 = get_u32_def("epochs", 1u);
    const bool use_cuda = get_bool_def("cuda", true);

    if (!use_cuda) {
        std::fprintf(stderr, "ge_corpus_replay: cuda=false is not permitted (no CPU path)\n");
        return 2;
    }

    const std::string pulses_path = corpus_state_dir + "/pulses.gepl";
    const std::string expected_store_path = corpus_state_dir + "/anchors.gecas";
    const std::string expected_graph_path = corpus_state_dir + "/graph.gecg";

    GE_CorpusPulseLog log;
    if (!log.load_from_file(pulses_path)) {
        std::fprintf(stderr, "ge_corpus_replay: failed to load pulses: %s\n", pulses_path.c_str());
        return 2;
    }

    uint64_t verified_ok = 0;
    for (size_t i = 0; i < log.records.size(); ++i) {
        std::string err;
        if (!GE_pulse_record_verify_against_payload(log.records[i], corpus_root, &err)) {
            std::fprintf(stderr, "ge_corpus_replay: pulse_verify_failed i=%zu err=%s\n", i, err.c_str());
            return 3;
        }
        verified_ok++;
    }

    // Rebuild store
    GE_CorpusAnchorStore store;
    store.records.reserve(log.records.size());
    for (const auto& r : log.records) {
        GE_CorpusAnchorRecord a;
        a.lane_u8 = r.lane_u8;
        a.domain_id9 = r.domain_id9;
        a.source_id9 = r.source_id9;
        a.seq_u32 = r.seq_u32;
        a.offset_u32 = r.offset_u32;
        a.size_u32 = r.size_u32;
        a.sc4 = r.sc4;
        a.carrier = r.carrier;
        a.anchor_id9 = GE_anchor_id9_from_provenance_and_carrier(a.lane_u8, a.domain_id9, a.source_id9, a.seq_u32, a.offset_u32, a.carrier);
        a.payload_relpath_utf8 = r.payload_relpath_utf8;
        a.payload_byte_off_u64 = r.payload_byte_off_u64;
        store.records.push_back(a);
    }
    store.sort_and_dedupe();

    // Load thresholds
    GE_AllLaneThresholds thresholds;
    if (!GE_load_lane_thresholds_from_file(thresholds_path, &thresholds)) {
        std::fprintf(stderr, "ge_corpus_replay: failed to load thresholds: %s\n", thresholds_path.c_str());
        return 2;
    }

    // Train
    genesis::LearningCheckpointGate gate;
    GE_CoherenceGraphStore graph;
    GE_TrainerParams tp;
    tp.topk_u32 = (uint32_t)ew_cli_get_u32(cli, "topk", 16u);
    tp.use_cuda_scoring = use_cuda;
    tp.opt_thresholds = &thresholds;
    GE_TrainerStats ts;

    for (uint32_t e = 0; e < epochs_u32; ++e) {
        tp.epoch_u32 = e;
        (void)GE_trainer_epoch(store, graph, gate, tp, ts);
    }

    const std::string tmp_store = corpus_state_dir + "/anchors.replay_tmp.gecas";
    const std::string tmp_graph = corpus_state_dir + "/graph.replay_tmp.gecg";
    if (!store.save_to_file(tmp_store)) {
        std::fprintf(stderr, "ge_corpus_replay: failed to save tmp store\n");
        return 4;
    }
    if (!graph.save_to_file(tmp_graph)) {
        std::fprintf(stderr, "ge_corpus_replay: failed to save tmp graph\n");
        return 5;
    }

    const bool store_ok = ge_files_equal(expected_store_path, tmp_store);
    const bool graph_ok = ge_files_equal(expected_graph_path, tmp_graph);

    std::printf("GE_REPLAY:verified=%llu store_equal=%d graph_equal=%d\n",
                (unsigned long long)verified_ok, store_ok ? 1 : 0, graph_ok ? 1 : 0);

    return (store_ok && graph_ok) ? 0 : 10;
}
