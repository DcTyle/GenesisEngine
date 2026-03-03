#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ew_id9.hpp"
#include "GE_metric_registry.hpp"

namespace genesis {

// -----------------------------------------------------------------------------
// Language Foundations
//
// Deterministic language substrate required before science curriculum.
// Loads:
//  - CMU Pronouncing Dictionary (cmudict)
//  - WordNet (index.* + data.*) as dictionary + thesaurus semantic graph
//  - Encyclopedia abstracts (Wikipedia abstracts XML or TSV)
//  - Speech corpus metadata (Common Voice TSV) for text/audio alignment tracking
//
// Notes:
// - This module is CPU-side orchestration/state only. It produces measurable
//   checkpoints and exposes metrics to the learning gate. It does not perform
//   any non-deterministic IO or network activity; all IO is explicit and user-
//   triggered through SubstrateMicroprocessor::language_bootstrap_from_dir.
// - Storage is ID9-vector based and bounded for stability.
// -----------------------------------------------------------------------------

struct LangLexiconStats {
    uint32_t word_count_u32 = 0;
    uint32_t pron_count_u32 = 0;
    uint32_t senses_count_u32 = 0;
    uint32_t relations_count_u32 = 0;
    uint32_t concept_count_u32 = 0;
    uint32_t speech_utt_count_u32 = 0;
    uint32_t speech_word_tokens_u32 = 0;
};

struct LangMetricVectorV1 {
    // Q32.32 values
    int64_t v_q32_32[genesis::GENESIS_METRIC_DIM_MAX] = {0};
    uint32_t dim_u32 = 0;
};

class LanguageFoundation {
public:
    LanguageFoundation();

    // Load datasets from a directory. Returns true if anything loaded.
    // Expected file layout (all optional; the loader is robust):
    //   <root>/cmudict/cmudict-*.txt
    //   <root>/wordnet/dict/index.* and data.*
    //   <root>/encyclopedia/enwiki-abstract*.xml or *.tsv
    //   <root>/speech/commonvoice/*.tsv (validated.tsv or train.tsv)
    bool bootstrap_from_dir(const std::string& root_dir_utf8, std::string* out_report_utf8);

    const LangLexiconStats& stats() const { return stats_; }

    // Produce a simulation metric vector for a given language checkpoint kind.
    // The returned vector is derived from loaded corpora and internal counts.
    genesis::MetricVector metrics_for_kind(genesis::MetricKind k) const;

    // Build a MetricTask target for a given language checkpoint.
    // Targets are derived from authoritative corpora counts, so correctness is
    // validated by strict equality (within tolerance, which should become 0).
    genesis::MetricTask make_task_for_kind(genesis::MetricKind k,
                                          uint64_t source_id_u64,
                                          uint32_t source_anchor_id_u32,
                                          uint32_t context_anchor_id_u32) const;

private:
    LangLexiconStats stats_;

    // ID9 sets (bounded) to provide stable counts without hashing.
    // We store 9D coordinate IDs derived by vectorized ASCII packing.
    std::vector<EigenWare::EwId9> word_ids_;
    std::vector<EigenWare::EwId9> pron_ids_;
    std::vector<EigenWare::EwId9> sense_ids_;
    std::vector<EigenWare::EwId9> relation_ids_;
    std::vector<EigenWare::EwId9> concept_ids_;

    // Helpers
    static void vec_insert_unique_bounded(std::vector<EigenWare::EwId9>& v, const EigenWare::EwId9& x, size_t cap);

    bool load_cmudict_dir(const std::string& dir_utf8, std::string* rep);
    bool load_wordnet_dir(const std::string& dir_utf8, std::string* rep);
    bool load_encyclopedia_dir(const std::string& dir_utf8, std::string* rep);
    bool load_speech_dir(const std::string& dir_utf8, std::string* rep);

    // Parsers
    void parse_cmudict_file(const std::string& path_utf8, std::string* rep);
    void parse_wordnet_index_file(const std::string& path_utf8, std::string* rep);
    void parse_wordnet_data_file(const std::string& path_utf8, std::string* rep);
    void parse_wiki_abstracts_xml_file(const std::string& path_utf8, std::string* rep);
    void parse_wiki_abstracts_tsv_file(const std::string& path_utf8, std::string* rep);
    void parse_common_voice_tsv_file(const std::string& path_utf8, std::string* rep);

    static bool ends_with(const std::string& s, const std::string& suf);
};

} // namespace genesis
