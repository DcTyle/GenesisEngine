# Harness Pocket 1 — Conservation Closure

Normative scaling + modality axis binding (canonical):
This document SHALL follow the canonical scaling and modality binding rules defined in:
- Equations_Eigen_substrates_v7.md, Section 3.0 (Canonical Scaling and Asymmetric Axis Control)
- system_pre_launch_KERNEL_AND_CONTRACTS_v7.md, Addendum H (Global scaling via H0/CMB; no rounding; per-axis sx/sy/sz; non-uniform Hilbert expansion)
Implementations MUST NOT apply global uniform normalization; per-axis scaling is the control surface.

# 3) Contract Harness 1 — Conservation Closure

## 3.1 Why this must be tested during core construction
If conservation does not close at the kernel level, every “emergent” effect becomes ambiguous:
- You cannot distinguish real dynamics from numerical leak.
- Entanglement‑like correlation can be faked by drift.
- Multi‑ledger transitions will silently create or destroy energy.

Conservation closure is the kernel’s “ground wire.” Without it, nothing downstream is interpretable.

## 3.2 Definitions (variables, expressions, derivations)

### 3.2.1 Per‑particle energies
**Kinetic energy**
- `ke_p = 0.5 * mass_p * dot(vel3_p, vel3_p)`

**Potential energy (if used)**
You must choose and define one potential model. Example:
- Pair potential: `pe = sum_{i<j} U(r_ij)`
- Distance: `r_ij = norm3(pos3_i - pos3_j)`

If you don’t use potential energy yet, set `pe = 0` explicitly.

**Total energy**
- `E = sum_p ke_p + pe + E_internal`
where `E_internal` is any explicitly defined internal energy stored in ancilla.

### 3.2.2 Momentum
- `p_p = mass_p * vel3_p`
- `P = sum_p p_p`

### 3.2.3 Residuals
Define expected external injection/removal per tick:
- `E_in_out = ledger_before.in_out_energy` (default 0)

**Energy residual**
- `res_E = ledger_after.energy_total - (ledger_before.energy_total + E_in_out)`

**Momentum residual**
- `res_P = ledger_after.momentum_total3 - ledger_before.momentum_total3`
(if no external forces; otherwise add explicit impulse terms)

### 3.2.4 Acceptance criteria
For each tick:
- `abs(res_E) <= eps_energy`
- `norm3(res_P) <= eps_momentum`

For long runs, measure drift slope:
- Fit `ledger_energy_total(tick)` to a line; require slope magnitude `<= slope_energy_max`.

## 3.3 Operators required (must exist)

### 3.3.1 `compute_ledger(EwState) -> EwLedger`
Computes ledger fields from the actual state.

### 3.3.2 `compute_ledger_delta(ledger_before, ledger_after) -> EwDelta`
Computes deltas.

### 3.3.3 `accept_state(delta_tick, tolerances) -> bool`
Returns `true` only if residuals are inside tolerance.

### 3.3.4 `apply_tick(state_in) -> state_out`
Single tick evolution.

## 3.4 How to test (fixtures + procedure)

### 3.4.1 Fixtures
- **Fixture A: single free particle**
 - No forces, constant velocity.
 - Expect perfect conservation.
- **Fixture B: two particles, elastic collision**
 - Define collision operator as deterministic.
 - Expect energy and momentum conserved (within epsilon).
- **Fixture C: three particles with pair potential**
 - Small dt, verify bounded error.
- **Fixture D: operator chain stress**
 - Apply a known sequence of operators (transport, collision, damping) that should conserve per your ledger definitions.

### 3.4.2 Procedure (per fixture)
1) Initialize `state0`.
2) Compute `ledger0 = compute_ledger(state0)`.
3) For `k = 1..N_ticks`:
 - `state_k = apply_tick(state_{k-1})`
 - `ledger_k = compute_ledger(state_k)`
 - `delta_k = compute_ledger_delta(ledger_{k-1}, ledger_k)`
 - If `accept_state(delta_k, tolerances)` is false:
 - Emit `failure_repro.txt` and stop.

### 3.4.3 What to do if it fails
- If `res_E` grows with `N_ticks`: dt too large or operator not conservative.
- If `res_E` is random‑sign noise but bounded: numerical precision; tighten type or revise integration.
- If `res_P` fails only on collisions: collision operator is not symmetric; fix impulse resolution.

## Canonical supporting sections from EigenWareSpec_v5.md

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
