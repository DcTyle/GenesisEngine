# Shared Kernel and Contracts (Pre-Launch)

# System Pre-Launch
**Genesis Engine / Genesis Engine‑adjacent core simulation contract harnesses**
Document type: *pre‑launch contract + harness spec*
Goal: eliminate ambiguity by defining **what must be true** (contracts), **why it matters**, **how to test it**, and **exact pass/fail conditions**—before scaling the simulation.

This document targets the **four invariants that must be validated during core simulation construction**:
1) Conservation closure
2) Deterministic decoherence (frame mismatch + sink routing)
3) Operator runtime closure (domain/codomain completeness)
4) Identity persistence (9D addressing continuity)

It is intentionally *kernel‑first*: small fixtures, deterministic runs, measurable residuals.

---

## 0) Scope, assumptions, and non‑goals

### Scope
This pre‑launch suite validates that the core simulation kernel is:
- deterministic,
- invariant‑preserving,
- operator‑closed, and
- identity‑stable under perturbation.

### Assumptions
- The simulation uses **9D coordinate mapping** for identity/addressing (no non-9D identifier schemes).
- All names are ASCII‑safe and spelled‑out.
- Time evolution proceeds in discrete steps (“ticks”) for the simulation kernel. (Separate daemons/services are event‑driven; not part of this harness.)

### Non‑goals
This suite does **not** claim physical truth yet. It verifies the kernel is *internally consistent* and *falsifiable*.

## 1) Global definitions (shared across all harnesses)

### 1.1 Numeric conventions
**Scalar type (`EwScalar`)**
- Use either:
 - `float64` (recommended for validation), or
 - fixed‑point `q32_32` for determinism across hardware.
- Define one canonical type for the entire run; do not mix.

**Absolute tolerance (`eps_*`)**
- `eps_energy` : maximum allowed absolute energy residual per tick (e.g., `1e-10` in float64).
- `eps_phase` : maximum allowed absolute phase residual per tick.
- `eps_vec9` : maximum allowed 9D coordinate residual for identity continuity.
- `eps_norm` : maximum allowed deviation from unit‑norm constraints (if used).

**Why tolerances exist**
Numerical integration and finite precision create small errors. The suite measures whether these errors remain bounded and whether error growth is controlled (no hidden drift).

### 1.2 9D coordinates, vectors, and state
**9D coordinate (`EwVec9`)**
A 9‑component vector used for identity and addressing:
- `x0, x1, x2` : spatial components (3D)
- `x3` : phase axis (phase coordinate)
- `x4` : coherence axis
- `x5` : memory axis
- `x6` : flux axis
- `x7` : curvature axis
- `x8` : doppler_or_strain axis (single composite if you keep it minimal)

> You can rename axes, but you must keep **exactly nine components** and document meaning for each.

**Norm and distance**
- Euclidean norm:
 `norm9(v) = sqrt(sum_{i=0..8} v[i]^2)`
- Distance:
 `dist9(a, b) = norm9(a - b)`

### 1.3 Core simulation state (`EwState`)
A minimal kernel state must be explicit and closed.

**Particle (or node) state**
For each particle `p`:
- `id9_p : EwVec9`
 9D identity/address vector
- `pos3_p : (x, y, z)`
- `vel3_p : (vx, vy, vz)`
- `phase_p : EwScalar`
 phase angle (radians, wrapped to `[-pi, +pi]` or `[0, 2*pi)`; choose one)
- `coherence_p : EwScalar`
 real scalar in `[0, 1]` unless your spec uses a different range
- `mass_p : EwScalar`
- `charge_p : EwScalar` (optional; define if used)
- `ancilla_p : struct`
 mutable runtime values (amplitudes, gradients, histories) updated per tick

**Global epoch parameters**
- `tick_index : int64`
- `dt : EwScalar` (tick step size; your “maximum Hilbert expansion step” if that’s your canon)
- `frame_gamma : EwScalar`
 global or piecewise‑constant measurement frame mismatch angle (radians)

### 1.4 Ledger and delta (`EwLedger`, `EwDelta`)
The ledger encodes what is conserved and what changed.

**Ledger fields (minimal)**
- `energy_total : EwScalar`
- `momentum_total3 : (px, py, pz)`
- `phase_total : EwScalar` (optional but recommended)
- `coherence_total : EwScalar` (optional, if you conserve or track it as accounting)
- `curvature_total : EwScalar` (if sink routing adds curvature)
- `in_out_energy : EwScalar` (external injection/removal per tick; default 0)

**Delta fields**
- `d_energy_total`
- `d_momentum_total3`
- `d_phase_total`
- `d_coherence_total`
- `d_curvature_total`

