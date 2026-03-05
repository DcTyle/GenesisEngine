#include "GE_rate_limiter.hpp"
#include <algorithm>

static inline const GE_DomainBucket* find_bucket_impl(const std::vector<GE_DomainBucket>& b, const EigenWare::EwId9& id) {
    size_t lo = 0, hi = b.size();
    while (lo < hi) {
        const size_t mid = lo + ((hi - lo) >> 1);
        const GE_DomainBucket& r = b[mid];
        if (r.domain_id9 == id) return &r;
        if (r.domain_id9 < id) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}
static inline GE_DomainBucket* find_bucket_mut(std::vector<GE_DomainBucket>& b, const EigenWare::EwId9& id) {
    return const_cast<GE_DomainBucket*>(find_bucket_impl(b, id));
}

void GE_RateLimiter::configure_from_policies(const GE_DomainPolicyTable& pol) {
    buckets.clear();
    buckets.reserve(pol.rows.size());
    for (const auto& p : pol.rows) {
        GE_DomainBucket b{};
        b.domain_id9 = p.domain_id9;
        b.req_per_sec_u32 = p.req_per_sec_u32;
        b.next_allowed_tick_u64 = 0;
        buckets.push_back(b);
    }
    std::stable_sort(buckets.begin(), buckets.end(), [](const GE_DomainBucket& a, const GE_DomainBucket& b){
        return a.domain_id9 < b.domain_id9;
    });
}

bool GE_RateLimiter::admit_request(const EigenWare::EwId9& domain_id9, uint64_t tick_u64) {
    GE_DomainBucket* b = find_bucket_mut(buckets, domain_id9);
    if (!b) return false;
    if (b->req_per_sec_u32 == 0) return false;
    if (tick_u64 < b->next_allowed_tick_u64) return false;
    // 1 req/sec => next_allowed = tick+1. For N req/sec, allow multiple within same tick? We avoid sub-tick clocks here.
    // Deterministic conservative policy: next_allowed = tick + 1 always (regardless of N) until sub-tick is implemented.
    b->next_allowed_tick_u64 = tick_u64 + 1;
    return true;
}

const GE_DomainBucket* GE_RateLimiter::find_bucket(const EigenWare::EwId9& domain_id9) const {
    return find_bucket_impl(buckets, domain_id9);
}
