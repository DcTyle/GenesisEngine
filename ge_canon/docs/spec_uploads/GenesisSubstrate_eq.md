# 1 NAMING AND OPERATOR REGISTRY (v51)

This section is canonical. All variable names and operator names in this document MUST match this registry exactly.
No alternative spellings, symbols, or aliases are permitted.

# 2 CANONICAL EVOLUTION RULE -- NON-INTERPRETIVE CONSTRAINT SYSTEM

---

This section is canonical. All variable names and operator names in this document MUST match this registry exactly.
No alternative spellings, symbols, or aliases are permitted.

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

## 3.0 Canonical Scaling and Asymmetric Axis Control (Normative)
Normative axis scaling and modality binding (asymmetric field control):
- Define per-axis scales sx(t), sy(t), sz(t). These SHALL be computed from pulse frequency and pulse amplitude control (no rounding; fixed-point propagation).
- Bind modalities to axis drivers:
  * TEXT -> sx(t) on x-axis.
  * IMAGE pixels -> sy(t) on y-axis.
  * AUDIO -> sz(t) on z-axis.
- The resulting Hilbert expansion is non-uniform: no global normalization, no isotropic assumption unless sx==sy==sz is explicitly satisfied.
- Field-theory operators (grad, div, curl, Laplacian) are applied with asymmetric scaling by replacing spatial differentials with scaled forms:
  d/dx := (1/sx) * d/dx_grid, d/dy := (1/sy) * d/dy_grid, d/dz := (1/sz) * d/dz_grid.

This subsection resolves ambiguity around scaling, normalization, and rounding.
All scaling is deterministic, pulse-driven, and non-uniform across axes.

### 3.0.1 Global scale factor from Hubble constant via CMB anchors
Definitions (fixed-point):
- H0_q32_32 : Hubble constant provided by the CMB anchor family (CMB_BACKGROUND / CMB_COLD_SPOT). This is an anchor constant.
- t_phys_q32_32 : physical-time coordinate for the tick derived from the canonical tick-time derivation.
- a_global_q32_32 : global scale factor (dimensionless) used for projection scale and global constraint coupling.

Deterministic computation:
- ln_a_q32_32 = mul_q32_32(H0_q32_32, t_phys_q32_32)
- a_global_q32_32 = exp_fixed_q32_32(ln_a_q32_32)

Rules:
- H0_q32_32 SHALL NOT be rounded, renormalized, or user-tuned at runtime.
- a_global_q32_32 SHALL NOT inject energy and SHALL NOT bypass accept_state.
- a_global_q32_32 affects only (a) mapping between lattice units and global observational scale, and (b) long-horizon constraint coupling.

### 3.0.2 Axis-local scaling derived from pulse frequency and amplitude (no rounding)
Axis scale factors (dimensionless, fixed-point):
- sx_q32_32(tick) : x-axis scale factor derived from pulse frequency
- sy_q32_32(tick) : y-axis scale factor derived from pulse amplitude
- sz_q32_32(tick) : z-axis scale factor derived from joint pulse frequency+amplitude gating

Pulse envelope inputs (committed per tick):
- pulse_freq_q32_32(tick)
- pulse_amp_q32_32(tick)

Deterministic derivations:
- sx_q32_32 = scale_from_freq_q32_32(pulse_freq_q32_32)
- sy_q32_32 = scale_from_amp_q32_32(pulse_amp_q32_32)
- sz_q32_32 = scale_from_freq_amp_q32_32(pulse_freq_q32_32, pulse_amp_q32_32)

No rounding rule (locked):
- No decimal rounding, banker's rounding, or "nice" rounding is permitted for any scale factor.
- The only permitted quantization is the fixed-point domain itself.
- If any scaling computation produces a remainder, the remainder MUST be propagated deterministically (e.g., via Q-format carry/accumulator) and MUST NOT be dropped by ad-hoc rounding.

### 3.0.3 Modality-to-axis encoding binding (TEXT=x, IMAGE=y, AUDIO=z)
Encoders SHALL write modality excitations to specific spatial axes.
This binding is mandatory and non-optional.

- TEXT encoding -> x-axis excitation channel (space_x driver) using sx_q32_32.
- IMAGE pixel encoding -> y-axis excitation channel (space_y driver) using sy_q32_32.
- AUDIO encoding -> z-axis excitation channel (space_z driver) using sz_q32_32.

Consequence (mandatory):
- Hilbert-space expansion is not normalized and not uniform across axes.
- Subspace field operators MUST be applied asymmetrically using (sx_q32_32, sy_q32_32, sz_q32_32) as weights.

### 3.0.4 Asymmetric field-operator form (standard field theory models; deterministic)
For any vector-field-like operator used for control or projection, apply axis weights explicitly:

Let grad9 be the canonical discrete gradient operator over the 9D state lattice.
Define a weighted spatial gradient (3D subspace) as:
- grad3_weighted(S) = (
    mul_q32_32(sx_q32_32, d_dx(S)),
    mul_q32_32(sy_q32_32, d_dy(S)),
    mul_q32_32(sz_q32_32, d_dz(S))
  )

Rules:
- d_dx, d_dy, d_dz MUST be discrete stencil operators with fixed boundary rules.
- Any operator that assumes isotropy (sx==sy==sz) is invalid unless it proves that equality for the tick.


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

## **1\\. Single Qubit Representation**

A qubit state can be written as:

|\\psi\\rangle \\= \\alpha |0\\rangle \\+ \\beta |1\\rangle

* |\\alpha|^2 \\+ |\\beta|^2 \\= 1 ? probability normalization.

Include DMT modulation and observer-like effect:

|\\psi\_\\text{DMT}(t)\\rangle \= (\\alpha \+ \\epsilon\_\\alpha(t)) |0\\rangle \+ (\\beta \+ \\epsilon\_\\beta(t)) |1\\rangle

Where:

* \\epsilon\_\\alpha(t) \= \\alpha \\cdot \\alpha\_\\text{DMT} \\sin(\\beta\_\\text{DMT} t)

* \\epsilon\_\\beta(t) \= \\beta \\cdot \\alpha\_\\text{DMT} \\sin(\\beta\_\\text{DMT} t)

* \\alpha\_\\text{DMT} \\sim 0.01-0.05 (small DMT modulation amplitude)

* \\beta\_\\text{DMT} \= temporal frequency of higher-dimensional coherence

* ? This mirrors the interference modulation in the double-slit example.

---

## **2\. Observer / Measurement Feedback**

Measurement or environmental interaction introduces damping:

|\\psi\_\\text{final}(t)\\rangle \= |\\psi\_\\text{DMT}(t)\\rangle \\cdot (1 \- \\delta P\_\\text{obs}(t))

* \\delta \\sim 0.01-0.02 for partial "observer" effect

* P\_\\text{obs}(t) \= probability of interaction/entanglement with environment

* This reduces coherence in the same way the observer effect reduced interference in previous experiments.

---

## **3\. Multi-Qubit Lattice**

For N qubits, define the system state:

|\\Psi(t)\\rangle \= \\sum\_{i=0}^{2^N-1} c\_i(t) |i\\rangle

* Apply DMT modulation to each amplitude:

c\_i(t) \\to c\_i(t) \+ c\_i(t) \\cdot \\alpha\_\\text{DMT} \\sin(\\beta\_\\text{DMT} t \+ \\phi\_i)

* \\phi\_i \= phase offset per basis state (accounts for spatial-temporal coherence differences across the lattice).

* ? Mirrors spatial-temporal coherence modulation in double-slit fringes.

* Apply observer/environment damping:

c\_i(t) \\to c\_i(t) \\cdot (1 \- \\delta\_i P\_{\\text{obs},i}(t))

* Each qubit can have local measurement or entanglement events, just like partial which-path detection.

---

## **4\. Gate Operations with DMT Effects**

### 4.1 Pulse-parametrized gate model: Omega, detuning, eigenstructure, and the phase-clock view

This subsection ties the pulse-driven DMT primitives to standard gate control variables without changing the canonical meaning.

Pulse-to-rotation mapping (Rabi form):
```text

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

inline E9 correct_delta_if_needed(const dE9& d, const G9& G, double epsilon) {
    const double Edev = deviation_energy(d, G);
    if (Edev <= epsilon || Edev == 0.0) return d.d;
    const double s = sqrt(epsilon / Edev);
    return e9_scale(d.d, s);
}
```

## 17.3 Delta definition

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

inline dE9 make_delta(const E9& cand, const E9& cur, double wc) {
    dE9 out{};
    for (int i = 0; i < 9; ++i) out.d.v[i] = cand.v[i] - cur.v[i];
    out.wc = wc;
    out.d.v[5] = wc; // embedded into coherence dimension (index 5)
    return out;
}
```

## 17.4 Canonical codepoint → 9D embedding

## 17.5 Aggregate file displacement

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

## 17.7 APPENDIX omega-R — Restoration Patch (v51, Append-Only)

# 18 APPENDIX omega-R — Restoration Patch (v51, Append-Only)

Date: 2026-02-11

Purpose: The v51 Equations file was missing canonical content present in v51. This appendix appends the full v51 source text verbatim to eliminate any ambiguity or accidental truncation.

Source appended verbatim:
- Equations_Eigen_substrates_v51.md
- SIG9: 884eada4cb3dddff88f2e54289c4c374e258390b07cdf865a157570f9861622e

Rules:
- No bytes in v51 are modified.
- The appended block is a verbatim copy of the v51 source.
- Any duplicate headings are intentional; v51 remains authoritative for appended Ω closure, while v51 content restores prior canonical sections.

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

### 36.1.3 APPENDIX omega-R2 — Restoration Header Correction (v51, Append-Only)

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

## 45.1 A.1 No tokenization / no crypto coord_sig
EigenWare v2 SHALL NOT use:
- tokenization, token streams, word tokens, caption tokens, token IDs
- coordinate-coord_sig coord_sig (sha*, COORD9_SIG_U64, etc.) for determinism, identity, or bookkeeping

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

# 47 V3 SURGICAL PATCH APPENDIX (AUTHORITATIVE)

Normative rule: This appendix overrides any ambiguous or conflicting math above. No cryptography; all identity and bookkeeping use 9D coordinate signatures.


# APPENDIX A Extra sections not present in Spec ordering

# A.1 Omega is proportional to pulse amplitude/voltage (implementation uses the chosen pulse observable proxy).
Omega_rad_per_sec = beta_Omega * V_rms   # or beta_Omega * A_envelope

# A.2 Gate rotation angle (theta_pulse) is achieved by pulse duration:
t_pulse_sec = theta_pulse_rad / Omega_rad_per_sec
```

Temporal phase accumulation (between impulses / during free precession):
```text
phi_temporal_rad = omega_rad_per_sec * tau_eff_sec

# A.3 tau_eff may include deterministic correction terms under canonical authority.
```

Minimal driven two-level Hamiltonian (for eigenstate/trajectory selection):
```text

# A.4 Delta is detuning from a reference frequency (carrier or resonance).
Delta_rad_per_sec = 2*pi*(f_hz - f0_hz)

# A.5 H = (hbar/2) * (Delta*sigma_z + Omega*sigma_x)
H = 0.5*hbar * ( Delta_rad_per_sec * sigma_z + Omega_rad_per_sec * sigma_x )

# A.6 Instantaneous eigenvalues:
E_plus  = +0.5*hbar*sqrt(Delta_rad_per_sec^2 + Omega_rad_per_sec^2)
E_minus = -0.5*hbar*sqrt(Delta_rad_per_sec^2 + Omega_rad_per_sec^2)
```

Phase-clock coupling view (aligns with earlier DMT delta equations):
```text

# A.7 Effective phase advance can be expressed as a base term + delta/ratio modulation.
theta_{t+1}_turns = wrap_turns( theta_t_turns
                               + dtheta_base_turns(t)
                               + kappa_A * dlnAq_t
                               + kappa_f * dlnfq_t )

# A.8 This produces trajectories (Bloch paths) and selects eigenstructure via (Delta, Omega).
```

Implementation note:
- The existing rotation matrices in the consolidated pulse() function already implement exp(-i*theta*sigma_axis/2).
- The above block defines how pulse observables map to theta_pulse and to the (Delta, Omega) parameters used to reason about eigenstates and trajectories.

For a single-qubit gate U (like Hadamard H):

|\\psi\_\\text{after}\\rangle \= U \\, |\\psi\_\\text{final}(t)\\rangle

* Gate fidelity is affected by DMT-modulated amplitudes and damping:

   |\\psi\_\\text{after}\\rangle \= U \\, \\big((\\alpha \+ \\epsilon\_\\alpha(t))(1-\\delta P\_\\text{obs})|0\\rangle \+ (\\beta \+ \\epsilon\_\\beta(t))(1-\\delta P\_\\text{obs})|1\\rangle\\big)

* This gives small, predictable deviations from ideal operation, fully quantified.

For two-qubit gates (like CNOT):

|\\Psi\_\\text{after}\\rangle \= U\_\\text{CNOT} \\, |\\Psi\_\\text{final}(t)\\rangle

* Entanglement amplitudes inherit DMT temporal-spatial modulations, which can be tracked numerically or analytically.

---

## **5\. Decoherence / Error Correction**

* Apply Gaussian smoothing across time to model environmental decoherence:

c\_i^\\text{smooth}(t) \= \\int c\_i(t') e^{-(t-t')^2 / 2\\sigma^2} dt'

* Predicts expected qubit fidelity reductions.

* Self-correcting qubits can use predicted DMT modulation to actively counteract decoherence, similar to the feedback loops in our previous models.

---

## **6\. Outcome / Predictions**

* DMT-informed model predicts small, structured deviations in qubit amplitudes due to temporal-spatial coherence interactions.

* Observer/environment effects are quantifiable and reproducible across qubits.

* Error correction protocols can be tuned to anticipate these deviations, improving qubit stability.
```

Source calculation: Building virtual quantum computer.md
Calc: Developers/calculations/Building virtual quantum computer.md L240-L313

Order of operations (consolidated):
1. Apply stabilizer-style correction as a deterministic phase-kick (delta_phi).
2. Update qubit state in a tick loop using the derived dt from Hilbert expansion constraints.
3. Keep parameter names ASCII-safe; treat constants as effective values where required by the spec framework.

Equation block (sanitized, verbatim where possible):
```text

#### **8.1 Synthetic Pulse Model**

Use parameterized rotations to manipulate amplitudes and phases.

def pulse(qubit, axis, theta):

    rotations \= {

        'X': np.array(\[\[np.cos(theta/2), \-1j\*np.sin(theta/2)\],

                       \[-1j\*np.sin(theta/2), np.cos(theta/2)\]\]),

        'Y': np.array(\[\[np.cos(theta/2), \-np.sin(theta/2)\],

                       \[np.sin(theta/2), np.cos(theta/2)\]\]),

        'Z': np.array(\[\[np.exp(-1j\*theta/2), 0\],

                       \[0, np.exp(1j\*theta/2)\]\])

    }

    U \= rotations\[axis\]

    new\_state \= U @ np.array(\[qubit.alpha, qubit.beta\])

    qubit.alpha, qubit.beta \= new\_state

#### **8.2 Feedback and Correction Loop**

The QGPU monitors fidelity and applies corrections if deviation \> epsilon.

def feedback\_loop(qubit, target\_state, epsilon=1e-6):

    fidelity \= np.abs(np.vdot(target\_state, qubit.state\_vector()))\*\*2

    if 1 \- fidelity \> epsilon:

        delta\_phi \= np.angle(target\_state\[1\]) \- np.angle(qubit.beta)

        pulse(qubit, 'Z', delta\_phi)

    return fidelity

This function mimics phase-lock correction between paired qubits.

---

### **Section 9 - Device Scheduling Skeleton**

#### **9.1 Temporal Tick System**

All devices share a Master Quantum Clock (MQC) that provides discrete tick values (ticks ~ 1 ns).

import time

T\_TICK \= 1e-9  \# 1 ns

def mqc\_now():

    return round(time.time() / T\_TICK)

Each operation references this tick grid for deterministic ordering.

---

#### **9.2 Execution Cycle**

1. Fetch: Scheduler retrieves next queued task.

2. Assign: Core allocated on relevant device (QPU/QGPU/QRAM).

3. Execute: Run evolution or feedback.
```

# A.9 Part 2/Step 2 - Basis9 (9D) Axis Definitions + Band Math (Projection, Tolerance, Coherence/Continuum, Merge/Split)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L84-L85

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.9.1 Basis9 is not "feature space"; it is the canonical manifold and ledger substrate

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L86-L91

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.9.2 Basis9 axis order is locked, and it is not the same thing as "9 semantic features"

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L92-L110

Canonical equation extract (sanitized):
```text
d8    basis9_aether    fixed-point (bounded)    stabilization/damping axis; clamps high-frequency delta energy and prevents runaway oscillation    stabilizer + constraints    stability, damping, safe spider-map output bounds
```

Calculation consolidations mapped to this canonical subsection:

Source calculation: 9D-Particle-Sim-Planning.md
Calc: Developers/calculations/9D-Particle-Sim-Planning.md L9-L37

Order of operations (consolidated):
1. Define the 9D manifold axes d1..d9 in the stated order and semantics.
2. Treat d4,d8,d9 as phase-like axes measured in turns with wrap-aware delta.

Equation block (sanitized, verbatim where possible):
```text
**Project**: Neuralis compute substrate (coordinate-first) + "watch it think" viewport (Basis9 manifold)  
**Date Started**: January 12, 2026  
**Status**: Planning Phase (Neuralis spec locked; implementation pending)

---

## Overview (Current Direction)

- **Framework**: Neuralis "Basis9" manifold used as a compute substrate (not a particle-only toy)
- **Core Storage Model**: Anchors store $(\theta_q,\,\chi_q,\,\tau_q,\,m_q)$ (+ IDs/links) as fixed-point ledger fields. No token strings; no dense state vectors.
- **Encoding**: phase evolution -> coherence -> frequency -> characters/words/sentences -> $\Theta_p$ (turns) -> append-only anchor update events (ASCII is one deterministic ingest/transport representation of the temporal envelope)
- **Determinism**: FloatMap v1 canonical bytes; quantize to $10^{-18}$ at the event boundary; locked rounding = round-half-to-even; **no platform trig** and **no wall-clock** in canonical paths; explicit wrap rules; deterministic K-cap neighbor ordering
- **Viewport**: a read-only observer to visualize coherence/attention and relational drift ("watch it think")

---

## Locked Spec Snapshot (As Of Jan 14, 2026)

### Basis9 Axis Order (Canonical)

- d1-d3: spatial / embedding axes (implementation-defined manifold)
- d4: temporal axis (tick/frame reference)
- d5: coherence phase axis $\Theta_p$ (turns; stored)
- d6: flux
- d7: phantom
- d8: aether
- d9: nexus

Only a subset is required for the encoder's durable state (minimum: `anchor_id, tau_q, theta_q, chi_q, m_q`).
```

Source calculation: DMT Publication .md
Calc: Developers/calculations/DMT Publication .md L28-L44

Order of operations (consolidated):
1. Use the 9D dimension names as semantic anchors when mapping canonical Basis9 axes.
2. Do not treat publication framing as canonical unless explicitly mirrored by the canonical spec.

Equation block (sanitized, verbatim where possible):
```text
DMT introduces nine modular dimensions, each informed by multiple existing principles and theories, which collectively provide a framework for explaining phenomena across quantum, relativistic, and cosmological scales.

