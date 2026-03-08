# Genesis Engine — Hilbert Anchor Actuation Contract

This patch intentionally branches from the pre-annotation/encoder baseline. It does **not** route CPU-owned files through the substrate annotation ingest spine.

## Purpose

The immediate goal is to make the anchor-side calculus gate explicit before any broader CPU-to-substrate encoding work continues. In this repo line, all active dynamical computation is supposed to occur through anchor Hilbert-state evolution, not through ad hoc CPU-side metadata migration.

## Canonical rules in this pass

1. There is still exactly one evolution path: `evolve_state(...)` in `src/GE_operator_registry.cpp`.
2. Hilbert-space actuation is now described through one bounded helper layer in `GE_hilbert_actuation.*`.
3. The helper layer exposes two things only:
   - the tick-level actuation budget
   - the per-anchor bounded phase/force envelope
4. This pass does **not** introduce a second operator registry, alternate scheduler, compatibility shim, or CPU encoder route.

## Budget terms

`EwHilbertActuationBudget` carries the canonical tick-level gating terms:
- `energy_budget_q32_32`
- `abs_zero_floor_q32_32`
- `ambient_temp_q32_32`
- `cmb_sink_turns_q`
- `force_magnitude_turns_q`
- `allow_state_update`
- `allow_force_update`

These are the same physical-surrogate gates that were already being used inline in `evolve_state(...)`; they are now made explicit so anchor actuation can be reasoned about before any encoder work proceeds.

## Anchor terms

`EwAnchorHilbertActuation` carries the per-anchor bounded envelope:
- `anchor_id_u32`
- `coherence_q32_32`
- `local_phase_headroom_q32_32`
- `max_phase_step_turns_q`
- `max_force_step_turns_q`
- `allow_force_update`

This pass uses `anchor.chi_q` relative to the existing `ctx.td_params.chi_ref_turns_q` as the canonical local coherence surrogate. That is not a final theory of anchor calculus. It is a deterministic contract surface that can be refined later without reviving CPU encoder work first.

## Why this pass exists

The recent annotation/encoder passes were premature for CPU-owned files. The correct sequencing is:

1. expose the anchor Hilbert-actuation contract
2. settle the actuation semantics and operator bounds
3. only then decide which CPU surfaces, if any, should be substrate-routed

That sequencing is what this corrective pass restores.
