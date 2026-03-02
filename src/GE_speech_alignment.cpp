#include "GE_speech_alignment.hpp"

#include <cstdio>
#include <cstring>

namespace genesis {

static bool ge_read_all(const std::string& path, std::string* out) {
    if (!out) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); return false; }
    out->resize((size_t)sz);
    if (sz > 0) {
        if (std::fread(out->data(), 1, (size_t)sz, f) != (size_t)sz) { std::fclose(f); return false; }
    }
    std::fclose(f);
    return true;
}

static std::vector<std::string> ge_split_tab(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('\t', i);
        if (j == std::string::npos) j = s.size();
        out.push_back(s.substr(i, j - i));
        i = j + 1;
        if (j == s.size()) break;
    }
    return out;
}

static void ge_trim_cr(std::string* s) {
    while (!s->empty() && (s->back() == '\r' || s->back()=='\n')) s->pop_back();
}

SpeechCorpusLoadResult ge_load_wav_text_manifest(const std::string& manifest_path, uint32_t max_samples_u32) {
    SpeechCorpusLoadResult r;
    std::string txt;
    if (!ge_read_all(manifest_path, &txt)) { r.info = "failed_read"; return r; }

    uint32_t cap = (max_samples_u32 == 0) ? 100000u : max_samples_u32;

    size_t line_start = 0;
    while (line_start < txt.size() && r.samples.size() < cap) {
        size_t line_end = txt.find('\n', line_start);
        if (line_end == std::string::npos) line_end = txt.size();
        std::string line = txt.substr(line_start, line_end - line_start);
        ge_trim_cr(&line);
        line_start = line_end + 1;

        if (line.empty()) continue;
        auto cols = ge_split_tab(line);
        if (cols.size() < 2) continue;
        SpeechAlignedSample s;
        s.wav_path = cols[0];
        s.text_utf8 = cols[1];
        r.samples.push_back(std::move(s));
    }

    r.ok = true;
    r.samples_u32 = (uint32_t)r.samples.size();
    r.info = "ok";
    return r;
}

SpeechCorpusLoadResult ge_load_common_voice_tsv(const std::string& tsv_path, const std::string& clips_dir_prefix, uint32_t max_samples_u32) {
    SpeechCorpusLoadResult r;
    std::string txt;
    if (!ge_read_all(tsv_path, &txt)) { r.info = "failed_read"; return r; }

    uint32_t cap = (max_samples_u32 == 0) ? 50000u : max_samples_u32;

    // Parse header
    size_t line_end = txt.find('\n');
    if (line_end == std::string::npos) { r.info = "bad_tsv"; return r; }
    std::string header = txt.substr(0, line_end);
    ge_trim_cr(&header);
    auto hdr = ge_split_tab(header);

    int idx_path = -1;
    int idx_sentence = -1;
    int idx_client = -1;
    int idx_locale = -1;
    for (size_t i = 0; i < hdr.size(); ++i) {
        const std::string& h = hdr[i];
        if (h == "path") idx_path = (int)i;
        else if (h == "sentence") idx_sentence = (int)i;
        else if (h == "client_id") idx_client = (int)i;
        else if (h == "locale") idx_locale = (int)i;
    }
    if (idx_path < 0 || idx_sentence < 0) { r.info = "missing_cols"; return r; }

    size_t off = line_end + 1;
    while (off < txt.size() && r.samples.size() < cap) {
        size_t e = txt.find('\n', off);
        if (e == std::string::npos) e = txt.size();
        std::string line = txt.substr(off, e - off);
        ge_trim_cr(&line);
        off = e + 1;
        if (line.empty()) continue;

        auto cols = ge_split_tab(line);
        if ((int)cols.size() <= std::max(idx_path, idx_sentence)) continue;

        SpeechAlignedSample s;
        std::string rel = cols[(size_t)idx_path];
        if (!clips_dir_prefix.empty()) {
            if (clips_dir_prefix.back() == '/' || clips_dir_prefix.back() == '\\') s.wav_path = clips_dir_prefix + rel;
            else s.wav_path = clips_dir_prefix + "/" + rel;
        } else {
            s.wav_path = rel;
        }
        s.text_utf8 = cols[(size_t)idx_sentence];
        if (idx_client >= 0 && (size_t)idx_client < cols.size()) s.speaker_id = cols[(size_t)idx_client];
        if (idx_locale >= 0 && (size_t)idx_locale < cols.size()) s.locale = cols[(size_t)idx_locale];
        r.samples.push_back(std::move(s));
    }

    r.ok = true;
    r.samples_u32 = (uint32_t)r.samples.size();
    r.info = "ok";
    return r;
}

} // namespace genesis