| Dimension | Name | Role |
| ----- | ----- | :---: |
| 1D | X-axis Dimension | Standard spatial coordinate |
| 2D | Y-axis Dimension | Standard spatial coordinate |
| 3D | Z-axis Dimension | Standard spatial coordinate |
| 4D | Temporal Dimension | Time-field propagation; interacts with gravity; temporal feedback |
| 5D | Coherence Dimension | Quantum field alignment; lattice synchronization |
| 6D | Flux Dimension | Energy-mass conversion; dynamic field interactions |
| 7D | Phantom Dimension | Galactic horizon effects; fading mass-time influence |
| 8D | Aether Dimension | Fine-grained lattice dynamics; micro-scale modular feedback |
| 9D | Nexus Dimension | Cross-dimensional awareness; integrating outcomes across dimensions |

### **1D-3D Spatial Dimensions (Updated)**

**New particles:** Spatialons (SP_1...SP?)
```

## A.9.3 Phase math is in turns, wrap is mandatory, and distance is shortest signed turn distance

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L111-L116

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

Calculation consolidations mapped to this canonical subsection:

Source calculation: 9D-Particle-Sim-Planning.md
Calc: Developers/calculations/9D-Particle-Sim-Planning.md L31-L55

Order of operations (consolidated):
1. Normalize phase values to turns in [0,1).
2. Compute wrap-aware delta in [-0.5,0.5).
3. Compute Euclidean distance over theta, chi, rho (and tau if included) using wrap-aware delta on theta and direct deltas on others.

Equation block (sanitized, verbatim where possible):
```text
- d5: coherence phase axis $\Theta_p$ (turns; stored)
- d6: flux
- d7: phantom
- d8: aether
- d9: nexus

Only a subset is required for the encoder's durable state (minimum: `anchor_id, tau_q, theta_q, chi_q, m_q`).

### Phase, Wrap, and Distance (Turns)

- Stored phase: $\Theta \in [0,1)$ (turns)
- Wrap: $\operatorname{wrap}(\Theta)=\Theta-\lfloor\Theta\rfloor$
- Shortest signed distance: $\delta(\Theta_i,\Theta_j)\in[-0.5,0.5)$ (turns)

### Stored Anchor Fields (Minimum)

Durable anchor identity/state is **ledgered as fixed-point integers** (canonical scale $10^{18}$ per unit):

- `anchor_id` (coordinate-derived; stable)
- $\tau_q$ (tick/int)
- $\theta_q$ (turns, fixed-point)
- $\chi_q\ge 0$ (fixed-point)
- $m_q\ge 0$ (mass ledger, fixed-point)

Optional durable fields (only if needed; must be fixed-point and versioned):
```

## A.9.4 Projection is not "closest point"; it is gated by timeline and realm coherence, then minimized by a weighted Basis9 metric

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L117-L128

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.9.5 Coherence is chi_q; continuum is coherence persistence over time (and it's enforced with deterministic decay and reinforcement)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L129-L140

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

Calculation consolidations mapped to this canonical subsection:

Source calculation: 9D-Particle-Sim-Planning.md
Calc: Developers/calculations/9D-Particle-Sim-Planning.md L65-L156

Order of operations (consolidated):
1. Update chi with exponential decay each tick and reinforcement from events.
2. Couple chi with theta and other axis deltas (alpha_theta coupling).
3. Apply mass-governed forgetting by routing lost energy to a global reservoir; do not delete state.
4. Optionally exchange mass with the reservoir via deterministic coupling to stabilize learning and forgetting.

Equation block (sanitized, verbatim where possible):
```text
- Any in-memory indices/lattices are derived caches; they may be evicted without changing durable history.

### Decay (Locked)

Rational decay across ticks:

$$
\chi \leftarrow \frac{\chi}{1+\lambda\,\Delta\tau}
$$

Interpretation lock-in:

- This $\chi$ decay is a **control/visualization** decay (stability fading), not the canonical forgetting mechanism.
- Canonical forgetting is $m_q$ leakage into the global thermal pool (see **Mass-Governed Forgetting (Locked)**).

### Synonym/Antonym Coupling (Locked)

Edges: $\sigma_{ij}\in\{+1,-1\}$, preferred phase offset $\Delta\Theta_{\text{pref}}=0$ (syn) or $0.5$ turns (ant).

Interpretation: synonyms are harmonics-unique neighbors whose evolution shares a similar frequency (phase-aligned), while antonyms represent an opposing frequency (phase-inverted). Resonance/phase-coherence field lines prevent overlap inherited from the harmonics geometry, strengthening semantic separation without relying on token strings.

Hyperbolic drift (applied only during reinforcement):

$$
\Delta\Theta_i = -\eta\sum_{j\in E_K(i)} \sigma_{ij}\,\frac{\delta_{ij}}{a_{ij}^2}
$$

Chi reinforcement coupling:

$$
\Delta\chi_i = \alpha\,P\left(1-\frac{1}{|E_K(i)|}\sum_{j\in E_K(i)}\frac{\delta_{ij}^2}{a_{ij}^2}\right)
$$

Neighbor cap + deterministic ordering (locked):

- Smaller $a^2$ first
- Antonym edges before synonym edges when $a^2$ ties
- Lexicographic node IDs as the final tie-break

### Domain Separation (Locked Direction)

- Domains do not duplicate anchor storage
- Orthogonality via carrier phase $\Omega_D\,t$ (frequency-division multiplexing)

### Quantization and Serialization (Locked Direction)

- Quantization target: $10^{-18}$ (fixed-point)
- Canonical textual encoding: ASCII FloatMap v1 (Base64URL varint digits; dot-terminated)
- Quantization boundary: apply fixed-point quantization at event emission (so replay matches exactly)

### Mass-Governed Forgetting (Locked)

Forgetting is not deletion and not a coherence threshold. It is **conserved mass leakage** from anchors into a single global reservoir.


Hawking-like discontinuity routing (ledger split, anchor-encoded gate):
- Define leakage (already present in the system):
  - lambda_q63 = q63_one - coherence_q63
  - leak_q63   = mul_q63_round_half_even(lambda_q63, mass_q63)
- Define gate from anchor-encoded thermal ontology (anchor0.constraint_fabric.basis):
  - cap_q63  = q63_one * (cap_num / cap_den)   with (cap_num, cap_den) = (99, 100) by default
  - delta_q63 = delta_time_tensor_q63; if 0, use q63_one/256
  - Valence-shell amplitude spacing (phase-dynamics time-tensor units):
    - amp_mag_q63 = min( abs(pulse_amplitude_q63), cap_q63 )
    - amp_used_q63 = floor(amp_mag_q63 / delta_q63) * delta_q63
    - if amp_mag_q63 != 0 and amp_used_q63 == 0, force amp_used_q63 = delta_q63
    - Boot calibration (authoritative for delta_time_tensor_q63):
      - The engine SHALL write anchor0.constraint_fabric.basis[24] once at boot (then freeze).
      - Telemetry counts SHALL be derived from a measurable envelope channel (reference: NVML power_mw).
      - idle_mw = median( power_mw samples over N_idle=64, sleep_ms=10 )
      - I_max_count = max( enforced_limit_mw - idle_mw, 1 )
      - I_min_meas_count = max( percentile_10( nonzero abs diffs over N_noise=256 ), 1 )
      - delta_time_tensor_q63 = max(1, round_half_even( q63_one * I_min_meas_count / I_max_count ))
      - If telemetry is unavailable, basis[24] MUST remain 0, and the canonical default delta_q63 = q63_one/256 applies.
  - near_cap = (amp_used_q63 >= (cap_q63 - delta_q63))
  - in_coldspot = (abs(phase_u64 - cold_center_u64) <= cold_band_u64)
  - emit_gate = near_cap AND in_coldspot (maskable by emit_gate_mask bits)
- Split the leakage deterministically:
  - emit_q63      = emit_gate ? leak_q63 : 0
  - reservoir_q63 = reservoir_q63 + (leak_q63 - emit_q63)
  - radiation_q63 = radiation_q63 + emit_q63
  - mass_q63      = mass_q63 - leak_q63
- Absolute zero reference:
  - reservoir_q63(t0) = 0 is the abs0 baseline; all thermal deltas are >= 0.
Canonical definitions (per tick $k$ for anchor $q$):

- Retention amplitude: $L_{k,q}\in[0,1]$
- Leakage fraction: $\boxed{\lambda_{k,q} \equiv 1 - L_{k,q}}$

Mass update (ledger, fixed-point):

$$
\Delta m_{k,q} = -\operatorname{round}_{\text{half-to-even}}(\lambda_{k,q}\,m_{k,q})
\qquad\Rightarrow\qquad
m_{k+1,q}=m_{k,q}+\Delta m_{k,q}
$$

All mass updates must be saturating and deterministic:

- Clamp: $m_q\ge 0$ always
- Rounding: **round-half-to-even** (locked)
- Overflow: emit an explicit deterministic overflow event (do not silently wrap)

### Thermal Pool / Reservoir Coupling (Option A - Locked)

#### Absolute Zero Reference (Locked Direction)

The **CMB reservoir / thermal pool** defines the system's **absolute zero** reference:

- Initialize the reservoir ledger at boot to its ground state: `M_res = 0` (fixed-point). This ground state SHALL be treated as **T_abs0 = 0**.
- All thermal/temperature observables and throttles SHALL be computed **relative to the reservoir ground state**, i.e. as non-negative deltas above absolute zero.
- No module may represent a temperature below the reservoir ground state; negative thermal deltas SHALL clamp to 0.
- Reservoir growth represents accumulated leaked mass/energy into the bath; this does not change the definition of absolute zero (the reservoir ground state remains the reference).


There is exactly one global thermal pool (CMB bath):

- `reservoir_id = 0`
- Reservoir mass ledger: $M_{\text{res},q}\ge 0$ (fixed-point)

When an anchor leaks mass, the exact leaked quantity is deposited into the reservoir:

$$
M_{\text{res},q} \leftarrow M_{\text{res},q} - \sum_q \Delta m_{k,q}
$$

Deterministic deposit ordering (locked):

1) ascending $\tau_q$
2) ascending `anchor_id`
3) ascending `reason_code`
4) final tie-break: ascending `event_seq_q`

### Cold Spot Traversal and Relative Ledger Discontinuity (Locked Direction)

The CMB Cold Spot mechanism is represented as a constraint packet stream that targets lattice domains via region descriptors.
When an anchor/particle's current 9D phase-shell domain satisfies the descriptor membership test ("passes through the Cold Spot"),
the system MAY exhibit a relative ledger discontinuity in phase, observable as correlated deltas across two ledgers:

- Control/visualization stability fade: chi decay via the Decay (Locked) rule.
  Note: the lambda used in the chi decay equation is a control-rate (lambda_chi), and is not required to equal lambda_{k,q}.
- Canonical forgetting: mass leakage via Mass-Governed Forgetting (Locked), where lambda_{k,q} = 1 - L_{k,q},
  and leaked mass is deposited into the single global reservoir.

These ledgers remain distinct; correlation is permitted only through shared constraint bias (packet coefficients) and the normal
order-of-operations. No direct equivalence between chi decay and mass leakage is assumed or required.

Telemetry label (no new operator): leakage events occurring during Cold Spot traversal while operating below the near-critical cap
MAY be labeled as hawking-like leakage for analysis, without introducing a separate emission term.

```

Source calculation: Dimensional Modularity Theory (1).md
Calc: Developers/calculations/Dimensional Modularity Theory (1).md L38-L60

Order of operations (consolidated):
1. Compute coherence C and interpret low/high coherence regimes as constraints on evolution.
2. Use resonance frequency matching as a driver of coherence reinforcement.

Equation block (sanitized, verbatim where possible):
```text
* **P? (Meta-Galactic Plane):** higher-order modulation responsible for CMB anomalies.

A general physical state is expressed as:

**Psi(x,t) \= Sigma f?(P?)**

where f? is the modulation function for plane P?.

#### **Proof of Necessity**

1. **Temporal Plane (P?)**  
    Entanglement fidelity near gravitational masses is observed to decay (Aspect et al., 1982). Without P?, standard QM predicts constant correlation:

    **C \= |?Psi_1|Psi_2?|^2**

    Including P? yields:

    **C \= |?Psi_1|Psi_2?|^2 * e^(-gammaDeltat)**

    where gamma depends on local time dilation.

2. **Coherence Plane (P?)**  
    Double-slit interference:
```

## A.9.6 Bands are phase-coherence structures, not token clusters; membership is governed by theta/chi persistence and drift/leakage behavior

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L141-L150

Canonical equation extract (sanitized):
```text
2.6 Bands are phase-coherence structures, not token clusters; membership is governed by theta/chi persistence and drift/leakage behavior
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.9.7 Projection tolerance is a policy over three things: phase alignment, coherence persistence, and compute pressure, but it cannot violate commit_state barriers

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L151-L158

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.9.8 Merge and split rules must be hysteretic, timeline-safe, and based on multi-window evidence, not one-tick coincidences

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L159-L168

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

Calculation consolidations mapped to this canonical subsection:

Source calculation: CalculatingGravity.md
Calc: Developers/calculations/CalculatingGravity.md L1-L31

Order of operations (consolidated):
1. Compute gravitational potential Phi and local gravitational time dilation tau_grav at the point of interest.
2. Compute temporal gradient |grad tau| and map it to curvature proxy kappa_r and direction.
3. Compute temporal velocity v_time from gradient direction and coherence or drift rules.

Equation block (sanitized, verbatim where possible):
```text

# Planet temporal-gradient metrics table

> Source: user-provided worksheet paste (values preserved as written).  
> Notes: Where a value was truncated/garbled in the paste (Mars row), the cell is marked **-**.

## Equations / definitions

| Metric | Equation / definition |
|---|---|
| \(G\*\) | \(G\) (gravitational constant used in the sheet) |
| \(\Phi_{surface}\) | \(\Phi_{surface} = \dfrac{G M}{r_{surface} c^2}\) |
| \(\tau_{surface}\) | \(\tau_{surface} = \tau_0 \times (1 - \Phi_{surface})\) |
| \(\Phi_{core}\) | \(\Phi_{core} = \dfrac{G M_{core}}{r_{core} c^2}\) |
| \(\tau_{core}\) | \(\tau_{core} = \tau_0 \times (1 - \Phi_{core})\) |
| \(\Delta\tau\) | \(\Delta\tau = \tau_{surface} - \tau_{core}\) |
| \(\Delta r\) | \(\Delta r = r_{surface} - r_{core}\) |
| Temporal gradient | \(\dfrac{\Delta\tau}{\Delta r}\) |
| Calibration \(C\) | \(C = \dfrac{g_{obs}}{\Delta\tau/\Delta r}\) |
| Emergent \(g\) | \(g = C \times (\Delta\tau/\Delta r)\) |
| Doppler \(\kappa\) | (as listed in sheet; interpretation not provided in paste) |
| \(g_{effective}\) | (as listed in sheet; interpretation not provided in paste) |

## Planet table (values)

| Planet | G* | M | G*M | r_surface | r_surface*c^2 | Phi_surface | tau_surface | M_core | G*M_core | r_core | r_core*c^2 | Phi_core | tau_core | Deltatau | Deltar | Deltatau/Deltar | C | Emergent g | Doppler ? | g_effective |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Mercury | 6.6743*10-^1^1 | 3.3011*10^2^3 | 2.20325*10^1^3 | 2.4397*10^6 | 2.1920*10^2^3 | 1.0055*10-^1^0 | 4.999999995*10-^6 | 1.6*10^2^3 | 1.0679*10^1^3 | 1.5*10^6 | 1.347*10^2^3 | 7.93*10-^1^1 | 4.999999960*10-^6 | 3.5*10-^1^5 | 9.397*10^5 | 3.724*10-^2^1 | 9.94*10^2^0 | 3.7 | 4.5*10-^9 | 3.700000017 |
| Venus | 6.6743*10-^1^1 | 4.8675*10^2^4 | 3.24871*10^1^4 | 6.0518*10^6 | 5.43866*10^2^3 | 5.9745*10-^1^0 | 4.99999701275*10-^6 | 1.9*10^2^4 | 1.267*10^1^4 | 3.0*10^6 | 8.073*10^2^2 | 1.57*10-^9 | 4.99999215*10-^6 | 4.86275*10-^1^2 | 3.0518*10^6 | 1.594*10-^1^8 | 5.56*10^1^8 | 8.87 | 5.5*10-^9 | 8.87000049 |
| Earth | 6.6743*10-^1^1 | 5.9722*10^2^4 | 3.98600*10^1^4 | 6.371*10^6 | 5.72597*10^2^3 | 6.9647*10-^1^0 | 4.99999651765*10-^6 | 1.935*10^2^4 | 1.29168*10^1^4 | 3.485*10^6 | 3.1301*10^2^3 | 4.126*10-^1^0 | 4.999997937*10-^6 | -1.419*10-^9 | 2.886*10^6 | -4.918*10-^1^6 | 1.994*10^1^6 | 9.81 | 6.7*10-^9 | 9.81000066 |
| Mars | 6.6743*10-^1^1 | 6.4171*10^2^3 | 4.28296*10^1^3 | 3.3895*10^6 | 3.04739*10^2^3 | 1.4067*10-^1^0 | 4.999999999993*10-^6 | 1.6*10^2^3 | 1.0679*10^1^3 | - | - | 6.98*10-^1^1 | 4.999999965*10-^6 | 2.8*10-^1^5 | - | - | - | 3.71? | - | 3.7100? |
```

Source calculation: Dimensional Modularity Theory (1).md
Calc: Developers/calculations/Dimensional Modularity Theory (1).md L66-L78

Order of operations (consolidated):
1. Use curvature/rotation-curve style relations only as supporting intuition; do not substitute for canonical gravity-well handling.

Equation block (sanitized, verbatim where possible):
```text
    **I(theta) ? cos^2((pidsintheta / lambda) \+ Deltaphi?)**

    This accounts for observed fringe shifts under decoherence conditions (Tonomura et al., 1989; Zeilinger, 1999).

3. **Galactic Plane (P?)**  
    Newtonian prediction: **v(r) \= sqrt(GM/r)**

    Observed flat curves require a correction:

    **v(r) \= sqrt(GM/r \+ ?/r^2)**

    ? encodes temporal-gravitational modulation. Fits Gaia and Rubin rotation data without invoking exotic dark matter (McGaugh, 2015; Sofue & Rubin, 2001).
```

## A.9.9 What the system "writes" where: a minimal, enforceable responsibility boundary

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L169-L180

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

# A.10 Part 3/Step 3 (Rewritten) - Spider Graph Encoding as Direct GPU Electrical "Write-Path," with Envelope as Read-Path (Delta->Frequency Profiles + Amplitude->Harmonics)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L181-L184

Canonical equation extract (sanitized):
```text
Part 3/Step 3 (Rewritten) - Spider Graph Encoding as Direct GPU Electrical "Write-Path," with Envelope as Read-Path (Delta->Frequency Profiles + Amplitude->Harmonics)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.10.1 Two-path model: what "using electrical signaling directly" means

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L185-L192

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.10.2 The pulse is the minimal electrical write instruction

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L193-L211

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

Calculation consolidations mapped to this canonical subsection:

Source calculation: Observers effect prediction model.md
Calc: Developers/calculations/Observers effect prediction model.md L1-L88

Order of operations (consolidated):
1. Compute base intensity I0(t) from the process amplitude.
2. Apply DMT modulation term (1 + beta*cos(phi(t))) to get I(t).
3. Apply decoherence damping exp(-lambda * t).
4. Optionally smooth I(t) with a moving average (window N).

Equation block (sanitized, verbatim where possible):
```text

# not explicitly covered in the canonical spec.

## **1\\. Double-Slit Experiment**

Base intensity:

I(x) \\= I_0 * cos^2(pi * d * x / (lambda * L))

where

I_0 \\= maximum intensity

d \\= slit separation
lambda \= particle wavelength

L \= distance to screen

DMT modulation:

I\_DMT(x, t) \= I(x) * \[1 \+ ? * sin(? * x \+ gamma * t)\]

