#include "GE_ai_vault.hpp"

#include "GE_runtime.hpp" // for SubstrateManager fields + tick
#include "GE_metric_registry.hpp"
#include "ew_txn_file.hpp"
#include "anchor.hpp"


#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace genesis {

static inline bool ge_has_suffix(const std::string& s, const char* suf) {
    const size_t n = s.size();
    const size_t m = std::strlen(suf);
    if (m > n) return false;
    return std::memcmp(s.data() + (n - m), suf, m) == 0;
}

static inline uint64_t ge_parse_u64_from_tag(const std::string& name, const char* tag) {
    // Parse digits after "tag" until non-digit.
    const char* p = std::strstr(name.c_str(), tag);
    if (!p) return 0ull;
    p += std::strlen(tag);
    uint64_t v = 0ull;
    while (*p >= '0' && *p <= '9') {
        v = v * 10ull + (uint64_t)(*p - '0');
        ++p;
    }
    return v;
}

static inline std::string ge_json_sanitize_ascii(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        const unsigned char uc = (unsigned char)c;
        if (c == '\\' || c == '"' || uc < 32u) o.push_back('_');
        else o.push_back(c);
    }
    return o;
}


static inline uint64_t ge_stable_id_u64_from_ascii(const std::string& s) {
    // Deterministic 64-bit labeling (not a security mechanism).
    uint64_t x = 0x9E3779B97F4A7C15ULL;
    for (unsigned char c : s) {
        x ^= (uint64_t)c + 0x9E3779B97F4A7C15ULL + (x << 6) + (x >> 2);
    }
    return x;
}

static inline void ge_hex_u64(char out17[17], uint64_t v) {
    static const char* kHex = "0123456789abcdef";
    for (int i = 15; i >= 0; --i) {
        out17[i] = kHex[(int)(v & 0xFULL)];
        v >>= 4;
    }
    out17[16] = '\0';
}




static inline fs::path ge_ai_asset_root_path() {
    // Mirrors AI-created artifacts into the project asset substrate so the editor
    // can expose them as a “vault-like” library surface.
    return fs::path("Draft Container/AssetSubstrate") / "AI";
}


static bool ge_write_asset_ref_textfile_(const fs::path& out_path,
                                        const std::string& content_ascii,
                                        uint64_t tick_u64,
                                        std::string* out_err) {
    // Deterministic, replay-safe: if the ref already exists, treat as success.
    std::error_code ec;
    if (fs::exists(out_path, ec) && !ec) return true;
    ec.clear();
    std::string err;
    const bool ok = ew_txn_write_file_text(out_path, content_ascii, tick_u64, &err);
    if (!ok && out_err) *out_err = err;
    return ok;
}

static bool ge_write_asset_ref_metric_(const fs::path& asset_dir,
                                      const fs::path& vault_file,
                                      const genesis::MetricTask& t,
                                      uint64_t tick_u64,
                                      bool accepted,
                                      std::string* out_err) {
    std::error_code ec;
    (void)fs::create_directories(asset_dir, ec);
    ec.clear();

    // Stable filename (no tick): one ref per task.
    char name_buf[256];
    std::snprintf(name_buf, sizeof(name_buf),
                  "ref_metric_k%u_oid%llu.geassetref",
                  (unsigned)t.target.kind,
                  (unsigned long long)t.task_id_u64);

    const fs::path out_path = asset_dir / name_buf;

    // Engine-native asset handle (text, bounded, deterministic).
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "GEASSETREF ver=1\n"
                  "ref_utf8=%s\n"
                  "ref_kind=metric\n"
                  "metric_kind_u32=%u\n"
                  "task_id_u64=%llu\n"
                  "accepted_u32=%u\n"
                  "completed_tick_u64=%llu\n",
                  vault_file.string().c_str(),
                  (unsigned)t.target.kind,
                  (unsigned long long)t.task_id_u64,
                  (unsigned)(accepted ? 1u : 0u),
                  (unsigned long long)tick_u64);

    return ge_write_asset_ref_textfile_(out_path, std::string(buf), tick_u64, out_err);
}


static bool ge_write_asset_ref_simple_(const fs::path& asset_dir,
                                      const fs::path& vault_file,
                                      const char* kind_ascii,
                                      uint64_t tick_u64,
                                      uint32_t count_u32,
                                      std::string* out_err) {
    std::error_code ec;
    (void)fs::create_directories(asset_dir, ec);
    ec.clear();

    const uint64_t id = ge_stable_id_u64_from_ascii(std::string("simple|") + std::string(kind_ascii));

    char name_buf[256];
    std::snprintf(name_buf, sizeof(name_buf),
                  "ref_%s_k0_oid%llu.geassetref",
                  kind_ascii,
                  (unsigned long long)id);

    const fs::path out_path = asset_dir / name_buf;

    char buf[768];
    std::snprintf(buf, sizeof(buf),
                  "GEASSETREF ver=1\n"
                  "ref_utf8=%s\n"
                  "ref_kind=%s\n"
                  "tick_u64=%llu\n"
                  "count_u32=%u\n",
                  vault_file.string().c_str(),
                  kind_ascii,
                  (unsigned long long)tick_u64,
                  (unsigned)count_u32);

    return ge_write_asset_ref_textfile_(out_path, std::string(buf), tick_u64, out_err);
}


