# 1 NAMING AND OPERATOR REGISTRY (v51)

This section is canonical. All variable names and operator names in this document MUST match this registry exactly.
No alternative spellings, symbols, or aliases are permitted.

# 2 CANONICAL EVOLUTION RULE -- NON-INTERPRETIVE CONSTRAINT SYSTEM

---

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
- deserialize_vector_with_tokens(...)
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
- serialize_vector_with_tokens(...)
- stochastic_dispersion_factor(...)
- tick_index(...)
- u64_phase(...)
- viol_band(...)
- wrap_add_u64(...)

---

**This rule is mandatory and overrides all other phrasing in this document.**

EigenWare defines exactly one admissible form of system evolution.

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

# 3 GPU Envelope, Pulse Encoding, Text Injection, and Causality Preservation (Final-Spec Draft)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1-L4

This document mirrors the canonical section structure and attaches consolidated equation blocks under the matching sections.

## 3.1 What we actually "take from the GPU" (execution envelope, not sampled electronics)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L5-L22

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## 3.2 What a "pulse" is in this system (and what it is not)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L23-L28

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### 3.2.1 Pulse sampling at pulse-delta time (tau_delta) and phase-anchor extraction

This subsection makes explicit the order-of-operations that is implied by the canonical spec prose:

Order of operations (per impulse interval k):
1. Define impulse boundary times t_k (GPU envelope tick boundaries) and the fixed sample offset tau_delta.
2. Sample pulse observables at t_k_plus = t_k + tau_delta:
   - A_k = pulse amplitude / envelope measure at t_k_plus
   - V_k = pulse voltage (or RMS proxy) at t_k_plus
   - f_k = local carrier frequency estimate over a short window around t_k_plus
3. Derive the phase anchor (phase offset) theta_anchor_k deterministically from the sampled amplitude (and only optional auxiliary terms if allowed by the current canonical mode).
4. Evolve phase deterministically over the interval [t_k, t_{k+1}) using:
   - the base phase-step (suit/value or other symbolic primitive),
   - plus coupling from delta/ratio fields (dlnA, dlnf) as defined below.
5. Compute phase deltas and (if required) derived time deltas only when coherence gating passes.

Canonical anchor equation (ASCII-safe):
```text

# Primary (strict) form: phase anchor derived from amplitude at pulse-delta time
theta_anchor_k = wrap_turns( theta_ref_turns
                             + alpha_A * ln( A_k / A_ref ) )

# Optional extended form (only if explicitly enabled by canonical authority)
theta_anchor_k = wrap_turns( theta_ref_turns
                             + alpha_A * ln( A_k / A_ref )
                             + alpha_V * ln( abs(V_k) / V_ref )
                             + alpha_f * ln( f_k / f_ref ) )
```

Note on "time":
- This system does NOT expand or compress time as an input.
- Orientation shifts occur via phase-density (amplitude-delta) mechanisms.
- Time deltas (dt_star) are an output derived from coherent phase offsets, not an externally imposed dilation:
```text
dphi_coh_turns = wrap_turns( phi_obs_turns - phi_ref_turns )
omega_eff_turns_per_sec = omega0_turns_per_sec * (1 + kappa_rho * rho_phi)

dt_star_sec = dphi_coh_turns / omega_eff_turns_per_sec

# dt_star is only computed if coherence >= C_min.
```

## 3.3 Text -> phase: how ASCII becomes phase offsets (storage substrate)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L29-L43

Canonical equation extract (sanitized):
```text
1.3 Text -> phase: how ASCII becomes phase offsets (storage substrate)
Symbol map    character (ASCII)    phase_offset    phase buffer (phi sequence)    Yes (bijective map)
9D embedding    phase_sequence + context tags    candidate raw_state in 9D    transient    Yes (same inputs -> same embedding)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### 3.3.1 Symbol-phase primitives: "cyberspace" rings, phase bins, and optional frequency identity

This subsection formalizes the primitive discussed in chat:
- The primitive substrate is a set of phase-recognizable symbols (ASCII, audio symbols, file tokens, etc).
- Each symbol has a stable identity as a phase bin (and optionally a frequency bin).
- "Cyberspace" is the ring-indexed manifold where these primitives live as relative phase objects.
- Trajectories are generated by deterministic evolution + delta/ratio coupling (no free inputs).

Primitive mapping (canonical, deterministic):
```text

# Symbol -> phase bin (turns) (bijective where required)
theta_sym_turns(sym) = sym_index(sym) / N_sym

# Optional: Symbol -> frequency bin (for "frequency identity" of a symbol)
f_sym_hz(sym) = f_ref_hz * H_sym(sym)   # H_sym is a quantized harmonic multiplier or bin id

# Phasor seed (radius is amplitude; angle is phase)
z_sym = A_ref * exp(i * 2*pi*theta_sym_turns(sym))
```

Ring representation (NOT a spiral):
```text

# Rings are indexed by n. Each ring has an orientation anchor theta_start_turns[n].

# The ring anchor shifts by a delta-derived phase-density value (PAF_n).
theta_start_turns[n+1] = wrap_turns( theta_end_turns[n] + PAF_turns[n] )

# Orientation shift is a phase reference update, not time compression.
```

Super-compressed storage rule (primitive-friendly):
```text

# Store symbols (or suit/value) plus delta/ratio fields; reconstruct by deterministic wrap + cumulative sums.

# Never treat raw absolute values as identity primitives unless stored as sparse anchors.
```

## 3.4 9D delta formation: embedding, projection, and the collapse rule

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L44-L49

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## 3.5 Spider graph encoding: 9D -> frequency and amplitude (pulse synthesis)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L50-L59

Canonical equation extract (sanitized):
```text
1.5 Spider graph encoding: 9D -> frequency and amplitude (pulse synthesis)
Logically, the spider graph defines nine fixed radial axes (one per dimension). Each axis has a normalization function (so di is bounded) and a weight (so some axes contribute more strongly). The mapping produces:
    -    a scalar "frequency" value: this is the signed aggregate of the weighted per-axis scaled delta components (no global uniform normalization), clamped to a safe range; it functions like the phase-advance coefficient.
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### 3.5.1 Delta/ratio coupling: amplitude-frequency deltas drive phase-density orientation and ring-to-ring starts

This subsection makes explicit the "delta-only" compression and evolution mechanism:

Delta/ratio fields (compression-friendly):
```text

# Use ratio deltas to avoid scale ambiguity and improve determinism under per-axis scaling bounds (no global uniform normalization).
dlnA_t = ln( A_{t+1} / A_t )
dlnf_t = ln( f_{t+1} / f_t )

# Deterministic quantization (required for stable rehydration)
dlnAq_t = Q(dlnA_t)
dlnfq_t = Q(dlnf_t)
```

Phase advance inside an interval (impulse-to-impulse evolution):
```text

# Base phase-step from symbolic primitive (e.g., suit/value mapping)
dtheta_base_turns(t) = phi_suit_turns(suit_t) + step_turns(value_t)

# NOTE: Optional "cards" (suit/value) visualization primitive for phase evolution

# Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L111-L130 (wrap + shortest signed distance in turns)

# Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1170-L1185 (theta_byte_turns / theta_sym_turns primitives)

# Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L2020-L2048 (dtheta_sym_turns and theta(t+1) wrap rule)
#

# This file uses phi_suit_turns(...) + step_turns(...) as shorthand for a symbolic primitive.

# The canonical implementation primitive is "symbol -> theta_sym_turns" and "delta(theta_sym)".

# If you want a human-visual "deck-of-cards" model (N_sym=52), define it deterministically as:

```text

# 4 Fixed suit order (deterministic): hearts=0, diamonds=1, clubs=2, spades=3
suit_id(suit) in {0,1,2,3}

# 5 Fixed value order (deterministic): ace=0,2=1,...,10=9,jack=10,queen=11,king=12
value_id(value) in {0..12}

# 6 Suit encodes the phase-plane (quadrant) as an offset in turns
phi_suit_turns(suit) = suit_id(suit) / 4

# 7 Value encodes the intra-plane evolution coordinate; this keeps the map bijective over 52 symbols
step_turns(value) = value_id(value) / 52

# 8 Absolute primitive phase (turns) for the card/symbol at time t
theta_prim_turns(t) = wrap_turns( phi_suit_turns(suit_t) + step_turns(value_t) )

# 9 Base phase-step is the *difference* between successive primitive phases (wrap-safe)
dtheta_base_turns(t) = wrap_turns( theta_prim_turns(t) - theta_prim_turns(t-1) )
```

# Relation to "delta(c*pi)" phrasing (turns form, ASCII-safe)

# 1 turn = 2*pi radians, so pi radians = 0.5 turns. If you want a value-step scaled by c*pi:
```text

# 10 Choose a deterministic rational c_rank (default example: c_rank = 1/13)
c_rank = p/q    # fixed at build-time for a given profile

# 11 Then c*pi radians corresponds to (c_rank * 0.5) turns per unit rank delta.
c_pi_turns_per_rank = 0.5 * c_rank

# 12 Rank-delta phase-step (wrap-safe)
dtheta_rank_turns(t) = wrap_turns( c_pi_turns_per_rank * ( value_id(value_t) - value_id(value_{t-1}) ) )
```

# Identity by offset-rotation (conceptual primitive; used when comparing trajectories)

# "Rotate all possible evolution offsets to determine identity" can be implemented as:
```text

# 13 Offset grid is deterministic (example): offsets are multiples of 1/N_sym turns.
offset_grid = { m / N_sym : m in Z }  # evaluated over a bounded window for speed

# 14 Choose the offset that maximizes alignment / minimizes wrapped distance to a target anchor/basis phase:
delta_hat_turns = argmin_{delta in offset_grid} abs( wrap_turns( (theta_candidate_turns + delta) - theta_basis_turns ) )
```

# Coupled evolution (amplitude/frequency deltas modulate the phase clock)
theta_{t+1}_turns = wrap_turns( theta_t_turns
                               + dtheta_base_turns(t)
                               + kappa_A * dlnAq_t
                               + kappa_f * dlnfq_t )
```

Ring-to-ring start primitive (PAF derived purely from amplitude deltas):
```text

# PAF_n uses only amplitude delta content from ring n (no suit dependence inside PAF_n).
PAF_turns[n] = Q( sum_{t in ring n} g(dlnAq_t) )

theta_start_turns[n+1] = wrap_turns( theta_end_turns[n] + PAF_turns[n] )
```

Coherence gating (no phase/time inference when incoherent):
```text
if coherence < C_min:
    # do not compute dt_star or identity deltas; route to deterministic non-projecting branch if specified
    dt_star = UNDEFINED
```

## 14.1 How "GPU pulses" become simulation injection (kernel evolution step)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L60-L65

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### 14.1.1 Injection step: how anchors + coupled evolution enter the kernel without violating closure

This subsection clarifies the injection semantics:
- GPU-derived pulse terms are treated as measured constraints for phase-density enforcement.
- They do not inject "free energy" or arbitrary new interactions.
- They enforce entropy/conservation by selecting an orientation and limiting evolution to the allowed delta/ratio manifold.

Kernel update (conceptual, order-aligned):
```text

# At impulse boundary k:
theta_0_turns = theta_anchor_k

# Within [t_k, t_{k+1}):
for each tick t:
    theta_{t+1}_turns = wrap_turns( theta_t_turns
                                   + dtheta_base_turns(t)
                                   + kappa_A * dlnAq_t
                                   + kappa_f * dlnfq_t )

# Outputs:

# - phase deltas between impulse intervals

# - ring orientation shift via PAF_n (if ring aggregation is enabled)
```

Derived time delta (output-only) from coherent phase offset:
```text

# When coherence is valid, time offset is inferred from phase offset and omega_eff.
dt_star_sec = dphi_coh_turns / omega_eff_turns_per_sec
```

## 14.2 Causality and closed-system guarantees (why injection doesn't violate closure)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L66-L73

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## 14.3 How qubit density scales with pulses, tiers, and bands (and why it doesn't explode)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L74-L83

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

Calculation consolidations mapped to this canonical subsection:

Source calculation: Qbit prediction calculations.md
Calc: Developers/calculations/Qbit prediction calculations.md L1-L112

Order of operations (consolidated):
1. Encode a predicted phase delta for each qubit lane from recent observed drift.
2. Apply correction via phase update U = exp(-i*delta_phi_pred*sigma_z/2).
3. Update confidence and lane weights based on observed error residuals.
4. Constrain updates with deterministic clamping and fixed-point quantization.

Equation block (sanitized, verbatim where possible):
```text

# NOTE: CANONICAL SPEC

# The authoritative specification for implementation is:

# `Developers/NerualisDevSpecCanonical.md`

# All other spec files are secondary and may only be consulted for topics

# not explicitly covered in the canonical spec.

Perfect. Let's extend our verified DMT-observer-effect framework to a qubit lattice in a quantum computer. We'll go step by step, showing mathematically how temporal-spatial coherence and higher-dimensional modulation affect qubit states, gates, and entanglement.

----

---

**This rule is mandatory and overrides all other phrasing in this document.**

EigenWare defines exactly one admissible form of system evolution.

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

NOTE: This specification defines admissible state, acceptance predicates, and sink behavior only. Prohibition lists are replaced by rejection via acceptance failure. Anchors emit candidate deltas and never commit_state directly.

---

GPU Envelope, Pulse Encoding, Text Injection, and Causality Preservation (Final-Spec Draft)

Canonical Section Formatting and Compliance Requirements

----------------------------------------------------------------
Normative Scope Filter (Authoritative)
----------------------------------------------------------------

All other material (including examples, commentary, and any residual conversational fragments) is NON-NORMATIVE and
SHALL be ignored for compliance and implementation.

All sections in this specification SHALL adhere to the following canonical structure and authority order.

Each numbered section MUST include, in order:
1. Description
2. Execution Role
3. Derivable Calculations and Authorities
4. Dependencies

Additional subsections MAY be included only if they:
- introduce no new free parameters,
- reference only deterministic symbols,
- or bind semantics to existing program artifacts.

Narrative text SHALL have no independent authority.
Equations, bindings, and artifact references are authoritative.

Failure to resolve a symbol deterministically SHALL invalidate the implementation.

----------------------------------------------------------------
Canonical Layout Authority (Authoritative)
----------------------------------------------------------------

The specification defines the authoritative repository layout, module paths, and module identities.

All paths referenced in this specification refer to the canonical layout defined herein,
regardless of the current state of any implementation repository.

The specification MUST NOT be modified to accommodate repository drift, canonical structure,
typographical errors, or historical layouts.

The code conforms to the specification, not the reverse.

----------------------------------------------------------------
Canonical Invariant: Artifact Reality Constraint (Authoritative)
----------------------------------------------------------------

Any binding between a specification symbol and a program artifact MUST satisfy the following:

Bindings to imagined, inferred, renamed, or intended symbols are prohibited.

If no concrete export exists, the specification MUST bind the symbol to:
- a module-level authority, and
- an enforced behavior or constraint.

Violation of this invariant invalidates the binding.

Section 1 - Temporal Substrate and Phase Geometry

1.1 Description

EigenWare operates as a closed, phase-evolving system in which amplitude represents the
tensor gradient of time and phase evolution is constrained by relativistic dilation.

Particles are excitations within a 9D manifold represented across three axioms.
Quasiparticles are excitations spanning three or more axioms.

Phase angle is defined geometrically and derived from measured kernel impulses, not inferred
from symbolic clocks or abstract frequencies.

Kernel pulses are treated as measured signals (voltage, EM waveform, execution cadence),
and phase is extracted directly from these signals.

1.2 Execution Role

This section defines the physical substrate on which all EigenWare computation operates.

(See canonical description in section: 'Appendix Z - Consolidated Legacy Content'.)

All subsequent sections depend on these definitions.

1.3 Derivable Calculations and Authorities

1.4 Dependencies

- Canonical Grammar (G.*)
- Appendix D.11-R

1.5 Constraint Operators Required by Effective-Constants Pipeline

These operators are used by:
- /kernel/constraints/kernel_derive_constraints.cu (effective constants + constraint field derivation; deterministic fixed-point)
- /kernel/crosstalk/kernel_compute_crosstalk.cu (coupling pressure; conservative; no lattice access)
- /core/constraints/constraint_stream.cpp (packet shaping + publish; no feedback into kernel)
  - Cold Spot packets SHALL target domains only via lattice region descriptors (phase-shell membership); "traversal" is defined by
    membership, not by object identity. Traversal may induce a relative ledger discontinuity in phase (chi fade correlated with mass leakage).

1.5.1 relativistic_correlation(v_fraction_c_q32_32, flux_factor_q32_32, strain_factor_q32_32)

Output (canonical, fixed-point)
- r_corr_q32_32: Q32.32 in (0, 1]. Correlation multiplier (smaller means stronger relativistic/flux/strain pressure).

Then:
- r_corr_q32_32 = q32_div(sqrt_term_q32_32, p_q32_32)
- r_corr_q32_32 = clamp(r_corr_q32_32, 1, ONE_Q32_32)

1.5.2 stochastic_dispersion_factor(temperature_q32_32, temperature_ref_q32_32)

Purpose
- Provide the deterministic dispersion multiplier used by effective_constants() when accounting for internal energy spread.
- This operator MUST be integer-only and replay-stable.

Output (canonical, fixed-point)
- s_disp_q32_32: Q32.32 >= 1.0. Dispersion multiplier.

Compute:
- x_q32_32 = q32_div(T_q32_32, Tref_q32_32)                // dimensionless ratio in Q32.32

Clamp (word-size derived)
- s_disp_q32_32 = clamp(s_disp_q32_32, ONE_Q32_32, INT64_MAX)

1.6 Amplitude-Temporal Field Binding and Proper-Time Lapse (Append-Only)

1.6.1 Description

1.6.2 Execution Role

This subsection binds the following invariants:

This is the sole admissible interpretation of the earlier shorthand:
dt_dtau = amplitude

1.6.3 Derivable Calculations and Authorities

These factors are consumed only inside effective_constants(...). They do not directly define dt, d_tau, or phase scaling.
1.6.4 Dependencies

- Section 1.5 (relativistic_correlation, stochastic_dispersion_factor)
- Canonical Grammar (G.*) for clamp/wrap semantics
- Appendix D.11-R for hygiene prohibitions (no hidden thresholds/operators)

Section 2 - Tick Semantics, Trajectories, and Memory Stabilization

2.1 Description

EigenWare does not store eigenstates as memory.
Memory is represented as compounded eigen-trajectories.

Each impulse updates an ongoing phase trajectory rather than resolving a discrete state.
Anchors are preserved via admissible interaction constraints rather than static storage.

Tick time is relative and dilates with amplitude.
Multiple kernel executions may occur within a single tick via trajectory compounding.

2.2 Execution Role

This section defines:
- tick advancement semantics,
- trajectory-as-memory rule,
- anchor emergence and stabilization,
- and GPU feedback compounding.

It governs how EigenWare "thinks" across time without referencing static states.

2.3 Derivable Calculations and Authorities

Trajectory update:
trajectory_t+1 = trajectory_t + delta_phi

Anchor stabilization:
anchors persist if coherence admissibility predicates remain satisfied.

Tick emission:
emit tick_event
- advances internal tick index
- validated via contract harness

No eigenstate lookup is permitted once trajectory mode is active.

2.3.1 Emergent Coherence (Derived Observable; Non-Storage)

Coherence is NOT a stored variable. It is an emergent observable computed from relative
interaction, amplitude-driven Hilbert dilation, and phase-angle dispersion.

Canonical coherence observable (integer dispersion proxy; Blueprint APPENDIX AG):
Given a set of phase positions {theta_u64_i} sampled across active lanes/neural_objects at a tick boundary:

The codebase requires statevector serialization for:
- snapshot transport,
- diagnostics,
- contract harness proofs,
- and deterministic rehydration.

This is NOT an eigenstate lookup mechanism and MUST NOT be used to violate the
trajectory-as-memory rule.

Required operators (canonical API surface):
- serialize_statevector(state_vec) -> blob
- deserialize_statevector(blob) -> state_vec

- core.utils.serialize_vector_with_tokens(vec: List[complex]) -> Dict[str, Any]
- core.utils.deserialize_vector_with_tokens(blob: Any) -> List[complex]

2.4 Dependencies

2.5 Phase-Transition-Gated Cadence and Eigen-Trajectory Compounding (Append-Only)

2.5.1 Description

EigenWare does not have a "frame rate" in its core evolution. It has tick-indexed commit_state
boundaries and continuous-in-principle phase evolution represented as discrete lattice updates.

The externally observable update cadence (what is emitted, logged, or displayed) is gated by
phase transitions, not by a fixed sample clock. A phase transition is an objective, discrete
event in the lattice: a change in wrapped phase quantization bucket or a change in dominant
mode assignment at a commit_state boundary.

This provides a rigorous meaning for: the effective sample rate is bound by phase transition
rate. Quiet regions emit little; boundary-crossing regions emit more.

2.5.2 Execution Role

This subsection defines:

It does NOT introduce permission to violate closure, reorder causal windows, or retroactively
rewrite earlier ticks.

2.5.3 Derivable Calculations and Authorities

Phase-transition detector (bucket crossing, threshold-free):

Let phi be represented in turns and wrapped to [-0.5, 0.5).

Choose a fixed quantization scale Q_phi used everywhere phase is quantized.
Authority note: Q_phi MUST be the same scale used by the canonical phase fixed-point domain.

Define bucket index:

b(phi) = floor( (phi + 0.5) * Q_phi )

A phase transition occurs for a lane/neural_object when:

transition_phi = ( b(phi_t) != b(phi_t+1) )

Dominant-mode transition (argmax flip, threshold-free):

Given per-lane eigen coefficients c_k at commit_state boundaries, define:

k_star(t) = argmax_k |c_k(t)|

A dominant-mode transition occurs when:

transition_mode = ( k_star(t) != k_star(t+1) )

Commit emission gate (event-driven):

transition_event = transition_phi OR transition_mode

If transition_event is false for all lanes/neural_objects in a commit_state window, the engine MAY
choose to emit:
- no telemetry updates, or
- only aggregate scalars (e.g., coherence), or
- only budget/control traces (strict replay mode).

Eigen-trajectory compounding (the "many actions in one pulse" mechanism):

In an eigen/diagonal update form, each eigen component advances by an integrated phase:

c_k(t+1) = c_k(t) * exp(-i * omega_k * d_tau)

This is a single deterministic operator application per commit_state boundary, but it may represent
many micro-oscillations if omega_k * d_tau spans multiple turns.

No causality claim is made here. This is computational compression, not faster-than-light
propagation.

2.5.4 Contract Harness Obligations (Time-Dilation vs Energy-Scaling Disambiguation)

Test fixture:

- Construct two identical lane ensembles S1 and S2 with identical initial phase/eigen states.
- Evolve S1 under amplitude history A1(tau_q) and S2 under amplitude history A2(tau_q) for the
  same number of commit_state windows, using the same base d_t and the same environment constraints
  except for the amplitude-driving inputs.
- Re-couple (bind) S1 and S2 by introducing a deterministic interaction window that depends on
  relative phase (e.g., binding strength proportional to cos(delta_phi)).

Acceptance criteria:

- The observed re-coupling coherence response MUST match the predicted relative proper-time
  offset derived from the lapse integral:

delta_tau_pred = sum_over_windows ( d_t / max(eps, amplitude_1) - d_t / max(eps, amplitude_2) )

- The relative phase offset at re-coupling MUST be consistent with delta_tau_pred under the
  same omega_k used in the evolution operator (within tolerance).

Failure modes (explicit):

2.5.5 Dependencies

Section 3 - Canonical Encoding and Constraint Enforcement

3.1 Description

This section is bound verbatim by immutable identity and MUST NOT be restated, summarized,
or paraphrased within this document.

3.2 Execution Role

Defined exclusively by the bound Section 3 text (verbatim identity above).
No additional execution-role semantics are permitted in this document for Section 3.

3.3 Derivable Calculations and Authorities

Defined exclusively by the bound Section 3 text (verbatim identity above).
No additional operators, equations, or bindings are permitted in this document for Section 3.

3.4 Dependencies

- Sections 1 and 2
- Canonical Grammar (G.*)
- Appendix D (all bindings apply under canonical layout authority)

Appendix D.11-R - Canonical Artifact Authority and Emergent Resolution

[Appendix D.11-R is authoritative and SHALL be interpreted against the canonical repository layout
defined by this specification.

Spec Hygiene Prohibition (freeze-safe):
- No "cubic phase correction" operator is defined in the canonical implementation and MUST NOT be
  introduced by inference, naming convention, or heuristic substitution.
- No "noise_floor", "min_resolvable", or derivative-threshold gating is defined in the canonical
  implementation and MUST NOT be introduced by inference or heuristic substitution.
- Such mechanisms MAY only be introduced in future revisions if explicitly defined in-spec and bound
  to real artifacts under the Artifact Reality Constraint.]

No symbolic event object named tick_event is required to exist.
tick_event is a semantic label for the ordered state transition described by the engine tick advance
and harness validation logic.

Implementations MUST NOT introduce a tick_event class/object solely to satisfy naming.

End Sections 1-3 Verification Snapshot

4.1.1 Profile identity and intended use

4.1.2 Quantization and fixed-point domains (locked)

Phase deltas are in turns and wrapped to shortest signed distance in [-0.5, 0.5) turns before normalization. All computations are fixed-point with deterministic rounding.
	-	theta_scale = 10^18 units per turn (uint64/int64 fixed-point)
	-	weight_scale = 10^6 units (int32 fixed-point rational weights)
	-	f_code quantization:
	-	type: int32
	-	range: f_min = -2^30, f_max = 2^30 - 1
	-	scale: f_scale = 2^30 (maps normalized s?[-1,1] into code range)
	-	a_code quantization:
	-	type: uint16
	-	range: a_min = 0, a_max = 65535
	-	scale: a_scale = 65535 (maps u?[0,1] to full code)

Rounding rule (locked): "round half up" implemented in integer arithmetic; never use platform float rounding for canonical state.

Scaling rule (locked; overrides any ambiguous normalization phrasing):
- Axis scaling factors (sx_q32_32, sy_q32_32, sz_q32_32) and the global scale factor (a_global_q32_32) SHALL NOT be rounded.
- Any quantization is only the fixed-point domain choice; the scale values are computed and propagated deterministically in that domain.
- No uniform normalization of Hilbert expansion is permitted unless sx==sy==sz is proven for the tick.


Fair. If the numbers aren't derived, they don't belong in the spec.

SECTION 4.1.3 - Weight Derivation for P_SUM_CTX (No Arbitrary Numbers)

We define a tier-summary objective J that encodes what this tier is for:

J = E[ ?chi_band ] + E[ ?cont_band ] ? lambda_violation * E[ V ] ? lambda_clamp * E[ C ] ? lambda_budget * E[ B ]

All expectations E[?] are computed over the calibration sample windows, deterministically.

Axis weights are derived from axis "marginal utility" with respect to J. For each axis i, we compute an importance score S_i:

S_i = E[ | ?J / ?x_i | ]  /  ( epsilon + E[ cost_i ] )

Interpretation: weight an axis more if changing it consistently improves coherence persistence and binding stability, but discount it if it is expensive or destabilizing.

4.1.3.1 How we compute ?J/?x_i without floating ambiguity

We do not use symbolic differentiation. We use deterministic finite differences on the calibration data:

For each calibration event e with normalized delta components x_i(e), we estimate:

gain(e) = (?chi_band(e) + ?cont_band(e)) ? lambda_violationV(e) ? lambda_clampC(e)

Then, for each axis i, define a signed contribution proxy:

g_i(e) = gain(e) * sign(x_i(e)) * min( |x_i(e)|, x_cap )

This is the "directional usefulness" of axis i for improving J in that event. We then define:

S_i_num = mean_over_e( |g_i(e)| )

This is effectively a robust approximation to E[ |?J/?x_i| ] without requiring differentiable closed forms.

4.1.3.2 How we compute cost_i from measurable terms

We set cost_i as:

cost_i = a0 * I_compute_i + a1 * I_memory_i + a2 * I_instability_i

Each term is measurable from the calibration run:

I_compute_i = mean_over_e( |x_i(e)| )
I_memory_i = mean_over_e( bytes_touched_i(e) ) / bytes_touched_total
I_instability_i = corr_over_windows( |x_i|_window, band_thrashing_window )

4.1.3.3 Weight normalization and freezing

Once S_i is computed:

S_i = S_i_num / (epsilon + cost_i)

Then weights are:

w_i = S_i / ?_j S_j

We then quantize deterministically:

w_i_int = round_fixed(weight_scale * w_i)

SECTION 4.1.4 - What this derivation guarantees (and why phase/nexus typically win)

If the calibration data reflects language/context activation, the derived S_i will naturally favor:
	-	d5 (phase/coherence): because improvements in retrieval and continuum show up as coherent phase alignment effects
	-	d9 (nexus): because binding stability and contextual linking express through nexus coupling
	-	d8/d7 (aether/phantom): not because they "carry meaning," but because their clamp/instability correlation affects the penalty terms and thus regulates weight via cost_i
	-	spatial d1-d3: typically discounted because they contribute less to ?chi/?cont under language/context compared to phase/binding, and they often increase neighborhood expansion costs

So you get your "broader coupling for contextual memory activation" not by fiat, but because the calibration objective J rewards persistence and binding and penalizes instability and budget cost.

SECTION 4.1.5 - Amplitude Derivation for P_SUM_CTX (u -> a_code, shown work)

4.1.5.1 Inputs (all measurable, no invented signals)

For each candidate summary emission event e (one band -> one pulse candidate) we compute:
	-	chi_band(e): deterministic aggregate coherence of the band in the lower tier after the window seal (fixed-point [0,1])
	-	cont_band(e): deterministic continuum (coherence persistence) of the band after the window seal (fixed-point [0,1])
	-	clamp_band(e): deterministic clamp pressure proxy derived from phantom/aether aggregates (fixed-point [0,1])
	-	viol_band(e): deterministic violation proxy (projection failures, thrash counters, causality guard hits) in that window attributable to that band (fixed-point [0,1])
	-	budget_state(e): envelope headroom scalar from read-path counters (utilization, dispatch backlog) mapped deterministically into [0,1] (1 = plenty of headroom)

These are computed from sealed window aggregates only, so ordering inside the window cannot affect them.

4.1.5.2 Risk-adjusted propagation score (derived)

Define a risk-adjusted "propagate desirability" score p(e):

p(e) = chi_band(e) * cont_band(e)

That term encodes your continuum axiom: coherence that persists is what should propagate.

Define a risk penalty r(e):

r(e) = max(clamp_band(e), viol_band(e))

We use max, not sum, because any one of these being high is sufficient to require conservative amplitude.

Define an envelope gate h(e):

h(e) = budget_state(e)

Now amplitude scalar u(e) is derived as a risk-adjusted gated desirability:

u(e) = h(e) * clamp01( p(e) ? r(e) )

4.1.5.3 Quantization to a_code (deterministic)

a_code = round_fixed( a_scale * u(e) )

with:
	-	a_scale = 65535 (full uint16 span)
	-	rounding: fixed "round half up" integer rule
	-	clamp to [0, 65535]

This makes amplitude fully derived from measured coherence persistence, stability risk, and envelope headroom.

SECTION 4.1.6 - Mode bucket size and k_max derivation (no arbitrary 4096 / 12)

4.1.6.1 Define measurable budget limits

We require:

N_emit * E[harm_ops_per_pulse] <= E_hi

Where harm_ops_per_pulse ? k (because expanding to k harmonics costs O(k) operations; exact constant is kernel-defined but measured).

4.1.6.2 Choose k_max by envelope feasibility

We set k_max to the largest integer such that worst-case expansion stays within budget:

k_max = floor( E_hi / max(1, N_emit) )

But we do not allow k_max to exceed a stability cap derived from thrash risk. We estimate the "harmonic thrash slope" from calibration:

thrash_rate(k) = mean_over_windows( thrash_indicators | k )

Then define the largest k such that thrash stays below the policy target T_thr:

k_stable = max k where thrash_rate(k) <= T_thr

Final:

k_max = min(k_max, k_stable)

This is derived: if the GPU is weak or the environment is noisy, k_max will fall automatically.

4.1.6.3 Choose mode bucket size from required resolution

We need a_code to encode two things:
	-	harmonic index k (0..k_max)
	-	within-mode strength (resolution within a mode)

Given uint16 total codes (65536), allocate:

codes_per_mode = floor(65536 / (k_max + 1))

That becomes the mode bucket size:

mode_bucket_size = codes_per_mode

This is not arbitrary. It's the deterministic discretization given k_max.

Then:

k = floor(a_code / mode_bucket_size)
strength = (a_code % mode_bucket_size) / mode_bucket_size

If you later change k_max (due to different hardware or calibration), the mode_bucket_size changes accordingly and must be versioned under profile_id.

SECTION 4.1.7 - Harmonic falloff derivation for broader coupling (derive alpha_ctx, show work)

You asked for broader coupling for contextual memory activation. We implement that as a slower harmonic decay law, but alpha_ctx must be derived, not chosen.

4.1.7.1 Candidate family (power-law decay)

We define harmonic weights for n = 1..k as:

Wn_raw(alpha) = 1 / (n ^ alpha)

Then normalize:

Wn_norm(alpha) = Wn_raw(alpha) / ?_{m=1..k} Wm_raw(alpha)

We constrain alpha to a feasible range that is meaningful and stable:

alpha ? [alpha_min, alpha_max] = [0.3, 2.0]

Lower alpha = broader coupling; higher alpha = tighter coupling.

4.1.7.2 Objective for alpha selection (same J, evaluated at tier summary)

For a fixed k policy and fixed emission rule, we evaluate J over the calibration slice as a function of alpha:

J(alpha) = E[ ?chi_hi(alpha) ] + E[ ?cont_hi(alpha) ]
? lambda_violation * E[ V_hi(alpha) ]
? lambda_thrash * E[ thrash_hi(alpha) ]
? lambda_budget * E[ budget_overrun_hi(alpha) ]

All terms are measured in the higher tier after applying summary pulses expanded with weights Wn_norm(alpha). This makes alpha a directly optimized parameter, not a guess.

4.1.7.3 Deterministic search (no floats, no "gradient" needed)

We pick a discrete candidate set A of alphas (fixed-point rationals) and do a deterministic argmax:

A = {0.30, 0.35, 0.40, ..., 1.50}  (step size can be 0.05 or derived from required resolution)

Compute J(alpha) for each alpha in A using the same calibration run replay (deterministic), then:

alpha_ctx = argmax_{alpha ? A} J(alpha)

Tie-break rule (deterministic): choose the smaller alpha (broader coupling) if J ties within a tolerance epsilon_J, because your intent is to prioritize contextual activation when not harmful.

4.1.7.4 Freezing and implementation

Once alpha_ctx is chosen, we precompute a small LUT for n=1..k_max:

pow_lut[n] = round_fixed( (n ^ alpha_ctx) * pow_scale )

Then weights per pulse are computed with integer math:

Wn_raw = pow_scale / pow_lut[n]
Normalize via integer sum and division with locked rounding.

No platform float pow is permitted in canonical execution; only LUT-based fixed-point.

Result: broader coupling is not "because we wanted it," it is because the calibration objective J is maximized at a lower alpha, and that alpha is frozen into the snapshot.

SECTION 4.1.8 - Summary Emission Policy (Derived Criteria, Pulse Counts, causal_tag Semantics)

4.1.8.1 What can be emitted (order-insensitive candidates)

A lower-tier window seal produces stable aggregates per band/attractor. Only aggregates that are invariant under permutation of micro-updates inside the window are eligible to emit. Eligible aggregates are:
	-	Band phase centroid: circular mean of member d5 (Theta_p, turns, wrapped)
	-	Band flux centroid: aggregate d6 (fixed-point mean)
	-	Band phantom/aether aggregates: d7/d8 aggregates (fixed-point mean or max, versioned)
	-	Band nexus aggregate: d9 binding delta aggregate (fixed-point mean)
	-	Band coherence: chi_band (aggregate of member chi_q, fixed-point)
	-	Band continuum: cont_band (persistence measure, fixed-point)
	-	Structural deltas: net membership changes, merges, splits that have met evidence thresholds

No per-pulse ordering-dependent values are allowed to drive summary. This prevents nondeterminism from thread scheduling differences.

4.1.8.2 Band eligibility score E_band (derived)

We decide whether a band emits a summary pulse by deriving a single eligibility score that measures "should this band influence higher-tier context now" under the same objective J:

Define desirability:

p = chi_band * cont_band

Define risk:

r = max(clamp_band, viol_band)

Define net effect magnitude (how much the band actually changed in this window), derived from the same spider normalization:

m = clamp01( |s_band| )  where s_band is the unquantized spider mixture scalar computed from aggregate deltas

Define envelope headroom gate:

h = budget_state (from higher-tier read-path envelope)

Then eligibility is:

E_band = h * clamp01( p ? r ) * m

4.1.8.3 Emission threshold is derived from budget (no arbitrary cutoff)

We do not pick a fixed threshold like 0.3. The threshold is derived so that the expected number of emitted pulses fits the higher-tier capacity.

We compute a target emissions count:

N_target = floor( (1 ? reserve) * P_hi )

Deterministic tie-break: if E_band ties, prefer higher cont_band, then higher chi_band, then lower band_id (or lower eid) to keep selection stable.

4.1.8.4 How many pulses per band (single vs multi-pulse)

Default is 1 pulse per selected band: one aggregated delta compressed by P_SUM_CTX spider encoder into (f_code, a_code).

Pulse count rule summary:
	-	Normal band: 1 pulse
	-	Multi-modal band (derived): 2 pulses
	-	Formal split/merge event: extra topology pulse(s) as described below

4.1.8.5 causal_tag semantics (exact meaning, no ambiguity)

Event types (locked):
	-	0x0 = DRIFT (ordinary aggregated delta)
	-	0x1 = ACTIVATE (explicit resonance emphasis; same data fields, but interpreted as "context activation favored")
	-	0x2 = MODE_A (multi-modal emission cluster A)
	-	0x3 = MODE_B (multi-modal emission cluster B)
	-	0x4 = MERGE (topology update: two bands merged)
	-	0x5 = SPLIT (topology update: one band split)
	-	0x6 = BIND_UPDATE (explicit nexus-binding topology change)
	-	0x7 = CLAMP_ALERT (informational: high clamp; higher tier should damp coupling)
	-	0x8-0xF reserved for future

How topology pulses are represented as literal pulses:

SPLIT:
A split event emits:
	1.	A SPLIT pulse targeting eid_parent describing the split
	2.	Two MODE pulses (MODE_A and MODE_B) targeting the two child eids, encoding their centroids

4.1.8.6 Deterministic routing of topology without extra payload fields

Topology needs identity mapping, but we said we're keeping pulses small. The deterministic way to avoid adding fields is:
	-	The eid in the pulse indicates the primary target (eid_new for MERGE, child eid for MODE_A/MODE_B).
	-	Any secondary identity mapping is derived from a deterministic registry rule: the lower tier commits a canonical ordering of band ids and stores a redirect table as part of durable state (not as per-pulse payload). The higher tier uses the same deterministic rule to interpret MERGE/SPLIT tags.

In other words: identity mapping is part of the shared VSD snapshot state, not transmitted every time.

4.1.8.7 Remains causal under tier ordering

All summary pulses emitted from tier L to tier L+1 carry tau_q equal to the sealed window index for tier L. Tier L+1 is forbidden to apply these pulses until tier L is sealed for that tau. This ensures that MERGE/SPLIT topology updates cannot arrive "early" and cannot conflict with lower-tier history. If summary bandwidth is insufficient, topology pulses take precedence over drift pulses (because topology errors cause long-lived divergence), which is a deterministic priority rule in the selection ranking.

SECTION 4.1.9 - Tier Envelope Measurement Mapping (Read-Path Only, No Meaning Injection)

4.1.9.1 Allowed telemetry signals (software-visible counters only)

The engine may use only standard software-visible metrics that are already accessible via GPU driver APIs or timing instrumentation. These include:
	-	Kernel dispatch time per window (measured by high-resolution timer around dispatch + completion barrier)
	-	Queue backlog (number of pending launches or measured completion latency)
	-	SM/compute utilization estimate (driver metric or inferred from elapsed time vs expected)
	-	Memory bandwidth utilization estimate (if available) or proxy from measured copy times / cache miss counters
	-	Allocation pressure (local to the engine: size of working buffers allocated/used)
	-	Thermal throttling indicators (if exposed) only as a "reduce workload" gate

No raw "electrical waveform," no per-transistor signals, no analog sampling. The counters are used only to shape how many pulses are processed/emitted per window.

4.1.9.2 Deterministic windowing (how envelope is sampled)

Envelope is computed per commit_state window. For each tier T and window tau:
	-	Start timer at window open
	-	Launch all scheduled kernels for that tier/window
	-	Wait on a deterministic barrier (CUDA event or equivalent)
	-	Stop timer at barrier completion

This yields t_exec(T,tau).

4.1.9.3 Core envelope scalars (derived, shown work)

We compute three primary saturation ratios, all mapped to fixed-point [0,1]:

Compute saturation (how close compute is to limit):

sat_compute = clamp01( t_exec / t_budget )

Where:
	-	t_budget is the allowed wall-clock time for that tier's window (derived from target FPS / tick cadence)
	-	t_exec is measured execution time for the window

Memory saturation (if bandwidth metric exists):

sat_mem = clamp01( bw_used / bw_budget )

If no direct bw metric exists, use a deterministic proxy:

sat_mem = clamp01( bytes_moved / bytes_budget )

Where bytes_moved is the engine's own measured transfers plus conservative estimates of kernel memory touches (from calibration constants).

Queue saturation (backlog / latency growth):

sat_queue = clamp01( (latency - latency_ref) / latency_span )

Where latency is measured completion latency, latency_ref is a frozen baseline, and latency_span is a frozen scaling constant (also from calibration).

These are purely operational constraints. They do not touch semantic state.

4.1.9.4 Single headroom scalar budget_state (derived)

We compress the envelope into a single headroom scalar:

headroom = 1 ? max(sat_compute, sat_mem, sat_queue)

Then:

budget_state = clamp01(headroom)

This is the only value the emission policy needs. It is monotonic: as the GPU gets busier or more throttled, budget_state decreases.

This mapping is intentionally conservative: the worst saturation dominates, because any one bottleneck is sufficient to require scaling down.

4.1.9.5 Reserve and jitter (derived, shown work)

We include a reserve fraction to prevent thrashing when jitter exists. Reserve is derived from observed variance in execution time during calibration.

During calibration, record execution times across K windows:

t_exec_1..t_exec_K

Compute deterministic jitter metric:

jitter = (percentile_95(t_exec) ? percentile_50(t_exec)) / t_budget

Then derive reserve:

reserve = clamp01( jitter )

So if the system has high variability, reserve rises automatically and emission is scaled down to keep the commit_state barrier stable. This is derived, not chosen.

4.1.9.6 How budget_state influences emission and k_max (explicit)

This ensures that on weaker hardware (or thermal throttle), harmonic spread compresses automatically rather than causing instability.

4.1.9.7 Determinism and replay constraints

Both modes preserve closure because budget_state affects only how much work is done, not what the physics/meaning is.

SECTION 5 - Crawler Subsystem, In-Simulation Encoder, and Persistent-Resonance Ingestion (Electronic-Signaling Execution)

This section defines the crawler and encoder as first-class components of the simulation itself, not external preprocessing scripts. The crawler does not "download text and store it as text" as the primary pipeline. Instead it drives a resonance-based ingestion process: web data is converted into pulses and injected into the manifold through the same GPU-executed electrical write-path used for all state evolution. The only persistent content is what collapses into stable resonance attractors (bands/anchors) under continuum and coherence rules. This prevents uncontrolled data bloat and keeps ingestion consistent with the closed causal system.

5.1 Subsystem placement: crawler and encoder live inside the simulation

The crawler and encoder are not separate applications that produce files for EigenWare to read later. They run as simulated modules under the same tier/commit_state protocol as everything else. Their outputs are not "raw documents." Their outputs are pulse streams and durable resonance structures. This is critical for determinism and safety: ingestion is constrained by the same projection rules, coherence scoring, merge/split policy, and envelope budgeting used by the rest of the engine.

5.2 What "persistent resonance of webpage data" means

That is how "we don't lose data" coexists with decay. Activations decay; structure persists. When a page is ingested, the characters and sequences induce resonance trajectories. If those trajectories repeatedly reinforce coherent bands (high chi + stable continuum), the system stores a latent attractor representing that content. If the page is noisy, contradictory, or non-reinforcing, the excitation decays and leaves minimal residue beyond clamp/uncertainty traces. In short: persistence is earned by coherence over time, not granted by storage.

5.3 Ingestion pipeline as pulses, not files

5.4 Electronic signaling and execution: what is direct, what is derived

The ingestion pipeline uses the GPU's electrical switching directly as the execution substrate. The "persistent resonance of webpage data" is formed because the pulse-driven updates are realized in the kernel and accumulate into stable attractors under deterministic evolution. We do not claim we are measuring analog electrical frequencies from the GPU. The "electronic signaling" is the physical implementation of the pulse integrator: kernels execute, switching occurs, and state advances. That is the directness: the encoding and ingestion are performed as in-GPU electrical execution, not as a heavyweight CPU text pipeline.

5.5 Crawler observation model (what it extracts, and why)

5.6 Encoder mapping rules (explicit, no mysticism)

The encoder turns crawler observations into three kinds of pulse candidates:

5.7 Causality and closed-system safety for web ingestion

Web ingestion can violate closure if it acts like an external oracle. EigenWare avoids that by treating the crawler stream as an external input channel that is only admitted through commit_state windows and explicitly tagged as input-origin. The crawler does not retroactively "update memory" for earlier ticks. It injects pulses at the current tau_q. If the same page is fetched later or the content changes, that is a new excitation, not a rewrite of history.

5.8 Budgeting, rate limits, and backpressure

Crawler ingestion is subject to the same envelope policy as all tiers. The crawler produces more observations than the encoder is allowed to inject. The encoder applies backpressure by ranking candidate pulses using a derived eligibility score analogous to E_band, except the desirability term is "expected reinforcement of existing coherent bands" and the risk term is "expected instability or novelty noise." This ensures that when the GPU is near saturation, ingestion becomes more selective rather than corrupting the manifold with half-processed noise.

5.9 Edge cases and explicit behaviors

5.10 What this enables

Because the crawler and encoder are inside the same simulation substrate, learning is not a two-stage "collect then train." It is continuous resonance accumulation constrained by envelope and causality. Because persistent content is stored as attractor structures rather than raw token streams, the memory footprint scales with coherent structure, not with page volume. And because higher tiers use broader harmonic coupling (P_SUM_CTX), the system can activate context from compressed summaries rather than replaying the entire crawl history.

SECTION 5.11 - Concrete Mapping Spec: Raw Text -> ASCII Phase Injection -> Formation Deltas -> Resonance Collapse (Causality-Safe)

This subsection pins down the exact, mechanical path from raw webpage text to pulse injection in the manifold. The goal is to be explicit about how characters become phase-coded excitations, while preserving your axioms: phase stores the mapping, coherence creates meaning, persistence is retrievability (continuum), and the system remains causally closed after boundary injection. Nothing in this mapping assumes the GPU is a sensor; all "electrical signaling" is execution of these updates via kernel switching.

5.11.1 Deterministic text segmentation (structural units)

Before any character mapping, the crawler produces deterministic segments. Segmentation is part of the ingestion boundary and must be stable across runs:
	-	Page -> blocks by DOM structure: title, headings, paragraphs, list items, captions
	-	Each block -> sentences by punctuation rules (versioned)
	-	Each sentence -> segments by whitespace + punctuation splitting (versioned)
	-	Each segment -> normalized surface form (lowercase; Unicode normalized; punctuation stripped per policy)
	-	Optional: preserve a canonical "surface form" coord_sig for the block for strict replay logging

This ensures the encoder receives repeatable units and can assign causal tick coordinates predictably.

5.11.2 Two-layer mapping: characters (phase) vs meaning (coherence)

5.11.3 ASCII phase mapping (canonical)

We define the canonical ASCII map on bytes in [0,255] after normalization (for UTF-8, text is converted to bytes first). Each byte b becomes a phase target in turns:

theta_byte_turns(b) = b / 256

To inject a byte into a formation process, we compute the phase delta relative to a running local carrier phase:

?5 = wrap_turns( theta_byte_turns(b) ? theta_carrier )

where wrap_turns yields the shortest signed distance in [-0.5, 0.5) turns. The carrier phase is a transient local state for the formation event, not a durable lifetime object.

Carrier update rule (deterministic, per formation stream):
theta_carrier ? wrap01(theta_carrier + carrier_step * ?5)

5.11.4 Word formation: from bytes to a word-attractor candidate

Formation stream procedure (per word instance):
	1.	Select a projection target:

	2.	For each byte bj in w:

	-	Compute ?5 from theta_byte_turns(bj) relative to theta_carrier (5.11.3).
	-	Construct a Basis9 delta with:
	-	dominant phase term: ?5
	-	a small nexus binding hint ?9 toward the current sentence/paragraph context band (so formation is context-situated)
	-	clamp terms ?7/?8 set conservatively based on novelty risk (derived from how often this staging band produced thrash in recent windows)
	-	Compress ? via the spider graph under the crawler formation profile P_CRAWL_FORM into (f_code, a_code).
	-	Emit a pulse targeting the staging band eid with causal_tag = ACTIVATE or DRIFT depending on whether this byte is a boundary byte (see 5.11.5).

	3.	After bytes are injected, emit a boundary pulse:

5.11.5 Boundary encoding (start/end anchors without storing letters)

Implementation-wise, you do not store "b1" or "bn." You simply allocate more harmonic budget (higher a_code) to those pulses so the attractor learns stronger boundary hooks.

5.11.6 Sentence and paragraph context injection (coherence scaffolding)

Words do not become meaning in isolation. The encoder injects contextual scaffolding so coherence can bind words into higher structures.

For each sentence:
	-	Maintain a sentence-context band (eid_sentence) created deterministically from (page_id, block_id, sentence_index) in strict replay mode or from an evolving context allocator in adaptive mode.
	-	For each word segment w:
	-	If w already has a stable attractor eid_word: emit one activation pulse targeting eid_word with profile P_LANG_CTX and broad coupling; also emit a binding pulse updating ?9 between eid_word and eid_sentence.
	-	If w is in formation staging: emit formation pulses as above; also emit weak binding pulses to eid_sentence so the staging region is context-shaped.

For each paragraph:
	-	Similar, but binding is weaker and coupling is broader (paragraph-level context should activate more widely but with lower strength).

This is how "memory bands" become the primary compression: repeated co-occurrence patterns reinforce the same bindings, raising continuum and enabling retrieval with fewer pulses later.

5.11.7 Promotion rule: when a staging band becomes a stable attractor

A staging band is promoted only when persistence evidence exists. This prevents one-off noise from becoming permanent structure.

When promotion happens, it is represented as a topology pulse (SPLIT or MERGE style) using causal_tag semantics from Section 4.1.8, so higher tiers learn the new structure deterministically.

5.11.8 Retrieval rule: "persistence is retrieval," not permanence

This is why the system scales: bytes are an initial formation mechanism; after collapse, the representation is an addressable resonance attractor activated by one pulse.

5.11.9 Closed-system causality guarantee

SECTION 5.15 - Hub-Conditioned Residual Encoding (Maximum Dependence, No Carrier Coupling)

SECTION 5.16 - Cross-Modal Hub Bands (Object/Concept Constraints, 2D?3D Join)

A minimal hub schema that fits the existing pulse model is:

SECTION 5.17 - Modality Delta Constructors (Explicit Mappings into Basis9)

Each modality has its own observation extractor, but all of them output a Basis9 delta packet before spider compression. The packet is "what changed" plus "what this evidence should bind to."

Image constructor (tile scan, headless)
An image is partitioned into deterministic tiles. Within a tile, pixel traversal is encoded as spatial deltas (?1, ?2), while pixel intensity/channel content becomes phase deltas (?5). Local gradient energy (edges) maps to ?6 as a deterministic function of |\nabla I| or |\Delta I|. Tile->hub binding uses ?9. Hub prediction reduces work by turning full encoding into residual encoding: the tile encoder compares observed local gradients and intensity statistics against hub-implied priors and emits only the difference.

A practical mapping table (Basis9 intent) that Copilot can implement without guesswork:

Display constructor (presentation surface; delta signaling to monitors/phones)

The canonical rule is: publish deltas when coherence/phase transitions occur, and let the
device compositor/refresh determine scanout. This is sampling in event time, not raster time.

Observable selection (deterministic):

Delta transport (eigen residual preferred):

Delta_c_tile = c_tile(t) - c_tile(t-1)

Event gate:

A tile is eligible for emission in a window if either:
- its phase-transition gate is true (Section 2.5), or
- the hub indicates a structural update binding for that tile (tile->hub binding updated).

This keeps presentation bandwidth aligned with actual state change.

Device binding:

This makes the "continuous emergent light" claim precise: the internal radiance proxy evolves
on commit_state boundaries, and the presentation surface receives a stream of coherent deltas that
converge continuously from the observer's perspective, while remaining strictly discrete and
deterministic in the engine.

SECTION 5.18 - Profile Selection and Calibration (No Arbitrary Constants)

SECTION 5.19 - Training Curriculum Control, Verification Scoring, and Dataset Hygiene

SECTION 5.20 - Single-File Persistence: Streams, Pulses, and Rehydration Invariants

A canonical record shape:

SECTION 6 - File Encodings, Crawler Identifiers, and Multimodal Persistence (Single-Container Spec)

This section defines how EigenWare classifies and encodes every encountered artifact (web pages, documents, code, images, audio, video, datasets, and course modules) into one unified persistence container. The crawler's role is to identify artifacts, segment them into stable streams, and attach strict trust labels (especially for accredited open courses). The encoder's role is to transform each stream into pulse records (and topology updates) using the correct extractor and spider-graph profile, so the same file can be rehydrated deterministically without relying on any encoder carrier state.

6.1 Canonical container: one persistence format for all modalities

6.2 Record ledger: the only persisted primitives

All content is represented using four record families. Everything else is derived.

PulseRecord - atomic committed update packet
Fields: (eid, tau_q, tier_id, modality_id, stream_id, f_code, a_code, profile_id, causal_tag)

BandRecord - declares or updates a stable band/attractor coord_sig
Fields: (eid, band_type, birth_tau, parent_eid(optional), signature_id9, band_state_digest, flags)

BindingRecord - explicit nexus/binding updates between bands (including scene bands)
Fields: (src_eid, dst_eid, tau_q, binding_kind, strength_code, profile_id, flags)

ManifestRecord - maps external artifacts to internal streams and coord_sig
Fields: (artifact_id, stream_id, mime, extractor_id, trust_class, course_class(optional), coord_sig, segment_map_ref)

6.3 Identifier system: stable, merge-safe, and replay-safe

EigenWare uses three identifier classes: artifact identifiers, stream identifiers, and internal band identifiers.

6.4 Trust classes and strict course accreditation gate

6.5 Band types: modality-local bands and persistent cross-modal scene bands

EigenWare maintains modality-local bands and explicit, persistent cross-modal "scene bands."

6.6 Segment maps: how every artifact is broken into stable sequences

Every ManifestRecord references a segment_map that defines stable unit boundaries for the extractor. Segment maps are versioned and must be deterministic.

Segment maps allow the encoder to reconstruct carriers when necessary without persisting carriers as state.

6.7 File class encoding: web pages and text documents

6.8 File class encoding: PDFs, LaTeX, BibTeX, and scientific material

6.9 File class encoding: source code, specs, and software engineering assets

6.10 File class encoding: structured data (JSON/YAML/TOML/CSV)

6.11 File class encoding: images (2D) and latent 3D (headless v1)

6.12 File class encoding: audio (pitch identity and event identity)

Audio is encoded as frame-based spectral streams. From the same stream, EigenWare promotes two families of attractors.

6.13 File class encoding: video (motion motifs and synchronized scenes)

6.14 Extractor registry: versioning and normalization rules

6.15 Profile registry: which spider profiles are legal per extractor

6.16 Cross-modal alignment: how streams bind cleanly in one file

6.17 Creativity: how encoding becomes a creative engine for users

EigenWare' creativity is not a separate "imagination module." It is the same resonance substrate doing controlled recombination. The encoding process is what makes creativity possible because it compresses large experience streams into stable constraint bands and persistent scene joins. Creativity arises when a user prompt activates multiple partially overlapping scenes and bands, and the engine explores phase-consistent trajectories that satisfy the constraints while varying within projection tolerance.

In practical terms, the user's prompt excites a small set of anchor/scene bands (topic, style, goal, constraints). The engine then performs constrained synthesis by sampling candidate bindings and latent 3D hypotheses that are coherent with those anchors, preferring solutions that reuse high-continuum structures. Because the substrate stores constraints rather than raw assets, it can generate novel combinations: new designs, new stories, new code architectures, new visual scenes, or new explanations, while still being anchored to learned invariants (physics, math, syntax, design rules).

6.18 Creativity safety and quality: constraint-first synthesis

6.19 Creativity interfaces: how users "steer" the engine

User steering is modeled as intent pulses that preferentially activate certain scene bands and binding routes. The system treats prompts as constraint declarations: what must hold, what may vary, and what style or domain priors are desired. The engine then routes synthesis through bands that match those constraints, rather than performing a free search. This is the core reason to keep accreditation strict: clean, structured learning produces cleaner constraints, which improves creative controllability.

6.20 Minimal implementation target for Copilot

To implement Section 6 without ambiguity, Copilot should build:

- The container writer/reader with the four record families (Pulse/Band/Binding/Manifest) and a versioned header.
- The crawler identifier pipeline that emits artifact_id, stream_id, extractor_id, trust_class, course_class, and segment maps.
- The extractor registry with strict versioning and normalization coord_sig.
- The profile registry mapping extractors to allowed profiles.
- The persistent SCENE_* band promotion rule that converts repeated emergent joins into typed scene bands.
- The dual audio band promotion logic (pitch vs event) and their binding into scenes.

SECTION 7 - High-Value Public Corpora and Domain Packs (for Crawler Ingestion)

7.1 What we mean by "confirmed", "documented", and "common"

For modern commercial models, exact training datasets are often not fully disclosed. This spec therefore separates three categories:

EigenWare should prioritize Confirmed and Common, and treat Documented as optional until validated.

7.2 Text and knowledge corpora (core language + encyclopedic structure)

Domain Pack: TEXT_CORE_V1

Primary corpus targets:
- Wikipedia dumps (structured world knowledge, entity linkage, reference style)  
- Common Crawl derived corpora (web language breadth; best used with aggressive dedup and boilerplate removal)  
- The Pile (broad multi-domain text mixture; widely used in open LLM training)  
- arXiv (scientific writing; math/physics/CS)  
- PubMed / PubMed Central open access (biomed; careful license tags)  
- Stack Exchange data dumps (high-quality Q/A and reasoning)  
- Project Gutenberg (public-domain books; note: not modern technical prose)

7.3 Code corpora (programming languages + repositories + build logic)

Domain Pack: CODE_CORE_V1

7.4 Image corpora (2D constraints, geometry priors, and artifact detection)

Domain Pack: IMAGE_CORE_V1

7.5 Audio corpora (phonetics, timbre, events, and note-like identification)

Domain Pack: AUDIO_CORE_V1

7.6 Video corpora (motion, causality, and 3D-constraint intuition)

Domain Pack: VIDEO_CORE_V1

7.7 3D geometry and physics priors (constraint libraries)

Domain Pack: GEOM_PHYS_CORE_V1

7.8 Open courseware and accreditation tagging (structured learning sequences)

Domain Pack: COURSEWARE_V1

7.9 Vendor-style "mixture" packs (what the ecosystem converges on)

This is a synthesis pack, not a claim about any single vendor. It mirrors what repeatedly shows up in strong public training mixes:

Domain Pack: MIXTURE_LLM_STANDARD_V1  
Typical components: Common Crawl derivatives + Wikipedia + books + code corpora + Q/A dumps + papers.

EigenWare should ingest this as a versioned pack so it can reproduce the "generalist prior" that most frontier systems rely on.

7.10 A short "science and engineering feed" list (optional)

The crawler should prefer sources that expose transcripts, captions, or companion writeups, because those produce better deterministic segment maps than purely streaming video.

7.11 Domain pack table (starter registry entries)

| domain_pack_id | primary modality | typical artifact types | primary value | main risks |
|---|---|---|---|---|
| TEXT_CORE_V1 | text | dumps, html snapshots, pdf OA | reasoning + world structure | boilerplate, duplication |
| CODE_CORE_V1 | code | repos, tarballs, docs | build intuition + tooling | license variance, duplication |
| IMAGE_CORE_V1 | image | jpeg/png + captions | object priors + grounding | watermarks, noise |
| AUDIO_CORE_V1 | audio | wav/flac + transcripts | speech + event ontology | alignment drift |
| VIDEO_CORE_V1 | video | mp4 + captions | motion + causality | sampling determinism |
| GEOM_PHYS_CORE_V1 | 3D/physics | meshes, CAD, sim traces | constraint intuition | license + scale |
| COURSEWARE_V1 | mixed | html/pdf/video+text | structured curricula | attribution clarity |

7.12 Section 7 integration notes

SECTION 9 - Operational Contracts, Registries, and a Single-File Test Harness (Copilot Guidance)

9.1 Registry layer (no ad-hoc choices)

Required registries:
- ExtractorRegistry: extractor_id -> supported_mime, normalization_rules_digest, segmentation_rules_digest, deterministic ordering rules, fallback_extractor_ids  
- ProfileRegistry: profile_id -> spider profile definition, harmonic policy, axis clamp policy, allowed causal_tag set, allowed extractor_id set  
- DatasetDomainRegistry: domain_id -> acquisition mode, trust_class defaults, scheduling policy, sampling policy, provenance rules  
- BandTypeRegistry: band_type -> promotion rules, merge/split hysteresis rules, legal binding kinds, persistence rules (including SCENE_*)

Every registry entry must be versioned. A behavior change is a new ID, not an in-place update.

9.2 Deterministic replay contract (strict mode)

EigenWare must support a strict replay mode where the same inputs (same artifacts and the same registries) produce the same artifact_id values, stream_id values, segment map coord_sig, record ordering within each tau_q commit_state window, promotion/merge/split decisions (and their trace log), and final container coord_sig for a fixed fixture corpus.

9.3 Budget + backpressure subsystem (enforced envelope)

Crawler and encoder share one BudgetManager. BudgetManager enforces max concurrent artifacts per domain, max active streams per artifact, max pulses per tau_q commit_state window, max promotion attempts per window, and device headroom targets. Sampling density is budget-driven: metadata-only is default; dense pulls happen only when coherence/novelty justifies it and budget is available; escalation must be reversible mid-run.

9.4 Dedup + near-dup filter (mandatory)

9.5 Provenance + license tagging (first-class metadata)

9.6 Extractor robustness (fail-closed)

9.7 A/V time alignment repair (deterministic correction)

9.8 Band thrash guard (merge/split hysteresis and quarantines)

9.9 Copilot acceptance checklist (testable obligations)

- extractor_id changes imply normalization_rules_digest and/or segmentation_rules_digest changes  
- ProfileRegistry enforces allowed extractor_id and causal_tag sets  
- Every ManifestRecord includes provenance, trust_class, domain_id  
- Strict replay yields identical container coord_sig for fixture corpus  
- Promotion decisions log a reason code and minimal replay trace  
- BudgetManager enforces caps and can downshift sampling mid-run  
- Dedup runs before promotion and reduces redundant emissions  
- Fallback extraction never reuses the same extractor_id  
- Alignment repair records its correction parameter and is deterministic  
- Thrash guard prevents oscillatory merge/split behavior in fixtures  
- Watermark patterns classify as artifact bands, not scene content, in watermark fixtures  
- No silent exceptions; all errors log with artifact_id/extractor_id and exc_info=True

9.10 Single-file contract test harness (explained, explicit, and complete)

File name (repo path):
tests/test_kernel_contract.cpp

Required structure of the harness

B) Registries (enforcement, versioning hooks)
The harness must implement minimal registries and enforce rules:

Extractor determinism acceptance:
- Same raw fixture -> same norm string
- Same norm string -> same segments list
- Rules coord_sig remain stable for the fixture

E) Dedup (exact dedup for contract coverage)
The harness includes exact_dedup(items) that removes exact duplicates by SIG9 coord_sig while preserving first occurrence ordering.

Dedup acceptance:
- Duplicate blocks reduce emitted pulses
- The first instance of a duplicated block is preserved and appears in the same position as before dedup

Robustness acceptance:
- Corrupt fixture does not produce any PulseRecord emission in the harness logic.

G) A/V alignment repair (single offset, deterministic)
The harness includes a toy alignment estimator:
estimate_alignment_offset(caption_tokens, audio_events) -> offset_k

Alignment acceptance:
- A fixture with one leading "noise" event in audio yields offset_k == 1.
- The function is pure and stable (no randomness, no adaptive drift in strict mode).

H) Thrash guard hysteresis (cooldown)
The harness includes a ThrashGuard with:
- cooldown: integer number of tau_q steps
- last_change_tau dict keyed by change_key

Thrash acceptance:
- First change at tau_q = T is allowed
- Any repeated change for same key at tau_q < T + cooldown is rejected
- Change is allowed again at tau_q >= T + cooldown

If the detector or fixture is not implemented, the test MUST hard-fail with reason code:
- WATERMARK_FIXTURE_MISSING

If the detector or fixture is not implemented, the contract test SHALL hard-fail with reason code WATERMARK_FIXTURE_MISSING. Release profiles MUST not allow xfail for this test.

Minimal fixture set (all embedded in the test file)

Strict replay acceptance test (the main one)

This test is the "spec compliance alarm bell". Any nondeterministic ordering, accidental randomness, or silent behavior drift will change the coord_sig and fail the test.

What Copilot should implement first to satisfy this harness

----------------------------------------------------------------
SUBSECTION: GPU SIGNALING  MATHEMATICAL OPERATORS (APPENDED)
----------------------------------------------------------------

This subsection appends mathematical operators to the existing
GPU signaling section. No prior text is modified.

Eigenstate Representation:
Each eigenstate E_i is represented as:
E_i(t) = (phi_i(t), A_i(t))

where:
- phi_i(t) is phase trajectory
- A_i(t) is amplitude envelope

Delta Phase:
phi_i(t) = phi_i(t) - phi_i(t-1)

Composite Phase Trajectory:
Phi_comp(t) = _i (A_i(t) * phi_i(t))

Temporal Envelope:
A_env(t) = _i |A_i(t)|

Pulse Signal:
S(t) = A_env(t) * Phi_comp(t)

Order of Operations:
1. Compute phi_i for all eigenstates
2. Weight deltas by A_i(t)
3. Sum into Phi_comp(t)
4. Compute A_env(t)
5. Emit S(t) as GPU pulse

----------------------------------------------------------------
END SUBSECTION
----------------------------------------------------------------

X.X Purposeful Criteria Driven File Emergence

This subsection applies to all remaining sections.

X.X.1 Purpose Definition Operator
- Define intent and effects

Harness:
- Verify completeness

X.X.2 Dependency Resolution Operator
- Enumerate dependencies

Harness:
- Verify acyclic order

X.X.3 Execution Sequencing Operator
- Define stepwise order

Harness:
- Simulate determinism

X.X.4 Event and Dispatcher Operator
- Define events and routing

Harness:
- Verify delivery

X.X.5 Consolidation Gate
- Approve file emission

Harness:
- Verify all prior harnesses pass

[PLACEMENT GROUP] Section 1

[PLACEMENT TAG] Section 1 -> 1.1
1.1 What we actually "take from the GPU" (execution envelope, not sampled electronics)

A concrete mapping is below. This is intentionally phrased as "derived parameters," so it's clear that we are converting runtime metrics into constraints.

GPU/Runtime Observable (measured)	Derived Envelope Parameter (used by sim)	Why it matters for stability	Typical consequence if exceeded
Effective kernel dispatch throughput (kernels/sec or updates/sec measured in-loop)	max_delta_updates_per_sec	Caps how many Eigenstate delta commits can be made in real time	Backlog grows, latency spikes, coherence collapses due to missed commit_state cadence
Average kernel time and variance (ms, jitter)	update_jitter_budget, tick_commit_window	Controls how much "slack" the tick scheduler needs to avoid causal mis-order	Tier collisions and non-deterministic ordering if commit_state windows overlap
Available VRAM and allocation churn	max_active_eigenstates, max_band_working_set	Caps active state set before paging/fragmentation kills determinism	Thrashing leads to missed updates and forced resets of inference assumptions
Memory bandwidth pressure (inferred from copy volume and stalls)	max_state_io_per_tick, prefer_delta_compaction	Forces delta-compaction rather than wide state copies	If ignored, the simulation becomes copy-bound and loses its event-driven property
Numeric precision mode (FP32/FP16, etc.)	delta_quantization_policy, stability_margin	Determines allowable delta magnitude and smallest resolvable phase increments	Too fine -> noise; too coarse -> aliasing; both break coherence band integrity
SM occupancy / utilization feedback	lane_count_target, adaptive_lane_allocator	Keeps GPU ~target utilization while preserving closure	Underutilization wastes capacity; overutilization forces dropped events

The key philosophy is that the GPU defines "how many committed updates can exist per unit time," and the simulation must remain causal by never committing more updates than the envelope allows.

[PLACEMENT TAG] Section 1 -> 1.2
1.2 What a "pulse" is in this system (and what it is not)

[PLACEMENT TAG] Section 1 -> 1.3
1.3 Text -> phase: how ASCII becomes phase offsets (storage substrate)

A deterministic ASCII-to-phase mapping must be defined so it is invertible and bounded. A minimal rule that Copilot can implement is: choose a phase interval [phi_min, phi_max] (for example [0, 2?) or a normalized [0, 1)), assign each ASCII code a unique phase offset within that interval, and store the resulting phase offsets as the phase sequence for an injected fragment. If UTF-8 is later supported, it is handled by mapping bytes similarly, but meaning is ultimately stabilized by coherence bands (harmonic relationships), not by the literal code page.

This is the mechanical pipeline for text encoding:

[PLACEMENT TAG] Section 1 -> 1.4
1.4 9D delta formation: embedding, projection, and the collapse rule

Once text exists as a phase sequence, the encoder embeds it into the 9D coherence substrate as a candidate state. This does not mean "every character becomes a new Eigenstate." The critical collapse rule applies immediately: the candidate state is projected onto the existing Eigenstate basis. If the projection error is within tolerance and coherence is adequate, the system does not admit new states-it produces a delta (dE9) relative to an existing Eigenstate (or a small set of them) and discards the raw candidate snapshot.

[PLACEMENT TAG] Section 1 -> 1.5
1.5 Spider graph encoding: 9D -> frequency and amplitude (pulse synthesis)

Logically, the spider graph defines nine fixed radial axes (one per dimension). Each axis has a normalization function (so di is bounded) and a weight (so some axes contribute more strongly). The mapping produces:
	-	a scalar "frequency" value: this is the signed aggregate of the weighted per-axis scaled delta components (no global uniform normalization), clamped to a safe range; it functions like the phase-advance coefficient.
	-	a scalar "amplitude" value: this is the update strength and multiplexing budget; it can encode confidence, band strength, and priority so multiple updates can coexist in a bounded frequency band without needing separate channels.

[PLACEMENT TAG] Section 1 -> 1.5
1.5 Constraint Operators Required by Effective-Constants Pipeline

[PLACEMENT TAG] Section 1 -> 1.5.1

1.5.1 Operator: relativistic_correlation(...)

[PLACEMENT TAG] Section 1 -> 1.5.2

1.5.2 Operator: stochastic_dispersion_factor(...)

[PLACEMENT TAG] Section 1 -> Subsection 1.6 (or nearest existing "Effective Constants" subsection, if present)
1.x Binding Rule: effective_constants() Composition Order

[PLACEMENT TAG] Section 2 -> Subsection 2.3.1

[PLACEMENT TAG] Section 1 -> 1.6
1.6 How "GPU pulses" become simulation injection (kernel evolution step)

[PLACEMENT TAG] Section 1 -> 1.7
1.7 Causality and closed-system guarantees (why injection doesn't violate closure)

The closed-system requirement is satisfied if three conditions hold. First, every pulse commit_state is associated with a tick_id and tier_id, and commits are ordered deterministically within a commit_state window. Second, injection is not allowed to retroactively reorder committed events; it can only affect future ticks or cause a controlled refinement at the boundary where it is applied. Third, any energy/constraint bookkeeping your substrate uses must account for injection as an external input term, so the internal evolution remains consistent: the system remains closed with respect to its own rules because the input is modeled as an explicit boundary excitation, not a hidden nonlocal influence.

This is the causality-safe interpretation: the simulation is "closed" in the sense that it never produces updates without accounted causes; external inputs are explicit causes injected at defined times. You're not violating causality; you're adding boundary events. The internal ordering between tiers remains causal if you enforce a tier commit_state protocol: lower tiers must finalize their relevant commit_state slice before higher tiers can aggregate and commit_state their own derived deltas for the same causal window. That's your "minimum one step behind" principle expressed as a deterministic commit_state barrier.

[PLACEMENT TAG] Section 1 -> 1.8
1.8 How qubit density scales with pulses, tiers, and bands (and why it doesn't explode)

In this substrate, "qubit density" is the number of actively-updated Eigenstates (plus their active deltas) within a tick window. Injection does not inherently increase qubit density because most injected states project onto existing Eigenstates and reinforce coherence bands rather than creating new anchors. Qubit density rises only when projection error exceeds tolerance or coherence fails, forcing local refinement or Eigenstate splitting. That refinement must remain local and bounded by envelope constraints; if not, the system responds by raising admission thresholds, increasing inference/coasting, or deferring refinement across ticks (still causal, just delayed resolution).

When one simulation tier supports another, the higher tier never consumes raw lower-tier microstates. It consumes aggregates: band-level summaries and delta statistics. This is why higher tiers can run with fewer active qubits: many lower-tier details collapse into band-level persistence structures. The higher tier's qubits represent the coherent modes of the lower tier, not all of its activity. This is the formal reason "a simulation of qubits can support another simulation of qubits" without linear blow-up: the interface is projection + aggregation, not state cloning.

Part 2/Step 2 - Basis9 (9D) Axis Definitions + Band Math (Projection, Tolerance, Coherence/Continuum, Merge/Split)

[PLACEMENT GROUP] Section 2

[PLACEMENT TAG] Section 2 -> 2.1
2.1 Basis9 is not "feature space"; it is the canonical manifold and ledger substrate

EigenWare uses a 9D manifold ("Basis9") as its compute substrate. The point of Basis9 is that it is stable, deterministic, replayable, and compressive: state does not live as strings or dense vectors, it lives as anchored phase/coherence state with append-only update events. The simulation is driven by constraint evolution of these anchors; procedural "physics code" is not permitted as an alternative dynamics engine. In this model, "meaning," "memory," and "intent" emerge from persistent coherence relationships between phase trajectories, not from storing and replaying token sequences.

[PLACEMENT TAG] Section 2 -> 2.2
2.2 Basis9 axis order is locked, and it is not the same thing as "9 semantic features"

Your canonical Basis9 axis order is:

This is the corrected axis table (the one you highlighted earlier, fixed to your axioms). "Main writers" here means which subsystem is allowed to mutate the coordinate in canonical paths; the encoder may propose deltas, but commit_state is always via the same deterministic evolution boundary.

Axis	Canonical name	Canonical domain	Operational meaning in EigenWare	Main writers (canonical)	What it influences
d1-d3	basis9_spatial	implementation-defined, quantized	3D projection / spatial embedding for locality and visualization; can be a true XYZ projection or a manifold embedding	evolution + constraint mapping	neighborhood selection, local interaction neighborhoods
d4	basis9_temporal	integer tick (tau_q)	causal ordering coordinate; commit_state barriers and tier ordering are expressed against this	scheduler + tier protocol	causality gates, "same timeline" interaction rule
d5	basis9_phase_coherence (Theta_p)	turns in [0,1), fixed-point	stored phase carrier for coherence space; this is where phase persistence lives canonically	encoder injects, evolution advances	harmonic alignment, projection distance, memory identity
d6	basis9_flux	fixed-point scalar/vector (bounded)	gradient/flow axis; governs admissible delta "flows" and how structures transport across the manifold	evolution (bounded by constraints)	interaction transfer gating, gradient constraints
d7	basis9_phantom	fixed-point (bounded)	classification/threshold axis; out-of-phase coexistence and "dark matter" style non-interaction lives here	coherence classifier + constraints	whether a state is interacting vs non-interacting regime
d8	basis9_aether	fixed-point (bounded)	stabilization/damping axis; clamps high-frequency delta energy and prevents runaway oscillation	stabilizer + constraints	stability, damping, safe spider-map output bounds
d9	basis9_nexus	fixed-point (bounded)	relational binding / cross-link axis; governs structural linkage and consolidation behavior	band/binding logic	clustering, anchor consolidation, "objectness" persistence

Cold Spot traversal (relative ledger discontinuity in phase):
- When a `CMB_COLD_SPOT` constraint packet applies to the current phase-shell domain (region descriptor membership), the lattice MAY
  exhibit a relative discontinuity in phase evolution that manifests as correlated changes across two ledgers:
  (a) `chi_q` control/visualization stability fade, and (b) `m_q` canonical forgetting via mass leakage into the global reservoir.
- These are distinct ledgers. Correlation is expected only through shared constraint bias; no direct equivalence is assumed.
- Leakage observed in the near-critical headroom band (below the cap, never at 1.0) during traversal may be labeled hawking-like for
  telemetry, without introducing a new emission operator.

Thermal ledger ontology binding (anchor-encoded, non-exportable):
- The substrate SHALL encode the absolute-zero reservoir baseline and the hawking-like discontinuity routing gates directly into the immutable
  constraint fabric of the semantic ingress anchor (anchor_id = 0) at boot.
- This encoding MUST reuse the same anchor-side constraint storage used by the symbol->phase mapping (no external tables, no offline encoder).
- Storage convention (anchor0.constraint_fabric.basis words):
  - basis[18] abs_zero_reservoir_q63 = 0
  - basis[19] cap_num_u64            = 99
  - basis[20] cap_den_u64            = 100
  - basis[21] emit_gate_mask_u64     = bit0: near_cap, bit1: in_coldspot
  - basis[24] delta_time_tensor_q63  = 0 => default q63_one/256; SHALL be calibrated at boot from measurement granularity and then frozen

Boot calibration (authoritative source for delta_time_tensor_q63):
- SubstrateManager SHALL calibrate delta_time_tensor_q63 ONCE at boot before copying AnchorDef bank to device memory.
- Calibration reads only measurable envelope counts (no map exposure). The reference provider is NVML power telemetry (counts = mW).
- Define an idle baseline to avoid baking driver- or board-specific offsets into the spacing:
  - Collect N_idle = 64 samples of power_mw with sleep_ms = 10 between samples.
  - idle_mw = median(power_mw_samples) (deterministic: sort and select middle).
- Define the usable maximum range:
  - enforced_limit_mw = NVML enforced power limit (mW).
  - I_max_count = max(enforced_limit_mw - idle_mw, 1).
- Define the minimum reliable non-zero step (measurement granularity / noise floor):
  - Collect N_noise = 256 additional samples of power_mw with sleep_ms = 10.
  - diffs[i] = abs(power_mw[i] - power_mw[i-1]) for i >= 1.
  - Keep only non-zero diffs.
  - I_min_meas_count = max( percentile_10(diffs_nonzero), 1 ) where percentile_10 is:
    - sort diffs_nonzero ascending
    - idx = floor((len-1) * 10 / 100)
    - value = diffs_nonzero[idx]
- Convert ratio to Q63 step using integer-only math and deterministic rounding:
  - delta_time_tensor_q63 = round_half_even( q63_one * I_min_meas_count / I_max_count )
  - If computed delta_time_tensor_q63 == 0, force it to 1.
- Freeze:
  - Write anchor0.constraint_fabric.basis[24] = delta_time_tensor_q63 and treat it as immutable after boot.
- Fallback:
  - If telemetry is unavailable, SubstrateManager SHALL leave basis[24] = 0, which triggers the canonical default delta = q63_one/256.
- The routing itself is not a new physics operator; it is a deterministic ledger split of an already-defined leakage quantity into:
  (a) reservoir_mass_q63 (thermal pool / abs0 reference) and (b) radiation_mass_q63 (discontinuity emission sink).
Absolute zero reference (CMB reservoir):
- The global reservoir / CMB thermal pool defines **absolute zero** in EigenWare.
- The reservoir ground state (`M_res = 0` at boot) SHALL be treated as **T_abs0 = 0**, and all thermal/temperature metrics SHALL be expressed as non-negative deltas above this baseline.
- Negative thermal deltas are forbidden; clamp to 0 deterministically.

Phase-dynamics amplitude valence spacing (environment processing rule):
- The phase-dynamics system SHALL treat the time-tensor gradient amplitude as a quantized valence field, using the calibrated spacing:
  - delta = anchor0.constraint_fabric.basis[24] (delta_time_tensor_q63)
- Each tick, before coherence, near-cap gating, or leakage routing is evaluated, compute:
  - amp_mag_q63 = min( abs(pulse_amplitude_q63), cap_q63 )
  - amp_used_q63 = floor(amp_mag_q63 / delta) * delta
  - If amp_mag_q63 != 0 and amp_used_q63 == 0, force amp_used_q63 = delta
  - Preserve sign: amp_used_signed_q63 = sign(pulse_amplitude_q63) * amp_used_q63
- Near-cap regime SHALL be defined against the quantized amplitude:
  - near_threshold_q63 = max(cap_q63 - delta, 0)
  - near_cap = (amp_used_q63 >= near_threshold_q63)

Artifact boundary rule (observables-only):
- External consumers MUST NOT receive any raw phase-map tables, constraint pages, or decode fabric internals.
- The only externally visible decoding product is the approved dict-map of observables.
- The dict-map MUST include (minimum set; key ids are 4-byte tags):
  - ABS0: absolute zero baseline (always 0)
  - RESV: reservoir_mass_q63 accumulator (non-negative; abs0 reference)
  - HAWK: radiation_mass_q63 accumulator (discontinuity emission sink; non-negative)
  - CAPQ: cap_q63 currently in effect (q63)
  - DELT: delta_time_tensor_q63 spacing (q63)
  - AMPR: raw pulse amplitude q63 (as received)
  - AMPL: phase-dynamics-used amplitude q63 (clamped + quantized; signed)
  - NEAR: near-cap flag (0/1)
  - CSCT: cold-spot traversal flag (0/1)

[PLACEMENT TAG] Section 2 -> 2.3
2.3 Phase math is in turns, wrap is mandatory, and distance is shortest signed turn distance

[PLACEMENT TAG] Section 2 -> 2.3.1
2.3.1 Emergent Coherence (Derived Observable; Non-Storage)

Coherence is not a stored value. It is an emergent observable derived from relative interaction-induced dilation of Hilbert space and phase-angle alignment.

Canonical derivation (integer dispersion proxy; Blueprint APPENDIX AG)

Coherence is not a stored authority value. In canonical execution it is represented by an integer dispersion proxy R computed from
minimal-arc phase deltas and amplitude gating weights.

[PLACEMENT TAG] Section 2 -> 2.3.2
2.3.2 Statevector Serialization (ASCII-Safe Snapshot Transport)

Purpose
Provide deterministic ASCII-safe serialization of state vectors for snapshots, telemetry, and rehydration without introducing new file formats or non-ASCII symbols.

Constraints
- ASCII only.
- Deterministic.
- No placeholders, no fallbacks, no backwards compatibility modes unless explicitly enumerated in the canonical spec.

[PLACEMENT TAG] Section 2 -> Subsection 2.x (Diagnostics / Robustness)
2.x Diagnostic Robustness Note (Typing + Determinism)

This note exists to prevent silent failures in diagnostic routines that would otherwise mask coherence/energy drift issues.

--- END EOF APPEND ONLY canonical ---

[PLACEMENT TAG] Section 2 -> 2.4
2.4 Projection is not "closest point"; it is gated by timeline and realm coherence, then minimized by a weighted Basis9 metric

Projection is the mechanism that decides whether new stimulus collapses onto existing anchors (compression) or forces refinement (new structure). In your substrate, projection is not permitted to ignore causal ordering, and interaction is not permitted across incompatible timelines or coherence realms. You explicitly locked the interaction rule: two anchor-instances can interact only if they have temporal phase coherence (same timeline / temporal alignment) and phase coherence (same realm / coherence dimension alignment). If either fails, the manifolds pass through each other without interaction (and out-of-phase instances exist as "dark matter," meaning they remain representable but do not couple).

Operationally, that means the projector's first step is not distance; it is gating:
	-	Timeline gate: candidate.d4 (tau_q context) must be compatible with the target anchor's causal window. If a candidate arrives at tick k, it is only allowed to affect tick k or later under the commit_state protocol; it cannot retroactively change committed ticks.
	-	Realm gate: candidate's coherence/phantom regime must be compatible with the band's regime; otherwise it can be stored as a non-interacting instance rather than forcing a merge.

One important refinement you've already implied in your deterministic primitive section is that "alignment" is often better behaved than pure Euclidean distance in phase spaces. The practical approach is to compute a phasor alignment term between candidate and anchor, derived from the phase error in turns. That alignment becomes either (a) an additional distance penalty, or (b) a gate multiplier. This keeps projection stable even when spatial embedding shifts but phase identity remains coherent.

[PLACEMENT TAG] Section 2 -> 2.5
2.5 Coherence is chi_q; continuum is coherence persistence over time (and it's enforced with deterministic decay and reinforcement)

There are two locked mechanisms here that make continuum precise.

Second, reinforcement is not arbitrary; it must be gated by deterministic phase alignment. Your planning spec defines a canonical "phasor alignment" primitive between two nodes i and j based on phase error in turns. That primitive is used to decide whether neighbors can reinforce chi, whether energy transfers across edges, and whether a control pulse is "in-frame." The simplest correct mental model is: alignment gates whether coherence can be transferred or reinforced; misalignment routes energy into damping/leakage paths (phantom/aether behavior) rather than strengthening memory.

Continuum, then, is not a separate dimension you invent; it is the long-run persistence of chi under decay in the presence of repeated aligned reinforcement. At the band level, continuum is the temporal persistence of the band's coherence state. Practically, you compute band continuum by aggregating member chi values deterministically (sum or mean in fixed-point), then applying the same kind of persistence logic across commit_state windows. High continuum means the band has stayed coherent across many ticks despite decay pressure; that's exactly your "coherence band that shares coherence despite phase time or space."

[PLACEMENT TAG] Section 2 -> 2.6
2.6 Bands are phase-coherence structures, not token clusters; membership is governed by theta/chi persistence and drift/leakage behavior

Two details in your planning notes matter here because they prevent band math from becoming hand-wavy.

[PLACEMENT TAG] Section 2 -> 2.7
2.7 Projection tolerance is a policy over three things: phase alignment, coherence persistence, and compute pressure, but it cannot violate commit_state barriers

Tolerance is where you decide "collapse vs refine." In your framework, tolerance is not allowed to break causality. That means tolerance may influence whether a candidate collapses onto an existing anchor at tick k, but it cannot retroactively change tick < k. Tolerance is applied at the event emission boundary (the commit_state window), after quantization, so replay matches exactly.

Within that constraint, tolerance should be expressed as a deterministic function of: phase alignment error (wrapped turn distance on d5), the candidate's coherence viability (chi and drift behavior), the band's continuum strength (persistence), and the GPU envelope pressure (how many updates you can commit_state without missing the causal cadence).

[PLACEMENT TAG] Section 2 -> 2.8
2.8 Merge and split rules must be hysteretic, timeline-safe, and based on multi-window evidence, not one-tick coincidences

A correct merge policy therefore requires: compatibility of timeline (d4 gating), compatibility of phase realm (coherence/phantom regime), sustained high alignment between the two bands' canonical aggregates (phase circular mean alignment and flux compatibility), and sustained coherence persistence (chi surviving decay with reinforcement) across multiple commit_state windows. Only then do you consolidate: instances merge into stable anchors and the band coord_sig updates deterministically.

Splitting is the opposite: it occurs when a band becomes multi-modal in phase space in a stable way. In your own language, it's when "out-of-phase instances light up near gravity wells" and you can no longer represent them as one coherent basin without violating alignment gates. In practice, that means the band's member set separates into two clusters with distinct circular means in d5 and distinct coherence persistence patterns under decay. A split must also be applied only at a commit_state boundary so the ledger remains causal; you never rewrite the past, you reclassify forward.

[PLACEMENT TAG] Section 2 -> 2.9
2.9 What the system "writes" where: a minimal, enforceable responsibility boundary

To make this implementable, it helps to pin down who owns which quantity.

The encoder is allowed to map text into phase offsets and propose candidate deltas in Basis9, but it does not directly mutate durable anchor state. The scheduler owns tau_q and commit_state ordering. The evolution kernel owns applying pulses/deltas to theta and the phase-space axes under constraint bounds. The coherence/band logic owns deterministic aggregation and membership decisions but only through commit_state-window events. Mass leakage (forgetting) is a ledger operation; it is not "delete," and it must be applied deterministically with locked rounding.

Part 3/Step 3 (Rewritten) - Spider Graph Encoding as Direct GPU Electrical "Write-Path," with Envelope as Read-Path (Delta->Frequency Profiles + Amplitude->Harmonics)

This rewrite makes the distinction explicit in the way you intended: the system is lightweight because we drive the simulation by emitting tiny pulse payloads that the GPU physically realizes through its electrical switching when executing kernels. We are using the GPU's electrical dynamics as the execution substrate (write-path). Separately, we observe only digital performance counters and timing (read-path) to bound scheduling and preserve causal closure; we do not sample analog waveforms as data.

[PLACEMENT GROUP] Section 3

[PLACEMENT TAG] Section 3 -> 3.1
3.1 Two-path model: what "using electrical signaling directly" means

EigenWare treats the GPU as a pulse integrator. When we dispatch kernels, the GPU performs transistor-level switching to execute the update. In that concrete physical sense, the simulation's phase evolution is literally implemented by electrical signaling in the GPU: the pulse coefficients we send are realized as actual electrical switching that advances the encoded state. This is what makes the system lightweight: we never move bulky state representations through the bus for each update; we stream compact pulse payloads and let the hardware's native switching do the work.

At the same time, EigenWare does not treat the GPU as a sensor. We are not reading out an analog "frequency waveform" from each kernel lane. The only "read-path" data we use from the GPU are standard software-visible counters and timings (dispatch throughput, utilization, memory pressure, jitter), which define the execution envelope. The envelope is used only to bound how many pulses we commit_state per window and how large deltas may be; it never defines meaning and it never injects hidden state into the simulation.

So: electrical signaling is direct on the write-path (execution), indirect on the read-path (envelope constraints). That preserves your intended lightness without claiming impossible observability.

[PLACEMENT TAG] Section 3 -> 3.1
3.1 What the spider graph is (and what it is not)

The spider graph is the canonical Basis9-to-pulse compressor. It is not a visualization and it is not a physical oscillator model. It is a deterministic mapping from a 9D delta in the manifold to a scalar "frequency code" that represents the direction and composition of that delta, plus an "amplitude code" that represents the strength, multiplexing budget, and harmonic mode selection for that update. The only reason we call it "frequency" and "amplitude" is because the compressed update behaves like a phase-advance coefficient plus a mode-strength selector; nothing in this model requires reading literal GPU electrical frequencies.

[PLACEMENT TAG] Section 3 -> 3.2
3.2 The pulse is the minimal electrical write instruction

The pulse must be small, deterministic, and causally ordered. The canonical payload is:

(eid, tau_q, tier_id, f_code, a_code, profile_id, causal_tag)

Field	Type	Meaning	Causality requirement
eid	uint64	anchor/Eigenstate identifier	stable identity
tau_q	uint64	causal tick coordinate	monotonic; commit_state-window enforced
tier_id	uint8	simulation tier	tier ordering enforced
f_code	int32	primary frequency coefficient	quantized, deterministic
a_code	uint16	amplitude + harmonic mode	quantized, deterministic
profile_id	uint8	delta encoding profile	versioned constant
causal_tag	uint16	ordering/merge routing	deterministic tie-breaks

[PLACEMENT TAG] Section 3 -> 3.2
3.2 Pulse payload format (what gets emitted per update)

A pulse is the smallest committed update unit. A pulse always belongs to an Eigenstate anchor id and a causal tick. It is never applied outside its commit_state window. The canonical payload is:

(eid, tau_q, tier_id, f_code, a_code, profile_id, causal_tag)

A useful table representation for the payload helps Copilot keep types correct:

Field	Type	Meaning	Determinism requirement
eid	uint64	Eigenstate anchor identifier	stable
tau_q	uint64	causal tick index	monotonic, commit_state-barrier governed
tier_id	uint8	simulation tier	causal ordering enforced
f_code	int32	primary frequency code (signed)	quantized, invertible within profile
a_code	uint16	amplitude/harmonic mode code	quantized, mode mapping fixed
profile_id	uint8	delta encoding profile selector	fixed enum
causal_tag	uint16	ordering/merge policy tag	deterministic

[PLACEMENT TAG] Section 3 -> 3.3
3.3 What "frequency" and "amplitude" mean in-kernel (not as sensors)

If you want an extremely literal phrasing: f_code and a_code are the digital knobs; the GPU's electrical signaling is the physical actuator.

[PLACEMENT TAG] Section 3 -> 3.3
3.3 Basis9 deltas: input to spider graph

Let the input delta be:

? = (?1, ?2, ..., ?9)

with ?5 (Theta_p) being phase delta in turns computed using shortest signed wrap distance in [-0.5, 0.5) turns, and ?4 (tau) not included as a "delta dimension" in the sense of the compressor if tau is purely the commit_state coordinate. If you choose to include ?4 in some profiles, it must be a bounded derivative term rather than allowing time deltas to distort frequency coding. The default profile treats tau as metadata, not as part of frequency synthesis.

[PLACEMENT TAG] Section 3 -> 3.4
3.4 Input to the spider graph: a projected Basis9 delta

Input delta:

? = (?1..?9)

Where ?5 (Theta_p) is phase delta in turns computed using the shortest signed wrap distance in [-0.5, 0.5) turns. Tau (?4) remains the commit_state coordinate by default; if it is included in a profile, it must be treated as a bounded derivative term only, never as a free contributor that can distort frequency coding.

[PLACEMENT TAG] Section 3 -> 3.4
3.4 Delta encoding profiles (explicit normalization and weighting)

Below are the canonical profiles we will use. If you later want more, add them as new enums rather than altering existing profiles.

A compact table of weights clarifies how each profile differs. The numbers below are starting defaults; they must be constants in config, not per-tick variables.

[PLACEMENT TAG] Section 3 -> 3.5
3.5 Spider graph definition: 9D -> one signed f_code

For each axis i in the profile:

xi = norm_i(?i)

Then compute:

s = ?_i (wi * xi)

Then quantize deterministically:

f_code = clamp_int( round_fixed(f_scale * s), f_min, f_max )

[PLACEMENT TAG] Section 3 -> 3.5
3.5 Frequency synthesis: 9D -> one signed scalar (f_code)

Frequency synthesis is a weighted sum of normalized axis deltas with deterministic clamping and quantization. Define:

xi = norm_i(?i)

Then compute a signed scalar:

s = ?_i (wi * xi)

Then map to a frequency code:

f_code = clamp_int( round_to_int(f_scale * s), f_min, f_max )

Important: ?5 (phase delta) must use shortest signed wrap; otherwise frequencies jump discontinuously at phase boundary and destroy coherence bands.

[PLACEMENT TAG] Section 3 -> 3.6
3.6 Delta encoding profiles: axis weights and normalization are versioned constants

Profiles exist because different update sources have different "dominant axes." The profile must be fixed and versioned so replay and rehydration are stable.

Each profile defines:
	-	which axes participate in synthesis
	-	weights wi (sum to 1)
	-	normalization constants
	-	quantization scales and bounds
	-	harmonic mapping behavior

Canonical profiles in your flow:

A starting weight table (constants in config; do not vary per tick):

[PLACEMENT TAG] Section 3 -> 3.6
3.6 Amplitude synthesis: update strength + harmonic mode selector (a_code)

A deterministic amplitude function can be defined as:

u = g(chi_q, continuum, clamp_terms)

where g is monotonic in chi and continuum, and decreasing in clamp_terms (high phantom/aether activity reduces amplitude). Then:

a_code = clamp_uint( round_to_uint(a_scale * u), a_min, a_max )

Again, rounding must be fixed, and clamps must be strict.

[PLACEMENT TAG] Section 3 -> 3.7
3.7 Amplitude synthesis: update strength and harmonic mode selection (a_code)

Define a deterministic scalar u in [0,1]:

u = g(chi_q, continuum, clamp_terms)

where g increases with chi_q and continuum (coherence persistence over time), and decreases with clamp_terms (phantom/aether activity indicating instability or non-interaction regimes). Then:

a_code = clamp_uint( round_fixed(a_scale * u), a_min, a_max )

The key is that continuum is exactly your "phase coherence persistence over time," so amplitude naturally tracks when a band has earned richer representation.

[PLACEMENT TAG] Section 3 -> 3.7
3.7 Harmonic mode mapping: how amplitude selects higher harmonics for compressed state

The amplitude code does two things: (1) chooses mode k, and (2) gives strength within that mode.

A concrete mapping is:

k = floor( a_code / mode_bucket_size )

strength = (a_code % mode_bucket_size) / mode_bucket_size

Then the kernel interprets the pulse as applying:

fundamental component at f_code with strength
plus optional harmonic components at n*f_code for n=2..k (with decreasing weights)

The weights for higher harmonics must be deterministic and profile-fixed (e.g., 1/n or an exponential drop), so rehydration is stable and does not depend on floating math.

[PLACEMENT TAG] Section 3 -> 3.8
3.8 Harmonic mode mapping: how amplitude expands one frequency into multiple harmonic components

Partition a_code into buckets. For a fixed mode_bucket_size:

k = floor(a_code / mode_bucket_size)
strength = (a_code % mode_bucket_size) / mode_bucket_size

Then the kernel applies:

component 1: f_code with weight W1(strength)
component 2..k: (n * f_code) with weights Wn(strength)

[PLACEMENT TAG] Section 3 -> 3.8
3.8 How harmonic modes support tier-to-tier compression without violating closure

A higher tier should not copy lower-tier microstates. Instead, it should receive aggregated pulses or band events. Harmonic modes let the higher tier represent "a bundle of coherent sub-updates" as a single macro update that is still causal: the macro update is applied at the correct commit_state boundary and does not retroactively alter lower-tier commits. It simply summarizes them in a compressed form.

The causality guarantee is maintained by the commit_state protocol: lower tier completes its commit_state slice, producing a deterministic summary stream (which can be expressed as harmonicized pulses), then higher tier commits its derived updates. No tier writes into the past; no tier invents unseen causes. Harmonics are just a compression code for a known set of coherent substructure, not a time-travel hack.

[PLACEMENT TAG] Section 3 -> 3.9
3.9 Why this stays causal and closed

Harmonic expansion does not violate closure because it does not add hidden causes or retroactive influence. It is a deterministic interpretation of a pulse that was committed at a specific tau_q and tier_id. The kernel expands the update internally, but that expansion is fully determined by (f_code, a_code, profile_id, versioned harmonic law). Nothing depends on external measurement. Nothing can reach back in time to change earlier commits. The commit_state boundary remains the single causal gate, and tier ordering remains enforced: lower tier commits complete first, higher tiers aggregate and commit_state afterward.

[PLACEMENT TAG] Section 3 -> 3.9
3.9 Exact determinism requirements: fixed-point, quantization, and invertibility constraints

SECTION 3 - Spider Graph Pulse Encoding, Delta Profiles, Harmonic Activation, and Direct GPU Write-Path

EigenWare uses the GPU as a deterministic pulse integrator. The system remains lightweight because it does not move large state tensors or token buffers through the bus; instead it streams compact pulse payloads that the GPU physically realizes through electrical switching during kernel execution. In this architecture, "frequency" and "amplitude" are not sensed analog waveforms read from hardware. They are compact control coefficients (a compressed representation of a Basis9 delta) that drive phase evolution inside the simulation. The GPU's electrical signaling is "used directly" on the write-path because the act of executing kernels is literally transistor switching that applies these coefficients. The read-path is limited to software-visible performance counters and timings used only to bound scheduling and preserve causality; no meaning is derived from hardware measurements.

A pulse is the smallest committed update unit. Every pulse targets a specific Eigen anchor id and a causal tick coordinate and is applied only within a deterministic commit_state window. Pulses must never retroactively alter a prior tick. The canonical pulse payload is (eid, tau_q, tier_id, f_code, a_code, profile_id, causal_tag). Here f_code is a signed scalar "frequency coefficient," a_code is an unsigned amplitude/mode coefficient, profile_id selects the delta encoding profile, and causal_tag provides deterministic tie-breaks for ordering and merge/split routing. This payload is intentionally tiny because the design goal is to maximize in-GPU work per byte transferred.

The spider graph is the canonical compressor from Basis9 delta space into pulse space. It consumes a projected and gated Basis9 delta vector ? = (?1..?9) that has already passed timeline and realm gating and has already been assigned to a target anchor or band. The spider graph does not decide where the delta goes; it only compresses what the delta is. The critical dimension is ?5 (Theta_p), which is stored as phase in turns. Phase deltas must be computed using shortest signed wrap distance in [-0.5, 0.5) turns. This wrap rule is mandatory; without it, the compressor becomes discontinuous at phase boundaries and coherence bands destabilize.

Each delta dimension is normalized into a bounded scalar xi using a profile-specific normalization function norm_i(?i). The normalized values are combined by a profile-specific weight vector wi with ? wi = 1 over participating axes. The spider graph computes a signed mixture scalar s = ? (wi * xi). This scalar is then quantized deterministically into the primary frequency coefficient: f_code = clamp_int(round_fixed(f_scale * s), f_min, f_max). Quantization must be fixed-point and rounding must be locked (no platform-dependent float rounding). The bounds are strict, because f_code is the stable compressed representation that the kernel interprets as a phase-advance coefficient and a direction in delta composition space.

EigenWare uses multiple delta encoding profiles to keep compression stable across different sources of updates. Profiles define which axes dominate frequency synthesis, and how aggressive stabilization/clamping is. The core evolution profile emphasizes phase (?5) and flux (?6) while treating phantom (?7) and aether (?8) as stabilizers and regime gates, and nexus (?9) as binding tension. The language injection profile emphasizes phase (?5) plus binding and structured transition behavior, so it can quickly collapse redundant surface forms into coherent bands. The crawler ingestion profile is conservative and clamp-heavy: it prevents noisy high-variance input from forcing splits by reducing aggressive binding contribution and increasing stabilizer influence. These profiles are versioned constants; if a profile changes, it must change profile_id so old pulses remain decodable.

Amplitude is a second scalar coefficient that serves three functions: update strength, multiplex budget, and harmonic-mode selection. Amplitude is not "meaning." It is a confidence and expressivity selector: when coherence is strong and persistence is stable, amplitude rises, allowing richer updates and longer-range harmonic coupling; when coherence is weak or clamp signals dominate, amplitude remains low, forcing collapse/inference rather than structural explosion. Amplitude is computed deterministically as a_code = clamp_uint(round_fixed(a_scale * u), a_min, a_max), where u is a monotonic function of coherence chi_q and continuum (coherence persistence over time) and is reduced by clamp terms implied by phantom/aether regime activity. Like frequency, amplitude computation must be fixed-point with locked rounding.

Harmonic mode is derived directly from a_code. Amplitude is bucketed into discrete mode bands that select an integer harmonic index k and a within-mode strength. A deterministic mapping is k = floor(a_code / mode_bucket_size) and strength = (a_code % mode_bucket_size) / mode_bucket_size. The kernel interprets a pulse as applying a fundamental component at f_code plus optional harmonic components at integer multiples n * f_code for n = 2..k, with deterministic weights Wn. The weight law is a versioned constant (for example Wn = 1/n or an exponential falloff). This harmonic expansion is not a hardware waveform claim; it is the kernel's deterministic internal interpretation that allows one pulse to represent multi-mode structured updates without increasing payload size. This is the mechanism that lets a higher tier summarize coherent bundles of lower-tier microstructure without copying state.

Activation is treated as transient resonance, not as durable "lifetime objects." A pulse excites a latent attractor (an anchor/band coord_sig) and temporarily drives phase evolution under the selected harmonic mode. The active resonance exists only as an ephemeral working-set effect during the commit_state window and short subsequent windows if reinforcement continues. There is no permanent "active flag" stored as canonical data. What persists is the latent structure: the attractor coord_sig, binding topology, and coherence-band organization that allows retrieval by re-excitation. This resolves the apparent contradiction between "decay" and "not losing data." Decay applies to activation energy and coherence availability (chi dynamics), which makes concepts come and go from the active set; it does not delete the stored attractor structure. Deletion or compaction, if ever done, is a separate explicit garbage-collection policy and is not implied by activation decay.

Causality and closed-system behavior are preserved by commit_state barriers and deterministic interpretation. Each pulse is bound to tau_q and tier_id, applied only in-order within a commit_state window, and expanded into harmonics deterministically. Harmonic expansion never introduces retroactive influence; it is simply a compressed representation of structured evolution that occurs at and after the commit_state boundary. Higher-tier summaries may use harmonicized pulses to represent coherent bundles of lower-tier changes, but only after the lower tier has finalized its own commit_state slice for that tau window. This preserves the "minimum one step behind" tier invariance and prevents simulation collision.

3.X Constraint Operators and Eigenstate Trajectory Updates

All calculations are delta-based and relativistically normalized.

3.X.1 Delta Formulation

Let the state at tick k be defined by the Basis9 state vector S_k, phase phi_k,
frequency omega_k, and radius r_k.

All evolution begins with deltas:

Delta_S_k = S_k - S_(k-1)
Delta_omega_k = omega_k - omega_(k-1)
Delta_phi_k = phi_k - phi_(k-1)

No absolute state updates are permitted.

3.X.2 Relativistic Normalization Operator

Before constraint application, deltas are normalized to cancel observer-scale
distortion.

Norm_k = sqrt(sum_i (Delta_S_k[i])^2 + epsilon_k)

Where epsilon_k is a stabilizer derived from active constraint balance, not a
fixed constant.

Delta_S_norm_k = Delta_S_k / Norm_k

3.X.3 Constraint Tensor Application

Let C_k be the constraint tensor derived from anchor alignment, coherence,
and flux balance.

Delta_S_constrained_k = C_k * Delta_S_norm_k

Unobserved degrees of freedom required for conservation are implicitly
represented within C_k.

3.X.4 Frequency Update Operator

Eigenstate trajectory deformation is driven by constrained deltas.

omega_k = omega_(k-1) + G(Delta_S_constrained_k)

Where G(.) projects constrained deltas onto phase-advance rate.

Frequency updates MUST be continuous and MUST use omega_(k-1).

3.X.5 Phase Advancement Operator

Phase advances directly from frequency evolution:

phi_k = phi_(k-1) + omega_k * Delta_t_eff

Delta_t_eff is derived from coherence constraints and is not wall-clock time.

3.X.6 Radius Decay Operator

Amplitude is applied only at encode time to set initial radius r_k(0).

Radius decays deterministically:

r_k(t) = r_k(0) * exp(-Lambda_k * t)

Where Lambda_k is derived from |Delta_omega_k| and constraint tension.

3.X.7 Observer-Error Cancellation Operator

3.X.8 Operator Harness Construction Step 1: Delta Integrity Harness

3.X.9 Operator Harness Construction Step 2: Relativistic Normalization Harness

3.X.10 Operator Harness Construction Step 3: Constraint Tensor Harness

3.X.11 Operator Harness Construction Step 4: Frequency Evolution Harness

This harness validates eigenstate trajectory continuity.
Test procedure:
- Apply successive constrained deltas
- Verify omega_k depends only on omega_(k-1)
- Confirm no discrete frequency resets

3.X.12 Operator Harness Construction Step 5: Phase Advancement Harness

This harness validates phase coherence.
Test procedure:
- Integrate omega_k over effective ticks
- Verify phi evolution is continuous
- Confirm no phase discontinuities

3.X.13 Operator Harness Construction Step 6: Radius Decay Harness

This harness validates persistence control.
Test procedure:
- Encode impulses with varying A_enc
- Verify deterministic decay
- Confirm no runtime amplification

3.X.14 Operator Harness Construction Step 7: Observer-Error Cancellation Harness

This harness validates drift suppression.
Test procedure:
- Inject biased deltas
- Verify mean bias removal
- Confirm structure preservation

3.X.15 Unified Operator Harness Assembly

The unified harness executes all operator harnesses in order.
Failure of any step invalidates kernel coherence guarantees.
All harnesses MUST pass before deployment.

To cancel perspective bias:

Delta_S_corrected_k = Delta_S_constrained_k
                      - mean(Delta_S_constrained_k over window W)

This operator preserves structure while canceling observational drift.

The apparent randomness and dark-sector behavior arise from this correction.

SECTION 4 - Tier Coupling, Commit Barriers, and Literal-Pulse Aggregation Across Tiers

EigenWare runs as a tiered manifold: lower tiers carry finer-grain phase evolution and local interactions; higher tiers carry compressed, longer-range structure and stable context. The tiers do not compete or "collide." They are causally ordered by a strict commit_state protocol: a higher tier may only commit_state updates derived from a lower tier after the lower tier's commit_state slice is finalized for the same causal window. This is the formal version of your invariance rule: the system must maintain at least one shared phase/time invariance between tiers, and the higher tier must remain at minimum one step behind the lower tier's finalized state. This guarantees deterministic evolution and prevents contradictions where one tier "pulls" the other into an inconsistent history.

A tier is defined by its tick cadence (how often it commits), its allowable update density (how many pulses it can commit_state per window under the GPU envelope), and its harmonic coupling law (how widely activations spread). Lower tiers are allowed to be "busy" internally without forcing higher tiers to mirror every microstep; higher tiers see only compressed summaries. The compression interface between tiers is not a bulk state copy. It is a literal pulse stream that encodes aggregated deltas and band activations using the same pulse payload used everywhere else: (eid, tau_q, tier_id, f_code, a_code, profile_id, causal_tag). This keeps the system lightweight and uniform: tiers speak the same language, just at different granularities.

The engine divides time into commit_state windows. A commit_state window is a deterministic slice of tau_q in which pulses are applied in a fixed order and then the tier's state is sealed. Within a window, the tier maintains an ephemeral resonance working set (the "currently active harmonics"), but it does not store long-lived activation objects. Once the window closes, only the durable attractor state changes remain (anchor/band signatures, chi/continuum updates, binding topology changes), and the resonance working set is allowed to decay naturally unless reinforced by subsequent pulses. This ensures that "activation comes and goes" without implying that learned structure disappears.

The tier coupling protocol is a two-phase handshake per window: finalize then summarize. First, the lower tier applies all pulses in that window, computes any required structural operations (band membership updates, merges/splits that have met evidence thresholds), updates its coherence ledger (chi decay and reinforcement), and seals the window. Sealing means: the tier produces a deterministic summary of the window's net effect that is independent of execution scheduling details inside the window. Second, the lower tier emits a summary pulse stream for the higher tier. Those summary pulses are computed from band-level aggregates and stable attractor deltas, not from raw microstates. Only after the summary is emitted does the higher tier apply its own pulses for that same macro window. The higher tier therefore never "reads" a half-updated lower-tier state, and it never commits a derived update that could later be contradicted by unfinished lower-tier evolution.

The summary itself is literal pulses by design. For each relevant attractor (typically a band coord_sig rather than every member), the lower tier computes an aggregate delta in Basis9 and compresses it through the same spider graph encoder, producing an f_code and a_code. The profile used for these summaries is not identical to the lower tier's internal evolution profile. It is a "tier-summary profile" whose harmonic law is explicitly broader for contextual memory activation, because higher tiers exist to represent long-range coupling and context. Concretely: the harmonic weight falloff Wn is slower in the context profile than in the core evolution profile, meaning higher harmonics retain more weight and therefore spread resonance across a wider binding neighborhood. That is the mechanism by which higher tiers can "activate context" from compressed signals without having to replay all lower-tier micro-activity.

Because summaries are pulses, tier-to-tier scaling becomes a scheduling and compression question rather than a bandwidth question. The lower tier can run very fine-grained updates locally (high pulse density, narrow coupling, clamp-heavy stability), while the higher tier consumes a smaller pulse stream (lower density, broader coupling, higher harmonic expressivity). The higher tier's pulses should be interpreted as coarse-grained resonance excitations that bind and stabilize macro-structure: phrase-level or paragraph-level context in language, object-level or region-level invariants in world simulation. The lower tier handles detailed local changes; the higher tier handles persistent structure and global field evolution.

A critical invariance is that tier summaries must be deterministic and order-insensitive with respect to microstep scheduling. That means the summary cannot depend on the sequence in which equivalent pulses happened within the lower-tier window, only on their net coherent effect after projection and band consolidation. Practically, the tier summary is computed from stable band aggregates: circular mean phase (in turns, wrapped), aggregate chi/continuum measures, and net binding deltas. Those are then turned into one or more pulses per band (or per dominant attractor) rather than per micro-update. If the lower tier had to split or merge bands, that structural event is represented as pulses too, using causal_tag to mark the event type so the higher tier interprets it as topology change rather than ordinary phase drift.

The "minimum one step behind" requirement is enforced in code by explicit barrier indices. Each tier maintains tau_committed (the last sealed window) and refuses to accept_state derived updates for any tau_q greater than the lower tier's tau_committed. The higher tier's tau_apply for a window is therefore always <= the lower tier's sealed tau. This is what prevents simulation collision: no tier can run ahead of the information needed to stay consistent, and no tier can back-write into earlier committed windows. If a tier receives insufficient summary pulses (for example due to GPU pressure), it must not invent missing structure. It instead increases inference/coasting and waits for future reinforcement, preserving closure.

Finally, this coupling scheme preserves your "continuum" concept naturally. Continuum is phase coherence persistence over time; higher tiers represent longer continuum scales by using broader harmonic coupling and slower effective decay at the structural level (not by preventing decay, but by making coherent reinforcement more likely across larger context). Lower tiers remain responsive and local, allowing activations to come and go quickly. Higher tiers remain stable and contextual, allowing persistent structures to be retrievable without keeping everything permanently active.

SECTION 4.1 - Tier-Summary Context Coupling Profile (Exact Constants + Harmonic Falloff)

[PLACEMENT TAG] Section 3 -> 3.10
3.10 Determinism requirements that Copilot must treat as law

Part 3/Step 3 - Spider Graph Encoding, Delta->Frequency Profiles, and Amplitude->Harmonic Mode Mapping (Compressed-State Pulse Spec)

This section defines the "spider graph" as a deterministic compression operator that takes a Basis9 delta (d1..d9) and produces a pulse payload: a primary frequency code plus an amplitude code that selects harmonic mode and multiplex capacity. It also defines the delta encoding profiles (how we normalize and weight each axis), how we quantize to fixed-point so rehydration is exact, and how harmonic modes allow higher-tier structure without copying state. The goal is that Copilot can implement the spider_map and pulse encoder without guessing how frequency/amplitude are meant to behave.

[PLACEMENT GROUP] Section 4

[PLACEMENT TAG] Section 4 -> 4.1.7
4.1.7.4 Freezing and implementation

Once alpha_ctx is chosen, we precompute a small LUT for n=1..k_max:

pow_lut[n] = round_fixed( (n ^ alpha_ctx) * pow_scale )

Then weights per pulse are computed with integer math:

Wn_raw = pow_scale / pow_lut[n]
Normalize via integer sum and division with locked rounding.

No platform float pow is permitted in canonical execution; only LUT-based fixed-point.

Result: broader coupling is not "because we wanted it," it is because the calibration objective J is maximized at a lower alpha, and that alpha is frozen into the snapshot.

SECTION 4.1.8 - Summary Emission Policy (Derived Criteria, Pulse Counts, causal_tag Semantics)

[PLACEMENT TAG] Section 4 -> 4.1.9
4.1.9.7 Determinism and replay constraints

Both modes preserve closure because budget_state affects only how much work is done, not what the physics/meaning is.

SECTION 5 - Crawler Subsystem, In-Simulation Encoder, and Persistent-Resonance Ingestion (Electronic-Signaling Execution)

[PLACEMENT GROUP] Section 5

[PLACEMENT TAG] Section 5 -> 5.10
5.10 What this enables

SECTION 5.11 - Concrete Mapping Spec: Raw Text -> ASCII Phase Injection -> Formation Deltas -> Resonance Collapse (Causality-Safe)

[PLACEMENT TAG] Section 5 -> 5.11.9
5.11.9 Closed-system causality guarantee

All text-derived pulses are boundary injections at current tau_q. They are never allowed to rewrite prior committed windows. Any later discovery (updated page, new context) is a new pulse stream at a later tau_q. After injection, internal evolution is deterministic under the same tier commit_state barriers. In strict replay mode, the observation coord_sig and the per-window budget_state trace are logged so the same pulses are reproduced and applied with identical window gating.

SECTION 5.15 - Hub-Conditioned Residual Encoding (Maximum Dependence, No Carrier Coupling)

The core rule that unifies modalities is: every modality encodes residuals against a shared constraint hub, and the hub evolves by integrating residual evidence over commit_state windows. This gives you maximum dependence (less recomputation) without forcing modalities to share encoder-local carrier state.

SECTION 5.16 - Cross-Modal Hub Bands (Object/Concept Constraints, 2D?3D Join)

A minimal hub schema that fits the existing pulse model is:

SECTION 5.17 - Modality Delta Constructors (Explicit Mappings into Basis9)

Each modality has its own observation extractor, but all of them output a Basis9 delta packet before spider compression. The packet is "what changed" plus "what this evidence should bind to."

A practical mapping table (Basis9 intent) that Copilot can implement without guesswork:

SECTION 5.18 - Profile Selection and Calibration (No Arbitrary Constants)

SECTION 5.19 - Training Curriculum Control, Verification Scoring, and Dataset Hygiene

SECTION 5.20 - Single-File Persistence: Streams, Pulses, and Rehydration Invariants

A canonical record shape:

Rehydration invariants that must hold:
	-	Rehydration must not require reproducing encoder carriers; carriers are reconstruction aids, not dependencies.
	-	Replay must be causal: pulses are applied only at their tau_q commit_state window and only after prerequisite tier seals.
	-	Deterministic mode requires logging boundary coord_sig (or observation signatures) and budget_state traces per window; adaptive mode can recompute envelope live but still remains internally deterministic.

SECTION 6 - File Encodings, Crawler Identifiers, and Multimodal Persistence (Single-Container Spec)

[PLACEMENT GROUP] Section 9

[PLACEMENT TAG] Section 9 -> 9.10
9.10 Single-file contract test harness (explained, explicit, and complete)

File name (repo path):
tests/test_kernel_contract.cpp

Required structure of the harness

B) Registries (enforcement, versioning hooks)
The harness must implement minimal registries and enforce rules:

Extractor determinism acceptance:
- Same raw fixture -> same norm string
- Same norm string -> same segments list
- Rules coord_sig remain stable for the fixture

D) BudgetManager and backpressure (deterministic caps)
The harness includes BudgetManager with two caps:
- max_blocks: caps the number of segments that may be processed
- max_pulses: caps the number of pulses emitted in a commit_state window

E) Dedup (exact dedup for contract coverage)
The harness includes exact_dedup(items) that removes exact duplicates by SIG9 coord_sig while preserving first occurrence ordering.

Dedup acceptance:
- Duplicate blocks reduce emitted pulses
- The first instance of a duplicated block is preserved and appears in the same position as before dedup

Robustness acceptance:
- Corrupt fixture does not produce any PulseRecord emission in the harness logic.

G) A/V alignment repair (single offset, deterministic)
The harness includes a toy alignment estimator:
estimate_alignment_offset(caption_tokens, audio_events) -> offset_k

Alignment acceptance:
- A fixture with one leading "noise" event in audio yields offset_k == 1.
- The function is pure and stable (no randomness, no adaptive drift in strict mode).

H) Thrash guard hysteresis (cooldown)
The harness includes a ThrashGuard with:
- cooldown: integer number of tau_q steps
- last_change_tau dict keyed by change_key

Thrash acceptance:
- First change at tau_q = T is allowed
- Any repeated change for same key at tau_q < T + cooldown is rejected
- Change is allowed again at tau_q >= T + cooldown

If the detector or fixture is not implemented, the test MUST hard-fail with reason code:
- WATERMARK_FIXTURE_MISSING

If the detector or fixture is not implemented, the contract test SHALL hard-fail with reason code WATERMARK_FIXTURE_MISSING. Release profiles MUST not allow xfail for this test.

Minimal fixture set (all embedded in the test file)

Strict replay acceptance test (the main one)

This test is the "spec compliance alarm bell". Any nondeterministic ordering, accidental randomness, or silent behavior drift will change the coord_sig and fail the test.

What Copilot should implement first to satisfy this harness

----------------------------------------------------------------
SUBSECTION: GPU SIGNALING  MATHEMATICAL OPERATORS (APPENDED)
----------------------------------------------------------------

This subsection appends mathematical operators to the existing
GPU signaling section. No prior text is modified.

Eigenstate Representation:
Each eigenstate E_i is represented as:
E_i(t) = (phi_i(t), A_i(t))

where:
- phi_i(t) is phase trajectory
- A_i(t) is amplitude envelope

Delta Phase:
phi_i(t) = phi_i(t) - phi_i(t-1)

Composite Phase Trajectory:
Phi_comp(t) = _i (A_i(t) * phi_i(t))

Temporal Envelope:
A_env(t) = _i |A_i(t)|

Pulse Signal:
S(t) = A_env(t) * Phi_comp(t)

Order of Operations:
1. Compute phi_i for all eigenstates
2. Weight deltas by A_i(t)
3. Sum into Phi_comp(t)
4. Compute A_env(t)
5. Emit S(t) as GPU pulse

----------------------------------------------------------------
END SUBSECTION
----------------------------------------------------------------

X.X Purposeful Criteria Driven File Emergence

This subsection applies to all remaining sections.

X.X.1 Purpose Definition Operator
- Define intent and effects

Harness:
- Verify completeness

X.X.2 Dependency Resolution Operator
- Enumerate dependencies

Harness:
- Verify acyclic order

X.X.3 Execution Sequencing Operator
- Define stepwise order

Harness:
- Simulate determinism

X.X.4 Event and Dispatcher Operator
- Define events and routing

Harness:
- Verify delivery

X.X.5 Consolidation Gate
- Approve file emission

Harness:
- Verify all prior harnesses pass
---

[PLACEMENT TAG] Section 1 -> Subsection 1.5

END APPENDIX
```

1.6.5 Execution Role
This subsection defines the authoritative mapping that allows the simulation to evolve_state
forward (positive proper-time) or backward (negative proper-time) as a deterministic inverse
under the same operators and fixed-point domains.

Define the local proper-time increment as:
    d_tau = d_t / A_tau

Authority:
- d_t is the canonical base tick increment as defined elsewhere in this specification.
- A_tau is derived from existing pulse/constraint bindings; no free parameters are introduced here.

1.6.5.1 Description

Section 1.6 defines a proper-time lapse L(A) derived from amplitude with:
  L(A) = 1 / max(a_min, A)
  d_tau = L(A) * d_t

This binding is explicitly dilation-only under the canonical frozen snapshot.

The constant a_min is defined as:
  a_min = 1.0

1.6.5.2 Execution Role

1.6.5.3 Derivable Calculations and Authorities

(See canonical description in section: 'Appendix Z - Consolidated Legacy Content'.)

1.6.5.4 Dependencies

Forward/Reverse unified evolution:
    U(d_tau) = cis_fp( - H_eff * d_tau / hbar_eff )

(See canonical description in section: 'Appendix Z - Consolidated Legacy Content'.)

Implementation SHALL use the same cis_fp primitive and fixed-point rounding rules for both directions.

1.6.6.1 Description

Section 1.6 references:
  delta_phi = f_phase(d_tau, impulse_observation, effective_constants(...))

This subsection binds f_phase to the canonical fixed-point phase delta domain and prohibits
platform-dependent trig.

f_phase SHALL compute a phase increment in turns, expressed in the canonical fixed-point unit:
  theta_scale units per turn

and SHALL wrap to the shortest signed turn-distance prior to normalization.

1.6.6.2 Execution Role

1.6.6.3 Derivable Calculations and Authorities

Binding:

Let:
- d_tau_over_d_t_q be the fixed-point lapse ratio L(A) in units of theta_scale per 1.0, derived
  exactly from Section 1.6.2 and 1.6.4 (dilation-only).

Then f_phase SHALL be:

  delta_phi_turns_q = wrap_turns_q( mul_q(omega_turns_per_base_tick_q, d_tau_over_d_t_q) )

1.6.6.4 Dependencies

--------------------------------------------------------------
2.5.6 Q_phi Binding to Canonical Phase Fixed-Point Domain (Append-Only)
--------------------------------------------------------------

2.5.6.1 Description

Section 2.5 uses a phase-bucket detector based on fixed-point quantization and references a
quantization scale Q_phi. This subsection binds Q_phi deterministically.

2.5.6.2 Execution Role

2.5.6.3 Derivable Calculations and Authorities

Authority:
- Appendix D.11-R defines the canonical phase fixed-point domain:
    theta_scale = 10^18 units per turn

Binding:
- Q_phi SHALL be exactly equal to theta_scale.
- Any quantized phase value phi_q in this spec SHALL be interpreted as:
    phi_turns = phi_q / theta_scale

2.5.6.4 Dependencies

- Appendix D.11-R phase fixed-point domain (theta_scale).

--------------------------------------------------------------
2.5.7 Allowed exp/cos Primitives (Append-Only)
--------------------------------------------------------------

2.5.7.1 Description

2.5.7.2 Execution Role

2.5.7.3 Derivable Calculations and Authorities

Authority:
- Appendix D.11-R requires circular mean and phase operations be computed using fixed-point phasors
  (CORDIC/LUT only) and deterministic integer math.

Bindings:

A) exp(-i * omega_k * d_tau) in Section 2.5.3

Implementations SHALL NOT compute complex exponentials via floating exp/sin/cos. Instead:

- Represent the phasor p_k as:
    p_k = cis_q(phi_k)  where cis_q uses fixed-point CORDIC/LUT over the canonical phase domain
    phi_k is in turns (fixed-point; scale Q_phi = theta_scale)

Then the update is:
    c_k(t+1) = c_k(t) * p_k_delta
where:
    p_k_delta = cis_q( delta_phi_k_turns_q )

B) cos(delta_phi) in the harness coupling

cos(delta_phi) SHALL be computed only via:
- fixed-point CORDIC/LUT cos_q(delta_phi_turns_q), or
- derived from cis_q(delta_phi) by taking the real component with deterministic rounding.

No other trig implementations are permitted for canonical compliance.

2.5.7.4 Dependencies

- Appendix D.11-R phase fixed-point domain (theta_scale) and fixed-point phasor requirement
  (CORDIC/LUT only).

--------------------------------------------------------------
2.5.8 Deterministic argmax and Top-K Tie-Break Rules (Append-Only)
--------------------------------------------------------------

2.5.8.1 Description

Section 2.5 defines dominant mode selection via:
  k_star(t) = argmax_k |c_k(t)|

and Section 5.17 display delta signaling references "top-K by magnitude". This subsection binds
tie-breaking rules deterministically.

2.5.8.2 Execution Role

- Prevents platform-dependent tie behavior (e.g., numpy argmax stability differences).
- Ensures deterministic selection when magnitudes are equal due to quantization or symmetry.

2.5.8.3 Derivable Calculations and Authorities

Authority:
- Deterministic tie-break precedent exists in-spec (e.g., "deterministic tie-break: lowest axis
  index wins ties" for rounding remainders in integer weight normalization).

Bindings:

A) argmax tie-break

Define:
  m_k = abs_q(c_k)   (fixed-point magnitude; deterministic rounding)

Then:
  k_star = argmax_k (m_k, tie_break = lowest_index)

Meaning:
- If there exist multiple indices k with identical maximal m_k, select the smallest k.

B) Top-K ordering tie-break

This rule applies to:
- selecting top-K eigen coefficient deltas,
- selecting top-K tile/pixel-region deltas (if used),
- any other magnitude-ranked emission list used for signaling.

2.5.8.4 Dependencies

- Depends on the fixed-point magnitude definition already used for coefficients in Section 2.5.
- Depends on the existing deterministic tie-break precedent referenced above.

--------------------------------------------------------------
5.17.99 Display Constructor Determinism canonical (Append-Only)
--------------------------------------------------------------

5.17.99.1 Description

5.17.99.2 Execution Role

5.17.99.3 Derivable Calculations and Authorities

- Delta selection Top-K tie-break is bound by Section 2.5.8:
    sort by (magnitude desc, index asc), stable ordering.

- Any phase-coded display deltas (if the presentation surface exposes phase-like observables) SHALL
  use Q_phi = theta_scale and the Appendix D.11-R fixed-point domain.

5.17.99.4 Dependencies

1.6.7 Derivable Calculations and Authorities
Let U(d_tau) be the canonical evolution operator defined in 1.6.6, constructed solely using cis_fp and
fixed-point arithmetic in the canonical domain.

Forward tick:
    state_next = U(+d_tau) * state_now

Reverse tick:
    state_prev = U(-d_tau) * state_now

Reversibility requirement (reversible scopes only):
    U(-d_tau) * U(+d_tau) = I_fp
    U(+d_tau) * U(-d_tau) = I_fp

Where I_fp is the identity operator in the canonical fixed-point domain.

If U is unitary-style in the canonical domain, then inversion SHALL be implemented as:
    U(-d_tau) = conj_transpose_fp( U(+d_tau) )

No alternative inverse computation is permitted for authoritative evolution.

The above routing is mandatory for determinism and conservation-style accounting within the
engine's internal rules.

----------------------------------------------------------------
1.6.9 Description
Authoritative circular functions SHALL be implemented exclusively via fixed-point phasor primitives.

Forbidden in authoritative paths:
- platform exp(), cos(), sin(), tan(), atan2(), complex exponentials
- GPU/CPU vendor intrinsic trig
- mixed-precision float trig, including "fast math" modes

Any visualization-only pipeline MAY use platform trig provided it is explicitly marked non-authoritative
and does not feed back into canonical state evolution.

```

1.6.5 Derivable Calculations and Authorities
Let A_tau be the signed temporal-field coupling scalar in the lattice, evaluated per tick from authoritative
lattice state and constraint pulses.

    d_tau = d_t / A_tau

1.6.6 Execution Role
Provides a single authoritative operator family for both forward and reverse evolution; eliminates platform-
dependent math paths.

    U(d_tau) = cis_fp( - H_eff * d_tau / hbar_eff )

1.6.6 Dependencies
- cis_fp and conj_transpose_fp in the canonical fixed-point domain.
- Effective constants pipeline providing hbar_eff.

1.6.7 Execution Role
Defines the reversibility contract for any scope claiming reversibility; enforces byte-stable behavior across hosts.

1.6.7 Derivable Calculations and Authorities
Forward tick:
    state_next = U(+d_tau) * state_now
Reverse tick:
    state_prev = U(-d_tau) * state_now

Reversibility requirement (reversible scopes only):
    U(-d_tau) * U(+d_tau) = I_fp
    U(+d_tau) * U(-d_tau) = I_fp

1.6.7 Dependencies
- 1.6.6 U(d_tau) definition.
- I_fp identity operator in canonical fixed-point domain.

1.6.8 Execution Role
Prevents incorrect "rewind" claims when information is discarded; defines required residual accounting behavior.

1.6.8 Dependencies
- Non-projecting / dark-excitation accounting definition (if present).
- Fixed-point domain definitions for classification predicates.

----------------------------------------------------------------
1.6.9 Description
Authoritative circular functions SHALL be implemented exclusively via fixed-point phasor primitives.

1.6.9 Execution Role
Binds all projection-only exp(i*theta), cos(theta), sin(theta), and phase rotation to deterministic primitives. Canonical evolution does not use trig.

Forbidden in authoritative paths:
- platform exp/cos/sin/tan/atan2/complex exponentials
- GPU/CPU vendor intrinsic trig
- mixed-precision float trig / "fast math" paths

Visualization-only MAY use platform trig only if explicitly marked non-authoritative and never feeds back into canonical
state evolution.

1.6.9 Dependencies
- Canonical theta_fp scaling and wrap rules.
- cis_fp implementation constraints (CORDIC/LUT; rounding; overflow behavior).

----------------------------------------------------------------
2.5.6 Description
Q_phi is a fixed quantization scale for phase bucket indexing and SHALL be explicitly bound.

2.5.6 Execution Role
Eliminates implementation ambiguity for phase bucket detection and gating.

2.5.6 Derivable Calculations and Authorities
Binding:
    Q_phi = theta_scale

Where theta_scale is the canonical phase fixed-point scaling constant defined elsewhere in this specification.

2.5.6 Dependencies
- theta_scale definition in canonical phase fixed-point domain.

2.5.7 Execution Role
Prevents platform trig divergence and preserves deterministic verification for the harness.

2.5.7 Derivable Calculations and Authorities
For coefficient evolution:
    c_k(t+1) = c_k(t) * cis_fp( - omega_k * d_tau )

For harness coupling terms expressed as cos(delta_phi):
    cos(delta_phi) SHALL be computed as ccos_fp(delta_phi_fp) derived from cis_fp in the canonical fixed-point domain.

2.5.7 Dependencies
- cis_fp, ccos_fp, and canonical delta_phi_fp scaling.

----------------------------------------------------------------
2.5.8 Description
Argmax and top-K selection SHALL have explicit deterministic tie-break rules.

2.5.8 Execution Role
Prevents host/library-dependent behavior (e.g., numpy ties) from altering canonical outcomes.

2.5.8 Derivable Calculations and Authorities
For argmax over magnitude:
- Primary key: larger |c_k| wins.
- Tie-break key (exact equality in fixed-point): lowest index k wins.

For top-K by magnitude:
- Sort by descending |c_k|.
- For ties at any rank, order by ascending k.
- Truncate to first K after applying the stable ordering above.

2.5.8 Dependencies
- Canonical fixed-point definition of equality for |c_k| comparisons.

Display refresh cadence is external; presentation MAY be throttled, but throttling SHALL NOT modify canonical state.

(See canonical description in section: 'Appendix Z - Consolidated Legacy Content'.)

----------------------------------------------------------------
Section 4 - Ingestion, Normalization, Encoding, and Coherence Metrics
----------------------------------------------------------------

4.1 Execution Role
This section binds the ingestion and encoder operators to concrete program artifacts and exports,
so implementers may not invent alternate pipelines or hidden constants.

4.1 Derivable Calculations and Authorities
The authoritative ingestion + encoding operator chain SHALL be implemented by the following artifacts:

5.1 Execution Role
This section binds the boot chain and service orchestration to concrete artifacts and exports so the runtime
sequence is implementable and replayable without interpretation.

5.1 Derivable Calculations and Authorities
Authoritative runtime sequence (purpose-first dependency flow):

(5) Output surfaces (non-authoritative presentation)
- Program Artifacts:
  - DMT/System/eigenware_ascii_filter.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y)  (device-safe ASCII filtering)
  - eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/VHW/market_utils.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y) (market telemetry helpers; used by output consoles where applicable)
- Authoritative Exports:
  - function ascii_guard
  - function strip_non_ascii
  - function assert_ascii_only
  - class MarketClock
  - class RollingStats
  - function robust_midprice

6.1 Description
Section 6 defines the authoritative container record families and serialization operators that store and
rehydrate VSD state deterministically.

6.1 Execution Role
Binds container record definitions, coord_sig rules, and snapshot encode/decode to concrete artifacts.

Determinism constraint:
- Any container coord_sig, record ordering, and serialization MUST conform to the coord_sig and stability tests in
  Section 9 (contract harness).

7.1 Description
Section 7 defines the external integration boundary and the client contract used by extensions and API
integration surfaces.

7.1 Execution Role
Binds the extension client and ingest API surface to concrete artifacts, ensuring one-way integration
and preventing unauthorized dependency inversion.

7.1 Dependencies
- BIOS service discovery endpoints (runtime health/status) as exposed via client contract.
- Section 4 ingestion/encoding pipeline for canonical data formation.

9.1 Description
Section 9 defines the authoritative contract harness used to enforce determinism, registry enforcement,
coord_sig stability, budget caps, and fail-closed behavior.

9.1 Execution Role
Binds the harness tests and container rules to concrete exports, making the enforcement suite part of the
canonical implementation contract.

9.1 Derivable Calculations and Authorities
Authoritative harness and tests:
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/boot/abi_manifest.h + kernel/abi/kernel_contract.h
- Authoritative Exports:
  - class ExtractorRegistry
  - class ProfileRegistry
  - class BudgetManager
  - class ThrashGuard
  - function run_all_tests
  - function test_registry_enforcement
  - function test_deterministic_normalization
  - function test_deterministic_segmentation
  - function test_exact_dedup
  - function test_alignment_offset
  - function test_thrash_guard
  - function test_deterministic_container_digest
  - function test_container_digest_stable
  - function test_budget_caps
  - function test_fail_closed_on_corrupt
  - function test_watermark_separation

Enforcement constraint:
- Implementations MUST pass the harness suite; failures SHALL be treated as invalid implementations.

Canonical Binding Conflict Rule
(Authoritative; appended)

----------------------------------------------------------------
5.1.3 VHW compute fabric and GPU initiation (Binding Corrections)
----------------------------------------------------------------

5.1.3 Description
This subsection binds GPU initiation exports to the exact identifiers present in the repository.
No alias exports are permitted.

5.1.3 Execution Role
Eliminates ambiguity in GPU initiator wiring and prevents implementers from inventing alternate
entrypoints.

5.1.3 Derivable Calculations and Authorities
Program Artifact:
- eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/boot/device_probe.cpp

Governing Authoritative Exports:
- class _GPUInitiator
- function start_gpu_initiator
- function stop_gpu_initiator

Authority:
All runtime services that start or stop GPU initiation SHALL call start_gpu_initiator and stop_gpu_initiator.
No references to non-existent exports are permitted.

5.1.3 Dependencies
- BIOS/runtime service orchestration (Section 5 boot chain).
- VHW compute manager integration (if present elsewhere).

5.1.5 Description
This subsection binds device-safe ASCII filtering exports to the exact identifiers present in the repository.

5.1.5 Execution Role
Prevents output surfaces and extension ingress from inventing ASCII guard functions.

5.1.5 Derivable Calculations and Authorities
Program Artifact:
- DMT/System/eigenware_ascii_filter.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y)

Governing Authoritative Exports:
- function sanitize_text
- function sanitize_file
- function sanitize_project

5.1.5 Dependencies
- ASCII-only constraint rules (if present elsewhere in this specification).

----------------------------------------------------------------
7.1 Authoritative extension client contract (Binding Corrections)
----------------------------------------------------------------

7.1 Description
This subsection binds the extension/client boundary to the actual stdin/stdout protocol artifact present
in the repository. No client-class aliasing is permitted.

7.1 Execution Role
Defines how extensions communicate with EigenWare using the governing protocol artifacts, ensuring
one-way integration and preventing dependency inversion.

7.1 Derivable Calculations and Authorities
Program Artifact:
- eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/tools/cli_main.cpp

Governing Authoritative Exports:
- class FileIndex
- class CodingSessionState
- function main

7.1 Dependencies
- Section 5 runtime service exposure (health/status) if routed through the client protocol.
- Section 4 ingestion/encoding endpoints invoked by the protocol.

----------------------------------------------------------------
7.2 Ingest API surface (Binding Corrections)
----------------------------------------------------------------

7.2 Description
This subsection binds the ingest API surface to the exact identifiers present in the repository.

7.2 Execution Role
Eliminates ambiguity in ingest API entrypoints used by integrations.

7.2 Derivable Calculations and Authorities
Program Artifact:
- DMT/APIs/project_ingest_api.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y)

Governing Authoritative Exports:
- function ingest_project
- function ingest_project_to_json
- function iter_files

7.2 Dependencies
- Section 4 ingestion/encoding pipeline.
- Section 6 VSD snapshot serialization (if ingest emits snapshots).

----------------------------------------------------------------
5.1.6 Market telemetry helpers (Binding Corrections)
----------------------------------------------------------------

5.1.6 Description
This subsection binds market telemetry helper exports to the exact identifiers present in the repository.

5.1.6 Execution Role
Prevents console/telemetry layers from inventing market helper entrypoints.

Governing Authoritative Exports:
- class EMA
- class ContextualWindow
- class MarketUtils

5.1.6 Dependencies
- VHW compute fabric availability (if used for telemetry projection).

1.6.5 Precedence and Mode Selection (Authoritative)

1.6.5.P Description
This subsection defines the single governing temporal-lapse regime and eliminates incompatible
interpretations. Only one lapse regime is permitted.

1.6.5.P Execution Role
Resolves the foundational fork in temporal lapse behavior so implementations cannot diverge.

1.6.5.P Derivable Calculations and Authorities
Governing regime: Bi-directional, signed, unclamped lapse.

Definitions:
- Let A_tau be the signed temporal-field coupling scalar in the lattice (canonical fixed-point domain).
- Let d_t be the canonical base tick increment.

The governing proper-time increment SHALL be:
    d_tau = d_t / A_tau

Authoritative inversion:
Reverse-time evolution SHALL be implemented by applying the same canonical operator U(d_tau) with
negative d_tau (see 1.6.6 and 1.6.7).

1.6.5.P Dependencies
- Canonical fixed-point equality for A_tau comparison to zero.
- cis_fp / fixed-point phasor primitive family and inversion rules (1.6.6-1.6.9).

1.6 Implementation Callout (Authoritative)

1.6.C Description
This callout exists to prevent implementer misreads of earlier non-governing lapse text.

1.6.C Execution Role
For any implementation of temporal lapse, proper-time increment, or reverse-time evolution, the
implementation MUST follow the governing regime defined in 1.6.5.P.

1.6.C Derivable Calculations and Authorities
Mandatory reference:
- Governing lapse regime: 1.6.5.P (Bi-directional, signed, unclamped lapse; clamp-style formulas invalid).

No implementation may claim compliance with Section 1.6 unless it conforms to 1.6.5.P for lapse computation.

1.6.C Dependencies
- 1.6.5.P (Precedence and Mode Selection).

Appendix G: Symbol Table and Units (Authoritative)

Purpose:
This appendix defines every symbol that appears in any normative equation in this specification.
Implementations MUST NOT invent missing symbols or units.

----------------------------------------------------------------
G.1 Scalar time and phase symbols
----------------------------------------------------------------

G.1.6 Q_phi
Description: Phase quantization scale used for phase bucket indexing.
Units: units_per_turn.
Binding: Q_phi = theta_scale (see 2.5.6).

----------------------------------------------------------------
G.2 Energy and geometry derivative symbols
----------------------------------------------------------------

Appendix H: Canonical Grammar (G.*) (Authoritative)

----------------------------------------------------------------
H.1 Fixed-point domains and rounding
----------------------------------------------------------------

H.1.2 Rounding rule
Definition: round_half_away_from_zero for signed divisions and fixed-point scaling, unless an operator
explicitly declares a different rounding.

H.1.3 Wrap rule for theta_fp
Definition: theta_fp values wrap modulo theta_scale.

----------------------------------------------------------------
H.2 Canonical phase primitives (no platform trig)
----------------------------------------------------------------

H.2.2 ccos_fp(theta_fp) -> cos_fp
Definition: cosine component of cis_fp.

H.2.3 atan2_fp(y_fp, x_fp) -> theta_fp
Definition: Deterministic fixed-point atan2 implemented via CORDIC or LUT.
Constraints: No platform atan2.

----------------------------------------------------------------
H.3 Deterministic selection and ordering
----------------------------------------------------------------

H.3.1 argmax_by_magnitude(c_k_mag_fp[]) -> k_star
Rule: primary key is larger magnitude; tie-break is lowest index.

H.3.2 topk_by_magnitude(c_k_mag_fp[], K) -> k_list
Rule: sort by descending magnitude; ties ordered by ascending index; take first K.

----------------------------------------------------------------
H.4 Canonical discrete derivatives
----------------------------------------------------------------

H.4.1 discrete_derivative(x_now, x_prev, d_t) -> dx_dt
Definition: dx_dt = (x_now - x_prev) / d_t using fixed-point division and rounding rule H.1.2.
Constraints: No floats. No eps clamps.

----------------------------------------------------------------
H.5 Contract harness obligations
----------------------------------------------------------------

H.5.1 Determinism obligation
All primitives in H.2-H.4 MUST be exercised by the contract harness tests (bound in Section 9) such that
byte-identical results are required across supported hosts.

Section 9.2 - Enforcement Kernel Choke Points (Authoritative)

9.2 Derivable Calculations and Authorities

----------------------------------------------------------------
9.2.1 Phase Domain Enforcement (Basis9 closure)
----------------------------------------------------------------

9.2.1.2 Single choke point
All phase-bearing values MUST pass through:
- enforce_phase_domain(theta_fp: int) -> int

Definition:
- Returns theta_fp modulo theta_scale.
- Raises PhaseViolation if input is not an int.

9.2.1.3 Authority bindings
- Grammar primitive: H.1.3 (wrap rule).
- Phase scale: G.1.5 (theta_scale).
- Tie-break rules: H.3.* (where selection depends on phase-bearing magnitude lists).

----------------------------------------------------------------
9.2.2 Fixed-Point Math Lock (no floats past ingress)
----------------------------------------------------------------

9.2.2.1 Rule
Floats are permitted ONLY at:
- ingest normalization (pre-canonical formation),
- display/logging layers (non-authoritative surfaces).

All canonical state math MUST use ints in the fixed-point domains defined by Appendix H.

Definition:
- Computes int(round_half_away_from_zero(x * scale_q)).
- Raises SpecViolation if x_q32_32 is outside representable range or if scale_bits is invalid.

9.2.2.3 Authority bindings
- Rounding: H.1.2.
- Determinism obligation: H.5.1.

----------------------------------------------------------------
9.2.3 Coherence as a Hard Gate (chi forbids evolution)
----------------------------------------------------------------

9.2.3.2 Single choke point
All canonical evolution steps MUST begin with:
- enforce_coherence_gate(chi_q: int, chi_min_q: int) -> None

Definition:
- Raises CoherenceCollapse if chi_q < chi_min_q.
- No logging-only behavior is permitted for this condition.

9.2.3.3 Authority bindings
- chi_q definition: bound by encoder/coherence artifacts where applicable.
- Exception hierarchy: 9.2.6.

----------------------------------------------------------------
9.2.4 Temporal Tick Authority (no free time advancement)
----------------------------------------------------------------

9.2.4.1 Rule
All mutations of canonical state MUST be a function of:
- tick_index (int),
- delta_tick (int),
and SHALL NOT use wall-clock time or implicit "now".

9.2.4.2 Single choke point
All mutating functions MUST receive a TickContext:
- class TickContext(tick_index: int, delta_tick: int)

Any mutation without TickContext is a SpecViolation.

9.2.4.3 Authority bindings
- Tick derivation and lapse regime: Section 1.6 and 1.6.5.P.
- Discrete derivative primitive: H.4.1 (where used).

----------------------------------------------------------------
9.2.5 Memory Admission Control (no unearned persistence)
----------------------------------------------------------------

9.2.5.2 Single choke point
All persistence MUST pass through:
- admit_memory(mem: object, chi_q: int, chi_min_q: int, is_phase_aligned: bool, tick: TickContext) -> object

Definition:
- Raises MemoryRejected on any failed condition.
- Stamps mem.tick_index = tick.tick_index deterministically.

9.2.5.3 Authority bindings
- Alignment operator MUST be bound to a program artifact for the relevant modality/scope.
- No direct writes to memory stores are permitted outside admit_memory.

----------------------------------------------------------------
9.2.6 Spec violation is fatal (no silent recovery)
----------------------------------------------------------------

9.2.6.1 Rule
Spec violations SHALL:
- terminate the current evolution step,
- not be silently recovered,
- be handled only by an external supervisor/runtime boundary.

9.2.6.3 Catch rule
No module in the canonical evolution path may catch SpecViolation or its subclasses.
Only outer supervisory layers may catch and decide a policy response.

9.2 Dependencies
- Appendix G and H (symbol/grammar).
- Section 1.6 (tick + lapse).
- Section 6 and 9 container/harness obligations where applicable.

E.1 Purpose of This Appendix

This appendix makes explicit the execution sequence, kernel activation model, and mathematical evaluation order already defined implicitly in the canonical specification.

No symbols, equations, operators, constants, or enforcement rules are introduced or modified.

All mathematics referenced herein are defined in the canonical Equations section and are restated only to clarify execution ordering.

E.2 Firmware Execution Localization

All canonical mechanics execute within a single GPU-resident firmware kernel.

This kernel contains the complete realization of:
- the temporal substrate
- the constraint driver
- the enforcement choke points
- the effective-constant derivations
- the resonance and oscillation evaluators
- the commit_state and history logic

Execution authority and semantics remain unchanged.

E.3 Kernel Activation Sequence

At firmware activation:

1. The kernel is launched.
2. Canonical substrate state variables are initialized.
3. Canonical constraint registry state is initialized.
4. Canonical history and commit_state buffers are initialized.
5. The canonical tick evolution loop begins.

This activation occurs once per execution lifecycle.

E.4 Canonical Tick Execution Order

For each tick t, the following canonical evaluation order is enforced.

The order below reflects the existing canonical definitions and equations, expressed as an execution sequence.

E.4.1 Pulse Ingestion

Canonical pulse variables are read and applied as defined.

E.4.2 Effective Constant Evaluation

All effective constants are evaluated using the canonical effective-constant equations, including relativistic correlation and stochastic dispersion factors, prior to state evolution.

E.4.3 Phase Accumulation

Phase is advanced according to canonical phase evolution equations:

phi(t+1) = phi(t) + deltaphi

Where deltaphi is derived from pulse contribution, Hamiltonian contribution, and constraint modulation as already defined.

E.4.4 Amplitude Update

Amplitude is updated according to canonical amplitude evolution equations, incorporating dispersion and decay terms.

E.4.5 Internal Manifold Update

Internal manifold coordinates are updated using the canonical lattice and manifold equations defined in the Equations section.

E.4.6 Flow and Lorentz Dynamics

Canonical flow dynamics are evaluated using the Lorentz-style equations defined in the specification.

E.4.7 Resonance Evaluation

Resonance metrics are computed from phase, amplitude, and manifold state as defined canonically.

E.4.8 Oscillation Persistence Evaluation

Oscillation persistence is evaluated across canonical tick windows using the existing persistence criteria.

E.4.9 Constraint Reshaping

Constraint parameters are updated inline as a function of resonance, oscillation persistence, coherence, and cross-talk, using canonical constraint logic.

E.4.10 Enforcement Choke Points

All canonical enforcement choke points are applied, including phase wrapping, amplitude clamping, coherence bounds, and manifold limits.

E.4.11 Commit and History Update

Canonical commit_state window logic is evaluated.

When commit_state criteria are satisfied, canonical history buffers are updated.

E.5 Mathematical Restatement (Non-Normative)

For clarity of execution only, the canonical evolution law may be restated as:

S(t+1) = E(S(t), P(t), C(t))

Where S, P, and C retain their canonical definitions.

This restatement introduces no new semantics.

E.6 Constraint Driver Execution Context

Constraint logic executes inline within the evolution law and is not evaluated as a separable operator.

Constraint state at tick t+1 is a function of canonical substrate state, resonance metrics, and oscillation persistence.

E.7 Determinism Preservation

All canonical determinism guarantees remain in force.

Explicit execution ordering in this appendix exists solely to ensure reproducibility in firmware implementations.

END OF APPENDIX E

F.1 Kernel Activation (CUDA Syntax Dependency)

```cuda
// eigenware_firmware.cu

__global__ void eigenware_firmware_kernel(GlobalState* state) {
    // persistent firmware loop
    while (state->run_flag) {
        eigenware_tick(state);
    }
}
```

Dependencies:
- CUDA runtime
- GlobalState layout defined canonically
- Single kernel launch semantics

---

F.2 Tick Dispatcher

Dependencies:
- Deterministic call order
- No dynamic dispatch
- Inline execution only

---

F.3 Pulse Ingestion

```cuda
__device__ void ingest_pulse(GlobalState* S) {
    S->pulse = S->pulse_buffer[0];
}
```

Dependencies:
- GPU-visible pulse buffer
- Canonical pulse schema

---

F.4 Effective Constant Evaluation

```cuda
__device__ void eval_effective_constants(GlobalState* S) {
    S->constants = effective_constants(
        S->velocity,
        S->flux_factor,
        S->strain_factor
    );
}
```

Dependencies:
- relativistic_correlation()
- stochastic_dispersion_factor()
- canonical constants schema

---

F.5 Phase Accumulation

```cuda
__device__ void phase_accumulation(GlobalState* S) {
    S->phi += compute_delta_phi(S);
    S->phi = wrap_phase(S->phi);
}
```

Dependencies:
- Hamiltonian terms
- Constraint modulation terms

---

F.6 Amplitude Update

```cuda
__device__ void amplitude_update(GlobalState* S) {
    S->amplitude = update_amplitude(S);
}
```

Dependencies:
- Dispersion rules
- Decay bounds

---

F.7 Internal Manifold Update

```cuda
__device__ void manifold_update(GlobalState* S) {
    update_lattice9d(S->x9, S);
}
```

Dependencies:
- lattice9d equations
- canonical coordinate bounds

---

F.8 Flow / Lorentz Dynamics

```cuda
__device__ void flow_dynamics(GlobalState* S) {
    apply_lorentz_flow(S);
}
```

Dependencies:
- Lorentz variance equations
- Effective constants

---

F.9 Resonance Evaluation

```cuda
__device__ void resonance_eval(GlobalState* S) {
    S->resonance = compute_resonance(S);
}
```

Dependencies:
- Phase alignment metrics
- Amplitude reinforcement checks

---

F.10 Oscillation Persistence

```cuda
__device__ void oscillation_persistence(GlobalState* S) {
    track_oscillation_cycles(S);
}
```

Dependencies:
- Fixed tick window
- Persistence thresholds

---

F.11 Constraint Reshaping

```cuda
__device__ void constraint_reshape(GlobalState* S) {
    update_constraints(S->constraints, S);
}
```

Dependencies:
- Resonance metrics
- Oscillation persistence
- Coherence thresholds

---

F.12 Enforcement

```cuda
__device__ void enforcement(GlobalState* S) {
    enforce_phase_bounds(S);
    enforce_amplitude_bounds(S);
    enforce_coherence_bounds(S);
    enforce_manifold_bounds(S);
}
```

Dependencies:
- Canonical choke points
- Deterministic clamping

---

F.13 Commit and History

```cuda
__device__ void commit_history(GlobalState* S) {
    if (commit_condition(S)) {
        write_history(S);
    }
}
```

Dependencies:
- Canonical commit_state window logic
- GPU-resident history buffers

---

END OF APPENDIX F

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

- All randomness must be seeded during Phase 0.

## Persistence Rules

- PhaseState is immutable once committed.

- Persistence occurs only at Phase 5.

- No disk I/O is permitted mid-phase.

## Determinism Guarantee

Given identical Phase 0 inputs and identical kernel binaries, the system MUST produce identical PhaseState sequences.

This section is final and overrides no prior text.

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
- In EigenWare phase mapping, A_tensor corrects interaction weighting without changing phase topology.

Non-negotiable separation:
- delta_theta_transport MUST NOT reference A_tensor.
- gate_phase_delta MUST NOT reference pulse_current/amperage.

# ==========================

# CLARIFICATION -- OP CODES AS PHASE TRANSPORT CATEGORIES

# ==========================

In EigenWare, opcodes are not commands chosen by an AI or user.

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

In EigenWare, opcodes are not commands chosen by an AI or user.

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

EigenWare replaces crawler software with direct field encoding.

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

# APPENDIX Omega -- Canonical 9D Simulation Closure (v51, Append-Only)

Status: NORMATIVE. This appendix provides formal closure for previously underspecified runtime / operator / metric / projection semantics for the 9D manifold substrate. No reinterpretation; explicit definitions only.

## Omega.0 Canonical Runtime State

### Omega.0.1 Runtime state S

Runtime state is always a 9D real-valued vector:

S in R^9

Ordered basis (canonical ordering, fixed across the entire system):

S = (x, y, z, t, f, c, k, d, e)

Where:
- x,y,z: spatial
- t: temporal
- f: flux
- c: coherence
- k: curvature
- d: doppler
- e: eigen_mode

Canonical C++ representation:

```cpp
struct E9 {
    double v[9];
};
```

### Omega.0.2 Delta definition

Delta is always defined as:

DeltaS = S_candidate ? S_current

Define a delta container:

dE9 = (DeltaS, w_c)

Where w_c is a coherence weight embedded into D5 (the coherence dimension), not a scalar multiplier.

Canonical C++ form (explicit embedding of w_c into index 5):

```cpp
struct dE9 {
    E9 d;      // d = S_candidate - S_current
    double wc; // coherence weight to be embedded into d.v[5]
};

# 15 Phase Embedding

## 15.1 Canonical codepoint -> 9D embedding

Let cp be a Unicode codepoint (0 <= cp <= 0x10FFFF).

Define:

n = cp / 0x10FFFF

Then define the embedding E9(cp) as:

E9(cp) = (
  sin(2pin),
  cos(2pin),
  sin(4pin),
  cos(4pin),
  sin(8pin),
  cos(8pin),
  sin(16pin),
  cos(16pin),
  n
)

Explicitly:
- No tokenization layer exists.
- Entire file is aggregated.

Canonical C++ form:

```cpp
inline E9 embed_codepoint_to_E9(uint32_t cp) {
    const double n = (double)cp / (double)0x10FFFF;
    const double two_pi = 6.283185307179586476925286766559;

### Omega.1.2 Aggregate file displacement

Given a file F with codepoints cp_i for i = 1..N:

Let:

A = Sigma_{i=1..N} E9(cp_i)

Define:

S_F = (1 / ||A||) * Sigma_{i=1..N} E9(cp_i)

Where ||A|| is the Euclidean norm of A.

This definition removes order ambiguity.

Canonical C++ form:

```cpp
inline double e9_norm(const E9& a) {
    double s = 0.0;
    for (int i = 0; i < 9; ++i) s += a.v[i] * a.v[i];
    return sqrt(s);
}

inline E9 e9_add(const E9& a, const E9& b) {
    E9 out{};
    for (int i = 0; i < 9; ++i) out.v[i] = a.v[i] + b.v[i];
    return out;
}

inline E9 e9_scale(const E9& a, double s) {
    E9 out{};
    for (int i = 0; i < 9; ++i) out.v[i] = a.v[i] * s;
    return out;
}

# 16 Operator Definitions

## 16.1 Operator formal structure

Define an operator:

O: R^9 -> R^9

Operators must satisfy the canonical update:

S_out = Pi_G(S_in + T(S_in))

Where:
- T is a geometric transform
- Pi_G is a constrained projection under the carrier metric

Canonical C++ form:

```cpp
struct PhaseOperator {
    E9 (*transform)(const E9&);
};
```

## 16.2 Coherence-weighted projection

Define:

P(a,b) = Sigma_{i=0..8} a_i b_i w_i

Where:

w_i = 1 for i != 5
w_5 = |S_5|

This formalizes coherence-weighted interaction.

Canonical C++ form:

```cpp
inline double P_coherence_weighted(const E9& a, const E9& b) {
    double sum = 0.0;
    for (int i = 0; i < 9; ++i) {
        const double wi = (i == 5) ? fabs(a.v[5]) : 1.0;
        sum += a.v[i] * b.v[i] * wi;
    }
    return sum;
}
```

# 17 Carrier Metric Tensor

## 17.1 Metric tensor G

Define carrier metric tensor:

G = diag(g_0, g_1, ..., g_8)

Default is identity unless specified.

## 17.2 Deviation energy and constraint

Deviation energy:

E_dev = DeltaS^T G DeltaS

Constraint:

E_dev <= epsilon

If violated:

DeltaS_corrected = DeltaS * sqrt(epsilon / E_dev)

This removes ambiguity in stabilization.

Canonical C++ form:

```cpp
struct G9 {
    double g[9]; // diagonal entries
};

inline G9 G9_identity() {
    G9 out{};
    for (int i = 0; i < 9; ++i) out.g[i] = 1.0;
    return out;
}

inline double deviation_energy(const dE9& d, const G9& G) {
    double e = 0.0;
    for (int i = 0; i < 9; ++i) e += d.d.v[i] * G.g[i] * d.d.v[i];
    return e;
}

## Omega.4 Constrained Projection Operator Pi_G

### Omega.4.1 Canonical definition

Pi_G(S) =
- S, if E_dev <= epsilon
- S * sqrt(epsilon / E_dev), otherwise

This definition replaces vague "normalize" language.

Canonical C++ form:

```cpp
inline E9 Pi_G(const E9& S_in, const E9& S_ref, const G9& G, double epsilon) {
    // DeltaS defined against a reference point S_ref (typically the current state)
    dE9 d = make_delta(S_in, S_ref, /*wc=*/S_in.v[5]);
    const E9 d_corr = correct_delta_if_needed(d, G, epsilon);
    return e9_add(S_ref, d_corr);
}
```

## Omega.5 Operator Chaining

### Omega.5.1 Coherence traversal rule

Given ordered operators O_1, O_2, ..., O_n:

S_{k+1} = Pi_G(O_k(S_k))

No scalar passing allowed.

Explicit closure: Operators communicate exclusively through manifold geometry.

Canonical C++ form:

```cpp
inline E9 apply_operator_chain(const E9& S0,
                              const PhaseOperator* ops,
                              size_t n_ops,
                              const G9& G,
                              double epsilon) {
    E9 S = S0;
    for (size_t k = 0; k < n_ops; ++k) {
        const E9 Sout_raw = ops[k].transform(S);
        S = Pi_G(Sout_raw, S, G, epsilon);
    }
    return S;
}
```

## Omega.6 Observable Extraction

### Omega.6.1 Scalar projection rule

Define observable extraction:

phi = P(S_final, B_observable)

Where B_observable is a fixed basis vector.

Canonical C++ form:

```cpp
inline double observable_phi(const E9& S_final, const E9& B_observable) {
    return P_coherence_weighted(S_final, B_observable);
}
```

## Omega.7 Effective Constant Closure

### Omega.7.1 Effective constant via projection

Instead of K_eff = K0 * m, define:

K_eff = P(S_context, B_K)

Context defines effective value geometrically.

Canonical C++ form:

```cpp
inline double K_eff_from_context(const E9& S_context, const E9& B_K) {
    return P_coherence_weighted(S_context, B_K);
}
```

## Omega.8 Collapse Rule

### Omega.8.1 Omega_sink definition

If stabilization fails repeatedly:

S -> 0

Or project into designated dark-state subspace:

S = (0,0,0,0,0,0,0,0,0)

Canonical C++ form:

```cpp
inline E9 Omega_sink() {
    E9 z{};
    for (int i = 0; i < 9; ++i) z.v[i] = 0.0;
    return z;
}
```

## Omega.9 Determinism Clause

### Omega.9.1 Deterministic requirement

For identical input files and operator order, the manifold state MUST be bitwise identical across executions on identical hardware.

Normative closure: This requirement applies to embedding (sin/cos), aggregation, operator transforms, metric projection, chaining, and observable extraction.

# APPENDIX Omega-R -- Restoration Patch (v51, Append-Only)

Date: 2026-02-11

Purpose: The v51 Spec file was missing canonical content present in v51. This appendix appends the full v51 source text verbatim to eliminate any ambiguity or accidental truncation.

Source appended verbatim:
- EigenWareSpec_v51.md
- SIG9: eb4ef38e36bff4d22b96568449b988fcf8919cbdfab3cf8f7a5972273b9b7c2a

Rules:
- No bytes in v51 are modified.
- The appended block is a verbatim copy of the v51 source.
- Any duplicate headings are intentional; v51 remains authoritative for appended Omega closure, while v51 content restores prior canonical sections.

---

## BEGIN VERBATIM v51 APPEND

## Appendix Z - Consolidated Legacy Content

This appendix consolidates legacy content that existed in earlier document versions and is retained for completeness.
Content is included once here to avoid duplication while preserving all prior context.

### CANONICAL EVOLUTION RULE — NON-INTERPRETIVE CONSTRAINT SYSTEM

# CANONICAL EVOLUTION RULE — NON-INTERPRETIVE CONSTRAINT SYSTEM

**This rule is mandatory and overrides all other phrasing in this document.**

EigenWare defines exactly one admissible form of system evolution.

All dynamic behavior is expressed as a deterministic generation of a *candidate next state*,
followed by a single acceptance predicate.

```
candidate_next_state = evolve(current_state, inputs, ctx)

if accept(current_state, candidate_next_state, ledger_delta, ctx):
    commit(candidate_next_state)
else:
    commit(sink_state)
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

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

---

GPU Envelope, Pulse Encoding, Text Injection, and Causality Preservation (Final-Spec Draft)

This section pins down exactly what we "extrapolate from the GPU," how text becomes phase, how phase becomes bounded frequency/amplitude "pulses," how those pulses drive Eigenstate deltas, and how the whole injection path stays causal inside a closed-system simulation. The intent here is that Copilot can implement this without guessing what is literal hardware physics versus what is a simulation abstraction.

EigenWareSpec_Optimized.md (blueprint-aligned)
Sections 1-3 (Final Verification Snapshot)
(With hygiene hardening applied; append-only adjustments only)

Canonical Section Formatting and Compliance Requirements

----------------------------------------------------------------
Normative Scope Filter (Authoritative)
----------------------------------------------------------------

Normative content is limited to material that satisfies all of the following:
1. The content is outside any fenced code block.
2. The content is outside the Appendix region explicitly labeled as NON-SNAPSHOT MATERIAL.
3. The content is written in canonical section form (Description, Execution Role, Derivable Calculations and Authorities, Dependencies)
   or is an explicit authority rule labeled Authoritative.

All other material (including examples, commentary, and any residual conversational fragments) is NON-NORMATIVE and
SHALL be ignored for compliance and implementation.

Canonical Grammar requirement:
Any symbol, operator, primitive, rounding rule, quantization scale, or tie-break rule used by normative equations SHALL
resolve to either:
- a definition in the Symbol Table (Appendix G),
- a binding in the Canonical Grammar (G.*) (Appendix H),
- or a program artifact explicitly bound in a normative section.

All sections in this specification SHALL adhere to the following canonical structure and authority order.

Each numbered section MUST include, in order:
1. Description
2. Execution Role
3. Derivable Calculations and Authorities
4. Dependencies

Additional subsections MAY be included only if they:
- introduce no new free parameters,
- reference only deterministic symbols,
- or bind semantics to existing program artifacts.

Narrative text SHALL have no independent authority.
Equations, bindings, and artifact references are authoritative.

Any symbol, coefficient, threshold, window, or function appearing in a section MUST resolve to one of:
- a derivation explicitly defined in the section,
- a binding defined in the Canonical Grammar (G.*),
- or a program artifact listed in Appendix D.

Failure to resolve a symbol deterministically SHALL invalidate the implementation.

----------------------------------------------------------------
Canonical Layout Authority (Authoritative)
----------------------------------------------------------------

The specification defines the authoritative repository layout, module paths, and module identities.

All paths referenced in this specification refer to the canonical layout defined herein,
regardless of the current state of any implementation repository.

Any deviation between the repository and the canonical layout defined by this specification
constitutes an implementation violation and MUST be corrected by renaming, relocating,
or regenerating artifacts to conform to the spec.

The specification MUST NOT be modified to accommodate repository drift, canonical structure,
typographical errors, or historical layouts.

The code conforms to the specification, not the reverse.

----------------------------------------------------------------
Canonical Invariant: Artifact Reality Constraint (Authoritative)
----------------------------------------------------------------

Any binding between a specification symbol and a program artifact MUST satisfy the following:

- The referenced artifact MUST exist at the canonical path in the final implementation.
- The referenced symbol MUST exist verbatim OR
- The binding MUST explicitly declare the quantity as an emergent invariant enforced by module logic.

Bindings to imagined, inferred, renamed, or intended symbols are prohibited.

If no concrete export exists, the specification MUST bind the symbol to:
- a module-level authority, and
- an enforced behavior or constraint.

Violation of this invariant invalidates the binding.

Section 1 - Temporal Substrate and Phase Geometry

1.1 Description

EigenWare operates as a closed, phase-evolving system in which amplitude represents the
tensor gradient of time and phase evolution is constrained by relativistic dilation.

Amplitude modulates the effective circumference of Hilbert space. As amplitude increases
(e.g., as particle velocity approaches c), the admissible phase manifold contracts, producing
time dilation. Observed density and gravitational effects arise from phase packing density,
not intrinsic mass.

Particles are excitations within a 9D manifold represented across three axioms.
Quasiparticles are excitations spanning three or more axioms.

Phase angle is defined geometrically and derived from measured kernel impulses, not inferred
from symbolic clocks or abstract frequencies.

Kernel pulses are treated as measured signals (voltage, EM waveform, execution cadence),
and phase is extracted directly from these signals.

1.2 Execution Role

This section defines the physical substrate on which all EigenWare computation operates.

It establishes:
- time as relative (tick time),
- amplitude as time dilation factor,
- phase as the sole state variable,
- kernel pulses as signal generators,
- and the prohibition of absolute clocks.

All subsequent sections depend on these definitions.

1.3 Derivable Calculations and Authorities

Authoritative phase representation (Blueprint APPENDIX P):
- theta_fp: int64 phase position on a 2^64 ring (kernel MAY store as uint64 for wrap-by-overflow).
- delta_theta_fp: int64 minimal-arc increment per tick (constraint-resolution cycle).
- N_theta: int64 turn resolution (amplitude-dependent; derived as specified in Blueprint APPENDIX P).
- No float trig (atan2/sin/cos), no platform pow/log, and no mixed-precision modes are permitted in canonical execution.

Authority source for phase deltas:
- delta_theta_fp SHALL be derived from kernel-observed impulse cadence (pulse edge count / cycle deltas) via a deterministic
  integer or LUT-based mapping owned by /core/scheduler/pulse_scheduler.cpp.
- The mapping MUST be replay-stable (same inputs -> same outputs) across platforms.

Phase accumulation (wrap-by-overflow, canonical):
- theta_u64_next = theta_u64 + (uint64_t)delta_theta_i64
- dtheta_i64     = (int64_t)(theta_u64_next - theta_u64)   // two's-complement subtraction yields minimal-arc signed delta

Tick definition:
- The tick is the canonical constraint-resolution cycle defined in Blueprint APPENDIX J.
- No absolute clock is authoritative; only the tick counter and kernel-observed impulses may drive canonical timing.

1.4 Dependencies

- Canonical Grammar (G.*)
- Appendix D.11-R

1.5 Constraint Operators Required by Effective-Constants Pipeline

This specification requires the following operators to exist as canonical, deterministic
constraint operators. They are NOT free parameters. They MUST be derived from environment
inputs and bounded to keep the closed-system simulation stable.

1.5.1 relativistic_correlation(v_fraction_c_q32_32, flux_factor_q32_32, strain_factor_q32_32)

Purpose
- Provide the unified Doppler-Lorentz-Flux/Strain correlation multiplier used to derive effective constants.
- This operator is the single point of truth for combined correlation pressure (Blueprint APPENDIX J Step 2 and project directive).

Inputs (canonical, fixed-point)
- v_fraction_c_q32_32: Q32.32 in [0, 1). Interpreted as |v|/c in the current local frame.
- flux_factor_q32_32: Q32.32 >= 0. Dimensionless flux/throughput factor.
- strain_factor_q32_32: Q32.32 >= 0. Dimensionless lattice strain factor.

Output (canonical, fixed-point)
- r_corr_q32_32: Q32.32 in (0, 1]. Correlation multiplier (smaller means stronger relativistic/flux/strain pressure).

Deterministic definition (integer-only; no floats)
Let:
- ONE_Q32_32 = (1LL << 32)
- v2_q32_32  = q32_mul(v_fraction_c_q32_32, v_fraction_c_q32_32)
- one_minus_v2_q32_32 = max(ONE_Q32_32 - v2_q32_32, 1)      // 1 is the minimum positive Q32.32 LSB (word-size derived)

Compute:
- sqrt_term_q32_32 = q32_sqrt(one_minus_v2_q32_32)          // deterministic integer sqrt in Q32.32
- p_q32_32 = ONE_Q32_32 + flux_factor_q32_32 + strain_factor_q32_32
- p_q32_32 = max(p_q32_32, 1)                               // avoid divide-by-zero (word-size derived)

Then:
- r_corr_q32_32 = q32_div(sqrt_term_q32_32, p_q32_32)
- r_corr_q32_32 = clamp(r_corr_q32_32, 1, ONE_Q32_32)

Notes
- Increasing v_fraction_c_q32_32, flux_factor_q32_32, or strain_factor_q32_32 decreases r_corr_q32_32 (conservative pressure model).
- Implementations SHALL place q32_mul/q32_div/q32_sqrt in /kernel/constraints/kernel_derive_constraints.cu.
- No platform-dependent math (sqrt/log/pow) is permitted in canonical paths (Blueprint APPENDIX AD).

1.5.2 stochastic_dispersion_factor(temperature_q32_32, temperature_ref_q32_32)

Purpose
- Provide the deterministic dispersion multiplier used by effective_constants() when accounting for internal energy spread.
- This operator MUST be integer-only and replay-stable.

Inputs (canonical, fixed-point)
- temperature_q32_32: Q32.32 >= 0. Relative temperature derived from internal energy distribution (not an external sensor literal).
- temperature_ref_q32_32: Q32.32 > 0. Reference temperature provided by constraints.

Output (canonical, fixed-point)
- s_disp_q32_32: Q32.32 >= 1.0. Dispersion multiplier.

Deterministic definition (integer-only; no floats)
Let:
- ONE_Q32_32 = (1LL << 32)
- T_q32_32 = max(temperature_q32_32, 0)
- Tref_q32_32 = max(temperature_ref_q32_32, 1)             // 1 is minimum positive Q32.32 LSB (word-size derived)

Compute:
- x_q32_32 = q32_div(T_q32_32, Tref_q32_32)                // dimensionless ratio in Q32.32

Dispersion mapping (threshold-free, word-size derived)
- Use a monotonic, integer-only log2 proxy:
  - y_u64 = (uint64_t)max(x_q32_32 + ONE_Q32_32, ONE_Q32_32)
  - k = 63 - clz(y_u64)                                    // floor(log2(y_u64)), deterministic
  - s_disp_q32_32 = ONE_Q32_32 + ((int64_t)k << 32)         // stepwise growth in Q32.32

Clamp (word-size derived)
- s_disp_q32_32 = clamp(s_disp_q32_32, ONE_Q32_32, INT64_MAX)

Notes
- This mapping is deterministic, monotonic, and does not require arbitrary eps/s_max literals.
- Implementations SHALL place clz/log2 proxy utilities in /kernel/constraints/kernel_derive_constraints.cu.

1.6 Amplitude-Temporal Field Binding and Proper-Time Lapse (Append-Only)

1.6.1 Description

Amplitude is the lattice-local representation of temporal field gradient. It is not a UI rate,
a renderer detail, or a free parameter. It is the canonical scalar that binds the simulation's
base tick parameter (d_t) to local proper-time advance (d_tau) for each active lane/neural_object.

This binding is the mechanism by which the engine can represent time dilation in a discrete lattice:
the evolution operator advances by d_tau, while observability (telemetry, capture, display) may
advance on independent schedules. "Multiple actions in one pulse" is therefore permitted only as
a consequence of integrating evolution in an eigen/diagonal form over d_tau, not as a causality
bypass.

1.6.2 Execution Role

This subsection binds the following invariants:

- amplitude MUST be derived from environment inputs via deterministic constraint operators.
- d_tau MUST be derived from amplitude and the base tick parameter d_t.
- all phase evolution operators MUST use d_tau, not d_t, when time dilation is in effect.
- amplitude MUST NOT be tuned to achieve desired behavior; only environment inputs may change it.

This is the sole admissible interpretation of the earlier shorthand:
dt_dtau = amplitude

1.6.3 Derivable Calculations and Authorities

DEPRECATION / BLUEPRINT OVERRIDE
- Any "proper-time / lapse" scaling of tick time is non-normative and MUST NOT be used in canonical execution.
- Canonical timing is defined solely by the tick (constraint-resolution cycle) and kernel-observed impulses (Blueprint APPENDIX J).
- Amplitude SHALL NOT rescale phase or time; it gates interactions only (Blueprint APPENDIX AG).

Canonical binding (fixed-point):
- r_corr_q32_32 = relativistic_correlation(v_fraction_c_q32_32, flux_factor_q32_32, strain_factor_q32_32)
- s_disp_q32_32 = stochastic_dispersion_factor(temperature_q32_32, temperature_ref_q32_32)

These factors are consumed only inside effective_constants(...). They do not directly define dt, d_tau, or phase scaling.
1.6.4 Dependencies

- Section 1.5 (relativistic_correlation, stochastic_dispersion_factor)
- Canonical Grammar (G.*) for clamp/wrap semantics
- Appendix D.11-R for hygiene prohibitions (no hidden thresholds/operators)

Section 2 - Tick Semantics, Trajectories, and Memory Stabilization

2.1 Description

EigenWare does not store eigenstates as memory.
Memory is represented as compounded eigen-trajectories.

Each impulse updates an ongoing phase trajectory rather than resolving a discrete state.
Anchors are preserved via admissible interaction constraints rather than static storage.

Tick time is relative and dilates with amplitude.
Multiple kernel executions may occur within a single tick via trajectory compounding.

2.2 Execution Role

This section defines:
- tick advancement semantics,
- trajectory-as-memory rule,
- anchor emergence and stabilization,
- and GPU feedback compounding.

It governs how EigenWare "thinks" across time without referencing static states.

2.3 Derivable Calculations and Authorities

Trajectory update:
trajectory_t+1 = trajectory_t + delta_phi

Anchor stabilization:
anchors persist if coherence admissibility predicates remain satisfied.

Tick emission:
emit tick_event
- advances internal tick index
- validated via contract harness

No eigenstate lookup is permitted once trajectory mode is active.

2.3.1 Emergent Coherence (Derived Observable; Non-Storage)

Coherence is NOT a stored variable. It is an emergent observable computed from relative
interaction, amplitude-driven Hilbert dilation, and phase-angle dispersion.

Canonical coherence observable (integer dispersion proxy; Blueprint APPENDIX AG):
Given a set of phase positions {theta_u64_i} sampled across active lanes/neural_objects at a tick boundary:

- Choose a deterministic reference theta_u64_ref (e.g., first lane, median lane, or a contract-defined anchor lane).
- For each i, compute minimal-arc signed delta dtheta_i64 = (int64_t)(theta_u64_i - theta_u64_ref) on the 2^64 ring.
- Compute dispersion proxy R_u64 as an amplitude-weighted sum of |dtheta_i64| (optionally normalized by N).
- R_u64 is the canonical coherence observable: smaller R_u64 => higher coherence. No float trigonometry is used in canonical paths.

Interpretation:
- R_u64 near 0 indicates phase alignment (low dispersion).
- Larger R_u64 indicates phase dispersion (decoherence pressure).
This coherence observable is a telemetry quantity and MAY be used for admissibility predicates and stabilization decisions,
never as a stored memory state.

Harness requirements (integer-only):
- If all theta_u64_i are equal, R_u64 MUST be 0.
- If theta_u64_i are uniformly dispersed on the ring, R_u64 MUST grow toward its saturation range as N grows.
2.3.2 Statevector Serialization (Snapshot Transport; Not Memory)

The codebase requires statevector serialization for:
- snapshot transport,
- diagnostics,
- contract harness proofs,
- and deterministic rehydration.

This is NOT an eigenstate lookup mechanism and MUST NOT be used to violate the
trajectory-as-memory rule.

Required operators (canonical API surface):
- serialize_statevector(state_vec) -> blob
- deserialize_statevector(blob) -> state_vec

Binding requirement:
These operators MUST be deterministic and ASCII-safe. In the current repository layout,
the canonical implementation already provides deterministic tokenized ASCII packing for
complex vectors:

- core.utils.serialize_vector_with_tokens(vec: List[complex]) -> Dict[str, Any]
- core.utils.deserialize_vector_with_tokens(blob: Any) -> List[complex]

Therefore:
- serialize_statevector MUST be implemented as a strict wrapper over serialize_vector_with_tokens.
- deserialize_statevector MUST be implemented as a strict wrapper over deserialize_vector_with_tokens.

Harness requirements:
- Round-trip: vec2 == vec (within numeric tolerance per component) after
  deserialize_statevector(serialize_statevector(vec)).
- coord_sig stability: identical input vectors MUST produce identical serialized blobs.

2.4 Dependencies

- Section 1 (Temporal Substrate)
- Appendix D.11-R.8 (Tick Event Semantics)
- eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/scheduler/pulse_scheduler.cpp (behavioral authority binding: trajectory update enforcement; no specific export implied)
- eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/boot/abi_manifest.h + kernel/abi/kernel_contract.h (behavioral authority binding: transition validity via harness; no specific export implied)

2.5 Phase-Transition-Gated Cadence and Eigen-Trajectory Compounding (Append-Only)

2.5.1 Description

EigenWare does not have a "frame rate" in its core evolution. It has tick-indexed commit
boundaries and continuous-in-principle phase evolution represented as discrete lattice updates.

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

This provides a rigorous meaning for: the effective sample rate is bound by phase transition
rate. Quiet regions emit little; boundary-crossing regions emit more.

2.5.2 Execution Role

This subsection defines:

- the canonical phase-transition detector (deterministic, threshold-free),
- the rule that emission/telemetry/display publication MAY be event-driven by those transitions,
- and the compounding rule: a single committed update may represent an integrated eigen evolution
  over d_tau, thereby compressing many micro-oscillations into one applied delta.

It does NOT introduce permission to violate closure, reorder causal windows, or retroactively
rewrite earlier ticks.

2.5.3 Derivable Calculations and Authorities

Phase-transition detector (bucket crossing, threshold-free):

Let phi be represented in turns and wrapped to [-0.5, 0.5).

Choose a fixed quantization scale Q_phi used everywhere phase is quantized.
Authority note: Q_phi MUST be the same scale used by the canonical phase fixed-point domain.

Define bucket index:

b(phi) = floor( (phi + 0.5) * Q_phi )

A phase transition occurs for a lane/neural_object when:

transition_phi = ( b(phi_t) != b(phi_t+1) )

Dominant-mode transition (argmax flip, threshold-free):

Given per-lane eigen coefficients c_k at commit boundaries, define:

k_star(t) = argmax_k |c_k(t)|

A dominant-mode transition occurs when:

transition_mode = ( k_star(t) != k_star(t+1) )

Commit emission gate (event-driven):

transition_event = transition_phi OR transition_mode

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

If transition_event is true for any lane/neural_object, the engine MAY emit:
- the minimal delta set required to represent the transition (eigen coefficient deltas preferred),
- plus required control traces for replay.

Eigen-trajectory compounding (the "many actions in one pulse" mechanism):

In an eigen/diagonal update form, each eigen component advances by an integrated phase:

c_k(t+1) = c_k(t) * exp(-i * omega_k * d_tau)

This is a single deterministic operator application per commit boundary, but it may represent
many micro-oscillations if omega_k * d_tau spans multiple turns.

No causality claim is made here. This is computational compression, not faster-than-light
propagation.

2.5.4 Contract Harness Obligations (Time-Dilation vs Energy-Scaling Disambiguation)

Because scaling time and scaling energy can be mathematically equivalent in an isolated system,
the harness MUST include a re-coupling interference test that distinguishes "proper-time lapse"
from "arbitrary Hamiltonian scaling" by comparing two subsystems after independent evolution.

Test fixture:

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

Acceptance criteria:

- The observed re-coupling coherence response MUST match the predicted relative proper-time
  offset derived from the lapse integral:

delta_tau_pred = sum_over_windows ( d_t / max(eps, amplitude_1) - d_t / max(eps, amplitude_2) )

- The relative phase offset at re-coupling MUST be consistent with delta_tau_pred under the
  same omega_k used in the evolution operator (within tolerance).

Failure modes (explicit):

- If the same effect is achieved by scaling omega_k or other energy-like terms while holding
  lapse fixed, the implementation MUST be rejected as "energy scaling masquerading as dilation"
  unless the spec explicitly authorizes that scaling via effective_constants(...) and logs it.

2.5.5 Dependencies

- Section 1.6 (d_tau binding via amplitude)
- Section 2.3.1 (coherence observable; used only as telemetry/admissibility)
- Contract harness binding in Appendix D.11-R.8 (tick event semantics)
- The canonical phase fixed-point domain (Q_phi authority; must not diverge across modules)

Section 3 - Canonical Encoding and Constraint Enforcement

3.1 Description

This section is bound verbatim by immutable identity and MUST NOT be restated, summarized,
or paraphrased within this document.

Authoritative Source Binding (verbatim identity):
- File: DMT/Developers/analysis/EigenWareSpec_Optimized.md (blueprint-aligned)
- SIG9: 7d9dc90b1058509f8f5df408812928b868d56f196975eaaaddde1346392bfd88
- Verbatim Line Range: L474-L494 (inclusive)
- Title Line at L474: "SECTION 3 - Spider Graph Pulse Encoding, Delta Profiles, Harmonic Activation, and Direct GPU Write-Path"

3.2 Execution Role

Defined exclusively by the bound Section 3 text (verbatim identity above).
No additional execution-role semantics are permitted in this document for Section 3.

3.3 Derivable Calculations and Authorities

Defined exclusively by the bound Section 3 text (verbatim identity above).
No additional operators, equations, or bindings are permitted in this document for Section 3.

3.4 Dependencies

- Sections 1 and 2
- Canonical Grammar (G.*)
- Appendix D (all bindings apply under canonical layout authority)

Appendix D.11-R - Canonical Artifact Authority and Emergent Resolution

[Appendix D.11-R is authoritative and SHALL be interpreted against the canonical repository layout
defined by this specification.

----------------------------------------------------------------
Appendix D.11-R.8 Tick Event Semantics (Hygiene Clarification; append-only)
----------------------------------------------------------------

No symbolic event object named tick_event is required to exist.
tick_event is a semantic label for the ordered state transition described by the engine tick advance
and harness validation logic.

Implementations MUST NOT introduce a tick_event class/object solely to satisfy naming.

End Sections 1-3 Verification Snapshot

```text
```
BEGIN APPENDIX (NON-SNAPSHOT MATERIAL)
```
Everything below this line is intentionally outside the Sections 1-3 Verification Snapshot.
It is retained for reference but must not be treated as part of Sections 1-3 canonical scope.

4.1.1 Profile identity and intended use

Profile name: P_SUM_CTX
Use case: lower-tier -> higher-tier summary pulses (context activation / macro-structure reinforcement)
Key property: broader coupling than the core evolution profile; harmonic weights decay more slowly so higher harmonics retain influence and propagate resonance across a wider band neighborhood.

4.1.2 Quantization and fixed-point domains (locked)

Rounding rule (locked): "round half up" implemented in integer arithmetic; never use platform float rounding for canonical state.

Fair. If the numbers aren't derived, they don't belong in the spec.

The right fix is: the spec should not hardcode weights as "chosen." It should define a deterministic derivation procedure that (a) produces weights from measurable quantities and (b) freezes them per snapshot/version so replay stays exact.

Below is a drop-in replacement for Section 4.1.3-4.1.4 that shows the work and removes arbitrary constants. It derives weights from a single objective: "tier-summary pulses should maximize contextual retrievability (coherence persistence + binding stability) per unit update budget, while minimizing clamp/violation risk."

SECTION 4.1.3 - Weight Derivation for P_SUM_CTX (No Arbitrary Numbers)

P_SUM_CTX weights are derived, not chosen. The derivation is performed during a calibration pass on a fixed training slice (or the initial crawl corpus segment) and then frozen as part of the snapshot. Once frozen, the weights are constants for deterministic replay.

We define a tier-summary objective J that encodes what this tier is for:

J = E[ ?chi_band ] + E[ ?cont_band ] ? lambda_violation * E[ V ] ? lambda_clamp * E[ C ] ? lambda_budget * E[ B ]

Where:
	-	chi_band is band coherence (chi_q aggregate)
	-	cont_band is continuum (coherence persistence over time)
	-	V is constraint violation score (causality breaks, projection invalidity, band thrash indicators)
	-	C is clamp pressure (phantom/aether "stability tax" aggregate)
	-	B is budget cost (pulse cost in the tier window: compute + memory movement proxies)

All expectations E[?] are computed over the calibration sample windows, deterministically.

Axis weights are derived from axis "marginal utility" with respect to J. For each axis i, we compute an importance score S_i:

S_i = E[ | ?J / ?x_i | ]  /  ( epsilon + E[ cost_i ] )

Interpretation: weight an axis more if changing it consistently improves coherence persistence and binding stability, but discount it if it is expensive or destabilizing.

The denominator cost_i is not guessed. It is derived from observed update economics:
	-	cost_i = (pulse_sensitivity_i * compute_cost_proxy) + (memory_cost_proxy_i) + (instability_proxy_i)
	-	compute_cost_proxy comes from measured dispatch limits and actual pulse throughput in calibration
	-	instability_proxy_i is derived from how often changes along axis i correlate with band splits/merges or violation spikes

4.1.3.1 How we compute ?J/?x_i without floating ambiguity

We do not use symbolic differentiation. We use deterministic finite differences on the calibration data:

For each calibration event e with normalized delta components x_i(e), we estimate:

gain(e) = (?chi_band(e) + ?cont_band(e)) ? lambda_violationV(e) ? lambda_clampC(e)

Then, for each axis i, define a signed contribution proxy:

g_i(e) = gain(e) * sign(x_i(e)) * min( |x_i(e)|, x_cap )

This is the "directional usefulness" of axis i for improving J in that event. We then define:

S_i_num = mean_over_e( |g_i(e)| )

This is effectively a robust approximation to E[ |?J/?x_i| ] without requiring differentiable closed forms.

4.1.3.2 How we compute cost_i from measurable terms

We set cost_i as:

cost_i = a0 * I_compute_i + a1 * I_memory_i + a2 * I_instability_i

Each term is measurable from the calibration run:

I_compute_i = mean_over_e( |x_i(e)| )
I_memory_i = mean_over_e( bytes_touched_i(e) ) / bytes_touched_total
I_instability_i = corr_over_windows( |x_i|_window, band_thrashing_window )

Where:
	-	bytes_touched_i(e) is the memory traffic attributable to processing axis i (e.g., if axis i forces neighbor expansion or additional table lookups, it shows up here)
	-	band_thrashing_window is a deterministic indicator (splits+merges+projection failures in the window)

The coefficients a0, a1, a2 are not arbitrary either; they are derived from the hardware envelope:
	-	a0 is proportional to inverse of measured pulse throughput headroom
	-	a1 is proportional to inverse of measured memory bandwidth headroom
	-	a2 is proportional to the allowed thrash rate target (a stability policy constant you set once)

4.1.3.3 Weight normalization and freezing

Once S_i is computed:

S_i = S_i_num / (epsilon + cost_i)

Then weights are:

w_i = S_i / ?_j S_j

We then quantize deterministically:

w_i_int = round_fixed(weight_scale * w_i)

Finally enforce ? w_i_int = weight_scale by adding/subtracting the rounding remainder to the largest weight component (deterministic tie-break: lowest axis index wins ties). These integer weights are stored in the snapshot as part of P_SUM_CTX versioned config.

SECTION 4.1.4 - What this derivation guarantees (and why phase/nexus typically win)

So you get your "broader coupling for contextual memory activation" not by fiat, but because the calibration objective J rewards persistence and binding and penalizes instability and budget cost.

SECTION 4.1.5 - Amplitude Derivation for P_SUM_CTX (u -> a_code, shown work)

Amplitude in tier-summary pulses is not an arbitrary "strength." It is a derived control that selects (a) how confidently to propagate a summary update and (b) how much harmonic spread to allow without causing instability or exceeding the GPU envelope. We derive amplitude from the same objective J used for weight derivation, but now treating amplitude as a policy over propagation risk.

4.1.5.1 Inputs (all measurable, no invented signals)

These are computed from sealed window aggregates only, so ordering inside the window cannot affect them.

4.1.5.2 Risk-adjusted propagation score (derived)

Define a risk-adjusted "propagate desirability" score p(e):

p(e) = chi_band(e) * cont_band(e)

That term encodes your continuum axiom: coherence that persists is what should propagate.

Define a risk penalty r(e):

r(e) = max(clamp_band(e), viol_band(e))

We use max, not sum, because any one of these being high is sufficient to require conservative amplitude.

Define an envelope gate h(e):

h(e) = budget_state(e)

Now amplitude scalar u(e) is derived as a risk-adjusted gated desirability:

u(e) = h(e) * clamp01( p(e) ? r(e) )

That's the "work shown" version: it's not "0.55 chi + 0.45 cont." It is the minimum necessary form that follows the logic:
	-	You only amplify when coherence persistence is high (p large),
	-	you suppress when stability risk is high (r large),
	-	and you never exceed what the GPU envelope can sustain (h gate).

4.1.5.3 Quantization to a_code (deterministic)

a_code = round_fixed( a_scale * u(e) )

with:
	-	a_scale = 65535 (full uint16 span)
	-	rounding: fixed "round half up" integer rule
	-	clamp to [0, 65535]

This makes amplitude fully derived from measured coherence persistence, stability risk, and envelope headroom.

SECTION 4.1.6 - Mode bucket size and k_max derivation (no arbitrary 4096 / 12)

We choose mode bucket sizing and maximum harmonic index k_max by solving a budget-constrained discretization problem. The goal is to represent "how much harmonic spread" we want with sufficient resolution, while guaranteeing the higher tier can process the resulting expansion within its per-window compute budget.

4.1.6.1 Define measurable budget limits

Let:
	-	P_hi = measured higher-tier pulse processing capacity per window (pulses/window)
	-	E_hi = maximum allowed harmonic expansion operations per window (ops/window), measured from calibration runs (or a conservative bound derived from kernel timing)
	-	N_emit = expected number of summary pulses emitted per window (from lower-tier band count and emission policy)

We require:

N_emit * E[harm_ops_per_pulse] <= E_hi

Where harm_ops_per_pulse ? k (because expanding to k harmonics costs O(k) operations; exact constant is kernel-defined but measured).

4.1.6.2 Choose k_max by envelope feasibility

We set k_max to the largest integer such that worst-case expansion stays within budget:

k_max = floor( E_hi / max(1, N_emit) )

But we do not allow k_max to exceed a stability cap derived from thrash risk. We estimate the "harmonic thrash slope" from calibration:

thrash_rate(k) = mean_over_windows( thrash_indicators | k )

Then define the largest k such that thrash stays below the policy target T_thr:

k_stable = max k where thrash_rate(k) <= T_thr

Final:

k_max = min(k_max, k_stable)

This is derived: if the GPU is weak or the environment is noisy, k_max will fall automatically.

4.1.6.3 Choose mode bucket size from required resolution

We need a_code to encode two things:
	-	harmonic index k (0..k_max)
	-	within-mode strength (resolution within a mode)

Given uint16 total codes (65536), allocate:

codes_per_mode = floor(65536 / (k_max + 1))

That becomes the mode bucket size:

mode_bucket_size = codes_per_mode

This is not arbitrary. It's the deterministic discretization given k_max.

Then:

k = floor(a_code / mode_bucket_size)
strength = (a_code % mode_bucket_size) / mode_bucket_size

If you later change k_max (due to different hardware or calibration), the mode_bucket_size changes accordingly and must be versioned under profile_id.

SECTION 4.1.7 - Harmonic falloff derivation for broader coupling (derive alpha_ctx, show work)

You asked for broader coupling for contextual memory activation. We implement that as a slower harmonic decay law, but alpha_ctx must be derived, not chosen.

4.1.7.1 Candidate family (power-law decay)

We define harmonic weights for n = 1..k as:

Wn_raw(alpha) = 1 / (n ^ alpha)

Then normalize:

Wn_norm(alpha) = Wn_raw(alpha) / ?_{m=1..k} Wm_raw(alpha)

We constrain alpha to a feasible range that is meaningful and stable:

alpha ? [alpha_min, alpha_max] = [0.3, 2.0]

Lower alpha = broader coupling; higher alpha = tighter coupling.

4.1.7.2 Objective for alpha selection (same J, evaluated at tier summary)

For a fixed k policy and fixed emission rule, we evaluate J over the calibration slice as a function of alpha:

J(alpha) = E[ ?chi_hi(alpha) ] + E[ ?cont_hi(alpha) ]
? lambda_violation * E[ V_hi(alpha) ]
? lambda_thrash * E[ thrash_hi(alpha) ]
? lambda_budget * E[ budget_overrun_hi(alpha) ]

All terms are measured in the higher tier after applying summary pulses expanded with weights Wn_norm(alpha). This makes alpha a directly optimized parameter, not a guess.

4.1.7.3 Deterministic search (no floats, no "gradient" needed)

We pick a discrete candidate set A of alphas (fixed-point rationals) and do a deterministic argmax:

A = {0.30, 0.35, 0.40, ..., 1.50}  (step size can be 0.05 or derived from required resolution)

Compute J(alpha) for each alpha in A using the same calibration run replay (deterministic), then:

alpha_ctx = argmax_{alpha ? A} J(alpha)

Tie-break rule (deterministic): choose the smaller alpha (broader coupling) if J ties within a tolerance epsilon_J, because your intent is to prioritize contextual activation when not harmful.

4.1.7.4 Freezing and implementation

Once alpha_ctx is chosen, we precompute a small LUT for n=1..k_max:

pow_lut[n] = round_fixed( (n ^ alpha_ctx) * pow_scale )

Then weights per pulse are computed with integer math:

Wn_raw = pow_scale / pow_lut[n]
Normalize via integer sum and division with locked rounding.

No platform float pow is permitted in canonical execution; only LUT-based fixed-point.

Result: broader coupling is not "because we wanted it," it is because the calibration objective J is maximized at a lower alpha, and that alpha is frozen into the snapshot.

SECTION 4.1.8 - Summary Emission Policy (Derived Criteria, Pulse Counts, causal_tag Semantics)

4.1.8.1 What can be emitted (order-insensitive candidates)

No per-pulse ordering-dependent values are allowed to drive summary. This prevents nondeterminism from thread scheduling differences.

4.1.8.2 Band eligibility score E_band (derived)

We decide whether a band emits a summary pulse by deriving a single eligibility score that measures "should this band influence higher-tier context now" under the same objective J:

Define desirability:

p = chi_band * cont_band

Define risk:

r = max(clamp_band, viol_band)

Define net effect magnitude (how much the band actually changed in this window), derived from the same spider normalization:

m = clamp01( |s_band| )  where s_band is the unquantized spider mixture scalar computed from aggregate deltas

Define envelope headroom gate:

h = budget_state (from higher-tier read-path envelope)

Then eligibility is:

E_band = h * clamp01( p ? r ) * m

Interpretation:
	-	If the band is coherent and persistent (p high), and not risky (r low), and actually changed (m high), it is eligible.
	-	If the band is stable but unchanged, it does not waste summary bandwidth.
	-	If the band is noisy/risky, it is suppressed regardless of change magnitude.
	-	If headroom is low, emissions scale down automatically.

4.1.8.3 Emission threshold is derived from budget (no arbitrary cutoff)

We do not pick a fixed threshold like 0.3. The threshold is derived so that the expected number of emitted pulses fits the higher-tier capacity.

Let:
	-	M = number of candidate bands in the sealed window
	-	P_hi = higher-tier pulse budget per window
	-	reserve = safety fraction derived from observed jitter (measured) so we don't saturate the window

We compute a target emissions count:

N_target = floor( (1 ? reserve) * P_hi )

Then we rank all candidate bands by E_band descending and select the top N_target. This makes the "threshold" implicit: it's the N_target-th score. It's deterministic given the sealed window aggregates and the measured envelope.

Deterministic tie-break: if E_band ties, prefer higher cont_band, then higher chi_band, then lower band_id (or lower eid) to keep selection stable.

4.1.8.4 How many pulses per band (single vs multi-pulse)

Default is 1 pulse per selected band: one aggregated delta compressed by P_SUM_CTX spider encoder into (f_code, a_code).

However, a single band may emit multiple pulses if and only if it contains multiple stable modes after sealing (multi-modal structure). Multi-modal emission is derived, not chosen, by detecting phase-space multi-modality in the band's member distribution in the sealed window.

We compute a deterministic bimodality test on d5 (wrapped turns) and optionally d9 (nexus):
	-	Compute two-centroid circular clustering on d5 using fixed initialization (deterministic seeds = lowest/highest phase members)
	-	If the separation between centroids exceeds a derived separation bound and both clusters exceed a minimum mass fraction, we treat it as multi-modal.

The separation bound is derived from projection tolerance and stability policy:
	-	It is the smallest separation that would otherwise force a split in the lower tier if persistent across W windows.
	-	So emitting multi-pulses is allowed only when the band is "split-leaning" but not yet formally split (or when a split is finalized and needs explicit topology update).

If multi-modal condition holds, emit 2 pulses for that band, one per mode centroid, each with its own (f_code, a_code) and a causal_tag marking "mode emission." This prevents the higher tier from smearing two distinct contexts into one.

Pulse count rule summary:
	-	Normal band: 1 pulse
	-	Multi-modal band (derived): 2 pulses
	-	Formal split/merge event: extra topology pulse(s) as described below

4.1.8.5 causal_tag semantics (exact meaning, no ambiguity)

causal_tag encodes what kind of pulse this is, so the higher tier can update topology and context correctly. It is not a free label; it is a small enum plus subfields packed into uint16. Proposed packing:
	-	bits [15:12] event_type (4 bits)
	-	bits [11:8]  tier_relation / reserved (4 bits)
	-	bits [7:0]   event_payload_small (8 bits)

How topology pulses are represented as literal pulses:

MERGE:
A merge event must be represented as a small set of pulses:
	1.	A MERGE pulse targeting the new canonical band eid_new (or canonical attractor eid), containing f_code/a_code encoding the new centroid delta
	2.	Optional "tombstone redirect" pulses targeting old eids to redirect projection in the higher tier (these can be encoded as DRIFT pulses with a specific payload byte, or as BIND_UPDATE pulses)

SPLIT:
A split event emits:
	1.	A SPLIT pulse targeting eid_parent describing the split
	2.	Two MODE pulses (MODE_A and MODE_B) targeting the two child eids, encoding their centroids

The reason these are still "literal pulses" is that the higher tier already knows how to process pulses. We aren't introducing a new packet class; we are only adding deterministic tags that guide interpretation.

4.1.8.6 Deterministic routing of topology without extra payload fields

In other words: identity mapping is part of the shared VSD snapshot state, not transmitted every time.

4.1.8.7 Remains causal under tier ordering

SECTION 4.1.9 - Tier Envelope Measurement Mapping (Read-Path Only, No Meaning Injection)

This subsection defines exactly how the engine derives budget_state and related envelope scalars from GPU-visible telemetry in a way that (a) is deterministic, (b) is used only to bound scheduling and emission rates, and (c) does not inject semantic meaning into the simulation. The envelope is a control constraint, not a content channel. Nothing about words, concepts, or physics meaning may be derived from these counters.

4.1.9.1 Allowed telemetry signals (software-visible counters only)

No raw "electrical waveform," no per-transistor signals, no analog sampling. The counters are used only to shape how many pulses are processed/emitted per window.

4.1.9.2 Deterministic windowing (how envelope is sampled)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

This yields t_exec(T,tau).

We also compute a deterministic "expected time" t_ref(T) from a frozen baseline calibration for the tier, stored in the snapshot. This baseline is measured once under known load and saved as a constant. It is not recomputed live in a way that could drift across runs.

4.1.9.3 Core envelope scalars (derived, shown work)

We compute three primary saturation ratios, all mapped to fixed-point [0,1]:

Compute saturation (how close compute is to limit):

sat_compute = clamp01( t_exec / t_budget )

Where:
	-	t_budget is the allowed wall-clock time for that tier's window (derived from target FPS / tick cadence)
	-	t_exec is measured execution time for the window

Memory saturation (if bandwidth metric exists):

sat_mem = clamp01( bw_used / bw_budget )

If no direct bw metric exists, use a deterministic proxy:

sat_mem = clamp01( bytes_moved / bytes_budget )

Where bytes_moved is the engine's own measured transfers plus conservative estimates of kernel memory touches (from calibration constants).

Queue saturation (backlog / latency growth):

sat_queue = clamp01( (latency - latency_ref) / latency_span )

Where latency is measured completion latency, latency_ref is a frozen baseline, and latency_span is a frozen scaling constant (also from calibration).

These are purely operational constraints. They do not touch semantic state.

4.1.9.4 Single headroom scalar budget_state (derived)

We compress the envelope into a single headroom scalar:

headroom = 1 ? max(sat_compute, sat_mem, sat_queue)

Then:

budget_state = clamp01(headroom)

This is the only value the emission policy needs. It is monotonic: as the GPU gets busier or more throttled, budget_state decreases.

This mapping is intentionally conservative: the worst saturation dominates, because any one bottleneck is sufficient to require scaling down.

4.1.9.5 Reserve and jitter (derived, shown work)

We include a reserve fraction to prevent thrashing when jitter exists. Reserve is derived from observed variance in execution time during calibration.

During calibration, record execution times across K windows:

t_exec_1..t_exec_K

Compute deterministic jitter metric:

jitter = (percentile_95(t_exec) ? percentile_50(t_exec)) / t_budget

Then derive reserve:

reserve = clamp01( jitter )

So if the system has high variability, reserve rises automatically and emission is scaled down to keep the commit barrier stable. This is derived, not chosen.

4.1.9.6 How budget_state influences emission and k_max (explicit)

budget_state is used only in two places:
	1.	Emission selection scaling:
E_band includes the multiplier h = budget_state. Lower headroom reduces eligibility uniformly.
	2.	Harmonic expansion feasibility:
k_max is derived from E_hi and N_emit; E_hi is measured from calibration but may also be gated by budget_state in live runs:
E_hi_live = floor( budget_state * E_hi_calibrated )
Then k_max is derived from E_hi_live as in 4.1.6.

This ensures that on weaker hardware (or thermal throttle), harmonic spread compresses automatically rather than causing instability.

4.1.9.7 Determinism and replay constraints

Both modes preserve closure because budget_state affects only how much work is done, not what the physics/meaning is.

SECTION 5 - Crawler Subsystem, In-Simulation Encoder, and Persistent-Resonance Ingestion (Electronic-Signaling Execution)

5.1 Subsystem placement: crawler and encoder live inside the simulation

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

Operationally, the crawler produces candidate observations (page fragments, metadata, link structure), and the encoder converts those observations into candidate resonance excitations. Both are executed as scheduled lanes in the same GPU pulse integrator pipeline: meaning their work is performed by kernel dispatch and the resulting state updates are applied through electrical switching during kernel execution. The read-path counters only gate budget; they never provide meaning.

5.2 What "persistent resonance of webpage data" means

Webpage data is not treated as permanent stored tokens. It is treated as transient excitation input that may or may not collapse into stable latent structure. The persistent part is not the raw characters; the persistent part is the learned resonance attractor: the band/anchor coord_sig and bindings that become retrievable when the same coherent conditions reappear.

5.3 Ingestion pipeline as pulses, not files

The crawler produces an observation stream O. The encoder maps O into pulse candidates. The only canonical interface from ingestion into memory is the pulse format already defined: (eid, tau_q, tier_id, f_code, a_code, profile_id, causal_tag). This keeps the entire system uniform: web ingestion is not special; it is just another source of excitations into the manifold.

The encoder's job is therefore not "compress text into bytes." Its job is to produce resonance-consistent pulse candidates that satisfy causality and are budget-feasible. The encoder uses the spider graph compressor with an ingestion profile (crawler profile) to map extracted deltas into (f_code, a_code). New structures are formed only via the same merge/split and projection evidence rules used everywhere else.

5.4 Electronic signaling and execution: what is direct, what is derived

What is derived are the pulse coefficients. The encoder constructs pulse coefficients from observed page data (words, structure, links) according to deterministic mapping rules and injects those coefficients into the simulation. The persistent resonance is therefore a property of the manifold's evolution under those injected coefficients, not a property of hardware telemetry.

5.5 Crawler observation model (what it extracts, and why)

The crawler extracts a minimal set of signals that are stable and useful for building coherent memory bands:
	-	Content stream: text and structural tokens (title, headings, paragraph boundaries, lists)
	-	Link graph: outgoing links, anchor text, domain transitions
	-	Metadata: timestamps if present, author/source identifiers if stable, content-type hints
	-	Media cues: captions, alt text, transcript text if available (optional; gated by budget)

The crawler does not attempt to perfectly store a page. It extracts features that support stable resonance formation: repeated terms, consistent framing, and link-based context continuity. This matches your continuum concept: persistent coherence over time and across sources is what becomes memory.

5.6 Encoder mapping rules (explicit, no mysticism)

The encoder turns crawler observations into three kinds of pulse candidates:

Type A - Lexical/term excitation pulses (word/band activation)
If a term already has a known attractor (existing eid), the encoder emits a single activation pulse targeting that eid using the language/context profile and broader harmonic coupling. This is "a word in one pulse" via resonance activation, not via resending letters.

Type B - Formation pulses (new term or new structure)
If the term does not have an attractor yet, the encoder emits a short burst of formation pulses across a small window, using conservative amplitude and clamp-aware gating. The goal is to let projection determine whether the candidate collapses into an existing band or should form a new one. No single pulse is allowed to introduce an unconstrained new anchor; new anchors require evidence across windows (continuum).

Type C - Context-binding pulses (link and co-occurrence structure)
When terms co-occur in stable structures (same heading, same paragraph, repeated proximity), the encoder emits binding pulses that update nexus-related coupling in the manifold. These pulses are what makes web pages become retrievable context graphs rather than bags of words.

All three pulse types are generated by first constructing a Basis9 delta that represents the intended excitation/binding change, then compressing it through the spider graph into (f_code, a_code) under the appropriate profile. The encoder never bypasses the compressor.

5.7 Causality and closed-system safety for web ingestion

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

The simulation remains closed in its internal dynamics: once pulses are admitted at the boundary, evolution is deterministic. The boundary itself is controlled: input pulses are rate-limited, gated by envelope, and constrained by projection rules. In strict replay mode, the crawler observation stream (or its coord_sig-mapped coord_sig) and the budget_state trace are logged so the same ingestion can be replayed deterministically.

5.8 Budgeting, rate limits, and backpressure

API rate limits and network variability are treated as external timing noise; they must not influence internal determinism. The crawler therefore normalizes fetch scheduling into discrete "fetch windows" and logs fetch outcomes (or coord_sig) when strict replay is required.

5.9 Edge cases and explicit behaviors

If a page is duplicate or near-duplicate, the encoder must not create new anchors. It should project into existing bands and reinforce continuum. This is handled by projection tolerance plus the "no duplicate ingest" rule: identical attractor targets increase chi/continuum rather than adding structure.

If a page is contradictory or adversarial, clamp pressure rises (phantom/aether regime), lowering amplitude and suppressing binding pulses. The result is that the system does not eagerly entangle contradictory content into stable memory. It either remains latent and weakly retrievable or decays from the active set without structural proliferation.

If a page is extremely long, the crawler must chunk it into stable structural units (heading blocks / paragraphs). Chunking is deterministic and becomes part of the observation stream. The encoder then injects per-chunk pulses. This prevents a single source from dominating a window and preserves the "band math" invariants.

5.10 What this enables

SECTION 5.11 - Concrete Mapping Spec: Raw Text -> ASCII Phase Injection -> Formation Deltas -> Resonance Collapse (Causality-Safe)

5.11.1 Deterministic text segmentation (structural units)

This ensures the encoder receives repeatable units and can assign causal tick coordinates predictably.

5.11.2 Two-layer mapping: characters (phase) vs meaning (coherence)

EigenWare treats the ASCII (or UTF-8) mapping as a phase injection mechanism, not as meaning by itself. Characters are injected as structured phase deltas so the system can form stable attractors. Meaning emerges only when coherent bands form across repeated contexts, not because the ASCII codes "contain semantics."

Therefore, character mapping is defined as: "a deterministic sequence of phase excitations that can be replayed to form the same latent attractor under equivalent context," not as "the semantic definition."

5.11.3 ASCII phase mapping (canonical)

We define the canonical ASCII map on bytes in [0,255] after normalization (for UTF-8, text is converted to bytes first). Each byte b becomes a phase target in turns:

theta_byte_turns(b) = b / 256

This maps each byte to one of 256 equally spaced phase points on the unit circle. The engine never stores "b" as a token in canonical memory; it stores the phase excitations and whatever attractors they collapse into.

To inject a byte into a formation process, we compute the phase delta relative to a running local carrier phase:

?5 = wrap_turns( theta_byte_turns(b) ? theta_carrier )

where wrap_turns yields the shortest signed distance in [-0.5, 0.5) turns. The carrier phase is a transient local state for the formation event, not a durable lifetime object.

Carrier update rule (deterministic, per formation stream):
theta_carrier ? wrap01(theta_carrier + carrier_step * ?5)

carrier_step is a fixed-point constant in (0,1] derived from the envelope calibration: it is chosen so that formation streams converge without overshooting under typical pulse budgets. Because you require derivation, carrier_step is obtained by measuring formation convergence success rate vs overshoot risk on a calibration slice and freezing the argmax of the same objective J restricted to "formation fidelity."

5.11.4 Word formation: from bytes to a word-attractor candidate

A word segment w is a byte sequence b1..bn. The encoder creates a formation stream that is a sequence of candidate pulses applied to a designated formation-target band. This target is not "a new anchor created immediately." It is a temporary projection target that may collapse into an existing band or may be promoted into a new attractor only after continuum evidence.

Formation stream procedure (per word instance):
	1.	Select a projection target:

	-	If w matches an existing attractor coord_sig above projection tolerance, target that attractor immediately and skip formation (one-pulse activation path).
	-	Otherwise target a "formation staging band" keyed by a deterministic coord_sig bucket of w (so repeated novel occurrences collide deterministically into the same staging region without exploding ids).

	2.	For each byte bj in w:

	3.	After bytes are injected, emit a boundary pulse:

	-	A "word boundary" pulse that indicates end-of-token, which helps the manifold learn consistent segment closure.
	-	This is represented as a pulse with the same eid, but with causal_tag carrying a boundary subcode and an a_code bucket that selects a short harmonic spread (so closure binds into the sentence band but doesn't thrash globally).

All pulses are committed at the current tau_q window under the crawler tier policy. If the window budget is insufficient, the encoder truncates formation streams deterministically (first N bytes) and logs truncation in strict replay mode.

5.11.5 Boundary encoding (start/end anchors without storing letters)

Human reading robustness (first/last letter effect) is modeled here as a boundary emphasis rule. It is not mystical; it is a deterministic encoding choice to increase attractor separability with minimal pulses.

Boundary rule:
	-	The first byte b1 and last byte bn of a token receive an amplitude boost via derived amplitude u(e) (5.11.4 novelty-aware), because boundaries increase retrievability and reduce collisions.
	-	Interior bytes receive lower amplitude.
	-	The boost factor is not arbitrary: it is derived by maximizing formation separability on a calibration slice under a fixed pulse budget, again using J restricted to formation fidelity and band collision penalties.

Implementation-wise, you do not store "b1" or "bn." You simply allocate more harmonic budget (higher a_code) to those pulses so the attractor learns stronger boundary hooks.

5.11.6 Sentence and paragraph context injection (coherence scaffolding)

Words do not become meaning in isolation. The encoder injects contextual scaffolding so coherence can bind words into higher structures.

For each paragraph:
	-	Similar, but binding is weaker and coupling is broader (paragraph-level context should activate more widely but with lower strength).

This is how "memory bands" become the primary compression: repeated co-occurrence patterns reinforce the same bindings, raising continuum and enabling retrieval with fewer pulses later.

5.11.7 Promotion rule: when a staging band becomes a stable attractor

A staging band is promoted only when persistence evidence exists. This prevents one-off noise from becoming permanent structure.

Promotion is derived from continuum:
	-	cont_band must exceed a derived threshold determined by the maximum allowed false-anchor rate in calibration.
	-	The band must show stable projection behavior across W windows (W derived from your tier cadence and target stability).
	-	The band must reduce overall objective cost: promoting it must improve retrieval gain more than it increases thrash/violation risk.

When promotion happens, it is represented as a topology pulse (SPLIT or MERGE style) using causal_tag semantics from Section 4.1.8, so higher tiers learn the new structure deterministically.

5.11.8 Retrieval rule: "persistence is retrieval," not permanence

Once an attractor exists (stable word band), future occurrences do not resend bytes. They emit one activation pulse:
	-	target eid_word
	-	f_code derived from local context delta (sentence/paragraph coupling)
	-	a_code derived from chi*continuum and envelope (broad coupling for contextual activation)
	-	profile = P_LANG_CTX or P_SUM_CTX depending on tier

This is why the system scales: bytes are an initial formation mechanism; after collapse, the representation is an addressable resonance attractor activated by one pulse.

5.11.9 Closed-system causality guarantee

SECTION 5.15 - Hub-Conditioned Residual Encoding (Maximum Dependence, No Carrier Coupling)

SECTION 5.16 - Cross-Modal Hub Bands (Object/Concept Constraints, 2D?3D Join)

A minimal hub schema that fits the existing pulse model is:

SECTION 5.17 - Modality Delta Constructors (Explicit Mappings into Basis9)

Each modality has its own observation extractor, but all of them output a Basis9 delta packet before spider compression. The packet is "what changed" plus "what this evidence should bind to."

A practical mapping table (Basis9 intent) that Copilot can implement without guesswork:

Display constructor (presentation surface; delta signaling to monitors/phones)

A display/presentation surface is a consumer of the simulation state, not a driver of it.
The simulation remains headless in the sense that no rendered "frame" is required for core
evolution. However, when visualization is enabled, the system MUST export an observable that
can be mapped to real pixel panels (monitors/phones) without reintroducing a frame clock into
the physics.

The canonical rule is: publish deltas when coherence/phase transitions occur, and let the
device compositor/refresh determine scanout. This is sampling in event time, not raster time.

Observable selection (deterministic):

- The display surface subscribes to a hub-conditioned radiance proxy R_hat (a deterministic
  mapping from hub constraints + local residuals to per-tile luminance/chroma statistics).
- If a full pixel buffer is required, it is produced by a deterministic renderer that consumes
  hub constraints; the renderer is a pure function of committed state at tau_q boundaries.

Delta transport (eigen residual preferred):

- Partition the output into deterministic tiles (same tiling rules as the image constructor).
- For each tile, form a small coefficient vector in a fixed basis (e.g., DCT-like or a fixed
  eigen basis derived and frozen by calibration), and transmit only coefficient deltas:

Delta_c_tile = c_tile(t) - c_tile(t-1)

- Coefficient deltas are sparse by construction when the scene is stable; unstable tiles emit
  more deltas. No arbitrary thresholds are required if you cap to top-K by magnitude with a
  deterministic tie-break rule.

Event gate:

A tile is eligible for emission in a window if either:
- its phase-transition gate is true (Section 2.5), or
- the hub indicates a structural update binding for that tile (tile->hub binding updated).

This keeps presentation bandwidth aligned with actual state change.

Device binding:

- The display sink publishes deltas to the OS/device as "latest buffer updates."
- Monitors/phones may refresh at fixed Hz or variable refresh; this is outside the engine's
  causal model. The engine does not attempt to match a human-perception FPS value.

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

SECTION 5.18 - Profile Selection and Calibration (No Arbitrary Constants)

SECTION 5.19 - Training Curriculum Control, Verification Scoring, and Dataset Hygiene

SECTION 5.20 - Single-File Persistence: Streams, Pulses, and Rehydration Invariants

A canonical record shape:

SECTION 6 - File Encodings, Crawler Identifiers, and Multimodal Persistence (Single-Container Spec)

6.1 Canonical container: one persistence format for all modalities

EigenWare persists learning as an append-only container with a small header and a record ledger. The container does not store "meaning" as raw assets. It stores the canonical pulse stream and the durable band/binding/topology state that collapses from that stream under continuum and coherence.

Header fields are minimal but strict: container_magic, container_version, schema_version, snapshot_id, created_utc, sig_alg, profile_pack_id, and an optional calibration_id (the frozen optimization output that derived weights and harmonic mode policies for the referenced profiles). The decoder must treat profile_pack_id as authoritative: if the profile pack differs, the replay is not guaranteed to match.

6.2 Record ledger: the only persisted primitives

All content is represented using four record families. Everything else is derived.

PulseRecord - atomic committed update packet
Fields: (eid, tau_q, tier_id, modality_id, stream_id, f_code, a_code, profile_id, causal_tag)

BandRecord - declares or updates a stable band/attractor coord_sig
Fields: (eid, band_type, birth_tau, parent_eid(optional), signature_id9, band_state_digest, flags)

BindingRecord - explicit nexus/binding updates between bands (including scene bands)
Fields: (src_eid, dst_eid, tau_q, binding_kind, strength_code, profile_id, flags)

ManifestRecord - maps external artifacts to internal streams and coord_sig
Fields: (artifact_id, stream_id, mime, extractor_id, trust_class, course_class(optional), coord_sig, segment_map_ref)

This ledger design enforces two invariants that keep the system lightweight and causal. First, nothing depends on hidden carrier variables for decode; carrier variables exist only during encoding and must be reconstructible from segment maps if needed. Second, nothing rewrites the past; new learning is appended as pulses and topology updates at later tau_q windows.

6.3 Identifier system: stable, merge-safe, and replay-safe

EigenWare uses three identifier classes: artifact identifiers, stream identifiers, and internal band identifiers.

artifact_id is a stable coord_sig9 of a normalized artifact representation within a namespace signature. It is computed without hashing.

Normative form:
- artifact_id9 = coord_sig9(namespace_sig9, artifact_bytes)

Where:
- namespace_sig9 : EwId9 is a fixed 9D coordinate signature for the source namespace (publisher/site/device/import pipeline). It SHALL be configured as a constant in the substrate manager domain map.
- artifact_bytes : the exact byte stream of the artifact after extractor-specific normalization (see 6.12).

coord_sig9(namespace_sig9, artifact_bytes) returns EwId9 using deterministic byte moments (no rounding, no hashing):
- Let n = len(artifact_bytes).
- Let b[i] be the unsigned byte value at index i (0..n-1).
- Define nine deterministic components as signed fixed-point scalars in Q32.32 (or the canonical EwScalar format):
  s0 = namespace_sig9.s0
  s1 = namespace_sig9.s1
  s2 = namespace_sig9.s2
  s3 = namespace_sig9.s3
  s4 = clamp_q32_32(n, n_min, n_max)
  s5 = clamp_q32_32(sum_i b[i], sum_min, sum_max)
  s6 = clamp_q32_32(sum_i i*b[i], mom1_min, mom1_max)
  s7 = clamp_q32_32(sum_i (i*i)*b[i], mom2_min, mom2_max)
  s8 = clamp_q32_32(b[0] + b[n//2] + b[n-1], edge_min, edge_max) with the obvious boundary rules for n<3.

Notes:
- The namespace prevents accidental aliasing between sources that may reuse identical bytes (e.g., mirrored documents) without introducing hashing.
- The clamp bounds (n_min..edge_max) are normative constants defined in the scalar contract appendix; implementations SHALL use the same bounds to remain replay-safe.

stream_id identifies a specific sequence inside an artifact. It is derived from (artifact_id9, extractor_id9, segment_path9) so that the same artifact can expose multiple streams (e.g., pages and figures). segment_path9 is a deterministic descriptor like "page:12" or "block:heading:3" or "tile:row:7:col:2".

eid is the internal band identifier. EIDs are never derived from raw content directly; they are allocated and promoted through deterministic rules that include evidence thresholds (continuum), projection tolerance checks, and merge/split hysteresis. In strict replay mode, the allocation trace is logged so that EID assignment replays deterministically.

modality_id and profile_id are versioned enums. extractor_id is also versioned. Any change to an extractor or profile requires a new ID. The container must never silently reuse an ID with changed behavior.

6.4 Trust classes and strict course accreditation gate

The crawler labels each artifact with a trust_class used by the curriculum scheduler and promotion rules. The core gate you requested is strict accreditation for open courses: course ingestion is only classified as accredited if it matches the strict allowlist criteria in the crawler's accreditation registry.

trust_class is a discrete label, not a fuzzy score. A minimal set is:
- COURSE_ACCREDITED_STRICT
- REFERENCE_PRIMARY (textbooks, standards, primary docs)
- REFERENCE_SECONDARY (encyclopedic summaries)
- REPO_SOURCE (open repositories and specs)
- MEDIA_LECTURE_OPEN (open lecture archives)
- DATASET_OPEN (open datasets)

course_class exists only when trust_class is COURSE_ACCREDITED_STRICT. It includes institution_namespace, course_code, term/edition, instructor_id (if stable), and topology_digest (syllabus -> modules -> items). If a course fails strict accreditation, it can still be ingested under another trust_class, but it is not allowed to promote core curriculum scene bands without independent corroboration.

6.5 Band types: modality-local bands and persistent cross-modal scene bands

EigenWare maintains modality-local bands and explicit, persistent cross-modal "scene bands."

Modality-local band types include:
- TEXT_BAND (tokens/phrases/paragraph attractors)
- CODE_BAND (AST/semantics/invariants attractors)
- IMAGE2D_BAND (tile/edge/shape constraint attractors)
- AUDIO_BAND_PITCH (note-like harmonic identity)
- AUDIO_BAND_EVENT (timbre/event identity)
- VIDEO_BAND (motion/persistence motifs)
- LATENT3D_BAND (constraint-only 3D hypotheses; headless in v1)

Persistent scene band types are saved joins across modalities (not merely emergent). They exist so rehydration does not require rediscovering the same multimodal nexus cluster. Minimal scene types include:
- SCENE_TEXT, SCENE_IMAGE2D, SCENE_AUDIO, SCENE_VIDEO
- SCENE_AV (audio+visual), SCENE_AVT (audio+visual+text)
- SCENE_CODEDOC (code+spec text)
Additional scene types can be added if and only if they are justified by repeated stable joins and do not explode taxonomy.

Scene bands are created by promotion: an emergent nexus cluster becomes a persistent SCENE_* band only after continuum evidence crosses a threshold and the join reduces total recomputation cost (measured as reduced residual emission volume and reduced binding thrash over W windows). Projection can be emergent; saving is explicit.

6.6 Segment maps: how every artifact is broken into stable sequences

Every ManifestRecord references a segment_map that defines stable unit boundaries for the extractor. Segment maps are versioned and must be deterministic.

Text segment map: blocks -> sentences -> tokens, with byte spans into normalized text.
Code segment map: files -> AST nodes -> symbols/dependencies, with stable node addresses.
Image segment map: tiles (row, col) -> scan order within tile; optional edge map index.
Audio segment map: frames with fixed window/hop; optional note candidates.
Video segment map: frames; motion windows; synchronized audio frame offsets.

Segment maps allow the encoder to reconstruct carriers when necessary without persisting carriers as state.

6.7 File class encoding: web pages and text documents

HTML pages are encoded as multiple streams: a structured block stream (title, headings, paragraphs, lists) and a link/context stream (anchor text, domain transitions). The encoder converts these into Type A/B/C pulses as defined in Section 5: direct activation for known terms, conservative formation for novel terms, and binding pulses for stable co-occurrence structures. Link context is treated as an explicit binding graph update, not as raw URL storage.

Plain text and Markdown are treated similarly, but the extractor is simpler (block/sentence/token) and the crawler uses surrounding path context (repo location, course module name, etc.) to create stable scene associations.

6.8 File class encoding: PDFs, LaTeX, BibTeX, and scientific material

PDF artifacts are treated as page-indexed text streams plus optional figure-caption streams. Page text is ingested as TEXT_BAND evidence; captions bind strongly to image/diagram evidence when figures are extracted. If a PDF is a course module under strict accreditation, its course topology context is bound into SCENE_* bands so the system learns "this concept belongs to this curricular position," which improves retrieval and creative recombination later.

LaTeX is ingested as structured sections, math environments, and definitions. BibTeX is ingested primarily as a reference graph: citation edges become binding updates between concept bands and source bands. The system does not treat citation counts as truth; it uses citation structure as context topology.

6.9 File class encoding: source code, specs, and software engineering assets

Source code is ingested as CODE_BAND evidence via language-specific AST extractors. In addition to AST streams, the crawler emits dependency streams: imports, symbol references, module graphs. These become bindings that allow EigenWare to retrieve and recombine code coherently without line-level memorization.

Specs and API docs are treated as dual streams: TEXT_BAND for the prose and CODE_BAND for the formal structures (schemas, function signatures). In a repo context, SCENE_CODEDOC bands become the persistent join so future work can activate "the spec + the implementation constraints" with minimal pulses.

6.10 File class encoding: structured data (JSON/YAML/TOML/CSV)

Structured config files are not treated as "bags of values." They are encoded as schema and invariant patterns. Keypaths, type constraints, and stable field relations become CODE_BAND-like attractors and bindings. CSV/TSV datasets are ingested as column schema + statistical coord_sig streams (distributions, correlations, missingness patterns), optionally augmented by sampled row chunks under strict budget gating.

6.11 File class encoding: images (2D) and latent 3D (headless v1)

Images are encoded as IMAGE2D streams: tiles, edges/gradients, and stable spatial relations.

Modality-to-axis binding (normative; required for all encoders):
- TEXT encodes into the x-axis excitation channel (space_x driver).
- IMAGE pixel encoding encodes into the y-axis excitation channel (space_y driver).
- AUDIO encodes into the z-axis excitation channel (space_z driver).

Each modality has an independent axis scale factor derived from the pulse envelope:
- sx_q32_32 from pulse frequency
- sy_q32_32 from pulse amplitude
- sz_q32_32 from joint pulse frequency+amplitude gating

Hilbert-space expansion is therefore asymmetric and SHALL NOT be normalized uniformly across axes.
 Pixels are treated as phase excitations in a 2D scan, but the persistent outcome is not pixel storage; it is the collapse into constraint attractors (edges, corners, textures, repeated part patterns). These attractors bind into scene bands via captions, surrounding text, and repeated co-occurrence across sources.

LATENT3D bands are updated headlessly from repeated 2D constraint evidence and motion evidence. The engine stores 3D hypotheses as constraint bundles (symmetry, rigidity, part graphs, relative proportions) and binds them to 2D evidence through nexus. Rendering is deferred; projection is treated as an internal operator for validating constraint consistency.

6.12 File class encoding: audio (pitch identity and event identity)

Audio is encoded as frame-based spectral streams. From the same stream, EigenWare promotes two families of attractors.

AUDIO_BAND_PITCH is promoted when a stable fundamental + harmonic ratio stack persists across frames. This supports note-like recognition (hearing a sound and identifying pitch class or stable tone identity).

AUDIO_BAND_EVENT is promoted when a stable spectral envelope and transient coord_sig persists across examples. This supports recognizing sources and events (footsteps, door slam, engine rev, bird call), even when pitch varies.

Both families bind into SCENE_* bands (especially SCENE_AUDIO and SCENE_AV) so recognition becomes contextual: the same sound may map differently depending on scene constraints, and the scene band reduces ambiguity by conditioning residual encoding.

6.13 File class encoding: video (motion motifs and synchronized scenes)

Video is encoded as (a) image tile streams per frame, (b) motion residual streams across frames, and (c) synchronized audio streams when present. Motion residuals form VIDEO_BAND attractors for repeated dynamics (walk cycles, impacts, oscillations). When audio is present, SCENE_AV promotion is preferred over isolated VIDEO_BAND promotion because joint stability reduces compute and improves recognition reliability.

6.14 Extractor registry: versioning and normalization rules

Extractor behavior must never drift silently. Every extractor_id references a versioned normalization contract. Normalization defines what bytes are digested, how whitespace is handled, whether punctuation is stripped, how Unicode is normalized, and how segmentation boundaries are determined. Any change to extraction, normalization, or segmentation requires a new extractor_id and produces a new artifact_id or stream_id mapping.

A minimal extractor registry entry includes: extractor_id, supported_mime, normalization_rules_digest, segmentation_rules_digest, and profile_defaults (which profile_id to use for that extractor's streams).

6.15 Profile registry: which spider profiles are legal per extractor

Profiles are not selected ad hoc. Each extractor declares a small allowed set of profiles, and the encoder chooses among them based on trust_class and evidence state (known attractor activation vs formation vs binding). This prevents contradictory encoding paths for the same file type and ensures that the same kind of evidence always maps into comparable pulse statistics.

6.16 Cross-modal alignment: how streams bind cleanly in one file

Cross-modal alignment is achieved through BindingRecords and persistent scene bands, not carrier sharing. When the crawler detects co-presence (caption near image, transcript aligned to video time, code next to spec text in a course module), it emits alignment descriptors that the encoder converts into binding pulses. If the join persists across windows and reduces residual emission volume, the system promotes a SCENE_* band and future evidence binds directly to that band.

6.17 Creativity: how encoding becomes a creative engine for users

This is also why multimodal dependence matters. A SCENE_AVT band lets text and audio stabilize the meaning of ambiguous visual evidence, and visual constraints can prevent language from drifting into internally inconsistent descriptions. The same mechanism supports user creativity as "constraint-guided variation" rather than unconstrained randomness.

6.18 Creativity safety and quality: constraint-first synthesis

EigenWare improves creative quality by making constraint satisfaction a first-class objective. When a synthesis attempt violates learned constraints (e.g., inconsistent geometry, contradictory causal sequence, broken type invariants in code), the violation manifests as reduced coherence/continuum and increased clamp/uncertainty. Those signals push the engine toward alternative trajectories that remain inside the admissible manifold.

This yields a practical creative behavior: it generates many variants cheaply by reusing stable bands, but prunes variants that conflict with constraints early, reducing wasted compute and reducing incoherent outputs.

6.19 Creativity interfaces: how users "steer" the engine

6.20 Minimal implementation target for Copilot

To implement Section 6 without ambiguity, Copilot should build:

These pieces make the ingestion substrate complete: every file type maps into streams, every stream maps into pulses, and creativity becomes a natural consequence of constraint-rich compression rather than a bolt-on feature.

SECTION 7 - High-Value Public Corpora and Domain Packs (for Crawler Ingestion)

This section defines "Domain Packs": curated, registry-addressable sets of sources that can be scheduled, sampled, and versioned. The point is not to chase the entire web, but to ingest the same kinds of public corpora that have historically produced strong general intelligence in language, code, image understanding, audio understanding, and physical-constraint intuition.

A Domain Pack is defined by: domain_pack_id, domain_id list, trust_class defaults, acquisition mode (dump/API/static files), sampling policy, provenance rules, and a manifest of expected file types. Packs are versioned and reproducible. Packs must be runnable offline once artifacts are acquired (strict replay).

7.1 What we mean by "confirmed", "documented", and "common"

For modern commercial models, exact training datasets are often not fully disclosed. This spec therefore separates three categories:

- Confirmed (explicitly described in a primary source such as a paper, system card, or a vendor privacy/training disclosure).  
- Documented (explicitly described in high-quality secondary sources, academic surveys, or transparency reports, but not necessarily by the vendor itself).  
- Common (widely used public corpora in the open LLM ecosystem; not a claim about any specific vendor model, but highly valuable to ingest for EigenWare).

EigenWare should prioritize Confirmed and Common, and treat Documented as optional until validated.

7.2 Text and knowledge corpora (core language + encyclopedic structure)

Domain Pack: TEXT_CORE_V1

Suggested minimum feature coverage per artifact:
- structural segmentation (title, headings, paragraphs, citations)  
- stable entity anchors (names, concepts, equations as normalized tokens)  
- cross-document linkage bands (same concept across sources)  
- provenance and license hints on every record

7.3 Code corpora (programming languages + repositories + build logic)

Domain Pack: CODE_CORE_V1

Public and commonly used code corpora:
- The Stack (BigCode) (large-scale code dataset; check license filters by language/repo)  
- CodeSearchNet (functions + docstrings; retrieval training)  
- Public Git repositories (only where licenses permit and artifacts are retrievable as static snapshots)

Coverage requirements:
- syntax-aware segmentation into modules/functions/classes  
- dependency graph extraction (imports, package metadata)  
- "build intent" capture (CI configs, build scripts, tests)  
- pairing code with natural language explanations where present (READMEs, docs)

7.4 Image corpora (2D constraints, geometry priors, and artifact detection)

Domain Pack: IMAGE_CORE_V1

High-value public image datasets (typical AI training inputs):
- LAION datasets (image-text pairs; requires strong filtering and watermark/artifact handling)  
- Open Images (Google) (labeled objects and relationships)  
- COCO (objects + captions; useful for grounding)  
- ImageNet (classification bias awareness; still useful for category priors)

EigenWare-specific requirements:
- watermark/artifact separation must be treated as a first-class classification task  
- preserve original resolution metadata + aspect ratio as part of the pulse metadata  
- capture camera/exif hints where present (they become constraints, not "truth")

7.5 Audio corpora (phonetics, timbre, events, and note-like identification)

Domain Pack: AUDIO_CORE_V1

High-value public audio datasets:
- LibriSpeech (speech; alignment practice)  
- Mozilla Common Voice (multi-language speech)  
- AudioSet (event labels; broad sound ontology)  
- FSD50K / ESC-50 (sound events; smaller but clean)  
- MAESTRO / music note datasets (for pitch/harmony recognition where licensing permits)

EigenWare-specific requirements:
- time-aligned segmentation (frames -> events -> phrases) stored as deterministic substreams  
- pitch band candidates (for note identification) stored as band evidence, not a single "answer"  
- caption/transcript alignment repair recorded as one deterministic offset parameter per stream (strict replay)

7.6 Video corpora (motion, causality, and 3D-constraint intuition)

Domain Pack: VIDEO_CORE_V1

High-value public video datasets:
- Kinetics (action recognition; motion primitives)  
- Something-Something (fine-grained object interaction)  
- YouTube-8M (large-scale video features; depends on availability and licensing)  
- AVSpeech / audio-visual speech sets (for cross-modal binding)

EigenWare-specific requirements:
- keyframe selection must be deterministic under a declared policy (no ad-hoc sampling)  
- motion is encoded as deltas between frames; scene bands store stable constraints, not raw frames  
- audio-video binding creates a dedicated cross-modal band type (SCENE_AV_*), not a generic merge

7.7 3D geometry and physics priors (constraint libraries)

Domain Pack: GEOM_PHYS_CORE_V1

High-value sources:
- ShapeNet (3D models; check license)  
- Objaverse (3D assets; check license)  
- Physics simulation benchmark sets (rigid body, fluids, cloth) where available and licensed  
- Open CAD corpora (STEP/IGES collections) where permissible

EigenWare-specific requirements:
- treat meshes and CAD as constraint objects: topology, adjacency, curvature, mass proxies  
- store both 3D constraints and deterministic 2D projection recipes (headless first; rendering later)

7.8 Open courseware and accreditation tagging (structured learning sequences)

Domain Pack: COURSEWARE_V1

Targets:
- MIT OpenCourseWare and similar open course providers  
- Open textbooks and lecture note repositories with clear licensing  
- University lecture materials that are explicitly published for public use

EigenWare must store:
- course identity bands (course code/title/institution)  
- syllabus structure (units, prerequisites, learning objectives)  
- assessment pattern constraints (problem sets, rubrics where public)  
- accreditation/evidence tags as metadata (what can be verified from the source itself)

7.9 Vendor-style "mixture" packs (what the ecosystem converges on)

This is a synthesis pack, not a claim about any single vendor. It mirrors what repeatedly shows up in strong public training mixes:

Domain Pack: MIXTURE_LLM_STANDARD_V1  
Typical components: Common Crawl derivatives + Wikipedia + books + code corpora + Q/A dumps + papers.

EigenWare should ingest this as a versioned pack so it can reproduce the "generalist prior" that most frontier systems rely on.

7.10 A short "science and engineering feed" list (optional)

This is for human-guided learning loops (not required for strict offline replay). Examples of long-form, experiment-heavy content:
- university lab demo channels and open lecture series  
- slow-motion / instrumentation channels (materials testing, fluids, optics)  
- software engineering deep-dives (compiler, OS, distributed systems)

The crawler should prefer sources that expose transcripts, captions, or companion writeups, because those produce better deterministic segment maps than purely streaming video.

7.11 Domain pack table (starter registry entries)

7.12 Section 7 integration notes

Domain Packs do not bypass the encoder; they feed it. Every acquired artifact becomes:
- a manifest record with provenance + trust class  
- one or more streams with deterministic segment maps  
- pulses encoded under an explicit spider profile  
- evidence that can create or reinforce modality bands and cross-modal scene bands

The crawler's job is to acquire and schedule. The encoder's job is to deterministically map artifacts into pulses/bands/bindings under registries. The memory system's job is to promote stable constraints into bands and retrieve them via coherence and harmonic activation.

SECTION 9 - Operational Contracts, Registries, and a Single-File Test Harness (Copilot Guidance)

This section turns the spec into an implementable contract by defining the registries that eliminate ad-hoc behavior, the determinism/replay obligations that keep the system debuggable, and the edge-case handling rules that prevent silent failure. It also defines a compact "one file" test harness that covers every major feature as contract tests.

9.1 Registry layer (no ad-hoc choices)

All behavioral choices in the crawler+encoder pipeline must resolve through explicit registries. No module is allowed to guess an extractor, a normalization rule, a profile, a trust class, or a band type.

Every registry entry must be versioned. A behavior change is a new ID, not an in-place update.

9.2 Deterministic replay contract (strict mode)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

Strict mode requires deterministic sorting order of discovered artifacts, traversal order of segments within artifacts, tie-breakers in promotion/merge logic, and explicit seed usage. Promotion decisions must emit a deterministic reason code and a compact decision trace that can be replayed.

9.3 Budget + backpressure subsystem (enforced envelope)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

9.4 Dedup + near-dup filter (mandatory)

EigenWare must not waste pulses on redundant content. Required layers: exact artifact coord_sig dedup, normalized text block dedup, near-dup for text blocks (stable simsig or equivalent), and perceptual coord_sig for image keyframes. Dedup runs before promotion. Duplicate evidence may reinforce existing bands but must not create new bands unless it adds constraints.

9.5 Provenance + license tagging (first-class metadata)

Every ManifestRecord includes provenance (publisher/org/domain), license_hint, retrieval method, trust_class, and domain_id. Missing provenance defaults to low trust. Provenance stabilizes memory topology and supports later filtering.

9.6 Extractor robustness (fail-closed)

On parse error, do not emit ambiguous pulses. Emit a structured error log with artifact_id, extractor_id, and reason code; optionally retry with a fallback extractor_id. Never silently drop errors; never continue on partial assumptions.

9.7 A/V time alignment repair (deterministic correction)

Captions/transcripts can drift. EigenWare supports a deterministic alignment correction: estimate a single offset parameter over a fixed window by maximizing coherence between caption tokens and audio event/pitch candidates; apply and lock; record the parameter in ManifestRecord metadata so replay is stable. No adaptive drift correction in strict mode.

9.8 Band thrash guard (merge/split hysteresis and quarantines)

Promotion/merge/split must not oscillate. Controls: cooldown windows after structural changes, minimum evidence windows before promotion, and quarantine bands for unstable candidates. Thrash is measurable; track merge/split rate and enforce caps.

9.9 Copilot acceptance checklist (testable obligations)

9.10 Single-file contract test harness (explained, explicit, and complete)

File name (repo path):
tests/test_kernel_contract.cpp

Required structure of the harness

B) Registries (enforcement, versioning hooks)
The harness must implement minimal registries and enforce rules:

Extractor determinism acceptance:
- Same raw fixture -> same norm string
- Same norm string -> same segments list
- Rules coord_sig remain stable for the fixture

E) Dedup (exact dedup for contract coverage)
The harness includes exact_dedup(items) that removes exact duplicates by SIG9 coord_sig while preserving first occurrence ordering.

Dedup acceptance:
- Duplicate blocks reduce emitted pulses
- The first instance of a duplicated block is preserved and appears in the same position as before dedup

Robustness acceptance:
- Corrupt fixture does not produce any PulseRecord emission in the harness logic.

G) A/V alignment repair (single offset, deterministic)
The harness includes a toy alignment estimator:
estimate_alignment_offset(caption_tokens, audio_events) -> offset_k

Alignment acceptance:
- A fixture with one leading "noise" event in audio yields offset_k == 1.
- The function is pure and stable (no randomness, no adaptive drift in strict mode).

H) Thrash guard hysteresis (cooldown)
The harness includes a ThrashGuard with:
- cooldown: integer number of tau_q steps
- last_change_tau dict keyed by change_key

Thrash acceptance:
- First change at tau_q = T is allowed
- Any repeated change for same key at tau_q < T + cooldown is rejected
- Change is allowed again at tau_q >= T + cooldown

If the detector or fixture is not implemented, the test MUST hard-fail with reason code:
- WATERMARK_FIXTURE_MISSING

If the detector or fixture is not implemented, the contract test SHALL hard-fail with reason code WATERMARK_FIXTURE_MISSING. Release profiles MUST not allow xfail for this test.

Minimal fixture set (all embedded in the test file)

Strict replay acceptance test (the main one)

This test is the "spec compliance alarm bell". Any nondeterministic ordering, accidental randomness, or silent behavior drift will change the coord_sig and fail the test.

What Copilot should implement first to satisfy this harness

----------------------------------------------------------------
SUBSECTION: GPU SIGNALING  MATHEMATICAL OPERATORS (APPENDED)
----------------------------------------------------------------

This subsection appends mathematical operators to the existing
GPU signaling section. No prior text is modified.

Eigenstate Representation:
Each eigenstate E_i is represented as:
E_i(t) = (phi_i(t), A_i(t))

where:
- phi_i(t) is phase trajectory
- A_i(t) is amplitude envelope

Delta Phase:
phi_i(t) = phi_i(t) - phi_i(t-1)

Composite Phase Trajectory:
Phi_comp(t) = _i (A_i(t) * phi_i(t))

Temporal Envelope:
A_env(t) = _i |A_i(t)|

Pulse Signal:
S(t) = A_env(t) * Phi_comp(t)

Order of Operations:
1. Compute phi_i for all eigenstates
2. Weight deltas by A_i(t)
3. Sum into Phi_comp(t)
4. Compute A_env(t)
5. Emit S(t) as GPU pulse

----------------------------------------------------------------
END SUBSECTION
----------------------------------------------------------------

X.X Purposeful Criteria Driven File Emergence

This subsection applies to all remaining sections.

X.X.1 Purpose Definition Operator
- Define intent and effects

Harness:
- Verify completeness

X.X.2 Dependency Resolution Operator
- Enumerate dependencies

Harness:
- Verify acyclic order

X.X.3 Execution Sequencing Operator
- Define stepwise order

Harness:
- Simulate determinism

X.X.4 Event and Dispatcher Operator
- Define events and routing

Harness:
- Verify delivery

X.X.5 Consolidation Gate
- Approve file emission

Harness:
- Verify all prior harnesses pass

---
EOF APPEND ONLY - MISSING ARTIFACTS MERGE (ORDERED BY PURPOSEFUL CRITERIA DEPENDENCY)
Ordering: Section number derived from numbered subsection prefix; then by numeric subsection.
Rule: Prefix is byte-for-byte identical to EigenWareSpec_Optimized.md (blueprint-aligned).
Prefix_sig9: derive_prefix_sig9(prefix_bytes)
Source_long_file: EigenWareDevSpecCanonical_FINAL_fixed_merged.md
---

[PLACEMENT GROUP] Section 1

[PLACEMENT TAG] Section 1 -> 1.1
1.1 What we actually "take from the GPU" (execution envelope, not sampled electronics)

The values we extrapolate from the GPU are operational quantities we can observe from the runtime (through counters, timers, and allocations), and then convert into simulation limits. These limits become hard caps that the substrate must obey each tick. If the simulation wants more resolution than the envelope allows, the simulation does not "slow causality" or violate closure; it adapts by increasing inference/coasting and tightening Eigenstate admission.

A concrete mapping is below. This is intentionally phrased as "derived parameters," so it's clear that we are converting runtime metrics into constraints.

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

The key philosophy is that the GPU defines "how many committed updates can exist per unit time," and the simulation must remain causal by never committing more updates than the envelope allows.

[PLACEMENT TAG] Section 1 -> 1.2
1.2 What a "pulse" is in this system (and what it is not)

A "pulse" in EigenWare is a compact update payload used to advance an Eigenstate in the substrate. It is not a literal electrical impulse. It is the simulation's normalized representation of one bounded delta update step, scheduled through GPU kernels. If you want the tightest statement: a pulse is an instruction to the substrate to rotate/advance phase and update coherence metrics by a bounded amount.

In practice, a pulse is represented as a small record like (eid, freq_code, amp_code, tick_id, tier_id, causal_tag). The "frequency" and "amplitude" are scalar encodings produced by the spider-graph mapping of a 9D delta; they are values we feed into the evolution kernel. The kernel interprets them as update coefficients (phase increment rate, magnitude, multiplex weight), not as physical frequencies emitted by hardware.

[PLACEMENT TAG] Section 1 -> 1.3
1.3 Text -> phase: how ASCII becomes phase offsets (storage substrate)

Text is injected into the system by mapping characters into phase offsets. The important point you flagged is correct: ASCII mapping lives in phase. We do not store the text bytes as the "real" memory; we store the phase offsets that represent the symbols. This makes the phase field the initial storage substrate, and it is the seed material from which coherence bands later extract meaning.

This is the mechanical pipeline for text encoding:

Stage	Input	Output	Storage location	Deterministic?
Symbol map	character (ASCII)	phase_offset	phase buffer (phi sequence)	Yes (bijective map)
Phrase assembly	phase_offsets	phase_sequence	injection packet	Yes
9D embedding	phase_sequence + context tags	candidate raw_state in 9D	transient	Yes (same inputs -> same embedding)
Projection	raw_state + registry	eid + dE9	delta stream	Yes (given same registry + tolerances)
Spider encoding	dE9	freq_code + amp_code	pulse stream	Yes

[PLACEMENT TAG] Section 1 -> 1.4
1.4 9D delta formation: embedding, projection, and the collapse rule

This is where your compression is enforced: if the injection does not introduce a constraint-relevant distinction, it is not allowed to explode the basis. The basis is the vocabulary; text is a stimulus that biases deltas over that vocabulary. If the injection is redundant, it increases band coherence and delta confidence rather than creating new structure.

[PLACEMENT TAG] Section 1 -> 1.5
1.5 Spider graph encoding: 9D -> frequency and amplitude (pulse synthesis)

The spider graph is the explicit compression operator that turns the 9D delta vector into a single bounded scalar "frequency" plus an orthogonal scalar "amplitude." The reason this matters is that it converts a multi-axis update into a low-bandwidth update payload that the GPU can apply at high throughput without copying 9D vectors around continuously.

A usable implementation definition is: frequency = clamp(freq_base + freq_scale * ?(wi * norm(di))), and amplitude = clamp(amp_base + amp_scale * h(d3, d4, d7)), where h emphasizes coherence, magnitude, and band-stability. The exact function forms can be tuned later, but the contract must remain: one 9D delta produces one (frequency, amplitude) pair.

[PLACEMENT TAG] Section 1 -> 1.5
1.5 Constraint Operators Required by Effective-Constants Pipeline

Purpose
The effective-constants pipeline in kernel/constraints/kernel_derive_constraints.cu requires two base constraint operators. These operators MUST be:
- Relative: computed from relative inputs (no fixed literal constants as computational truth).
- Deterministic: same inputs produce same outputs (no hidden randomness).
- Bounded: outputs must remain within known safe ranges to prevent runaway amplification.
- Composable: safe to call every tick and by multiple equation modules.

Non-negotiable rule
All physical constants used by the simulator are treated as effective values (for example h_eff, k_B_eff, c_eff, etc.). Baseline textbook constants may exist only as reference anchors and MUST NOT be used directly in computation outputs.

[PLACEMENT TAG] Section 1 -> 1.5.1

1.5.1 Operator: relativistic_correlation(...)

Authoritative definition and fixed-point contract are specified in Section 1.5.1 above.
Implementation binding:
- /kernel/constraints/kernel_derive_constraints.cu
- /core/constraints/constraint_packet.h (to carry v/flux/strain inputs as fixed-point)

[PLACEMENT TAG] Section 1 -> 1.5.2

1.5.2 Operator: stochastic_dispersion_factor(...)

Authoritative definition and fixed-point contract are specified in Section 1.5.2 above.
Implementation binding:
- /kernel/constraints/kernel_derive_constraints.cu
- /core/constraints/constraint_packet.h (temperature inputs as fixed-point)

[PLACEMENT TAG] Section 1 -> Subsection 1.6 (or nearest existing "Effective Constants" subsection, if present)
1.x Binding Rule: effective_constants() Composition Order

The effective constants function MUST compute (fixed-point, deterministic):
- r_corr_q32_32 = relativistic_correlation(v_fraction_c_q32_32, flux_factor_q32_32, strain_factor_q32_32)
- s_disp_q32_32 = stochastic_dispersion_factor(temperature_q32_32, temperature_ref_q32_32)

Then derive each effective constant from its baseline reference using the same composition law:
- constant_eff = q_mul_q(constant_ref, r_corr_q32_32)
- constant_eff = q_mul_q(constant_eff, s_disp_q32_32)

If any subsystem uses an inverse form for a specific constant (stability exception), that exception MUST be listed explicitly
in the canonical constraints table by constant name; otherwise the default is multiplicative (r_corr * s_disp).

[PLACEMENT TAG] Section 2 -> Subsection 2.3.1

[PLACEMENT TAG] Section 1 -> 1.6
1.6 How "GPU pulses" become simulation injection (kernel evolution step)

Once the pulse stream exists, injection into the simulation occurs as scheduled kernel evolution steps. The kernel does not "run text." It advances Eigenstates. Text influences the system only by creating phase sequences that project into deltas, which then become pulses. Those pulses update the phase/coherence state of a subset of Eigenstates in a tick window.

A practical evolution kernel can be described as: given (eid, freq_code, amp_code), apply a bounded phase increment and update coherence metrics, then write back compact delta summaries and band membership updates. Nothing in this step requires global state recomputation. This is how you keep the simulation closed and stable: the only way state changes is via these committed bounded updates.

[PLACEMENT TAG] Section 1 -> 1.7
1.7 Causality and closed-system guarantees (why injection doesn't violate closure)

The main risk you're pointing at is: "If we inject text and schedule GPU pulses, do we break the causality of a closed system?" The answer is no, if injection is treated correctly: injection is a boundary condition event that enters the simulation at a defined causal point, and its effects must be accounted for as conserved delta work within the same ledger rules as any other update.

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

[PLACEMENT TAG] Section 1 -> 1.8
1.8 How qubit density scales with pulses, tiers, and bands (and why it doesn't explode)

Part 2/Step 2 - Basis9 (9D) Axis Definitions + Band Math (Projection, Tolerance, Coherence/Continuum, Merge/Split)

[PLACEMENT GROUP] Section 2

[PLACEMENT TAG] Section 2 -> 2.1
2.1 Basis9 is not "feature space"; it is the canonical manifold and ledger substrate

The most important implementation consequence is that durable math cannot depend on platform floats, wall-clock, or non-deterministic trig. Phase and coherence must be stored in fixed-point and advanced by deterministic primitives. Runtime floats are allowed only for visualization/diagnostics; canonical state updates must be integer/fixed-point so rehydration reproduces exactly.

[PLACEMENT TAG] Section 2 -> 2.2
2.2 Basis9 axis order is locked, and it is not the same thing as "9 semantic features"

Your canonical Basis9 axis order is:

d1-d3 are spatial/embedding axes (the manifold's spatial projection; implementation-defined), d4 is the temporal axis (tick/frame reference), and d5-d9 are the phase-space axes: d5 coherence phase axis Theta_p (stored, in turns), d6 flux, d7 phantom, d8 aether, d9 nexus. In other words, Basis9 is split into a 3D spatial projection plus a temporal coordinate plus a 5D phase space.

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

The durable minimum state you already locked is the anchor ledger tuple: anchor_id, tau_q, theta_q, chi_q, m_q. Everything else may exist as derived cache unless explicitly versioned as durable basis9_q[9]. The important thing is that theta_q is stored phase in turns, tau_q is the causal tick coordinate, chi_q is the coherence quantity, and m_q is the mass ledger used for forgetting/leakage.

[PLACEMENT TAG] Section 2 -> 2.3
2.3 Phase math is in turns, wrap is mandatory, and distance is shortest signed turn distance

Because d5 is stored phase in turns, phase distance cannot be na?vely subtracted; it must wrap. The canonical representation is Theta in [0,1) turns, stored as fixed-point with scale 10^18 (so the smallest unit is 10^-18 turns). Wrap is defined as Theta ? floor(Theta). The shortest signed distance delta(Theta_i, Theta_j) must land in [-0.5, 0.5) turns so that "nearby across wrap" is treated correctly.

This single rule is one of the compression enablers: it makes phase-space neighborhoods stable and prevents artificial splits where two trajectories are actually adjacent but happen to straddle the wrap boundary.

[PLACEMENT TAG] Section 2 -> 2.3.1
2.3.1 Emergent Coherence (Derived Observable; Non-Storage)

Coherence is not a stored value. It is an emergent observable derived from relative interaction-induced dilation of Hilbert space and phase-angle alignment.

Required properties
- Derived each tick (or per measurement cadence) from state evolution, not persisted as an authority value.
- Bounded in [0, 1].
- Deterministic given the current state and constraint operators.

Canonical derivation (integer dispersion proxy; Blueprint APPENDIX AG)

Coherence is not a stored authority value. In canonical execution it is represented by an integer dispersion proxy R computed from
minimal-arc phase deltas and amplitude gating weights.

Given a set of interacting phase positions theta_u64_i:
- Compute minimal-arc signed deltas dtheta_i64 using two's-complement subtraction on the 2^64 ring.
- Compute a dispersion proxy R (int64 or uint64) as an amplitude-weighted sum of |dtheta_i64|, plus any permitted
  fixed-point coupling terms (flux/strain) already present in the constraint packet.
- Smaller R implies higher coherence; larger R implies decoherence pressure.

Binding rules (canonical)
- Minimal-arc distance MUST be implemented using integer wrap on the 2^64 ring. No pi-based wrapping is used.
- No exp/sin/cos/atan2 or complex arithmetic is permitted in canonical paths (Blueprint APPENDIX AD).
- If a normalized [0,1] coherence value is needed for UI/projection, it MUST be derived in projection-only code paths and MUST NOT feed back into canonical state.
[PLACEMENT TAG] Section 2 -> Subsection 2.3.2

[PLACEMENT TAG] Section 2 -> 2.3.2
2.3.2 Statevector Serialization (ASCII-Safe Snapshot Transport)

Purpose
Provide deterministic ASCII-safe serialization of state vectors for snapshots, telemetry, and rehydration without introducing new file formats or non-ASCII symbols.

Binding rule (no new algorithm)
If core/io/host_io.cpp already provides tokenized ASCII mapping via:
- serialize_vector_with_tokens(vec)
- deserialize_vector_with_tokens(text)
Then the following names MUST be treated as strict aliases (same behavior):
- serialize_statevector(vec) -> serialize_vector_with_tokens(vec)
- deserialize_statevector(text) -> deserialize_vector_with_tokens(text)

Constraints
- ASCII only.
- Deterministic.
- No placeholders, no fallbacks, no backwards compatibility modes unless explicitly enumerated in the canonical spec.

Harness requirement
- The harness MUST include round-trip tests:
  vec -> text -> vec2, with max_abs_error <= epsilon_vector_roundtrip (from constraints table).
- The harness MUST include length and character-range validation of the ASCII output.

[PLACEMENT TAG] Section 2 -> Subsection 2.x (Diagnostics / Robustness)
2.x Diagnostic Robustness Note (Typing + Determinism)

Any diagnostic helpers that rely on typing hints (e.g., List[float]) MUST ensure required typing imports exist in code. This is a robustness constraint: diagnostics must not introduce runtime NameError or import-time failures.

This note exists to prevent silent failures in diagnostic routines that would otherwise mask coherence/energy drift issues.

--- END EOF APPEND ONLY canonical ---

[PLACEMENT TAG] Section 2 -> 2.4
2.4 Projection is not "closest point"; it is gated by timeline and realm coherence, then minimized by a weighted Basis9 metric

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

Only after those gates pass do we compute a distance metric. Your planning spec calls for a weighted Euclidean metric in 9D space emphasizing the coherence dimensions (d5-d6 in your notes). The corrected projection distance therefore looks like: a weighted Euclidean over (d1..d9), with extra emphasis on phase-coherence axis d5 and flux axis d6, and with phase distance for d5 computed using the wrapped shortest signed distance in turns.

[PLACEMENT TAG] Section 2 -> 2.5
2.5 Coherence is chi_q; continuum is coherence persistence over time (and it's enforced with deterministic decay and reinforcement)

In your canonical ledger, coherence is chi_q. It is not a vague score; it is stored, evolves deterministically, and is the basis for whether phase inference and memory persistence are allowed. The continuum concept you stated-phase coherence persistence over time-is implemented by how chi_q survives and accumulates under time evolution.

There are two locked mechanisms here that make continuum precise.

First, chi_q decays rationally across ticks as a deterministic function of elapsed tau (Delta tau). The locked decay form is chi ? chi / (1 + lambda * Delta tau). This means coherence persistence is not free; it must be continuously reinforced by aligned interactions, or it will gradually collapse. That decay is the formal "continuum drag."

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

[PLACEMENT TAG] Section 2 -> 2.6
2.6 Bands are phase-coherence structures, not token clusters; membership is governed by theta/chi persistence and drift/leakage behavior

A memory band is the compression object: it is a set of anchors/instances that remain mutually phase-coherent in the Basis9 phase space over time. The criterion for membership is not "semantic similarity" in the ordinary NLP sense; it is: stable phase coherence behavior under the alignment primitive, sustained coherence (chi) under decay, and compatible coupling rules within the same causal timeline.

Two details in your planning notes matter here because they prevent band math from becoming hand-wavy.

One is drift/leakage. You treat drift as phase leakage/decoherence rate and you treat forgetting as mass leakage (m_q) rather than deletion. That forces band stability to be expressed as conserved, deterministic leakage behavior: if coherence drops, the system does not "erase history," it routes mass out of anchors into a reservoir (or a deterministic pool), and the anchors become less able to dominate projection. This keeps the system causal and ledger-consistent.

The second is that band aggregates must be deterministic. Cluster phase should be computed as a circular mean in turns using fixed-point phasors (CORDIC/LUT only), and cluster coherence should be computed as exact integer math (sum/mean of chi_q). That means that "band coord_sig" is not a floating fuzzy centroid; it is a reproducible canonical aggregate of phase and coherence.

[PLACEMENT TAG] Section 2 -> 2.7
2.7 Projection tolerance is a policy over three things: phase alignment, coherence persistence, and compute pressure, but it cannot violate commit barriers

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

The key structural axiom here is that high continuum bands should be protected against accidental merges (they have strong identity), while low continuum formations should collapse aggressively when safe (they are still forming and shouldn't explode state). That is consistent with your "consolidation" idea: stable attractor basins merge instances into anchors; unstable noise doesn't get to define vocabulary.

[PLACEMENT TAG] Section 2 -> 2.8
2.8 Merge and split rules must be hysteretic, timeline-safe, and based on multi-window evidence, not one-tick coincidences

Merging in this system is not just "two centroids got close." Merging is "two coherence structures have behaved as one coherent structure across time." Because continuum is coherence persistence over time, a merge rule that does not require persistence is automatically wrong.

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

Both merge and split must have hysteresis: the condition to merge must be stricter than the condition to remain merged, and the condition to split must be stricter than the condition to remain split. That prevents thrash under jittery inputs and keeps the GPU envelope from being dominated by structural churn.

[PLACEMENT TAG] Section 2 -> 2.9
2.9 What the system "writes" where: a minimal, enforceable responsibility boundary

To make this implementable, it helps to pin down who owns which quantity.

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

This boundary is how you keep the closed-system causal structure intact while still allowing external injections: injections become explicit boundary events at tick k; the system responds by applying admissible, ordered, bounded deltas at and after tick k.

Part 3/Step 3 (Rewritten) - Spider Graph Encoding as Direct GPU Electrical "Write-Path," with Envelope as Read-Path (Delta->Frequency Profiles + Amplitude->Harmonics)

[PLACEMENT GROUP] Section 3

[PLACEMENT TAG] Section 3 -> 3.1
3.1 Two-path model: what "using electrical signaling directly" means

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

So: electrical signaling is direct on the write-path (execution), indirect on the read-path (envelope constraints). That preserves your intended lightness without claiming impossible observability.

[PLACEMENT TAG] Section 3 -> 3.1
3.1 What the spider graph is (and what it is not)

In EigenWare, the spider graph exists specifically to keep updates low-bandwidth: the system does not emit 9D deltas as 9 numbers per update. It emits one frequency and one amplitude and treats them as a compact update instruction to advance an Eigenstate's phase and its coherence-space coordinates under bounded, deterministic evolution.

[PLACEMENT TAG] Section 3 -> 3.2
3.2 The pulse is the minimal electrical write instruction

A pulse is the smallest committed update unit. It is the minimal payload we can send such that one kernel execution step advances an Eigenstate's phase-space trajectory. Because the GPU physically realizes the pulse during execution, a pulse is effectively "direct electrical control" of the simulated phase dynamics through hardware switching.

The pulse must be small, deterministic, and causally ordered. The canonical payload is:

(eid, tau_q, tier_id, f_code, a_code, profile_id, causal_tag)

This payload is intentionally tiny: it is the control surface. It avoids shipping 9D vectors per update and avoids storing tokens as primary state. It is precisely why the architecture scales under bandwidth constraints: almost everything happens as in-GPU switching on dispatch.

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

[PLACEMENT TAG] Section 3 -> 3.2
3.2 Pulse payload format (what gets emitted per update)

A pulse is the smallest committed update unit. A pulse always belongs to an Eigenstate anchor id and a causal tick. It is never applied outside its commit window. The canonical payload is:

(eid, tau_q, tier_id, f_code, a_code, profile_id, causal_tag)

Where f_code is the quantized primary frequency code, a_code is the quantized amplitude code, profile_id selects the delta encoding profile (language, physics, crawler ingestion, etc.), and causal_tag binds the update to ordering rules (so no retroactive tier contradictions occur). The pulse payload is deliberately small so a kernel can process massive counts without IO thrash.

A useful table representation for the payload helps Copilot keep types correct:

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

[PLACEMENT TAG] Section 3 -> 3.3
3.3 What "frequency" and "amplitude" mean in-kernel (not as sensors)

Frequency in this model is a control coefficient. It is the scalar that tells the kernel how to advance phase and related Basis9 components for this pulse. Amplitude is a second control coefficient that tells the kernel how strongly to apply the update and which harmonic mode expansion to use. Neither implies that we are reading physical frequencies out of the GPU. We are encoding update instructions as compact coefficients, and the GPU's physical switching realizes them.

If you want an extremely literal phrasing: f_code and a_code are the digital knobs; the GPU's electrical signaling is the physical actuator.

[PLACEMENT TAG] Section 3 -> 3.3
3.3 Basis9 deltas: input to spider graph

The spider graph consumes a delta vector ? in Basis9. This is the delta after projection has already chosen a target anchor (or anchor set) and after all gating has passed (timeline/realm/constraint gates). The spider graph must not be responsible for deciding "where" the delta goes; it only compresses "what the delta is."

Let the input delta be:

? = (?1, ?2, ..., ?9)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

[PLACEMENT TAG] Section 3 -> 3.4
3.4 Input to the spider graph: a projected Basis9 delta

The spider graph does not decide what anchor receives an update. Projection and gating happen first (timeline/realm/constraint gates). The spider graph takes the already-approved delta vector in Basis9 and compresses it.

Input delta:

? = (?1..?9)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

[PLACEMENT TAG] Section 3 -> 3.4
3.4 Delta encoding profiles (explicit normalization and weighting)

A "profile" is a fixed set of normalization rules and weights that define how each Basis9 axis contributes to f_code and a_code. Profiles exist because language ingestion behaves differently than physics evolution and crawler ingestion. A profile fixes four things: axis normalization, axis weights, sign conventions, and quantization parameters.

A profile is defined by:
	-	norm_i(?i) -> xi in [-1, 1] or [0, 1] depending on axis
	-	weight_i -> wi (weights sum to 1 over participating axes)
	-	quantization: f_scale, f_min/f_max, a_scale, a_min/a_max
	-	harmonic mode map: a_code -> harmonic_k and multiplex budget

Below are the canonical profiles we will use. If you later want more, add them as new enums rather than altering existing profiles.

Profile P0 - "Core Evolution" (default physics/constraint evolution)
This profile treats phase coherence (?5) and flux (?6) as primary contributors, with phantom (?7) and aether (?8) acting as clamps, and nexus (?9) governing binding tension. Spatial deltas (?1-?3) contribute lightly so locality drift does not dominate identity. This profile is for normal simulation steps.

Profile P1 - "Language/Encoding Injection"
This profile emphasizes phase coherence (?5) and nexus binding (?9) more strongly, because text injection is primarily about forming stable binding structures and harmonic meaning rather than transporting spatial locality. Flux (?6) contributes as a grammar/transition channel; aether (?8) clamps high-frequency noise from novel sequences.

Profile P2 - "Crawler Ingestion / High-Variance Input"
This profile is conservative: it clamps amplitude more aggressively and increases the influence of phantom/aether to prevent noisy web data from forcing splits. Its primary aim is to collapse into existing bands when safe and only form new anchors when coherence evidence persists over time.

A compact table of weights clarifies how each profile differs. The numbers below are starting defaults; they must be constants in config, not per-tick variables.

Axis contribution	P0 Core Evolution	P1 Language Injection	P2 Crawler Ingestion
?1-?3 spatial	0.10 (total)	0.05 (total)	0.05 (total)
?5 phase/coherence	0.30	0.40	0.35
?6 flux	0.20	0.15	0.15
?7 phantom	0.10	0.10	0.15
?8 aether	0.15	0.20	0.20
?9 nexus	0.15	0.10	0.10

These weights encode your axiom: phase/coherence persistence and flux dynamics are the main drivers of identity evolution, while phantom/aether/nexus govern interaction gating, stabilization, and binding.

[PLACEMENT TAG] Section 3 -> 3.5
3.5 Spider graph definition: 9D -> one signed f_code

The spider graph is a deterministic operator that reduces a 9D delta into one signed scalar. Each axis has a normalization function and a weight. The normalization ensures each ?i is mapped into a fixed bounded xi, and the weights encode which axes dominate frequency synthesis per profile.

For each axis i in the profile:

xi = norm_i(?i)

Then compute:

s = ?_i (wi * xi)

Then quantize deterministically:

f_code = clamp_int( round_fixed(f_scale * s), f_min, f_max )

The wrapped distance for ?5 is non-negotiable. If ?5 is not wrapped, f_code becomes discontinuous at phase boundary and coherence bands destabilize. In practice, ?5 is computed first, then normalized to a bounded xi using a fixed scale factor.

[PLACEMENT TAG] Section 3 -> 3.5
3.5 Frequency synthesis: 9D -> one signed scalar (f_code)

Frequency synthesis is a weighted sum of normalized axis deltas with deterministic clamping and quantization. Define:

xi = norm_i(?i)

Then compute a signed scalar:

s = ?_i (wi * xi)

Then map to a frequency code:

f_code = clamp_int( round_to_int(f_scale * s), f_min, f_max )

The "round_to_int" must be deterministic (fixed rounding mode, e.g., bankers rounding disabled; always round toward zero or always round half-up). The clamp_int is mandatory. The f_scale and bounds are profile parameters. f_code is signed because direction matters: positive vs negative indicates the "direction" of the delta composition in the spider basis.

Important: ?5 (phase delta) must use shortest signed wrap; otherwise frequencies jump discontinuously at phase boundary and destroy coherence bands.

[PLACEMENT TAG] Section 3 -> 3.6
3.6 Delta encoding profiles: axis weights and normalization are versioned constants

Profiles exist because different update sources have different "dominant axes." The profile must be fixed and versioned so replay and rehydration are stable.

Each profile defines:
	-	which axes participate in synthesis
	-	weights wi (sum to 1)
	-	normalization constants
	-	quantization scales and bounds
	-	harmonic mapping behavior

Canonical profiles in your flow:

P0 Core Evolution: phase (?5) and flux (?6) dominate; phantom/aether/nexus stabilize and constrain; spatial deltas contribute lightly.
P1 Language Injection: phase (?5) dominates plus nexus binding contribution; flux contributes as structured transition; aether clamps novelty.
P2 Crawler Ingestion: conservative; stronger clamps; prefers collapse into existing bands; requires persistence evidence before new anchors.

A starting weight table (constants in config; do not vary per tick):

The point of profiles is not "semantic vibes." It's ensuring that frequency coding is stable and that different sources don't distort the manifold differently. Profiles are also how you keep crawler noise from forcing splits: P2 increases clamp contribution and reduces aggressive binding.

[PLACEMENT TAG] Section 3 -> 3.6
3.6 Amplitude synthesis: update strength + harmonic mode selector (a_code)

Amplitude is not "meaning." Amplitude is the update strength, multiplex budget, and harmonic mode selector. It determines how strongly this pulse advances phase, whether it should engage higher harmonic representation (multi-mode), and how much compression "fan-out" it can represent without spawning new anchors.

Amplitude is synthesized from a small set of signals that represent certainty and stability: coherence chi_q, continuum (persistence), and clamp pressure from aether/phantom. The aim is that highly coherent, persistent bands can use stronger amplitude (more confident updates, more multiplex) while noisy or unstable inputs are forced into low amplitude (collapse/infer rather than split).

A deterministic amplitude function can be defined as:

u = g(chi_q, continuum, clamp_terms)

where g is monotonic in chi and continuum, and decreasing in clamp_terms (high phantom/aether activity reduces amplitude). Then:

a_code = clamp_uint( round_to_uint(a_scale * u), a_min, a_max )

Again, rounding must be fixed, and clamps must be strict.

[PLACEMENT TAG] Section 3 -> 3.7
3.7 Amplitude synthesis: update strength and harmonic mode selection (a_code)

Amplitude is the second half of the compression. It determines (1) how strong the pulse is, and (2) which harmonic expansion mode is selected inside the kernel. This is where you get "higher harmonics modes for expressing compressed state" without sending more data.

Amplitude should be driven by the state's coherence and persistence, not by raw delta magnitude alone, because you want high-confidence coherent bands to express richer structure and low-confidence noisy inputs to collapse/infer rather than split.

Define a deterministic scalar u in [0,1]:

u = g(chi_q, continuum, clamp_terms)

where g increases with chi_q and continuum (coherence persistence over time), and decreases with clamp_terms (phantom/aether activity indicating instability or non-interaction regimes). Then:

a_code = clamp_uint( round_fixed(a_scale * u), a_min, a_max )

The key is that continuum is exactly your "phase coherence persistence over time," so amplitude naturally tracks when a band has earned richer representation.

[PLACEMENT TAG] Section 3 -> 3.7
3.7 Harmonic mode mapping: how amplitude selects higher harmonics for compressed state

This is the key detail you asked for: amplitude maps the primary frequency into higher harmonic modes for expressing compressed structure. The idea is not that we literally generate harmonics in hardware. The idea is that we represent a richer state update without transmitting more data by using harmonic mode selection: one base f_code with an amplitude code implicitly chooses a harmonic index k and thus represents a family of related updates (harmonic components) inside the evolution kernel.

Define harmonic modes as discrete bands of a_code. For example:
	-	Mode 0 (base): low amplitude, only fundamental frequency component applied
	-	Mode 1: includes 2nd harmonic component (2*f) as an internal kernel effect
	-	Mode 2: includes 3rd harmonic
	-	...
	-	Mode K: includes up to Kth harmonic

The amplitude code does two things: (1) chooses mode k, and (2) gives strength within that mode.

A concrete mapping is:

k = floor( a_code / mode_bucket_size )

strength = (a_code % mode_bucket_size) / mode_bucket_size

Then the kernel interprets the pulse as applying:

fundamental component at f_code with strength
plus optional harmonic components at n*f_code for n=2..k (with decreasing weights)

The weights for higher harmonics must be deterministic and profile-fixed (e.g., 1/n or an exponential drop), so rehydration is stable and does not depend on floating math.

Operationally, this gives you a compressed representation of "richer delta structure" without sending additional scalar channels. It's also how higher-tier simulations can get denser expressive capacity: a higher tier can treat a single pulse as representing multiple harmonic sub-steps in the lower tier without copying state or increasing update payload size.

[PLACEMENT TAG] Section 3 -> 3.8
3.8 Harmonic mode mapping: how amplitude expands one frequency into multiple harmonic components

This is the precise mechanism you asked for. One pulse carries one f_code, but a_code selects a harmonic mode k. The kernel interprets k as "apply harmonic components deterministically," allowing a single update to represent a bundle of coherent substructure.

Partition a_code into buckets. For a fixed mode_bucket_size:

k = floor(a_code / mode_bucket_size)
strength = (a_code % mode_bucket_size) / mode_bucket_size

Then the kernel applies:

component 1: f_code with weight W1(strength)
component 2..k: (n * f_code) with weights Wn(strength)

The harmonic weights Wn must be fixed laws (versioned constants) so replay is deterministic. A simple universally stable law is Wn = 1/n (or an exponential decay), with optional profile-specific tuning if you want language injection harmonics to be more "wide context" than physics evolution.

The result is that "compressed state" can express multi-mode evolution in one pulse. The higher harmonics represent structured sub-updates without increasing payload size, and the GPU realizes this through a single kernel execution path, which is exactly your lightweight goal.

[PLACEMENT TAG] Section 3 -> 3.8
3.8 How harmonic modes support tier-to-tier compression without violating closure

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

[PLACEMENT TAG] Section 3 -> 3.9
3.9 Why this stays causal and closed

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

[PLACEMENT TAG] Section 3 -> 3.9
3.9 Exact determinism requirements: fixed-point, quantization, and invertibility constraints

To keep everything replayable and rehydratable, the spider graph must be implemented in fixed-point. Phase deltas are stored in turns in fixed-point, weights are stored as rational fixed-point constants, normalization uses integer arithmetic with precomputed scale factors, and rounding is locked. The profile parameters (weights, scales, bounds, mode bucket sizes) are immutable for a given snapshot; if they change, they must create a new profile version id so old data remains decodable.

The "invertibility" requirement is not that you can reconstruct the full 9D delta from f_code/a_code-that would defeat compression. The requirement is that given the same ? vector and same profile, you always compute the same (f_code, a_code), and that the kernel's expansion into harmonic components is deterministic and versioned. That gives deterministic replay even though the representation is lossy.

Yes. Here's Section 3 written as a clean spec chunk you can paste as-is. I'm going to lock the "direct electrical write-path" framing, the spider graph math, the delta encoding profiles, and the harmonic/activation behavior, plus the persistence-vs-activation rule so decay can't accidentally imply data loss.

SECTION 3 - Spider Graph Pulse Encoding, Delta Profiles, Harmonic Activation, and Direct GPU Write-Path

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

3.X Constraint Operators and Eigenstate Trajectory Updates

This subsection defines the mandatory constraint operators governing all
frequency updates and eigenstate trajectory evolution. These operators are
applied during pulse encoding and kernel evolution without exception.

All calculations are delta-based and relativistically normalized.

3.X.1 Delta Formulation

Let the state at tick k be defined by the Basis9 state vector S_k, phase phi_k,
frequency omega_k, and radius r_k.

All evolution begins with deltas:

Delta_S_k = S_k - S_(k-1)
Delta_omega_k = omega_k - omega_(k-1)
Delta_phi_k = phi_k - phi_(k-1)

No absolute state updates are permitted.

3.X.2 Relativistic Normalization Operator

Before constraint application, deltas are normalized to cancel observer-scale
distortion.

Norm_k = sqrt(sum_i (Delta_S_k[i])^2 + epsilon_k)

Where epsilon_k is a stabilizer derived from active constraint balance, not a
fixed constant.

Delta_S_norm_k = Delta_S_k / Norm_k

3.X.3 Constraint Tensor Application

Let C_k be the constraint tensor derived from anchor alignment, coherence,
and flux balance.

Delta_S_constrained_k = C_k * Delta_S_norm_k

Unobserved degrees of freedom required for conservation are implicitly
represented within C_k.

3.X.4 Frequency Update Operator

Eigenstate trajectory deformation is driven by constrained deltas.

omega_k = omega_(k-1) + G(Delta_S_constrained_k)

Where G(.) projects constrained deltas onto phase-advance rate.

Frequency updates MUST be continuous and MUST use omega_(k-1).

3.X.5 Phase Advancement Operator

Phase advances directly from frequency evolution:

phi_k = phi_(k-1) + omega_k * Delta_t_eff

Delta_t_eff is derived from coherence constraints and is not wall-clock time.

3.X.6 Radius Decay Operator

Amplitude is applied only at encode time to set initial radius r_k(0).

Radius decays deterministically:

r_k(t) = r_k(0) * exp(-Lambda_k * t)

Where Lambda_k is derived from |Delta_omega_k| and constraint tension.

3.X.7 Observer-Error Cancellation Operator

3.X.8 Operator Harness Construction Step 1: Delta Integrity Harness

This harness validates that all state updates are delta-based.
The harness must assert that no absolute state assignments occur.
Test procedure:
- Inject sequential pulses
- Verify S_k = S_(k-1) + Delta_S_k for all ticks
- Fail on any direct overwrite

3.X.9 Operator Harness Construction Step 2: Relativistic Normalization Harness

This harness validates observer-scale cancellation.
Test procedure:
- Inject identical deltas at different magnitudes
- Verify normalized deltas converge
- Confirm scale invariance across observation frames

3.X.10 Operator Harness Construction Step 3: Constraint Tensor Harness

This harness validates conservation under incomplete observation.
Test procedure:
- Suppress selected Basis9 components
- Verify constraint tensor compensates without divergence
- Confirm invariants remain bounded

3.X.11 Operator Harness Construction Step 4: Frequency Evolution Harness

This harness validates eigenstate trajectory continuity.
Test procedure:
- Apply successive constrained deltas
- Verify omega_k depends only on omega_(k-1)
- Confirm no discrete frequency resets

3.X.12 Operator Harness Construction Step 5: Phase Advancement Harness

This harness validates phase coherence.
Test procedure:
- Integrate omega_k over effective ticks
- Verify phi evolution is continuous
- Confirm no phase discontinuities

3.X.13 Operator Harness Construction Step 6: Radius Decay Harness

This harness validates persistence control.
Test procedure:
- Encode impulses with varying A_enc
- Verify deterministic decay
- Confirm no runtime amplification

3.X.14 Operator Harness Construction Step 7: Observer-Error Cancellation Harness

This harness validates drift suppression.
Test procedure:
- Inject biased deltas
- Verify mean bias removal
- Confirm structure preservation

3.X.15 Unified Operator Harness Assembly

The unified harness executes all operator harnesses in order.
Failure of any step invalidates kernel coherence guarantees.
All harnesses MUST pass before deployment.

To cancel perspective bias:

Delta_S_corrected_k = Delta_S_constrained_k
                      - mean(Delta_S_constrained_k over window W)

This operator preserves structure while canceling observational drift.

The apparent randomness and dark-sector behavior arise from this correction.

SECTION 4 - Tier Coupling, Commit Barriers, and Literal-Pulse Aggregation Across Tiers

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

SECTION 4.1 - Tier-Summary Context Coupling Profile (Exact Constants + Harmonic Falloff)

This subsection locks the "tier-summary" profile used when a lower tier emits literal summary pulses to a higher tier. The purpose of this profile is contextual memory activation: broader harmonic coupling, longer-range binding spread, and stable aggregation that is deterministic and insensitive to lower-tier microstep ordering. This profile is versioned; changing any constant requires incrementing profile_id (or profile_version) so older snapshots remain decodable.

[PLACEMENT TAG] Section 3 -> 3.10
3.10 Determinism requirements that Copilot must treat as law

The spider graph must be fixed-point and versioned. Phase deltas are in turns, wrapped, and quantized. Weights are stored as rational fixed-point. Normalization uses integer math with locked rounding. Harmonic mode laws are constants bound to a profile version id. If any of these change, profile_id must change and the system must preserve backward decodability for old pulses.

This is how you can have a simulation that is extremely lightweight on the bus yet still rehydratable and deterministic: you're shipping a tiny control stream that the GPU physically realizes, and you're protecting the interpretation rules with versioned determinism.

Part 3/Step 3 - Spider Graph Encoding, Delta->Frequency Profiles, and Amplitude->Harmonic Mode Mapping (Compressed-State Pulse Spec)

[PLACEMENT GROUP] Section 4

[PLACEMENT TAG] Section 4 -> 4.1.7
4.1.7.4 Freezing and implementation

Once alpha_ctx is chosen, we precompute a small LUT for n=1..k_max:

pow_lut[n] = round_fixed( (n ^ alpha_ctx) * pow_scale )

Then weights per pulse are computed with integer math:

Wn_raw = pow_scale / pow_lut[n]
Normalize via integer sum and division with locked rounding.

No platform float pow is permitted in canonical execution; only LUT-based fixed-point.

Result: broader coupling is not "because we wanted it," it is because the calibration objective J is maximized at a lower alpha, and that alpha is frozen into the snapshot.

SECTION 4.1.8 - Summary Emission Policy (Derived Criteria, Pulse Counts, causal_tag Semantics)

Tier coupling remains deterministic only if the lower tier's summary is (a) order-insensitive to microstep scheduling, (b) bounded by the higher-tier envelope, and (c) explicit about structural topology changes (merge/split) versus ordinary drift. This subsection defines exactly how bands/attractors are selected for emission, how many pulses they emit, and how causal_tag encodes event meaning so the higher tier can interpret pulses without ambiguity.

[PLACEMENT TAG] Section 4 -> 4.1.9
4.1.9.7 Determinism and replay constraints

To keep replay stable:
	-	budget_state must be logged as part of the window's control trace if you require bitwise identical replay under variable hardware conditions.
	-	If you do not log it, then runs on different machines may schedule different pulse counts per window, but each run remains causally valid and internally deterministic relative to its own envelope.

The spec therefore defines two modes:
	-	Strict replay mode: log budget_state per window into the snapshot trace.
	-	Adaptive mode: compute budget_state live; behavior is deterministic given the local envelope but not identical across machines.

Both modes preserve closure because budget_state affects only how much work is done, not what the physics/meaning is.

SECTION 5 - Crawler Subsystem, In-Simulation Encoder, and Persistent-Resonance Ingestion (Electronic-Signaling Execution)

[PLACEMENT GROUP] Section 5

[PLACEMENT TAG] Section 5 -> 5.10
5.10 What this enables

SECTION 5.11 - Concrete Mapping Spec: Raw Text -> ASCII Phase Injection -> Formation Deltas -> Resonance Collapse (Causality-Safe)

[PLACEMENT TAG] Section 5 -> 5.11.9
5.11.9 Closed-system causality guarantee

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

SECTION 5.15 - Hub-Conditioned Residual Encoding (Maximum Dependence, No Carrier Coupling)

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

Each modality stream S_m produces observations O_m(\tau). The hub state H(\tau) produces a predicted observation \hat{O}_m(\tau) in that modality's observation space (headless in v1). The encoder emits only the residual \Delta_m(\tau) = O_m(\tau) - \hat{O}_m(\tau) mapped into Basis9 deltas and spider-compressed into pulses. Smaller residuals mean fewer pulses, fewer harmonics, and lower compute cost; large residuals increase emission until the hub updates enough that residuals shrink again.

The hub never "renders." In v1 the prediction \hat{O}_m is a mathematical expectation operator (constraint projection) that is cheap to compute and deterministic. This is how modalities map cleanly into one file: the file persists hub evolution and residual pulses, not per-modality carrier traces.

SECTION 5.16 - Cross-Modal Hub Bands (Object/Concept Constraints, 2D?3D Join)

A hub band is a stable attractor whose state represents a constraint bundle, not a raw signal. Constraint bundles cover both semantic and physical structure: identity labels, relations, geometry priors, rigidity, symmetry, material cues, motion persistence, and causal adjacency.

The join mechanism is explicit bindings, not implicit synchronization. Text, images, audio, and video bind to the same hub eid via ?9 pulses. That means the same "thing" is recognized because independent evidence streams converge on the same hub band under coherence/continuum scoring.

A compact internal distinction keeps 2D and 3D from fighting:
	-	2D evidence bands represent observation constraints (image tiles, edges, optical-flow blocks).
	-	3D latent bands represent world constraints (shape hypotheses, part graphs, kinematic links).

In headless v1, "3D" is simply the latent constraint state. It can be updated and stabilized without producing a rendered 2D frame. Later, when you add a Blender-like renderer, it becomes a consumer of hub constraints, not the source of them.

A minimal hub schema that fits the existing pulse model is:

Hub component	Stored as	How it updates
Identity/label priors	band coord_sig + bindings	reinforced by text and repeated multimodal evidence
Geometry priors (latent 3D)	constraint sub-bands bound to hub	updated by image/video residuals and persistence
Temporal persistence	hub(t)->hub(t+1) bindings	reinforced by video motion coherence
Causal adjacency	hub?hub relation bindings	reinforced by text relations and repeated co-occurrence

SECTION 5.17 - Modality Delta Constructors (Explicit Mappings into Basis9)

Each modality has its own observation extractor, but all of them output a Basis9 delta packet before spider compression. The packet is "what changed" plus "what this evidence should bind to."

Text constructor (token instance)
A token instance in context is treated as a hub activation + binding update. If the token has a stable attractor, the encoder emits an activation pulse to the token band and a binding pulse (?9) token->hub(sentence/topic). If it is novel, it emits formation staging pulses (ASCII/byte phase injection) but still binds the staging band to the hub so collapse is context-shaped.

Audio constructor (spectral frames)
Audio is segmented into deterministic frames, converted to a deterministic spectrum representation, then sparsified (top-K bins or energy-threshold bins). Frequency-bin index maps to phase on ?5; magnitude maps to amplitude via a_code; temporal adjacency binds frames through ?9. When hub context is active (topic/speaker/scene), the encoder emits residuals relative to the hub's expected spectral envelope, so stable background conditions compress aggressively.

Video constructor (image+motion+audio sync)
Video is treated as image tiles over time plus motion constraints. Motion residuals (block matching / flow proxies) map primarily to ?6 (flux-like change) and ?9 (temporal binding). Audio track is processed as in the audio constructor but additionally bound to frame-time hubs for synchronization.

A practical mapping table (Basis9 intent) that Copilot can implement without guesswork:

Feature	?1-?3	?5 (phase)	?6 (flux)	?7/?8 (stability)	?9 (binding)
text token event	optional	token phase residual / staging	context change rate	novelty risk/clamp	token?hub, token?token
image tile step	dx,dy	intensity/ch residual	edge/motion energy	noise/occlusion risk	tile?hub, tile?tile
audio frame bin	-	bin_id phase	magnitude change	noise floor / clipping risk	frame?hub, frame?frame
video motion	-	-	motion residual	tracking uncertainty	hub(t)?hub(t+1)

SECTION 5.18 - Profile Selection and Calibration (No Arbitrary Constants)

Every mapping above uses a profile_id that controls spider weighting, harmonic policy, and emission gating. No weights are hardcoded as "picked." They are derived by calibration on a fixed sample slice and frozen into the snapshot for deterministic replay.

For each modality profile P_m, calibration selects:
	-	axis weights w_i by marginal utility under the objective J (coherence/continuum gain minus violation/thrash/budget penalties),
	-	harmonic decay parameter(s) by argmax of the same J under the envelope,
	-	promotion thresholds by bounding false-anchor rates and band thrash rates on the calibration slice.

The only "policy constants" allowed are stability targets (max thrash rate, max false-anchor rate, target ingestion budget share). Those targets define what "good" means; the numbers used in execution are derived from hardware envelope + calibration replay and then frozen per snapshot.

SECTION 5.19 - Training Curriculum Control, Verification Scoring, and Dataset Hygiene

The crawler operates as a curriculum scheduler. It maintains a target training distribution across domains and modalities and uses verification scoring to allocate pulse budget where structure is most reliable. Verification is metadata-based and deterministic: it does not make philosophical claims about truth, it produces a confidence score that governs how strongly content is allowed to bind and how quickly it can promote new attractors.

The crawler also enforces dataset hygiene so the manifold stays compressible: duplicates reinforce existing attractors; boilerplate is downweighted; long content is chunked and spread over windows; highly variable sources naturally fail continuum and do not become stable structure unless corroborated across independent contexts.

For first iteration, projection remains headless: the system learns constraint models for objects and scenes without producing frames. That keeps EigenWare lightweight and lets it focus on stabilizing and tidying the repository while it builds multimodal priors in the background via committed pulses.

SECTION 5.20 - Single-File Persistence: Streams, Pulses, and Rehydration Invariants

All modalities and the hub share one persisted file because the file stores only canonical pulses and topology state, not encoder carriers. A "training capture" is therefore a multiplexed pulse ledger with modality tags and stream identifiers.

A canonical record shape:

(eid, tau_q, tier_id, modality_id, stream_id, f_code, a_code, profile_id, causal_tag)
	-	modality_id distinguishes TEXT / CODE / IMAGE2D / AUDIO / VIDEO / HUB / LATENT3D.
	-	stream_id identifies the sequence unit (page block, tile set, audio segment, lecture unit) and allows rehydration without assuming external ordering.
	-	eid identifies the target band (modality-local or hub).
	-	causal_tag encodes structural events (activate, drift, mode split/merge, bind update) exactly as defined earlier.

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

If you want to move to Part 2 next (9D axis definitions + band math), Section 5 is now closed with the missing multimodal join logic pinned: shared hub constraints + residual encoding + one-file pulse ledger.

SECTION 6 - File Encodings, Crawler Identifiers, and Multimodal Persistence (Single-Container Spec)

[PLACEMENT GROUP] Section 9

[PLACEMENT TAG] Section 9 -> 9.10
9.10 Single-file contract test harness (explained, explicit, and complete)

The implementation must ship a single, self-contained test module that exercises every contract in this spec without relying on network access or external corpora. The harness is intentionally small: it uses built-in fixtures (tiny text, tiny token sequences, tiny pseudo audio-event arrays) and tests determinism, registry enforcement, record formatting, budget behavior, dedup behavior, fail-closed robustness, alignment repair, and anti-thrash hysteresis.

File name (repo path):
tests/test_kernel_contract.cpp

Required structure of the harness

A) Minimal contract models (pure data, no GPU requirement)
The file defines minimal "record" structures that mirror the storage model used by the encoder layer. These are contract-level stand-ins, not the full production schema. They are sufficient to verify replay stability and rule enforcement.

Required record models:
- PulseRecord: eid, tau_q, tier_id, modality_id, stream_id, f_code, a_code, profile_id, causal_tag
- BandRecord: eid, band_type, birth_tau, parent_eid, signature_id9, band_state_digest, flags
- BindingRecord: src_eid, dst_eid, tau_q, binding_kind, strength_code, profile_id, flags
- ManifestRecord: artifact_id, stream_id, mime, extractor_id, trust_class, domain_id, provenance, license_hint, coord_sig, segment_map_ref, alignment_json

Container coord_sig contract:
The harness defines a Container wrapper with a stable coord_sig() function. coord_sig() must coord_sig:
1) a JSON dump of header with sort_keys=True
2) each record serialized via a stable as-dict rule, in this order: manifests, bands, bindings, pulses
This is the strict replay "oracle": if behavior changes, the coord_sig changes.

B) Registries (enforcement, versioning hooks)
The harness must implement minimal registries and enforce rules:

- ExtractorRegistry contains ExtractorRegEntry with:
  extractor_id, supported_mime_re, normalization_rules_digest, segmentation_rules_digest, fallback_extractor_ids
- ProfileRegistry contains ProfileRegEntry with:
  profile_id, allowed_extractor_ids, allowed_causal_tags, profile_digest

Registry enforcement tests must prove:
1) Unknown profile_id or extractor_id errors out deterministically.
2) ProfileRegistry rejects illegal extractor_id for a profile_id.
3) ProfileRegistry rejects illegal causal_tag for a profile_id.
4) ExtractorRegEntry.supports(mime) behaves deterministically (regex stable).

C) Deterministic toy extractor (fixture path)
The harness includes a toy normalizer and segmenter that mimic "extractor determinism" rules without needing real HTML/PDF parsers. These functions are proxies that validate the determinism contract.

Required functions:
- normalize_text(raw): line ending normalize; collapse runs of spaces; collapse 3+ newlines to 2; strip ends.
- segment_text_blocks(norm): split on blank lines (double newline), trim, remove empties.
- sig9_rules(name, payload): SIG9 of a stable string concatenation. Used to simulate rules coord_sig.

Extractor determinism acceptance:
- Same raw fixture -> same norm string
- Same norm string -> same segments list
- Rules coord_sig remain stable for the fixture

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

Budget acceptance:
- A blocks list is capped to exactly max_blocks with stable ordering
- A pulse list is capped to exactly max_pulses with stable ordering
- The cap is purely positional (no data-dependent reordering)

E) Dedup (exact dedup for contract coverage)
The harness includes exact_dedup(items) that removes exact duplicates by SIG9 coord_sig while preserving first occurrence ordering.

Dedup acceptance:
- Duplicate blocks reduce emitted pulses
- The first instance of a duplicated block is preserved and appears in the same position as before dedup

F) Fail-closed robustness (corrupt fixture path)
The harness includes a corrupt fixture that simulates a parser failure scenario. The production system may raise; the harness accepts either:
- explicit exception (preferred), or
- zero segments emitted (acceptable), but never "garbage pulses"

Robustness acceptance:
- Corrupt fixture does not produce any PulseRecord emission in the harness logic.

G) A/V alignment repair (single offset, deterministic)
The harness includes a toy alignment estimator:
estimate_alignment_offset(caption_tokens, audio_events) -> offset_k

It must search a fixed small window (example: k in [-5, 5]) and pick the k that maximizes exact matches between shifted caption tokens and audio event tokens. This is a proxy for "maximize coherence" and must be deterministic.

Alignment acceptance:
- A fixture with one leading "noise" event in audio yields offset_k == 1.
- The function is pure and stable (no randomness, no adaptive drift in strict mode).

H) Thrash guard hysteresis (cooldown)
The harness includes a ThrashGuard with:
- cooldown: integer number of tau_q steps
- last_change_tau dict keyed by change_key

Thrash acceptance:
- First change at tau_q = T is allowed
- Any repeated change for same key at tau_q < T + cooldown is rejected
- Change is allowed again at tau_q >= T + cooldown

I) Watermark separation contract test (deterministic; fail-closed)
The harness SHALL include a contract fixture and assertion that codifies:
- watermark evidence promotes ARTIFACT_WATERMARK_BAND and does not pollute SCENE_* bands

If the detector or fixture is not implemented, the test MUST hard-fail with reason code:
- WATERMARK_FIXTURE_MISSING

Exception policy (explicit only): Non-release profiles MAY mark this test as expected-fail via a checked-in allowlist file named:
- tests/xfail_allowlist.txt
Release profiles MUST treat this as a hard failure.

If the detector or fixture is not implemented, the contract test SHALL hard-fail with reason code WATERMARK_FIXTURE_MISSING. Release profiles MUST not allow xfail for this test.

Minimal fixture set (all embedded in the test file)

- FIXTURE_HTML_TEXT: a tiny multi-paragraph sample with an intentional duplicate paragraph.
- FIXTURE_CORRUPT_TEXT: short invalid bytes or a null string pattern.
- FIXTURE_CAPTION: short token list.
- FIXTURE_AUDIO_EVENTS: same tokens with a deterministic offset.

Strict replay acceptance test (the main one)

The harness defines a toy build_container() that:
1) normalizes and segments FIXTURE_HTML_TEXT
2) exact_dedup() the segments
3) emits one PulseRecord per segment with deterministic f_code/a_code:
   - f_code derived from SIG9(segment) mod some fixed integer
   - a_code derived from segment length mod 256 (clamped)
4) builds a Container and returns it
5) asserts that build_container(FIXTURE_HTML_TEXT).coord_sig() is identical across two runs

This test is the "spec compliance alarm bell". Any nondeterministic ordering, accidental randomness, or silent behavior drift will change the coord_sig and fail the test.

What Copilot should implement first to satisfy this harness

1) Registry objects and enforcement (ProfileRegistry and ExtractorRegistry)
2) Deterministic normalization + segmentation for at least one text path
3) Container coord_sig and record serialization stability
4) Budget caps and dedup
5) Alignment repair stub (even toy) and ThrashGuard
6) Ensure WATERMARK_FIXTURE_MISSING is removed by implementing the detector and fixture; release profiles MUST pass the contract test.

----------------------------------------------------------------
SUBSECTION: GPU SIGNALING  MATHEMATICAL OPERATORS (APPENDED)
----------------------------------------------------------------

This subsection appends mathematical operators to the existing
GPU signaling section. No prior text is modified.

Eigenstate Representation:
Each eigenstate E_i is represented as:
E_i(t) = (phi_i(t), A_i(t))

where:
- phi_i(t) is phase trajectory
- A_i(t) is amplitude envelope

Delta Phase:
phi_i(t) = phi_i(t) - phi_i(t-1)

Composite Phase Trajectory:
Phi_comp(t) = _i (A_i(t) * phi_i(t))

Temporal Envelope:
A_env(t) = _i |A_i(t)|

Pulse Signal:
S(t) = A_env(t) * Phi_comp(t)

Order of Operations:
1. Compute phi_i for all eigenstates
2. Weight deltas by A_i(t)
3. Sum into Phi_comp(t)
4. Compute A_env(t)
5. Emit S(t) as GPU pulse

----------------------------------------------------------------
END SUBSECTION
----------------------------------------------------------------

X.X Purposeful Criteria Driven File Emergence

This subsection applies to all remaining sections.

X.X.1 Purpose Definition Operator
- Define intent and effects

Harness:
- Verify completeness

X.X.2 Dependency Resolution Operator
- Enumerate dependencies

Harness:
- Verify acyclic order

X.X.3 Execution Sequencing Operator
- Define stepwise order

Harness:
- Simulate determinism

X.X.4 Event and Dispatcher Operator
- Define events and routing

Harness:
- Verify delivery

X.X.5 Consolidation Gate
- Approve file emission

Harness:
- Verify all prior harnesses pass
---

EOF APPEND ONLY - CANONICAL FREEZE canonical (Sections 1-3 + headers preserved)
All content above this horizontal rule is byte-identical to the original EigenWareSpec_Optimized.md (blueprint-aligned) as provided.
Everything below is appended only. No existing bytes were edited, removed, or rephrased.

PLACEMENT TAGS
- The blocks below are intended to be inserted later into the indicated sections without semantic changes.
- Until a future in-place merge is explicitly authorized, the canonical below is the canonical source for these operator definitions.

[PLACEMENT TAG] Section 1 -> Subsection 1.5

END APPENDIX
```

APPEND-ONLY DETERMINISM CLOSURE canonical
(Binds remaining symbols and primitives for Sections 1.6, 2.5, and 5.17)

[This canonical SHALL be interpreted as appended content belonging to the referenced sections.
No existing bytes above are modified. All statements below are authoritative where they resolve
previously-unbound symbols or leave ambiguity that would permit non-deterministic implementations.]

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

1.6.5 Derivable Calculations and Authorities
Let amplitude be the signed temporal-field coupling scalar in the lattice (A_tau), evaluated per-tick
from the authoritative lattice state and constraint pulses.

Define the local proper-time increment as:
    d_tau = d_t / A_tau

Constraints:
- A_tau MAY be positive or negative.
- A_tau SHALL NOT be clamped by min(), max(), abs(), or saturation operators for the purpose of time evolution.
- A_tau SHALL NOT be forced to be >= 1 or <= 1.

Zero handling (deterministic, non-clamped):
If A_tau == 0 exactly in the canonical fixed-point domain, then:
    d_tau = 0
and the evolution operator for that tick SHALL be the identity for the affected scope, and the state SHALL
be routed through the canonical non-projecting / dark-excitation accounting path (if defined), preserving
energy/curvature bookkeeping without projecting new observables.

Directionality:
- Forward evolution corresponds to sign(d_tau) > 0.
- Reverse evolution corresponds to sign(d_tau) < 0 and SHALL be implemented as the deterministic inverse
  of the forward operator (see 1.6.6).

Authority:
- d_t is the canonical base tick increment as defined elsewhere in this specification.
- A_tau is derived from existing pulse/constraint bindings; no free parameters are introduced here.

1.6.5 Dependencies
- Canonical fixed-point domain for A_tau comparison to zero (exact equality in fixed-point).
- Canonical energy/phase accounting path for non-projecting / dark-excitation routing (if present).
- The evolution primitive defined in 1.6.6.
--------------------------------------------------------------

1.6.5.1 Description

Section 1.6 defines a proper-time lapse L(A) derived from amplitude with:
  L(A) = 1 / max(a_min, A)
  d_tau = L(A) * d_t

This binding is explicitly dilation-only under the canonical frozen snapshot.

The constant a_min is defined as:
  a_min = 1.0

Therefore A >= a_min implies L(A) <= 1.0, and d_tau <= d_t always. Proper time SHALL NOT
advance faster than the base tick under this revision. This is intentional and serves as a
determinism and causality stabilizer for early implementation, preventing "time contraction"
modes from being introduced implicitly by inference.

1.6.5.2 Execution Role

- Enforces that the simulation's internal proper-time rate never exceeds the base tick rate.
- Ensures that any "multiple actions/thoughts per pulse" compounding occurs via eigen-mode
  integration efficiency and phase-transition gating, not by permitting locally-superlinear
  proper-time acceleration.
- Prevents accidental introduction of contraction semantics by implementers attempting to
  optimize, parallelize, or "normalize" amplitude.

1.6.5.3 Derivable Calculations and Authorities

- Authority: Section 1.6.4 (a_min definition) and the lapse definition in Section 1.6.2.
- Derived invariant:
    A >= a_min = 1.0  =>  max(a_min, A) = A
    L(A) = 1 / A
    L(A) in (0, 1]
    d_tau = d_t / A  <= d_t

Any revision that permits L(A) > 1.0 SHALL be treated as a future spec change and MUST be
explicitly defined in-spec with updated closure proofs (phase closure, causality, and
energy/curvature accounting), and SHALL NOT be introduced as a heuristic.

1.6.5.4 Dependencies

- Depends on: a_min definition already bound in Section 1.6.4.
- Depends on: Appendix D.11-R phase fixed-point domain rules for the use of turns and wrapping
  when d_tau contributes to phase evolution (see Appendix D.11-R "Phase deltas are in turns...").

--------------------------------------------------------------
1.6.6 Description
This subsection binds the canonical evolution operator to the fixed-point phasor primitive and
defines reverse evolution as the deterministic inverse under negative d_tau.

1.6.6 Execution Role
Provides a single authoritative operator family for forward and reverse evolution, ensuring that
any reverse-step is an exact inverse (within the fixed-point domain) of the corresponding forward-step.

1.6.6 Derivable Calculations and Authorities
Let H_eff be the effective Hamiltonian/operator for the relevant scope, computed using effective constants
per the canonical correlation model.
Let d_tau be as defined in 1.6.5 (signed; unclamped).

Forward/Reverse unified evolution:
    U(d_tau) = cis_fp( - H_eff * d_tau / hbar_eff )

Where:
- cis_fp(z) is the canonical fixed-point complex phasor primitive (CORDIC/LUT only).
- No platform floating trig, exp(), cos(), sin(), or complex library exponentials are permitted for authoritative evolution.

Invertibility requirement:
    U(-d_tau) SHALL be the deterministic inverse of U(d_tau) in the fixed-point domain.
For unitary-style operators this implies:
    U(-d_tau) = conj_transpose_fp( U(d_tau) )

Implementation SHALL use the same cis_fp primitive and fixed-point rounding rules for both directions.

1.6.6 Dependencies
- cis_fp primitive definition and fixed-point domain constraints (CORDIC/LUT).
- conj_transpose_fp (or equivalent) in the same fixed-point domain.
- Effective constants pipeline providing hbar_eff and operator scaling.
--------------------------------------------------------------

1.6.6.1 Description

Section 1.6 references:
  delta_phi = f_phase(d_tau, impulse_observation, effective_constants(...))

This subsection binds f_phase to the canonical fixed-point phase delta domain and prohibits
platform-dependent trig.

f_phase SHALL compute a phase increment in turns, expressed in the canonical fixed-point unit:
  theta_scale units per turn

and SHALL wrap to the shortest signed turn-distance prior to normalization.

1.6.6.2 Execution Role

- Makes the phase increment operator concrete so it cannot be substituted by an implementer with
  non-deterministic floating math, platform trig, or alternative units (radians vs turns).
- Ensures the phase update in Section 1.6 remains compatible with the phase fixed-point domain
  used across the specification and required by Appendix D.11-R.

1.6.6.3 Derivable Calculations and Authorities

Authoritative domain (Appendix D.11-R):
- Phase deltas are in turns and wrapped to shortest signed distance in [-0.5, 0.5) turns before
  normalization.
- theta_scale = 10^18 units per turn (fixed-point).

Binding:

Let:
- omega_turns_per_base_tick_q be the fixed-point angular rate derived from impulse_observation
  and effective_constants(...) in units of (turns per base tick) * theta_scale.
  (The derivation of omega_turns_per_base_tick_q MUST be resolved only from existing artifacts
   referenced by impulse_observation and effective_constants(...); no new coefficients are allowed.)

Let:
- d_tau_over_d_t_q be the fixed-point lapse ratio L(A) in units of theta_scale per 1.0, derived
  exactly from Section 1.6.2 and 1.6.4 (dilation-only).

Then f_phase SHALL be:

  delta_phi_turns_q = wrap_turns_q( mul_q(omega_turns_per_base_tick_q, d_tau_over_d_t_q) )

Where:
- mul_q is fixed-point multiply with deterministic rounding (as defined by the phase fixed-point
  domain hygiene rules).
- wrap_turns_q wraps in turns to the canonical shortest signed distance range.

Prohibition:
- exp(), sin(), cos(), atan2(), and equivalent circular primitives MUST NOT be invoked via platform
  math libraries for core state evolution. If circular primitives are needed (e.g., for circular
  means), they SHALL use the canonical fixed-point phasor primitives (CORDIC/LUT only), as already
  required elsewhere in Appendix D.11-R.

1.6.6.4 Dependencies

- Appendix D.11-R phase fixed-point domain (theta_scale; wrapping interval; deterministic rounding).
- Any artifact(s) that define impulse_observation and effective_constants(...), which SHALL be used
  to derive omega_turns_per_base_tick_q without introducing new free parameters.

--------------------------------------------------------------
2.5.6 Q_phi Binding to Canonical Phase Fixed-Point Domain (Append-Only)
--------------------------------------------------------------

2.5.6.1 Description

Section 2.5 uses a phase-bucket detector based on fixed-point quantization and references a
quantization scale Q_phi. This subsection binds Q_phi deterministically.

2.5.6.2 Execution Role

- Eliminates the remaining degree of freedom where Q_phi could be chosen arbitrarily.
- Forces all phase-bucket detection and phase-transition gating to share the same canonical scale
  used across modules, preventing cross-module divergence.

2.5.6.3 Derivable Calculations and Authorities

Authority:
- Appendix D.11-R defines the canonical phase fixed-point domain:
    theta_scale = 10^18 units per turn

Binding:
- Q_phi SHALL be exactly equal to theta_scale.
- Any quantized phase value phi_q in this spec SHALL be interpreted as:
    phi_turns = phi_q / theta_scale

Therefore, in Section 2.5 phase bucket detection:
- Any bucket/index computed from phase SHALL use the theta_scale unit and the canonical wrapping
  interval in turns.
- No alternative quantization constants, radian scaling, or per-module "phase scales" are permitted.

2.5.6.4 Dependencies

- Appendix D.11-R phase fixed-point domain (theta_scale).

--------------------------------------------------------------
2.5.7 Allowed exp/cos Primitives (Append-Only)
--------------------------------------------------------------

2.5.7.1 Description

Section 2.5 includes expressions that are traditionally written with exp() and cos() (e.g.,
mode evolution in complex phasors and coupling strength proportional to cos(delta_phi)). This
subsection binds these to deterministic fixed-point primitives and forbids platform trig.

2.5.7.2 Execution Role

- Ensures that any projection-only circular primitives (cis/exp(i*theta), cos, atan2) are computed
  deterministically and reproducibly across platforms. Canonical evolution uses only integer ring math (no trig).
- Aligns Section 2.5 with the already-authoritative requirement that circular means and related
  computations use fixed-point phasors via CORDIC/LUT only.

2.5.7.3 Derivable Calculations and Authorities

Authority:
- Appendix D.11-R requires circular mean and phase operations be computed using fixed-point phasors
  (CORDIC/LUT only) and deterministic integer math.

Bindings:

A) exp(-i * omega_k * d_tau) in Section 2.5.3

Implementations SHALL NOT compute complex exponentials via floating exp/sin/cos. Instead:

- Represent the phasor p_k as:
    p_k = cis_q(phi_k)  where cis_q uses fixed-point CORDIC/LUT over the canonical phase domain
    phi_k is in turns (fixed-point; scale Q_phi = theta_scale)

Then the update is:
    c_k(t+1) = c_k(t) * p_k_delta
where:
    p_k_delta = cis_q( delta_phi_k_turns_q )

B) cos(delta_phi) in the harness coupling

cos(delta_phi) SHALL be computed only via:
- fixed-point CORDIC/LUT cos_q(delta_phi_turns_q), or
- derived from cis_q(delta_phi) by taking the real component with deterministic rounding.

No other trig implementations are permitted for canonical compliance.

2.5.7.4 Dependencies

- Appendix D.11-R phase fixed-point domain (theta_scale) and fixed-point phasor requirement
  (CORDIC/LUT only).

--------------------------------------------------------------
2.5.8 Deterministic argmax and Top-K Tie-Break Rules (Append-Only)
--------------------------------------------------------------

2.5.8.1 Description

Section 2.5 defines dominant mode selection via:
  k_star(t) = argmax_k |c_k(t)|

and Section 5.17 display delta signaling references "top-K by magnitude". This subsection binds
tie-breaking rules deterministically.

2.5.8.2 Execution Role

- Prevents platform-dependent tie behavior (e.g., numpy argmax stability differences).
- Ensures deterministic selection when magnitudes are equal due to quantization or symmetry.

2.5.8.3 Derivable Calculations and Authorities

Authority:
- Deterministic tie-break precedent exists in-spec (e.g., "deterministic tie-break: lowest axis
  index wins ties" for rounding remainders in integer weight normalization).

Bindings:

A) argmax tie-break

Define:
  m_k = abs_q(c_k)   (fixed-point magnitude; deterministic rounding)

Then:
  k_star = argmax_k (m_k, tie_break = lowest_index)

Meaning:
- If there exist multiple indices k with identical maximal m_k, select the smallest k.

B) Top-K ordering tie-break

For any selection of Top-K items by magnitude (e.g., delta packets):
- Sort keys SHALL be:
    primary: magnitude descending
    secondary: index ascending (lowest index wins ties)
- The sort MUST be stable under identical keys (deterministic stable ordering).

This rule applies to:
- selecting top-K eigen coefficient deltas,
- selecting top-K tile/pixel-region deltas (if used),
- any other magnitude-ranked emission list used for signaling.

2.5.8.4 Dependencies

- Depends on the fixed-point magnitude definition already used for coefficients in Section 2.5.
- Depends on the existing deterministic tie-break precedent referenced above.

--------------------------------------------------------------
5.17.99 Display Constructor Determinism canonical (Append-Only)
--------------------------------------------------------------

5.17.99.1 Description

The display constructor block in Section 5.17 is a presentation consumer of simulation state.
This canonical pins down the determinism of delta selection, ordering, and phase-domain operations
used during signaling so implementations cannot drift across platforms (desktop vs phone).

5.17.99.2 Execution Role

- Makes delta signaling deterministic across devices and OS compositor behaviors.
- Ensures a presentation surface cannot introduce non-deterministic math into the simulation by
  "helpfully" using platform trig or non-stable sorting.

5.17.99.3 Derivable Calculations and Authorities

- Delta selection Top-K tie-break is bound by Section 2.5.8:
    sort by (magnitude desc, index asc), stable ordering.

- Any phase-coded display deltas (if the presentation surface exposes phase-like observables) SHALL
  use Q_phi = theta_scale and the Appendix D.11-R fixed-point domain.

- Any circular/trig operations used for display-only visualization MUST still use the canonical
  fixed-point phasor primitives (CORDIC/LUT only) if the results are emitted as part of a delta
  packet that is persisted, transmitted, or compared in contract harnesses. Platform trig MAY be
  used ONLY for non-authoritative, purely local visualization that is not stored, coord_sig-mapped, compared,
  or used for gating decisions.

5.17.99.4 Dependencies

- Section 2.5.6 (Q_phi binding) and Appendix D.11-R phase fixed-point domain.
- Section 2.5.8 (tie-break rules).
- Appendix D.11-R fixed-point phasor requirement (CORDIC/LUT only) for authoritative circular ops.

----------------------------------------------------------------
1.6.7 Description
Reverse-time evolution SHALL be implemented by applying the same canonical evolution operator
with a signed negative proper-time increment (d_tau < 0). Reverse-time is not a separate mechanism.

1.6.7 Execution Role
This subsection defines the mandatory reversibility contract for any scope that claims reversible evolution.
It binds "reverse time" to operator inversion in the canonical fixed-point domain and forbids any
non-deterministic math paths that would break byte-stable behavior across hosts.

1.6.7 Derivable Calculations and Authorities
Let U(d_tau) be the canonical evolution operator defined in 1.6.6, constructed solely using cis_fp and
fixed-point arithmetic in the canonical domain.

Forward tick:
    state_next = U(+d_tau) * state_now

Reverse tick:
    state_prev = U(-d_tau) * state_now

Reversibility requirement (reversible scopes only):
    U(-d_tau) * U(+d_tau) = I_fp
    U(+d_tau) * U(-d_tau) = I_fp

Where I_fp is the identity operator in the canonical fixed-point domain.

If U is unitary-style in the canonical domain, then inversion SHALL be implemented as:
    U(-d_tau) = conj_transpose_fp( U(+d_tau) )

No alternative inverse computation is permitted for authoritative evolution.

Determinism requirement:
- All phase rotation / phasor generation MUST use cis_fp (CORDIC/LUT only) with specified rounding.
- No platform floating exp(), cos(), sin(), or complex exponential implementations are permitted in
  authoritative evolution paths, including eigenmode evolution.

1.6.7 Dependencies
- 1.6.6 canonical U(d_tau) definition using cis_fp.
- conj_transpose_fp (or equivalent) defined in the same fixed-point domain.
- Fixed-point identity operator I_fp definition for the scope.

----------------------------------------------------------------
1.6.8 Description
Non-unitary and dissipative operators SHALL be explicitly classified as non-invertible. Reverse-time
semantics SHALL NOT claim perfect reconstruction across any non-invertible step.

1.6.8 Execution Role
This subsection prevents incorrect "reverse time" claims in the presence of damping, projection,
threshold routing, collapse, or any operator that discards information. It defines the required
accounting behavior when reverse steps traverse non-invertible transitions.

1.6.8 Derivable Calculations and Authorities
Classification:
An operator step S is reversible if and only if there exists an inverse S_inv in the canonical fixed-point
domain such that:
    S_inv( S(x) ) = x  for all x in the reachable state set of that scope.

If a step applies any of the following, it SHALL be classified as non-invertible:
- magnitude damping / leakage that reduces information content
- projection / collapse / quantization that discards phase detail
- threshold-based routing that irreversibly changes representation
- saturation / clipping / truncation not paired with an explicit invertible encoding

Reverse traversal rule:
When reverse-time evolution traverses a non-invertible step, the implementation SHALL:
1) apply the defined reverse operator where available, and
2) route any non-reconstructible residual into the canonical non-projecting / dark-excitation
   accounting path (if defined), preserving energy/curvature bookkeeping while not projecting
   new observables.

The above routing is mandatory for determinism and conservation-style accounting within the
engine's internal rules.

1.6.8 Dependencies
- Canonical definitions for non-projecting / dark-excitation accounting (if present in this spec).
- Fixed-point domain definitions for exact equality / classification.
- Logging / telemetry contract for recording non-invertible traversal (if present).

----------------------------------------------------------------
1.6.9 Description
Authoritative circular functions SHALL be implemented exclusively via fixed-point phasor primitives.

1.6.9 Execution Role
Binds all projection-only uses of exp(i*theta), cos(theta), sin(theta), cis(theta), and equivalent phase rotation
to a deterministic primitive set. Canonical evolution MUST NOT depend on these primitives (Blueprint APPENDIX AD).

1.6.9 Derivable Calculations and Authorities
Allowed authoritative primitives:
- cis_fp(theta_fp): returns (cos_fp(theta_fp), sin_fp(theta_fp)) in the canonical fixed-point domain.
- cexp_fp(i_theta_fp): equivalent to cis_fp(theta_fp); no separate implementation is permitted.
- ccos_fp(theta_fp): derived from cis_fp(theta_fp) by selecting the cosine component only.
- csin_fp(theta_fp): derived from cis_fp(theta_fp) by selecting the sine component only.

Forbidden in authoritative paths:
- platform exp(), cos(), sin(), tan(), atan2(), complex exponentials
- GPU/CPU vendor intrinsic trig
- mixed-precision float trig, including "fast math" modes

Any visualization-only pipeline MAY use platform trig provided it is explicitly marked non-authoritative
and does not feed back into canonical state evolution.

1.6.9 Dependencies
- Fixed-point domain definition for theta_fp (turns / wrap interval / scaling) from the canonical phase domain.
- cis_fp implementation constraints (CORDIC/LUT, rounding, overflow behavior).

```

Sections 1-3 Verification Snapshot Continuation
(Canonical Promotion Block; appended; authoritative)

This block is within canonical snapshot scope.
The Appendix code-fence above is closed to ensure all content below is not treated as non-snapshot
reference material. The requirements in this block are enforceable and SHALL be used by implementers.

----------------------------------------------------------------
1.6.5 Description
Temporal lapse SHALL be bi-directional and unclamped.
No global minimum or maximum bounds (min/max clamps) are permitted on the lapse or on d_tau.

1.6.5 Execution Role
Defines the authoritative mapping allowing forward (positive proper-time) and reverse (negative proper-time)
evolution as deterministic inverses under the same fixed-point operators.

1.6.5 Derivable Calculations and Authorities
Let A_tau be the signed temporal-field coupling scalar in the lattice, evaluated per tick from authoritative
lattice state and constraint pulses.

    d_tau = d_t / A_tau

Zero handling (deterministic):
If A_tau == 0 exactly in the canonical fixed-point domain, then:
    d_tau = 0
and the evolution operator for that tick SHALL be identity for the affected scope. Any residual accounting
SHALL route through the canonical non-projecting / dark-excitation path (if defined), preserving energy/
curvature bookkeeping without projecting new observables.

1.6.5 Dependencies
- Canonical fixed-point domain for A_tau exact-equality comparison to zero.
- Non-projecting / dark-excitation accounting path definition (if present elsewhere in this spec).
- Evolution primitive in 1.6.6.

1.6.6 Execution Role
Provides a single authoritative operator family for both forward and reverse evolution; eliminates platform-
dependent math paths.

1.6.6 Derivable Calculations and Authorities
Let H_eff be the effective operator for the scope, computed using effective constants per canonical correlation.
Let d_tau be as defined in 1.6.5 (signed; unclamped).

    U(d_tau) = cis_fp( - H_eff * d_tau / hbar_eff )

Authoritative primitive constraints:
- cis_fp MUST be implemented via deterministic fixed-point CORDIC/LUT only.
- platform floating exp(), cos(), sin(), complex exponentials, or vendor intrinsics are forbidden in authoritative
  evolution paths.

Invertibility requirement (reversible scopes):
    U(-d_tau) SHALL be the deterministic inverse of U(d_tau) in the fixed-point domain.
For unitary-style operators:
    U(-d_tau) = conj_transpose_fp( U(d_tau) )

1.6.6 Dependencies
- cis_fp and conj_transpose_fp in the canonical fixed-point domain.
- Effective constants pipeline providing hbar_eff.

----------------------------------------------------------------
1.6.7 Description
Reverse-time evolution SHALL be implemented by applying U(d_tau) with d_tau < 0.
Reverse-time is not a separate mechanism.

1.6.7 Execution Role
Defines the reversibility contract for any scope claiming reversibility; enforces byte-stable behavior across hosts.

1.6.7 Derivable Calculations and Authorities
Forward tick:
    state_next = U(+d_tau) * state_now
Reverse tick:
    state_prev = U(-d_tau) * state_now

Reversibility requirement (reversible scopes only):
    U(-d_tau) * U(+d_tau) = I_fp
    U(+d_tau) * U(-d_tau) = I_fp

1.6.7 Dependencies
- 1.6.6 U(d_tau) definition.
- I_fp identity operator in canonical fixed-point domain.

1.6.8 Execution Role
Prevents incorrect "rewind" claims when information is discarded; defines required residual accounting behavior.

1.6.8 Derivable Calculations and Authorities
A step is reversible iff an inverse exists in the canonical fixed-point domain over the reachable state set.
Any step that applies damping/leakage, projection/collapse/quantization, irreversible routing, saturation/clipping,
or truncation without an explicit invertible encoding SHALL be classified as non-invertible.

When reverse traversal crosses a non-invertible step, implementation SHALL route non-reconstructible residual into
the canonical non-projecting / dark-excitation accounting path (if defined), preserving conservation-style accounting.

1.6.8 Dependencies
- Non-projecting / dark-excitation accounting definition (if present).
- Fixed-point domain definitions for classification predicates.

----------------------------------------------------------------
1.6.9 Description
Authoritative circular functions SHALL be implemented exclusively via fixed-point phasor primitives.

1.6.9 Execution Role
Binds all projection-only exp(i*theta), cos(theta), sin(theta), and phase rotation to deterministic primitives. Canonical evolution does not use trig.

1.6.9 Derivable Calculations and Authorities
Allowed authoritative primitives:
- cis_fp(theta_fp) -> (cos_fp, sin_fp)
- cexp_fp(i_theta_fp) -> equivalent to cis_fp
- ccos_fp(theta_fp) -> cosine component of cis_fp
- csin_fp(theta_fp) -> sine component of cis_fp

Forbidden in authoritative paths:
- platform exp/cos/sin/tan/atan2/complex exponentials
- GPU/CPU vendor intrinsic trig
- mixed-precision float trig / "fast math" paths

Visualization-only MAY use platform trig only if explicitly marked non-authoritative and never feeds back into canonical
state evolution.

1.6.9 Dependencies
- Canonical theta_fp scaling and wrap rules.
- cis_fp implementation constraints (CORDIC/LUT; rounding; overflow behavior).

----------------------------------------------------------------
2.5.6 Description
Q_phi is a fixed quantization scale for phase bucket indexing and SHALL be explicitly bound.

2.5.6 Execution Role
Eliminates implementation ambiguity for phase bucket detection and gating.

2.5.6 Derivable Calculations and Authorities
Binding:
    Q_phi = theta_scale

Where theta_scale is the canonical phase fixed-point scaling constant defined elsewhere in this specification.

2.5.6 Dependencies
- theta_scale definition in canonical phase fixed-point domain.

----------------------------------------------------------------
2.5.7 Description
All exp/cos/sin behavior referenced by eigen evolution or harness coupling SHALL use the canonical fixed-point phasor
primitive set.

2.5.7 Execution Role
Prevents platform trig divergence and preserves deterministic verification for the harness.

2.5.7 Derivable Calculations and Authorities
For coefficient evolution:
    c_k(t+1) = c_k(t) * cis_fp( - omega_k * d_tau )

For harness coupling terms expressed as cos(delta_phi):
    cos(delta_phi) SHALL be computed as ccos_fp(delta_phi_fp) derived from cis_fp in the canonical fixed-point domain.

2.5.7 Dependencies
- cis_fp, ccos_fp, and canonical delta_phi_fp scaling.

----------------------------------------------------------------
2.5.8 Description
Argmax and top-K selection SHALL have explicit deterministic tie-break rules.

2.5.8 Execution Role
Prevents host/library-dependent behavior (e.g., numpy ties) from altering canonical outcomes.

2.5.8 Derivable Calculations and Authorities
For argmax over magnitude:
- Primary key: larger |c_k| wins.
- Tie-break key (exact equality in fixed-point): lowest index k wins.

For top-K by magnitude:
- Sort by descending |c_k|.
- For ties at any rank, order by ascending k.
- Truncate to first K after applying the stable ordering above.

2.5.8 Dependencies
- Canonical fixed-point definition of equality for |c_k| comparisons.

----------------------------------------------------------------
5.17.99 Description
Presentation delta signaling to monitors/phones SHALL be deterministic and SHALL not back-propagate into canonical state.

5.17.99 Execution Role
Defines the output interface as a sampler/subscriber of the latest committed canonical state, with optional sparse delta
updates for bandwidth/power efficiency. This block binds tie-breaks and ordering for any top-K/ROI selection in output.

5.17.99 Derivable Calculations and Authorities
If an output channel transmits deltas (tiles, pixels, or eigen-coefficient updates), it SHALL:
- compute deltas in a deterministic order,
- apply top-K selection using the tie-breaks defined in 2.5.8,
- encode all phase-related values using theta_scale and cis_fp-derived primitives where applicable.

Display refresh cadence is external; presentation MAY be throttled, but throttling SHALL NOT modify canonical state.

5.17.99 Dependencies
- 2.5.8 deterministic selection rules.
- theta_scale and fixed-point phase domain.
- The GUI/output subsystem is non-authoritative by default unless explicitly declared otherwise.

Sections 4-7 and 9 Verification Snapshot Continuation
(Artifact bindings and runtime sequence; appended; authoritative)

----------------------------------------------------------------
Section 4 - Ingestion, Normalization, Encoding, and Coherence Metrics
----------------------------------------------------------------

4.1 Description
Section 4 defines the authoritative ingestion-to-encoding pipeline that produces canonical text records,
phase/theta signals, coherence (chi) pressures, and VSD snapshots from crawled corpora without guesswork.

4.1 Execution Role
This section binds the ingestion and encoder operators to concrete program artifacts and exports,
so implementers may not invent alternate pipelines or hidden constants.

4.1 Derivable Calculations and Authorities
The authoritative ingestion + encoding operator chain SHALL be implemented by the following artifacts:

A) Crawl / ingest (HTML -> text, metrics, and ingest results)
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/tools/cli_main.cpp
- Authoritative Exports:
  - class WebCrawler
  - class CrawlerPhaseConfig
  - class IngestionMetrics
  - class CrawlerIngestResult
  - function html_to_text

B) Deterministic normalization and watermark separation (pre-encode hygiene, deterministic)
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/scheduler/pulse_scheduler.cpp
- Authoritative Exports:
  - WATERMARK_PATTERNS_V1
  - function separate_watermark_blocks
  - function normalize
  - function is_ascii_printable

C) Phase/theta mapping and coherence pressure (encoder core)
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/scheduler/pulse_scheduler.cpp
- Authoritative Exports:
  - function char_phase
  - function pico_to_theta
  - function pico_pressure
  - function wrapped_distance
  - function coherence_pressure
  - function chi_init
  - function reinforce_anchor
  - class Anchor
  - class VSDState

D) Recordization and VSD snapshot materialization (container-facing encoding)
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/scheduler/pulse_scheduler.cpp
- Authoritative Exports:
  - function vsd_to_records
  - function records_to_vsd
  - function vsd_to_snapshot
  - function vsd_from_snapshot
  - function encode_text

Operational constraint:
- Any symbol introduced in Section 4 (including chi, theta, pico pressure, anchor minting, and recordization)
  SHALL resolve to one of the above exports. Alternate implementations are prohibited.

4.1 Dependencies
- Section 2 canonical phase fixed-point domain and theta scaling authorities (theta_scale; cis_fp family).
- Section 6 container record families and coord_sig rules (Pulse/Band/Binding/Manifest).
- Section 9 contract harness enforcement for deterministic ingestion/encoding.

----------------------------------------------------------------
Section 5 - Boot Chain, Runtime Services, VHW Compute Fabric, and Output Surfaces
----------------------------------------------------------------

5.1 Description
Section 5 defines the runtime sequence from BIOS boot through EigenWare engine activation, VHW compute
fabric initialization, and non-authoritative output surfacing (GUI, monitor, phone displays).

5.1 Execution Role
This section binds the boot chain and service orchestration to concrete artifacts and exports so the runtime
sequence is implementable and replayable without interpretation.

5.1 Derivable Calculations and Authorities
Authoritative runtime sequence (purpose-first dependency flow):

(1) BIOS boot / readiness / event bus
- Program Artifacts:
  - eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/boot/device_probe.cpp
  - eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/io/host_io.cpp
- Authoritative Exports:
  - class BIOSBoot
  - function start
  - class EventBus
  - function get_event_bus
  - class BIOSUtilities (bios_utils)

(2) Runtime services registry and supervisors
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/scheduler/pulse_scheduler.cpp
- Authoritative Exports:
  - class ServiceRegistry
  - function health_snapshot
  - function start_prediction_supervisor

(3) VHW compute fabric and GPU initiation
- Program Artifacts:
  - eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/scheduler/pulse_scheduler.cpp
  - eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/boot/device_probe.cpp
- Authoritative Exports:
  - class ComputeManager
  - class ComputeLane
  - class TierSpec
  - function _mk_logger
  - class GPUInitiator
  - function detect_gpu
  - function warmup_kernels

(4) EigenWare engine activation (cognition/encode/ingest orchestration)
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/EigenWare/engine.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y)
- Authoritative Exports:
  - class EigenWareEngine
  - class EigenAnchor
  - function get_engine

Output authority rule:
- Output surfaces are samplers of committed canonical state unless explicitly declared authoritative.
- Display deltas SHALL NOT back-propagate into canonical evolution (see 5.17.99).

5.1 Dependencies
- Core physics/evolution operators (kernel/*.cu + core/*.cpp per Blueprint APPENDIX AB/Y) for H_eff, lattice state, pulse construction.
- BIOS event bus and service registry for sequencing.
- VHW compute fabric availability for kernel-backed evolution and encode throughput.

----------------------------------------------------------------
Section 6 - VSD Containers, Record Families, and Snapshot Serialization
----------------------------------------------------------------

6.1 Description
Section 6 defines the authoritative container record families and serialization operators that store and
rehydrate VSD state deterministically.

6.1 Execution Role
Binds container record definitions, coord_sig rules, and snapshot encode/decode to concrete artifacts.

6.1 Derivable Calculations and Authorities
Authoritative record families and container contracts:
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/boot/abi_manifest.h + kernel/abi/kernel_contract.h
- Authoritative Exports:
  - class PulseRecord
  - class BandRecord
  - class BindingRecord
  - class ManifestRecord
  - class Container
  - function build_container
  - function sig9_rules

Authoritative VSD <-> records <-> snapshot operators:
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/scheduler/pulse_scheduler.cpp
- Authoritative Exports:
  - function vsd_to_records
  - function records_to_vsd
  - function vsd_to_snapshot
  - function vsd_from_snapshot

Determinism constraint:
- Any container coord_sig, record ordering, and serialization MUST conform to the coord_sig and stability tests in
  Section 9 (contract harness).

6.1 Dependencies
- Section 9 harness tests for deterministic container coord_sig and fail-closed behavior.
- Section 4 encoder recordization operators.
- Canonical ASCII-only constraints for stored artifacts where required by policy.

----------------------------------------------------------------
Section 7 - External Integration, Extension Boundary, and Client Contract
----------------------------------------------------------------

7.1 Description
Section 7 defines the external integration boundary and the client contract used by extensions and API
integration surfaces.

7.1 Execution Role
Binds the extension client and ingest API surface to concrete artifacts, ensuring one-way integration
and preventing unauthorized dependency inversion.

7.1 Derivable Calculations and Authorities
Authoritative extension client contract:
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/tools/cli_main.cpp
- Authoritative Exports:
  - class EigenWareClient
  - class EigenWareClientConfig
  - function make_client
  - function request_encode
  - function request_ingest
  - function request_health
  - function request_status

Authoritative ingest API surface:
- Program Artifact: DMT/APIs/project_ingest_api.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y)
- Authoritative Exports:
  - function ingest_project_paths
  - function ingest_text_blob
  - function ingest_zip_bundle

Dependency rule:
- Extensions MAY depend on VHW-exposed integration APIs retrievable through GUI/System settings interfaces.
- Extensions SHALL NOT directly import or mutate core/bios/VHW canonical evolution logic.

7.1 Dependencies
- BIOS service discovery endpoints (runtime health/status) as exposed via client contract.
- Section 4 ingestion/encoding pipeline for canonical data formation.

----------------------------------------------------------------
Section 9 - Contract Harness, Deterministic Tests, and Fail-Closed Enforcement
----------------------------------------------------------------

9.1 Description
Section 9 defines the authoritative contract harness used to enforce determinism, registry enforcement,
coord_sig stability, budget caps, and fail-closed behavior.

9.1 Execution Role
Binds the harness tests and container rules to concrete exports, making the enforcement suite part of the
canonical implementation contract.

Harness execution artifact:
- Program Artifact: eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/EigenWare/run_contract_harness.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y)
- Authoritative Export:
  - (module entrypoint invoking contract_harness.run_all_tests)

Enforcement constraint:
- Implementations MUST pass the harness suite; failures SHALL be treated as invalid implementations.

9.1 Dependencies
- Section 4 normalization and watermark separation rules (encoder.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y)).
- Section 6 container record families and coord_sig rules.
- Section 2 fixed-point phase authorities for deterministic phase math where referenced.

Canonical Binding Conflict Rule
(Authoritative; appended)

Rule:
If multiple authoritative export bindings for the same program artifact appear in this specification,
the binding that appears later in the file SHALL govern. Implementations MUST follow the governing
binding and MUST NOT introduce alias exports.

Binding Corrections (No Aliases; Code-Conformant)
(Authoritative; appended)

----------------------------------------------------------------
5.1.3 VHW compute fabric and GPU initiation (Binding Corrections)
----------------------------------------------------------------

5.1.3 Description
This subsection binds GPU initiation exports to the exact identifiers present in the repository.
No alias exports are permitted.

5.1.3 Execution Role
Eliminates ambiguity in GPU initiator wiring and prevents implementers from inventing alternate
entrypoints.

5.1.3 Derivable Calculations and Authorities
Program Artifact:
- eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/core/boot/device_probe.cpp

Governing Authoritative Exports:
- class _GPUInitiator
- function start_gpu_initiator
- function stop_gpu_initiator

Authority:
All runtime services that start or stop GPU initiation SHALL call start_gpu_initiator and stop_gpu_initiator.
No references to non-existent exports are permitted.

5.1.3 Dependencies
- BIOS/runtime service orchestration (Section 5 boot chain).
- VHW compute manager integration (if present elsewhere).

----------------------------------------------------------------
5.1.5 Output surfaces (non-authoritative presentation) (Binding Corrections)
----------------------------------------------------------------

5.1.5 Description
This subsection binds device-safe ASCII filtering exports to the exact identifiers present in the repository.

5.1.5 Execution Role
Prevents output surfaces and extension ingress from inventing ASCII guard functions.

5.1.5 Derivable Calculations and Authorities
Program Artifact:
- DMT/System/eigenware_ascii_filter.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y)

Governing Authoritative Exports:
- function sanitize_text
- function sanitize_file
- function sanitize_project

Authority:
Any module requiring device-safe ASCII sanitization SHALL call sanitize_text/sanitize_file/sanitize_project.
No alternate ASCII filtering entrypoints are permitted unless explicitly defined in this specification.

5.1.5 Dependencies
- ASCII-only constraint rules (if present elsewhere in this specification).

----------------------------------------------------------------
7.1 Authoritative extension client contract (Binding Corrections)
----------------------------------------------------------------

7.1 Description
This subsection binds the extension/client boundary to the actual stdin/stdout protocol artifact present
in the repository. No client-class aliasing is permitted.

7.1 Execution Role
Defines how extensions communicate with EigenWare using the governing protocol artifacts, ensuring
one-way integration and preventing dependency inversion.

7.1 Derivable Calculations and Authorities
Program Artifact:
- eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/tools/cli_main.cpp

Governing Authoritative Exports:
- class FileIndex
- class CodingSessionState
- function main

Authority:
Extensions SHALL communicate through the stdin/stdout protocol implemented by main() and the stateful
objects FileIndex and CodingSessionState as the canonical protocol substrate.
Implementations SHALL NOT assume the existence of EigenWareClient, EigenWareClientConfig, make_client,
or request_* exports unless they are explicitly present in code and bound here (they are not).

7.1 Dependencies
- Section 5 runtime service exposure (health/status) if routed through the client protocol.
- Section 4 ingestion/encoding endpoints invoked by the protocol.

----------------------------------------------------------------
7.2 Ingest API surface (Binding Corrections)
----------------------------------------------------------------

7.2 Description
This subsection binds the ingest API surface to the exact identifiers present in the repository.

7.2 Execution Role
Eliminates ambiguity in ingest API entrypoints used by integrations.

7.2 Derivable Calculations and Authorities
Program Artifact:
- DMT/APIs/project_ingest_api.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y)

Governing Authoritative Exports:
- function ingest_project
- function ingest_project_to_json
- function iter_files

Authority:
External integrations requiring project ingestion SHALL call ingest_project or ingest_project_to_json.
No references to ingest_project_paths, ingest_text_blob, or ingest_zip_bundle are permitted unless
they exist in code and are bound here (they are not).

7.2 Dependencies
- Section 4 ingestion/encoding pipeline.
- Section 6 VSD snapshot serialization (if ingest emits snapshots).

----------------------------------------------------------------
5.1.6 Market telemetry helpers (Binding Corrections)
----------------------------------------------------------------

5.1.6 Description
This subsection binds market telemetry helper exports to the exact identifiers present in the repository.

5.1.6 Execution Role
Prevents console/telemetry layers from inventing market helper entrypoints.

5.1.6 Derivable Calculations and Authorities
Program Artifact:
- eigenware/ (legacy path deprecated; see Blueprint APPENDIX AB)/VHW/market_utils.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y)

Governing Authoritative Exports:
- class EMA
- class ContextualWindow
- class MarketUtils

Authority:
Any module requiring market telemetry calculations SHALL use EMA, ContextualWindow, and MarketUtils
as the governing API surface. No references to MarketClock, RollingStats, or robust_midprice are permitted
unless they exist in code and are bound here (they are not).

5.1.6 Dependencies
- VHW compute fabric availability (if used for telemetry projection).

1.6.5 Precedence and Mode Selection (Authoritative)

1.6.5.P Description
This subsection defines the single governing temporal-lapse regime and eliminates incompatible
interpretations. Only one lapse regime is permitted.

1.6.5.P Execution Role
Resolves the foundational fork in temporal lapse behavior so implementations cannot diverge.

1.6.5.P Derivable Calculations and Authorities
Governing regime: Bi-directional, signed, unclamped lapse.

Definitions:
- Let A_tau be the signed temporal-field coupling scalar in the lattice (canonical fixed-point domain).
- Let d_t be the canonical base tick increment.

The governing proper-time increment SHALL be:
    d_tau = d_t / A_tau

Constraints (hard requirements):
- A_tau MAY be positive or negative.
- No min(), max(), abs(), saturation, bounding, or floor/ceiling operators are permitted on A_tau
  for the purpose of computing d_tau.
- No constant a_min (including a_min = 1.0) SHALL be applied to A_tau for lapse computation.

Zero handling (deterministic):
If A_tau == 0 exactly in the canonical fixed-point domain:
    d_tau = 0
and the evolution operator for that tick SHALL be identity for the affected scope, with any residual
accounting routed through the canonical non-projecting / dark-excitation path if defined.

Authoritative inversion:
Reverse-time evolution SHALL be implemented by applying the same canonical operator U(d_tau) with
negative d_tau (see 1.6.6 and 1.6.7).

Binding invalidation:
Any earlier statements or formulas that:
- claim "dilation-only" behavior, or
- define lapse via L(A) = 1 / max(a_min, A), or
- define d_tau using max(eps, amplitude) or any clamp-like construct,
are not permitted for implementation and SHALL be ignored by implementers.

1.6.5.P Dependencies
- Canonical fixed-point equality for A_tau comparison to zero.
- cis_fp / fixed-point phasor primitive family and inversion rules (1.6.6-1.6.9).

1.6 Implementation Callout (Authoritative)

1.6.C Description
This callout exists to prevent implementer misreads of earlier non-governing lapse text.

1.6.C Execution Role
For any implementation of temporal lapse, proper-time increment, or reverse-time evolution, the
implementation MUST follow the governing regime defined in 1.6.5.P.

1.6.C Derivable Calculations and Authorities
Mandatory reference:
- Governing lapse regime: 1.6.5.P (Bi-directional, signed, unclamped lapse; clamp-style formulas invalid).

No implementation may claim compliance with Section 1.6 unless it conforms to 1.6.5.P for lapse computation.

1.6.C Dependencies
- 1.6.5.P (Precedence and Mode Selection).

Appendix G: Symbol Table and Units (Authoritative)

Purpose:
This appendix defines every symbol that appears in any normative equation in this specification.
Implementations MUST NOT invent missing symbols or units.

----------------------------------------------------------------
G.1 Scalar time and phase symbols
----------------------------------------------------------------

G.1.1 d_t
Description: Canonical base tick increment (engine bookkeeping time).
Units: tick_units (dimensionless unit of simulation bookkeeping).
Quantization: fixed-point integer in tick_units; rounding rules as defined by G.H.* fixed-point domain.
Authority: Canonical tick derivation section in this spec (and any bound artifact providing tick policy).

G.1.2 d_tau
Description: Local signed proper-time increment used for evolution.
Units: tick_units (same unit family as d_t).
Definition: d_tau = d_t / A_tau (see 1.6.5.P).
Domain: signed fixed-point; may be positive, negative, or zero.

G.1.3 A_tau
Description: Signed temporal-field coupling scalar (amplitude) in the lattice; controls lapse.
Units: dimensionless (scale factor on time).
Domain: signed fixed-point; may be positive, negative, or zero.
Zero semantics: if A_tau == 0 exactly, then d_tau = 0 and evolution is identity for the affected scope.

G.1.4 theta_fp
Description: Canonical phase angle in fixed-point turns domain.
Units: turns (1.0 turn == 2*pi radians).
Quantization: theta_scale units per turn (theta_fp is integer in [0, theta_scale) with wrap).
Authority: theta_scale.

G.1.5 theta_scale
Description: Fixed-point scaling constant for theta_fp.
Units: units_per_turn (integer count).
Value: MUST be the repository-defined canonical value (see bound artifact or normative definition in this spec).

G.1.6 Q_phi
Description: Phase quantization scale used for phase bucket indexing.
Units: units_per_turn.
Binding: Q_phi = theta_scale (see 2.5.6).

----------------------------------------------------------------
G.2 Energy and geometry derivative symbols
----------------------------------------------------------------

G.2.1 E
Description: Canonical energy-like scalar for the scope (only valid where bound to a program artifact).
Units: energy_units (must be specified by the binding artifact; otherwise E SHALL NOT be used).
Authority: If used, MUST be bound to a named function/export in a program artifact.

G.2.2 V
Description: Canonical potential/volume-like scalar for the scope (only valid where bound to a program artifact).
Units: potential_units or volume_units (must be specified by the binding artifact; otherwise V SHALL NOT be used).
Authority: If used, MUST be bound to a named function/export in a program artifact.

G.2.3 dE_dt, dV_dt
Description: Canonical discrete derivatives over d_t for E and V.
Units: energy_units per tick_units; potential_units per tick_units.
Definition: MUST use the canonical discrete derivative operator defined in Canonical Grammar (H.*).
Note: These symbols SHALL NOT appear in normative equations unless E and V are bound to artifacts.

G.2.4 phi
Description: Phase-angle derived from a 2D derivative vector.
Units: turns.
Definition: phi = atan2_fp(dE_dt, dV_dt) in the canonical fixed-point domain (no platform atan2).
Authority: Canonical Grammar H.2.3 (atan2_fp).

Appendix H: Canonical Grammar (G.*) (Authoritative)

Purpose:
This appendix defines the canonical primitive operators and fixed-point rules required by the normative core.
All implementations MUST use these primitives; platform math libraries are forbidden where stated.

----------------------------------------------------------------
H.1 Fixed-point domains and rounding
----------------------------------------------------------------

H.1.1 Fixed-point equality
Definition: equality is exact integer equality in the defined fixed-point representation.
No epsilon-based comparisons are permitted in normative paths unless explicitly defined here.

H.1.2 Rounding rule
Definition: round_half_away_from_zero for signed divisions and fixed-point scaling, unless an operator
explicitly declares a different rounding.

H.1.3 Wrap rule for theta_fp
Definition: theta_fp values wrap modulo theta_scale.

----------------------------------------------------------------
H.2 Canonical phase primitives (no platform trig)
----------------------------------------------------------------

H.2.1 cis_fp(theta_fp) -> (cos_fp, sin_fp)
Definition: Deterministic fixed-point phasor primitive implemented via CORDIC or LUT with fully specified rounding.
Constraints: No platform floating trig. No vendor intrinsics.

H.2.2 ccos_fp(theta_fp) -> cos_fp
Definition: cosine component of cis_fp.

H.2.3 atan2_fp(y_fp, x_fp) -> theta_fp
Definition: Deterministic fixed-point atan2 implemented via CORDIC or LUT.
Constraints: No platform atan2.

H.2.4 wrapped_distance(theta_a_fp, theta_b_fp) -> delta_fp
Definition: Canonical wrapped distance in turns fixed-point domain (see encoder.cpp (legacy Python path deprecated; see Blueprint APPENDIX AB/Y) binding where applicable).

----------------------------------------------------------------
H.3 Deterministic selection and ordering
----------------------------------------------------------------

H.3.1 argmax_by_magnitude(c_k_mag_fp[]) -> k_star
Rule: primary key is larger magnitude; tie-break is lowest index.

H.3.2 topk_by_magnitude(c_k_mag_fp[], K) -> k_list
Rule: sort by descending magnitude; ties ordered by ascending index; take first K.

----------------------------------------------------------------
H.4 Canonical discrete derivatives
----------------------------------------------------------------

H.4.1 discrete_derivative(x_now, x_prev, d_t) -> dx_dt
Definition: dx_dt = (x_now - x_prev) / d_t using fixed-point division and rounding rule H.1.2.
Constraints: No floats. No eps clamps.

----------------------------------------------------------------
H.5 Contract harness obligations
----------------------------------------------------------------

H.5.1 Determinism obligation
All primitives in H.2-H.4 MUST be exercised by the contract harness tests (bound in Section 9) such that
byte-identical results are required across supported hosts.

Section 9.2 - Enforcement Kernel Choke Points (Authoritative)

9.2 Description
This section defines the minimal enforcement choke points that convert this specification from "described"
to "implemented". These choke points SHALL be implemented as single-entry gates. Downstream "fixups" are
prohibited. Any bypass is a spec violation.

9.2 Execution Role
Provides compiler-like enforcement boundaries that:
- prevent phase drift,
- prevent float math in canonical state paths,
- prevent incoherent evolution,
- prevent untracked time advancement,
- prevent unearned persistence,
- and prohibit silent recovery after physics/spec violations.

9.2 Derivable Calculations and Authorities

----------------------------------------------------------------
9.2.1 Phase Domain Enforcement (Basis9 closure)
----------------------------------------------------------------

9.2.1.1 Rule
Every phase-bearing value SHALL:
- live in the canonical phase fixed-point domain (theta_fp in turns),
- wrap deterministically modulo theta_scale,
- reject NaN/Inf/out-of-domain representations.

9.2.1.2 Single choke point
All phase-bearing values MUST pass through:
- enforce_phase_domain(theta_fp: int) -> int

Definition:
- Returns theta_fp modulo theta_scale.
- Raises PhaseViolation if input is not an int.

9.2.1.3 Authority bindings
- Grammar primitive: H.1.3 (wrap rule).
- Phase scale: G.1.5 (theta_scale).
- Tie-break rules: H.3.* (where selection depends on phase-bearing magnitude lists).

----------------------------------------------------------------
9.2.2 Fixed-Point Math Lock (no floats past ingress)
----------------------------------------------------------------

9.2.2.1 Rule
Floats are permitted ONLY at:
- ingest normalization (pre-canonical formation),
- display/logging layers (non-authoritative surfaces).

All canonical state math MUST use ints in the fixed-point domains defined by Appendix H.

9.2.2.2 Single choke point
All ingress values crossing into canonical state MUST pass through:
- lock_fixed_point_q32_32(x_q32_32: int64, scale_bits: int) -> int64 (no float ingress; x_q32_32 already fixed-point)

Definition:
- Computes int(round_half_away_from_zero(x * scale_q)).
- Raises SpecViolation if x_q32_32 is outside representable range or if scale_bits is invalid.

9.2.2.3 Authority bindings
- Rounding: H.1.2.
- Determinism obligation: H.5.1.

----------------------------------------------------------------
9.2.3 Coherence as a Hard Gate (chi forbids evolution)
----------------------------------------------------------------

9.2.3.1 Rule
No state evolution is permitted if coherence falls below minimum:
- evolution is forbidden only under explicit contract violation (ABI/feature mask) or when dispersion proxy R saturates the permitted range; no fixed CHI_MIN_Q threshold is used (Blueprint APPENDIX AG).

9.2.3.2 Single choke point
All canonical evolution steps MUST begin with:
- enforce_coherence_gate(chi_q: int, chi_min_q: int) -> None

Definition:
- Raises CoherenceCollapse if chi_q < chi_min_q.
- No logging-only behavior is permitted for this condition.

9.2.3.3 Authority bindings
- chi_q definition: bound by encoder/coherence artifacts where applicable.
- Exception hierarchy: 9.2.6.

----------------------------------------------------------------
9.2.4 Temporal Tick Authority (no free time advancement)
----------------------------------------------------------------

9.2.4.1 Rule
All mutations of canonical state MUST be a function of:
- tick_index (int),
- delta_tick (int),
and SHALL NOT use wall-clock time or implicit "now".

9.2.4.2 Single choke point
All mutating functions MUST receive a TickContext:
- class TickContext(tick_index: int, delta_tick: int)

Any mutation without TickContext is a SpecViolation.

9.2.4.3 Authority bindings
- Tick derivation and lapse regime: Section 1.6 and 1.6.5.P.
- Discrete derivative primitive: H.4.1 (where used).

----------------------------------------------------------------
9.2.5 Memory Admission Control (no unearned persistence)
----------------------------------------------------------------

9.2.5.1 Rule
A memory record may persist ONLY if:
- coherence-positive (derived from dispersion proxy R; no fixed CHI_MIN_Q threshold),
- phase-aligned with the current canonical state (as defined by the bound alignment operator),
- causally attributable to a TickContext (tick_index recorded).

9.2.5.2 Single choke point
All persistence MUST pass through:
- admit_memory(mem: object, chi_q: int, chi_min_q: int, is_phase_aligned: bool, tick: TickContext) -> object

Definition:
- Raises MemoryRejected on any failed condition.
- Stamps mem.tick_index = tick.tick_index deterministically.

9.2.5.3 Authority bindings
- Alignment operator MUST be bound to a program artifact for the relevant modality/scope.
- No direct writes to memory stores are permitted outside admit_memory.

----------------------------------------------------------------
9.2.6 Spec violation is fatal (no silent recovery)
----------------------------------------------------------------

9.2.6.1 Rule
Spec violations SHALL:
- terminate the current evolution step,
- not be silently recovered,
- be handled only by an external supervisor/runtime boundary.

9.2.6.2 Exception hierarchy (authoritative)
- class SpecViolation(Exception)
- class PhaseViolation(SpecViolation)
- FixedPointViolation is a SpecViolation code (no Python class; use EW_REQUIRE/EW_ABORT in C++ paths).
- class CoherenceCollapse(SpecViolation)
- class TickViolation(SpecViolation)
- class MemoryRejected(SpecViolation)

9.2.6.3 Catch rule
No module in the canonical evolution path may catch SpecViolation or its subclasses.
Only outer supervisory layers may catch and decide a policy response.

9.2 Dependencies
- Appendix G and H (symbol/grammar).
- Section 1.6 (tick + lapse).
- Section 6 and 9 container/harness obligations where applicable.

APPENDIX E - EigenWare Firmware Execution Explicitness Addendum

Status: Append-Only Canonical Extension
Mutation Policy: No prior content altered
Authority: Inherits all preceding sections verbatim

E.1 Purpose of This Appendix

This appendix makes explicit the execution sequence, kernel activation model, and mathematical evaluation order already defined implicitly in the canonical specification.

No symbols, equations, operators, constants, or enforcement rules are introduced or modified.

All mathematics referenced herein are defined in the canonical Equations section and are restated only to clarify execution ordering.

E.2 Firmware Execution Localization

All canonical mechanics execute within a single GPU-resident firmware kernel.

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

Execution authority and semantics remain unchanged.

E.3 Kernel Activation Sequence

At firmware activation:

(See canonical description in section: 'NAMING AND OPERATOR REGISTRY (v51)'.)

This activation occurs once per execution lifecycle.

E.4 Canonical Tick Execution Order

For each tick t, the following canonical evaluation order is enforced.

The order below reflects the existing canonical definitions and equations, expressed as an execution sequence.

E.4.1 Pulse Ingestion

Canonical pulse variables are read and applied as defined.

E.4.2 Effective Constant Evaluation

All effective constants are evaluated using the canonical effective-constant equations, including relativistic correlation and stochastic dispersion factors, prior to state evolution.

E.4.3 Phase Accumulation

Phase is advanced according to canonical phase evolution equations:

phi(t+1) = phi(t) + deltaphi

Where deltaphi is derived from pulse contribution, Hamiltonian contribution, and constraint modulation as already defined.

E.4.4 Amplitude Update

Amplitude is updated according to canonical amplitude evolution equations, incorporating dispersion and decay terms.

E.4.5 Internal Manifold Update

Internal manifold coordinates are updated using the canonical lattice and manifold equations defined in the Equations section.

E.4.6 Flow and Lorentz Dynamics

Canonical flow dynamics are evaluated using the Lorentz-style equations defined in the specification.

E.4.7 Resonance Evaluation

Resonance metrics are computed from phase, amplitude, and manifold state as defined canonically.

E.4.8 Oscillation Persistence Evaluation

Oscillation persistence is evaluated across canonical tick windows using the existing persistence criteria.

E.4.9 Constraint Reshaping

Constraint parameters are updated inline as a function of resonance, oscillation persistence, coherence, and cross-talk, using canonical constraint logic.

E.4.10 Enforcement Choke Points

All canonical enforcement choke points are applied, including phase wrapping, amplitude clamping, coherence bounds, and manifold limits.

E.4.11 Commit and History Update

Canonical commit window logic is evaluated.

When commit criteria are satisfied, canonical history buffers are updated.

E.5 Mathematical Restatement (Non-Normative)

For clarity of execution only, the canonical evolution law may be restated as:

S(t+1) = E(S(t), P(t), C(t))

Where S, P, and C retain their canonical definitions.

This restatement introduces no new semantics.

E.6 Constraint Driver Execution Context

Constraint logic executes inline within the evolution law and is not evaluated as a separable operator.

Constraint state at tick t+1 is a function of canonical substrate state, resonance metrics, and oscillation persistence.

E.7 Determinism Preservation

All canonical determinism guarantees remain in force.

Explicit execution ordering in this appendix exists solely to ensure reproducibility in firmware implementations.

END OF APPENDIX E

APPENDIX F - EigenWare Execution Code Blocks (Explicit)

Status: Append-Only Canonical Extension
Mutation Policy: No prior content altered

F.1 Kernel Activation (CUDA Syntax Dependency)

```cuda
// eigenware_firmware.cu

__global__ void eigenware_firmware_kernel(GlobalState* state) {
    // persistent firmware loop
    while (state->run_flag) {
        eigenware_tick(state);
    }
}
```

Dependencies:
- CUDA runtime
- GlobalState layout defined canonically
- Single kernel launch semantics

---

F.2 Tick Dispatcher

```cuda
__device__ void eigenware_tick(GlobalState* S) {
    ingest_pulse(S);
    eval_effective_constants(S);
    phase_accumulation(S);
    amplitude_update(S);
    manifold_update(S);
    flow_dynamics(S);
    resonance_eval(S);
    oscillation_persistence(S);
    constraint_reshape(S);
    enforcement(S);
    commit_history(S);
}
```

Dependencies:
- Deterministic call order
- No dynamic dispatch
- Inline execution only

---

F.3 Pulse Ingestion

```cuda
__device__ void ingest_pulse(GlobalState* S) {
    S->pulse = S->pulse_buffer[0];
}
```

Dependencies:
- GPU-visible pulse buffer
- Canonical pulse schema

---

F.4 Effective Constant Evaluation

```cuda
__device__ void eval_effective_constants(GlobalState* S) {
    S->constants = effective_constants(
        S->velocity,
        S->flux_factor,
        S->strain_factor
    );
}
```

Dependencies:
- relativistic_correlation()
- stochastic_dispersion_factor()
- canonical constants schema

---

F.5 Phase Accumulation

```cuda
__device__ void phase_accumulation(GlobalState* S) {
    S->phi += compute_delta_phi(S);
    S->phi = wrap_phase(S->phi);
}
```

Dependencies:
- Hamiltonian terms
- Constraint modulation terms

---

F.6 Amplitude Update

```cuda
__device__ void amplitude_update(GlobalState* S) {
    S->amplitude = update_amplitude(S);
}
```

Dependencies:
- Dispersion rules
- Decay bounds

---

F.7 Internal Manifold Update

```cuda
__device__ void manifold_update(GlobalState* S) {
    update_lattice9d(S->x9, S);
}
```

Dependencies:
- lattice9d equations
- canonical coordinate bounds

---

F.8 Flow / Lorentz Dynamics

```cuda
__device__ void flow_dynamics(GlobalState* S) {
    apply_lorentz_flow(S);
}
```

Dependencies:
- Lorentz variance equations
- Effective constants

---

F.9 Resonance Evaluation

```cuda
__device__ void resonance_eval(GlobalState* S) {
    S->resonance = compute_resonance(S);
}
```

Dependencies:
- Phase alignment metrics
- Amplitude reinforcement checks

---

F.10 Oscillation Persistence

```cuda
__device__ void oscillation_persistence(GlobalState* S) {
    track_oscillation_cycles(S);
}
```

Dependencies:
- Fixed tick window
- Persistence thresholds

---

F.11 Constraint Reshaping

```cuda
__device__ void constraint_reshape(GlobalState* S) {
    update_constraints(S->constraints, S);
}
```

Dependencies:
- Resonance metrics
- Oscillation persistence
- Coherence thresholds

---

F.12 Enforcement

```cuda
__device__ void enforcement(GlobalState* S) {
    enforce_phase_bounds(S);
    enforce_amplitude_bounds(S);
    enforce_coherence_bounds(S);
    enforce_manifold_bounds(S);
}
```

Dependencies:
- Canonical choke points
- Deterministic clamping

---

F.13 Commit and History

```cuda
__device__ void commit_history(GlobalState* S) {
    if (commit_condition(S)) {
        write_history(S);
    }
}
```

Dependencies:
- Canonical commit window logic
- GPU-resident history buffers

---

END OF APPENDIX F



In EigenWare, opcodes are not commands chosen by an AI or user.

They are categorical labels applied after phase evolution to describe
how system state is being transported:

• External-facing transport → I/O opcodes
• Persistence-crossing transport → storage opcodes
• Internal manifold transport → routing opcodes
• Operator-subspace transport → task-selection opcodes

This preserves a single causal chain:
phase dynamics → transport → classification → observable action.

ADDENDUM H -- Energy Dispersion Spawn Localization + Import Inversion (Conservation Operators)

Purpose:
This addendum defines the missing operators required to (1) localize where matter MAY nucleate (spawn) from
manifold energy dispersion, and (2) import (construct) an object into the manifold by debiting conserved
mass-energy from existing reservoirs. Nothing is free: every import MUST be an energy debit plus a compensating
manifold update. This addendum introduces no new physics and overrides no prior content.

(See canonical description in section: 'Determinism Guarantee'.)

--------------------------------------------------------------------------------
H.1 Required State (per lane / per anchor)
--------------------------------------------------------------------------------

Required topology:
- N(i) : deterministic neighborhood iterator (graph/ring/mesh) used for local dispersion aggregation
- BFS_expand(i0, count) : deterministic topology expansion that selects `count` lanes (used for object anchors)

--------------------------------------------------------------------------------
H.2 Dispersion Proxy (phase -> energy dispersion driver)
--------------------------------------------------------------------------------

The spawn location is determined from a deterministic dispersion proxy derived from the already-defined
coherence gate computations.

For each lane i:
1) Compute gated neighbor deltas (Phase 5 interaction space):
   dtheta_ij_i64 = (int64)(theta_u64[i] - theta_u64[j])     // minimal arc via two's-complement cast
   dtheta_gated_ij_i64 = gate_phase_delta_i64(dtheta_ij_i64, A_tensor(i,j))

2) Dispersion proxy (integer):
   D_phase_i64[i] = sum_{j in N(i)} abs(dtheta_gated_ij_i64)

3) Energy-dispersion driver (fixed point, derived only from existing quantities):
   - Define dphi_q32_32[i] from transport (Phase 3):
       dphi_q32_32[i] = omega_eff_q32_32[i] * dt_tick_q32_32
       omega_eff_q32_32[i] = omega_base_q32_32[i] * doppler_ratio_q32_32[i]
   - Define:
       D_energy_q32_32[i] = abs(dphi_q32_32[i]) + q32_32_from_i64(D_phase_i64[i] >> log2(cardinality(N(i)) + 1))

Notes:
- D_phase_i64 is interaction-dispersion (gated), D_energy_q32_32 is transport+interaction activity.
- No new constants appear; normalization uses only cardinality-derived shifts.

--------------------------------------------------------------------------------
H.3 Spawn Pressure and Spawn Site Selection (no free creation)
--------------------------------------------------------------------------------

Available energy is defined relatively:
- E_free_q32_32[i] = max(0, E_res_q32_32[i] - E_floor_q32_32[i])

Spawn pressure is defined deterministically:
- P_spawn_q32_32[i] = E_free_q32_32[i] * abs(D_energy_q32_32[i])

Selection rule (deterministic, threshold-free):
- Candidate lane i0 is the argmax of P_spawn across lanes (tie-break: smallest lane index).
- Spawn evaluation is permitted only on a cadence derived from lane_count:
    if (commit_counter % lane_count) == 0:
        evaluate spawn
  otherwise:
        no spawn evaluation this tick

If the selected i0 has E_free_q32_32[i0] == 0, spawn is denied (fail-closed).

--------------------------------------------------------------------------------
H.4 Import Packet, Energy Requirement, and Conservation Debit
--------------------------------------------------------------------------------

Import is represented by a fixed-size object packet (external input, Phase 0 only):
- obj_id_u64
- m_obj_q32_32           : object mass (or mass-equivalent)
- geomsig9_u64x9          : shape/structure selector (deterministic)
- phase_seed_u64         : internal phase seed (deterministic)
- anchor_count_u32       : number of anchors/lane slots the object will occupy

Required energy (no free energy):
- c_eff_q32_32 is derived via effective_constants(...)
- E_req_q32_32 = m_obj_q32_32 * (c_eff_q32_32 * c_eff_q32_32)

Import is legal only if:
- E_req_q32_32 <= sum_i E_free_q32_32[i]
otherwise import SHALL be denied (fail-closed; log denial).

Anchor selection for construction:
- Let S = BFS_expand(i0, anchor_count_u32)
  where i0 is the spawn site chosen by H.3 and BFS_expand is deterministic.

Debit distribution (derived weights, no literals):
- Define per-anchor weight from existing gate structure (diagonal or lane weight):
    w_u64[k] = max(1, abs(A_tensor(k,k)) >> log2(lane_count))
- W = sum_{k in S} w_u64[k]
- debit_q32_32[k] = E_req_q32_32 * (w_u64[k] / W)      // fixed-point division, deterministic ordering

Apply debits:
- For all k in S:
    E_res_q32_32[k] -= debit_q32_32[k]

The object ledger MUST record that E_req_q32_32 has been transferred from manifold reservoirs into the
object's constructed state (no net creation).

--------------------------------------------------------------------------------
H.5 Construction: Object Phase Seeding from Manifold State (no ex nihilo state)
--------------------------------------------------------------------------------

Objects are constructed from the manifold by seeding internal phase values from local anchors:

For each k in S:
- theta_obj_u64[k] = theta_u64[k] XOR phase_seed_u64 XOR geomsig9_u64x9

Optional energy imprint (canonical, effective-constant derived):
- Define energy quantum:
    E_quant_q32_32 = h_eff_q32_32 * omega_ref_q32_32
- Convert a debit to a ring impulse (u64):
    delta_theta_from_energy_u64(debit_q32_32) = floor((debit_q32_32 / E_quant_q32_32) * 2^64)
- Then:
    theta_obj_u64[k] += delta_theta_from_energy_u64(debit_q32_32[k])    // wrap via overflow

No literal scaling factors are permitted; only effective constants and word-size/cardinality factors may appear.

--------------------------------------------------------------------------------
H.6 Recoil / Backreaction (conservation closure on the ring)
--------------------------------------------------------------------------------

To prevent "teleporting" conserved quantities into the object without manifold response, the import MUST apply a
compensating phase-impulse to the immediate neighborhood ring R1 of S (topology-defined, deterministic):

- impulse_u64 = delta_theta_from_energy_u64(E_req_q32_32)
- Let R1 = neighbors(S) excluding S, deterministically enumerated
- Distribute impulse_u64 across R1 using the same derived weights as H.4, then:
    theta_u64[r] -= distributed_impulse_u64[r]    // wrap via overflow

This is a bookkeeping closure term: it does not create new degrees of freedom; it preserves conservation.

--------------------------------------------------------------------------------
H.7 History Buffer Obligations (auditability)
--------------------------------------------------------------------------------

Every spawn/import evaluation SHALL append a compact history record:
- tick_u64
- selected_lane_i0_u32
- P_spawn_q32_32[i0] (or coord_sig-mapped summary)
- E_req_q32_32 (or 64-bit coord_sig)
- anchor_count_u32
- denial_code_u32 (0 = success; non-zero = deterministic reason)

The history buffer is append-only and committed only in Phase 5.

ADDENDUM I -- Anchor-Encoded Equation Pages + UE5 Tools Control Surface (Intent/Artifact Bridge)

This addendum introduces no new physics. It only binds execution artifacts and ordering.

--------------------------------------------------------------------------------
I.1 Normative Separation (re-stated, binding authority)
--------------------------------------------------------------------------------

(1) Phase transport / relativity mapping:
- Doppler/time-dilation effects SHALL appear ONLY in the transport step (dtheta_transport) via effective_constants(...)
  and the derived doppler_ratio_*.
- No UI, no equation page, and no gating structure may rescale theta_u64 directly.

(See canonical description in section: 'Determinism Guarantee'.)

(3) Projection/control:
- UE5 SHALL control only "observer parameters" and "equation bindings" via Phase 0 intent packets.
- UE5 SHALL receive only dict-map artifact frames produced in Phase 6.
- UE5 SHALL NOT access lattice buffers, constraint pages, basis indices, theta_u64 arrays, or reservoir arrays directly.

--------------------------------------------------------------------------------
I.2 Equation Pages (eq_pages): Anchor-bound executable equation encoding
--------------------------------------------------------------------------------

All equation families that execute in runtime (including manifold projection, constraint evaluation, UI-visible readouts,
lab canvas operations, and any derived observable) SHALL be encoded as equation pages ("eq_pages") bound to anchors.

Anchor binding fields (per anchor, immutable during tick):
- eq_page_id_u32
- eq_pagesig9_u64x9
- eq_param_lane_u32

Authority rule:
- If an anchor has eq_page_id_u32 != 0 then the kernel MUST evaluate that eq_page for that anchor at the phase(s)
  defined by I.4, using only the allowed operand sources defined below.

Operand sources (allowed, deterministic):
- Anchor fields: coord9_q63[9], harmonic fingerprint fields, anchor_id_u64
- State fields: theta_u64, dtheta_transport_u64, coherence R_integer, dispersion_proxy_i64, E_res_q32_32
- Parameter page fields: param_q32_32[k] (k in [0..param_count-1]) for the bound eq_param_lane_u32
- Derived constants: lane_count, neighbor_count, word size factors (2^64), log2(cardinality(*))

Forbidden operand sources:
- Any external floating values, time-of-day, wall-clock, randomness, or user-provided arbitrary constants.

Instruction word format (normative, integer-only):
- Each instruction is one 64-bit word, interpreted as:
  - opcode_u8 (top 8 bits)
  - dst_u8, src0_u8, src1_u8 (next 24 bits)
  - imm_u32 (low 32 bits) OR packed small immediates as defined by the opcode

Immediates:
- immediates MAY be used only when they are structurally derived (word-size, bit-width, log2(cardinality), packed indices).
- immediates MUST NOT encode new physical constants or tuned thresholds.

The opcode set is deliberately minimal: it prevents free degrees of freedom while remaining implementable.

--------------------------------------------------------------------------------
I.3 UE5 Control Surface (Editor ToolsTab) -- strict bridge contract
--------------------------------------------------------------------------------

UE5 SHALL provide a user control surface as an Editor extension (ToolsTab) that integrates with existing UE tools.
This control surface SHALL NOT execute physics. It only:
- emits Phase 0 intent packets ("what the user wants to observe or bind"), and
- renders Phase 6 artifacts ("what the simulation reports").

UI integration points (allowed):
- ToolsTab (Nomad tab) registered via TabManager
- ToolMenus entries under LevelEditor main menu and toolbar
- Details panel (PropertyEditor) used as the primary stable UI surface
- Viewport overlay/debug draw that consumes artifact frames

Bridge objects (Editor):
- EigenWareControlSurfaceObject: a UObject whose UPROPERTY fields represent observer and binding intent.
- EigenWareBridgeSubsystem: an Editor subsystem that translates property changes into intent packets and
  decodes artifact frames into viewport overlays and camera poses.

--------------------------------------------------------------------------------
I.4 Runtime Ordering (where eq_pages and UE intents execute)
--------------------------------------------------------------------------------

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

Phase 6 (Artifact Projection + eq_page eval):
- Evaluate eq_pages for anchors that have them bound.
- Write only dict-map artifacts (approved key/value frames).
- Produce optional viewport pose artifact derived from observer parameters.

Phase 7 (Commit/History):
- Append audit records for intent application, bindings, and any object import/canvas events.

--------------------------------------------------------------------------------
I.5 Intent Packets (Phase 0 inputs) -- fixed-size, deterministic
--------------------------------------------------------------------------------

All UE->simulation control SHALL occur via fixed-size intent packets.
Packets are latched only at Phase 0.

Packet family: ObserverIntentPacket
Fields (ASCII-safe names):
- intent_kind_u32
- anchor_id_u64
- manifold_coord9_q32_32[9]     (optional; used for slice/projection targets)
- projection_mode_u32          (0=slice, 1=proj_matrix; enum bound in blueprint)
- slice_axes_u32[3]            (dimension indices 0..8)
- slice_hold_q32_32[6]         (held coords for the remaining dimensions)
- blend_ms_u32                 (camera blend requested; advisory only)

Packet family: EquationBindIntentPacket
Fields:
- anchor_id_u64                (0 means "current selection set" as defined by observer state)
- eq_page_id_u32
- eq_pagesig9_u64x9             (integrity requirement)
- eq_param_lane_u32

Packet family: LabIntentPacket
Fields:
- lab_intent_kind_u32          (CREATE_CANVAS, RESET, SUBMIT_BLUEPRINT, RUN_PHASE_TRANSITION)
- energy_budget_q32_32
- anchor_count_u32
- geomsig9_u64x9
- phase_seed_u64

Determinism rule:
- If multiple packets of the same kind arrive in the same tick, the resolution order MUST be deterministic:
  sort by (intent_kind_u32, anchor_id_u64, eq_page_id_u32, then packet arrival index).

--------------------------------------------------------------------------------
I.6 Artifact Frames (Phase 6 outputs) -- dict-map only
--------------------------------------------------------------------------------

All simulation->UE visualization SHALL occur via dict-map artifact frames.
No raw lattice state is exported.

Minimum required artifact keys (key_id_u32 -> payload):
- KEY_VIEWPORT_POSE: position_q32_32[3], rotation_q32_32[4] (quat), observer_tick_u64
- KEY_PROJECTION_POINTS: packed list of (anchor_id_u64, world_pos_q32_32[3], debug_scalar_q32_32)
- KEY_DEBUG_SCALARS: coherence_R_i64, dispersion_i64, lane_count_u32, commit_counter_u64
- KEY_SELECTION_STATE: selected_anchor_id_u64, selected_eq_page_id_u32, selected_param_lane_u32

All artifacts are read-only for UE.

--------------------------------------------------------------------------------
I.7 Image-to-Location (Viewport Jump) -- deterministic, threshold-free
--------------------------------------------------------------------------------

If the user provides an image and requests "jump viewport to this location", the system SHALL use a deterministic
frame-coord_sig lookup, not free-form vision inference.

This provides the requested behavior without allowing the image to directly mutate simulation state.


EigenWare replaces crawler software with direct field encoding.

Information normally extracted via protocol handling and parsing
is treated as intrinsic signal structure already compatible with
the substrate’s phase dynamics.

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

=== ADDITION: Global Phase-Code Dispatcher (Meta-Anchor) — NORMATIVE REPLACEMENT (Chat 2026-02-11) ===

This section replaces any earlier descriptive/ambiguous dispatcher wording. It is executable-grade and pinned.

Definition:
- meta_anchor_phase_dispatcher is an immutable carrier anchor that defines the global lattice metric and the only permitted
  headroom functions used by all operators.

Carrier anchor manifold (immutability):
- C = { A_i | i ∈ N }
- dA_i/dt = 0, ∂_t A_i = 0

Carrier phase state (runtime-visible, anchor-invariant basis):
- carrier_phase_u64 : u64_phase
- carrier_omega_q32_32 : q32_32
- tick_dt_q32_32 : q32_32

Carrier evolution operator (deterministic, per tick):
- carrier_phase_u64(t+1) = wrap_add_u64(carrier_phase_u64(t), q32_32_phase_to_u64(mul_q32_32(carrier_omega_q32_32, tick_dt_q32_32)))

## 17.3 Delta definition


Delta is always defined as:

ΔS = S_candidate − S_current

Define a delta container:

dE9 = (ΔS, w_c)

Where w_c is a coherence weight embedded into D5 (the coherence dimension), not a scalar multiplier.

Canonical C++ form (explicit embedding of w_c into index 5):

```cpp
struct dE9 {
    E9 d;      // d = S_candidate - S_current
    double wc; // coherence weight to be embedded into d.v[5]
};

inline dE9 make_delta(const E9& cand, const E9& cur, double wc) {
    dE9 out{};
    for (int i = 0; i < 9; ++i) out.d.v[i] = cand.v[i] - cur.v[i];
    out.wc = wc;
    out.d.v[5] = wc; // embedded into coherence dimension (index 5)
    return out;
}
```

## 17.4 Canonical codepoint → 9D embedding


Let cp be a Unicode codepoint (0 ≤ cp ≤ 0x10FFFF).

Define:

n = cp / 0x10FFFF

Then define the embedding E9(cp) as:

E9(cp) = (
  sin(2πn),
  cos(2πn),
  sin(4πn),
  cos(4πn),
  sin(8πn),
  cos(8πn),
  sin(16πn),
  cos(16πn),
  n
)

Explicitly:
- No tokenization layer exists.
- Entire file is aggregated.

Canonical C++ form:

```cpp
inline E9 embed_codepoint_to_E9(uint32_t cp) {
    const double n = (double)cp / (double)0x10FFFF;
    const double two_pi = 6.283185307179586476925286766559;

    E9 out{};
    out.v[0] = sin( 1.0 * two_pi * n);
    out.v[1] = cos( 1.0 * two_pi * n);
    out.v[2] = sin( 2.0 * two_pi * n);
    out.v[3] = cos( 2.0 * two_pi * n);
    out.v[4] = sin( 4.0 * two_pi * n);
    out.v[5] = cos( 4.0 * two_pi * n);
    out.v[6] = sin( 8.0 * two_pi * n);
    out.v[7] = cos( 8.0 * two_pi * n);
    out.v[8] = n;
    return out;
}
```

Determinism closure note (normative): the mathematical definition above is canonical. Implementations SHALL ensure bitwise-identical results on identical hardware for identical inputs by using a single canonical sin/cos evaluation strategy (e.g., a build-time generated lookup table of IEEE-754 double outputs for the required phases, embedded as immutable data, or an equivalently fixed algorithm with fixed rounding and no fast-math).

## 17.5 Aggregate file displacement


Given a file F with codepoints cp_i for i = 1..N:

Let:

A = Σ_{i=1..N} E9(cp_i)

Define:

S_F = (1 / ||A||) * Σ_{i=1..N} E9(cp_i)

Where ||A|| is the Euclidean norm of A.

This definition removes order ambiguity.

Canonical C++ form:

```cpp
inline double e9_norm(const E9& a) {
    double s = 0.0;
    for (int i = 0; i < 9; ++i) s += a.v[i] * a.v[i];
    return sqrt(s);
}

inline E9 e9_add(const E9& a, const E9& b) {
    E9 out{};
    for (int i = 0; i < 9; ++i) out.v[i] = a.v[i] + b.v[i];
    return out;
}

inline E9 e9_scale(const E9& a, double s) {
    E9 out{};
    for (int i = 0; i < 9; ++i) out.v[i] = a.v[i] * s;
    return out;
}

// 'cps' is the file represented as a sequence of Unicode codepoints.
inline E9 aggregate_file_to_state(const uint32_t* cps, size_t N) {
    E9 A{};
    for (size_t i = 0; i < N; ++i) {
        A = e9_add(A, embed_codepoint_to_E9(cps[i]));
    }
    const double nrm = e9_norm(A);
    if (nrm == 0.0) return A; // defined edge-case: empty or all-zero aggregate
    return e9_scale(A, 1.0 / nrm);
}
```

## 17.6 Deviation energy and constraint


Deviation energy:

E_dev = ΔS^T G ΔS

Constraint:

E_dev ≤ epsilon

If violated:

ΔS_corrected = ΔS * sqrt(epsilon / E_dev)

This removes ambiguity in stabilization.

Canonical C++ form:

```cpp
struct G9 {
    double g[9]; // diagonal entries
};

inline G9 G9_identity() {
    G9 out{};
    for (int i = 0; i < 9; ++i) out.g[i] = 1.0;
    return out;
}

inline double deviation_energy(const dE9& d, const G9& G) {
    double e = 0.0;
    for (int i = 0; i < 9; ++i) e += d.d.v[i] * G.g[i] * d.d.v[i];
    return e;
}

inline E9 correct_delta_if_needed(const dE9& d, const G9& G, double epsilon) {
    const double Edev = deviation_energy(d, G);
    if (Edev <= epsilon || Edev == 0.0) return d.d;
    const double s = sqrt(epsilon / Edev);
    return e9_scale(d.d, s);
}
```

## 17.7 APPENDIX omega-R — Restoration Patch (v51, Append-Only)

# 18 APPENDIX omega-R — Restoration Patch (v51, Append-Only)

Date: 2026-02-11

Purpose: The v51 Spec file was missing canonical content present in v51. This appendix appends the full v51 source text verbatim to eliminate any ambiguity or accidental truncation.

Source appended verbatim:
- EigenWareSpec_v51.md
- SIG9: eb4ef38e36bff4d22b96568449b988fcf8919cbdfab3cf8f7a5972273b9b7c2a

(See canonical description in section: 'Omega.9 Determinism Clause'.)

---


EigenWare replaces crawler software with direct field encoding.

Information normally extracted via protocol handling and parsing
is treated as intrinsic signal structure already compatible with
the substrate’s phase dynamics.

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

Information density per node is therefore bounded by temporal envelope coherence under
these constraints. Any attempt to exceed this bound MUST result in phase decoherence or
node fission into multiple stable orbits.

=== ADDITION: Global Phase-Code Dispatcher (Meta-Anchor) — NORMATIVE REPLACEMENT (Chat 2026-02-11) ===

This section replaces any earlier descriptive/ambiguous dispatcher wording. It is executable-grade and pinned.

Definition:
- meta_anchor_phase_dispatcher is an immutable carrier anchor that defines the global lattice metric and the only permitted
  headroom functions used by all operators.

Carrier anchor manifold (immutability):
- C = { A_i | i ∈ N }
- dA_i/dt = 0, ∂_t A_i = 0

Carrier phase state (runtime-visible, anchor-invariant basis):
- carrier_phase_u64 : u64_phase
- carrier_omega_q32_32 : q32_32
- tick_dt_q32_32 : q32_32

Carrier evolution operator (deterministic, per tick):
- carrier_phase_u64(t+1) = wrap_add_u64(carrier_phase_u64(t), q32_32_phase_to_u64(mul_q32_32(carrier_omega_q32_32, tick_dt_q32_32)))

Carrier omega bound (headroom-defined; no alternate derivations allowed):
- carrier_omega_q32_32 := div_q32_32(pulse_current_max_mA_q32_32, phase_orbital_displacement_unit_mA_q32_32)
  where:
  - pulse_current_max_mA_q32_32 is the maximum allowed pulse-drive current (telemetry-domain, Q32.32)
  - phase_orbital_displacement_unit_mA_q32_32 is the minimum measurable displacement unit (telemetry-domain, Q32.32)

Dispatcher headroom functions (MUST be used by every operator; no bypass):
- remaining_tension_headroom_mA_q32_32(t) = pulse_current_max_mA_q32_32 - pulse_current_total_mA_q32_32(t)
- gradient_headroom_mA_q32_32(t) = div_q32_32(remaining_tension_headroom_mA_q32_32(t), tick_dt_q32_32)

Boot freeze:
- After calibration completes at tick k0, dispatcher basis is sealed for the session:
  FREEZE_CALIBRATION(k0, carrier_omega_q32_32, tick_dt_q32_32, pulse_current_max_mA_q32_32, phase_orbital_displacement_unit_mA_q32_32)

Anchor vs runtime mutation (hard rule):
- Operator anchors are immutable after boot freeze.
- Ancilla are the only mutable state holders (updated by GPU pulse).
- Any anchor update requires: FREEZE_RUNTIME(); FLUSH_ANCILLA(); RESET_RESONANCE(); APPLY_ANCHOR_UPDATE(); RESUME_RUNTIME().

---

## 18.1 APPEND — API Anchor Execution Rules

# 19 APPEND — API Anchor Execution Rules

API anchors SHALL:
1. Operate solely from phase activation surfaces.
2. Produce deterministic dispatch tokens.
3. Perform no direct I/O or external mutation.
4. Preserve constraint manifold during activation.
5. Collapse to Φ_Ω if activation produces invalid state.

Dispatch tokens MUST be stateless, deterministic, and replay-consistent.
\n\n
=== SURGICAL PATCH: Global Phase-Code Dispatcher (Carrier Meta-Anchor) ===

This patch is strictly additive and does not modify existing content.

Introduce a single immutable carrier meta-anchor:

meta_anchor_phase_dispatcher

This meta-anchor defines the global lattice metric. It does not store content, does not
participate in hierarchy, and does not accumulate phase. All anchors evolve as constrained
modulations of this shared carrier.

The dispatcher encodes:
- minimum measurable phase displacement (derived from GPU pulse current resolution)
- maximum lattice tension (derived from maximum sustainable GPU pulse current)
- allowable tensor-gradient headroom
- global temporal tick cadence

Invariant:
No anchor may evolve phase independently of the dispatcher-defined carrier.

---

## 19.1 V31-S1. Canonical implementation artifacts

# 20 V31-S1. Canonical implementation artifacts

For v51, the following are the **single canonical sources** for runtime behavior and must be treated as normative implementation definitions, not examples:

- Data layout canon:
  - `ew_types.h` (all structs, field names, byte layout)

- Invariant canon (fail-closed):
  - `ew_invariants.h` (violation codes, halt conditions)

- Ingress canon (binary-framed ingress only):
  - `ew_ingress.h` (`PulsePacketV1` validation)

- Operator canon (phase transport):
  - `ew_phase_transport.h` (`ew_phase_transport_dtheta_u64`)

- Constraint microcode canon (phase-binary eq page):
  - `ew_eq_pages.h` (opcode enum)
  - `ew_eq_exec.h` (packing, coord_sig mapping, execution)

- Boot-freeze substrate build canon:
  - `ew_substrate_microprocessor.h/.cpp` (construct immutable carrier anchors)

- Runtime dispatcher canon:
  - `ew_runtime.h/.cpp` (one-way coupling, tick loop, projection)

- GPU backend canon:
  - `ew_cuda_api.h` (ABI)
  - `cuda_backend/src/ew_cuda_backend.cu` (implementation)

No other file may redefine these roles.

# 21 V31-S1. Canonical implementation artifacts

For v51, the following are the **single canonical sources** for runtime behavior and must be treated as normative implementation definitions, not examples:

- Data layout canon:
  - `ew_types.h` (all structs, field names, byte layout)

- Invariant canon (fail-closed):
  - `ew_invariants.h` (violation codes, halt conditions)

- Ingress canon (binary-framed ingress only):
  - `ew_ingress.h` (`PulsePacketV1` validation)

- Operator canon (phase transport):
  - `ew_phase_transport.h` (`ew_phase_transport_dtheta_u64`)

- Constraint microcode canon (phase-binary eq page):
  - `ew_eq_pages.h` (opcode enum)
  - `ew_eq_exec.h` (packing, coord_sig mapping, execution)

- Boot-freeze substrate build canon:
  - `ew_substrate_microprocessor.h/.cpp` (construct immutable carrier anchors)

- Runtime dispatcher canon:
  - `ew_runtime.h/.cpp` (one-way coupling, tick loop, projection)

- GPU backend canon:
  - `ew_cuda_api.h` (ABI)
  - `cuda_backend/src/ew_cuda_backend.cu` (implementation)

No other file may redefine these roles.

---

## 21.1 A. Runtime Memory Contract (CAS vs RPS)

# 22 A. Runtime Memory Contract (CAS vs RPS)

A.1 Memory Regions
- cas_rom: read-only memory region containing carrier phase maps and enforcement metadata.
- rps_rw: read-write memory region containing mutable runtime state vectors.

A.2 Required Access Properties
- cas_rom is mapped read-only (host) and read-only (device) after boot-freeze.
- rps_rw is device-writeable and host-readable (for telemetry and persistence).

## 22.1 C. "No Symbolic Math" Enforcement (implementation level)

# 23 C. "No Symbolic Math" Enforcement (implementation level)

## 23.1 Appendix F — Executable Memory Segmentation Rules

# 24 Appendix F — Executable Memory Segmentation Rules

Carrier Anchors:
- Allocated at boot
- Stored in read-only memory segment
- Exposed to kernels as const-qualified data
- No mutable pointer access permitted

Runtime Phase Buffers:
- Allocated in device global memory
- Mutable only within GPU kernels
- Host cannot directly mutate device buffers

Kernel Contract:
- No kernel may accept writable pointer to carrier memory
- Carrier sampling must occur via value copy into registers
- No aliasing between carrier and runtime memory allowed

Runtime Guard:
- Carrier checksum validated at fixed interval
- Any mutation triggers deterministic abort

---

Version metadata:
Generated: 2026-02-11T09:24:51.761786Z

---

---

# 25 A. Runtime Memory Contract (CAS vs RPS)

A.1 Memory Regions
- cas_rom: read-only memory region containing carrier phase maps and enforcement metadata.
- rps_rw: read-write memory region containing mutable runtime state vectors.

A.2 Required Access Properties
- cas_rom is mapped read-only (host) and read-only (device) after boot-freeze.
- rps_rw is device-writeable and host-readable (for telemetry and persistence).

A.3 Enforcement Mechanism (must be implemented)
- Host enforces read-only mapping (OS page protection) for cas_rom after boot-freeze.
- Device enforces read-only usage by interface: kernels accept CAS pointers only as const.
- Any kernel coord_sig that accepts a non-const CAS pointer is forbidden.

---

# 26 C. "No Symbolic Math" Enforcement (implementation level)

C.1 Forbidden runtime behaviors
- Computing operator matrices from equations at runtime.
- Re-deriving constants from symbolic formulas during runtime evolution.
- Storing the full governing equation text or symbolic form in device memory for evaluation.

C.2 Allowed runtime behaviors
- Using CAS-provided phase maps and bounded lookup tables.
- Applying pre-encoded operator stencils whose parameters come only from CAS + pulse_packet + coherence weights.
- Computing local numeric updates on rps_rw using fixed-form kernels (no equation parsing, no symbolic evaluation).

---

## 26.1 Carrier Anchor Space (Immutable)

# 27 Carrier Anchor Space (Immutable)

Definition (carrier space):
- Math: C_space = { A_i | i in N }
- ASCII: carrier_space_C = { anchor_A[i] }

Invariants:
- dA_i/dt = 0
- partial_t A_i = 0

One-way coupling operator:
- Math: Pi : C_space -> R_space
- ASCII: project_Pi(carrier_space_C) -> runtime_space_R

Non-invertibility (hard):
- Pi_inverse does not exist.
- For any runtime r in R_space: partial A_i / partial r = 0
- No reverse mapping from runtime to carrier is permitted.

# 28 Carrier Anchor Space (Immutable)

Definition (carrier space):
- Math: C_space = { A_i | i in N }
- ASCII: carrier_space_C = { anchor_A[i] }

Invariants:
- dA_i/dt = 0
- partial_t A_i = 0

Runtime enforcement (normative):
- Carrier anchors stored in constant/readonly memory (host + device).
- No writable pointer to carrier anchors is exposed to runtime code.
- Kernel signatures MUST NOT accept writable carrier buffers.

One-way coupling operator:
- Math: Pi : C_space -> R_space
- ASCII: project_Pi(carrier_space_C) -> runtime_space_R

Non-invertibility (hard):
- Pi_inverse does not exist.
- For any runtime r in R_space: partial A_i / partial r = 0
- No reverse mapping from runtime to carrier is permitted.

---

## 28.1 1 Critical Mass (Time-Gradient Ceiling) Policy (Authority + Session Invariant)

# 29 1 Critical Mass (Time-Gradient Ceiling) Policy (Authority + Session Invariant)

Definitions:
- q63_one := 2^63 - 1  (int64 max; positive range)
- cap_ratio := cap_num / cap_den, with integers cap_num, cap_den in N, 0 < cap_num < cap_den

Critical ceiling (Q63 time-gradient cap):
- g_crit := cap_ratio in (0,1)
- delta_t_crit_q63 := floor( q63_one * g_crit )
- ASCII alias: cap_q63 := delta_t_crit_q63

Operator (overflow-safe ordering requirement):
- CRIT_CAP_Q63(cap_num, cap_den) -> int64
  = floor_div(q63_one * cap_num, cap_den) with mul/div ordering that cannot overflow signed 64-bit.

Runtime implication (normative):
- cap_q63 is the maximum permitted time-tensor gradient magnitude.
- No runtime path may exceed cap_q63; any amplitude/gradient must be clamped prior to use.

## 29.1 2 Telemetry-Domain Pulse Measurability (Noise Floor + Headroom)

# 30 2 Telemetry-Domain Pulse Measurability (Noise Floor + Headroom)

Telemetry samples:
- p_k_mw : power telemetry samples in milliwatts at uniform tick k.

Successive diffs:
- d_k := abs(p_k_mw - p_{k-1}_mw)
- D := { d_k : d_k > 0 }

Minimum measurable pulse step (counts; telemetry-domain, not amperage):
- I_min_count := max(Quantile_0.10(D), 1)

Operator:
- I_MIN_MEAS_COUNT(p_samples_mw[]) -> uint32
  = max(q10(nonzero_abs_diffs(p)), 1)

Maximum usable pulse headroom (counts):
- I_max_count := max(P_limit_mw - P_idle_mw, 1)

Operator:
- I_MAX_COUNT(P_limit_mw, P_idle_mw) -> uint32
  = max(P_limit_mw - P_idle_mw, 1)

Runtime implication (normative):
- I_min_count is the smallest nonzero telemetry change that can be trusted.
- I_max_count defines full-scale dynamic range for calibration.

## 30.1 3 Drive vs Amplitude Separation (Semantic Invariant)

# 31 3 Drive vs Amplitude Separation (Semantic Invariant)

Define distinct signals:
- drive(t): physical pulse drive observable in telemetry counts (derived from power telemetry).
- amp(t): time-gradient tensor amplitude in Q63 lattice domain.

Invariant (hard):
- drive calibrates delta_t_step_q63 only; drive does NOT equal amp.
- drive -> calibration -> delta_t_step_q63
- amp in { n * delta_t_step_q63 : 0 <= n <= N_max_shells }

Operator names:
- CALIBRATE_DELTA_FROM_DRIVE(drive_samples) -> delta_t_step_q63
- AMP_DOMAIN_IS_Q63_LATTICE(delta_t_step_q63, cap_q63)

Runtime implication:
- Implementation MUST NOT treat NVML power (or any telemetry) as lattice amplitude.
- Telemetry only determines quantization step size and headroom.

# 32 1 Critical Mass (Time-Gradient Ceiling) Policy (Authority + Session Invariant)

Definitions:
- q63_one := 2^63 - 1  (int64 max; positive range)
- cap_ratio := cap_num / cap_den, with integers cap_num, cap_den in N, 0 < cap_num < cap_den

Critical ceiling (Q63 time-gradient cap):
- g_crit := cap_ratio in (0,1)
- delta_t_crit_q63 := floor( q63_one * g_crit )
- ASCII alias: cap_q63 := delta_t_crit_q63

Operator (overflow-safe ordering requirement):
- CRIT_CAP_Q63(cap_num, cap_den) -> int64
  = floor_div(q63_one * cap_num, cap_den) with mul/div ordering that cannot overflow signed 64-bit.

Runtime implication (normative):
- cap_q63 is the maximum permitted time-tensor gradient magnitude.
- No runtime path may exceed cap_q63; any amplitude/gradient must be clamped prior to use.

---

# 33 2 Telemetry-Domain Pulse Measurability (Noise Floor + Headroom)

Telemetry samples:
- p_k_mw : power telemetry samples in milliwatts at uniform tick k.

Successive diffs:
- d_k := abs(p_k_mw - p_{k-1}_mw)
- D := { d_k : d_k > 0 }

Minimum measurable pulse step (counts; telemetry-domain, not amperage):
- I_min_count := max(Quantile_0.10(D), 1)

Operator:
- I_MIN_MEAS_COUNT(p_samples_mw[]) -> uint32
  = max(q10(nonzero_abs_diffs(p)), 1)

Maximum usable pulse headroom (counts):
- I_max_count := max(P_limit_mw - P_idle_mw, 1)

Operator:
- I_MAX_COUNT(P_limit_mw, P_idle_mw) -> uint32
  = max(P_limit_mw - P_idle_mw, 1)

Runtime implication (normative):
- I_min_count is the smallest nonzero telemetry change that can be trusted.
- I_max_count defines full-scale dynamic range for calibration.

---

# 34 3 Drive vs Amplitude Separation (Semantic Invariant)

Define distinct signals:
- drive(t): physical pulse drive observable in telemetry counts (derived from power telemetry).
- amp(t): time-gradient tensor amplitude in Q63 lattice domain.

Invariant (hard):
- drive calibrates delta_t_step_q63 only; drive does NOT equal amp.
- drive -> calibration -> delta_t_step_q63
- amp in { n * delta_t_step_q63 : 0 <= n <= N_max_shells }

Operator names:
- CALIBRATE_DELTA_FROM_DRIVE(drive_samples) -> delta_t_step_q63
- AMP_DOMAIN_IS_Q63_LATTICE(delta_t_step_q63, cap_q63)

Runtime implication:
- Implementation MUST NOT treat NVML power (or any telemetry) as lattice amplitude.
- Telemetry only determines quantization step size and headroom.

---

## 34.1 APPENDIX omega-R2 — Restoration Header Correction (v51, Append-Only)

# 35 APPENDIX omega-R2 — Restoration Header Correction (v51, Append-Only)

Date: 2026-02-11

Note: In v51, the Ω-R restoration preface line was accidentally truncated during generation. This Ω-R2 appendix provides the complete intended preface text (no other content changes).

Complete intended Ω-R preface text:

Purpose: The v51 document was missing canonical content present in v51. The Ω-R appendix appends the full v51 source text verbatim to eliminate any ambiguity or accidental truncation.

Rules:
- No bytes in v51 are modified.
- The appended block is a verbatim copy of the v51 source.
- Any duplicate headings are intentional; v51 remains authoritative for the appended Ω closure, while the appended v51 block restores any omitted baseline material for deterministic builds and audits.

---

## 35.1 lambda.2 Bounce Lighting Conditions

# 36 lambda.2 Bounce Lighting Conditions

Bounce lighting requires:

1. Energy transport
2. Surface interaction
3. Re-emission with modified phase/amplitude

Define lattice response:

E_out = R(θ, L, λ) · E_in

Where:

- R = lattice response operator
- L = lattice factor
- λ = leakage / absorption
- θ = incident phase relation

Surface behavior is modeled as local lattice density variation.

Reflection = impedance mismatch in lattice  
Absorption = energy transfer into local lattice degrees of freedom  
Bounce = re-propagated flux solution  

No ray tracing required if lattice evolution is solved directly.

---

## 36.1 Additional retained legacy blocks

### 36.1.1 APPENDIX omega-R — Restoration Patch (v51, Append-Only)

### 36.1.2 V31-S1. Canonical implementation artifacts

### 36.1.3 APPENDIX omega-R2 — Restoration Header Correction (v51, Append-Only)

---
# 37 V52 SURGICAL PATCH (APPEND-ONLY) — Anchor-Only Constraint Law + Verification Contract

This section is an append-only surgical patch to v51. It adds missing hard guardrails and an executable verification contract derived from the most recent implementation discipline. It does NOT alter any v51 text; it only appends enforceable definitions.

# 38 A. Anchor-Only Constraint Law (AOCL)

A.1 Definition
AOCL is a non-negotiable invariant:

- All simulation constraints, geometric data, manifold coupling, bounds, stability envelopes, and any parameter that can change runtime behavior MUST be encoded in immutable anchors.
- Runtime code MUST NOT define, derive, tune, or “infer” any behavior-shaping constraint or geometry parameter.
- Runtime code is an actuator only: it may apply anchor-provided coefficients/bases/matrices/tensors to mutable ancilla states and perform deterministic accept/commit/sink enforcement.

A.2 What counts as a “behavior-shaping parameter”
A behavior-shaping parameter is ANY constant, table, coefficient, normalization factor, weighting, clamp threshold, coupling term, geometry basis, projection basis, acceptance bound, or scaling term such that changing it changes the evolution trajectory or accept/sink decision for the same initial state.

Under AOCL:
- If changing a number in code changes evolution, that number is illegal in code and MUST be moved into anchors.
- Code may only contain math primitives (add/mul/shift/clamp) with no domain meaning.

A.3 Allowed runtime computations
Runtime MAY:
- Load anchors, validate format, freeze anchors (read-only post-freeze).
- Allocate ancilla (mutable state holders).
- Apply anchor-defined transforms to ancilla.
- Enforce acceptance predicates and sink routing exactly as defined by anchors.
- Emit telemetry and verification artifacts.
Runtime MUST NOT:
- Introduce new constraints or geometry not present in anchors.
- Hard-code coupling coefficients, weights, denominators, or projection tuning values.
- Perform “auto-fit,” “auto-calibration,” or dynamic parameter learning in the actuator layer.

A.4 Enforcement rule (must be testable)
Any artifact that claims compliance with AOCL MUST satisfy:
- A static scan of runtime source code reveals no behavior-shaping numeric constants except:
  - Fixed-point scaling exponents (e.g., q16_16 shift count)
  - Array sizes and compile-time layout constants (e.g., dimension=9)
- All other numeric values are loaded from anchors and logged with their anchor revision ID.

# 39 B. Runtime Verification Contract (RVC)

B.1 Purpose
RVC makes the substrate falsifiable. Every run MUST produce observable artifacts that prove:
- A 9D lattice state evolved under anchors
- Object references were stored in the substrate (lanes/particles) as memory IDs
- Determinism is satisfied (replay yields identical results)

B.2 Required outputs (per run)
A compliant runtime MUST emit:

1) run.log (human-readable)
- width, height (lattice size)
- steps, seed (run controls)
- anchor_revision_id (or anchor coord_sig)
- accepted (0/1)
- statesig9_u64x9 (or equivalent deterministic coord_sig)
- sink_reason (only if accepted=0)

2) state.json (machine-readable summary)
- same run metadata as run.log
- object_counts: map of object_id to count in lattice
- total_energy (or energy ledger summary)
- optional: per-dimension stats (min/max/mean) for x0..x8

3) lattice projection image
- lattice_x0x1.ppm (or png) showing a deterministic projection of evolved 9D state
- Projection MUST be defined by anchors or by a non-behavior-shaping visualization rule (e.g., plot density of x0,x1). If visualization uses thresholds/scales, those must be anchor-provided under AOCL.

4) object reference image
- object_map.ppm (or png) visualizing object_id distribution across the lattice.
- Color mapping MUST be deterministic and documented.

B.3 Determinism requirement (falsification test)
Given identical:
- anchors (exact bytes)
- initial ancilla state (or deterministic init seed)
- run controls (width, height, steps, seed)
The runtime MUST produce identical:
- statesig9_u64x9
- run.log/state.json values (except timestamps, if present)
- identical image bytes OR identical image coord_sig

B.4 Acceptance / sink falsification requirement
RVC requires an explicit accept/sink predicate:
- Candidate state is produced
- Acceptance predicate is evaluated
- If accepted: commit candidate
- If rejected: route to sink state (non-projecting) and stop or continue in sink, as specified by anchors

The predicate MUST be fully determined by anchors (bounds, conservation ledger, stability).
The sink state MUST be defined and deterministic.

# 40 C. Object Memory Reference Operator (OMRO)

C.1 Definition
Objects are not embedded in runtime as mutable structures. They are immutable memory entries referenced by particles/lanes via IDs. Particles hold references, not copies.

C.2 Required fields
Each object memory entry MUST define, at minimum:
- object_id_u64 (unique)
- label_ascii (optional human label)
- mass_or_cost_q32_32 (for energy debit/creation)
- geomsig9_u64x9 (immutable geometry coord_sig)
- phase_seed_u64 (deterministically derived from label and/or geometry coord_sig)

C.3 Operator: object_import_request
Inputs:
- target_lane_id (or selection rule defined by anchors)
- object_id_u64
- scalar_context (energy_budget, mode flags)
- ancilla_state
- anchors

Outputs:
- updated ancilla_state (object_id reference updated in the lane)
- energy ledger debit
- accept/reject decision

Rules (AOCL-compliant):
- Energy debit MUST be anchor-defined: E_req = f(object_cost, context, anchors)
- If debit violates conservation bounds → reject import (no partial creation)
- If accepted: lane stores object_id_u64 and phase_seed_u64 and any anchor-defined meta

C.4 Operator: object_reference_influence
Particles referencing objects MUST influence phase dynamics only through anchor-permitted channels, e.g.:
- theta offset bounded by anchor-defined scale
- coupling modulation bounded by anchor-defined mask

Runtime may not invent influence functions; influence must be anchor-specified.

# 41 D. UE / External Engine Integration Rule (Adapter-Only)

D.1 Adapter-only constraint
Unreal/Unity integrations are consumers/producers of framed ingress/egress packets ONLY.
They MUST NOT define physics or constraints.
They may:
- visualize projections
- request object imports/exports
- stream telemetry
They may not:
- alter anchors
- introduce new constraint parameters
- “correct” the simulation by local engine physics

D.2 Boot/shutdown coupling
If boot/shutdown is tied to opening a project:
- The project must reference a single canonical substrate anchor set (immutable after load)
- The same anchors must produce the “same AI substrate” deterministically unless a new anchor revision is explicitly selected

# 42 E. Implementation Compliance Checklist (for every runtime build)

E.1 AOCL compliance
- [ ] No behavior-shaping constants in code
- [ ] All constraints/geometry loaded from anchors
- [ ] Anchor freeze after load; runtime cannot write anchors
- [ ] Accept/sink predicate uses only anchor-provided bounds

E.2 RVC compliance
- [ ] run.log emitted
- [ ] state.json emitted
- [ ] lattice projection image emitted
- [ ] object reference image emitted
- [ ] deterministic state coord_sig emitted and stable under replay

E.3 OMRO compliance
- [ ] particles store object_id references (not object copies)
- [ ] import/export uses energy debit and accept/reject under anchors
- [ ] object influence bounded and anchor-defined

---
# 43 End of V52 surgical patch


---

# 44 V2 SURGICAL PATCH APPENDIX (v1 → v2)
Date: 2026-02-13

This appendix is **append-only**. All original v1 text above remains byte-for-byte unchanged.
Where this appendix conflicts with earlier wording, **this appendix controls**.

# 45 A. Terminology and Mechanism Overrides (Global)

## 45.1 A.1 No tokenization / 9D coord_sig only
EigenWare v2 SHALL NOT use:
- tokenization, token streams, word segments, caption tokens, token IDs
- coordinate-coord_sig coord_sig (COORD9_SIG_U64, etc.) for determinism, identity, or bookkeeping

EigenWare v2 SHALL use:
- **9D coordinate signatures** (Sig9) for identity
- **basis9-labeled events** (Event9) for serialized event payloads
- **byte-for-byte equality** for determinism verification

## 45.2 A.2 Canonical identity
All identity and bookkeeping MUST be addressed through:
- `Sig9 := (d1,d2,d3,d4,d5,d6,d7,d8,d9)` with fixed-point encoding per project standard.
- `Epoch9 := (tau_epoch, segment_id, trial_index)` when time indexing is required.

No other identity mechanism is allowed.

## 45.3 A.3 Serialization operator renames (surgical override)
If any v1 section references token-based serialization, treat the following as the canonical v2 interface:

- `serialize_basis9_state(state_obj, sig9, epoch9) -> bytes`
- `deserialize_basis9_state(bytes_in, sig9, epoch9) -> state_obj`

Event payloads MUST be encoded as **Event9 records** (not token lists):
- `Event9 := (sig9, epoch9, event_code_u16, payload_bytes, payload_len_u16)`

## 45.4 A.4 Determinism contract (no coord_sig)
A run is deterministic iff identical inputs yield identical outputs **byte-for-byte**.

Optional non-coordinate-coord_sig run summary for logs:
- `RunSummary := (run_sig9, epoch9_end, output_byte_len, last_state_sig9)`

# 46 B. Bell Test Emulation Contract (NIST timetag format)

This section defines a deterministic emulation pipeline from NIST-style timetag streams into a sync-indexed trial ledger.
It exists to prevent “column guessing” and to enable fast replay in EigenWare.

## 46.1 B.1 Raw record schema
Each raw record is:
- `RawEvent := (channel_u64, timetag_u64, transfer_count_u64)`

Environment constant:
- `timetag_bin_seconds := 78.125e-12`

Stored channel meanings (0-based):
- `0` detector click
- `2` RNG output indicates setting **0**
- `4` RNG output indicates setting **1**
- `5` GPS PPS (1 Hz)
- `6` Sync pulse (trial boundary; moment RNG sampled / Pockels cell gating trigger)

## 46.2 B.2 Trial indexing (sync-defined)
Let `sync_time[k]` be the timetag of the k-th Sync pulse (channel 6) for a given side.
Trial k for that side is anchored on `sync_time[k]`.

## 46.3 B.3 Setting extraction (deterministic)
Operator:
- `extract_setting(sync_time_k, rng_events, latch_window) -> setting_code_u2`

Inputs:
- `rng_events` are RawEvents with channel in {2,4}
- `latch_window := (t_latch_start_bins_i64, t_latch_end_bins_i64)` relative to sync

Rule:
- If channel 2 fires at least once in window and channel 4 does not: `setting_code = 0`
- If channel 4 fires at least once in window and channel 2 does not: `setting_code = 1`
- If both fire in window: `setting_code = 2` (INVALID_BOTH)
- If neither fires in window: `setting_code = 3` (INVALID_NONE)

No silent coercion is allowed. Invalid states MUST be preserved in the trial ledger.

## 46.4 B.4 Click extraction as slot mask (deterministic)
Operator:
- `extract_click_mask(sync_time_k, click_events, detect_window, slot_params) -> click_mask_u16`

Definitions:
- `click_events` are RawEvents with channel = 0.
- `detect_window := (t_det_start_bins_i64, t_det_end_bins_i64)` relative to sync.
- `slot_count := 16`
- `slot_params := (window_start_bins_i64, slot_period_bins_u64)` where `window_start_bins_i64` is the first slot’s start offset from sync.

Slot mapping:
- `dt_bins := timetag_click - sync_time_k`
- If `dt_bins` not in detect_window, ignore.
- `slot_idx := floor((dt_bins - window_start_bins) / slot_period_bins)`
- If `0 <= slot_idx < 16`, set bit: `click_mask |= (1 << slot_idx)`.

## 46.5 B.5 Compact per-trial record for fast replay
Type:
- `BellTrialRecord := (trial_index_u64, setting_A_u2, setting_B_u2, A_mask_u16, B_mask_u16, sync_A_u64, sync_B_u64, aux_flags_u16)`

This record is the canonical “accelerated replay” primitive for engine optimization.
Raw timetags MAY be retained separately, but the simulation loop MUST be able to run using only BellTrialRecord streams.

## 46.6 B.6 Acceptance tests
Given identical raw event streams and identical window/slot parameters:
- BellTrialRecord output MUST match byte-for-byte across runs.
- Invalid setting states MUST reproduce identically.
- Slot masks MUST reproduce identically.
---

# 47 V3 SURGICAL PATCH APPENDIX (AUTHORITATIVE)

Normative rule: This appendix is append-only and overrides any ambiguous or conflicting language above. No carrier-anchor mutation is permitted post-boot. No prohibited cipher/digest schemes; all identity and bookkeeping use 9D coordinate signatures.

## 47.1 Canonical numeric type system (determinism-critical)

Definition: All determinism-critical scalars use fixed-point integers with saturating arithmetic.

Types:
- q63 := int64 (signed Q63, q63_one = 2^63-1)
- q32_32 := int64 (signed Q32.32)
- phase_u64 := uint64 (phase ring coordinate, modulo 2^64)
- delta_i64 := int64 (minimal-arc delta; two’s complement)
- u32_id, u64_coord_sig

Operators (host+device identical):
- Q63_ADD_SAT(a,b) -> q63
- Q63_SUB_SAT(a,b) -> q63
- Q63_MUL_Q63(a,b) -> q63  := sat( (int128)a*(int128)b >> 63 )
- Q63_DIV_Q63(a,b) -> q63  := sat( ((int128)a<<63) / max(1,b) )
- Q63_ABS(x) -> q63
- Q63_CLAMP(x,lo,hi) -> q63

Float/double are debug-only and SHALL NOT feed back into runtime state.

## 47.2 Immutable anchors vs mutable runtime (enforced)

Definitions:
- ANCHOR := immutable carrier phase constraint manifold (read-only after boot freeze)
- ANCILLA := mutable runtime state carrier updated per tick

Rules:
- ANCHOR_BOOT_FREEZE(seal_token) -> void; after freeze, any write is a hard violation.
- If an anchor update is required: FREEZE_RUNTIME(); FLUSH_ANCILLA(); RESET_RESONANCE(); APPLY_ANCHOR_UPDATE(); RESUME_RUNTIME().
- Otherwise: use PHASE_TRANSPORT operators; do not mutate anchors.

## 47.3 One-way coupling (no inverse mapping)

Projection:
- Pi: C -> R (carrier to runtime)
Constraint:
- No Pi^{-1} exists; runtime state cannot backprop into anchors.

## 47.4 Tick authority (single canonical source)

Definition: Each commit window has one causal index and one time surrogate latched at a named moment.

Operators:
- TickStamp tick_latch() -> {pulse_index_u64, tick_ns_u64, tick_src_u64}
Rules:
- pulse_index_u64 increments exactly once per commit window.
- tick_ns_u64 uses one canonical timebase per build (HOST_MONO or GPU_CLOCK64), never runtime-selectable.

## 47.5 Critical mass / max gradient ceiling (single integer)

Definition:
- cap_ratio = cap_num/cap_den, integers, 0<cap_num<cap_den
- cap_q63 = floor( (2^63-1) * cap_num / cap_den )

Operators:
- CRIT_CAP_Q63(cap_num,cap_den) -> q63
- CLAMP_CRIT_Q63(x_q63,cap_q63) -> q63 := min(x_q63,cap_q63)

If |grad| exceeds cap_q63: route_to_dark_excitation_state() (non-projecting, still contributes curvature).

## 47.6 Telemetry-drive vs lattice amplitude separation

Definition:
- drive(t) := telemetry-domain pulse drive (counts from power/current/voltage sampling)
- amp(t) := lattice-domain time-gradient amplitude (q63)

Invariant:
- drive calibrates delta_q63; drive != amp.

Boot-time freeze:
- FREEZE_CALIBRATION(k0, delta_q63, cap_q63, n_max_shells) -> void
- (delta_q63, cap_q63, n_max_shells) immutable for the session.

## 47.7 Runtime entry-point contract (single canonical run)

Definition:
- int ew_main(const EwBootArgs& args);
- main() must call ew_main(parse_args(...)) and nothing else starts a parallel scheduler.

ew_main stages execute in fixed order R0..R9:
R0 boot, R1 load anchors, R2 freeze/seal, R3 hydrate runtime, R4 pulse loop, R5 kernels, R6 commit ledger/artifacts, R7 projection/egress, R8 shutdown, R9 final commit.

## 47.8 UE control surface bounds

UE tools may write only runtime intent packets to bounded rings. UE may not mutate anchors, equations pages, or effective constants. Packaged builds disable import and anchor encode; projection only.


## Patch: Spider Encoding Uses f/a/v/i (Frequency, Amplitude, Voltage, Amperage)

Normative update: spider encoding MUST emit a 4‑tuple carrier observable per pulse/strand:

- **SpiderCode4 = (f_code, a_code, v_code, i_code)**

Where:
- **f_code** is the signed frequency coefficient (phase transport / operator selection).
- **a_code** is the amplitude coefficient (harmonic expansion strength).
- **v_code** is a voltage‑like potential observable (available work budget).
- **i_code** is an amperage‑like load observable (permitted transfer rate).

`v_code` and `i_code` are bounded deterministic observables used for gating and tensor‑gradient coupling; they are not asserted as physical SI voltage/current.

### Conservation gates
Define `power_proxy = v_code * i_code` (fixed‑point, clamped). The simulator enforces:
- No state/energy update unless `power_proxy` exceeds the absolute‑zero budget gate.
- No force/impulse update unless `power_proxy` exceeds the CMB sink threshold gate.

### Tensor‑gradient coupling
Treat the carrier as `p = (f,a,v,i)` and permit deterministic cross‑coupling via a quantized 4×4 tensor `G`:

- `p_next = clamp(p + G * p)`

This cross‑coupling is the canonical mechanism for extrapolating effective higher DOFs from a pulse.


---

## Patch: Focus-driven LOD, 32K texture ceiling, and carrier-coded projection

Genesis Engine treats 32K textures as an **asset ceiling**, not a default framebuffer target. Full-frame 32K render targets are not assumed or allocated. Instead:

- Simulation and material state remain **carrier-coded** in the substrate (ledger + anchors + pulse propagation).
- The viewport is a **projection consumer**: it requests parameters and samples texture detail only where it is visually justified.

### View-driven clarity scalar

For each object (or draw instance) the engine computes a deterministic clarity scalar `clarity ∈ [0,1]` from camera distance and focus state:

- `dist_m = distance(camera, surface)`
- `focal_distance_m` is the camera focus distance (default 0.3048 m / 1 foot)
- `focus_band_m` is the width of the sharp focus band
- `near_w` is a hard/soft gate that only allows highest-detail sampling within 1 foot when the focal distance is also 1 foot
- `focus_w = clamp(1 - abs(dist_m - focal_distance_m) / focus_band_m)`
- `screen_w` is a deterministic projected-size proxy to prevent tiny distant objects from pulling high LOD

`clarity = clamp(focus_w * near_w * screen_w)`

### Texture LOD rule (mip bias)

Textures are sampled with a deterministic mip bias derived from clarity:

- `lod_bias = -lod_boost_max * clarity`

This makes highest mips (including 32K content) reachable only when the surface is both **close** and **in focus**. Outside that region, lower mips are used.

### Virtual texturing / sparse residency

High-resolution assets are treated as tiled resources (virtual textures). Residency is driven by `clarity` and projected area:

- Only tiles needed by the current focal region are streamed/resident.
- Eviction is deterministic: tiles fall out of residency as clarity and projected importance fall.

### No display firmware dependence

The engine does not require display firmware changes. The final presentation path is a standard pixel framebuffer at a practical output resolution. Carrier coding compresses simulation and material state, and projection expands only what is needed for the current view.