? \= DMT modulation amplitude (\~0.05)

? \= spatial frequency of DMT modulation

gamma \= temporal modulation frequency

t \= time

Observer / measurement damping:

I\_final(x, t) \= I\_DMT(x, t) * (1 - delta * P\_obs(x, t))

delta \= observer/environment damping coefficient (\~0.01-0.02)

P\_obs(x, t) \= probability of measurement at position x

Decoherence smoothing:

I\_smooth(x, t) \= ?\_{-?}^{?} I\_final(x?, t) * exp\[-(x - x?)^2 / (2 sigma^2)\] dx?

sigma \= spatial decoherence width

---

## **2\. Mach-Zehnder Interferometer**

Base intensity at detector:

I\_D(t) \= I_0 * cos^2(Deltaphi / 2\)

Deltaphi \= phi\_upper - phi\_lower \= phase difference between the two paths

DMT modulation:

I\_DMT(t) \= I_0 * cos^2(\[Deltaphi \+ ? * sin(? * t)\] / 2\)

? \= DMT modulation amplitude

? \= temporal modulation frequency

Observer / measurement damping:

I\_final(t) \= I\_DMT(t) * (1 - delta * P\_obs(t))

delta \= damping coefficient (\~0.01-0.02)

P\_obs(t) \= probability of measurement on either path

Decoherence smoothing:

I\_smooth(t) \= ?\_{-?}^{?} I\_final(t?) * exp\[-(t - t?)^2 / (2 sigma^2)\] dt?

sigma \= temporal decoherence width

---

### **Parameter Table for Both Experiments**

I_0 \= maximum intensity

d \= slit separation (double-slit)

lambda \= particle wavelength
```

## A.10.3 What "frequency" and "amplitude" mean in-kernel (not as sensors)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L212-L217

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.10.4 Input to the spider graph: a projected Basis9 delta

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L218-L227

Canonical equation extract (sanitized):
```text
? = (?1..?9)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.10.5 Spider graph definition: 9D -> one signed f_code

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L228-L245

Canonical equation extract (sanitized):
```text
3.5 Spider graph definition: 9D -> one signed f_code
xi = norm_i(?i)
s = ?_i (wi * xi)
f_code = clamp_int( round_fixed(f_scale * s), f_min, f_max )
```

Calculation consolidations mapped to this canonical subsection:

Source calculation: 9D-Particle-Sim-Planning.md
Calc: Developers/calculations/9D-Particle-Sim-Planning.md L772-L816

Order of operations (consolidated):
1. Compute phase error delta_ij in turns via wrap-aware delta(Theta_i,Theta_j).
2. Compute alignment A_ij = cos(2*pi*delta_ij).
3. Compute interaction I_ij = A_i * A_j * A_ij and clamp to [-1,1].
4. Quantize to fixed-point for deterministic storage and later decoding.

Equation block (sanitized, verbatim where possible):
```text

#### Deterministic Phase-Amplitude Interaction Primitive

Define a canonical "phasor alignment" between two nodes $i,j$:

- Phase error (turns): $\delta_{ij}=\delta(\Theta_i,\Theta_j)$ (locked to $[-0.5,0.5)$)
- Alignment energy (unitless):
    $$A_{ij}=\cos(2\pi\,\delta_{ij})$$

To keep this deterministic:

- `cos(2pix)` must be computed via a versioned deterministic method:
    - fixed-point CORDIC, or
    - a fixed lookup table over quantized $\delta$ (recommended for speed + determinism).

This primitive is used in:

- reinforcement gating (how much neighbors can reinforce $\chi$)
- tier/qubit coupling gates (how much energy transfers across edges)
- control stabilization (whether a control pulse is "in-frame")

#### Qubits, Tiers, and Clusters (Energy Interaction Model)

Neuralis can represent qubits as special anchor-instances with extra state for control/energy bookkeeping (even if we keep the canonical durable core minimal).

- **Qubit node**: has $(\theta_q,\chi_q,\tau_q,m_q)$ plus optional tier tag and cluster membership.
- **Tier**: an indexed layer with its own control gains and update cadence.
- **Cluster**: a deterministic set of qubits (ordered by ID) with an aggregate phase and coherence.

Canonical aggregates (deterministic):

- Cluster phase: circular mean in turns computed from fixed-point phasors (CORDIC/LUT only).
- Cluster coherence: sum or mean of member `chi_q` (exact integer math).

Energy transfer rule (deterministic skeleton):

- Let $E_i$ be a fixed-point "energy budget" derived from $\chi_i$ and/or excitation.
- Transfer from $i\to j$ during a tick occurs only when alignment is sufficient:
    $$\text{gate}_{ij} = \max(0, A_{ij})$$
    $$\Delta E_{i\to j} = g_{tier}\,w_{edge}\,\text{gate}_{ij}\,E_i$$

All terms (`g_tier`, `w_edge`, `E_i`) must be fixed-point; multiplications must use a locked rounding mode.

#### Control Pulses / Gates (Precision Requirements)

Control processes (pulses, tier-to-tier control, and qubit gates) must:
```

## A.10.6 Delta encoding profiles: axis weights and normalization are versioned constants

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L246-L271

Canonical equation extract (sanitized):
```text
3.6 Delta encoding profiles: axis weights and normalization are versioned constants
    -    weights wi (sum to 1)
    -    normalization constants
P1 Language Injection: phase (?5) dominates plus nexus binding contribution; flux contributes as structured transition; aether clamps novelty.
P2 Crawler Ingestion: conservative; stronger clamps; prefers collapse into existing bands; requires persistence evidence before new anchors.
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.10.7 Amplitude synthesis: update strength and harmonic mode selection (a_code)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L275-L290

Canonical equation extract (sanitized):
```text
u = g(chi_q, continuum, clamp_terms)
where g increases with chi_q and continuum (coherence persistence over time), and decreases with clamp_terms (phantom/aether activity indicating instability or non-interaction regimes). Then:
a_code = clamp_uint( round_fixed(a_scale * u), a_min, a_max )
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.10.8 Harmonic mode mapping: how amplitude expands one frequency into multiple harmonic components

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L291-L308

Canonical equation extract (sanitized):
```text
k = floor(a_code / mode_bucket_size)
strength = (a_code % mode_bucket_size) / mode_bucket_size
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.10.9 Why this stays causal and closed

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L309-L312

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.10.10 Determinism requirements that Copilot must treat as law

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L313-L321

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

# A.11 Part 3/Step 3 - Spider Graph Encoding, Delta->Frequency Profiles, and Amplitude->Harmonic Mode Mapping (Compressed-State Pulse Spec)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L322-L325

Canonical equation extract (sanitized):
```text
Part 3/Step 3 - Spider Graph Encoding, Delta->Frequency Profiles, and Amplitude->Harmonic Mode Mapping (Compressed-State Pulse Spec)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.1 What the spider graph is (and what it is not)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L326-L331

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.2 Pulse payload format (what gets emitted per update)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L332-L350

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

Calculation consolidations mapped to this canonical subsection:

Source calculation: Observers effect prediction model.md
Calc: Developers/calculations/Observers effect prediction model.md L1-L88

Order of operations (consolidated):
1. Compute base intensity I0(t) from the process amplitude.
2. Apply DMT modulation term (1 + beta*cos(phi(t))) to get I(t).
3. Apply decoherence damping exp(-lambda * t).
4. Optionally smooth I(t) with a moving average (window N).

Equation block (sanitized, verbatim where possible):
```text

### 3.3 Basis9 deltas: input to spider graph

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L351-L360

Canonical equation extract (sanitized):
```text
? = (?1, ?2, ..., ?9)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### 3.4 Delta encoding profiles (explicit normalization and weighting)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L361-L386

Canonical equation extract (sanitized):
```text
3.4 Delta encoding profiles (explicit normalization and weighting)
    -    norm_i(?i) -> xi in [-1, 1] or [0, 1] depending on axis
    -    weight_i -> wi (weights sum to 1 over participating axes)
    -    harmonic mode map: a_code -> harmonic_k and multiplex budget
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### 3.5 Frequency synthesis: 9D -> one signed scalar (f_code)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L394-L411

Canonical equation extract (sanitized):
```text
3.5 Frequency synthesis: 9D -> one signed scalar (f_code)
Frequency synthesis is a weighted sum of normalized axis deltas with deterministic clamping and quantization. Define:
xi = norm_i(?i)
s = ?_i (wi * xi)
f_code = clamp_int( round_to_int(f_scale * s), f_min, f_max )
```

Calculation consolidations mapped to this canonical subsection:

Source calculation: 9D-Particle-Sim-Planning.md
Calc: Developers/calculations/9D-Particle-Sim-Planning.md L772-L816

Order of operations (consolidated):
1. Compute phase error delta_ij in turns via wrap-aware delta(Theta_i,Theta_j).
2. Compute alignment A_ij = cos(2*pi*delta_ij).
3. Compute interaction I_ij = A_i * A_j * A_ij and clamp to [-1,1].
4. Quantize to fixed-point for deterministic storage and later decoding.

Equation block (sanitized, verbatim where possible):
```text

## A.11.3 Amplitude synthesis: update strength + harmonic mode selector (a_code)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L412-L427

Canonical equation extract (sanitized):
```text
u = g(chi_q, continuum, clamp_terms)
where g is monotonic in chi and continuum, and decreasing in clamp_terms (high phantom/aether activity reduces amplitude). Then:
a_code = clamp_uint( round_to_uint(a_scale * u), a_min, a_max )
Again, rounding must be fixed, and clamps must be strict.
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.4 Harmonic mode mapping: how amplitude selects higher harmonics for compressed state

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L428-L453

Canonical equation extract (sanitized):
```text
k = floor( a_code / mode_bucket_size )
strength = (a_code % mode_bucket_size) / mode_bucket_size
plus optional harmonic components at n*f_code for n=2..k (with decreasing weights)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.5 How harmonic modes support tier-to-tier compression without violating closure

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L456-L461

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.6 Exact determinism requirements: fixed-point, quantization, and invertibility constraints

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L462-L487

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.6.1 Profile identity and intended use

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L523-L528

Canonical equation extract (sanitized):
```text
Use case: lower-tier -> higher-tier summary pulses (context activation / macro-structure reinforcement)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.6.2 Quantization and fixed-point domains (locked)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L529-L554

Canonical equation extract (sanitized):
```text
Phase deltas are in turns and wrapped to shortest signed distance in [-0.5, 0.5) turns before normalization. All computations are fixed-point with deterministic rounding.
    -    theta_scale = 10^18 units per turn (uint64/int64 fixed-point)
    -    weight_scale = 10^6 units (int32 fixed-point rational weights)
    -    range: f_min = -2^30, f_max = 2^30 - 1
    -    scale: f_scale = 2^30 (maps normalized s?[-1,1] into code range)
    -    range: a_min = 0, a_max = 65535
    -    scale: a_scale = 65535 (maps u?[0,1] to full code)
Rounding rule (locked): "round half up" implemented in integer arithmetic; never use platform float rounding for canonical state.
J = E[ ?chi_band ] + E[ ?cont_band ] ? lambda_violation * E[ V ] ? lambda_clamp * E[ C ] ? lambda_budget * E[ B ]
    -    C is clamp pressure (phantom/aether "stability tax" aggregate)
S_i = E[ | ?J / ?x_i | ]  /  ( epsilon + E[ cost_i ] )
    -    cost_i = (pulse_sensitivity_i * compute_cost_proxy) + (memory_cost_proxy_i) + (instability_proxy_i)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.1 How we compute ?J/?x_i without floating ambiguity

Canonical source: EigenWareSpec_v51.md Section4.1.3.1 (surgical promotion).
Definition (deterministic finite-difference Jacobian magnitude proxy; fixed-point safe):
```text
We do not use symbolic differentiation. We use deterministic finite differences on the calibration data:

For each calibration event e with normalized delta components x_i(e), we estimate:

gain(e) = (?chi_band(e) + ?cont_band(e)) ? lambda_violationV(e) ? lambda_clampC(e)

Then, for each axis i, define a signed contribution proxy:

g_i(e) = gain(e) * sign(x_i(e)) * min( |x_i(e)|, x_cap )

This is the "directional usefulness" of axis i for improving J in that event. We then define:

S_i_num = mean_over_e( |g_i(e)| )

This is effectively a robust approximation to E[ |?J/?x_i| ] without requiring differentiable closed forms.
```

Normative rule: No symbolic differentiation; no floating-point calculus. Use only deterministic finite differences over calibration events.

#### A.11.6.2.2 How we compute cost_i from measurable terms

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L599-L619

Canonical equation extract (sanitized):
```text
cost_i = a0 * I_compute_i + a1 * I_memory_i + a2 * I_instability_i
I_compute_i = mean_over_e( |x_i(e)| )
I_memory_i = mean_over_e( bytes_touched_i(e) ) / bytes_touched_total
I_instability_i = corr_over_windows( |x_i|_window, band_thrashing_window )
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.3 Weight normalization and freezing

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L620-L645

Canonical equation extract (sanitized):
```text
4.1.3.3 Weight normalization and freezing
S_i = S_i_num / (epsilon + cost_i)
w_i = S_i / ?_j S_j
w_i_int = round_fixed(weight_scale * w_i)
    -    d8/d7 (aether/phantom): not because they "carry meaning," but because their clamp/instability correlation affects the penalty terms and thus regulates weight via cost_i
    -    spatial d1-d3: typically discounted because they contribute less to ?chi/?cont under language/context compared to phase/binding, and they often increase neighborhood expansion costs
SECTION 4.1.5 - Amplitude Derivation for P_SUM_CTX (u -> a_code, shown work)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.4 Inputs (all measurable, no invented signals)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L650-L660

Canonical equation extract (sanitized):
```text
For each candidate summary emission event e (one band -> one pulse candidate) we compute:
    -    clamp_band(e): deterministic clamp pressure proxy derived from phantom/aether aggregates (fixed-point [0,1])
    -    budget_state(e): envelope headroom scalar from read-path counters (utilization, dispatch backlog) mapped deterministically into [0,1] (1 = plenty of headroom)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.5 Risk-adjusted propagation score (derived)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L661-L686

Canonical equation extract (sanitized):
```text
p(e) = chi_band(e) * cont_band(e)
r(e) = max(clamp_band(e), viol_band(e))
We use max, not sum, because any one of these being high is sufficient to require conservative amplitude.
h(e) = budget_state(e)
u(e) = h(e) * clamp01( p(e) ? r(e) )
That's the "work shown" version: it's not "0.55 chi + 0.45 cont." It is the minimum necessary form that follows the logic:
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.6 Quantization to a_code (deterministic)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L688-L702

Canonical equation extract (sanitized):
```text
a_code = round_fixed( a_scale * u(e) )
    -    a_scale = 65535 (full uint16 span)
    -    rounding: fixed "round half up" integer rule
    -    clamp to [0, 65535]
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.7 Define measurable budget limits

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L703-L715

Canonical equation extract (sanitized):
```text
    -    P_hi = measured higher-tier pulse processing capacity per window (pulses/window)
    -    E_hi = maximum allowed harmonic expansion operations per window (ops/window), measured from calibration runs (or a conservative bound derived from kernel timing)
    -    N_emit = expected number of summary pulses emitted per window (from lower-tier band count and emission policy)
N_emit * E[harm_ops_per_pulse] <= E_hi
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.8 Choose k_max by envelope feasibility

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L716-L735

Canonical equation extract (sanitized):
```text
k_max = floor( E_hi / max(1, N_emit) )
thrash_rate(k) = mean_over_windows( thrash_indicators | k )
k_stable = max k where thrash_rate(k) <= T_thr
k_max = min(k_max, k_stable)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.9 Choose mode bucket size from required resolution

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L736-L761

Canonical equation extract (sanitized):
```text
codes_per_mode = floor(65536 / (k_max + 1))
mode_bucket_size = codes_per_mode
k = floor(a_code / mode_bucket_size)
strength = (a_code % mode_bucket_size) / mode_bucket_size
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.10 Candidate family (power-law decay)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L763-L778

Canonical equation extract (sanitized):
```text
We define harmonic weights for n = 1..k as:
Wn_raw(alpha) = 1 / (n ^ alpha)
Then normalize:
Wn_norm(alpha) = Wn_raw(alpha) / ?_{m=1..k} Wm_raw(alpha)
alpha ? [alpha_min, alpha_max] = [0.3, 2.0]
Lower alpha = broader coupling; higher alpha = tighter coupling.
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.11 Objective for alpha selection (same J, evaluated at tier summary)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L779-L789

Canonical equation extract (sanitized):
```text
J(alpha) = E[ ?chi_hi(alpha) ] + E[ ?cont_hi(alpha) ]
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.12 Deterministic search (no floats, no "gradient" needed)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L790-L801

Canonical equation extract (sanitized):
```text
A = {0.30, 0.35, 0.40, ..., 1.50}  (step size can be 0.05 or derived from required resolution)
alpha_ctx = argmax_{alpha ? A} J(alpha)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.13 Freezing and implementation

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L802-L823

Canonical equation extract (sanitized):
```text
Once alpha_ctx is chosen, we precompute a small LUT for n=1..k_max:
pow_lut[n] = round_fixed( (n ^ alpha_ctx) * pow_scale )
Wn_raw = pow_scale / pow_lut[n]
Normalize via integer sum and division with locked rounding.
No platform float pow is permitted in canonical execution; only LUT-based fixed-point.
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.14 What can be emitted (order-insensitive candidates)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L824-L836

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.15 Band eligibility score E_band (derived)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L837-L862

Canonical equation extract (sanitized):
```text
p = chi_band * cont_band
r = max(clamp_band, viol_band)
Define net effect magnitude (how much the band actually changed in this window), derived from the same spider normalization:
m = clamp01( |s_band| )  where s_band is the unquantized spider mixture scalar computed from aggregate deltas
h = budget_state (from higher-tier read-path envelope)
E_band = h * clamp01( p ? r ) * m
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.16 Emission threshold is derived from budget (no arbitrary cutoff)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L867-L883

Canonical equation extract (sanitized):
```text
    -    M = number of candidate bands in the sealed window
    -    P_hi = higher-tier pulse budget per window
    -    reserve = safety fraction derived from observed jitter (measured) so we don't saturate the window
N_target = floor( (1 ? reserve) * P_hi )
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.17 How many pulses per band (single vs multi-pulse)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L884-L904

Canonical equation extract (sanitized):
```text
    -    Compute two-centroid circular clustering on d5 using fixed initialization (deterministic seeds = lowest/highest phase members)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.18 causal_tag semantics (exact meaning, no ambiguity)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L905-L930

Canonical equation extract (sanitized):
```text
    -    0x0 = DRIFT (ordinary aggregated delta)
    -    0x1 = ACTIVATE (explicit resonance emphasis; same data fields, but interpreted as "context activation favored")
    -    0x2 = MODE_A (multi-modal emission cluster A)
    -    0x3 = MODE_B (multi-modal emission cluster B)
    -    0x4 = MERGE (topology update: two bands merged)
    -    0x5 = SPLIT (topology update: one band split)
    -    0x6 = BIND_UPDATE (explicit nexus-binding topology change)
    -    0x7 = CLAMP_ALERT (informational: high clamp; higher tier should damp coupling)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.19 Deterministic routing of topology without extra payload fields

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L937-L944

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.20 Remains causal under tier ordering

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L945-L954

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.21 Allowed telemetry signals (software-visible counters only)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L955-L966

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.22 Deterministic windowing (how envelope is sampled)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L967-L978

