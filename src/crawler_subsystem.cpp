#include "crawler_subsystem.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "anchor.hpp"
#include "crawler_encode_cuda.hpp"
#include "delta_profiles.hpp"
#include "GE_corpus_allowlist.hpp"
#include "GE_runtime.hpp"
#include "text_encoder.hpp"

#include "ew_id9.hpp"

// Host pinned buffer (DMA-friendly). This is orchestration-only; all compute
// remains GPU-side.
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
#include <cuda_runtime_api.h>
#endif

CrawlerSubsystem::CrawlerSubsystem() {
    stats_ = {};
    stats_.last_coord_id9 = EigenWare::EwId9{};

    pinned_bytes_ = nullptr;
    pinned_cap_bytes_ = 0;
    pinned_is_cuda_ = false;
}

CrawlerSubsystem::~CrawlerSubsystem() {
    if (pinned_bytes_) {
        if (pinned_is_cuda_) {
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
            cudaFreeHost(pinned_bytes_);
#else
            std::free(pinned_bytes_);
#endif
        } else {
            std::free(pinned_bytes_);
        }
        pinned_bytes_ = nullptr;
        pinned_cap_bytes_ = 0;
        pinned_is_cuda_ = false;
    }
}

bool CrawlerSubsystem::ensure_pinned_capacity_bytes_(size_t need_cap_bytes) {
    if (need_cap_bytes <= pinned_cap_bytes_ && pinned_bytes_) return true;

    // Grow to next power-of-two-ish capacity for amortized reuse.
    size_t new_cap = 1;
    while (new_cap < need_cap_bytes) {
        new_cap <<= 1;
        if (new_cap == 0) break;
    }
    if (new_cap < need_cap_bytes) new_cap = need_cap_bytes;

    // Release old.
    if (pinned_bytes_) {
        if (pinned_is_cuda_) {
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
            cudaFreeHost(pinned_bytes_);
#else
            std::free(pinned_bytes_);
#endif
        } else {
            std::free(pinned_bytes_);
        }
        pinned_bytes_ = nullptr;
        pinned_cap_bytes_ = 0;
        pinned_is_cuda_ = false;
    }

    void* p = nullptr;
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
    const cudaError_t ce = cudaHostAlloc(&p, new_cap, cudaHostAllocPortable);
    if (ce == cudaSuccess && p) {
        pinned_bytes_ = (uint8_t*)p;
        pinned_cap_bytes_ = new_cap;
        pinned_is_cuda_ = true;
        return true;
    }
#endif
    p = std::malloc(new_cap);
    if (p) {
        pinned_bytes_ = (uint8_t*)p;
        pinned_cap_bytes_ = new_cap;
        pinned_is_cuda_ = false;
        return true;
    }
    return false;
}

void CrawlerSubsystem::enqueue_observation_utf8(
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
) {
    CrawlerObs o{};
    o.artifact_id_u64 = artifact_id_u64;
    o.target_anchor_id_u32 = target_anchor_id_u32;
    o.crawler_anchor_id_u32 = crawler_anchor_id_u32;
    o.context_anchor_id_u32 = context_anchor_id_u32;
    o.stream_id_u32 = stream_id_u32;
    o.extractor_id_u32 = extractor_id_u32;
    o.trust_class_u32 = trust_class_u32;
    o.causal_tag_u32 = causal_tag_u32;
    o.domain_ascii = domain_ascii;
    o.url_ascii = url_ascii;
    o.domain_id9 = EigenWare::ew_id9_from_string_ascii(domain_ascii);
    o.url_id9 = EigenWare::ew_id9_from_string_ascii(url_ascii);
    o.utf8 = utf8;
    q_.push_back(o);
    stats_.enqueued_obs_u64 += 1;
    stats_.pending_obs_u32 = (uint32_t)q_.size();
    stats_.last_utf8_bytes_u32 =
        (utf8.size() > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)utf8.size();
}

EigenWare::EwId9 CrawlerSubsystem::id9_from_spidercode4_(uint16_t f, uint16_t a, uint16_t v, uint16_t i) {
    EigenWare::EwId9 id;
    id.u32[0] = ((uint32_t)f) | (((uint32_t)a) << 16);
    id.u32[1] = ((uint32_t)v) | (((uint32_t)i) << 16);
    id.u32[8] = 2u;
    return id;
}

// NOTE: Crawler ingestion is GPU-driven. We do not do CPU text segmentation or
// encoding. The only CPU responsibility here is deterministic batching and
// scheduling (queue order + size caps), then dispatching the GPU encoder.

