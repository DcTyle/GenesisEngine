#pragma once
#include <string>
#include <cstdint>

// Forward declaration to avoid heavy includes.
class SubstrateManager;

// -----------------------------------------------------------------------------
// EwAiAnticipator
// -----------------------------------------------------------------------------
// Deterministic "anticipation" layer: routes plain UI text into a structured
// command prefix (QUERY:/WEBSEARCH:/WEBFETCH:/OPEN:) using lexical cues plus
// local corpus grounding (corpus_query_best_score).
//
// This is NOT an ML model. It is a deterministic predictive-programming shim
// that makes the UI usable: the user can type naturally and the substrate will
// receive a consistent command form.
//
// Contract:
// - Never rewrites explicit commands (anything containing ':' in the first token,
//   or starting with '/', or already starting with known prefixes).
// - Produces stable output for identical inputs.
// - Provides an auditable ui_tag string explaining the route.
// -----------------------------------------------------------------------------
struct EwAiAnticipator {
    // Returns true if a rewrite is suggested. out_line is the suggested command
    // line. ui_tag is an optional UI telemetry string.
    bool route(SubstrateManager* sm,
               const std::string& in_line,
               std::string& out_line,
               std::string& ui_tag) const;
};