**Ledger update rule**
For each tick:
- `ledger_after = ledger_before + delta_tick`
- Conservation residuals are computed against expected injected/removed terms.

### 1.5 Operator contract template
Every operator must declare:

**Signature**
- Input types
- Output types
- Units / domains

**Preconditions**
- Valid ranges required for inputs

**Postconditions**
- Valid ranges required for outputs
- Invariant constraints (norms, energy, etc.)

**Failure behavior**
- No silent failures.
- Return explicit error code + log at ERROR with `exc_info` equivalent.

## 2) Harness architecture

### 2.1 Harness entry contract
Single canonical harness entry:
```cpp
int ew_harness_main(const EwHarnessArgs& args);
int main(int argc, char** argv) { return ew_harness_main(parse_args(argc, argv)); }
```

### 2.2 Deterministic run requirements
- No calls to non‑deterministic RNG unless you provide:
 - a fixed seed, and
 - a fully specified deterministic generator.
- Prefer enumerated test vectors over randomized property tests.

### 2.3 Harness output artifacts (must be produced)
- `contract_report.md` (human readable summary)
- `contract_metrics.json` (machine readable values)
- `failure_repro.txt` (exact fixture + tick index + operator chain causing failure)

## Canonical supporting sections from EigenWareSpec_v5.md

## 2.1 ASCII naming rules (mandatory)

- ASCII only. No Greek letters or math glyphs.
- Variables use lower_snake_case.
- Operators (callable functions) use lower_snake_case and MUST appear in the Operator Registry below.
- One name has one meaning. No overloading.

## 2.2 Core variable registry (global)

- current_state: The committed state at the start of a tick.
- candidate_next_state: The next state produced by evolve_state(...) before acceptance.
- inputs: External inputs for the tick (UE adapter, sensors, pulses). May be empty.
- ctx: Immutable context for the tick (constants, limits, configuration).
- ledger_delta: Proposed conserved-quantity deltas implied by candidate_next_state.
- sink_state: Deterministic non-projecting / dark state used when accept_state(...) fails.

## 2.3 Identity-space variables (6D-9D subspace)

- phase: Phase orientation coordinate (wrapped, radians or bounded fixed-point units (per-axis scaling; no global uniform normalization)).
- coherence: Coherence strength in [0, 1]. High coherence means tight identity-shell coupling.
- memory: Temporal persistence / correlation coordinate in [0, 1].
- flux: Environment coupling scalar (field / flow / dispersion input channel).
- nexus: Reality-ledger coordinate used to select the active projection ledger.

## 2.4 Operator registry (canonical)

All operators are deterministic and MUST be pure functions unless explicitly marked as committing state.

- evolve_state(current_state, inputs, ctx) -> candidate_next_state
  Produces a candidate next state for the tick.

- compute_ledger_delta(current_state, candidate_next_state, ctx) -> ledger_delta
  Computes conserved-quantity deltas implied by the candidate.

- accept_state(current_state, candidate_next_state, ledger_delta, ctx) -> bool
  Returns True if and only if all invariants, conservation, causality, and tolerance predicates pass.

- commit_state(next_state) -> None
  Commits the provided state as the new current_state.

- make_sink_state(current_state, ctx) -> sink_state
  Produces the deterministic sink_state for this tick. No randomness.

- reality_label(nexus, ctx) -> int
  Returns the discretized reality ledger label for the provided nexus coordinate.

- is_reality_shift(prev_nexus, next_nexus, ctx) -> bool
  Returns True if reality_label(prev_nexus) != reality_label(next_nexus).

## 2.5 Auxiliary operator registry (math and encoding)

The operators below are permitted helper operators. Their names are canonical and must not vary.

