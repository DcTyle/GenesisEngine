#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ew_id9.hpp"

// Domain policy derived from the corpus allowlist.
// No hashing/crypto IDs; domain_id9 is a 9D coordinate derived from ASCII.
struct GE_DomainPolicy {
    EigenWare::EwId9 domain_id9{};
    std::string domain_ascii;
    uint32_t lane_u32 = 0;
    // Requests per second for live crawling. 0 => disallow live network.
    uint32_t req_per_sec_u32 = 0;
    // Maximum bytes admitted per response body (cap before encoding).
    uint32_t max_body_bytes_u32 = 0;
    // If true, the domain is offline-only (dumps, local corpora).
    bool offline_only = true;
    // If true, allow http:// (plaintext) live fetch. https requires separate TLS module.
    bool allow_http = false;
};

struct GE_DomainPolicyTable {
    std::vector<GE_DomainPolicy> rows;

    const GE_DomainPolicy* find_by_domain_ascii(const std::string& domain_ascii) const;
    const GE_DomainPolicy* find_by_id9(const EigenWare::EwId9& id9) const;

    // Deterministically build from a parsed allowlist.
    void build_from_allowlist(const struct GE_CorpusAllowlist& allow);

    // Stable sort + unique by domain_id9.
    void finalize_deterministic();
};