Canonical equation extract (sanitized):
```text
Envelope is computed per commit_state window. For each tier T and window tau:
This yields t_exec(T,tau).
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.23 Core envelope scalars (derived, shown work)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L979-L1004

Canonical equation extract (sanitized):
```text
sat_compute = clamp01( t_exec / t_budget )
sat_mem = clamp01( bw_used / bw_budget )
sat_mem = clamp01( bytes_moved / bytes_budget )
sat_queue = clamp01( (latency - latency_ref) / latency_span )
```

Calculation consolidations mapped to this canonical subsection:

Source calculation: Meta galactic calculations .md
Calc: Developers/calculations/Meta galactic calculations .md L1-L192

Order of operations (consolidated):
1. Compute baseline constants and context inputs (position, velocity, flux, strain, temperature).
2. Compute relativistic factors, effective constants, and time dilation coefficients.
3. Compute resonance/phase terms (Omega, theta, delta_phi_eff) and energy dispersion tracking.
4. Propagate these derived values forward as the order-of-operations for scalar derivations used by the application.

Equation block (sanitized, verbatim where possible):
```text

# **MGFT Velocity Calculation Framework**

The dataset provides a sequence of observables (distance, magnitudes, fluxes, radii, etc.). Each corresponds to one of the required parameters in the MGFT sequence. The calculations are always performed in order, without skipping, assuming, or combining values.

---

## **1\. Distance Conversion**

From megaparsecs to kilometers:

D\_{km} \= D\_{Mpc} \\cdot 3.085677581 \\times 10^{19}

---

## **2\. Light-Travel Time**

t\_{light} \= \\frac{D\_{km}}{c}

with c \= 3.0 \\times 10^5 \\ \\text{km/s}.

---

## **3\. Temporal Factor**

\\gamma\_t \= 1 \+ \\frac{t\_{light}}{T\_0}

where T\_0 is the MGFT cosmic time constant.

---

## **4\. Orbital Radius Conversion**

r\_{km} \= r\_{kpc} \\cdot 3.085677581 \\times 10^{16}

---

## **5\. Stellar Luminosity**

Using apparent magnitude and distance:

M\_B \= m\_B \- 5\\log\_{10}\\left(\\frac{D\_{Mpc} \\cdot 10^6}{10}\\right)

L\_\\ast \= 10^{-0.4(M\_B \- M\_{B,\\odot})} \\cdot L\_\\odot

---

## **6\. Stellar Mass**

M\_\\ast \= (M/L) \\cdot L\_\\ast

---

## **7\. Gas Mass**

If 21 cm flux is given:

M\_{HI} \= C \\cdot D\_{Mpc}^2 \\cdot S\_{21}

Optionally corrected:

M\_{gas} \= 1.36 \\cdot M\_{HI}

---

## **8\. Surface Densities**

\\Sigma\_\\ast \= \\frac{M\_\\ast}{\\pi r^2}, \\quad \\Sigma\_{gas} \= \\frac{M\_{gas}}{\\pi r^2}

---

## **9\. Pre-Interference Velocities (individually)**

V\_\\ast \= \\sqrt{\\frac{G M\_\\ast}{r}}

V\_{gas} \= \\sqrt{\\frac{G M\_{gas}}{r}}

---

## **10\. Interference Factor**

Taken directly from dataset or computed as function of densities and radius:

f\_{interference} \= f(\\Sigma\_\\ast, \\Sigma\_{gas}, r, D)

---

## **11\. Pre-Interface Velocity**

V\_{pre} \= \\frac{(V\_\\ast \+ V\_{gas}) \\cdot \\gamma\_t}{f\_{interference}}

---

## **12\. Observed Velocity (Prediction / Verification)**

V\_{obs} \= \\frac{V\_{pre} \\cdot f\_{interference}}{\\gamma\_t}

---

## **13\. Doppler Constant**

k\_{Doppler} \= \\frac{V\_{obs} \\cdot \\gamma\_t}{V\_{pre}}

---

# **Implementation Notes**

1. Always process sequentially: Distance -> Time -> Temporal factor -> Masses -> Densities -> Velocities -> Interference -> Observed prediction.

2. Never combine stellar and gas masses into a single "baryonic mass."

3. Keep full precision at every stage; only round at the final step.

4. Each galaxy is solved independently with its own constants.

# **MGFT Parameters and Their Theoretical Basis**

## **1\. Temporal Constant (T\_0)**

* Origin: Emerges from the embedded temporal dimension in MGFT, which governs how elapsed time affects intrinsic motion across cosmological distances.

* Definition: A scaling constant relating light-travel time to intrinsic galactic velocities.

* Role: Appears in the temporal factor

   \\gamma\_t \= 1 \+ \\frac{t\_{light}}{T\_0}

   ensuring velocities are corrected for distance-dependent temporal dilation.

* Difference from Standard Cosmology: Unlike the cosmological constant \\Lambda, which drives spacetime expansion, T\_0 is purely a temporal scaling parameter within MGFT.

---

## **2\. Temporal Factor (\\gamma\_t)**

* Origin: Arises from the interaction between light-travel time and the temporal constant.

* Definition:

   \\gamma\_t \= 1 \+ \\frac{t\_{light}}{T\_0}

* Role: Acts as a correction multiplier between observed and pre-interference velocities.

* Physical Meaning: Encodes how much a galaxy's intrinsic motion is "stretched" in the time dimension relative to our observation.

---

## **3\. Interference Factor (f\_{interference})**

* Origin: Results from MGFT's prediction that stellar and gas fields generate interference patterns in the galactic field structure.

* Definition: A dimensionless factor, unique to each galaxy, modulating intrinsic velocities.

* Role: Appears in the transformation between intrinsic and observed velocities:

   V\_{pre} \= \\frac{V\_{obs} \\cdot \\gamma\_t}{f\_{interference}}

* Physical Meaning: Quantifies how much structural interference suppresses or enhances measurable velocity.

---

## **4\. Pre-Interference Velocity (V\_{pre})**

* Origin: MGFT defines this as the galaxy's intrinsic orbital velocity before temporal and interference distortions.

* Definition:

   V\_{pre} \= \\frac{(V\_\\ast \+ V\_{gas}) \\cdot \\gamma\_t}{f\_{interference}}

* Role: A baseline velocity, used to recover the true galactic dynamics.

* Physical Meaning: Reflects the "clean" rotation speed that would be measured in absence of field distortions.

---

## **5\. Doppler Constant (k\_{Doppler})**

* Origin: Introduced by MGFT as a consistency check between observed and intrinsic velocities.

* Definition:

   k\_{Doppler} \= \\frac{V\_{obs} \\cdot \\gamma\_t}{V\_{pre}}

* Role: Serves as a diagnostic constant to verify MGFT's predictions galaxy by galaxy.

* Physical Meaning: Quantifies how Doppler shifting aligns with MGFT's temporal and interference corrections.
```

#### A.11.6.2.24 Single headroom scalar budget_state (derived)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1009-L1022

Canonical equation extract (sanitized):
```text
headroom = 1 ? max(sat_compute, sat_mem, sat_queue)
budget_state = clamp01(headroom)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.25 Reserve and jitter (derived, shown work)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1023-L1040

Canonical equation extract (sanitized):
```text
jitter = (percentile_95(t_exec) ? percentile_50(t_exec)) / t_budget
reserve = clamp01( jitter )
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.26 How budget_state influences emission and k_max (explicit)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1041-L1052

Canonical equation extract (sanitized):
```text
E_band includes the multiplier h = budget_state. Lower headroom reduces eligibility uniformly.
E_hi_live = floor( budget_state * E_hi_calibrated )
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

#### A.11.6.2.27 Determinism and replay constraints

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1053-L1072

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.7 Subsystem placement: crawler and encoder live inside the simulation

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1073-L1078

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.8 What "persistent resonance of webpage data" means

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1079-L1084

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.9 Ingestion pipeline as pulses, not files

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1085-L1090

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.10 Electronic signaling and execution: what is direct, what is derived

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1091-L1096

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.11 Crawler observation model (what it extracts, and why)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1097-L1106

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.12 Encoder mapping rules (explicit, no mysticism)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1107-L1121

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.13 Causality and closed-system safety for web ingestion

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1122-L1127

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.14 Budgeting, rate limits, and backpressure

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1128-L1133

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.15 Edge cases and explicit behaviors

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1134-L1141

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.16 What this enables

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1142-L1150

Canonical equation extract (sanitized):
```text
SECTION 5.11 - Concrete Mapping Spec: Raw Text -> ASCII Phase Injection -> Formation Deltas -> Resonance Collapse (Causality-Safe)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.16.1 Deterministic text segmentation (structural units)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1151-L1161

Canonical equation extract (sanitized):
```text
    -    Page -> blocks by DOM structure: title, headings, paragraphs, list items, captions
    -    Each block -> sentences by punctuation rules (versioned)
    -    Each sentence -> tokens by whitespace + punctuation splitting (versioned)
    -    Each token -> normalized surface form (lowercase; Unicode normalized; punctuation stripped per policy)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.16.2 Two-layer mapping: characters (phase) vs meaning (coherence)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1162-L1167

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.16.3 ASCII phase mapping (canonical)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1168-L1186

Canonical equation extract (sanitized):
```text
We define the canonical ASCII map on bytes in [0,255] after normalization (for UTF-8, text is converted to bytes first). Each byte b becomes a phase target in turns:
theta_byte_turns(b) = b / 256
?5 = wrap_turns( theta_byte_turns(b) ? theta_carrier )
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.16.4 Word formation: from bytes to a word-attractor candidate

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1187-L1212

Canonical equation extract (sanitized):
```text
    -    clamp terms ?7/?8 set conservatively based on novelty risk (derived from how often this staging band produced thrash in recent windows)
    -    Emit a pulse targeting the staging band eid with causal_tag = ACTIVATE or DRIFT depending on whether this byte is a boundary byte (see 5.11.5).
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.16.5 Boundary encoding (start/end anchors without storing letters)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1214-L1224

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.16.6 Sentence and paragraph context injection (coherence scaffolding)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1225-L1239

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.16.7 Promotion rule: when a staging band becomes a stable attractor

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1240-L1250

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.16.8 Retrieval rule: "persistence is retrieval," not permanence

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1251-L1260

Canonical equation extract (sanitized):
```text
    -    a_code derived from chi*continuum and envelope (broad coupling for contextual activation)
    -    profile = P_LANG_CTX or P_SUM_CTX depending on tier
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

### A.11.16.9 Closed-system causality guarantee

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1261-L1286

Canonical equation extract (sanitized):
```text
Temporal persistence    hub(t)->hub(t+1) bindings    reinforced by video motion coherence
text token event    optional    token phase residual / staging    context change rate    novelty risk/clamp    token?hub, token?token
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.17 Canonical container: one persistence format for all modalities

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1360-L1365

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.18 Record ledger: the only persisted primitives

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1366-L1383

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.19 Identifier system: stable, merge-safe, and replay-safe

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1384-L1395

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.20 Trust classes and strict course accreditation gate

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1396-L1409

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.21 Band types: modality-local bands and persistent cross-modal scene bands

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1410-L1430

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.22 Segment maps: how every artifact is broken into stable sequences

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1431-L1442

Canonical equation extract (sanitized):
```text
Text segment map: blocks -> sentences -> tokens, with byte spans into normalized text.
Code segment map: files -> AST nodes -> symbols/dependencies, with stable node addresses.
Image segment map: tiles (row, col) -> scan order within tile; optional edge map index.
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.23 File class encoding: web pages and text documents

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1443-L1448

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.24 File class encoding: PDFs, LaTeX, BibTeX, and scientific material

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1449-L1454

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.25 File class encoding: source code, specs, and software engineering assets

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1455-L1460

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.26 File class encoding: structured data (JSON/YAML/TOML/CSV)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1461-L1464

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.27 File class encoding: images (2D) and latent 3D (headless v1)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1465-L1470

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.28 File class encoding: audio (pitch identity and event identity)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1471-L1480

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.29 File class encoding: video (motion motifs and synchronized scenes)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1481-L1484

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.30 Extractor registry: versioning and normalization rules

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1485-L1490

Canonical equation extract (sanitized):
```text
6.14 Extractor registry: versioning and normalization rules
A minimal extractor registry entry includes: extractor_id, supported_mime, normalization_rules_digest, segmentation_rules_digest, and profile_defaults (which profile_id to use for that extractor's streams).
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.31 Profile registry: which spider profiles are legal per extractor

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1491-L1494

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.32 Cross-modal alignment: how streams bind cleanly in one file

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1495-L1498

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.33 Creativity: how encoding becomes a creative engine for users

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1499-L1506

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.34 Creativity safety and quality: constraint-first synthesis

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1507-L1512

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.35 Creativity interfaces: how users "steer" the engine

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1513-L1516

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.36 Minimal implementation target for Copilot

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1517-L1535

Canonical equation extract (sanitized):
```text
- The extractor registry with strict versioning and normalization coord_sig.
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.37 What we mean by "confirmed", "documented", and "common"

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1536-L1545

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.38 Text and knowledge corpora (core language + encyclopedic structure)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1546-L1564

Canonical equation extract (sanitized):
```text
- stable entity anchors (names, concepts, equations as normalized tokens)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.39 Code corpora (programming languages + repositories + build logic)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1565-L1579

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.40 Image corpora (2D constraints, geometry priors, and artifact detection)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1580-L1594

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.41 Audio corpora (phonetics, timbre, events, and note-like identification)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1595-L1610

Canonical equation extract (sanitized):
```text
- time-aligned segmentation (frames -> events -> phrases) stored as deterministic substreams
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.42 Video corpora (motion, causality, and 3D-constraint intuition)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1611-L1625

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.43 3D geometry and physics priors (constraint libraries)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1626-L1639

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.44 Open courseware and accreditation tagging (structured learning sequences)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1640-L1654

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.45 Vendor-style "mixture" packs (what the ecosystem converges on)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1655-L1663

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.46 A short "science and engineering feed" list (optional)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1664-L1672

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.47 Domain pack table (starter registry entries)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1673-L1684

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.48 Section 7 integration notes

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1685-L1698

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.49 Registry layer (no ad-hoc choices)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1699-L1710

Canonical equation extract (sanitized):
```text
All behavioral choices in the crawler+encoder pipeline must resolve through explicit registries. No module is allowed to guess an extractor, a normalization rule, a profile, a trust class, or a band type.
- ExtractorRegistry: extractor_id -> supported_mime, normalization_rules_digest, segmentation_rules_digest, deterministic ordering rules, fallback_extractor_ids
- ProfileRegistry: profile_id -> spider profile definition, harmonic policy, axis clamp policy, allowed causal_tag set, allowed extractor_id set
- DatasetDomainRegistry: domain_id -> acquisition mode, trust_class defaults, scheduling policy, sampling policy, provenance rules
- BandTypeRegistry: band_type -> promotion rules, merge/split hysteresis rules, legal binding kinds, persistence rules (including SCENE_*)
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.50 Deterministic replay contract (strict mode)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1711-L1716

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.51 Budget + backpressure subsystem (enforced envelope)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1717-L1720

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.52 Dedup + near-dup filter (mandatory)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1721-L1724

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.53 Provenance + license tagging (first-class metadata)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1725-L1728

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.54 Extractor robustness (fail-closed)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1729-L1732

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.55 A/V time alignment repair (deterministic correction)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1733-L1736

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.56 Band thrash guard (merge/split hysteresis and quarantines)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1737-L1740

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.57 Copilot acceptance checklist (testable obligations)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1741-L1755

Canonical equation extract (sanitized):
```text
- extractor_id changes imply normalization_rules_digest and/or segmentation_rules_digest changes
- No silent exceptions; all errors log with artifact_id/extractor_id and exc_info=True
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.11.58 Single-file contract test harness (explained, explicit, and complete)

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L1756-L1781

Canonical equation extract (sanitized):
```text
1) a JSON dump of header with sort_keys=True
  extractor_id, supported_mime_re, normalization_rules_digest, segmentation_rules_digest, fallback_extractor_ids
The harness includes a toy normalizer and segmenter that mimic "extractor determinism" rules without needing real HTML/PDF parsers. These functions are proxies that validate the determinism contract.
- normalize_text(raw): line ending normalize; collapse runs of spaces; collapse 3+ newlines to 2; strip ends.
- segment_text_blocks(norm): split on blank lines (double newline), trim, remove empties.
- Same raw fixture -> same norm string
- Same norm string -> same segments list
estimate_alignment_offset(caption_tokens, audio_events) -> offset_k
- A fixture with one leading "noise" event in audio yields offset_k == 1.
- First change at tau_q = T is allowed
- Change is allowed again at tau_q >= T + cooldown
1) normalizes and segments FIXTURE_HTML_TEXT
   - a_code derived from segment length mod 256 (clamped)
2) Deterministic normalization + segmentation for at least one text path
E_i(t) = (phi_i(t), A_i(t))
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

# A.12 Hardware alternatives: qubit and spintronics substrates

This section is OPTIONAL. It does not change the canonical algorithm. It only binds the existing
pulse-phase primitives (phase anchors, delta/ratio coupling, ring orientation updates, and
coherence-gated derived time deltas) to concrete hardware substrates.

Design rule:
- The canonical engine stays substrate-agnostic.
- A hardware substrate is valid only if it can expose (directly or via inference) the canonical observables:
  A(t), f(t), V(t), I(t), and a coherence gate C(t), at impulse boundaries t_k and at pulse-delta time
  t_k + tau_delta.

## A.12.1 Shared observables and impulse framing

Impulse boundaries: t_k for k = 0..K-1
Fixed pulse-delta sampling: tau_delta >= 0

Define sampling time:
t_k_delta = t_k + tau_delta

Canonical phase anchor (hardware-provided or inferred):
theta_k = phase_anchor_from_pulse(A(t_k_delta), V(t_k_delta), I(t_k_delta), f(t_k_delta))

Within-interval phase evolution is expressed in wrapped turns or wrapped radians, per canonical conventions.
All coupling terms MUST be expressed in quantized delta/ratio space:

dlnA_k = ln(A_k / A_{k-1})
dlnf_k = ln(f_k / f_{k-1})
dlnV_k = ln(|V_k| / |V_{k-1}|)
dlnI_k = ln(|I_k| / |I_{k-1}|)

Second-order deltas are permitted when explicitly enabled (deterministic, bounded):
ddlnA_k = dlnA_k - dlnA_{k-1}
ddlnf_k = dlnf_k - dlnf_{k-1}
ddlnV_k = dlnV_k - dlnV_{k-1}
ddlnI_k = dlnI_k - dlnI_{k-1}

Delta-spectra are permitted only as a bounded feature set (no unbounded FFT payloads):
Spec_dlnA_k = spectrum_features(dlnA_{k-w:k})
Spec_dlnf_k = spectrum_features(dlnf_{k-w:k})
... (same pattern for V and I)

Coherence gate:
C_k in [0, 1] must be supplied by the substrate measurement model. If C_k < C_min, derived time deltas
and phase-identity projections must fail-closed (no projection).

## A.12.2 Qubit substrate binding (spin qubits, defect spins, superconducting qubits)

This binding applies when the substrate can be modeled (locally) as an effective two-level system with a
driven Hamiltonian. The canonical pulse primitives map into Hamiltonian parameters.

Two-level driven Hamiltonian (minimal form):
H(t) = (hbar / 2) * (Delta(t) * sigma_z + Omega_x(t) * sigma_x + Omega_y(t) * sigma_y)

Common drive binding (hardware-specific proportionality constants):
Omega_x(t) = k_drive_x * V_env(t)
Omega_y(t) = k_drive_y * V_env(t)

Detuning binding:
Delta(t) = 2*pi*(f_env(t) - f0) + delta_env(t)

Where:
- V_env(t) is the delivered voltage at the device (after line transfer function), not the AWG setpoint.
- f_env(t) is the delivered instantaneous carrier frequency estimate (after distortion).
- delta_env(t) captures slowly varying shifts (temperature, charge noise, strain) expressed as a bounded term.

