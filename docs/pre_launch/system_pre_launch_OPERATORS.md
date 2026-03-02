# Harness Pocket 3 — Operator Runtime Closure

Normative scaling + modality axis binding (canonical):
This document SHALL follow the canonical scaling and modality binding rules defined in:
- Equations_Eigen_substrates_v7.md, Section 3.0 (Canonical Scaling and Asymmetric Axis Control)
- system_pre_launch_KERNEL_AND_CONTRACTS_v7.md, Addendum H (Global scaling via H0/CMB; no rounding; per-axis sx/sy/sz; non-uniform Hilbert expansion)
Implementations MUST NOT apply global uniform normalization; per-axis scaling is the control surface.

# 5) Contract Harness 3 — Operator Runtime Closure (Domain/Codomain completeness)

## 5.1 Why this must be tested during core construction
If any operator can emit an invalid state, everything later becomes ambiguous:
- “physics effects” can be artifacts of NaNs or out‑of‑domain values,
- emergent behavior can depend on undefined branches,
- bugs appear only at scale and are nearly impossible to reproduce.

This contract enforces that the operator graph is total over the declared domain.

## 5.2 Definitions

### 5.2.1 Operator descriptor (`EwOpDesc`)
Each operator must publish:
- `name : string`
- `inputs : list[type]`
- `outputs : list[type]`
- `preconditions : list[bool expression]`
- `postconditions : list[bool expression]`
- `domain_bounds : (min, max) per numeric input`

### 5.2.2 Valid state predicate
Define:
- `is_valid_state(state) -> bool`

Minimum validity checks:
- all scalars finite (not NaN, not inf)
- `coherence_p` within range
- positions and velocities within declared bounds
- `id9_p` finite
- any norm constraints within `eps_norm`

### 5.3.1 `validate_state(state) -> EwValidationResult`
Returns all failures and the first failure tick/operator.

### 5.3.2 `operator_apply(op_name, state_in) -> (state_out, status)`
Must never throw uncaught exceptions.

### 5.4.1 Closure grid
For each operator:
- Build a deterministic set of boundary inputs:
 - min, max, mid values for each bound dimension
 - known singular points (0, small epsilon, threshold values)

Then:
1) Apply operator.
2) Validate postconditions and `is_valid_state`.

### 5.4.2 Composition closure
Build a directed operator chain list:
- `transport -> interaction -> coherence_update -> sink_route -> ledger_compute`

Run the chain on each fixture input.
Fail fast on invalid states.

### 5.4.3 Pass/fail
- 100% of operator applications must return:
 - success status OR explicit controlled failure (documented)
 - never silent failure
 - never invalid state

### 5.4.4 What to do if it fails
- Add preconditions for illegal inputs and enforce them.
- Add postcondition clamps only if physically justified (document why).
- If an operator requires a narrower domain, narrow the declared domain and update all callers.

## Canonical supporting sections from EigenWareSpec_v5.md

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
- No intermediate-indexing layer exists.
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

# 18 APPENDIX omega-R — Restoration Patch

Date: 2026-02-11

Purpose: A prior Spec bundle was missing canonical content present in the same bundle. This appendix appends the full source text verbatim to eliminate any ambiguity or accidental truncation.

Source appended verbatim:
- EigenWareSpec_v51.md
- SIG9: eb4ef38e36bff4d22b96568449b988fcf8919cbdfab3cf8f7a5972273b9b7c2a

(See canonical description in section: 'Omega.9 Determinism Clause'.)

---


Genesis Engine replaces crawler software with direct field encoding.

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
