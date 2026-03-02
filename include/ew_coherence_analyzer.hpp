#pragma once

#include <string>

// Coherence analyzer: deterministic static checks for naming and routing.
//
// Scope here is intentionally small and stable:
// - Validate operator name surface for ASCII safety.
// - Detect duplicates.
// - Emit a deterministic report string.

namespace ew_coherence_analyzer {

// Returns true if the name surface is coherent.
// On failure, out_report contains a deterministic, newline-separated report.
bool ew_analyze_operator_name_surface(std::string& out_report);

} // namespace ew_coherence_analyzer
