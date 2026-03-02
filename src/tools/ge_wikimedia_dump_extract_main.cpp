#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ew_cli_args.hpp"

static bool write_file(const std::string& path, const std::string& s) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t n = std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
    return n == s.size();
}

static bool ensure_dir(const std::string& p) {
#ifdef _WIN32
    std::string cmd = "mkdir \"" + p + "\" 2>nul";
#else
    std::string cmd = "mkdir -p \"" + p + "\"";
#endif
    return std::system(cmd.c_str()) == 0;
}

// Extremely small deterministic XML text extractor for Wikimedia dumps.
// - Reads uncompressed .xml
// - Extracts <title>...</title> and <text ...>...</text>
// - Writes page files under out_root/dumps.wikimedia.org/wiki_pages/
// This is an offline adapter used to avoid network nondeterminism.

int main(int argc, char** argv) {
    ew::CliArgsKV args;
    if (!ew::ew_cli_parse_kv_ascii(argc, argv, args)) {
        std::fprintf(stderr, "usage: ge_wikimedia_dump_extract in=<dump.xml> out_dir=<root> [max_pages=N]\n");
        return 2;
    }
    auto get_str_def = [&](const char* k, const char* defv)->std::string {
        std::string out;
        if (ew::ew_cli_get_str(args, k, out)) return out;
        return std::string(defv ? defv : "");
    };
    auto get_u32_def = [&](const char* k, uint32_t defv)->uint32_t {
        uint32_t out = defv;
        (void)ew::ew_cli_get_u32(args, k, out);
        return out;
    };

    const std::string in = get_str_def("in", "");
    const std::string out_root = get_str_def("out_dir", "");
    const uint32_t max_pages = get_u32_def("max_pages", 1000u);

    if (in.empty() || out_root.empty()) {
        std::fprintf(stderr, "usage: ge_wikimedia_dump_extract in=<dump.xml> out_dir=<root> [max_pages=N]\n");
        return 2;
    }

    FILE* f = std::fopen(in.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "failed_open: %s\n", in.c_str());
        return 3;
    }

    const std::string out_dir = out_root + "/dumps.wikimedia.org/wiki_pages";
    if (!ensure_dir(out_root) || !ensure_dir(out_root + "/dumps.wikimedia.org") || !ensure_dir(out_dir)) {
        std::fprintf(stderr, "failed_mkdir: %s\n", out_dir.c_str());
        std::fclose(f);
        return 4;
    }

    std::string buf;
    buf.reserve(1 << 20);
    std::vector<char> chunk(1 << 16);
    for (;;) {
        const size_t n = std::fread(chunk.data(), 1, chunk.size(), f);
        if (n == 0) break;
        buf.append(chunk.data(), n);
        if (buf.size() > (1ull << 27)) {
            // Hard cap: do not buffer enormous dumps in memory.
            break;
        }
    }
    std::fclose(f);

    auto find_tag = [&](const std::string& s, const char* tag, size_t start)->size_t {
        const std::string t = std::string("<") + tag;
        return s.find(t, start);
    };

    uint32_t wrote = 0;
    size_t p = 0;
    while (wrote < max_pages) {
        const size_t page_a = buf.find("<page>", p);
        if (page_a == std::string::npos) break;
        const size_t page_b = buf.find("</page>", page_a);
        if (page_b == std::string::npos) break;
        const std::string page = buf.substr(page_a, page_b - page_a);

        std::string title;
        {
            const size_t ta = page.find("<title>");
            const size_t tb = page.find("</title>", ta == std::string::npos ? 0 : ta);
            if (ta != std::string::npos && tb != std::string::npos && tb > ta + 7) {
                title = page.substr(ta + 7, tb - (ta + 7));
            }
        }
        std::string text;
        {
            const size_t xa = page.find("<text");
            if (xa != std::string::npos) {
                const size_t gt = page.find('>', xa);
                const size_t xb = page.find("</text>", gt == std::string::npos ? xa : gt);
                if (gt != std::string::npos && xb != std::string::npos && xb > gt + 1) {
                    text = page.substr(gt + 1, xb - (gt + 1));
                }
            }
        }
        if (!text.empty()) {
            // Deterministic file name: page_<index>.txt
            const std::string path = out_dir + "/page_" + std::to_string((unsigned long long)(wrote + 1)) + ".txt";
            std::string payload;
            if (!title.empty()) {
                payload += title;
                payload += "\n\n";
            }
            payload += text;
            payload += "\n";
            if (!write_file(path, payload)) {
                std::fprintf(stderr, "failed_write: %s\n", path.c_str());
                return 5;
            }
            wrote += 1;
        }
        p = page_b + 7;
    }

    std::printf("GE_WIKIMEDIA_EXTRACT:wrote_pages=%u out_dir=%s\n", (unsigned)wrote, out_dir.c_str());
    return 0;
}