static bool ge_write_asset_ref_corpus_(const fs::path& asset_dir,
                                      const fs::path& vault_file,
                                      const char* kind_ascii,
                                      uint64_t key_u64,
                                      uint32_t lane_u32,
                                      uint32_t stage_u32,
                                      uint64_t tick_u64,
                                      std::string* out_err) {
    std::error_code ec;
    (void)fs::create_directories(asset_dir, ec);
    ec.clear();

    // Stable filename derived from key/lane/stage.
    char name_buf[256];
    std::snprintf(name_buf, sizeof(name_buf),
                  "ref_%s_k0_oid%llu_lane_%u_stage_%u.geassetref",
                  kind_ascii,
                  (unsigned long long)key_u64,
                  (unsigned)lane_u32,
                  (unsigned)stage_u32);

    const fs::path out_path = asset_dir / name_buf;

    char buf[896];
    std::snprintf(buf, sizeof(buf),
                  "GEASSETREF ver=1\n"
                  "ref_utf8=%s\n"
                  "ref_kind=%s\n"
                  "key_u64=%llu\n"
                  "lane_u32=%u\n"
                  "stage_u32=%u\n"
                  "tick_u64=%llu\n",
                  vault_file.string().c_str(),
                  kind_ascii,
                  (unsigned long long)key_u64,
                  (unsigned)lane_u32,
                  (unsigned)stage_u32,
                  (unsigned long long)tick_u64);

    return ge_write_asset_ref_textfile_(out_path, std::string(buf), tick_u64, out_err);
}



void AiVault::ensure_dirs_() {
    std::error_code ec;
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "experiments" / "metrics", ec);
    ec.clear();
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "experiments" / "failures", ec);
    ec.clear();
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "materials", ec);
    ec.clear();
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "objects", ec);
    ec.clear();
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "inventions", ec);
    ec.clear();
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "machines", ec);
    ec.clear();
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "components", ec);
    ec.clear();
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "uspto", ec);
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "corpus" / "allowlist_pages", ec);
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "corpus" / "resonant_pages", ec);
    (void)fs::create_directories(fs::path(canonical_dir_utf8_) / "corpus" / "speech_boot", ec);

    ec.clear();

    ec.clear();

    // Ephemeral partition.
    (void)fs::create_directories(fs::path(ephemeral_dir_utf8_) / "experiments" / "metrics_failures", ec);
    ec.clear();

    // Additional ephemeral partitions (schema-locked; may be populated by later phases).
    (void)fs::create_directories(fs::path(ephemeral_dir_utf8_) / "scratch_synth", ec);
    ec.clear();
    (void)fs::create_directories(fs::path(ephemeral_dir_utf8_) / "corpus" / "unpromoted_pages", ec);
    ec.clear();


    // Asset substrate mirror (AI partition). These are reference files pointing at
    // the canonical/ephemeral vault artifacts.
    const fs::path ai = ge_ai_asset_root_path();
    (void)fs::create_directories(ai / "research", ec);
    ec.clear();
    (void)fs::create_directories(ai / "experiments" / "metrics", ec);
    ec.clear();
    (void)fs::create_directories(ai / "experiments" / "metrics_failures", ec);
    ec.clear();
    (void)fs::create_directories(ai / "machines", ec);
    ec.clear();
    (void)fs::create_directories(ai / "inventions", ec);
    ec.clear();
    (void)fs::create_directories(ai / "components", ec);
    ec.clear();
    (void)fs::create_directories(ai / "materials", ec);
    ec.clear();
    (void)fs::create_directories(ai / "corpus" / "allowlist_pages", ec);
    ec.clear();
    (void)fs::create_directories(ai / "corpus" / "resonant_pages", ec);
    ec.clear();
    (void)fs::create_directories(ai / "corpus" / "speech_boot", ec);
    ec.clear();

    (void)fs::create_directories(ai / "objects", ec);
    ec.clear();
    (void)fs::create_directories(ai / "uspto", ec);
    ec.clear();
}

