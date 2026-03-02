#pragma once
#include <cstdint>

// Canonical phase-binary equation page opcode enumeration per Spec v7.
// This repo currently binds operator pages to packed templates; this enum is a stable surface
// for later expansion of true microcode execution.
enum EwEqOpcode : uint32_t {
    EW_EQ_NOP = 0,
    EW_EQ_PHASE_TRANSPORT_DTHETA = 0x10,
    EW_EQ_CRITICAL_MASS_CEILING = 0x20,
    EW_EQ_PROJECT_COH_DOT = 0x30,
    EW_EQ_CONSTRAIN_PI_G = 0x40,
    EW_EQ_Q32_32_MUL = 0x50
};

