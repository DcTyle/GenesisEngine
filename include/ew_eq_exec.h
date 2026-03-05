#pragma once
#include <cstdint>
#include <vector>

#include "ew_eq_pages.h"

class SubstrateMicroprocessor;

// Canonical equation-page executor surface per Spec v7.
// In this repo iteration, execution is implemented via packed operator packets.
// This header provides a stable signature for later migration to true page microcode.
struct EwEqExecResult {
    bool ok = false;
    uint32_t op_executed_u32 = 0;
};

EwEqExecResult ew_eq_exec_packet(SubstrateMicroprocessor* sm,
                                 uint32_t opcode_u32,
                                 const std::vector<uint64_t>& args_u64);