void AiVault::recount_() {
    std::error_code ec;
    uint64_t committed = 0ull;
    uint64_t eph = 0ull;
    uint64_t allow_pages = 0ull;
    uint64_t reson_pages = 0ull;


    const fs::path p_comm = fs::path(canonical_dir_utf8_) / "experiments" / "metrics";
    if (fs::exists(p_comm, ec)) {
        ec.clear();
        for (auto it = fs::directory_iterator(p_comm, ec);
             !ec && it != fs::directory_iterator();
             ++it) {
            const auto& fp = it->path();
            if (fp.has_extension() && fp.extension() == ".json") committed++;
        }
    }
    ec.clear();

    const fs::path p_eph = fs::path(ephemeral_dir_utf8_) / "experiments" / "metrics_failures";
    if (fs::exists(p_eph, ec)) {
        ec.clear();
        for (auto it = fs::directory_iterator(p_eph, ec);
             !ec && it != fs::directory_iterator();
             ++it) {
            const auto& fp = it->path();
            if (fp.has_extension() && fp.extension() == ".json") eph++;
        }
    }

    const fs::path p_allow = fs::path(canonical_dir_utf8_) / "corpus" / "allowlist_pages";
    if (fs::exists(p_allow, ec)) {
        ec.clear();
        for (auto it = fs::directory_iterator(p_allow, ec);
             !ec && it != fs::directory_iterator();
             ++it) {
            const auto& fp = it->path();
            if (fp.has_extension() && fp.extension() == ".json") allow_pages++;
        }
    }
    ec.clear();


    const fs::path p_res = fs::path(canonical_dir_utf8_) / "corpus" / "resonant_pages";
    if (fs::exists(p_res, ec)) {
        ec.clear();
        for (auto it = fs::directory_iterator(p_res, ec);
             !ec && it != fs::directory_iterator();
             ++it) {
            const auto& fp = it->path();
            if (fp.has_extension() && fp.extension() == ".json") reson_pages++;
        }
    }
    ec.clear();

    committed_experiment_count_u64_ = committed;
    ephemeral_experiment_count_u64_ = eph;
    committed_allowlist_page_count_u64_ = allow_pages;
    committed_resonant_page_count_u64_ = reson_pages;

}

void AiVault::init_once(::SubstrateManager* sm) {
    if (inited_) {
        if (sm) {
            sm->vault_experiments_committed_u64 = committed_experiment_count_u64_;
            sm->vault_experiments_ephemeral_u64 = ephemeral_experiment_count_u64_;
            sm->vault_allowlist_pages_u64 = committed_allowlist_page_count_u64_;
            sm->vault_resonant_pages_u64 = committed_resonant_page_count_u64_;
        sm->emit_ai_event_line("commit_resonant_page", std::string("key=") + std::to_string(key_u64) + " rq15=" + std::to_string((uint32_t)best_q15) + " bytes=" + std::to_string((uint64_t)len_u32));

        }
        return;
    }
    inited_ = true;

    // Root under user’s preferred “Draft Container”.
    root_utf8_ = "Draft Container/AI_Vault";
    canonical_dir_utf8_ = root_utf8_ + "/canonical";
    ephemeral_dir_utf8_ = root_utf8_ + "/_ephemeral";

    ensure_dirs_();
    recount_();

    if (sm) {
        sm->vault_allowlist_pages_u64 = committed_allowlist_page_count_u64_;
        sm->vault_resonant_pages_u64 = committed_resonant_page_count_u64_;

        sm->vault_experiments_committed_u64 = committed_experiment_count_u64_;
        sm->vault_experiments_ephemeral_u64 = ephemeral_experiment_count_u64_;
    }
}


bool AiVault::vec_contains_u64_(const std::vector<uint64_t>& v, uint64_t x) {
    for (uint64_t y : v) if (y == x) return true;
    return false;
}

void AiVault::vec_insert_bounded_u64_(std::vector<uint64_t>& v, uint64_t x, size_t cap) {
    if (vec_contains_u64_(v, x)) return;
    if (v.size() < cap) {
        v.push_back(x);
        return;
    }
    // Drop oldest deterministically.
    if (cap == 0u) return;
    for (size_t i = 1; i < cap; ++i) v[i - 1] = v[i];
    v[cap - 1] = x;
    if (v.size() > cap) v.resize(cap);
}