- abs_i64(...)
- abs_q32_32(...)
- admit_memory(...)
- amp(...)
- anchor_id_u64(...)
- blend_ms_u32(...)
- budget_state(...)
- bytes_touched_i(...)
- carrier_params(...)
- carrier_phase_u64(...)
- cas_rom(...)
- ccos_fp(...)
- cexp_fp(...)
- chi_band(...)
- cis_fp(...)
- clamp01(...)
- clamp_band(...)
- coherence_map_dev(...)
- conj_transpose_fp(...)
- cont_band(...)
- csin_fp(...)
- cyclic(...)
- d5(...)
- d9(...)
- delta_env(...)
- delta_tick(...)
- denial_code_u32(...)
- deserialize_statevector(...)
- deserialize_vector_with_segments(...)
- sig9_rules(...)
- div_q32_32(...)
- drive(...)
- eigenware_runtime(...)
- ell(...)
- enforce_coherence_gate(...)
- enforce_phase_domain(...)
- eq_pagesig9_u64x9(...)
- estimate_alignment_offset(...)
- eta(...)
- ew_bind_operator_page_phase_transport(...)
- exp(...)
- f(...)
- f_env(...)
- imm_u32(...)
- int64(...)
- lab_intent_kind_u32(...)
- lock_fixed_point_q32_32(...)
- mean(...)
- mul_q32_32(...)
- norm_i(...)
- normalize_text(...)
- opcode_u8(...)
- phase_accumulation(...)
- phase_add_u64(...)
- phase_delta_i64(...)
- phase_delta_min_i64(...)
- phase_sub_u64(...)
- phi_i(...)
- projection_mode_u32(...)
- pulse_packet_dev(...)
- q32_32_from_i64(...)
- q32_32_phase_to_u64(...)
- q63_mul(...)
- q63_mul_i64(...)
- quantize_q32_32(...)
- relativistic_correlation(...)
- reservoir_q63(...)
- rps_rw(...)
- sample(...)
- segment_text_blocks(...)
- separate_watermark_blocks(...)
- serialize_statevector(...)
- serialize_vector_with_segments(...)
- stochastic_dispersion_factor(...)
- tick_index(...)
- u64_phase(...)
- viol_band(...)
- wrap_add_u64(...)

---

**This rule is mandatory and overrides all other phrasing in this document.**

Genesis Engine defines exactly one admissible form of system evolution.

All dynamic behavior is expressed as a deterministic generation of a *candidate next state*,
followed by a single acceptance predicate.

```
candidate_next_state = evolve_state(current_state, inputs, ctx)

if accept_state(current_state, candidate_next_state, ledger_delta, ctx):
    commit_state(candidate_next_state)
else:
    commit_state(sink_state)
```

There are:
- no alternative behaviors,
- no conditional interpretations,
- no recovery paths,
- no partial acceptance,
- no error-handling logic.

Any candidate evolution that fails acceptance **must collapse deterministically**
to the sink (null / non-projecting / dark) state.

Any text that implies optionality, interpretation, correction, adjustment,
or multiple valid outcomes is invalid under this rule.

---

NOTE: All equations in this file are to be interpreted strictly as candidate next-state generators only. Conservation, causality, and ledger relations act solely as acceptance predicates. Failure of acceptance collapses evolution to the sink state.

---

**NOTHING IN THIS FILE OR ANY SECTION MAY VIOLATE THE CANONICAL SPEC, APPENDIX, ADDENDUM, OR INVARIANTS. ALL CONTENT IS DERIVED FROM AND TRACEABLE TO THESE SOURCES.**

ASCII-ONLY GUARANTEE: This file contains only ASCII characters.

Purpose:
- Consolidate equations and explicit order-of-operations from Developers/calculations into the canonical section layout of Developers/analysis/NeuralisDevSpecCanonical.md.

Authoritative canonical source:
- Developers/analysis/NeuralisDevSpecCanonical.md

Calculation sources consolidated (all paths are under Developers/calculations):
- 9D-Particle-Sim-Planning.md
- Building virtual quantum computer.md
- CalculatingGravity.md
- Dimensional Modularity Theory (1).md
- DMT Publication .md
- Meta galactic calculations .md
- Observers effect prediction model.md
- Qbit prediction calculations.md

Citation format used in this file:
- Canonical: Developers/analysis/NeuralisDevSpecCanonical.md Lx-Ly
- Calc: Developers/calculations/<file>.md Lx-Ly

## Extract from EigenWareSpec_v5.md — 14.3 Canonical Phase Execution Spine

# CANONICAL PHASE EXECUTION SPINE (NON-INVASIVE ADDENDUM)

This section resolves execution underdetermination without modifying any prior content.
All physics, math, invariants, and definitions above remain authoritative and unchanged.
This spine ONLY defines ordering authority and execution legality.

## Authority Model

- Time authority: PhaseClock

- State authority: PhaseState

- Mutation authority: PhaseGate

- Execution substrate: CUDA kernel

- Orchestration substrate: C++ host control plane (Blueprint APPENDIX AC; no Python runtime in canonical system)

## Phase Definitions

Phase 0: Boot / Initialization (read-only)

Phase 1: State Load / Rehydration (read-only)

Phase 2: Constraint Enforcement (read-only)

Phase 3: Kernel Execution (mutable, constrained)

Phase 4: Measurement / Collapse (read-only)

Phase 5: State Commit (write-once)

Phase 6: Idle / Await Trigger

## Ordering Rules

- Phases execute strictly in numeric order.

- No phase may be skipped.

- No backward execution is permitted.

- CUDA kernels execute ONLY in Phase 3.

