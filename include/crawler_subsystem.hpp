#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "ew_id9.hpp"

// Crawler Subsystem (Spec Section 5).
// The crawler is a simulated module inside the substrate.
// It does not fetch network content or write files. External worlds provide
// observations (e.g., UTF-8 text fragments) through enqueue calls.
// The crawler deterministically segments observations and converts them into
// pulse candidates using the existing encoder path and the crawler ingestion
// profile. Injection into the manifold happens only through the same pulse
// admission path used by all other inputs.

struct CrawlerObs {
    uint64_t artifact_id_u64;
    // Target anchor within the crawler's context.
    uint32_t target_anchor_id_u32;
    // Owning crawler anchor id.
    uint32_t crawler_anchor_id_u32;
    // Owning context anchor id.
    uint32_t context_anchor_id_u32;
    uint32_t stream_id_u32;
    uint32_t extractor_id_u32;
    uint32_t trust_class_u32;
    uint32_t causal_tag_u32;
    // Deterministic routing (no hashing): 9D IDs derived from ASCII.
    EigenWare::EwId9 domain_id9;
    EigenWare::EwId9 url_id9;
    std::string domain_ascii;
    std::string url_ascii;
    std::string utf8;
};

struct CrawlerStats {
    uint64_t enqueued_obs_u64;
    uint64_t admitted_pulses_u64;
    uint64_t truncated_segments_u64;
    uint64_t dropped_obs_u64;
    uint64_t last_tick_u64;
    uint32_t pending_obs_u32 = 0;
    uint32_t last_utf8_bytes_u32 = 0;
    EigenWare::EwId9 last_coord_id9;
};

class SubstrateManager;

class CrawlerSubsystem {
public:
    explicit CrawlerSubsystem();
    ~CrawlerSubsystem();

    void enqueue_observation_utf8(
        uint64_t artifact_id_u64,
        uint32_t target_anchor_id_u32,
        uint32_t crawler_anchor_id_u32,
        uint32_t context_anchor_id_u32,
        uint32_t stream_id_u32,
        uint32_t extractor_id_u32,
        uint32_t trust_class_u32,
        uint32_t causal_tag_u32,
        const std::string& domain_ascii,
        const std::string& url_ascii,
        const std::string& utf8
    );

    // Called by the substrate tick. Deterministically consumes queued
    // observations, generates pulse candidates, and injects them into the
    // substrate's inbound pulse queue up to the per-tick crawler budget.
    void tick(SubstrateManager* sm);

    const CrawlerStats& stats() const { return stats_; }

private:
    std::deque<CrawlerObs> q_;
    CrawlerStats stats_;

    // Pinned host staging arena (RAM ingress).
    // Note: The crawler performs no CPU compute. This buffer exists only to
    // provide a deterministic, contiguous byte arena for DMA upload / CUDA
    // batch encode calls.
    uint8_t* pinned_bytes_;
    size_t pinned_cap_bytes_;
    bool pinned_is_cuda_;

    // Deterministic segmentation: split on 2+ newlines, then cap segment size.
    // Returns number of segments emitted into out[].
    // CPU text segmentation is not used in canonical crawler ingestion.
    // Crawler encoding is GPU-driven and operates on raw byte streams.

    // No hashing/crypto: pack spidercode into a 9D coordinate.
    static EigenWare::EwId9 id9_from_spidercode4_(uint16_t f, uint16_t a, uint16_t v, uint16_t i);

    bool ensure_pinned_capacity_bytes_(size_t need_cap_bytes);
};