bool AiVault::write_metric_json_(::SubstrateManager* sm,
                                const std::string& dir_utf8,
                                const std::string& prefix_utf8,
                                const genesis::MetricTask& t,
                                std::string* out_written_path_utf8) {
    if (!sm) return false;

    // Canonical metrics are de-duped by (kind,task_id). Ephemeral failures may include tick.
    const bool stable_per_task = (prefix_utf8 == "metric");
    const uint64_t tick_u64 = (t.completed_tick_u64 != 0ull) ? t.completed_tick_u64 : sm->canonical_tick_u64();

    char name_buf[256];
    if (stable_per_task) {
        std::snprintf(name_buf, sizeof(name_buf),
                      "%s_kind_%u_task_%llu.json",
                      prefix_utf8.c_str(),
                      (unsigned)t.target.kind,
                      (unsigned long long)t.task_id_u64);
    } else {
        std::snprintf(name_buf, sizeof(name_buf),
                      "%s_kind_%u_task_%llu_tick_%llu.json",
                      prefix_utf8.c_str(),
                      (unsigned)t.target.kind,
                      (unsigned long long)t.task_id_u64,
                      (unsigned long long)tick_u64);
    }

    const fs::path out_path = fs::path(dir_utf8) / name_buf;
    if (out_written_path_utf8) *out_written_path_utf8 = out_path.string();

    // Replay-safe dedupe: canonical metrics are singletons per task.
    if (stable_per_task) {
        std::error_code ec_exists;
        if (fs::exists(out_path, ec_exists) && !ec_exists) return true;
    }

    // Deterministic payload: no floats.
    char buf[4096];
    // Serialize target + best_sim vectors in Q32.32.
    std::string tgt = "[";
    std::string sim = "[";
    for (uint32_t i = 0; i < t.target.target.dim_u32 && i < GENESIS_METRIC_DIM_MAX; ++i) {
        if (i) { tgt += ","; sim += ","; }
        tgt += std::to_string((long long)t.target.target.v_q32_32[i]);
        sim += std::to_string((long long)t.best_sim.v_q32_32[i]);
    }
    tgt += "]";
    sim += "]";

    std::snprintf(buf, sizeof(buf),
                  "{"
                  "\"kind\":%u,"
                  "\"task_id\":%llu,"
                  "\"source_id\":%llu,"
                  "\"source_anchor\":%u,"
                  "\"context_anchor\":%u,"\"has_claim\":%u,"\"claim_value_q32_32\":%lld,"\"claim_unit\":%u,"\"claim_ordinal\":%u,"\"declared_work_units\":%u,"
                  "\"accepted\":%s,"
                  "\"tol_num\":%u,"
                  "\"tol_den\":%u,"
                  "\"best_err_q32_32\":%lld,"
                  "\"completed_tick\":%llu,"
                  "\"target_q32_32\":%s,"
                  "\"best_sim_q32_32\":%s"
                  "}",
                  (unsigned)t.target.kind,
                  (unsigned long long)t.task_id_u64,
                  (unsigned long long)t.source_id_u64,
                  (unsigned)t.source_anchor_id_u32,
                  (unsigned)t.context_anchor_id_u32,
                  (unsigned)t.has_claim_u32,
                  (long long)t.claim.value_q32_32,
                  (unsigned)t.claim.unit_code_u32,
                  (unsigned)t.claim.claim_ordinal_u32,
                  (unsigned)t.declared_work_units_u32,

                  t.accepted ? "true" : "false",
                  (unsigned)t.target.tol_num_u32,
                  (unsigned)t.target.tol_den_u32,
                  (long long)t.best_err_q32_32,
                  (unsigned long long)tick_u64,
                  tgt.c_str(),
                  sim.c_str());

    std::string err;
    const bool ok = ew_txn_write_file_text(out_path, std::string(buf), tick_u64, &err);
    (void)err;
    return ok;
}

bool AiVault::commit_metric_task(::SubstrateManager* sm, const genesis::MetricTask& t) {
    init_once(sm);

    const fs::path dir = fs::path(canonical_dir_utf8_) / "experiments" / "metrics";
    std::string written;
    const bool ok = write_metric_json_(sm, dir.string(), "metric", t, &written);
    if (ok) {
        committed_experiment_count_u64_++;
        if (sm) sm->vault_experiments_committed_u64 = committed_experiment_count_u64_;

        // Regression/visibility: stable key is the task_id.
        last_commit_key_u64_ = t.task_id_u64;
        last_commit_kind_u32_ = 1u;
        if (sm) { sm->vault_last_commit_key_u64 = last_commit_key_u64_; sm->vault_last_commit_kind_u32 = last_commit_kind_u32_; }
        if (sm) sm->emit_ai_event_line("commit_metric", std::string("key=") + std::to_string(last_commit_key_u64_) + " kind=" + ew_metric_kind_name_ascii(t.kind));

        // Mirror a stable reference into the AssetSubstrate AI partition.
        {
            const uint64_t tick_u64 = (t.completed_tick_u64 != 0ull) ? t.completed_tick_u64 : (sm ? sm->canonical_tick_u64() : 0ull);
            std::string err;
            (void)ge_write_asset_ref_metric_(ge_ai_asset_root_path() / "experiments" / "metrics",
                                          fs::path(written),
                                          t,
                                          tick_u64,
                                          true,
                                          &err);
        }

        // If an ephemeral failure artifact exists for this task, remove it.
        std::error_code ec;
        const fs::path eph_dir = fs::path(ephemeral_dir_utf8_) / "experiments" / "metrics_failures";
        if (fs::exists(eph_dir, ec)) {
            ec.clear();
            std::vector<fs::path> cand;
            for (auto it = fs::directory_iterator(eph_dir, ec);
                 !ec && it != fs::directory_iterator();
                 ++it) {
                const fs::path fp = it->path();
                const std::string fn = fp.filename().string();
                if (!ge_has_suffix(fn, ".json")) continue;
                const uint64_t task_id = ge_parse_u64_from_tag(fn, "task_");
                if (task_id == t.task_id_u64) cand.push_back(fp);
            }
            ec.clear();
            std::sort(cand.begin(), cand.end(), [](const fs::path& a, const fs::path& b){
                return a.u8string() < b.u8string();
            });
            for (const auto& fp : cand) {
                (void)fs::remove(fp, ec);
                ec.clear();
                if (ephemeral_experiment_count_u64_ > 0) ephemeral_experiment_count_u64_--;
            }
            if (sm) sm->vault_experiments_ephemeral_u64 = ephemeral_experiment_count_u64_;
        }
    }
    return ok;
}

