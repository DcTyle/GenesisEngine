#include "GE_live_crawler.hpp"
#include "GE_runtime.hpp"
#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static inline void stable_sort_queue(std::deque<GE_LiveCrawlTarget>& q) {
    if (q.size() <= 1) return;
    std::vector<GE_LiveCrawlTarget> tmp(q.begin(), q.end());
    std::stable_sort(tmp.begin(), tmp.end(), [](const GE_LiveCrawlTarget& a, const GE_LiveCrawlTarget& b){
        if (a.domain_id9 != b.domain_id9) return a.domain_id9 < b.domain_id9;
        if (a.url_id9 != b.url_id9) return a.url_id9 < b.url_id9;
        return a.seq_u64 < b.seq_u64;
    });
    q.clear();
    for (auto& t : tmp) q.push_back(t);
}

void GE_LiveCrawler::seed_default_roots(const GE_DomainPolicyTable& pol) {
    // Conservative seeds; only domains that allow_http and are not offline_only.
    for (const auto& r : pol.rows) {
        if (r.offline_only) continue;
        if (!r.allow_http) continue;
        // Default to http://domain/
        enqueue_url(std::string("http://") + r.domain_ascii + "/");
    }
}

void GE_LiveCrawler::enqueue_url(const std::string& url_ascii) {
    // Only accept http://
    if (url_ascii.rfind("http://", 0) != 0) return;
    std::string dom, path;
    uint16_t port = 80;
    if (!parse_http_url_(url_ascii, dom, path, port)) return;
    GE_LiveCrawlTarget t{};
    t.url_ascii = url_ascii;
    t.domain_ascii = dom;
    t.domain_id9 = EigenWare::ew_id9_from_string_ascii(dom);
    t.url_id9 = EigenWare::ew_id9_from_string_ascii(url_ascii);
    t.seq_u64 = next_seq_u64++;
    q.push_back(t);
}

bool GE_LiveCrawler::parse_http_url_(const std::string& url, std::string& out_domain, std::string& out_path, uint16_t& out_port) const {
    if (url.rfind("http://", 0) != 0) return false;
    std::string rest = url.substr(7);
    const size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out_path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    out_port = 80;
    const size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
        out_domain = hostport.substr(0, colon);
        const std::string port_s = hostport.substr(colon + 1);
        const int p = std::atoi(port_s.c_str());
        if (p <= 0 || p > 65535) return false;
        out_port = (uint16_t)p;
    } else {
        out_domain = hostport;
    }
    if (out_domain.empty()) return false;
    return true;
}

bool GE_LiveCrawler::http_get_body_(const std::string& domain, uint16_t port, const std::string& path, uint32_t cap_bytes, std::string& out_body) const {
#if defined(_WIN32)
    static bool wsa_inited = false;
    if (!wsa_inited) {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2,2), &w) != 0) return false;
        wsa_inited = true;
    }
#endif
    struct addrinfo hints{};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portbuf[16];
    std::snprintf(portbuf, sizeof(portbuf), "%u", (unsigned)port);

    struct addrinfo* res = nullptr;
    if (getaddrinfo(domain.c_str(), portbuf, &hints, &res) != 0 || !res) return false;

    int sockfd = -1;
    struct addrinfo* it = res;
    for (; it; it = it->ai_next) {
        sockfd = (int)socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sockfd < 0) continue;
        if (connect(sockfd, it->ai_addr, (int)it->ai_addrlen) == 0) break;
#if defined(_WIN32)
        closesocket(sockfd);
#else
        close(sockfd);
#endif
        sockfd = -1;
    }
    freeaddrinfo(res);
    if (sockfd < 0) return false;

    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + domain + "\r\nConnection: close\r\nUser-Agent: GenesisEngineCrawler/1\r\nAccept: text/html,text/plain,*/*\r\n\r\n";
    const char* rbuf = req.c_str();
    size_t remaining = req.size();
    while (remaining) {
#if defined(_WIN32)
        int n = send(sockfd, rbuf, (int)remaining, 0);
#else
        ssize_t n = send(sockfd, rbuf, remaining, 0);
#endif
        if (n <= 0) break;
        rbuf += n;
        remaining -= (size_t)n;
    }

    std::string resp;
    resp.reserve((cap_bytes ? cap_bytes : 65536u) + 1024u);
    char buf[8192];
    while (resp.size() < (size_t)cap_bytes) {
#if defined(_WIN32)
        int n = recv(sockfd, buf, (int)sizeof(buf), 0);
#else
        ssize_t n = recv(sockfd, buf, sizeof(buf), 0);
#endif
        if (n <= 0) break;
        size_t take = (size_t)n;
        if (resp.size() + take > (size_t)cap_bytes) take = (size_t)cap_bytes - resp.size();
        resp.append(buf, buf + take);
        if (take < (size_t)n) break;
    }

#if defined(_WIN32)
    closesocket(sockfd);
#else
    close(sockfd);
#endif

    // Split headers/body at first \r\n\r\n
    const std::string sep = "\r\n\r\n";
    const size_t pos = resp.find(sep);
    if (pos == std::string::npos) {
        out_body = resp;
    } else {
        out_body = resp.substr(pos + sep.size());
    }
    return true;
}

void GE_LiveCrawler::tick(SubstrateManager* sm, const GE_DomainPolicyTable& pol, GE_RateLimiter* rl) {
    if (!enabled || !consent_granted) return;
    if (!sm || !rl) return;

    // Deterministic queue order.
    stable_sort_queue(q);

    const uint32_t max_fetch = 2u; // conservative
    uint32_t fetched = 0;

    while (!q.empty() && fetched < max_fetch) {
        const GE_LiveCrawlTarget t = q.front();
        q.pop_front();

        const GE_DomainPolicy* p = pol.find_by_id9(t.domain_id9);
        if (!p) continue;
        if (p->offline_only) continue;
        if (!p->allow_http) continue;

        if (!rl->admit_request(t.domain_id9, sm->canonical_tick)) {
            // Not allowed yet; requeue at back deterministically.
            q.push_back(t);
            break;
        }

        std::string dom, path;
        uint16_t port = 80;
        if (!parse_http_url_(t.url_ascii, dom, path, port)) continue;
        std::string body;
        const uint32_t cap = (p->max_body_bytes_u32 == 0u) ? (512u * 1024u) : p->max_body_bytes_u32;
        if (!http_get_body_(dom, port, path, cap, body)) continue;

        // Emit as a crawler observation into the substrate. target anchor id is 0 for now.
        sm->crawler.enqueue_observation_utf8(
            /*artifact_id*/ (uint64_t)t.seq_u64,
            /*target_anchor_id*/ (sm->ui_livecrawl_target_anchor_idx_u32 ? sm->ui_livecrawl_target_anchor_idx_u32 : 0u),
            /*crawler_anchor_id*/ 0u,
            /*context_anchor_id*/ 0u,
            /*stream_id*/ 0u,
            /*extractor_id*/ 0u,
            /*trust_class*/ 1u,
            /*causal_tag*/ 0u,
            dom,
            t.url_ascii,
            body
        );

        sm->emit_ui_line("GE_LIVE_CRAWL:ok domain=" + dom + " bytes=" + std::to_string((uint32_t)body.size()));
        fetched += 1;
    }
}