void CrawlerSubsystem::tick(SubstrateManager* sm) {
    if (!sm) return;

    // Deterministic queue ordering: stable sort by (domain_id9, url_id9, artifact_id, causal_tag).
    // Note: q_ is a deque; convert to vector for sorting when size > 1.
    if (q_.size() > 1) {
        std::vector<CrawlerObs> tmp(q_.begin(), q_.end());
        std::stable_sort(tmp.begin(), tmp.end(), [](const CrawlerObs& a, const CrawlerObs& b) {
            if (a.domain_id9 != b.domain_id9) return a.domain_id9 < b.domain_id9;
            if (a.url_id9 != b.url_id9) return a.url_id9 < b.url_id9;
            if (a.artifact_id_u64 != b.artifact_id_u64) return a.artifact_id_u64 < b.artifact_id_u64;
            return a.causal_tag_u32 < b.causal_tag_u32;
        });
        q_.clear();
        for (const auto& o : tmp) q_.push_back(o);
    }

    // Per-tick budget for crawler admission.
    const uint32_t max_pulses = sm->crawler_max_pulses_per_tick_u32;
    uint32_t admitted = 0;

    // Chunk-stream mode is enabled only when the learning backlog is below
    // the configured threshold. This prevents the crawler from outpacing the
    // visualization/integration pipeline.
    const uint32_t backlog = sm->learning_gate.registry().pending_count_u32();
    const bool allow_visible_chunk_stream = sm->visualization_headless || (sm->crawler_allow_chunk_stream_when_visible_u32 != 0u);
    // Chunk-stream backlog threshold is derived from the learning window unless explicitly set.
    const uint32_t thr_cfg = sm->crawler_chunk_stream_backlog_threshold_u32;
    const uint32_t thr = (thr_cfg == 0u) ? sm->derived_crawler_chunk_stream_backlog_threshold_u32() : thr_cfg;
    const bool chunk_stream_enabled = allow_visible_chunk_stream &&
                                      (sm->crawler_emit_chunk_stream_u32 != 0u) &&
                                      (backlog < thr);
    const uint32_t max_chunks_cfg = sm->crawler_max_chunks_per_obs_u32;
    const uint32_t max_chunks = (max_chunks_cfg == 0u) ? sm->derived_crawler_max_chunks_per_obs_u32() : max_chunks_cfg;
    const uint32_t per_obs_pulses = chunk_stream_enabled ? (1u + max_chunks) : 1u;
    const uint32_t max_docs_this_tick = (per_obs_pulses == 0u) ? 0u : (max_pulses / per_obs_pulses);

    // Batch observations to encode in parallel on GPU.
    // - FIFO ordering determines batching order
    // - total concat cap ensures bounded staging
    const size_t max_concat = (size_t)sm->derived_crawler_concat_cap_bytes_u64();
    std::vector<CrawlerObs> batch;
    batch.reserve((size_t)max_pulses);
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> lengths;
    offsets.reserve((size_t)max_pulses);
    lengths.reserve((size_t)max_pulses);

    if (!ensure_pinned_capacity_bytes_(max_concat)) {
        // Out of host memory; drop admission this tick deterministically.
        return;
    }
    size_t concat_sz = 0;

    // Allowlist enforcement: reject observations whose domain is not allowed.
    const GE_CorpusAllowlist* allow = sm->corpus_allowlist_ptr();

    while (!q_.empty() && batch.size() < (size_t)((max_docs_this_tick == 0u) ? max_pulses : max_docs_this_tick)) {
        const CrawlerObs& o = q_.front();
        if (allow && !allow->is_domain_allowed(o.domain_ascii)) {
            q_.pop_front();
            stats_.dropped_obs_u64 += 1;
            continue;
        }
        const size_t need = o.utf8.size();
        if (need == 0) {
            q_.pop_front();
            stats_.dropped_obs_u64 += 1;
            continue;
        }
        if (concat_sz + need > max_concat) {
            // Defer remaining items to next tick.
            break;
        }
        offsets.push_back((uint32_t)concat_sz);
        lengths.push_back((uint32_t)need);
        std::memcpy(pinned_bytes_ + concat_sz, o.utf8.data(), need);
        concat_sz += need;
        batch.push_back(o);
        q_.pop_front();
    }

    if (!batch.empty()) {
        const size_t n_docs = batch.size();
        const uint32_t cb_cfg = sm->crawler_chunk_bytes_u32;
        const size_t cb = (size_t)((cb_cfg == 0u) ? sm->derived_crawler_chunk_bytes_u32() : cb_cfg);
        if (!chunk_stream_enabled) {
            std::vector<SpiderCode4> out_sc(n_docs);
            std::vector<uint16_t> out_h((size_t)n_docs * (size_t)Anchor::HARMONICS_N);
            std::vector<uint16_t> out_h_mean((size_t)n_docs);
            const bool ok = ew_encode_spidercode4_and_harmonics32_from_bytes_batch_cuda(
                (const uint8_t*)pinned_bytes_,
                concat_sz,
                offsets.data(),
                lengths.data(),
                (uint32_t)n_docs,
                cb,
                out_sc.data(),
                out_h.data(),
                out_h_mean.data()
            );
            if (!ok) {
                stats_.dropped_obs_u64 += (uint64_t)n_docs;
            } else {
                for (size_t bi = 0; bi < n_docs; ++bi) {
                    const CrawlerObs& o = batch[bi];
                    const uint32_t anchor_id = o.target_anchor_id_u32;
                    if (anchor_id >= sm->anchors.size()) {
                        // Fail closed: never redirect to a "default" anchor.
                        stats_.dropped_obs_u64 += 1;
                        continue;
                    }
                    const SpiderCode4 sc = out_sc[bi];
                    Pulse p{};
                    p.anchor_id = anchor_id;
                    p.f_code = sc.f_code;
                    p.a_code = sc.a_code;
                    p.v_code = sc.v_code;
                    p.i_code = sc.i_code;
                    p.profile_id = (uint8_t)EW_PROFILE_CRAWLER_INGESTION;
                    p.causal_tag = (uint8_t)(o.causal_tag_u32 & 0xFFU);
                    p.pad0 = 0;
                    p.pad1 = 0;
                    p.tick = sm->canonical_tick_u64();
                    sm->enqueue_inbound_pulse(p);

                    // Write harmonic artifact into the anchor (GPU-derived; CPU only assigns).
                    if (anchor_id < sm->anchors.size()) {
                        Anchor& A = sm->anchors[anchor_id];
                        const size_t base = bi * (size_t)Anchor::HARMONICS_N;
                        for (uint32_t k = 0; k < Anchor::HARMONICS_N; ++k) {
                            A.harmonics_q15[k] = out_h[base + (size_t)k];
                        }
                        A.harmonics_mean_q15 = out_h_mean[bi];
                        A.harmonics_epoch_u32 += 1u;
                    }

                    stats_.last_coord_id9 = id9_from_spidercode4_(p.f_code, p.a_code, p.v_code, p.i_code);

                    admitted += 1;
                    stats_.admitted_pulses_u64 += 1;
                }
            }
        } else {
            const uint32_t mcpd = max_chunks;
            std::vector<SpiderCode4> page_sc(n_docs);
            std::vector<SpiderCode4> chunk_sc(n_docs * (size_t)mcpd);
            std::vector<uint32_t> chunk_counts(n_docs);
            const bool ok = ew_encode_spidercode4_page_and_chunks_batch_cuda(
                (const uint8_t*)pinned_bytes_,
                concat_sz,
                offsets.data(),
                lengths.data(),
                (uint32_t)n_docs,
                cb,
                mcpd,
                page_sc.data(),
                chunk_sc.data(),
                chunk_counts.data()
            );
            if (!ok) {
                stats_.dropped_obs_u64 += (uint64_t)n_docs;
            } else {
                for (size_t bi = 0; bi < n_docs; ++bi) {
                    if (admitted >= max_pulses) break;
                    const CrawlerObs& o = batch[bi];
                    uint32_t anchor_id = o.target_anchor_id_u32;
                    if (anchor_id >= sm->anchors.size()) anchor_id = (sm->anchors.empty()) ? 0U : 0U;
                    // Page-level pulse first.
                    {
                        const SpiderCode4 sc = page_sc[bi];
                        Pulse p{};
                        p.anchor_id = anchor_id;
                        p.f_code = sc.f_code;
                        p.a_code = sc.a_code;
                        p.v_code = sc.v_code;
                        p.i_code = sc.i_code;
                        p.profile_id = (uint8_t)EW_PROFILE_CRAWLER_INGESTION;
                        p.causal_tag = (uint8_t)(o.causal_tag_u32 & 0xFFU);
                        p.pad0 = 0;
                        p.pad1 = 0;
                        p.tick = sm->canonical_tick_u64();
                        sm->enqueue_inbound_pulse(p);
                        admitted += 1;
                        stats_.admitted_pulses_u64 += 1;
                    }
                    // Chunk stream pulses (bounded).
                    const uint32_t n_chunks = (chunk_counts[bi] > mcpd) ? mcpd : chunk_counts[bi];
                    for (uint32_t ci = 0; ci < n_chunks && admitted < max_pulses; ++ci) {
                        const SpiderCode4 sc = chunk_sc[bi * (size_t)mcpd + (size_t)ci];
                        Pulse p{};
                        p.anchor_id = anchor_id;
                        p.f_code = sc.f_code;
                        p.a_code = sc.a_code;
                        p.v_code = sc.v_code;
                        p.i_code = sc.i_code;
                        p.profile_id = (uint8_t)EW_PROFILE_CRAWLER_INGESTION;
                        // causal_tag encodes chunk index for deterministic ordering.
                        p.causal_tag = (uint8_t)(ci & 0xFFu);
                        p.pad0 = 0;
                        p.pad1 = 0;
                        p.tick = sm->canonical_tick_u64();
                        sm->enqueue_inbound_pulse(p);
                        admitted += 1;
                        stats_.admitted_pulses_u64 += 1;
                    }
                }
            }
        }
    }

    stats_.last_tick_u64 = sm->canonical_tick_u64();
    stats_.pending_obs_u32 = (uint32_t)q_.size();
}
