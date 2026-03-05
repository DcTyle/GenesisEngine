#include "ew_coherence_analyzer.hpp"

#include "GE_operator_registry.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ew_coherence_analyzer {

static bool is_ascii_printable(char c) {
    const unsigned char u = (unsigned char)c;
    return (u >= 32u) && (u <= 126u);
}

bool ew_analyze_operator_name_surface(std::string& out_report) {
    out_report.clear();

    const EwOpNameList names = ew_operator_name_list();
    std::vector<std::string> seen;
    seen.reserve(names.count);

    bool ok = true;
    for (uint32_t i = 0; i < names.count; ++i) {
        const char* n = names.names[i];
        if (!n || !*n) {
            ok = false;
            out_report += "ERR: empty operator name at index=" + std::to_string(i) + "\n";
            continue;
        }
        std::string s(n);

        // ASCII safety: deterministic, locale-free.
        for (char c : s) {
            if (!is_ascii_printable(c)) {
                ok = false;
                out_report += "ERR: non-ascii-printable char in operator name: " + s + "\n";
                break;
            }
        }

        // Naming: [a-z0-9_]
        for (char c : s) {
            const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '_');
            if (!allowed) {
                ok = false;
                out_report += "ERR: invalid char in operator name (allowed [a-z0-9_]): " + s + "\n";
                break;
            }
        }

        seen.push_back(std::move(s));
    }

    std::sort(seen.begin(), seen.end());
    for (size_t i = 1; i < seen.size(); ++i) {
        if (seen[i] == seen[i-1]) {
            ok = false;
            out_report += "ERR: duplicate operator name: " + seen[i] + "\n";
        }
    }

    if (ok) {
        out_report = "OK: operator name surface coherent (count=" + std::to_string((unsigned)names.count) + ")\n";
    }
    return ok;
}

} // namespace ew_coherence_analyzer
