#pragma once

#include <cstdint>

#include "GE_metric_registry.hpp"

namespace genesis {

// GPU-driven learning checkpoint update.
//
// This function performs exactly one per-tick update for a single MetricTask:
// - Optional deterministic "reverse-calc" first attempt (params derived from target).
// - Otherwise, executes a batched stochastic exploration step on the GPU.
// - Updates best-fit state, decrements budgets, and finalizes acceptance when the
//   request window is exhausted.
//
// IMPORTANT: This is compute-only; CPU is allowed to orchestrate (launch kernels,
// move small structs) but MUST NOT perform the search/fit loop.
bool ew_learning_gate_tick_cuda(
    MetricTask* task_host,
    uint64_t canonical_tick_u64,
    uint64_t tries_this_tick_u64,
    uint32_t steps_this_tick_u32
);

// Bind a read-only view of the authoritative GPU lattice so learning metrics can
// be derived from measurable field state.
//
// NOTE: The learning kernels treat these pointers as read-only; world evolution
// remains owned by the lattice step kernels.
bool ew_learning_bind_world_lattice_cuda(
    const float* d_E_curr,
    const float* d_flux,
    const float* d_coherence,
    const float* d_curvature,
    const float* d_doppler,
    int gx,
    int gy,
    int gz
);

// Bind a read-only view of the learning sandbox/probe lattice.
// Learning kernels may use this lattice as the evolution target for parameter
// molding and metric extraction, without perturbing the world lattice.
bool ew_learning_bind_probe_lattice_cuda(
    const float* d_E_curr,
    const float* d_flux,
    const float* d_coherence,
    const float* d_curvature,
    const float* d_doppler,
    int gx,
    int gy,
    int gz
);

} // namespace genesis
