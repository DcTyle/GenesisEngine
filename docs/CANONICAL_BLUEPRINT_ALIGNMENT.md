# Canonical Blueprint Alignment

This repository is aligned against the following canonical sources:

- `GenesisSubstrate_eq.md`
- `GenesisengineSpec.md`
- `GenesisEngineBlueprint.md`

This alignment pass treats those documents as authoritative for the research-confinement,
GPU pulse, and process-substrate execution path.

## Canonical Runtime Rule

The active runtime contract remains:

```text
candidate_next_state = evolve_state(current_state, inputs, ctx)
ledger_delta = compute_ledger_delta(current_state, candidate_next_state, ctx)

if accept_state(current_state, candidate_next_state, ledger_delta, ctx):
    commit_state(candidate_next_state)
else:
    commit_state(sink_state)
```

The repository exposes that contract in:

- `ResearchConfinement/process_substrate_calculus_encoding_schema.json`
- `docs/PROCESS_SUBSTRATE_CALCULUS_OS.md`
- `include/GE_temporal_summaries.hpp`
- `include/GE_runtime.hpp`
- `src/GE_runtime.cpp`

Legacy engine type names remain in place for ABI and build stability, but the
canonical state names are now preserved in the schema and runtime alignment docs.

## GPU Pulse And Lattice Alignment

The research runtime now aligns to the blueprint requirements in these ways:

- Pulse prediction is lattice-backed instead of being only an external parameter sweep.
- Predicted interference is fed directly into temporal coupling for the next emitted GPU pulse.
- Axis-local scaling is explicit and deterministic:
  - `sx_q32_32` from pulse frequency
  - `sy_q32_32` from pulse amplitude
  - `sz_q32_32` from joint frequency and amplitude
- Modality-axis binding is preserved in the lattice probe:
  - text -> x
  - image -> y
  - audio -> z
- The runtime publishes lattice probe norms, prediction-surface lattice coordinates,
  next-pulse correction strength, and next-pulse quartet into the research artifacts.

Implementation map:

- `src/GE_research_confinement.cpp`
- `include/GE_research_confinement.hpp`
- `vulkan_app/src/GE_app.cpp`
- `ResearchConfinement/process_substrate_calculus_encoding_schema.json`

## Produced Artifacts

The canonical-aligned runtime surfaces are:

- `ResearchConfinement/gpu_kernel_interference_prediction_surface.json`
- `ResearchConfinement/live_compute_interference_ledger.json`
- `ResearchConfinement/photon_volume_expansion.gevsd`

These artifacts now carry the fields needed to audit:

- lattice-backed prediction inputs
- axis-local scale state
- predicted interference
- temporal coupling
- next-pulse correction
- live compute readiness

## Verification Expectation

Any production-aligned change in this area should keep the following green:

- `test_research_confinement`
- `genesis_runtime`
- `genesis_editor`

This file is the repository-local alignment note for the canonical blueprint set.
