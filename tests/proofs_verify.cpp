#include <cstdio>
#include <cstdlib>
#include <string>

static std::string read_all(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return std::string(); }
    std::string s;
    s.resize((size_t)n);
    std::fread(&s[0], 1, (size_t)n, f);
    std::fclose(f);
    return s;
}

static void write_all(const std::string& path, const std::string& s) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
}

static bool json_contains_true(const std::string& j, const char* key) {
    // Minimal deterministic check: find "<key>": {"pass": true
    const std::string needle = std::string("\"") + key + "\": {\"pass\": true";
    return j.find(needle) != std::string::npos;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    const std::string j = read_all("contract_metrics.json");
    const bool ok =
        json_contains_true(j, "determinism") &&
        json_contains_true(j, "conservation") &&
        json_contains_true(j, "decoherence") &&
        json_contains_true(j, "identity");

    write_all("integration_proofs.json", std::string("{\n  \"integration_proofs_pass\": ") + (ok ? "true" : "false") + "\n}\n");
    return ok ? 0 : 3;
}
