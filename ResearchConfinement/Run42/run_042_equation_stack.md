# Run 042 Equation Stack (Encoding + Collapse Calculus)

This file records the equations used by the Run42 frequency-domain model and the collapse-to-calculus post-analysis operator.

## 1) Encoded Controls

Control quartet (from run inputs):
- `f_code` = normalized frequency control
- `a_code` = normalized amplitude control
- `i_code` = normalized amperage control
- `v_code` = normalized voltage control

Run42 values:
- `f_code = 0.245`
- `a_code = 0.18`
- `i_code = 0.33`
- `v_code = 0.33`

## 2) Spectral State Update (per packet p, axis a, bin b, step t)

Let `S[p,a,b,t]` be the complex spectrum state.

1. Log-gradients:
- `dlnA = d/db ln(|S| + eps)`
- `dlnf = d/db ln(f_b * (1 + 0.12 * freq_drive_p * (0.70 + 0.80*f_code)) + eps)`

2. Voltage and coupling phase terms:
- `phi_V = volt_drive_p * (0.35 + 0.85*v_code) * b_norm * (0.5 + 0.35*sin(0.15*t))`
- `phi_C = kappa_couple * phase_lock(p,q) * (0.5 - b_norm)` where `q=(p+1) mod N`

3. Core update:
- `S_next = S * exp(kappa_a * dlnA) * exp(i * (kappa_f * dlnf + phi_V + phi_C))`

4. Amplitude compression / leakage smoothing:
- `|S_next| <- blur_sigma(|S_next|, sigma = 0.95 + 1.35 * current_drive_p)`
- `|S_next| <- clamp(|S_next|, 0, max_amplitude)`

Coefficient set used in Run42:
- `kappa_a = 0.085`
- `kappa_f = 0.055`
- `kappa_couple = 0.080`
- `kappa_leak = 0.035`

## 3) Cross-Talk / Coherence

Pairwise phase lock:
- `C_pq = | <S_p, S_q> | / (||S_p|| * ||S_q|| + eps )`

Shared-vs-individual packet assignment:
- Packet is labeled shared if its lock score is above the dynamic threshold from the run distribution.

## 4) Collapse-to-Calculus Product Operator (Run42 post-analysis)

For each timestep `t`:
- `Shared(t) = { p | packet p classified shared }`
- `X(t) = mean_{p in Shared(t)} phase_coupling_p(t)` (cross-talk mean)
- `I(t) = mean_{p in Shared(t)} temporal_inertia_p(t)` (temporal feedback)
- `G(t) = 1 if X(t) >= 0.997 and trap_ratio >= 0.15 else 0` (collapse gate)

Amplitude-coupling sum:
- `M(t) = sum_{p in Shared(t)} [ amplitude_p(t) * phase_coupling_p(t) ]`

Calculus product trace:
- `P(t) = G(t) * M(t) * (1 + I(t))`

The resulting time-series is exported in:
- `run_042_collapse_calculus_timeseries.csv`

## 5) Certainty Interpretation (Model Scope)

Why these equations are considered to "work" inside this run:
- They maintain bounded spectral states (no uncontrolled divergence).
- They preserve measurable shared/individual separation.
- They produce non-trivial but finite conservation drift.
- They generate stable cross-talk collapse gates with an explicit calculus product trace.

Important boundary:
- This is model-internal certainty, not a proof that physical silicon directly obeys the same operator form.
