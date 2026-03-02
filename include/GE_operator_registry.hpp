#pragma once

#include "GE_runtime.hpp"
#include "spec_aux_ops.hpp"

// -----------------------------------------------------------------------------
//  Canonical Operator Registry (Blueprint compliance)
// -----------------------------------------------------------------------------
// All operators below are deterministic. evolve_state / commit_state are the
// only ones that create/commit mutable state transitions.
//
// NOTE: These are defined as free functions with lower_snake_case names to
// match the Blueprint registry and to prevent accidental overloading.

EwState evolve_state(const EwState& current_state, const EwInputs& inputs, const EwCtx& ctx);
EwLedger compute_ledger(const EwState& state);
EwLedgerDelta compute_ledger_delta(const EwState& current_state, const EwState& candidate_next_state, const EwCtx& ctx);
bool accept_state(const EwState& current_state, const EwState& candidate_next_state, const EwLedgerDelta& ledger_delta, const EwCtx& ctx);
void commit_state(EwState& current_state, const EwState& next_state);
EwState make_sink_state(const EwState& current_state, const EwCtx& ctx);

int reality_label(int64_t nexus_turns_q, const EwCtx& ctx);
bool is_reality_shift(int64_t prev_nexus_turns_q, int64_t next_nexus_turns_q, const EwCtx& ctx);

// -----------------------------------------------------------------------------
//  Blueprint C: Object Memory Reference Operator (OMRO)
// -----------------------------------------------------------------------------

// Insert or replace an immutable object entry in the store.
bool object_store_upsert(EwState& state, const EwObjectEntry& entry);

// Operator: object_import_request
// Updates the target anchor to reference the object_id, debiting the ledger.
// Reject codes:
//  0 = accepted
//  1 = object_not_found
//  2 = budget_insufficient
//  3 = reservoir_insufficient
//  4 = synthesis_missing (object has no voxel volume)
bool object_import_request(EwState& state,
                           uint32_t target_anchor_id,
                           uint64_t object_id_u64,
                           int64_t energy_budget_q32_32,
                           const EwCtx& ctx,
                           uint32_t* out_reject_code);

// Operator: object_synthesize_voxelize
// Deterministically voxelizes an object into an occupancy_u8 volume stored in OMRO.
// This is the mandatory Genesis synthesis step: imported assets are not
// physics/control-usable until a voxel volume exists.
//
// Parameters:
//  - object_id_u64: object to synthesize
//  - grid_x/y/z: output volume dimensions
//
// Reject codes:
//  0 = accepted
//  1 = object_not_found
//  2 = invalid_grid
bool object_synthesize_voxelize(EwState& state,
                                uint64_t object_id_u64,
                                uint32_t grid_x_u32,
                                uint32_t grid_y_u32,
                                uint32_t grid_z_u32,
                                const EwCtx& ctx,
                                uint32_t* out_reject_code);

// -----------------------------------------------------------------------------
//  Operator name surface (validator support)
// -----------------------------------------------------------------------------
// Returns the list of canonical operator names implemented by this build.
// This is used by the validator to enforce registry completeness.
struct EwOpNameList {
    const char* const* names;
    uint32_t count;
};

EwOpNameList ew_operator_name_list();
