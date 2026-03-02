#include "GE_domain_policy.hpp"
#include "GE_corpus_allowlist.hpp"
#include <algorithm>

static inline bool id9_less(const EigenWare::EwId9& a, const EigenWare::EwId9& b) {
    return a < b;
}

const GE_DomainPolicy* GE_DomainPolicyTable::find_by_domain_ascii(const std::string& domain_ascii) const {
    const EigenWare::EwId9 id = EigenWare::ew_id9_from_string_ascii(domain_ascii);
    return find_by_id9(id);
}

const GE_DomainPolicy* GE_DomainPolicyTable::find_by_id9(const EigenWare::EwId9& id9) const {
    // Binary search (rows must be finalized).
    size_t lo = 0, hi = rows.size();
    while (lo < hi) {
        const size_t mid = lo + ((hi - lo) >> 1);
        const GE_DomainPolicy& r = rows[mid];
        if (r.domain_id9 == id9) return &r;
        if (id9_less(r.domain_id9, id9)) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

void GE_DomainPolicyTable::build_from_allowlist(const GE_CorpusAllowlist& allow) {
    rows.clear();
    rows.reserve(allow.domains.size());
    for (const auto& e : allow.domains) {
        GE_DomainPolicy p{};
        p.domain_ascii = e.domain_ascii;
        p.domain_id9 = EigenWare::ew_id9_from_string_ascii(e.domain_ascii);
        p.lane_u32 = (uint32_t)e.lane_u8;
        // Default policies: offline-first.
        p.offline_only = true;
        p.req_per_sec_u32 = 0;
        p.max_body_bytes_u32 = 0;

        // Allow live http fetch for a conservative subset.
        // (No TLS here; https remains offline-only unless adapter implements TLS.)
        // Safe defaults: 1 req/sec, 2 MiB max body.
        if (e.domain_ascii == "khanacademy.org" || e.domain_ascii == "commoncrawl.org") {
            p.offline_only = false;
            p.allow_http = true;
            p.req_per_sec_u32 = 1;
            p.max_body_bytes_u32 = 2u * 1024u * 1024u;
        }
        rows.push_back(p);
    }
    finalize_deterministic();
}

void GE_DomainPolicyTable::finalize_deterministic() {
    std::stable_sort(rows.begin(), rows.end(), [](const GE_DomainPolicy& a, const GE_DomainPolicy& b) {
        if (a.domain_id9 != b.domain_id9) return a.domain_id9 < b.domain_id9;
        return a.domain_ascii < b.domain_ascii;
    });
    // Unique by domain_id9 (keep first deterministically).
    std::vector<GE_DomainPolicy> out;
    out.reserve(rows.size());
    EigenWare::EwId9 prev{};
    bool have_prev = false;
    for (const auto& r : rows) {
        if (have_prev && r.domain_id9 == prev) continue;
        out.push_back(r);
        prev = r.domain_id9;
        have_prev = true;
    }
    rows.swap(out);
}
