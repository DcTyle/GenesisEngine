# Process Substrate Calculus OS

Purpose:
- Bind the canonical rules in `GenesisSubstrate_eq.md` to the runtime that already exists in this repository.
- Treat simulation calculus, kernel processing, feedback control, and external API integration as one deterministic process substrate.
- Keep the adapter boundary thin: external systems execute side effects, but they do not own state evolution.

This file does not add new canonical operator names.
It maps existing runtime surfaces onto the canonical names already defined in `GenesisSubstrate_eq.md`.

## 1. Canonical Runtime Mapping

Canonical substrate evolution already exists in the runtime:

```text
candidate_next_state = evolve_state(current_state, inputs, ctx)
ledger_delta = compute_ledger_delta(current_state, candidate_next_state, ctx)

if accept_state(current_state, candidate_next_state, ledger_delta, ctx):
    commit_state(candidate_next_state)
else:
    commit_state(make_sink_state(current_state, ctx))
```

Concrete bindings:
- `current_state` -> `EwState` in `include/GE_runtime.hpp`
- `inputs` -> `EwInputs` in `include/GE_runtime.hpp`
- `ctx` -> `EwCtx` in `include/GE_runtime.hpp`
- `evolve_state(...)` -> `GE_operator_registry.hpp` / `GE_operator_registry.cpp`
- `compute_ledger_delta(...)` -> `GE_operator_registry.hpp` / `GE_operator_registry.cpp`
- `accept_state(...)` -> `GE_operator_registry.hpp` / `GE_operator_registry.cpp`
- `make_sink_state(...)` -> `GE_operator_registry.hpp` / `GE_operator_registry.cpp`
- authoritative commit path -> `src/GE_runtime.cpp`

The live commit sequence is already wired in `SubstrateManager::tick()`:
- build `candidate_next_state`
- compute `ledger_delta`
- accept or collapse to sink
- commit back into live runtime state

That means the product should be framed as a process substrate operating system, not as a separate simulation stack layered beside the substrate.

## 2. Process Substrate Interpretation

The correct product interpretation is:
- GPU kernels are process microprocessors inside the substrate.
- Pulse readings are boundary observations, not direct state mutation.
- Simulation calculus is derived from deterministic state transitions inside the substrate tick.
- External APIs are adapter-executed side effects scheduled by substrate state.

This is already visible in the runtime:
- raw GPU pulse samples are submitted through `submit_gpu_pulse_sample_v2(...)`
- derived scale factors and control effects are computed during tick, not at the adapter boundary
- external API requests are emitted by the substrate and returned as response packets or ingest chunks

In other words, the substrate is the operating system.
The adapter layer is only a transport and execution boundary.

## 3. Calculus Encoding Contract

The repository already contains the right low-level pieces for a calculus encoding:
- intent plane: actuation and forcing
- measured plane: spectral response
- residual plane: discrepancy and collapse routing
- operator plane: bounded feedback updates

Runtime surfaces:
- intent plane -> `EwIntentSummary`, `EwActuationPacket`, `forcing_hat`
- measured plane -> `EwMeasuredSummary`, `phi_hat`, energy and leakage summaries
- residual plane -> `EwResidualSummary`
- operator plane -> `op_gain_q15`, `op_phase_bias_q15`, `op_band_w_q15`, hook packets

Recommended encoding model per tick:

```text
intent_k = low_band(forcing_hat_k)
measured_k = low_band(phi_hat_k)
error_k = measured_k - intent_k
delta_error_k = error_k - error_k_minus_1
integral_error_k = integral_error_k_minus_1 + error_k * dt
operator_update_k = bounded_feedback(error_k, delta_error_k, integral_error_k)
```

This makes calculus a substrate-native encoding:
- differential terms come from sequential tick deltas
- integral terms come from bounded accumulators
- operator updates are committed only through the canonical accept/commit path
- no external system writes directly into the authoritative state

## 4. Feedback Control Operating System

The feedback control operating system should be defined as this closed loop:

1. Boundary ingestion
- GPU kernel pulse samples, text, image, audio, and sensor data enter through `EwInputs`.

2. Deterministic state evolution
- `evolve_state(current_state, inputs, ctx)` produces `candidate_next_state`.

3. Acceptance and conservation
- `compute_ledger_delta(...)` and `accept_state(...)` gate all updates.
- rejection collapses deterministically to `sink_state`.

4. Microprocessor feedback
- spectral field anchors compute intent, measured response, residual, leakage, and bounded operator adaptation.
- coherence bus anchors translate residual and leakage into hook packets.
- voxel coupling anchors provide boundary-conditioned forcing.

5. External integration
- substrate emits `EwExternalApiRequest` packets.
- adapter executes requests.
- adapter returns `EwExternalApiResponse` or ingest chunks.
- substrate ingests the returned bytes on later ticks.

6. Next tick
- the returned evidence becomes part of the next deterministic evolution cycle.

This satisfies the product goal:
- compute inside the substrate
- schedule outside actions through APIs
- preserve deterministic ownership of state and control

## 5. Product Rules

The process substrate OS should follow these rules:

- Do not treat the adapter as a decision engine.
- Do not let external systems mutate substrate state directly.
- Do not compute derived pulse factors outside the substrate tick.
- Do not create a separate "simulation kernel" authority beside the canonical operator path.
- Do use the spectral field, coherence bus, and voxel coupling anchors as bounded kernel-process primitives.
- Do export calculus telemetry as a deterministic encoding derived from committed state.
- Do keep every control update admissible through `accept_state(...)`.

## 6. Recommended Build Direction

Phase 1:
- Treat the existing spectral field anchor as the first process-substrate kernel.
- Export intent, measured, residual, and operator summaries as the canonical calculus telemetry.

Phase 2:
- Add a bounded process controller that consumes calculus telemetry and writes hook packets only.
- Keep controller outputs limited to operator replacement, dt scaling, learning coupling, and hold/freeze decisions.

Phase 3:
- Bind external API profiles to substrate-owned request packets for tools, crawlers, external services, and automation.
- Keep parsing, admission, and routing decisions inside the substrate.

Phase 4:
- Optionally attach the photon confinement research path as a bounded microprocessor profile.
- Use it as one kernel family inside the substrate OS, not as the product-level control authority.

## 7. Why This Matches GenesisSubstrate_eq.md

`GenesisSubstrate_eq.md` defines:
- one admissible evolution rule
- deterministic sink collapse on rejection
- pulse-driven axis scaling
- modality-to-axis binding
- fixed operator naming
- closed-system constraint enforcement

The existing runtime already implements those foundations.
The missing step is not a new theory layer.
The missing step is a clean integration contract that says:

- simulation calculus is substrate telemetry
- feedback control is substrate evolution
- GPU kernels are substrate microprocessors
- APIs are adapter-executed side effects scheduled by substrate state

That is the product architecture this repository is already moving toward.