bool AiVault::store_ephemeral_metric_task(::SubstrateManager* sm, const genesis::MetricTask& t) {
    init_once(sm);
    const fs::path dir = fs::path(ephemeral_dir_utf8_) / "experiments" / "metrics_failures";
    std::string written;
    const bool ok = write_metric_json_(sm, dir.string(), "fail", t, &written);
    if (ok) {
        ephemeral_experiment_count_u64_++;
        if (sm) sm->vault_experiments_ephemeral_u64 = ephemeral_experiment_count_u64_;

        // Regression/visibility: stable key is the task_id.
        last_commit_key_u64_ = t.task_id_u64;
        last_commit_kind_u32_ = 2u;
        if (sm) { sm->vault_last_commit_key_u64 = last_commit_key_u64_; sm->vault_last_commit_kind_u32 = last_commit_kind_u32_; }
        if (sm) sm->emit_ai_event_line("commit_metric", std::string("key=") + std::to_string(last_commit_key_u64_) + " kind=" + ew_metric_kind_name_ascii(t.kind));

        // Mirror a stable reference into the AssetSubstrate AI partition.
        {
            const uint64_t tick_u64 = (t.completed_tick_u64 != 0ull) ? t.completed_tick_u64 : (sm ? sm->canonical_tick_u64() : 0ull);
            std::string err;
            (void)ge_write_asset_ref_metric_(ge_ai_asset_root_path() / "experiments" / "metrics_failures",
                                          fs::path(written),
                                          t,
                                          tick_u64,
                                          false,
                                          &err);
        }
    }
    return ok;
}



bool AiVault::commit_speechboot_vocab(::SubstrateManager* sm,
                                      const std::vector<std::string>& words_ascii,
                                      uint32_t vocab_min_u32,
                                      uint64_t tick_u64) {
    init_once(sm);
    ensure_dirs_();

    const fs::path dir = fs::path(canonical_dir_utf8_) / "corpus" / "speech_boot";

    // Deterministic file name derived from tick.
    char name_buf[256];
    std::snprintf(name_buf, sizeof(name_buf),
                  "speech_vocab.txt");

    const fs::path out_path = dir / name_buf;

    // If file already exists, treat as success (replay-safe).
    std::error_code ec;
    if (fs::exists(out_path, ec) && !ec) {
        return true;
    }
    ec.clear();

    std::string txt;
    txt.reserve(1024);
    txt += "tick=" + std::to_string((unsigned long long)tick_u64);
    txt += " vocab_min=" + std::to_string((unsigned)vocab_min_u32);
    txt += " vocab_count=" + std::to_string((unsigned long long)words_ascii.size());
    txt.push_back('\n');

    // Deterministic ordering: caller must provide sorted unique words.
    for (const auto& w : words_ascii) {
        const EwId9 id9 = ew_id9_from_string_ascii(w);
        txt += w;
        txt.push_back('\t');
        for (int i = 0; i < 9; ++i) {
            if (i) txt.push_back(',');
            txt += std::to_string((unsigned long long)id9.u32[(size_t)i]);
        }
        txt.push_back('\n');
    }

    std::string err;
    const bool ok = ew_txn_write_file_text(out_path, txt, tick_u64, &err);
    if (ok) {
        // Regression/visibility: stable key for speech vocab.
        last_commit_key_u64_ = ge_stable_id_u64_from_ascii("speech_vocab");
        last_commit_kind_u32_ = 5u;
        if (sm) { sm->vault_last_commit_key_u64 = last_commit_key_u64_; sm->vault_last_commit_kind_u32 = last_commit_kind_u32_; }
        if (sm) sm->emit_ai_event_line("commit_speech_vocab", std::string("key=") + std::to_string(last_commit_key_u64_));

        // Mirror a stable reference into the AssetSubstrate AI partition.
        std::string e2;
        (void)ge_write_asset_ref_simple_(ge_ai_asset_root_path() / "corpus" / "speech_boot",
                                             out_path,
                                             "speech_vocab",
                                             tick_u64,
                                             (uint32_t)words_ascii.size(),
                                             &e2);
    }
    return ok;
}