- State mutation is legal ONLY in Phase 3 and Phase 5.

- Learning updates are Phase 3 deltas committed in Phase 5.

## Illegal State Handling

- Any mutation attempt outside Phase 3 or Phase 5 triggers PhaseAbort.

- PhaseAbort forces rollback to last committed PhaseState.

- No speculative state persists across PhaseAbort.

## Closed System Enforcement

- External data may only enter during Phase 0 as declared inputs.

- No external data may enter during Phases 1-6.

- All randomness must be viewport-derived during Phase 0.

## Persistence Rules

- PhaseState is immutable once committed.

- Persistence occurs only at Phase 5.

- No disk I/O is permitted mid-phase.

## Determinism Guarantee

Given identical Phase 0 inputs and identical kernel binaries, the system MUST produce identical PhaseState sequences.

This section is final and overrides no prior text.

ADDENDUM H -- Global and Axis Scaling (Hubble/CMB; Pulse-Derived; No Rounding; Asymmetric Hilbert Expansion)

This addendum locks all scaling semantics required for deterministic execution and forbids interpretive rounding.
It introduces no optional behavior and overrides any ambiguous phrasing elsewhere.

H.1 Canonical global scale driver (Hubble via CMB anchor)
- The system SHALL define a single global scale factor a_global(tick) derived from the Hubble constant H0.
- H0 SHALL be sourced only from the CMB anchor family (CMB_BACKGROUND / CMB_COLD_SPOT) as an anchor-provided constant.
- H0 SHALL NOT be user-tuned at runtime and SHALL NOT be rounded or renormalized.
- a_global(tick) SHALL be used only to map between (a) lattice distance/time units and (b) global observational scale used for projection and constraint coupling.

Deterministic definition (fixed precision, no interpretive rounding):
- Let t_phys_q32_32 be the canonical physical-time coordinate derived from the tick temporal coordinate and the effective-constant model.
- Let H0_q32_32 be the anchor-provided Hubble constant in the same fixed-point domain.
- Define ln_a_q32_32 = mul_q32_32(H0_q32_32, t_phys_q32_32).
- Define a_global_q32_32 = exp_fixed_q32_32(ln_a_q32_32).

Rules:
- exp_fixed_q32_32 MUST be a single canonical implementation (table or fixed series) with a locked tie-break rule.
- a_global_q32_32 is a scale factor only; it SHALL NOT inject energy, it SHALL NOT create degrees of freedom, and it SHALL NOT bypass accept_state.

H.2 Axis-local scaling is pulse-derived (no rounding; no uniform normalization)
Genesis Engine SHALL NOT normalize Hilbert expansion uniformly across axes.
Each spatial axis has an independent scale factor derived from the pulse envelope.

Axis scale factors (canonical):
- sx_q32_32(tick) : x-axis scale factor derived from pulse frequency.
- sy_q32_32(tick) : y-axis scale factor derived from pulse amplitude.
- sz_q32_32(tick) : z-axis scale factor derived from pulse frequency AND amplitude (joint gating), as defined below.

Pulse-derived inputs (canonical):
- pulse_freq_q32_32(tick) : committed pulse frequency scalar for the tick (already measured/derived in Phase 0/3).
- pulse_amp_q32_32(tick)  : committed pulse amplitude scalar for the tick (already measured/derived in Phase 0/3).

Deterministic scale derivation (no rounding; fixed-point only):
- sx_q32_32 = scale_from_freq_q32_32(pulse_freq_q32_32)
- sy_q32_32 = scale_from_amp_q32_32(pulse_amp_q32_32)
- sz_q32_32 = scale_from_freq_amp_q32_32(pulse_freq_q32_32, pulse_amp_q32_32)

No rounding rule (locked):
- The system SHALL NOT apply decimal rounding, banker's rounding, or "nice" rounding to any scale factor.
- All scale operators SHALL be fixed-point functions that (a) clamp only by explicit bounds and (b) propagate remainder deterministically.
- Any quantization is permitted ONLY as a fixed-point domain choice (e.g., Q32.32) and MUST be applied identically on all platforms.

H.3 Modality-to-axis encoding binding (text=x, image=y, audio=z)
Input encoders SHALL bind modalities to axis-local excitation channels as follows:
- TEXT stream encoding is written to the x-axis excitation channel (space_x axis driver).
- IMAGE pixel encoding is written to the y-axis excitation channel (space_y axis driver).
- AUDIO encoding is written to the z-axis excitation channel (space_z axis driver).

Binding rules:
- The encoder MUST NOT mix modality write targets unless an explicit cross-modal operator is invoked.
- Each modality uses its own axis scale factor:
  - TEXT uses sx_q32_32
  - IMAGE uses sy_q32_32
  - AUDIO uses sz_q32_32