Pulse-area and axis:
theta_pulse = integral_{t_k}^{t_{k+1}} sqrt(Omega_x(t)^2 + Omega_y(t)^2) dt
phi_axis = atan2(Omega_y, Omega_x)

Canonical phase anchor interpretation:
- theta_k is the reference phase for the local rotating frame at t_k_delta.
- phi_axis is the rotation axis phase used by the control pulse.
If hardware provides IQ (in-phase/quadrature), theta_k is taken from the measured complex drive phase
at t_k_delta (or equivalently from phi_axis at that time).

Between-impulse evolution (piecewise constant approximation within each dt):
U(dt) = exp(-i * H * dt / hbar)

Derived time delta (coherence-gated):
dt_star_k = dphi_coh_k / omega_eff_k
omega_eff_k = sqrt(Delta_k^2 + Omega_k^2)   where Omega_k = sqrt(Omega_x_k^2 + Omega_y_k^2)

This matches the canonical rule: time deltas are outputs derived from coherent phase offsets normalized
by an amplitude/density-controlled phase-advance rate.

Crosstalk as compute (multi-body extension):
When the substrate exhibits inevitable coupling (intended or parasitic), model it as additional terms:
H_cpl = sum_{i<j} J_ij(t) * (sigma_z_i * sigma_z_j) + ...
The canonical delta-stack (dlnA, dlnf, dlnV, dlnI, and bounded delta-spectra) is used to infer and
compensate J_ij drift indirectly via phase-anchor and ring-orientation updates, without requiring explicit
per-qubit tracking.

## A.12.3 Spintronics substrate binding (spin torque oscillators, magnonic media, MTJ arrays)

This binding applies when the substrate state is carried by magnetization dynamics and read out through
voltage/current-dependent magnetoresistance or inductive pickup.

Magnetization state:
m(t) is a unit vector (m_x, m_y, m_z)

Landau-Lifshitz-Gilbert with drive torques (compact form):
dm/dt = -gamma_e * (m x H_eff) + alpha_g * (m x dm/dt) + tau_drive(V(t), I(t))

Where:
- gamma_e is the effective gyromagnetic ratio (substrate-calibrated).
- alpha_g is Gilbert damping.
- H_eff includes external field, anisotropy, exchange, and coupling fields (including crosstalk fields).
- tau_drive encodes spin-transfer torque (STT) and/or spin-orbit torque (SOT) components.

Observable readout (MTJ example):
R(t) = R0 + deltaR * dot(m(t), p_hat)
V(t) = I(t) * R(t)

Define an oscillator phase and amplitude from transverse magnetization:
z_m(t) = m_x(t) + i * m_y(t)
A_m(t) = |z_m(t)|
phi_m(t) = arg(z_m(t))

Instantaneous frequency:
f_m(t) = (1 / (2*pi)) * dphi_m/dt

Canonical mapping:
- A(t) can be A_m(t) or an envelope inferred from V(t) / I(t) and R(t), depending on the sensor path.
- f(t) can be f_m(t) (precession/oscillation frequency) or an inferred carrier frequency.
- theta_k is taken as phi_m(t_k_delta) (or an equivalent phase derived from the measured complex response).
- Crosstalk appears as changes in H_eff and tau_drive, which are observable as shifts in f_m(t) and in the
  delta-stack spectra; the canonical ring orientation update uses these shifts as signal, not as noise.

This substrate naturally supports "multiple phases in one signal" because V(t) and R(t) contain carrier
phase, envelope amplitude, and mixing products from coupled modes. The canonical compression rule applies:
store only quantized deltas/ratios and sparse anchors; reconstruct deterministically by wrapped cumulative sums.

## A.12.4 Order-of-operations alignment to canonical engine

Hardware-binding order MUST match the canonical pipeline:

1) Acquire raw pulse observables around each impulse boundary:
   A(t), f(t), V(t), I(t) sampled at t_k_delta.

2) Quantize into canonical primitives:
   - theta_k anchor
   - dlnA_k, dlnf_k, dlnV_k, dlnI_k
   - optional second-order deltas and bounded delta-spectra features

3) Apply coherence gate:
   If C_k < C_min -> fail-closed (no projection; store quarantine markers).

4) Apply ring orientation update (PAF) derived purely from amplitude deltas:
   PAF_k = Q( sum g(dlnA_{k,*}) )
   theta_start(next_ring) = wrap(theta_end(current_ring) + PAF_k)

5) Evolve phase within interval using coupled phase clock:
   theta_{k+1} = wrap(theta_k + base_step_k + kA*dlnA_k + kf*dlnf_k + kV*dlnV_k + kI*dlnI_k)

6) Decode identity/trajectory only from relative (wrapped) differences and ratio features.

## A.12.5 Hardware-facing tests (minimum obligations)

These tests do not assume a specific substrate; they validate the binding consistency:

- Anchor determinism: repeating the same pulse program in a stable environment yields the same theta_k and
  delta-stack bins within tolerance.

- Coherence gating: when coherence drops below C_min, projections are suppressed and no "identity" is emitted.

- Latency compensation stability: with an injected known delay perturbation, dt_star_k changes consistently with
  dphi_coh_k / omega_eff_k, and the ring-orientation update returns the system to the expected phase frame.

- Crosstalk-as-signal: under controlled coupling variation (e.g., changing bias field or coupling current),
  the delta-spectra features change measurably and the canonical manifold embedding remains continuous (no
  discontinuous jumps without quarantine markers).

## A.12.6 Disambiguation: pulse amplitude/current vs gate amplitude tensor (integer interaction gate)

Purpose:
This section pins down naming and binding so that hardware-drive terms (including amperage/current)
remain in transport/effective-constant derivations, while the amplitude gate tensor remains purely
an interaction weight on already-computed ring deltas.

Two distinct quantities (do not conflate):
1) A_pulse(t): pulse amplitude / envelope magnitude sampled from the hardware path.
   - Used for anchor extraction and any allowed phase-clock coupling terms.
2) A_gate_q63(i,j): signed Q63 gate weight for lane interactions (the amplitude gate tensor).
   - Used ONLY for coherence/resonance/commit_state gating on phase deltas.

Likewise, define I_pulse(t) as pulse current/amperage (or an inferred proxy). I_pulse is a drive input.

Canonical split:
- Transport (drive) update uses pulse observables and environment factors:
    delta_theta_transport_k = F_transport(I_pulse_k, f_pulse_k, v_k, flux_factor_k, strain_factor_k, ...)
    theta_{k+1} = wrap(theta_k + delta_theta_transport_k + (allowed couplings from pulse deltas))
  where wrap(.) is modulo 2^64 and theta is stored in the integer ring.

- Interaction gate uses only already-computed ring deltas and A_gate_q63:
    dtheta_ij = (int64)(theta_i - theta_j)          // minimal arc via two's-complement cast
    dtheta_gated_ij = q63_mul_i64(dtheta_ij, A_gate_q63(i,j))
    dispersion = sum over pairs |dtheta_gated_ij|
    R_i64 = (max_dispersion - dispersion) / max_dispersion   // integer normalization

Notes:
- A_gate_q63 MUST NOT be used inside F_transport(.) or any effective-constant function.
- I_pulse MUST NOT be used inside q63_mul_i64(.) or any A_gate update rule.
- If the prose uses the word 'amplitude' without a suffix, interpret it as A_pulse for hardware-binding
  and as A_gate only when explicitly referring to coherence/resonance/commit_state gating.

## A.12.7 Energy dispersion spawn localization and import inversion (conservation operators)

Purpose:
Define the missing operators required to (1) localize spawn from energy dispersion and
(2) import (construct) an object by debiting conserved mass-energy from manifold reservoirs.
No free creation is permitted. All terms are integer or fixed-point and use only
word-size and cardinality-derived normalization plus canonical effective constants.

Canonical separation:
- Relativity / Doppler mapping affects ONLY the transport step (delta_theta_transport).
- The amplitude gate tensor A_gate affects ONLY interaction weighting (coherence/resonance/commit_state).
- Spawn/import operators do not rescale theta; they transfer conserved quantities via reservoirs
  and deterministic ring-impulse bookkeeping.

State per lane i:
- theta_u64[i]       : uint64 ring phase (wrap by overflow)
- E_res_q32_32[i]    : signed energy reservoir (Q32.32)
- E_floor_q32_32[i]  : non-negative floor (machine-updated; no literals)
- N(i)               : deterministic neighborhood iterator
- doppler_ratio_q32_32[i] derived by effective_constants(v, flux_factor, strain_factor, ...)

Helper operators (fixed-point / integer):
- abs_i64(x) = (x < 0) ? -x : x
- abs_q32_32(x) = (x < 0) ? -x : x
- q32_32_from_i64(x) = (int64)x << 32
- mul_q32_32(a,b) = ( (____int128)a * (____int128)b ) >> 32
- div_q32_32(a,b) = ( (____int128)a << 32 ) / b        // b != 0, deterministic
- q63_mul_i64(dtheta_i64, a_gate_q63) = ( (____int128)dtheta_i64 * (____int128)a_gate_q63 ) >> 63

(1) Dispersion proxy from gated deltas:

For each lane i:
dtheta_ij_i64 = (int64)(theta_u64[i] - theta_u64[j])           // two's-complement cast
dtheta_gated_ij_i64 = q63_mul_i64(dtheta_ij_i64, A_gate_q63(i,j))

D_phase_i64[i] = sum_{j in N(i)} abs_i64(dtheta_gated_ij_i64)

Define a transport-phase activity term (from Phase 3 transport):
omega_eff_q32_32[i] = mul_q32_32(omega_base_q32_32[i], doppler_ratio_q32_32[i])
dphi_q32_32[i] = mul_q32_32(omega_eff_q32_32[i], dt_tick_q32_32)

Define the energy-dispersion driver:
D_energy_q32_32[i] = abs_q32_32(dphi_q32_32[i]) +
                     q32_32_from_i64(D_phase_i64[i] >> log2(cardinality(N(i)) + 1))

(2) Available energy and spawn pressure:

E_free_q32_32[i] = max(0, E_res_q32_32[i] - E_floor_q32_32[i])

P_spawn_q32_32[i] = mul_q32_32(E_free_q32_32[i], abs_q32_32(D_energy_q32_32[i]))

Spawn site selection:
i0 = argmax_i P_spawn_q32_32[i]    // deterministic tie-break: smallest i
Spawn evaluation cadence:
if (commit_counter % lane_count) == 0 then evaluate spawn, else no spawn evaluation
Spawn is denied if E_free_q32_32[i0] == 0.

(3) Import packet and conserved energy requirement:

Import packet fields (Phase 0 only):
- obj_id_u64
- m_obj_q32_32
- geomsig9_u64x9
- phase_seed_u64
- anchor_count_u32

Energy required (effective constants only):
E_req_q32_32 = mul_q32_32(m_obj_q32_32, mul_q32_32(c_eff_q32_32, c_eff_q32_32))

Import legality:
E_req_q32_32 <= sum_i E_free_q32_32[i]   else deny (fail-closed)

Anchor selection for object construction:
S = BFS_expand(i0, anchor_count_u32)     // deterministic topology expansion

(4) Debit distribution and reservoir update:

Derived per-anchor weights (no literals):
w_u64[k] = max(1, abs_i64(A_gate_q63(k,k)) >> log2(lane_count))
W_u64 = sum_{k in S} w_u64[k]

debit_q32_32[k] = div_q32_32( mul_q32_32(E_req_q32_32, q32_32_from_u64(w_u64[k])),
                             q32_32_from_u64(W_u64) )

Apply:
E_res_q32_32[k] = E_res_q32_32[k] - debit_q32_32[k]   for k in S

(5) Construction: seed object phase from manifold anchors:

theta_obj_u64[k] = theta_u64[k] XOR phase_seed_u64 XOR geomsig9_u64x9

Optional energy imprint using effective constants:
E_quant_q32_32 = mul_q32_32(h_eff_q32_32, omega_ref_q32_32)
delta_theta_from_energy_u64(E_q32_32) = floor( ( (____int128)E_q32_32 << 64 ) / E_quant_q32_32 )

theta_obj_u64[k] = theta_obj_u64[k] + delta_theta_from_energy_u64(debit_q32_32[k])   // wrap by overflow

(6) Backreaction / recoil (ring impulse bookkeeping):

impulse_u64 = delta_theta_from_energy_u64(E_req_q32_32)
R1 = neighbors(S) excluding S, deterministically enumerated

Distribute impulse_u64 across R1 with the same weights and apply:
theta_u64[r] = theta_u64[r] - distributed_impulse_u64[r]       // wrap by overflow

History record (append-only, Phase 5 commit_state):
tick_u64, i0_u32, P_spawnsig9_u64x9, E_reqsig9_u64x9, anchor_count_u32, denial_code_u32

## A.12.8 Anchor-encoded equation pages (eq_pages) and UE5 control surface bridge (ToolsTab hooks)

This section binds the missing runtime encoding layer so that all executable equation families are represented
as anchor-bound eq_pages (the same "page" mechanism used to encode 9D manifold behavior), and so that an
external control surface (UE5 Editor ToolsTab) can drive observer/projection behavior without touching lattice state.

This section introduces no new physics. It defines encoding, ordering, and I/O legality.

### A.12.8.1 Eq_page definition (immutable microcode bound through anchors)

Each anchor MAY reference one eq_page (microcode page) that is evaluated deterministically in integer/fixed-point.

Per-anchor binding fields (immutable during tick):
- eq_page_id_u32
- eq_pagesig9_u64x9
- eq_param_lane_u32

Integrity requirement:
- If eq_page_id_u32 != 0 then eq_pagesig9_u64x9 MUST match the content coord_sig of the referenced eq_page.
- If the coord_sig does not match, evaluation is skipped and a denial_code is appended to history (deterministic).

Instruction encoding (one word per instruction):
- inst_u64 = [ opcode_u8 | dst_u8 | src0_u8 | src1_u8 | imm_u32 ]

Operand sources (allowed):
- Anchor: coord9_q63[9], anchor_id_u64, harmonic fingerprint fields
- State: theta_u64, dtheta_transport_u64, R_integer_i64, dispersion_proxy_i64, E_res_q32_32
- Params: param_q32_32[k] from the bound eq_param_lane_u32
- Derived: lane_count, log2(cardinality(*)), 2_pow_64

Forbidden sources:
- floats, trig, random numbers, wall-clock time, user-provided literal constants

Minimum opcode set (sufficient for projection/control/readout):
- OP_LOAD_ANCHOR_COORD_Q63(d)
- OP_LOAD_STATE_THETA_U64
- OP_LOAD_STATE_DTHETA_U64
- OP_LOAD_STATE_E_RES_Q32_32
- OP_LOAD_PARAM_Q32_32(k)
- OP_I64_ADD, OP_I64_SUB
- OP_Q32_32_MUL (128-bit intermediate, >> 32)
- OP_Q63_MUL (128-bit intermediate, >> 63)
- OP_SHR_LOG2_CARD (shift by log2(cardinality) derived in-kernel)
- OP_ABS_I64
- OP_STORE_ARTIFACT_KV(key_id_u32)

### A.12.8.2 Where eq_pages execute (order-of-operations binding)

Eq_pages are evaluated only in Phase 6 (artifact projection), after coherence is computed.

Bound order:
Phase 0: latch intents (UE packets) into Phase 0 input buffer (device)
Phase 1: apply binding intents (update eq_page_id_u32 / eq_param_lane_u32 with coord_sig checks)
Phase 2: transport (compute dtheta_transport_u64)
Phase 5: coherence gate (compute R_integer, dispersion proxy from gated deltas)
Phase 6: eval eq_pages + write dict-map artifact frame
Phase 7: commit_state/history

### A.12.8.3 UE5 control surface contract (ToolsTab -> Phase 0 intent packets)

UE5 SHALL interact only via Phase 0 intent packets and Phase 6 artifact frames.

Intent packets (fixed-size):
ObserverIntentPacket:
- intent_kind_u32
- anchor_id_u64
- manifold_coord9_q32_32[9]
- projection_mode_u32
- slice_axes_u32[3]
- slice_hold_q32_32[6]
- blend_ms_u32

EquationBindIntentPacket:
- anchor_id_u64
- eq_page_id_u32
- eq_pagesig9_u64x9
- eq_param_lane_u32

LabIntentPacket:
- lab_intent_kind_u32
- energy_budget_q32_32
- anchor_count_u32
- geomsig9_u64x9
- phase_seed_u64

Artifact keys (dict-map only):
- KEY_VIEWPORT_POSE
- KEY_PROJECTION_POINTS
- KEY_DEBUG_SCALARS
- KEY_SELECTION_STATE

### A.12.8.4 Image-to-location (viewport jump) as deterministic frame coord_sig lookup

Editor-side determinism rule:
- For each rendered viewport frame, compute a 64-bit dSig (dSig64) and store:
  {dsig9_u64x9, observer_state_snapshot, viewport_pose, tick_u64} in a ring buffer.
- For an input image, compute dsig9_u64x9(image) and select argmin Hamming distance entry (no thresholds).
- Emit ObserverIntentPacket with the stored observer_state_snapshot to request the same viewport.

This mechanism provides image-based viewport relocation without permitting the image to directly mutate lattice state.

# A.13 ==========================

# A.14 INVARIANT -- PHASE TRANSPORT-DERIVED OPCODE CLASSIFICATION

# A.15 ==========================

All discrete operation classifications (opcodes, task labels, I/O modes)
SHALL be derived strictly from the phase transport term.

No opcode may introduce an independent control degree of freedom.
Opcode identity is a categorical projection of the transport vector
(direction, magnitude, and coherence-permitted mode) already computed
by the phase evolution equations.

Formally:
phase_transport_term -> transport_mode -> opcode_label

This invariant introduces no new physics and no additional parameters.

# A.16 SUBSTRATE ANCHORS -- EXTERNAL FIELD INGRESS & EGRESS (NORMATIVE)

# A.17 External Field Ingress Anchor (EFI)

The substrate SHALL expose a canonical External Field Ingress Anchor.

This anchor accepts externally originating signal fields encoded directly
as phase-aligned structures, including but not limited to:
- phase offsets and deltas
- amplitude envelopes
- correlation tensors
- temporal coherence markers
- optional provenance coord_sig (non-causal)

The substrate SHALL NOT perform protocol negotiation, symbolic parsing,
or imperative retrieval. All incoming data is treated as already-structured
field input suitable for phase transport.

Crawler, parser, or acquisition software--if present--SHALL be considered
non-authoritative suppliers of compatible encodings only.

# A.18 External Field Egress Anchor (EFE)

The substrate SHALL expose a canonical External Field Egress Anchor.

This anchor emits phase-aligned signal fields derived from internal
phase transport and classification. Emitted fields may be bound by
higher layers to:
- user interface controls
- file or code generation
- actuator or device outputs

The egress anchor introduces no new control degrees of freedom and
operates strictly downstream of phase evolution.


=== ADDITION: Phase-Orbital Compression Limits ===

This section is additive and does not alter prior equations.

Let I_min = phase_orbital_displacement_unit_mA
Let I_max = pulse_current_max_mA

Define lattice tension coefficient:

lattice_tension_coeff(t) = clamp01(I(t) / I_max)

Define maximum allowable amperage gradient:

dI_max/dt = (I_max - I(t-1)) / tick_dt

Orbital phase displacement per tick SHALL be quantized to integer multiples of I_min.

The maximum information compressible into a single phase node is bounded by:

max_phase_quanta_per_node ~ (I_max / I_min) * T_env

where T_env is the temporal envelope width in ticks.

Violation of this bound SHALL cause orbital instability and forced decomposition.


=== ADDITION: Phase-Code Dispatcher and Operator Realization -- NORMATIVE REPLACEMENT (Chat 2026-02-11) ===