static inline uint16_t ge_topic_mask_resonance_q15(uint64_t a_u64, uint64_t b_u64) {
    const uint64_t inter = a_u64 & b_u64;
    const uint64_t uni = a_u64 | b_u64;
    const uint32_t i_pop = (uint32_t)__builtin_popcountll((unsigned long long)inter);
    const uint32_t u_pop = (uint32_t)__builtin_popcountll((unsigned long long)uni);
    if (u_pop == 0u) return 0u;
    // q15 fraction: floor((i/u) * 32768)
    const uint32_t q15 = (uint32_t)((uint64_t)i_pop * 32768ULL / (uint64_t)u_pop);
    return (q15 > 65535u) ? 65535u : (uint16_t)q15;
}

bool AiVault::maybe_commit_resonant_page(::SubstrateManager* sm,
                                        const std::string& domain_ascii,
                                        const std::string& url_ascii,
                                        uint32_t anchor_id_u32,
                                        uint32_t lane_u32,
                                        uint32_t stage_u32,
                                        const ::SpiderCode4& sc,
                                        uint16_t harmonics_mean_q15,
                                        uint32_t len_u32,
                                        uint64_t topic_mask_u64) {
    init_once(sm);
    if (!sm) return false;
    if (topic_mask_u64 == 0ULL) return false;

    // Resonance gate: require overlap with an existing canonical topic mask.
    // Single source of truth is the AI config anchor.
    uint16_t gate_q15 = 31457u;
    if (const EwAiConfigAnchorState* cfg = sm->ai_config_state()) gate_q15 = cfg->resonance_gate_q15;

    uint16_t best_q15 = 0u;
    for (uint64_t m : canonical_topic_masks_) {
        const uint16_t r = ge_topic_mask_resonance_q15(topic_mask_u64, m);
        if (r > best_q15) best_q15 = r;
        if (best_q15 >= gate_q15) break;
    }
    if (best_q15 < gate_q15) return false;

    const std::string dom = ge_json_sanitize_ascii(domain_ascii);
    const std::string url = ge_json_sanitize_ascii(url_ascii);

    const std::string key_src = std::string("reson|") + dom + "|" + url + "|" +
                                std::to_string(lane_u32) + "|" + std::to_string(stage_u32) + "|" +
                                std::to_string(anchor_id_u32);
    const uint64_t key_u64 = ge_stable_id_u64_from_ascii(key_src);

    if (vec_contains_u64_(seen_resonant_keys_, key_u64)) {
        return true;
    }

    char key_hex[17]; ge_hex_u64(key_hex, key_u64);

    const fs::path dir = fs::path(canonical_dir_utf8_) / "corpus" / "resonant_pages";

    char name_buf[320];
    std::snprintf(name_buf, sizeof(name_buf),
                  "reson_k%s_lane_%u_stage_%u_anchor_%u.json",
                  key_hex,
                  (unsigned)lane_u32,
                  (unsigned)stage_u32,
                  (unsigned)anchor_id_u32);

    const fs::path out_path = dir / name_buf;

    std::error_code ec_exists;
    if (fs::exists(out_path, ec_exists) && !ec_exists) {
        vec_insert_bounded_u64_(seen_resonant_keys_, key_u64, 4096u);
        return true;
    }
    ec_exists.clear();

    const uint64_t tick_u64 = sm->canonical_tick_u64();

    char buf[1792];
    std::snprintf(buf, sizeof(buf),
                  "{"
                  "\"domain_ascii\":\"%s\","
                  "\"url_ascii\":\"%s\","
                  "\"anchor_id\":%u,"
                  "\"lane\":%u,"
                  "\"stage\":%u,"
                  "\"tick\":%llu,"
                  "\"len_u32\":%u,"
                  "\"topic_mask_u64\":%llu,"
                  "\"resonance_q15\":%u,"
                  "\"spider\":[%d,%u,%u,%u],"
                  "\"harmonics_mean_q15\":%u"
                  "}",
                  dom.c_str(),
                  url.c_str(),
                  (unsigned)anchor_id_u32,
                  (unsigned)lane_u32,
                  (unsigned)stage_u32,
                  (unsigned long long)tick_u64,
                  (unsigned)len_u32,
                  (unsigned long long)topic_mask_u64,
                  (unsigned)best_q15,
                  (int)sc.f_code, (unsigned)sc.a_code, (unsigned)sc.v_code, (unsigned)sc.i_code,
                  (unsigned)harmonics_mean_q15);

    std::string err;
    const bool ok = ew_txn_write_file_text(out_path, std::string(buf), tick_u64, &err);
    if (ok) {
        committed_resonant_page_count_u64_ += 1ull;
        sm->vault_resonant_pages_u64 = committed_resonant_page_count_u64_;

        last_commit_key_u64_ = key_u64;
        last_commit_kind_u32_ = 4u;
        sm->vault_last_commit_key_u64 = last_commit_key_u64_;
        sm->vault_last_commit_kind_u32 = last_commit_kind_u32_;
        sm->emit_ai_event_line("commit_allowlist_page", std::string("key=") + std::to_string(last_commit_key_u64_) + " bytes=" + std::to_string((uint64_t)page_bytes_u32));

        // Bounded topic mask cache used for resonance gating.
        if (canonical_topic_masks_.size() < 1024u) canonical_topic_masks_.push_back(topic_mask_u64);
        else {
            for (size_t i = 1; i < 1024u; ++i) canonical_topic_masks_[i - 1u] = canonical_topic_masks_[i];
            canonical_topic_masks_[1023u] = topic_mask_u64;
        }

        vec_insert_bounded_u64_(seen_resonant_keys_, key_u64, 4096u);

        // Mirror stable reference into AssetSubstrate AI partition.
        (void)ge_write_asset_ref_corpus_(ge_ai_asset_root_path() / "corpus" / "resonant_pages",
                                             out_path,
                                             "resonant_page",
                                             key_u64,
                                             lane_u32,
                                             stage_u32,
                                             tick_u64,
                                             &err);
    }
    return ok;
}