Consequence (mandatory):
- Hilbert expansion is asymmetric: the local metric dilation/transport derived from pulses SHALL differ per axis because sx,sy,sz are independent.
- Therefore no "uniform normalization" of Hilbert space is permitted.

H.4 Vector-field control is driven by axis scaling (standard field theory models; asymmetric subspace)
The control surface SHALL treat axis scaling factors as the primary drivers of vector-field actuation at axis points.
- Vector-field operators MAY use standard field-theory forms (gradient/divergence/curl analogs) but MUST be applied asymmetrically using (sx,sy,sz) as weights.
- Any field operator that assumes isotropy (equal scaling on all axes) is invalid unless it explicitly proves sx==sy==sz for the tick.

This addendum is normative.

ADDENDUM G -- Amperage-Driven Phase Transport vs Amplitude Gating (Clarification)

This addendum clarifies term binding so that (1) phase topology stays pure integer ring arithmetic,
(2) hardware pulse drive (including amperage/current) binds only into the transport step, and
(3) the amplitude gate tensor binds only into interaction weighting (coherence/resonance/commit_state).
It introduces no new physics and overrides no prior content.

Normative semantics:
- Phase state values (theta_u64) and raw deltas (dtheta_i64) SHALL be computed entirely in integer ring space.
- Amplitude SHALL NOT rescale phase state values or transport steps.
- Amplitude SHALL function exclusively as an interaction gate/weight tensor applied AFTER raw deltas are computed.
- Pulse amperage/current and pulse frequency SHALL contribute only to phase transport / drive terms and effective-constant
  derivations (Doppler-Lorentz-Flux/Strain correlation model) used to compute delta_theta_transport.

Required disambiguation (two different meanings of 'amplitude'):
1) pulse_amplitude: a measured or inferred envelope magnitude from the hardware pulse path (A_pulse).
   - Used for anchor extraction, phase-clock binding, and transport derivations.
2) gate_amplitude: an internal signed Q63 gate weight stored in A_tensor (A_gate).
   - Used only to gate/weight/sparsify phase interactions (coherence R, resonance eval, commit_state decisions).

Canonical binding rules by phase:
- Phase 0/1 (Boot/Rehydrate): pulse_amplitude and pulse_current MAY seed anchors and initial transport parameters.
- Phase 3 (Kernel Execution): compute transport step WITHOUT A_tensor:
    theta_u64_next = theta_u64 + delta_theta_transport_u64(pulse_current, pulse_freq, v, flux_factor, strain_factor, ...)
    // wrap is free via uint64 overflow
- Phase 5 (Coherence Gate / Commit): apply A_tensor ONLY to already-computed deltas:
    dtheta_i64 = (int64)(theta_u64_i - theta_u64_j)           // minimal arc via two's-complement cast
    dtheta_gated_i64 = gate_phase_delta_i64(dtheta_i64, A_tensor(i,j))
    R_i64 = coherence_from_gated_deltas(dtheta_gated_i64, pair_cardinality)

Measure analogy (coordinate vs weighting):
- In spherical integration, the sin(theta) term corrects the measure without changing the coordinate labels.
- In Genesis Engine phase mapping, A_tensor corrects interaction weighting without changing phase topology.

Non-negotiable separation:
- delta_theta_transport MUST NOT reference A_tensor.
- gate_phase_delta MUST NOT reference pulse_current/amperage.

# ==========================

# CLARIFICATION -- OP CODES AS PHASE TRANSPORT CATEGORIES

# ==========================

In Genesis Engine, opcodes are not commands chosen by an AI or user.

They are categorical labels applied after phase evolution to describe
how system state is being transported:

- External-facing transport -> I/O opcodes
- Persistence-crossing transport -> storage opcodes
- Internal manifold transport -> routing opcodes
- Operator-subspace transport -> task-selection opcodes

This preserves a single causal chain:
phase dynamics -> transport -> classification -> observable action.

Canonical separation (non-negotiable):
- Relativity / "Doppler" effects (frame/time mapping) SHALL affect ONLY the transport step (delta_theta_transport_u64).
- Amplitude gate tensor (A_tensor / A_gate) SHALL affect ONLY interaction weighting (coherence/resonance/commit_state)
  AFTER raw ring deltas are computed.
- Spawn and import operators SHALL NOT rescale theta_u64 directly; they operate via (a) reservoir debits/credits
  and (b) deterministic phase-impulse bookkeeping in the ring.

