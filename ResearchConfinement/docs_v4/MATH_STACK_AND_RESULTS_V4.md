# MATH STACK AND RESULTS V4

## Core recurring structures

### Effective quantities
These appear across the visible framework files and continuation scripts as derived effective channels rather than arbitrary tuning constants:
- `omega_eff`
- `R_eff`
- `C_eff`
- `scale_eff_doppler`
- `h_eff`, `k_b_eff`, `c_eff`, `e_charge_eff`
- `stochastic_factor`
- `relativistic_corr`

### Pair-channel / support structures
- `Rel_ij`
- `O_ij`
- `R_ij`
- `Cgate_ij`
- `Tpair_base_ij`
- `Tflux_ij`
- `S_ij = Rel_ij^2 * O_ij * R_ij * Cgate_ij * Tpair_base_ij * Tflux_ij`

### Timing correction / replay structures
- `delta_t_pair(j) = t_local(j) * (1 - Rel_i_calc(j)) * sum_{i<j} S_ij`
- `t_corrected(j) = t_local(j) - delta_t_pair(j)`
- `n_j(t+1) = n_j(t) + dn_local_shifted_j(t+1) * (1 + sum_{i<j} S_ij * n_i(t) / n_i_final_baseline)`

### Closure / redistribution structures from later runs
Accessible and in-chat later passes revolve around these bookkeeping families:
- pair-memory-guided support routing
- traced-residence-guided reserve repayment
- parameter-correlation support / reserve routing
- lag-aware temporal tensor routing
- coherence-gated lag selection
- temporal-coupling collapse probes
- winner-selection point-vector collapse
- probability collapse from temporal alignments only
- interpolated sub-step collapse using top-two coherent branches

## Structural equations used conceptually in the later passes
These capture the mathematics described and implemented in the accessible scripts and chat-reported runs.

### Parameter-correlation closure family
Local channel correlation template:
`C_k^(a,b) = ((a_k - mean(a)) * (b_k - mean(b))) / (std(a) * std(b) + eps)`

Positive coherent routing score template:
`q_k = max(0, sum_i alpha_i * C_k^(i))`

Transport normalization template:
`Delta_advance_k = A * q_k / (sum_support q + eps)`
`Delta_repay_k = A * r_k / (sum_reserve r + eps)`
with exact closure constraint:
`sum Delta_advance = sum Delta_repay`

### Lag-aware temporal tensor family
For each candidate channel and lag, rolling local neighborhoods are scored and the best causal lag is selected from a bounded lag set. Support and reserve use different lag windows. Zero-point support drive and reserve relax can be derived from the same lag-aware tensor.

### Coherence gating family
Lag authority is attenuated by a coherence gate derived from:
- lag persistence across local neighborhood
- best-vs-second-best lag margin
- local channel strength

### Collapse / temporal-alignment families
Winner-selection collapse (run 028):
- candidate point-vector templates are channel@lag combinations
- softmax yields winner probabilities
- collapse score combines winner probability, probability margin, entropy rejection, winner-lock stability, and projected visibility

Probability collapse from temporal alignments (run 029):
- candidate score = `pair_support_norm * local_temporal_alignment * lag_match * future_alignment * retro_admissibility * persistence`
- selection logits exclude visible fraction
- readout score multiplies collapse probability by chosen alignment / future / retro terms and visible fraction only after selection

Interpolated temporal collapse (run 031):
- top-two temporal branches selected by probability
- trigger = `dual_branch_coherence * coherence_climb * internal_inconsistency`
- effective interpolated event = `step + weighted_average(top1_lag, top2_lag)`
- structural admissibility penalizes interpolated state by internal inconsistency

## External comparison result currently on record
The NIST surrogate projection pass shows current toy step granularity is not remotely within NIST uncertainty scale. This matters because it means future progress must come from sub-step / continuous event interpolation and physically meaningful projection into SI-observable quantities, not from pretending integer-step locks are already spectroscopy.

## Canonical target ledger for continuation
- onset targets: `r2=4`, `r3=5`, `r5=18`, `r6=31`
- final occupancies:
  - `r2=2.1869844098`
  - `r3=1.8317995831`
  - `r5=0.8290027574`
  - `r6=0.6721458802`

## Current frontier hypothesis
When two temporal alignments are off but still coherent, and coherence climbs while internal consistency breaks, the internal particle structure should collapse to an interpolated structure of the affected points rather than to a hard discrete winner. The next chat should continue directly from that frontier.
