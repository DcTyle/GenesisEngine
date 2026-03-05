# Harness Pocket 2 — Deterministic Decoherence

Normative scaling + modality axis binding (canonical):
This document SHALL follow the canonical scaling and modality binding rules defined in:
- Equations_Eigen_substrates_v7.md, Section 3.0 (Canonical Scaling and Asymmetric Axis Control)
- system_pre_launch_KERNEL_AND_CONTRACTS_v7.md, Addendum H (Global scaling via H0/CMB; no rounding; per-axis sx/sy/sz; non-uniform Hilbert expansion)
Implementations MUST NOT apply global uniform normalization; per-axis scaling is the control surface.

# 4) Contract Harness 2 — Deterministic Decoherence (Frame mismatch + Sink routing)

## 4.1 Why this must be tested during core construction
Your model replaces stochastic decoherence with deterministic mechanisms:
- measurement frame mismatch (`frame_gamma`)
- deterministic sink routing when coherence falls below threshold

If this is not validated early:
- you can accidentally re‑introduce randomness,
- you can destroy reproducibility,
- you can mask conservation bugs as “decoherence.”

This contract proves decoherence behavior is deterministic, bounded, and measurable.

## 4.2 Definitions (variables, expressions)

### 4.2.1 Measurement basis and frame shift
Represent a basis vector as `basis_vec` (3D unit vector).

**Axis‑angle rotation**
- Given axis `axis_unit` (unit vector) and angle `gamma`:
 - `basis_vec_shifted = R(axis_unit, gamma) * basis_vec`

Where `R` is the standard Rodrigues rotation:
- `R(v, gamma) * a = a*cos(gamma) + (v x a)*sin(gamma) + v*dot(v, a)*(1 - cos(gamma))`

**Frame gamma constraints**
- `frame_gamma` is global or piecewise‑constant by epoch.
- Not per‑trial, not per‑particle unless explicitly declared.

### 4.2.2 Coherence evolution
Define coherence scalar `coherence_p` and total coherence:
- `C_total = sum_p coherence_p`

Define deterministic decay rule (example template; you must choose your canon):
- `coherence_p_next = clamp01(coherence_p - decay_rate * mismatch_metric)`

Where:
- `decay_rate : EwScalar` (constant or derived from ancilla)
- `mismatch_metric : EwScalar` (e.g., `abs(frame_gamma)` or a function of relative basis misalignment)

### 4.2.3 Sink routing
Define a threshold:
- `coherence_sink_threshold : EwScalar` (e.g., 0.05)

Define sink predicate:
- `is_sink = (coherence_p_next <= coherence_sink_threshold)`

Define sink mapping:
- `state_out = sink_map(state_in)`
with explicit accounting:
- `ledger.curvature_total` increases by `sink_curvature_add`
- energy is conserved by transferring into a named bucket:
 - `E_visible_next + E_sink_bucket_next = E_total_before` (within epsilon)

You must explicitly define the sink energy bucket if you claim energy is conserved globally.

## 4.3 Operators required

### 4.3.1 `frame_shift_apply(basis_vec, frame_gamma, axis_unit) -> basis_vec_shifted`
Implements Rodrigues rotation.

### 4.3.2 `coherence_update(state_in) -> state_mid`
Deterministic coherence update.

### 4.3.3 `sink_route(state_mid) -> state_out`
Applies sink mapping to particles/nodes meeting sink predicate.

## 4.4 How to test

### 4.4.1 Fixtures
- **Fixture E: zero gamma**
 - Set `frame_gamma = 0`
 - Expect no coherence loss solely from mismatch.
- **Fixture F: constant gamma**
 - Set `frame_gamma = constant`
 - Expect deterministic monotone decay if your rule says so.
- **Fixture G: piecewise gamma**
 - Change gamma at a known tick boundary; check reproducibility.
- **Fixture H: sink threshold crossing**
 - Initialize coherence near threshold and verify sink routing is triggered exactly when expected.

### 4.4.2 Pass/fail conditions
- Repeat the same fixture twice: outputs must match bit‑for‑bit in fixed‑point mode, or within epsilon in float64 mode.
- Coherence must never become NaN and must remain within declared range.
- Sink routing must:
 - trigger deterministically,
 - preserve global accounting (`E_visible + E_sink_bucket` invariant),
 - avoid stochastic branching.

### 4.4.3 What to do if it fails
- If runs differ between repeats: hidden nondeterminism in coherence update or operator order.
- If energy accounting breaks at sink: sink bucket not implemented or not coupled to ledger.
- If coherence goes negative: clamp missing or decay rule incorrect.

## Canonical supporting sections from EigenWareSpec_v5.md

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

# 12 Rank-delta phase-step (wrap-safe)
dtheta_rank_turns(t) = wrap_turns( c_pi_turns_per_rank * ( value_id(value_t) - value_id(value_{t-1}) ) )
```

# Identity by offset-rotation (conceptual primitive; used when comparing trajectories)

# "Rotate all possible evolution offsets to determine identity" can be implemented as:
```text

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