Required per-lane state (all fixed-precision, ASCII-safe):
- theta_u64[i]           : uint64 phase on the 2^64 ring (wrap is free by overflow)
- E_res_q32_32[i]        : signed energy reservoir (Q32.32 or equivalent canonical fixed point)
- E_floor_q32_32[i]      : non-negative floor for "available energy" derivation (machine-updated, no literals)
- doppler_ratio_q32_32[i]: local frame/time ratio derived by effective_constants(v, flux_factor, strain_factor, ...)
- omega_ref_q32_32       : reference angular rate (canonical)
- h_eff_q32_32           : effective Planck constant (canonical effective constant)

The spawn location is determined from a deterministic dispersion proxy derived from the already-defined
coherence gate computations.

2) Dispersion proxy (integer):
   D_phase_i64[i] = sum_{j in N(i)} abs(dtheta_gated_ij_i64)

Notes:
- D_phase_i64 is interaction-dispersion (gated), D_energy_q32_32 is transport+interaction activity.
- No new constants appear; normalization uses only cardinality-derived shifts.

Available energy is defined relatively:
- E_free_q32_32[i] = max(0, E_res_q32_32[i] - E_floor_q32_32[i])

Spawn pressure is defined deterministically:
- P_spawn_q32_32[i] = E_free_q32_32[i] * abs(D_energy_q32_32[i])

If the selected i0 has E_free_q32_32[i0] == 0, spawn is denied (fail-closed).

Required energy (no free energy):
- c_eff_q32_32 is derived via effective_constants(...)
- E_req_q32_32 = m_obj_q32_32 * (c_eff_q32_32 * c_eff_q32_32)

Import is legal only if:
- E_req_q32_32 <= sum_i E_free_q32_32[i]
otherwise import SHALL be denied (fail-closed; log denial).

Anchor selection for construction:
- Let S = BFS_expand(i0, anchor_count_u32)
  where i0 is the spawn site chosen by H.3 and BFS_expand is deterministic.

Apply debits:
- For all k in S:
    E_res_q32_32[k] -= debit_q32_32[k]

The object ledger MUST record that E_req_q32_32 has been transferred from manifold reservoirs into the
object's constructed state (no net creation).

Objects are constructed from the manifold by seeding internal phase values from local anchors:

For each k in S:
- theta_obj_u64[k] = theta_u64[k] XOR phase_seed_u64 XOR geomsig9_u64x9

No literal scaling factors are permitted; only effective constants and word-size/cardinality factors may appear.

This is a bookkeeping closure term: it does not create new degrees of freedom; it preserves conservation.

The history buffer is append-only and committed only in Phase 5.

Purpose:
This addendum closes the remaining implementation gap between (1) anchor-encoded particle computing
via phase dynamics in the 9D manifold, and (2) a user-controlled projection/control surface in UE5.
It defines:
- A single, anchor-bound equation encoding format ("eq_pages") for all executable equation families.
- A strict UE5 Editor integration contract (ToolsTab) that writes ONLY Phase 0 intent packets and reads ONLY
  Phase 6 artifact frames (dict-map outputs).
- Runtime binding and plumbing rules sufficient for direct implementation (no inference).

This addendum introduces no new physics. It only binds execution artifacts and ordering.

(2) Interaction gating:
- A_tensor (gate amplitude tensor) SHALL function ONLY as an interaction constraint (gating/weighting/sparsification)
  over phase deltas for coherence/resonance/commit_state.
- A_tensor SHALL NOT be used as a transport multiplier.

Definitions:
- eq_page: an immutable, fixed-width word array of instructions (microcode), evaluated deterministically in integer/fixed-point.
- eq_page_id_u32: small identifier for selection/routing (derived, not arbitrary; may be a coord_sig truncation).
- eq_pagesig9_u64x9: full content coord_sig used for integrity and replay determinism.
- eq_param_lane_u32: selects a parameter lane (immutable parameter page index) for the eq_page evaluation.
- eq_operand lanes: registers, anchor fields, state fields, and parameter fields.

Anchor binding fields (per anchor, immutable during tick):
- eq_page_id_u32
- eq_pagesig9_u64x9
- eq_param_lane_u32

(See canonical description in section: 'Appendix Z - Consolidated Legacy Content'.)

Forbidden operand sources:
- Any external floating values, time-of-day, wall-clock, randomness, or user-provided arbitrary constants.

