#pragma once

#include "GE_runtime.hpp"
#include "frequency_collapse.hpp"
#include "fixed_point.hpp"

#include <cstdint>

// -----------------------------------------------------------------------------
// Trace toggle
// -----------------------------------------------------------------------------
// For performance builds, carrier-trace recording can be compiled out.
// Semantics of arithmetic MUST remain identical.
#ifndef EW_ALU_TRACE_ENABLE
#define EW_ALU_TRACE_ENABLE 0
#endif

#if defined(_MSC_VER)
#define EW_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define EW_FORCE_INLINE inline __attribute__((always_inline))
#else
#define EW_FORCE_INLINE inline
#endif

// -----------------------------------------------------------------------------
// Substrate ALU (carrier-harmonic microprocessor)
// -----------------------------------------------------------------------------
// Intent:
// - Every arithmetic operation in the runtime MAY be accompanied by a
//   deterministic carrier-wave collapse derived from its operands.
// - This makes "carrier frequencies" the universal processing substrate
//   for inspection and verification, while keeping semantics unchanged.
// - No randomness, no hidden state: only ancilla trace fields update.

// Derive a stable carrier id from two fixed-point operands.
// The mapping is intentionally simple and deterministic:
// - frequency bins are derived from operand magnitudes projected into TURN_SCALE
// - amplitudes are derived from absolute operand magnitudes (clamped)
// - phases are derived from operands projected into turns
uint64_t ew_alu_carrier_id_u64_from_q32_32_pair(const EwCtx& ctx, int64_t a_q32_32, int64_t b_q32_32);

// Derive a stable carrier id from two operands expressed in TURN_SCALE domain.
// This helper converts TURN_SCALE turns -> Q32.32 turns before collapsing.
uint64_t ew_alu_carrier_id_u64_from_turns_pair(const EwCtx& ctx, int64_t a_turns_q, int64_t b_turns_q);

// Record an ALU micro-op into ancilla trace fields.
EW_FORCE_INLINE void ew_alu_trace(ancilla_particle* an, uint64_t carrier_id_u64) {
#if EW_ALU_TRACE_ENABLE
    if (!an) return;
    an->last_carrier_id_u64 = carrier_id_u64;
    an->microop_count_u32 += 1u;
#else
    (void)an;
    (void)carrier_id_u64;
#endif
}

// Trace-only micro-op for arbitrary operand pairs.
EW_FORCE_INLINE void ew_alu_trace_pair(const EwCtx& ctx, ancilla_particle* an, int64_t a_q32_32, int64_t b_q32_32) {
#if EW_ALU_TRACE_ENABLE
    const uint64_t cid = ew_alu_carrier_id_u64_from_q32_32_pair(ctx, a_q32_32, b_q32_32);
    ew_alu_trace(an, cid);
#else
    (void)ctx;
    (void)an;
    (void)a_q32_32;
    (void)b_q32_32;
#endif
}

// Trace-only helper for TURN_SCALE-domain operands.
EW_FORCE_INLINE void ew_alu_trace_turns_pair(const EwCtx& ctx, ancilla_particle* an, int64_t a_turns_q, int64_t b_turns_q) {
#if EW_ALU_TRACE_ENABLE
    const uint64_t cid = ew_alu_carrier_id_u64_from_turns_pair(ctx, a_turns_q, b_turns_q);
    ew_alu_trace(an, cid);
#else
    (void)ctx;
    (void)an;
    (void)a_turns_q;
    (void)b_turns_q;
#endif
}

// Carrier-traced Q32.32 operations.
// These return the canonical arithmetic result, but also update ancilla trace.
EW_FORCE_INLINE int64_t ew_alu_add_q32_32(const EwCtx& ctx, ancilla_particle* an, int64_t x_q32_32, int64_t y_q32_32) {
#if EW_ALU_TRACE_ENABLE
    ew_alu_trace(an, ew_alu_carrier_id_u64_from_q32_32_pair(ctx, x_q32_32, y_q32_32));
#else
    (void)ctx;
    (void)an;
#endif
    return x_q32_32 + y_q32_32;
}

EW_FORCE_INLINE int64_t ew_alu_sub_q32_32(const EwCtx& ctx, ancilla_particle* an, int64_t x_q32_32, int64_t y_q32_32) {
#if EW_ALU_TRACE_ENABLE
    ew_alu_trace(an, ew_alu_carrier_id_u64_from_q32_32_pair(ctx, x_q32_32, y_q32_32));
#else
    (void)ctx;
    (void)an;
#endif
    return x_q32_32 - y_q32_32;
}

EW_FORCE_INLINE int64_t ew_alu_mul_q32_32(const EwCtx& ctx, ancilla_particle* an, int64_t x_q32_32, int64_t y_q32_32) {
#if EW_ALU_TRACE_ENABLE
    ew_alu_trace(an, ew_alu_carrier_id_u64_from_q32_32_pair(ctx, x_q32_32, y_q32_32));
#else
    (void)ctx;
    (void)an;
#endif
    return mul_q32_32(x_q32_32, y_q32_32);
}

EW_FORCE_INLINE int64_t ew_alu_div_q32_32(const EwCtx& ctx, ancilla_particle* an, int64_t x_q32_32, int64_t y_q32_32) {
#if EW_ALU_TRACE_ENABLE
    ew_alu_trace(an, ew_alu_carrier_id_u64_from_q32_32_pair(ctx, x_q32_32, y_q32_32));
#else
    (void)ctx;
    (void)an;
#endif
    return div_q32_32(x_q32_32, y_q32_32);
}
