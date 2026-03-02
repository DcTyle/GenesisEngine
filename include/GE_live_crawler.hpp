#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include "ew_id9.hpp"
#include "GE_domain_policy.hpp"
#include "GE_rate_limiter.hpp"

class SubstrateManager;

// Live crawling is off by default and requires explicit enabling.
// This module fetches ONLY http:// URLs (plaintext) without TLS.
// Determinism across runs is achieved through pulse logging and replay, not through network timing.
struct GE_LiveCrawlTarget {
    std::string url_ascii;
    std::string domain_ascii;
    EigenWare::EwId9 domain_id9{};
    EigenWare::EwId9 url_id9{};
    uint64_t seq_u64 = 0;
};

struct GE_LiveCrawler {
    bool enabled = false;
    // Explicit human consent token requirement gate.
    bool consent_granted = false;

    std::deque<GE_LiveCrawlTarget> q;
    uint64_t next_seq_u64 = 1;

    // Seeds: deterministic startup seeds can be loaded from allowlist/policy.
    void seed_default_roots(const GE_DomainPolicyTable& pol);

    // Add explicit target (from UI/CLI).
    void enqueue_url(const std::string& url_ascii);

    // Tick: fetch up to max_fetch_per_tick docs and submit observations.
    void tick(SubstrateManager* sm, const GE_DomainPolicyTable& pol, GE_RateLimiter* rl);

private:
    bool parse_http_url_(const std::string& url, std::string& out_domain, std::string& out_path, uint16_t& out_port) const;
    bool http_get_body_(const std::string& domain, uint16_t port, const std::string& path, uint32_t cap_bytes, std::string& out_body) const;
};
