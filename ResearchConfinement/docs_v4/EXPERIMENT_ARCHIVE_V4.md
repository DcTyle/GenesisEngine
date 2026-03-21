# EXPERIMENT ARCHIVE V4

## Scope
This archive consolidates the accessible run lineage used in the current chat and records missing transient runs reported in-chat but not available on disk.

## Baseline accessible material inherited from V3
- `research_handoff_package_v3_all_in_one/continuation_runs/handoff_pkg/research_handoff_package/`
  - pairwise timing correction artifacts and source script
- `research_handoff_package_v3_all_in_one/continuation_runs/next_pass_outputs/`
  - causal pair replay pass and outputs
- `research_handoff_package_v3_all_in_one/continuation_runs/quartet_projection_hybrid_test/`
  - hybrid quartet projection comparison and histories
- `research_handoff_package_v3_all_in_one/framework files/`
  - visible equation / framework Python scaffolding

## New accessible runs consolidated in V4

### Run 022 — pair memory adaptive closure
- Source files: `new_runs_v4/run_022/`
- What it tested: exact-sum partition closure using pair-memory-guided support windows and traced-residence-guided reserve repayment.
- How it was tested: froze onset-defining region, repaid advanced mass from each band's own late reserve, preserved final occupancies exactly.
- Reported result: exact closure residual 0.0, onset targets preserved, final occupancies preserved to floating-point noise.

### Run 025 — lag-aware temporal tensor closure
- Source files: `new_runs_v4/run_025/`
- What it tested: lag-aware rolling local temporal tensor replacing same-index correlation.
- How it was tested: support channels selected best causal lags over bounded windows; reserve channels used shorter lag windows; zero-point drive/relax derived from same tensor.
- Reported result: exact closure residual 0.0, zero-point residual 0.0, stronger earlier centroid shifts, support recruitment favored delayed upstream/pair structure while reserve repayment favored immediate donor/tail structure.

### Run 026 — coherence-gated lag tensor closure
- Source files: `new_runs_v4/run_026/`
- What it tested: attenuation of jittery lag winners using lag persistence, winner margin, and local channel strength.
- How it was tested: coherence gate multiplies lag-aware routing before transport is emitted.
- Reported result: exact closure residual 0.0, zero-point residual 0.0, transport reduced versus run 025 while onset/final targets remained locked.

### Run 027 — temporal coupling collapse probe
- Source files: `new_runs_v4/run_027/`
- What it tested: whether temporal coupling behaves like collapse initiation at onset rather than mere continuity support.
- How it was tested: derived collapse proxy from lag tensor and checked score jumps around onset.
- Reported result: sharp onset jumps in all outer bands; collapse initiation aligns with onset but strongest dominance builds afterward.

### Run 028 — winner-selection collapse operator
- Source files: `new_runs_v4/run_028/`
- What it tested: explicit point-vector winner selection from channel/lag candidates.
- How it was tested: softmax over candidate point-vector templates, collapse score from winner probability, margin, entropy rejection, winner-lock stability, projected visibility.
- Reported result: winner probability at onset ~0.998 for all outer bands, but threshold crossing still lagged a few steps, implying short lock interval rather than single-step snap.

### Run 029 — temporal-alignment probability collapse
- Source files: `new_runs_v4/run_029/`
- What it tested: probability collapse from temporal alignments only, excluding visible fraction from selection logits.
- How it was tested: branch scores derived from local temporal alignment, lag match, future alignment, retro-admissibility, persistence; visible fraction only in final readout.
- Reported result: all three outer bands lock within roughly 1–2 steps after onset; same leader before and at onset; behavior fits temporal-alignment probability collapse better than textbook measurement language.

### Run 030 — NIST surrogate projection
- Source files: `new_runs_v4/run_030/`
- What it tested: whether current toy step resolution could honestly be within NIST experimental uncertainty after surrogate mapping to lithium D-line scale.
- How it was tested: mapped toy r5→r6 onset spacing to D-line separation and compared per-step scale to linewidth and uncertainty scales.
- Reported result: not within NIST uncertainty scale; current toy step granularity remains far too coarse for a serious metrology claim.

### Run 031 — interpolated temporal-alignment collapse
- Source files: `new_runs_v4/run_031/`
- What it tested: sub-step collapse when two temporal alignments remain coherent while internal consistency breaks during a coherence climb.
- How it was tested: top-two branch interpolation with trigger = dual-branch coherence × coherence climb × internal inconsistency.
- Reported result: r5 and r6 fractional threshold crossings within about one-third step of onset; r3 weaker because coherence climb at onset was not active.

## Missing transient runs reported in chat but not accessible byte-for-byte

### Run 021 — exact partition closure
- Status: not present on disk during consolidation.
- In-chat description: exact-sum partition closure based on canonical replay, preserving onset structure and final occupancies while balancing advanced and repaid mass exactly.

### Run 023 — parameter-correlation closure
- Status: not present on disk during consolidation.
- In-chat description: support/reserve routing derived from positive local parameter correlations rather than manual coefficients; exact closure residual 0.0.

### Run 024 — correlation-derived zero-point closure
- Status: not present on disk during consolidation.
- In-chat description: zero-point redistribution derived from same correlation tensor as closure; exact closure residual 0.0 and near-zero zero-point residual.

## Immediate continuation logic from the current frontier
The current leading edge is the interpolation/collapse lineage:
- run 028: winner-selection collapse
- run 029: temporal-alignment probability collapse
- run 030: surrogate external-resolution check
- run 031: interpolated sub-step collapse

The next chat should continue from run 031 and preserve:
- onset targets
- final occupancy targets
- exact or near-exact closure bookkeeping
- temporal coupling as alignment/flux-lock structure
- interpolation when coherent temporal branches remain admissible but internal consistency forces merger
