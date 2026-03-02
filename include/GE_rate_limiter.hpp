#pragma once
#include <cstdint>
#include <vector>
#include "ew_id9.hpp"
#include "GE_domain_policy.hpp"

// Deterministic per-domain token bucket.
// Timebase is substrate canonical_tick (ticks as seconds unless scaled by adapter).
struct GE_DomainBucket {
    EigenWare::EwId9 domain_id9{};
    uint64_t next_allowed_tick_u64 = 0;
    uint32_t req_per_sec_u32 = 0;
};

struct GE_RateLimiter {
    std::vector<GE_DomainBucket> buckets;

    void configure_from_policies(const GE_DomainPolicyTable& pol);

    // Returns true if request is allowed at this tick; updates next_allowed_tick_u64.
    bool admit_request(const EigenWare::EwId9& domain_id9, uint64_t tick_u64);

    const GE_DomainBucket* find_bucket(const EigenWare::EwId9& domain_id9) const;
};