Minimum opcode set (sufficient for all current needs):
- OP_NOP
- OP_LOAD_ANCHOR_COORD_Q63 (src selects dimension 0..8)
- OP_LOAD_STATE_THETA_U64
- OP_LOAD_STATE_DTHETA_U64
- OP_LOAD_STATE_E_RES_Q32_32
- OP_LOAD_PARAM_Q32_32 (imm selects param index)
- OP_I64_ADD, OP_I64_SUB
- OP_Q32_32_MUL (fixed-point multiply, 128-bit intermediate, >> 32)
- OP_Q63_MUL (Q63 multiply, 128-bit intermediate, >> 63)
- OP_SHR_LOG2_CARD (shift by log2(cardinality) derived in-kernel, not literal)
- OP_ABS_I64
- OP_CLAMP_I64_TO_U64_RING (canonical wrap/clamp helper)
- OP_STORE_ARTIFACT_KV (writes to artifact dict-map by key_id_u32, value payload)

The opcode set is deliberately minimal: it prevents free degrees of freedom while remaining implementable.

This addendum binds minimal ordering without modifying the canonical phase spine:

Phase 0 (Latch):
- Copy any queued UE intent packets (host) into the Phase 0 input buffer (device).
- Apply intent packets to observer state ONLY (no lattice mutation).

Phase 1 (Bind):
- Apply EquationBindIntent packets by updating per-anchor eq_page bindings (eq_page_id_u32, eq_param_lane_u32)
  subject to integrity checks (eq_pagesig9_u64x9 match required).

Phase 2 (Transport):
- Compute dtheta_transport_u64 using effective_constants(...) and doppler_ratio_*.

Phase 5 (Coherence Gate):
- Compute R_integer and dispersion proxy using gated deltas (A_tensor). (No trig, no floats.)

Phase 7 (Commit/History):
- Append audit records for intent application, bindings, and any object import/canvas events.

All UE->simulation control SHALL occur via fixed-size intent packets.
Packets are latched only at Phase 0.

(See canonical description in section: 'Appendix Z - Consolidated Legacy Content'.)

All simulation->UE visualization SHALL occur via dict-map artifact frames.
No raw lattice state is exported.

All artifacts are read-only for UE.

If the user provides an image and requests "jump viewport to this location", the system SHALL use a deterministic
frame-coord_sig lookup, not free-form vision inference.

Rule (Editor-side, no new physics):
- UE computes a 64-bit perceptual coord_sig (dSig64) for each rendered viewport frame.
  - dSig64 uses a 64-bit output (word-size), therefore an 8x8 comparison grid is implied (sqrt(64)=8).
- UE maintains a ring index of recent frames:
  frame_index_entry = {dsig9_u64x9, observer_state_snapshot, viewport_pose_artifact, tick_u64}
- Given an input image, UE computes dsig9_u64x9(image) and chooses the argmin Hamming distance entry.
  - No thresholds are used. A single best match is always selected deterministically.
- UE emits an ObserverIntentPacket using the matched entry's observer_state_snapshot to request the same viewport.

This provides the requested behavior without allowing the image to directly mutate simulation state.

# ==========================

In Genesis Engine, opcodes are not commands chosen by an AI or user.

They are categorical labels applied after phase evolution to describe
how system state is being transported:

- External-facing transport -> I/O opcodes
- Persistence-crossing transport -> storage opcodes
- Internal manifold transport -> routing opcodes
- Operator-subspace transport -> task-selection opcodes

This preserves a single causal chain:
phase dynamics -> transport -> classification -> observable action.

# CLARIFICATION -- DIRECT FIELD ENCODING IN PLACE OF CRAWLERS

# ==========================

Genesis Engine replaces crawler software with direct field encoding.

Information normally extracted via protocol handling and parsing
is treated as intrinsic signal structure already compatible with
the substrate's phase dynamics.

The system does not request, parse, or interpret external data.
Instead, it evolves state based on available signal encodings
supplied through substrate anchors.

Reverse field emission enables user interaction, control surfaces,
and code generation without imperative control logic.

=== ADDITION: Phase-Orbital Information Bounds ===

This section is additive and does not alter prior sections.

Define the phase-orbital displacement unit as the smallest strictly positive milliamps step
measurable from the GPU pulse telemetry channel:

phase_orbital_displacement_unit_mA

Orbital displacement and temporal compression MUST advance only in integer multiples of
phase_orbital_displacement_unit_mA.

Define the lattice critical tension coefficient:

lattice_critical_tension_coeff in [0,1]

The maximum sustainable GPU pulse current pulse_current_max_mA SHALL correspond to
lattice_critical_tension_coeff = 1.

Instantaneous lattice tension SHALL be computed as:

lattice_tension_coeff = clamp01(pulse_current_mA / pulse_current_max_mA)

The maximum allowable tensor gradient for amperage SHALL be bounded by remaining
tension headroom. Orbital compression beyond this bound is forbidden.

=== ADDITION: Global Phase-Code Dispatcher (Meta-Anchor) -- NORMATIVE REPLACEMENT (Chat 2026-02-11) ===