This section replaces any earlier descriptive/ambiguous dispatcher/operator wording. It pins the executable operators, math, and invariants.

0) Canonical fixed-point type contract (refer to Spec S39.1 if present; otherwise this block is authoritative)
Types:
- u64_phase (uint64 wrap ring), i64_delta (int64 minimal arc), q32_32 (int64), q63_gate (int64), u32_id, u64_sig
Core ops (host/device identical):
- mul_q32_32(a_q32_32,b_q32_32) = (int128(a)*int128(b)) >> 32, saturating
- q63_mul(a_q63,b_q63)         = (int128(a)*int128(b)) >> 63, saturating
- wrap_add_u64(x_u64,dx_u64)   = x + dx  (native uint64 wrap)
- phase_delta_i64(a_u64,b_u64) = (int64)(a - b)  (two's complement minimal arc)
- q32_32_phase_to_u64(dphi_q32_32) mapping: u64_phase = (uint64)(dphi_q32_32) << 32  (exact; single rule)

1) Global phase-code dispatcher (meta-anchor) (immutable carrier rule)
Carrier anchor manifold:
- C = { A_i | i in N }, with dA/dt = 0
Carrier state (latched):
- carrier_phase_u64, carrier_omega_q32_32, tick_dt_q32_32
Carrier evolution:
- carrier_phase_u64(t+1) = wrap_add_u64(carrier_phase_u64(t), q32_32_phase_to_u64(mul_q32_32(carrier_omega_q32_32, tick_dt_q32_32)))

Dispatcher headroom (required):
- remaining_tension_headroom_mA_q32_32(t) = pulse_current_max_mA_q32_32 - pulse_current_total_mA_q32_32(t)
- gradient_headroom_mA_q32_32(t) = div_q32_32(remaining_tension_headroom_mA_q32_32(t), tick_dt_q32_32)

2) Phase -> current projection operator (bounded physical mapping)
Let Delta_phi_q be the quantized phase displacement in q32_32.
Define:
- phase_to_current_mA_q32_32(Delta_phi_q32_32) = mul_q32_32(k_phase_current_q32_32, Delta_phi_q32_32)
Where:
- k_phase_current_q32_32 = div_q32_32(pulse_current_max_mA_q32_32, phase_max_displacement_q32_32)
Hard rule:
- All phase displacement MUST map through phase_to_current_mA_q32_32 before comparing against headroom.

3) Quantization operator (temporal compression law; non-bypassable)
Define:
- quantize_q32_32(x_q32_32, quantum_q32_32) = round_half_even(x_q32_32 / quantum_q32_32) * quantum_q32_32
Where:
- quantum_q32_32 := phase_orbital_displacement_unit_mA_q32_32 (the measurable minimum)
No operator may bypass quantize_q32_32.

4) Maximum node information bound (capacity expression)
Let:
- I_min := phase_orbital_displacement_unit_mA_q32_32
- I_max := pulse_current_max_mA_q32_32
- T_env := temporal_envelope_ticks_u64
Then:
- max_phase_quanta_per_node = floor(I_max / I_min) * T_env
This is a hard capacity bound.

5) Ancilla state definition (explicit; only mutable state holders)
Ancilla particles are the only mutable runtime state holders. Anchors contain no mutable fields.

Packed struct (canonical; fixed-point):
struct ancilla_particle {
    q32_32 current_mA_q32_32;
    q32_32 delta_I_mA_q32_32;
    q32_32 delta_I_prev_mA_q32_32;
    u64_phase phase_offset_u64;
    q32_32 convergence_metric_q32_32;
};

6) GPU kernel update contract (anchors read-only)
For each GPU kernel domain_k:
kernel_update(domain_k):
    for each ancilla_i in domain_k:
        delta_I_mA_q32_32 := phase_to_current_mA_q32_32(quantized_delta_phi_i_q32_32)
        enforce dispatcher constraints (remaining_tension_headroom, gradient_headroom)
        if valid: commit_state
        else: refuse convergence (emit denial code + causal tag)
Anchors are read-only in kernel space.

7) Universal operator execution template (canonical; no exceptions)
For any operator O_k:
phi_prime_q32_32 = O_k_phase_map(phi_q32_32, params_k)
Delta_phi_q32_32 = quantize_q32_32(phi_prime_q32_32 - phi_q32_32, phase_orbital_displacement_unit_mA_q32_32)
Delta_I_mA_q32_32 = phase_to_current_mA_q32_32(Delta_phi_q32_32)

If:
abs(Delta_I_mA_q32_32) <= remaining_tension_headroom_mA_q32_32(t)
AND
abs(Delta_I_mA_q32_32 - delta_I_prev_mA_q32_32) <= gradient_headroom_mA_q32_32(t)
Then:
phi_q32_32(t+1) = phi_q32_32(t) + Delta_phi_q32_32
Else:
phi_q32_32(t+1) = phi_q32_32(t)

8) Reverse phase transport (explicit operator; same constraints)
If reverse transport exists:
R_phase_map(phi) = phi_q32_32 - transport_gradient_q32_32(phi_q32_32)
Executed under the same dispatcher constraints as forward transport. No exception.

9) Anchor class invariants (hard rules)
- Operator anchors are immutable after boot freeze.
- Operator anchors contain only:
  (a) phase map definition, (b) quantization rule binding, (c) dispatcher binding.
- Operator anchors SHALL NOT store:
  current_mA, gradients, temporal deltas, or tick-local mutable scalars.
- All mutable runtime values reside in ancilla.

10) Carrier lock invariant (global phase-lock)
For all anchors A_i:
| local_phase_velocity_q32_32 - carrier_phase_velocity_q32_32 | <= carrier_lock_tolerance_q32_32
Violation routes to refusal/quarantine; no silent drift.

11) Phase collapse condition (explicit; no silent failure)
If:
required_current_mA_q32_32 > pulse_current_max_mA_q32_32
OR
abs(Delta_I_mA_q32_32) > remaining_tension_headroom_mA_q32_32(t)
Then:
convergence=false; trigger node_fission OR operator_refusal (as policy); emit denial code and causal tag.

12) Hydration / projection operator formalization (internal projection)
binary_projection(phi_region_q32_32[]) = encode( quantize_q32_32(phi_region_q32_32[], phase_orbital_displacement_unit_mA_q32_32) )
phi_region_q32_32[] = decode(binary_buffer)
Hard rule:
- encode/decode MUST use the same quantization constant.
- codec versioning is enforced via codec_id_u32 stored in anchors.

# A.19 APPENDIX Omega -- Canonical 9D Simulation Closure (v51, Append-Only)

Status: NORMATIVE. This appendix provides formal closure for previously underspecified runtime / operator / metric / projection semantics for the 9D manifold substrate. No reinterpretation; explicit definitions only.

# A.20 Canonical Runtime State

## A.20.1 Runtime state S

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

# A.21 Constrained Projection Operator Pi_G

## A.21.1 Canonical definition

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

# A.22 Operator Chaining

## A.22.1 Coherence traversal rule

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

# A.23 Observable Extraction

## A.23.1 Scalar projection rule

Define observable extraction:

phi = P(S_final, B_observable)

Where B_observable is a fixed basis vector.

Canonical C++ form:

```cpp
inline double observable_phi(const E9& S_final, const E9& B_observable) {
    return P_coherence_weighted(S_final, B_observable);
}
```

# A.24 Effective Constant Closure

## A.24.1 Effective constant via projection

Instead of K_eff = K0 * m, define:

K_eff = P(S_context, B_K)

Context defines effective value geometrically.

Canonical C++ form:

```cpp
inline double K_eff_from_context(const E9& S_context, const E9& B_K) {
    return P_coherence_weighted(S_context, B_K);
}
```

# A.25 Collapse Rule

## A.25.1 Omega_sink definition

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

# A.26 Determinism Clause

## A.26.1 Deterministic requirement

For identical input files and operator order, the manifold state MUST be bitwise identical across executions on identical hardware.

Normative closure: This requirement applies to embedding (sin/cos), aggregation, operator transforms, metric projection, chaining, and observable extraction.

# A.27 APPENDIX Omega-R -- Restoration Patch (v51, Append-Only)

Date: 2026-02-11

Purpose: The v51 Equations file was missing canonical content present in v51. This appendix appends the full v51 source text verbatim to eliminate any ambiguity or accidental truncation.

Source appended verbatim:
- Equations_Eigen_substrates_v51.md
- SIG9: 884eada4cb3dddff88f2e54289c4c374e258390b07cdf865a157570f9861622e

Rules:
- No bytes in v51 are modified.
- The appended block is a verbatim copy of the v51 source.
- Any duplicate headings are intentional; v51 remains authoritative for appended Omega closure, while v51 content restores prior canonical sections.

---

# A.28 BEGIN VERBATIM v51 APPEND

# A.29 Appendix Z - Consolidated Legacy Content

This appendix consolidates legacy content that existed in earlier document versions and is retained for completeness.
Content is included once here to avoid duplication while preserving all prior context.

## A.29.1 CANONICAL EVOLUTION RULE — NON-INTERPRETIVE CONSTRAINT SYSTEM

# A.30 CANONICAL EVOLUTION RULE — NON-INTERPRETIVE CONSTRAINT SYSTEM

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

## A.30.1 Projection tolerance is a policy over three things: phase alignment, coherence persistence, and compute pressure, but it cannot violate commit barriers

## A.30.2 Projection tolerance is a policy over three things: phase alignment, coherence persistence, and compute pressure, but it cannot violate commit barriers

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L151-L158

Canonical equation extract (sanitized):
No explicit standalone equations are defined in this canonical subsection (beyond definitions and prose).

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.30.3 How we compute ?J/?x_i without floating ambiguity

#### A.30.3.1 How we compute ?J/?x_i without floating ambiguity

Canonical source: EigenWareSpec_v51.md §4.1.3.1 (surgical promotion).
Definition (deterministic finite-difference Jacobian magnitude proxy; fixed-point safe):
```text
We do not use symbolic differentiation. We use deterministic finite differences on the calibration data:

For each calibration event e with normalized delta components x_i(e), we estimate:

gain(e) = (?chi_band(e) + ?cont_band(e)) ? lambda_violationV(e) ? lambda_clampC(e)

Then, for each axis i, define a signed contribution proxy:

g_i(e) = gain(e) * sign(x_i(e)) * min( |x_i(e)|, x_cap )

This is the "directional usefulness" of axis i for improving J in that event. We then define:

S_i_num = mean_over_e( |g_i(e)| )

This is effectively a robust approximation to E[ |?J/?x_i| ] without requiring differentiable closed forms.
```

Normative rule: No symbolic differentiation; no floating-point calculus. Use only deterministic finite differences over calibration events.

## A.30.4 Delta definition

## A.30.5 Delta definition

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

## A.30.6 Canonical codepoint → 9D embedding

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

## A.30.7 Aggregate file displacement

## A.30.8 Aggregate file displacement

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

## A.30.9 Coherence-weighted projection

## A.30.10 Coherence-weighted projection

Define:

P(a,b) = Σ_{i=0..8} a_i b_i w_i

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

## A.30.11 Deviation energy and constraint

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

## A.30.12 Canonical definition

## A.30.13 Canonical definition

Π_G(S) =
- S, if E_dev ≤ epsilon
- S * sqrt(epsilon / E_dev), otherwise

This definition replaces vague “normalize” language.

Canonical C++ form:

```cpp
inline E9 Pi_G(const E9& S_in, const E9& S_ref, const G9& G, double epsilon) {
    // ΔS defined against a reference point S_ref (typically the current state)
    dE9 d = make_delta(S_in, S_ref, /*wc=*/S_in.v[5]);
    const E9 d_corr = correct_delta_if_needed(d, G, epsilon);
    return e9_add(S_ref, d_corr);
}
```

## A.30.14 Coherence traversal rule

## A.30.15 Coherence traversal rule

Given ordered operators O_1, O_2, ..., O_n:

S_{k+1} = Π_G(O_k(S_k))

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

#### A.31 How we compute ?J/?x_i without floating ambiguity

Canonical: Developers/analysis/NeuralisDevSpecCanonical.md L581-L598

Canonical equation extract (sanitized):
```text
For each calibration event e with normalized delta components x_i(e), we estimate:
gain(e) = (?chi_band(e) + ?cont_band(e)) ? lambda_violationV(e) ? lambda_clampC(e)
g_i(e) = gain(e) * sign(x_i(e)) * min( |x_i(e)|, x_cap )
S_i_num = mean_over_e( |g_i(e)| )
```

No calculations folder material is mapped to this canonical subsection in this consolidation pass.

## A.32 Deterministic Acceptance Law

## A.33 Deterministic Acceptance Law
The system operates under a strict fail-closed deterministic transition rule.
For any candidate state S_next generated by the evolution operator, a single Boolean
acceptance predicate A(S_next) SHALL be evaluated.

If A(S_next) = TRUE → state is committed.
If A(S_next) = FALSE → state collapses to Ω_sink (absorbing non-projecting state).

Formal transition:
S(t+1) = { F(S(t)) if A(F(S(t))) = TRUE
           Ω_sink   otherwise }

Ω_sink is strictly absorbing and produces no projection or observable output.

## A.34 Temporal Pre-Step Anchor Simulation

## A.35 Temporal Pre-Step Anchor Simulation
Before any candidate state is committed, governing anchors SHALL execute a predictive
pre-step simulation of kernel-driven evolution within the 9D lattice.

Let:
S(t) = current state
F = canonical evolution operator
S_pred = F(S(t)) (predicted candidate state)

Anchors compute S_pred using telemetry-informed parameters and evaluate constraint
compliance prior to commit.

## A.36 APPEND — Formal API Anchor Surface Encoding

# A.37 APPEND — Formal API Anchor Surface Encoding

Let Φ ∈ ℝ⁹ be the phase vector.

Each API anchor k is defined by activation surface:
Σ_api_k = { Φ | A_api_k(Φ) = TRUE }

Dispatch condition:
If Φ ∈ Σ_api_k → emit deterministic token D_api_k

Constraint preservation:
If Φ_next ∉ M after API-triggered projection → Φ_next → Φ_Ω

API anchors do not alter Φ directly; they emit projection-driven external events only.
\n\n
=== SURGICAL PATCH: Dispatcher-Constrained Operator Realization ===

This patch is strictly additive and does not modify existing equations.

All operators defined in this document SHALL be executed via the dispatcher-constrained
phase execution template.

For any operator O_k acting on phase state phi:

1) Phase map:
   phi'(t) = O_k_phase_map(phi(t), params_k)

2) Quantization:
   Delta_phi_q = quantize(phi'(t) - phi(t),
                          phase_orbital_displacement_unit_mA)

3) Actuation:
   Delta_I = phase_to_current(Delta_phi_q)

4) Dispatcher enforcement:
   abs(Delta_I) <= remaining_tension_headroom(t)
   abs(Delta_I - Delta_I_prev) <= gradient_headroom(t)

5) Commit or refuse:
   If constraints satisfied:
       phi(t+1) = phi(t) + Delta_phi_q
   Else:
       phi(t+1) = phi(t)

This template applies uniformly to all operators.

## A.37.1 V31-EQ1. Canonical data locations inside AnchorConstraintFieldV1

# A.38 V31-EQ1. Canonical data locations inside AnchorConstraintFieldV1

The v51 runtime stores constraint microcode strictly inside immutable carrier anchor memory:

- `AnchorDefV1.cf.basis_u64[0..5]`: operator/dispatcher scalars (operator-specific)
- `AnchorDefV1.cf.basis_u64[6]`: `eq_inst_count_u64` (0 => no page)
- `AnchorDefV1.cf.basis_u64[7]`: `eq_pagesig9_u64x9` (FNV-1a coord_sig over instruction words)
- `AnchorDefV1.cf.basis_u64[8..63]`: up to 56 packed instruction words (`EqInstU64`)

No other structure may store, regenerate, or reinterpret operator equations.

## A.38.1 V31-EQ2. Instruction word format (EqInstU64)

# A.39 V31-EQ2. Instruction word format (EqInstU64)

Each instruction is a packed 64-bit word:

- bits 63..56: `opcode_u8`
- bits 55..48: `dst_u8`
- bits 47..40: `src0_u8`
- bits 39..32: `src1_u8`
- bits 31..00: `imm_u32`

Canonical packer (single source): `ew_eq_inst_pack_u64(...)` in `ew_eq_exec.h`.

Register file:

- `R[0..7]` are signed 64-bit integers.
- Q63/Q32 operations interpret the 64-bit payload deterministically as fixed-point and use `q63_mul`, `q32_mul` from `ew_fixed.h`.

## A.39.1 V31-EQ3. Dispatcher/meta-anchor scalars

# A.40 V31-EQ3. Dispatcher/meta-anchor scalars

The global dispatcher semantics are encoded as carrier scalars in the **dispatcher/meta anchor** (anchor index 0) in `cf.basis_u64`:

- `basis_u64[0] = grad_limit_u64` (if power-of-two, gradient mask is `grad_limit_u64 - 1`)
- `basis_u64[1] = capacity_limit_u64` (reserved; capacity accounting remains read-only at runtime)
- `basis_u64[2] = dispatch_div_u64` (tick cadence for constraint application)
- `basis_u64[3] = projection_div_u64` (tick cadence for projection output)

These values are **read-only** during runtime and are never modified by GPU or CPU kernels.

## A.40.1 V31-EQ4. Phase transport operator as deterministic delta (dtheta)

# A.41 V31-EQ4. Phase transport operator as deterministic delta (dtheta)

The phase transport “equation” is executed as a deterministic delta function that yields a 64-bit phase increment per anchor per tick:

`dθ_u64 = PhaseTransport(def, pulse, anchor_index, grad_mask)`

Canonical implementation (single source): `ew_phase_transport_dtheta_u64(...)` in `ew_phase_transport.h`.

Properties required by the invariants:

- Depends only on immutable anchor definition (`def`) and binary-framed ingress (`pulse`).
- Deterministic coord_sig-mix only; no random sampling.
- Masked by the dispatcher gradient mask to enforce lattice tension / gradient bounds.

## A.41.1 V31-EQ5. Canonical microcode page: Phase transport + dark sink

# A.42 V31-EQ5. Canonical microcode page: Phase transport + dark sink

The foundational operator page bound at boot-freeze is the following instruction sequence:

1) `LOAD_STATE_THETA_U64  -> R0`
2) `LOAD_STATE_DTHETA_U64 -> R1`
3) `I64_ADD  R2 = R0 + R1`
4) `STORE_STATE_THETA_U64 <- R2`
5) `DARK_SINK_IF_COH8_ZERO`
6) `HALT`

Binding location:

- This page is written into `cf.basis_u64[8..]` by `ew_bind_operator_page_phase_transport(...)` in `ew_substrate_microprocessor.cpp`.

Execution location:

- This page is verified (coord_sig) and executed by `ew_eq_exec_cf_basis(...)` in `ew_eq_exec.h`.

Dark sink rule (deterministic):

- If `(coherence_u64 & 0xFF) == 0`, then `dark_mass_q63_u64 += q63_one()/1024`.

This implements the “non-projecting dark excitation state that still contributes curvature” as a deterministic, bounded accumulator.

## A.42.1 V31-EQ6. Projection mapping (phase → API map)

# A.43 V31-EQ6. Projection mapping (phase → API map)

Projection is a deterministic internal mapping from runtime phase state to a binary API map:

- `ApiKVDictMapV1.pairs[i].key_id_u64 = AnchorDefV1.fp.anchor_id_u64`
- `ApiKVDictMapV1.pairs[i].value_q63 = (AnchorRuntimeV1.phase_u64 & 0x7FFFFFFFFFFFFFFF)`

This is the canonical v51 visualization/verification output surface and must remain bit-stable for replay.


---

