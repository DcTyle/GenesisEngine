#pragma once
#include <cstdint>
#include "ew_types.h"

// Canonical ingress validator per Spec v7.
//
// Ingress is binary-framed and fail-closed. This implementation validates the
// structural constraints required for deterministic runtime evolution.
enum EwIngressViolation : uint32_t {
    EW_INGRESS_OK = 0,
    EW_INGRESS_NULL_PACKET = 1,
    EW_INGRESS_FREQ_ZERO = 2,
    EW_INGRESS_WIDTH_ZERO = 3,
    EW_INGRESS_AMP_OUT_OF_RANGE = 4
};

inline EwIngressViolation ew_pulsepacketv1_validate(const PulsePacketV1* p) {
    if (!p) return EW_INGRESS_NULL_PACKET;
    if (p->freq_hz_u32 == 0) return EW_INGRESS_FREQ_ZERO;
    if (p->width_ns_u32 == 0) return EW_INGRESS_WIDTH_ZERO;

    // Deterministic bound for Q32.32 amplitude: |amp| <= 16.0 by default.
    // This is a structural safety bound; higher ranges can be enabled later via spec gates.
    const int64_t amp_abs = (p->amp_q32_32 < 0) ? -p->amp_q32_32 : p->amp_q32_32;
    const int64_t amp_cap = (int64_t)(16) << 32;
    if (amp_abs > amp_cap) return EW_INGRESS_AMP_OUT_OF_RANGE;

    return EW_INGRESS_OK;
}