bool AiVault::commit_allowlist_page(::SubstrateManager* sm,
                                  const std::string& domain_ascii,
                                  const std::string& url_ascii,
                                  uint32_t anchor_id_u32,
                                  uint32_t lane_u32,
                                  uint32_t stage_u32,
                                  const ::SpiderCode4& sc,
                                  uint16_t harmonics_mean_q15,
                                  uint32_t len_u32,
                                  uint64_t topic_mask_u64) {
    init_once(sm);
    if (!sm) return false;

    const std::string dom = ge_json_sanitize_ascii(domain_ascii);
    const std::string url = ge_json_sanitize_ascii(url_ascii);

    // Stable key independent of tick (dedupe across time).
    const std::string key_src = std::string("allow|") + dom + "|" + url + "|" +
                                std::to_string(lane_u32) + "|" + std::to_string(stage_u32) + "|" +
                                std::to_string(anchor_id_u32);
    const uint64_t key_u64 = ge_stable_id_u64_from_ascii(key_src);

    if (vec_contains_u64_(seen_allowlist_keys_, key_u64)) {
        return true;
    }

    char key_hex[17]; ge_hex_u64(key_hex, key_u64);

    const fs::path dir = fs::path(canonical_dir_utf8_) / "corpus" / "allowlist_pages";

    char name_buf[320];
    std::snprintf(name_buf, sizeof(name_buf),
                  "allow_k%s_lane_%u_stage_%u_anchor_%u.json",
                  key_hex,
                  (unsigned)lane_u32,
                  (unsigned)stage_u32,
                  (unsigned)anchor_id_u32);

    const fs::path out_path = dir / name_buf;

    std::error_code ec_exists;
    if (fs::exists(out_path, ec_exists) && !ec_exists) {
        vec_insert_bounded_u64_(seen_allowlist_keys_, key_u64, 4096u);
        return true;
    }
    ec_exists.clear();

    const uint64_t tick_u64 = sm->canonical_tick_u64();

    char buf[1536];
    std::snprintf(buf, sizeof(buf),
                  "{"
                  "\"domain_ascii\":\"%s\","
                  "\"url_ascii\":\"%s\","
                  "\"anchor_id\":%u,"
                  "\"lane\":%u,"
                  "\"stage\":%u,"
                  "\"tick\":%llu,"
                  "\"len_u32\":%u,"
                  "\"topic_mask_u64\":%llu,"
                  "\"spider\":[%d,%u,%u,%u],"
                  "\"harmonics_mean_q15\":%u"
                  "}",
                  dom.c_str(),
                  url.c_str(),
                  (unsigned)anchor_id_u32,
                  (unsigned)lane_u32,
                  (unsigned)stage_u32,
                  (unsigned long long)tick_u64,
                  (unsigned)len_u32,
                  (unsigned long long)topic_mask_u64,
                  (int)sc.f_code, (unsigned)sc.a_code, (unsigned)sc.v_code, (unsigned)sc.i_code,
                  (unsigned)harmonics_mean_q15);

    std::string err;
    const bool ok = ew_txn_write_file_text(out_path, std::string(buf), tick_u64, &err);
    if (ok) {
        committed_allowlist_page_count_u64_ += 1ull;
        sm->vault_allowlist_pages_u64 = committed_allowlist_page_count_u64_;

        last_commit_key_u64_ = key_u64;
        last_commit_kind_u32_ = 3u;
        sm->vault_last_commit_key_u64 = last_commit_key_u64_;
        sm->vault_last_commit_kind_u32 = last_commit_kind_u32_;

        // Bounded topic mask cache used for resonance gating.
        if (canonical_topic_masks_.size() < 1024u) canonical_topic_masks_.push_back(topic_mask_u64);
        else {
            for (size_t i = 1; i < 1024u; ++i) canonical_topic_masks_[i - 1u] = canonical_topic_masks_[i];
            canonical_topic_masks_[1023u] = topic_mask_u64;
        }

        vec_insert_bounded_u64_(seen_allowlist_keys_, key_u64, 4096u);

        // Mirror stable reference into AssetSubstrate AI partition.
        (void)ge_write_asset_ref_corpus_(ge_ai_asset_root_path() / "corpus" / "allowlist_pages",
                                             out_path,
                                             "allowlist_page",
                                             key_u64,
                                             lane_u32,
                                             stage_u32,
                                             tick_u64,
                                             &err);
    }
    return ok;
}