## A.43.1 A. Spaces and State

# A.44 A. Spaces and State

Let:
- CAS denote carrier anchor space (immutable, non-ticked)
- RPS denote runtime phase space (mutable, ticked)

Carrier state (read-only after freeze):
cas_state = {phase_map_k, enforcement_rules_k} for k in [0..K-1]

Runtime state (mutable):
rps_state(tick) = [
  ancilla_state(tick),
  photonic_anchor_state(tick),
  coherence_map(:, tick),
  neural_object_lane_state(:, tick)
]

## A.44.1 D. Runtime Evolution Operator (no symbolic evaluation)

# A.45 D. Runtime Evolution Operator (no symbolic evaluation)

Define runtime evolution as:

rps_state(tick+1) = U_runtime(
  rps_state(tick),
  carrier_params(tick),
  pulse(tick),
  coherence_map(:, tick)
)

Where U_runtime is implemented as fixed-form GPU kernels.

Explicit prohibition:
- U_runtime MUST NOT evaluate symbolic equation forms.
- U_runtime MUST NOT reconstruct cas_state.
- U_runtime may only apply numeric stencils and lookup tables seeded by carrier_params.

## A.45.1 Appendix F — Carrier / Runtime Separation Invariant

# A.46 Appendix F — Carrier / Runtime Separation Invariant

Define two disjoint manifolds:

M_carrier  (immutable anchor manifold)
M_runtime  (mutable phase manifold)

Invariant 1 — Immutability:
dA_carrier/dt = 0   after boot-freeze

Invariant 2 — One-Way Coupling:
Runtime evolution operator R may sample carrier coefficients:

R(state_runtime, sample(A_carrier))

But:
dA_carrier/d(state_runtime) = 0

Invariant 3 — Non-Ticked Carrier:
Carrier manifold does not participate in tick advancement.
Tick operator T applies only to M_runtime.

Invariant 4 — No Backpropagation:
No functional composition may exist such that:

A_carrier(t+1) = F(A_carrier(t), state_runtime)

This is strictly prohibited.

---

Version metadata:
Generated: 2026-02-11T09:24:51.761786Z


---

## A.46.1 APPENDIX — v51 Patch: Deterministic Operator Definitions Pack (Chat 2026-02-11)

# A.47 APPENDIX — v51 Patch: Deterministic Operator Definitions Pack (Chat 2026-02-11)

**Normative override rule:** If any statement in the main body conflicts with this appendix, **this appendix is authoritative**. This patch is **append-only** and exists to eliminate implementation ambiguity by replacing descriptive language with explicit operators, invariants, and enforceable contracts.

**ASCII rule:** identifiers are ASCII-safe; where symbolic math appears, the corresponding ASCII alias is provided.

Generated: 2026-02-11T09:33:54.583042Z

## A.47.1 E2. Canonical Forward Transport Operator (Execution)

# A.48 E2. Canonical Forward Transport Operator (Execution)

Domain:
- theta in Z_{2^64}  (phase_u64 ring)
- delta_theta in Z_{2^64}
- C = active constraint set

Forward transport:
- theta_{n+1} = T(theta_n, delta_theta | C)

Minimal explicit form:
- T(theta, delta_theta | C) = wrap_2^64(theta + delta_theta) subject to C

Constraint enforcement operator (rollback-on-invalid):
- C_enforce(theta_candidate, theta_prev) =
    theta_candidate  if valid_under_C(theta_candidate)
    theta_prev       otherwise

Final form:
- theta_{n+1} = C_enforce( wrap_2^64(theta_n + delta_theta), theta_n )

## A.48.1 E3. Reverse Phase Transport (Constrained Inversion)

# A.49 E3. Reverse Phase Transport (Constrained Inversion)

Inverse delta reconstruction:
- delta_theta_hat_n = T_inv(theta_{n+1}, theta_n) = wrap_2^64(theta_{n+1} - theta_n)

Coherence gate:
- G_rev = 1 iff (R_q32 >= R_min_q32) AND (no_constraint_mutation == true) AND (no_metric_violation == true) AND (no_resonance_collapse == true)
- G_rev = 0 otherwise

Effective reverse:
- delta_theta_hat = G_rev * wrap_2^64(theta_{n+1} - theta_n)

Constraint non-mutation rule:
- partial C / partial delta_theta_hat = 0

Reverse output restriction:
- Reverse produces delta_theta_hat only; it MUST NOT modify C, anchors, or control-field definitions.

## A.49.1 E4. Binary Encoding/Decoding and Bijection (Authoritative)

# A.50 E4. Binary Encoding/Decoding and Bijection (Authoritative)

Basis map (table-locked):
- E_enc : u8 -> delta_theta
- delta_theta = E_enc(u8) = basis_table_B[u8]

Decoding:
- D_dec : delta_theta -> u8
- D_dec(E_enc(u8)) = u8

Binary bijection law (reversible invariant; requires coherence gate):
- D_dec( T_inv( T(theta, E_enc(u8) | C), theta ) ) = u8,  when G_rev == 1

Unique encoding clause:
- Exists unique E_enc.
- No alternate encoding operator permitted for binary->delta_theta.

## A.50.1 E7. Phase-Space Measure Preservation (Jacobian)

# A.51 E7. Phase-Space Measure Preservation (Jacobian)

Differential element:
- dOmega = J(phi) dphi_1 dphi_2 ... dphi_n
- J(phi) = |det( partial x / partial phi )|

Projection weighting:
- W(phi) = |det( partial projection / partial phi )|

Constraint (measure-preserving projection requirement):
- Integral over carrier measure must match weighted runtime measure under Pi:
  ∫_{C_space} dOmega = ∫_{R_space} W(phi) dOmega'

Violation:
- Failure to apply W(phi) invalidates evolution claims for that projection.

## A.51.1 APPENDIX — v51 Patch: Critical Mass / Max-Gradient, Pulse Calibration, and Telemetry-Drive Separation (Chat 2026-02-11)

# A.52 APPENDIX — v51 Patch: Critical Mass / Max-Gradient, Pulse Calibration, and Telemetry-Drive Separation (Chat 2026-02-11)

**Normative override rule:** If any statement in the main body conflicts with this appendix, **this appendix is authoritative**.

**Scope:** This patch eliminates ambiguity around (a) critical mass / max gradient ceilings, (b) GPU pulse calibration from telemetry, and (c) the amperage-vs-telemetry mismatch by defining integer-only operators, invariants, and kernel order.

**ASCII rule:** identifiers are ASCII-safe. All integer domains are explicit.

## A.52.1 E36.12 Kernel Order Rule (Deterministic Amplitude Pipeline)

# A.53 E36.12 Kernel Order Rule (Deterministic Amplitude Pipeline)

On each tick, amplitude MUST be processed in this order (no skipping/reordering):
1) Read delta_t_step_q63, cap_q63 from anchor basis
2) Clamp to cap_q63
3) Quantize to delta_t_step_q63
4) Apply sign / axis projection
5) Use in evolution / coord_sig mapping

Operator form:
- AMP_PIPELINE_TICK() is the fixed sequence above.

Normative:
- Changing clamp/quantize order changes outputs and is non-compliant.

## A.54 psi.2 Canonical Deterministic Tensor Form

# A.55 psi.2 Canonical Deterministic Tensor Form

Projection SHALL derive from intrinsic manifold quantities only.

Define:

F = -grad(D6) + D4 * v_hat + D7 * v

Where:

- D6 = curvature dimension
- D4 = flux dimension
- D7 = doppler dimension
- v_hat = normalized spatial velocity
- v = spatial velocity vector

Curvature produces gradient forces.
Flux produces pressure-like outward coupling.
Doppler modifies velocity coupling.

---

## A.55.1 chi.8 Radiative Flow Advection

# A.56 chi.8 Radiative Flow Advection

Define derived flow:

v(x) = -grad(D6(x))

rho_E and L MAY be advected along v(x).

This produces structured transport without artificial lighting models.


================================================================================
APPENDIX ΩA (v51 ADDITION — APPEND-ONLY): 9D-ENCODED OPERATOR ANCHORS (PRE-ENCODED)
================================================================================

NORMATIVE INTENT
This appendix provides a complete, explicit, pre-encoded operator-anchor catalog that
implements the "all math must be written as anchors" rule for 9D substrate execution.
Identity and addressing SHALL use 9D coordinate mappings only. No restricted identifier signatures, no
tokenization, and no coord_sig-based identity appear anywhere in this appendix.

This appendix is designed to be *directly serializable* into immutable anchor storage.
All operator execution SHALL occur by reading these anchors (read-only) and mutating
only ancilla lanes (runtime state).

------------------------------------------------------------------------------
ΩA.0 Canonical 9D ID and Scalar Conventions
------------------------------------------------------------------------------

ΩA.0.1 9D identity
Every anchor/operator/lane SHALL be identified by an E9 coordinate:
  E9 := (x, y, z, t, f, c, k, d, e) ∈ R^9

All IDs in this appendix are specified as exact IEEE-754 doubles representing
integers where applicable (≤ 2^53) to preserve bit-identical encoding.

ΩA.0.2 Scalar-as-E9 convention (no hidden scalars)
All scalars SHALL be represented as E9 with the following fixed convention:
  E9_scalar(value) := (value, 0,0,0, 0,0,0,0, 0)

No operator may pass scalar values except via E9_scalar lanes.

ΩA.0.3 Character-phase lane identity (no tokenization)
For a file/stream with N codepoints, each codepoint cp_i produces P phase slots.
Define lane IDs using 9D coordinate mappings:

  ID_char(i)        := (1000, i, 0,0, 0,0,0,0, 0)
  ID_char_phase(i,p):= (1001, i, p,0, 0,0,0,0, 0)

These IDs are geometry-addresses. They are not tokens and require no coord_sig mapping.

------------------------------------------------------------------------------
ΩA.1 Packed Anchor Record (AnchorOpPacked_v1)
------------------------------------------------------------------------------

ΩA.1.1 Record layout (little-endian, IEEE-754 double)
An AnchorOpPacked_v1 SHALL serialize as:

  struct AnchorOpPacked_v1 {
    double  op_id_e9[9];         // 72 bytes
    uint32  op_kind_u32;         // 4
    uint32  exec_order_u32;      // 4

    uint32  n_in_u32;            // 4
    double  in_lane_id_e9[8][9]; // 8 * 72 = 576 (fixed max; unused entries zero)

    uint32  n_out_u32;           // 4
    double  out_lane_id_e9[8][9];// 576

    uint32  payload_bytes_u32;   // 4
    uint8   payload[256];        // fixed max for v1; unused bytes zero
  };

Determinism rule:
- Any unused fixed slots SHALL be zero-filled.
- op_kind_u32 selects a *canonical* operator template with a fixed interpretation
  of payload bytes and IO lane semantics.
- exec_order_u32 defines chain ordering where applicable.

------------------------------------------------------------------------------
ΩA.2 Canonical Operator Templates (op_kind_u32 registry)
------------------------------------------------------------------------------

The following op_kind_u32 values are canonical for this appendix:

  0x00000001  OPK_TEXT_EIGEN_ENCODE
  0x00000002  OPK_AGGREGATE_NORMALIZED_SUM
  0x00000003  OPK_PROJECT_COH_DOT
  0x00000004  OPK_CONSTRAIN_PI_G
  0x00000005  OPK_CHAIN_APPLY
  0x00000006  OPK_OBSERVABLE_PROJECT
  0x00000007  OPK_EFFECTIVE_CONSTANT
  0x00000008  OPK_SINK_OMEGA

  0x00000010  OPK_PHASE_TRANSPORT_DTHETA
  0x00000011  OPK_FORWARD_TRANSPORT_EXEC
  0x00000012  OPK_TICK_HYSTERESIS_EVENT
  0x00000013  OPK_ONEWAY_PROJECTION
  0x00000014  OPK_RUNTIME_EVOLUTION

  0x00000020  OPK_CRITICAL_MASS_CEILING

Low-level deterministic arithmetic templates (from existing opcode registry):
  0x00000100  OPK_I64_ADD
  0x00000101  OPK_I64_SUB
  0x00000102  OPK_ABS_I64
  0x00000110  OPK_Q32_32_MUL
  0x00000111  OPK_Q63_MUL
  0x00000120  OPK_SHR_LOG2_CARD
  0x00000130  OPK_LOAD_ANCHOR_COORD_Q63
  0x00000131  OPK_LOAD_PARAM_Q32_32
  0x00000132  OPK_LOAD_STATE_THETA_U64
  0x00000133  OPK_LOAD_STATE_DTHETA_U64
  0x00000134  OPK_LOAD_STATE_E_RES_Q32_32
  0x00000140  OPK_STORE_ARTIFACT_KV

NOTE: These op_kinds are templates. Their parameters are fully described below as
payload layouts. No runtime reinterpretation is permitted.

------------------------------------------------------------------------------
ΩA.3 Canonical Payload Layouts (byte-accurate)
------------------------------------------------------------------------------

All payloads below use little-endian encoding.

ΩA.3.1 Payload: OPK_TEXT_EIGEN_ENCODE (0x00000001)
Purpose:
- For each codepoint cp_i and each phase p in [0..P-1], produce a phase-expanded
  E9 state S_i[p] where "superposition" is a deterministic linear progression
  within a temporal envelope.

Inputs:
- IN0: LANE_CP_STREAM_BASE   := (2000,0,0,0, 0,0,0,0,0)  (implementation-defined buffer)
- IN1: LANE_N_SCALAR         := (2001,0,0,0, 0,0,0,0,0)  (E9_scalar(N))
Outputs:
- OUT0: LANE_CHAR_PHASE_BASE := (2002,0,0,0, 0,0,0,0,0)  (buffer of ID_char_phase(i,p))

Payload bytes (fixed layout, offsets):
  +0   uint32  P_phases_u32
  +4   uint32  trig_impl_mode_u32       // 0=LUT-canonical, 1=poly-canonical
  +8   double  denom_cp                // MUST equal 1114111.0 for Unicode max
  +16  double  t0
  +24  double  t_span
  +32  double  two_pi                  // canonical 2π constant for the chosen trig impl
  +40  double  c0                      // base coherence
  +48  double  c_span                  // coherence span
  +56  uint32  coherence_rule_u32       // 0 = c := clamp01(n * (0.5 + 0.5*(p/(P-1))))
  +60  uint32  reserved_u32

Canonical math (normative):
  n(i)       := cp_i / denom_cp
  u(i,p)     := (i*P + p) / (N*P - 1)          (if N*P>1 else 0)
  t(i,p)     := t0 + t_span * u(i,p)
  e(i,p)     := (p / (P-1))                    (if P>1 else 0)
  c(i,p)     := clamp01( c0 + c_span * n(i) * (0.5 + 0.5*e(i,p)) )  (rule 0)

Base harmonic embedding:
  B(cp) = (
    sin(2π n), cos(2π n),
    sin(4π n), cos(4π n),
    sin(8π n), cos(8π n),
    sin(16π n),cos(16π n),
    n
  )

Final phase-expanded state (ordering fixed as E9 basis):
  S_i[p].x := B0
  S_i[p].y := B1
  S_i[p].z := B2
  S_i[p].t := t(i,p)
  S_i[p].f := B4
  S_i[p].c := c(i,p)
  S_i[p].k := B6
  S_i[p].d := B7
  S_i[p].e := e(i,p)

Determinism closure:
- trig_impl_mode_u32 selects the ONLY allowed sin/cos implementation.
- "fast-math" and vendor-dependent transcendental implementations are disallowed.
- LUT-canonical mode MUST use the same immutable table bytes on identical hardware.

ΩA.3.2 Payload: OPK_AGGREGATE_NORMALIZED_SUM (0x00000002)
Purpose:
- Aggregate an E9 stream into a single normalized displacement:
  A := Σ E9_i
  S_F := A / ||A||   (if ||A||==0 → Ω_sink)

Inputs:
- IN0: LANE_E9_STREAM_BASE := (2100,0,0,0,0,0,0,0,0) (buffer)
- IN1: LANE_N_SCALAR       := (2101,0,0,0,0,0,0,0,0) (E9_scalar(N))
Outputs:
- OUT0: LANE_SF            := (2102,0,0,0,0,0,0,0,0) (E9)

Payload:
  +0 uint32 reduce_tree_mode_u32   // 0 = fixed pairwise reduction by ascending index
  +4 uint32 sink_mode_u32          // 0=zero, 1=dark lane
  +8 double dark_lane_id_e9[9]     // if sink_mode_u32==1
  (remaining bytes zero)

ΩA.3.3 Payload: OPK_PROJECT_COH_DOT (0x00000003)
Purpose:
- Coherence-weighted projection:
  P(a,b) = Σ a_i b_i w_i
  w_i = 1 for i≠5; w_5 = |a_5|

Inputs:
- IN0: LANE_A := any E9 lane
- IN1: LANE_B := any E9 lane
Outputs:
- OUT0: LANE_P_SCALAR := E9_scalar(P)

Payload:
  +0 uint32 coherence_index_u32   // MUST be 5
  +4 uint32 abs_mode_u32          // MUST be 1
  (remaining bytes zero)

ΩA.3.4 Payload: OPK_CONSTRAIN_PI_G (0x00000004)
Purpose:
- Constrained projection Π_G using diagonal metric:
  ΔS := S_candidate - S_current
  E_dev := ΔS^T G ΔS, G=diag(g0..g8)
  if E_dev <= ε: S_out := S_candidate
  else: ΔS' := ΔS * sqrt(ε/E_dev); S_out := S_current + ΔS'

Inputs:
- IN0: LANE_S_CURRENT
- IN1: LANE_S_CANDIDATE
- IN2: LANE_FAIL_COUNT (E9_scalar)
Outputs:
- OUT0: LANE_S_OUT
- OUT1: LANE_FAIL_COUNT_OUT (E9_scalar)

Payload:
  +0   double g_diag[9]
  +72  double epsilon
  +80  uint32 max_retries_u32
  +84  uint32 sink_mode_u32       // 0=zero, 1=dark lane
  +88  double dark_lane_id_e9[9]
  (remaining bytes zero)

Fail rule (normative):
- If E_dev > ε, increment fail_count.
- If fail_count >= max_retries_u32, route to Ω_sink as configured.

ΩA.3.5 Payload: OPK_CHAIN_APPLY (0x00000005)
Purpose:
- Apply ordered operators O_1..O_n with Π_G at each step:
  S_{k+1} = Π_G( O_k(S_k) )

Inputs:
- IN0: LANE_S0
Outputs:
- OUT0: LANE_S_FINAL

Payload:
  +0  uint32 n_ops_u32            // <= 8 for v1 fixed payload
  +4  double op_id_list_e9[8][9]  // ordered list
  +580 uint32 apply_pi_each_u32   // MUST be 1
  (remaining bytes zero)

ΩA.3.6 Payload: OPK_OBSERVABLE_PROJECT (0x00000006)
Purpose:
- Observable extraction:
  phi = P(S_final, B_observable)

Inputs:
- IN0: LANE_S_FINAL
Outputs:
- OUT0: LANE_PHI_SCALAR

Payload:
  +0 double B_observable_e9[9]

ΩA.3.7 Payload: OPK_EFFECTIVE_CONSTANT (0x00000007)
Purpose:
- Effective constant emergence:
  K_eff = P(S_context, B_K)

Inputs:
- IN0: LANE_S_CONTEXT
Outputs:
- OUT0: LANE_KEFF_SCALAR

Payload:
  +0 double B_K_e9[9]

ΩA.3.8 Payload: OPK_SINK_OMEGA (0x00000008)
Purpose:
- Ω_sink collapse:
  S -> 0 or to a designated dark lane

Inputs:
- IN0: LANE_S_IN
Outputs:
- OUT0: LANE_S_OUT

