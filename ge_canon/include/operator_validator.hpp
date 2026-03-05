#pragma once

#include <string>

// -----------------------------------------------------------------------------
// Operator Validator
// -----------------------------------------------------------------------------
// This file provides a deterministic validation surface that can be called
// from tests or integration harnesses.
//
// The validator is intentionally conservative:
// - It checks that canonical operators are callable.
// - It runs small deterministic computations with fixed inputs.
// - It exercises a minimal "actuate" path via SubstrateMicroprocessor tick.
//
// No external resources are accessed.

struct EwOperatorValidationReport {
    bool ok = false;
    std::string details;
};

EwOperatorValidationReport validate_operators_and_microprocessor();
