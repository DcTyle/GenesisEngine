#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ew_id9.hpp"

// Corpus allowlist policy derived from docs/neuralis_corpus_domains_expanded_v2.md.
// Determinism rules:
// - Parsing is line-based and locale-free.
// - Domains are ASCII-lowered.
// - Output ordering is stable: (lane_u8, domain_ascii, domain_id9).
//
// Note: This allowlist is a *routing + safety* structure. Rate limits and
// licensing constraints can be refined by a separate policy layer, but the
// fields exist here so the runtime can enforce them deterministically.

struct GE_CorpusDomainPolicy {
    EwId9 domain_id9{};
    std::string domain_ascii;
    uint8_t lane_u8 = 0;

    // Ingestion modes
    bool allow_live_http = false;     // live network fetch permitted (still requires explicit enable at runtime)
    bool allow_offline = true;        // offline dumps/files permitted
    bool offline_only = false;        // if true, live network must be rejected
    bool respect_robots = true;       // policy flag; enforcement occurs in the live crawler adapter
    bool respect_terms = true;        // policy flag (ToS / usage constraints)
    bool license_filter_required = true; // require license metadata filtering for artifacts

    // Rate limiting (fixed-point friendly)
    // tokens_per_sec_q16_16 = tokens_per_sec * 65536.
    uint32_t tokens_per_sec_q16_16 = (1u << 16);
    uint32_t burst_tokens_u32 = 1;

    // Back-compat fields (kept for older callers; mirrored from q16_16).
    uint32_t rate_tokens_per_sec_u32 = 1;
    uint32_t rate_burst_u32 = 1;
    uint32_t target_pages_u32 = 0;
};

struct GE_CorpusAllowlist {
    std::vector<GE_CorpusDomainPolicy> domains;
    const GE_CorpusDomainPolicy* find_by_domain_ascii(const std::string& domain_ascii) const;
    const GE_CorpusDomainPolicy* find_by_domain_id9(const EwId9& id9) const;
    bool is_domain_allowed(const std::string& domain_ascii) const;
};

bool GE_load_corpus_allowlist_from_md(const std::string& md_path_utf8, GE_CorpusAllowlist& out);
bool GE_load_corpus_allowlist_from_md_text(const std::string& md_text_utf8, GE_CorpusAllowlist& out);
