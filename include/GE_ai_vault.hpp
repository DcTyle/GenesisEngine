#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace genesis {
class MetricTask;
}

class SubstrateManager;

struct SpiderCode4;

namespace genesis {

// -----------------------------------------------------------------------------
// AI Vault
//
// Deterministic persistence surface for AI artifacts.
//
// Canonical vs Ephemeral
// - Canonical: committed knowledge artifacts (allowlist/resonance/validated).
// - Ephemeral: scratch artifacts (failed validation / weak resonance). These are
//   retained for a bounded TTL + max-count and garbage-collected deterministically.
// -----------------------------------------------------------------------------

class AiVault {
public:
    AiVault() = default;

    void init_once(::SubstrateManager* sm);

    // Called from SubstrateManager::tick. Performs bounded deterministic GC.
    void tick_gc(::SubstrateManager* sm);

    // Persist an accepted metric task as a canonical experiment artifact.
    bool commit_metric_task(::SubstrateManager* sm, const genesis::MetricTask& t);

    // Persist a rejected metric task as an ephemeral artifact (failure).
    bool store_ephemeral_metric_task(::SubstrateManager* sm, const genesis::MetricTask& t);

    // Persist an allowlisted page ingestion as a canonical corpus artifact (no resonance gate).
    bool commit_allowlist_page(::SubstrateManager* sm,
                              const std::string& domain_ascii,
                              const std::string& url_ascii,
                              uint32_t anchor_id_u32,
                              uint32_t lane_u32,
                              uint32_t stage_u32,
                              const ::SpiderCode4& sc,
                              uint16_t harmonics_mean_q15,
                              uint32_t len_u32,
                              uint64_t topic_mask_u64);

    // For non-allowlisted exploratory pages: commit only if topic-mask resonance with
    // existing canonical knowledge exceeds the resonance gate.
    bool maybe_commit_resonant_page(::SubstrateManager* sm,
                                   const std::string& domain_ascii,
                                   const std::string& url_ascii,
                                   uint32_t anchor_id_u32,
                                   uint32_t lane_u32,
                                   uint32_t stage_u32,
                                   const ::SpiderCode4& sc,
                                   uint16_t harmonics_mean_q15,
                                   uint32_t len_u32,
                                   uint64_t topic_mask_u64);

    // Persist the SpeechBoot vocabulary artifact (local-only mini-pack).
    bool commit_speechboot_vocab(::SubstrateManager* sm,
                                const std::vector<std::string>& words_ascii,
                                uint32_t vocab_min_u32,
                                uint64_t tick_u64);

    uint64_t committed_experiment_count_u64() const { return committed_experiment_count_u64_; }
    uint64_t ephemeral_experiment_count_u64() const { return ephemeral_experiment_count_u64_; }
    uint64_t committed_allowlist_page_count_u64() const { return committed_allowlist_page_count_u64_; }
    uint64_t committed_resonant_page_count_u64() const { return committed_resonant_page_count_u64_; }

    // Last deterministic commit key for regression fingerprinting/visibility.
    // kind_u32: 0=none,1=metric,2=metric_fail,3=allowlist_page,4=resonant_page,5=speech_vocab
    uint64_t last_commit_key_u64() const { return last_commit_key_u64_; }
    uint32_t last_commit_kind_u32() const { return last_commit_kind_u32_; }

private:
    bool inited_ = false;

    // Root is relative to project root. Uses user’s preferred folder name.
    std::string root_utf8_;
    std::string canonical_dir_utf8_;
    std::string ephemeral_dir_utf8_;

    // Counters mirrored into SubstrateManager for UI.
    uint64_t committed_experiment_count_u64_ = 0;
    uint64_t ephemeral_experiment_count_u64_ = 0;
    uint64_t committed_allowlist_page_count_u64_ = 0;
    uint64_t committed_resonant_page_count_u64_ = 0;

    // Regression/visibility: last successful commit key + kind.
    uint64_t last_commit_key_u64_ = 0ull;
    uint32_t last_commit_kind_u32_ = 0u;

    // Canonical topic masks (bounded) used for resonance checks.
    std::vector<uint64_t> canonical_topic_masks_;

    // Deterministic dedupe keys (bounded, linear scan).
    std::vector<uint64_t> seen_allowlist_keys_;
    std::vector<uint64_t> seen_resonant_keys_;

    // Deterministic GC settings.
    // TTL in substrate ticks. Default: 21600 ticks (about 60s at 360 Hz).
    uint64_t ephemeral_ttl_ticks_u64_ = 21600ull;
    // Hard max number of ephemeral experiment artifacts.
    uint32_t ephemeral_max_u32_ = 256u;
    // Run GC only every N ticks (deterministic cadence).
    uint32_t gc_stride_u32_ = 360u;

    void ensure_dirs_();
    void recount_();

    bool write_metric_json_(::SubstrateManager* sm,
                           const std::string& dir_utf8,
                           const std::string& prefix_utf8,
                           const genesis::MetricTask& t,
                           std::string* out_written_path_utf8);

    void gc_ephemeral_(::SubstrateManager* sm);

    static bool vec_contains_u64_(const std::vector<uint64_t>& v, uint64_t x);
    static void vec_insert_bounded_u64_(std::vector<uint64_t>& v, uint64_t x, size_t cap);
};

} // namespace genesis