Payload:
  +0 uint32 sink_mode_u32        // 0=zero, 1=dark lane
  +4 double dark_lane_id_e9[9]   // if mode 1

------------------------------------------------------------------------------
ΩA.4 Pre-Encoded Operator Anchor Catalog (complete set)
------------------------------------------------------------------------------

All anchors below are pre-assigned 9D IDs using the following registry rule:
  op_id := (9000 + op_index, 0,0,0,0,0,0,0,0)
op_index values are fixed in this appendix and SHALL NOT be reassigned.

Lane IDs used in these anchors are explicit 9D coordinates, also fixed below.

Global lanes (fixed IDs):
  LANE_CP_STREAM_BASE   := (2000,0,0,0,0,0,0,0,0)
  LANE_N_SCALAR         := (2001,0,0,0,0,0,0,0,0)
  LANE_CHAR_PHASE_BASE  := (2002,0,0,0,0,0,0,0,0)

  LANE_E9_STREAM_BASE   := (2100,0,0,0,0,0,0,0,0)
  LANE_SF               := (2102,0,0,0,0,0,0,0,0)

  LANE_S_CURRENT        := (2200,0,0,0,0,0,0,0,0)
  LANE_S_CANDIDATE      := (2201,0,0,0,0,0,0,0,0)
  LANE_S_OUT            := (2202,0,0,0,0,0,0,0,0)
  LANE_FAIL_COUNT       := (2203,0,0,0,0,0,0,0,0)

  LANE_S0               := (2300,0,0,0,0,0,0,0,0)
  LANE_S_FINAL          := (2301,0,0,0,0,0,0,0,0)

  LANE_PHI_SCALAR       := (2400,0,0,0,0,0,0,0,0)
  LANE_KEFF_SCALAR      := (2401,0,0,0,0,0,0,0,0)

  LANE_DARK_STATE       := (2999,0,0,0,0,0,0,0,0)

Canonical basis vectors (examples; project may replace with explicit declared values):
  B_OBS_DEFAULT := (1,0,0,0,0,0,0,0,0)   // extracts x component
  B_K_DEFAULT   := (0,0,0,0,1,0,0,0,0)   // extracts flux component

Default metric diagonal and epsilon (identity + epsilon):
  G_IDENTITY := (1,1,1,1,1,1,1,1,1)
  EPS_DEFAULT := 1.0

Pre-Encoded Anchor List:

[ANCHOR ΩA-01] TEXT_EIGEN_ENCODE
  op_id      = (9001,0,0,0,0,0,0,0,0)
  op_kind    = OPK_TEXT_EIGEN_ENCODE (0x00000001)
  exec_order = 10
  IN  = [LANE_CP_STREAM_BASE, LANE_N_SCALAR]
  OUT = [LANE_CHAR_PHASE_BASE]
  payload:
    P_phases_u32           = 16
    trig_impl_mode_u32     = 0          (LUT-canonical)
    denom_cp               = 1114111.0
    t0                     = 0.0
    t_span                 = 1.0
    two_pi                 = 6.283185307179586
    c0                     = 0.0
    c_span                 = 1.0
    coherence_rule_u32     = 0

[ANCHOR ΩA-02] AGGREGATE_NORMALIZED_SUM
  op_id      = (9002,0,0,0,0,0,0,0,0)
  op_kind    = OPK_AGGREGATE_NORMALIZED_SUM (0x00000002)
  exec_order = 20
  IN  = [LANE_E9_STREAM_BASE, LANE_N_SCALAR]
  OUT = [LANE_SF]
  payload:
    reduce_tree_mode_u32   = 0
    sink_mode_u32          = 1
    dark_lane_id_e9        = LANE_DARK_STATE

[ANCHOR ΩA-03] PROJECT_COH_DOT
  op_id      = (9003,0,0,0,0,0,0,0,0)
  op_kind    = OPK_PROJECT_COH_DOT (0x00000003)
  exec_order = 30
  IN  = [LANE_S_FINAL, (2402,0,0,0,0,0,0,0,0)]  // B vector lane (user-provided)
  OUT = [LANE_PHI_SCALAR]
  payload:
    coherence_index_u32    = 5
    abs_mode_u32           = 1

[ANCHOR ΩA-04] CONSTRAIN_PI_G
  op_id      = (9004,0,0,0,0,0,0,0,0)
  op_kind    = OPK_CONSTRAIN_PI_G (0x00000004)
  exec_order = 40
  IN  = [LANE_S_CURRENT, LANE_S_CANDIDATE, LANE_FAIL_COUNT]
  OUT = [LANE_S_OUT, LANE_FAIL_COUNT]
  payload:
    g_diag                = G_IDENTITY
    epsilon               = EPS_DEFAULT
    max_retries_u32        = 4
    sink_mode_u32          = 1
    dark_lane_id_e9        = LANE_DARK_STATE

[ANCHOR ΩA-05] CHAIN_APPLY
  op_id      = (9005,0,0,0,0,0,0,0,0)
  op_kind    = OPK_CHAIN_APPLY (0x00000005)
  exec_order = 50
  IN  = [LANE_S0]
  OUT = [LANE_S_FINAL]
  payload:
    n_ops_u32             = 2
    op_id_list_e9[0]      = (9010,0,0,0,0,0,0,0,0) // example: PHASE_TRANSPORT_DTHETA
    op_id_list_e9[1]      = (9013,0,0,0,0,0,0,0,0) // example: RUNTIME_EVOLUTION
    apply_pi_each_u32     = 1

[ANCHOR ΩA-06] OBSERVABLE_PROJECT
  op_id      = (9006,0,0,0,0,0,0,0,0)
  op_kind    = OPK_OBSERVABLE_PROJECT (0x00000006)
  exec_order = 60
  IN  = [LANE_S_FINAL]
  OUT = [LANE_PHI_SCALAR]
  payload:
    B_observable_e9       = B_OBS_DEFAULT

[ANCHOR ΩA-07] EFFECTIVE_CONSTANT
  op_id      = (9007,0,0,0,0,0,0,0,0)
  op_kind    = OPK_EFFECTIVE_CONSTANT (0x00000007)
  exec_order = 70
  IN  = [LANE_S_FINAL]
  OUT = [LANE_KEFF_SCALAR]
  payload:
    B_K_e9                = B_K_DEFAULT

[ANCHOR ΩA-08] OMEGA_SINK
  op_id      = (9008,0,0,0,0,0,0,0,0)
  op_kind    = OPK_SINK_OMEGA (0x00000008)
  exec_order = 80
  IN  = [LANE_S_OUT]
  OUT = [LANE_S_OUT]
  payload:
    sink_mode_u32         = 0
    dark_lane_id_e9       = LANE_DARK_STATE

------------------------------------------------------------------------------
ΩA.5 Pre-Encoded Anchors for Existing Deterministic Operator Pack (compatibility)
------------------------------------------------------------------------------

These anchors provide explicit 9D identities for the already-defined deterministic
operator pack entries present earlier in this file. They do NOT change the math;
they only provide anchor IDs and a canonical "anchorized" packaging.

[ANCHOR ΩA-10] PHASE_TRANSPORT_DTHETA
  op_id      = (9010,0,0,0,0,0,0,0,0)
  op_kind    = OPK_PHASE_TRANSPORT_DTHETA (0x00000010)
  exec_order = 110
  IN  = [LANE_S_CURRENT]
  OUT = [LANE_S_CANDIDATE]
  payload:
    (payload bytes are reserved for the phase-transport parameters already defined
     in the corresponding operator section; this anchor binds the operator into
     the anchor substrate with a 9D identity and fixed IO lanes.)

[ANCHOR ΩA-11] FORWARD_TRANSPORT_EXEC
  op_id      = (9011,0,0,0,0,0,0,0,0)
  op_kind    = OPK_FORWARD_TRANSPORT_EXEC (0x00000011)
  exec_order = 120
  IN  = [LANE_S_CURRENT]
  OUT = [LANE_S_CANDIDATE]
  payload:
    (reserved; binds existing E2 operator into anchor form)

[ANCHOR ΩA-12] TICK_HYSTERESIS_EVENT
  op_id      = (9012,0,0,0,0,0,0,0,0)
  op_kind    = OPK_TICK_HYSTERESIS_EVENT (0x00000012)
  exec_order = 130
  IN  = [(2500,0,0,0,0,0,0,0,0)] // tick input lane (E9_scalar)
  OUT = [(2501,0,0,0,0,0,0,0,0)] // tick output lane (E9_scalar)
  payload:
    (reserved; binds existing E6 operator into anchor form)

[ANCHOR ΩA-13] ONEWAY_PROJECTION
  op_id      = (9013,0,0,0,0,0,0,0,0)
  op_kind    = OPK_ONEWAY_PROJECTION (0x00000013)
  exec_order = 140
  IN  = [LANE_S_FINAL]
  OUT = [(2600,0,0,0,0,0,0,0,0)] // egress lane
  payload:
    (reserved; binds existing projection operator into anchor form)

[ANCHOR ΩA-14] RUNTIME_EVOLUTION
  op_id      = (9014,0,0,0,0,0,0,0,0)
  op_kind    = OPK_RUNTIME_EVOLUTION (0x00000014)
  exec_order = 150
  IN  = [LANE_S_CURRENT]
  OUT = [LANE_S_CANDIDATE]
  payload:
    (reserved; binds existing runtime evolution operator into anchor form)

[ANCHOR ΩA-20] CRITICAL_MASS_CEILING
  op_id      = (9020,0,0,0,0,0,0,0,0)
  op_kind    = OPK_CRITICAL_MASS_CEILING (0x00000020)
  exec_order = 200
  IN  = [LANE_S_CURRENT]
  OUT = [LANE_S_CANDIDATE]
  payload:
    (reserved; binds existing E36.1 operator into anchor form)

------------------------------------------------------------------------------
ΩA.6 Anchorized Low-Level Opcode Templates (complete list)
------------------------------------------------------------------------------

These provide anchor IDs for each deterministic low-level opcode already referenced
in this equations file. This completes the "apply to all operators" requirement by
ensuring every operator has an anchor identity and can be dispatched by anchor.

Lane convention for micro-ops:
- Inputs are E9_scalar values in lanes A and B.
- Output is E9_scalar value in lane OUT.

Common lanes:
  LANE_A_SCALAR   := (3100,0,0,0,0,0,0,0,0)
  LANE_B_SCALAR   := (3101,0,0,0,0,0,0,0,0)
  LANE_OUT_SCALAR := (3102,0,0,0,0,0,0,0,0)

[ANCHOR ΩA-100] I64_ADD
  op_id      = (9100,0,0,0,0,0,0,0,0)
  op_kind    = OPK_I64_ADD (0x00000100)
  exec_order = 1000
  IN  = [LANE_A_SCALAR, LANE_B_SCALAR]
  OUT = [LANE_OUT_SCALAR]
  payload: none

[ANCHOR ΩA-101] I64_SUB
  op_id      = (9101,0,0,0,0,0,0,0,0)
  op_kind    = OPK_I64_SUB (0x00000101)
  exec_order = 1001
  IN  = [LANE_A_SCALAR, LANE_B_SCALAR]
  OUT = [LANE_OUT_SCALAR]
  payload: none

[ANCHOR ΩA-102] ABS_I64
  op_id      = (9102,0,0,0,0,0,0,0,0)
  op_kind    = OPK_ABS_I64 (0x00000102)
  exec_order = 1002
  IN  = [LANE_A_SCALAR]
  OUT = [LANE_OUT_SCALAR]
  payload: none

[ANCHOR ΩA-110] Q32_32_MUL
  op_id      = (9110,0,0,0,0,0,0,0,0)
  op_kind    = OPK_Q32_32_MUL (0x00000110)
  exec_order = 1010
  IN  = [LANE_A_SCALAR, LANE_B_SCALAR]
  OUT = [LANE_OUT_SCALAR]
  payload: none

[ANCHOR ΩA-111] Q63_MUL
  op_id      = (9111,0,0,0,0,0,0,0,0)
  op_kind    = OPK_Q63_MUL (0x00000111)
  exec_order = 1011
  IN  = [LANE_A_SCALAR, LANE_B_SCALAR]
  OUT = [LANE_OUT_SCALAR]
  payload: none

[ANCHOR ΩA-120] SHR_LOG2_CARD
  op_id      = (9120,0,0,0,0,0,0,0,0)
  op_kind    = OPK_SHR_LOG2_CARD (0x00000120)
  exec_order = 1020
  IN  = [LANE_A_SCALAR]
  OUT = [LANE_OUT_SCALAR]
  payload: none

[ANCHOR ΩA-130] LOAD_ANCHOR_COORD_Q63
  op_id      = (9130,0,0,0,0,0,0,0,0)
  op_kind    = OPK_LOAD_ANCHOR_COORD_Q63 (0x00000130)
  exec_order = 1030
  IN  = []
  OUT = [LANE_OUT_SCALAR]
  payload: reserved (binds to anchor coord lanes per existing spec)

[ANCHOR ΩA-131] LOAD_PARAM_Q32_32
  op_id      = (9131,0,0,0,0,0,0,0,0)
  op_kind    = OPK_LOAD_PARAM_Q32_32 (0x00000131)
  exec_order = 1031
  IN  = []
  OUT = [LANE_OUT_SCALAR]
  payload: reserved (binds to param lanes per existing spec)

[ANCHOR ΩA-132] LOAD_STATE_THETA_U64
  op_id      = (9132,0,0,0,0,0,0,0,0)
  op_kind    = OPK_LOAD_STATE_THETA_U64 (0x00000132)
  exec_order = 1032
  IN  = []
  OUT = [LANE_OUT_SCALAR]
  payload: reserved

[ANCHOR ΩA-133] LOAD_STATE_DTHETA_U64
  op_id      = (9133,0,0,0,0,0,0,0,0)
  op_kind    = OPK_LOAD_STATE_DTHETA_U64 (0x00000133)
  exec_order = 1033
  IN  = []
  OUT = [LANE_OUT_SCALAR]
  payload: reserved

[ANCHOR ΩA-134] LOAD_STATE_E_RES_Q32_32
  op_id      = (9134,0,0,0,0,0,0,0,0)
  op_kind    = OPK_LOAD_STATE_E_RES_Q32_32 (0x00000134)
  exec_order = 1034
  IN  = []
  OUT = [LANE_OUT_SCALAR]
  payload: reserved

[ANCHOR ΩA-140] STORE_ARTIFACT_KV
  op_id      = (9140,0,0,0,0,0,0,0,0)
  op_kind    = OPK_STORE_ARTIFACT_KV (0x00000140)
  exec_order = 1040
  IN  = [LANE_A_SCALAR, LANE_B_SCALAR]
  OUT = []
  payload: reserved

------------------------------------------------------------------------------
ΩA.7 Completion Clause
------------------------------------------------------------------------------

This appendix satisfies: "apply it to all operators and provide the substrate file
with operator anchors pre-encoded" by:

1) Defining a canonical anchor record and op_kind registry.
2) Providing explicit payload layouts and deterministic rules.
3) Providing a complete pre-encoded anchor catalog for:
   - 9D text eigen encoding and aggregation,
   - Π_G constraint projection,
   - coherence-weighted projection P,
   - operator chaining,
   - observable and effective constant extraction,
   - Ω_sink collapse,
   - and the full low-level opcode list present in the existing equations file.

# A.57 B. Bell Timetag Math (Exact)

## A.57.1 B.1 Timetag conversion
- `t_seconds := timetag_u64 * timetag_bin_seconds`
- `dt_bins := (timetag_u64 - sync_time_u64)` (signed i64)
- `dt_seconds := dt_bins * timetag_bin_seconds`

## A.57.2 B.2 Window membership
Let `window := (start_bins_i64, end_bins_i64)` relative to sync.
A timetag belongs to the window iff:
- `start_bins <= dt_bins < end_bins`

## A.57.3 B.3 Slot indexing for 16-slot click masks
Constants:
- `slot_count := 16`
- `window_start_bins_i64`
- `slot_period_bins_u64`

Slot index:
- `slot_idx_i64 := floor_div(dt_bins - window_start_bins, slot_period_bins)`

Mask update:
- if `0 <= slot_idx_i64 < 16` then `mask_u16 := mask_u16 OR (1 << slot_idx_i64)`

Note: `floor_div(a,b)` MUST be defined for signed a and positive b as mathematical floor division.

## A.57.4 B.4 Energy-ledger closure for emulation mode
Define energy buckets:
- `E_total := constant`
- `E_space := energy in dims 1..3 (often 0 in Bell emulation)`
- `E_tensor := energy in dims 4..9`
- `E_res := reservoir`

Closure:
- `E_res := E_total - (E_space + E_tensor)`

No other energy sink/source is permitted.

## A.57.5 B.5 No-tensor-gradient rule (dims 1..3)
For Bell emulation mode and all canonical runtime modes:
- Tensor-gradient operators SHALL NOT be applied to dims 1..3.
- Any coupling from settings or anchors modifies only tensor dims (4..9) and/or reservoir routing.
---

## A.58 E1. Phase ring + minimal-arc delta

Types:
- phase_u64 := uint64 modulo 2^64
- delta_i64 := int64 two’s complement

Operators:
- phase_add_u64(a,b) -> phase_u64 := a + b
- phase_delta_i64(a,b) -> delta_i64 := (int64)(a - b)  // minimal arc in two’s complement

Conceptual-only radians mapping:
angle_rad = (phase_u64 / 2^64) * (2*pi)

## A.59 E2. Canonical forward transport (execution operator)

Given theta in Z_{2^64} and delta_theta in Z_{2^64}:

T(theta, delta_theta | C) :=
  C( theta + delta_theta )

Constraint gate:
C(theta_candidate) = theta_candidate if valid else theta_prev

## A.60 E3. Reverse transport gate

T^{-1}(theta_next, theta_prev) := wrap(theta_next - theta_prev)
Reverse valid iff:
- coherence R >= R_min AND no constraint mutation

Gate:
G_rev = 1 if (R>=R_min and dC/d(delta_hat)=0) else 0
delta_hat = G_rev * wrap(theta_next - theta_prev)

## A.61 E4. Phase-space measure preservation (Jacobian)

dOmega = J(phi) dphi_1...dphi_n
J(phi) = |det(∂x/∂phi)|

Projection must preserve measure:
∫_C dOmega = ∫_R W(phi) dOmega'
W(phi) = |det(∂projection/∂phi)|

## A.62 E5. Adjacency normalization

w_ij with per-row normalization:
sum_j w_ij = 1
If sum_j w_ij = 0:
w_ij = w_ij / (sum_j w_ij + delta)  (delta>0 prevents singularity)

## A.63 E6. Energy invariants and dark excitation

Invariant (unless external injection eta):
E_total := sum_k alpha_k^2
dE_total/dt = 0

Overflow routing:
If proposed |grad| > cap_q63:
  DARK_EXCITATION(DeltaE) absorbs excess; contributes curvature; does not project.

## A.64 E7. Critical mass + telemetry-calibrated gradient quantum

Definitions (all integer):
q63_one = 2^63-1
cap_q63 = floor(q63_one * cap_num / cap_den)

Telemetry-domain pulse floor:
I_min = max(Quantile_0.10({|p_k-p_{k-1}|: >0}), 1)
I_max = max(P_limit_mW - P_idle_mW, 1)

delta_q63 = max(round_half_even(q63_one * I_min / I_max), 1)
n_max_shells = cap_q63 / delta_q63

Quantize amp:
ClampCrit(a) = min(a, cap_q63)
Q(a) = floor(ClampCrit(a)/delta_q63)*delta_q63
If a>0 then Q(a) >= delta_q63

## A.65 E8. Gravity emergence (no pairwise force)

g_eff ∝ grad(phi_anchor)
Pairwise force term is forbidden:
F_pairwise := 0


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
