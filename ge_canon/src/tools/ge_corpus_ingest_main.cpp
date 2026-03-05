#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "GE_corpus_allowlist.hpp"
#include "GE_corpus_anchor_store.hpp"
#include "GE_corpus_canonicalizer.hpp"
#include "GE_overlap_router.hpp"
#include "GE_quantity_peeling_trainer.hpp"
#include "GE_coherence_graph_store.hpp"

#include "crawler_encode_cuda.hpp"
#include "ew_cli_args.hpp"

static inline bool ge_read_file_bytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    is.seekg(0, std::ios::end);
    const std::streamoff n = is.tellg();
    if (n < 0) return false;
    is.seekg(0, std::ios::beg);
    out.resize(size_t(n));
    if (n > 0) {
        if (!is.read(reinterpret_cast<char*>(out.data()), n)) return false;
    }
    return true;
}

static inline std::string ge_path_to_utf8(const std::filesystem::path& p) {
    // Deterministic: use generic_u8string.
    auto u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

static inline bool ge_write_blob_append(std::ofstream& blob, const std::string& s, uint64_t& out_off) {
    out_off = uint64_t(blob.tellp());
    if (!blob) return false;
    blob.write(s.data(), std::streamsize(s.size()));
    // Separator for deterministic slicing.
    const char nl = '\n';
    blob.write(&nl, 1);
    return bool(blob);
}

int main(int argc, char** argv) {
    ew::CliArgsKV cli;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, cli)) {
        std::fprintf(stderr, "ge_corpus_ingest: malformed args\n");
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

    const std::string allowlist_path = get_str_def("allowlist", "docs/neuralis_corpus_domains_expanded_v2.md");
    const std::string corpus_dir = get_str_def("corpus_dir", "");
    const std::string out_dir = get_str_def("out_dir", "GenesisEngineState/Corpus");
    const uint32_t chunk_bytes_u32 = get_u32_def("chunk_bytes", 65536u);
    const uint32_t lane_u32 = get_u32_def("lane", 0u);
    const uint32_t max_files_u32 = get_u32_def("max_files", 0u);
    const uint32_t topk_u32 = get_u32_def("topk", 16u);
    const std::string thresholds_path = get_str_def("thresholds", "docs/ge_lane_thresholds_v1.txt");
    const uint32_t epochs_u32 = get_u32_def("epochs", 1u);
    const bool use_cuda = get_bool_def("cuda", true);

    if (!use_cuda) {
        std::fprintf(stderr, "ge_corpus_ingest: cuda=false is not permitted (no CPU path)\n");
        return 2;
    }

    if (corpus_dir.empty()) {
        std::fprintf(stderr, "ge_corpus_ingest: corpus_dir is required\n");
        return 2;
    }

    GE_CorpusAllowlist allow;
    GE_AllLaneThresholds thresholds;
    if (!GE_load_lane_thresholds_from_file(thresholds_path, &thresholds)) {
        std::fprintf(stderr, "ge_corpus_ingest: failed to load thresholds: %s\n", thresholds_path.c_str());
        return 2;
    }

    if (!GE_load_corpus_allowlist_from_md(allowlist_path, allow)) {
        std::fprintf(stderr, "ge_corpus_ingest: failed to load allowlist: %s\n", allowlist_path.c_str());
        return 2;
    }

    std::filesystem::create_directories(out_dir);
    const std::string blob_path = out_dir + "/blobs.txt";
    const std::string store_path = out_dir + "/anchors.gecas";
    const std::string pulses_path = out_dir + "/pulses.gepl";
    const std::string graph_path = out_dir + "/graph.gecg";

    std::ofstream blob(blob_path, std::ios::binary | std::ios::app);
    if (!blob) {
        std::fprintf(stderr, "ge_corpus_ingest: failed to open blob: %s\n", blob_path.c_str());
        return 2;
    }

    // Collect files deterministically.
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(corpus_dir); it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) continue;
        files.push_back(it->path());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.generic_u8string() < b.generic_u8string();
    });
    if (max_files_u32 > 0 && files.size() > max_files_u32) files.resize(max_files_u32);

    GE_CorpusCanonicalizeStats canon_stats;
    GE_CorpusAnchorStore store;
    GE_CorpusPulseLog pulse_log;
    store.records.reserve(files.size() * 4);

    uint32_t seq = 0;
    for (const auto& fp : files) {
        const std::string rel = ge_path_to_utf8(std::filesystem::relative(fp, corpus_dir));
        // Domain inference: first path segment.
        std::string domain = rel;
        const size_t slash = domain.find('/');
        if (slash != std::string::npos) domain = domain.substr(0, slash);
        domain = ew_ascii_lower(domain);

        const auto* pol = allow.find_by_domain_ascii(domain);
        if (!pol) {
            continue; // strict allowlist
        }
        if (pol->lane_u8 != uint8_t(lane_u32)) {
            continue; // strict lane selection
        }

        std::vector<uint8_t> bytes;
        if (!ge_read_file_bytes(fp.string(), bytes)) {
            continue;
        }
        std::string canon;
        if (!GE_canonicalize_utf8_strict(bytes.data(), bytes.size(), canon, canon_stats)) {
            continue;
        }
        if (canon.empty()) continue;

        // Chunk deterministically.
        const size_t chunk_bytes = size_t(chunk_bytes_u32);
        const size_t total = canon.size();
        size_t off = 0;
        while (off < total) {
            const size_t take = (off + chunk_bytes <= total) ? chunk_bytes : (total - off);
            const uint8_t* chunk_ptr = reinterpret_cast<const uint8_t*>(canon.data() + off);
            const size_t chunk_len = take;

            SpiderCode4 sc{};
            if (!ew_encode_spidercode4_from_bytes_chunked_cuda(chunk_ptr, chunk_len, 4096u, &sc)) {
                std::fprintf(stderr, "ge_corpus_ingest: spidercode4_encode_failed rel=%s seq=%u off=%zu\n", rel.c_str(), seq, off);
                return 3;
            }

            // Map SpiderCode4 into 4 frequency components for deterministic carrier collapse.
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
            if (!ew_collapse_frequency_components_q32_32(comps, carrier)) {
                std::fprintf(stderr, "ge_corpus_ingest: carrier_collapse_failed rel=%s seq=%u off=%zu\n", rel.c_str(), seq, off);
                return 3;
            }

            // Store payload ref.
            uint64_t blob_off = 0;
            if (!ge_write_blob_append(blob, std::string(canon.data() + off, canon.data() + off + take), blob_off)) {
                std::fprintf(stderr, "ge_corpus_ingest: blob_write_failed\n");
                return 3;
            }

            GE_CorpusAnchorRecord r;
            r.lane_u8 = uint8_t(lane_u32);
            r.domain_id9 = pol->domain_id9;
            r.source_id9 = ew_id9_from_string_ascii(rel);
            r.seq_u32 = seq;
            r.offset_u32 = uint32_t(off);
            r.size_u32 = uint32_t(take);
            r.anchor_id9 = GE_anchor_id9_from_provenance_and_carrier(r.lane_u8, r.domain_id9, r.source_id9, r.seq_u32, r.offset_u32, carrier);
            r.sc4 = sc;
            r.carrier = carrier;
            r.payload_relpath_utf8 = "blobs.txt";
            r.payload_byte_off_u64 = blob_off;
            store.records.push_back(r);
            GE_CorpusPulseRecord pr;
            pr.lane_u8 = r.lane_u8;
            pr.domain_id9 = r.domain_id9;
            pr.source_id9 = r.source_id9;
            pr.seq_u32 = r.seq_u32;
            pr.offset_u32 = r.offset_u32;
            pr.size_u32 = r.size_u32;
            pr.sc4 = r.sc4;
            pr.carrier = r.carrier;
            pr.payload_relpath_utf8 = r.payload_relpath_utf8;
            pr.payload_byte_off_u64 = r.payload_byte_off_u64;
            pulse_log.records.push_back(std::move(pr));

            off += take;
        }
        seq++;
    }

    store.sort_and_dedupe();
    pulse_log.sort_stable();
    if (!store.save_to_file(store_path)) {
        std::fprintf(stderr, "ge_corpus_ingest: failed to save store: %s\n", store_path.c_str());
        return 2;
    }

    if (!pulse_log.save_to_file(pulses_path)) {
        std::fprintf(stderr, "ge_corpus_ingest: failed to save pulse log: %s\n", pulses_path.c_str());
        return 2;
    }


    // Minimal training epoch to form initial coherence edges.
    genesis::LearningCheckpointGate gate;
    GE_CoherenceGraphStore graph;
    GE_TrainerParams tp;
    tp.topk_u32 = topk_u32;
    GE_TrainerStats ts;
        tp.use_cuda_scoring = use_cuda;
    tp.opt_thresholds = &thresholds;
    for (uint32_t e = 0; e < epochs_u32; ++e) {
        tp.epoch_u32 = e;
        (void)GE_trainer_epoch(store, graph, gate, tp, ts);
    }

    if (!graph.save_to_file(graph_path)) {
        std::fprintf(stderr, "ge_corpus_ingest: failed to save graph: %s\n", graph_path.c_str());
        return 2;
    }

    // Emit deterministic stats.
    std::printf("GE_CORPUS_INGEST:records=%zu bytes_in=%llu bytes_out=%llu rejects_utf8=%llu\n",
                store.records.size(),
                (unsigned long long)canon_stats.bytes_in_u64,
                (unsigned long long)canon_stats.bytes_out_u64,
                (unsigned long long)canon_stats.invalid_utf8_rejects_u64);
    // Observables: graph size and saturation behavior.
    uint64_t edges_u64 = 0;
    uint32_t max_deg = 0;
    uint64_t saturated_nodes_u64 = 0;
    for (const auto& n : graph.nodes) {
        edges_u64 += (uint64_t)n.edges.size();
        if (n.edges.size() > max_deg) max_deg = (uint32_t)n.edges.size();
        if (tp.max_degree_u32 != 0u && n.edges.size() >= (size_t)tp.max_degree_u32) saturated_nodes_u64++;
    }

    std::printf("GE_TRAIN_EPOCH:proposals=%llu accepted=%llu last_rel_err_q32_32=%lld nodes=%zu edges=%llu max_deg=%u saturated_nodes=%llu edge_writes=%llu edge_write_cap=%llu\n",
                (unsigned long long)ts.proposals_u64,
                (unsigned long long)ts.accepted_u64,
                (long long)ts.last_rel_err_q32_32,
                graph.nodes.size(),
                (unsigned long long)edges_u64,
                (unsigned)max_deg,
                (unsigned long long)saturated_nodes_u64,
                (unsigned long long)ts.edge_writes_u64,
                (unsigned long long)tp.safety_caps.max_edge_writes_u64);

    return 0;
}