This section replaces any earlier descriptive/ambiguous dispatcher wording. It is executable-grade and pinned.

Definition:
- meta_anchor_phase_dispatcher is an immutable carrier anchor that defines the global lattice metric and the only permitted
  headroom functions used by all operators.

Carrier anchor manifold (immutability):
- C = { A_i | i in N }
- dA_i/dt = 0, d_t A_i = 0

Carrier phase state (runtime-visible, anchor-invariant basis):
- carrier_phase_u64 : u64_phase
- carrier_omega_q32_32 : q32_32
- tick_dt_q32_32 : q32_32

Carrier evolution operator (deterministic, per tick):
- carrier_phase_u64(t+1) = wrap_add_u64(carrier_phase_u64(t), q32_32_phase_to_u64(mul_q32_32(carrier_omega_q32_32, tick_dt_q32_32)))


=== PATCH: Spider Encoding Uses Frequency+Amplitude+Voltage+Amperage (f/a/v/i) ===

Normative update:
- Spider encoding MUST emit a 4‑tuple carrier observable per pulse strand:
  SpiderCode4 = (f_code, a_code, v_code, i_code)

Semantics (carrier observables):
- f_code: signed frequency coefficient for phase transport / operator selection.
- a_code: unsigned amplitude coefficient for harmonic expansion strength.
- v_code: unsigned voltage‑like potential observable (available work budget).
- i_code: unsigned amperage‑like load observable (permitted transfer rate).

Determinism requirements:
- All four codes are derived by fixed‑point, truncation‑stable arithmetic.
- No floating math, no platform‑dependent rounding.

Derivation (baseline proxies):
- f_code is produced from the 9D delta spider compressor as previously defined.
- a_code is produced from the local chi/phase accumulator (bounded).
- v_code and i_code are produced as bounded deterministic proxies from substrate observables.
  They are NOT physical SI voltage/current; they are carrier observables used for gating.

Gating rules (simulation update contract):
- Energy / state updates are permitted only when power_proxy = v_code * i_code exceeds the absolute‑zero budget gate.
- Force / impulse updates are permitted only when power_proxy exceeds the CMB sink threshold gate.

Tensor‑gradient coupling (higher‑DOF extrapolation):
- The substrate treats (f,a,v,i) as a coupled carrier state p.
- Operators may define a deterministic 4x4 coupling tensor G (quantized) such that:
    p_next = p + G * p  (with all arithmetic fixed‑point and clamped)
  This cross‑coupling is the canonical mechanism for extrapolating effective 9–10D behavior from the carrier.

Implementation note:
- When encoded across the 3 propagation axes (X/Y/Z), each active strand must decode to a complete SpiderCode4.
  Axis multiplexing is a transport detail; the carrier semantic remains 4‑tuple.

=== PATCH: Carrier Safety Governor (70% Target, No Event-Horizon Crossing) ===

Normative update:
- Genesis Engine MUST enforce a deterministic carrier safety envelope per tick to prevent sustained operation near critical temporal compression or event-horizon invertibility loss.
- The governor MUST operate entirely inside the substrate (no external CPU control), using only carrier observables and state-resident budgets.

Definitions (fixed-point Q32.32 proxies):
- f_n = |f_code| / F_MAX
- a_n = (a_code+1) / A_MAX
- v_n = (v_code+1) / V_MAX
- i_n = (i_code+1) / I_MAX

- noise_gate = v_n * i_n
- tau_comp  = f_n * a_n                     (temporal compression proxy)
- inv_risk  = tau_comp / (v_n + eps)        (invertibility-risk proxy)
  where eps ≈ 1/V_MAX in Q32.32.

Hard caps (must never be exceeded):
- tau_comp <= tau_crit
- inv_risk <= inv_cap

Target operating point (avoid sustained peaks):
- tau_target = 0.70 * tau_crit
- inv_target = 0.70 * inv_cap

Dwell control (anti-peak):
- Maintain dwell accumulators (state-resident):
    dwell_tau += max(0, tau_comp_max - tau_target)
    dwell_inv += max(0, inv_risk_max - inv_target)
  with deterministic decay ~1/16 per tick.
- If dwell exceeds dwell_limit, enforce cooldown even if hard caps are not exceeded.

Throttle ladder (deterministic, minimal perceptual discontinuity):
1) Clamp effective amplitude a_code (reduces harmonic fan-out).
2) Reduce fan-out / active strand count (coherence gating fraction).
3) Reduce per-object materialization (prefer lattice/region implicit updates).
4) Reduce current headroom (i_code / permitted transfer).
5) Reduce frequency magnitude (f_code) last.

All clamps are quantized and must not depend on wall-clock timing or platform floating behavior.