void AiVault::gc_ephemeral_(::SubstrateManager* sm) {
    if (!sm) return;

    const uint64_t now = sm->canonical_tick_u64();
    std::error_code ec;
    const fs::path eph_dir = fs::path(ephemeral_dir_utf8_) / "experiments" / "metrics_failures";
    if (!fs::exists(eph_dir, ec)) return;
    ec.clear();

    struct Item { fs::path p; uint64_t tick; std::string name; };
    std::vector<Item> items;
    for (auto it = fs::directory_iterator(eph_dir, ec);
         !ec && it != fs::directory_iterator();
         ++it) {
        const fs::path fp = it->path();
        const std::string fn = fp.filename().string();
        if (!ge_has_suffix(fn, ".json")) continue;
        const uint64_t tick = ge_parse_u64_from_tag(fn, "tick_");
        items.push_back({fp, tick, fn});
    }
    ec.clear();

    // Sort deterministically: oldest tick first, then name.
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.name < b.name;
    });

    // TTL removal.
    uint64_t removed = 0ull;
    uint64_t removed_over = 0ull;
    for (const auto& it : items) {
        const uint64_t age = (now >= it.tick) ? (now - it.tick) : 0ull;
        if (age > ephemeral_ttl_ticks_u64_) {
            (void)fs::remove(it.p, ec);
            ec.clear();
            removed++;
        }
    }

    if (removed != 0ull) {
        if (ephemeral_experiment_count_u64_ >= removed) ephemeral_experiment_count_u64_ -= removed;
        else ephemeral_experiment_count_u64_ = 0ull;
    }

    // Enforce max count.
    if (items.size() > (size_t)ephemeral_max_u32_) {
        const size_t over = items.size() - (size_t)ephemeral_max_u32_;
        for (size_t i = 0; i < over; ++i) {
            (void)fs::remove(items[i].p, ec);
            ec.clear();
            removed_over++;
            if (ephemeral_experiment_count_u64_ > 0ull) ephemeral_experiment_count_u64_--;
        }
    }

    if (sm) sm->vault_experiments_ephemeral_u64 = ephemeral_experiment_count_u64_;
    const uint64_t total_removed = removed + removed_over;
    if (total_removed != 0ull) {
        sm->emit_ai_event_line("gc_ephemeral", std::string("removed=") + std::to_string(total_removed) + " ttl_removed=" + std::to_string(removed) + " over_removed=" + std::to_string(removed_over));
    }

}

void AiVault::tick_gc(::SubstrateManager* sm) {
    init_once(sm);
    if (!sm) return;

    // Pull deterministic GC policy from the AI config anchor (single source).
    if (const EwAiConfigAnchorState* cfg = sm->ai_config_state()) {
        ephemeral_ttl_ticks_u64_ = cfg->ephemeral_ttl_ticks_u64;
        ephemeral_max_u32_ = cfg->max_ephemeral_count_u32;
        gc_stride_u32_ = cfg->ephemeral_gc_stride_ticks_u32;
    }

    const uint64_t tick = sm->canonical_tick_u64();
    if (gc_stride_u32_ == 0u) return;
    if ((tick % (uint64_t)gc_stride_u32_) != 0ull) return;
    gc_ephemeral_(sm);
}

} // namespace genesis
