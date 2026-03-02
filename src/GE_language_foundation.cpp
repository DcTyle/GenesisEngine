#include "GE_language_foundation.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace genesis {

static inline void rep_append(std::string* rep, const std::string& s) {
    if (!rep) return;
    if (!rep->empty()) rep->push_back('\n');
    rep->append(s);
}

LanguageFoundation::LanguageFoundation() {
    stats_ = LangLexiconStats{};
}

bool LanguageFoundation::ends_with(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    return s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

void LanguageFoundation::vec_insert_unique_bounded(std::vector<EigenWare::EwId9>& v, const EigenWare::EwId9& x, size_t cap) {
    // Deterministic and bounded: allow duplicates during ingest, and unique at finalize.
    if (v.size() < cap) v.push_back(x);
}

static void sort_unique(std::vector<EigenWare::EwId9>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

static std::vector<std::string> list_files_sorted(const std::string& dir_utf8) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::path d = fs::u8path(dir_utf8);
    if (!fs::exists(d, ec) || ec) return out;

    for (auto it = fs::directory_iterator(d, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2) || ec2) continue;
        out.push_back(it->path().u8string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool LanguageFoundation::bootstrap_from_dir(const std::string& root_dir_utf8, std::string* out_report_utf8) {
    bool any = false;

    const std::string cmu_dir = (fs::u8path(root_dir_utf8) / fs::u8path("cmudict")).u8string();
    const std::string wn_dir  = (fs::u8path(root_dir_utf8) / fs::u8path("wordnet") / fs::u8path("dict")).u8string();
    const std::string enc_dir = (fs::u8path(root_dir_utf8) / fs::u8path("encyclopedia")).u8string();
    const std::string sp_dir  = (fs::u8path(root_dir_utf8) / fs::u8path("speech") / fs::u8path("commonvoice")).u8string();

    any |= load_cmudict_dir(cmu_dir, out_report_utf8);
    any |= load_wordnet_dir(wn_dir, out_report_utf8);
    any |= load_encyclopedia_dir(enc_dir, out_report_utf8);
    any |= load_speech_dir(sp_dir, out_report_utf8);

    // Deterministic finalize: sort/unique and compute stats.
    sort_unique(word_ids_);
    sort_unique(pron_ids_);
    sort_unique(sense_ids_);
    sort_unique(relation_ids_);
    sort_unique(concept_ids_);

    stats_.word_count_u32 = (uint32_t)word_ids_.size();
    stats_.pron_count_u32 = (uint32_t)pron_ids_.size();
    stats_.senses_count_u32 = (uint32_t)sense_ids_.size();
    stats_.relations_count_u32 = (uint32_t)relation_ids_.size();
    stats_.concept_count_u32 = (uint32_t)concept_ids_.size();

    return any;
}

bool LanguageFoundation::load_cmudict_dir(const std::string& dir_utf8, std::string* rep) {
    auto files = list_files_sorted(dir_utf8);
    bool any = false;
    for (const auto& f : files) {
        const std::string base = fs::path(fs::u8path(f)).filename().u8string();
        if (base.find("cmudict") != std::string::npos) {
            parse_cmudict_file(f, rep);
            any = true;
        }
    }
    if (any) rep_append(rep, "LANG_BOOTSTRAP:cmudict=OK");
    return any;
}

bool LanguageFoundation::load_wordnet_dir(const std::string& dir_utf8, std::string* rep) {
    auto files = list_files_sorted(dir_utf8);
    bool any = false;
    for (const auto& f : files) {
        const std::string base = fs::path(fs::u8path(f)).filename().u8string();
        if (base.rfind("index.", 0) == 0) {
            parse_wordnet_index_file(f, rep);
            any = true;
        } else if (base.rfind("data.", 0) == 0) {
            parse_wordnet_data_file(f, rep);
            any = true;
        }
    }
    if (any) rep_append(rep, "LANG_BOOTSTRAP:wordnet=OK");
    return any;
}

bool LanguageFoundation::load_encyclopedia_dir(const std::string& dir_utf8, std::string* rep) {
    auto files = list_files_sorted(dir_utf8);
    bool any = false;
    for (const auto& f : files) {
        if (ends_with(f, ".xml")) {
            parse_wiki_abstracts_xml_file(f, rep);
            any = true;
        } else if (ends_with(f, ".tsv")) {
            parse_wiki_abstracts_tsv_file(f, rep);
            any = true;
        }
    }
    if (any) rep_append(rep, "LANG_BOOTSTRAP:encyclopedia=OK");
    return any;
}

bool LanguageFoundation::load_speech_dir(const std::string& dir_utf8, std::string* rep) {
    auto files = list_files_sorted(dir_utf8);
    bool any = false;
    for (const auto& f : files) {
        if (!ends_with(f, ".tsv")) continue;
        const std::string base = fs::path(fs::u8path(f)).filename().u8string();
        if (base.find("validated") != std::string::npos || base.find("train") != std::string::npos || base.find("dev") != std::string::npos) {
            parse_common_voice_tsv_file(f, rep);
            any = true;
        }
    }
    if (any) rep_append(rep, "LANG_BOOTSTRAP:speech=OK");
    return any;
}

void LanguageFoundation::parse_cmudict_file(const std::string& path_utf8, std::string* rep) {
    std::ifstream in(fs::u8path(path_utf8), std::ios::binary);
    if (!in) {
        rep_append(rep, std::string("LANG_BOOTSTRAP:cmudict_open_fail:") + path_utf8);
        return;
    }

    std::string line;
    uint64_t lines = 0;
    const size_t cap = 1500000;

    while (std::getline(in, line)) {
        ++lines;
        if (line.size() < 3) continue;
        if (line.size() >= 3 && line[0] == ';' && line[1] == ';' && line[2] == ';') continue;

        // Find first whitespace
        size_t sp = line.find_first_of(" \t");
        if (sp == std::string::npos || sp == 0) continue;

        std::string word = line.substr(0, sp);
        // Strip variants WORD(1)
        size_t lp = word.find('(');
        if (lp != std::string::npos) word = word.substr(0, lp);
        if (word.empty()) continue;

        const EigenWare::EwId9 wid = EigenWare::ew_id9_from_ascii(word.data(), word.size());
        vec_insert_unique_bounded(word_ids_, wid, cap);

        size_t pos = sp;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
        if (pos < line.size()) {
            const EigenWare::EwId9 pid = EigenWare::ew_id9_from_ascii(line.data() + pos, line.size() - pos);
            vec_insert_unique_bounded(pron_ids_, pid, cap);
        }
    }

    rep_append(rep, "LANG_BOOTSTRAP:cmudict_lines=" + std::to_string((unsigned long long)lines));
}

void LanguageFoundation::parse_wordnet_index_file(const std::string& path_utf8, std::string* rep) {
    std::ifstream in(fs::u8path(path_utf8), std::ios::binary);
    if (!in) {
        rep_append(rep, std::string("LANG_BOOTSTRAP:wordnet_index_open_fail:") + path_utf8);
        return;
    }

    std::string line;
    uint64_t lines = 0;
    const size_t cap = 2000000;

    while (std::getline(in, line)) {
        ++lines;
        if (line.empty()) continue;
        if (line[0] == ' ' || line[0] == '\t' || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string lemma;
        std::string pos;
        uint32_t synset_cnt = 0;
        uint32_t p_cnt = 0;
        if (!(iss >> lemma >> pos >> synset_cnt >> p_cnt)) continue;

        const EigenWare::EwId9 wid = EigenWare::ew_id9_from_ascii(lemma.data(), lemma.size());
        vec_insert_unique_bounded(word_ids_, wid, cap);

        // Skip pointer symbols.
        for (uint32_t i = 0; i < p_cnt; ++i) {
            std::string sym;
            if (!(iss >> sym)) break;
        }

        uint32_t sense_cnt = 0;
        uint32_t tagsense_cnt = 0;
        (void)tagsense_cnt;
        iss >> sense_cnt >> tagsense_cnt;

        // Offsets for senses.
        for (uint32_t s = 0; s < sense_cnt; ++s) {
            std::string off;
            if (!(iss >> off)) break;
            const std::string key = lemma + "/" + pos + "/" + off;
            const EigenWare::EwId9 sid = EigenWare::ew_id9_from_ascii(key.data(), key.size());
            vec_insert_unique_bounded(sense_ids_, sid, cap);
        }
    }

    rep_append(rep, "LANG_BOOTSTRAP:wordnet_index_lines=" + std::to_string((unsigned long long)lines));
}

static bool parse_hex_u32(const std::string& s, uint32_t* out) {
    if (!out) return false;
    uint32_t v = 0;
    if (s.empty()) return false;
    for (char c : s) {
        uint32_t d = 0;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + (uint32_t)(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + (uint32_t)(c - 'A');
        else return false;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

void LanguageFoundation::parse_wordnet_data_file(const std::string& path_utf8, std::string* rep) {
    std::ifstream in(fs::u8path(path_utf8), std::ios::binary);
    if (!in) {
        rep_append(rep, std::string("LANG_BOOTSTRAP:wordnet_data_open_fail:") + path_utf8);
        return;
    }

    std::string line;
    uint64_t lines = 0;
    const size_t cap = 3000000;

    while (std::getline(in, line)) {
        ++lines;
        if (line.empty()) continue;
        if (line[0] == ' ' || line[0] == '\t' || line[0] == '#') continue;

        const size_t bar = line.find(" | ");
        const std::string main = (bar == std::string::npos) ? line : line.substr(0, bar);

        std::istringstream iss(main);
        std::string synset_offset, lex_filenum, ss_type;
        if (!(iss >> synset_offset >> lex_filenum >> ss_type)) continue;

        std::string w_cnt_s;
        if (!(iss >> w_cnt_s)) continue;
        uint32_t w_cnt = 0;
        if (!parse_hex_u32(w_cnt_s, &w_cnt)) continue;

        // Read words and lex_ids
        for (uint32_t i = 0; i < w_cnt; ++i) {
            std::string w;
            std::string lexid;
            if (!(iss >> w >> lexid)) break;
            // WordNet uses '_' for spaces.
            for (char& c : w) if (c == '_') c = ' ';
            const EigenWare::EwId9 wid = EigenWare::ew_id9_from_ascii(w.data(), w.size());
            vec_insert_unique_bounded(word_ids_, wid, cap);
        }

        // Pointer count
        uint32_t p_cnt = 0;
        iss >> p_cnt;
        for (uint32_t i = 0; i < p_cnt; ++i) {
            std::string ptr_sym, off, pos, src_tgt;
            if (!(iss >> ptr_sym >> off >> pos >> src_tgt)) break;
            const std::string rel = ptr_sym + "/" + pos;
            const EigenWare::EwId9 rid = EigenWare::ew_id9_from_ascii(rel.data(), rel.size());
            vec_insert_unique_bounded(relation_ids_, rid, cap);
        }
    }

    rep_append(rep, "LANG_BOOTSTRAP:wordnet_data_lines=" + std::to_string((unsigned long long)lines));
}

// Wikipedia abstracts XML dump: simple structure with <doc><title>..</title><abstract>..</abstract>
void LanguageFoundation::parse_wiki_abstracts_xml_file(const std::string& path_utf8, std::string* rep) {
    std::ifstream in(fs::u8path(path_utf8), std::ios::binary);
    if (!in) {
        rep_append(rep, std::string("LANG_BOOTSTRAP:wiki_xml_open_fail:") + path_utf8);
        return;
    }

    std::string line;
    uint64_t docs = 0;
    const size_t cap = 5000000;

    auto extract_tag = [](const std::string& s, const char* tag) -> std::string {
        const std::string open = std::string("<") + tag + ">";
        const std::string close = std::string("</") + tag + ">";
        size_t a = s.find(open);
        if (a == std::string::npos) return {};
        a += open.size();
        size_t b = s.find(close, a);
        if (b == std::string::npos) return {};
        return s.substr(a, b - a);
    };

    std::string cur_title;
    while (std::getline(in, line)) {
        if (line.find("<title>") != std::string::npos) {
            cur_title = extract_tag(line, "title");
        }
        if (!cur_title.empty() && line.find("</doc>") != std::string::npos) {
            const EigenWare::EwId9 cid = EigenWare::ew_id9_from_ascii(cur_title.data(), cur_title.size());
            vec_insert_unique_bounded(concept_ids_, cid, cap);
            cur_title.clear();
            docs++;
        }
    }

    rep_append(rep, "LANG_BOOTSTRAP:wiki_xml_docs=" + std::to_string((unsigned long long)docs));
}

// TSV format: title \t abstract
void LanguageFoundation::parse_wiki_abstracts_tsv_file(const std::string& path_utf8, std::string* rep) {
    std::ifstream in(fs::u8path(path_utf8), std::ios::binary);
    if (!in) {
        rep_append(rep, std::string("LANG_BOOTSTRAP:wiki_tsv_open_fail:") + path_utf8);
        return;
    }

    std::string line;
    uint64_t rows = 0;
    const size_t cap = 5000000;

    while (std::getline(in, line)) {
        size_t tab = line.find('\t');
        const std::string title = (tab == std::string::npos) ? line : line.substr(0, tab);
        if (!title.empty()) {
            const EigenWare::EwId9 cid = EigenWare::ew_id9_from_ascii(title.data(), title.size());
            vec_insert_unique_bounded(concept_ids_, cid, cap);
        }
        rows++;
    }

    rep_append(rep, "LANG_BOOTSTRAP:wiki_tsv_rows=" + std::to_string((unsigned long long)rows));
}

// Common Voice TSV: has header; includes sentence field.
void LanguageFoundation::parse_common_voice_tsv_file(const std::string& path_utf8, std::string* rep) {
    std::ifstream in(fs::u8path(path_utf8), std::ios::binary);
    if (!in) {
        rep_append(rep, std::string("LANG_BOOTSTRAP:cv_tsv_open_fail:") + path_utf8);
        return;
    }

    std::string header;
    if (!std::getline(in, header)) return;

    // Identify sentence column index.
    std::vector<std::string> cols;
    {
        std::string tmp;
        std::istringstream iss(header);
        while (std::getline(iss, tmp, '\t')) cols.push_back(tmp);
    }
    int sentence_idx = -1;
    for (int i = 0; i < (int)cols.size(); ++i) {
        if (cols[i] == "sentence") { sentence_idx = i; break; }
    }
    if (sentence_idx < 0) sentence_idx = (int)cols.size() - 1;

    uint64_t rows = 0;
    uint64_t word_tokens = 0;
    const size_t cap = 4000000;

    std::string line;
    while (std::getline(in, line)) {
        rows++;
        // Split tsv columns
        std::vector<std::string> parts;
        parts.reserve(cols.size());
        {
            std::string tmp;
            std::istringstream iss(line);
            while (std::getline(iss, tmp, '\t')) parts.push_back(tmp);
        }
        if (sentence_idx >= (int)parts.size()) continue;
        const std::string& sentence = parts[(size_t)sentence_idx];

        // Tokenize sentence on ASCII whitespace/punct.
        std::string cur;
        for (size_t i = 0; i <= sentence.size(); ++i) {
            const char c = (i < sentence.size()) ? sentence[i] : ' ';
            const bool is_word = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '\'');
            if (is_word) {
                if (cur.size() < 64) cur.push_back(c);
            } else {
                if (!cur.empty()) {
                    const EigenWare::EwId9 wid = EigenWare::ew_id9_from_ascii(cur.data(), cur.size());
                    vec_insert_unique_bounded(word_ids_, wid, cap);
                    word_tokens++;
                    cur.clear();
                }
            }
        }
    }

    stats_.speech_utt_count_u32 = (uint32_t)((rows > (uint64_t)0xFFFFFFFFu) ? 0xFFFFFFFFu : rows);
    stats_.speech_word_tokens_u32 = (uint32_t)((word_tokens > (uint64_t)0xFFFFFFFFu) ? 0xFFFFFFFFu : word_tokens);

    rep_append(rep, "LANG_BOOTSTRAP:cv_rows=" + std::to_string((unsigned long long)rows));
}

MetricVector LanguageFoundation::metrics_for_kind(MetricKind k) const {
    MetricVector mv;
    mv.dim_u32 = 4;
    // Encode counts in Q32.32.
    auto q = [](uint32_t x) -> int64_t { return (int64_t)x << 32; };

    if (k == MetricKind::Lang_Dictionary_LexiconStats) {
        mv.v_q32_32[0] = q(stats_.word_count_u32);
        mv.v_q32_32[1] = q(stats_.pron_count_u32);
        mv.v_q32_32[2] = q(stats_.senses_count_u32);
        mv.v_q32_32[3] = q(0u);
    } else if (k == MetricKind::Lang_Thesaurus_RelationStats) {
        mv.v_q32_32[0] = q(stats_.relations_count_u32);
        mv.v_q32_32[1] = q(stats_.senses_count_u32);
        mv.v_q32_32[2] = q(stats_.word_count_u32);
        mv.v_q32_32[3] = q(0u);
    } else if (k == MetricKind::Lang_Encyclopedia_ConceptStats) {
        mv.v_q32_32[0] = q(stats_.concept_count_u32);
        mv.v_q32_32[1] = q(stats_.word_count_u32);
        mv.v_q32_32[2] = q(0u);
        mv.v_q32_32[3] = q(0u);
    } else if (k == MetricKind::Lang_SpeechCorpus_AlignmentStats) {
        mv.v_q32_32[0] = q(stats_.speech_utt_count_u32);
        mv.v_q32_32[1] = q(stats_.speech_word_tokens_u32);
        mv.v_q32_32[2] = q(stats_.word_count_u32);
        mv.v_q32_32[3] = q(stats_.pron_count_u32);
    } else {
        mv.dim_u32 = 0;
    }

    return mv;
}

MetricTask LanguageFoundation::make_task_for_kind(MetricKind k, uint64_t source_id_u64, uint32_t source_anchor_id_u32, uint32_t context_anchor_id_u32) const {
    MetricTask t;
    t.task_id_u64 = 0; // assigned by registry
    t.source_id_u64 = source_id_u64;
    t.source_anchor_id_u32 = source_anchor_id_u32;
    t.context_anchor_id_u32 = context_anchor_id_u32;

    t.target.kind = k;
    t.target.target = metrics_for_kind(k);

    // For language checkpoints, we demand strict equality (tol=0).
    t.target.tol_num_u32 = 0;
    t.target.tol_den_u32 = 1;

    // Budget defaults: allow the standard one-second window, but since the
    // simulation is direct evaluation, it will complete at window end.
    t.tries_remaining_u64 = 0;
    t.tries_per_step_u32 = 0;
    t.ticks_remaining_u32 = 0;

    return t;
}

} // namespace genesis
