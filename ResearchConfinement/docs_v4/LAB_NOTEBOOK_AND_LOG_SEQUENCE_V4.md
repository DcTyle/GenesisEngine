# Lab Notebook And Log Sequence V4

This document consolidates the accessible research lab record into one append-only sequence.
The ordering follows [RUN_LEDGER_V4.csv](RUN_LEDGER_V4.csv) and preserves missing transient runs in-place so the chronology stays intact.

For this program, the durable run log is the stored data itself:
- history CSV files
- summary CSV files
- comparison, ledger, leader, or candidate CSV files
- supporting summary JSON files and pass scripts when present

There are no standalone text log captures inside the accessible V4 run folders, so the linked data artifacts below are the lab log record.

## Sequence Overview

- Legacy baseline: run 020
- Missing transient record: run 021
- Accessible continuation: runs 022, 025, 026, 027, 028, 029, 030, 031
- Missing transient record preserved in chronology: runs 023 and 024

## Run 020 - causal pair replay baseline

Status:
- present in the inherited V3 continuity package

Primary log and data record:
- [lithium_abszero_causal_pair_replay_history.csv](../research_handoff_package_v3_all_in_one/continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_history.csv)
- [lithium_abszero_pairwise_timing_correction.csv](../research_handoff_package_v3_all_in_one/continuation_runs/next_pass_outputs/lithium_abszero_pairwise_timing_correction.csv)

Supporting provenance:
- [lithium_abszero_causal_pair_replay_summary.json](../research_handoff_package_v3_all_in_one/continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_summary.json)
- [causal_pair_replay_pass.py](../research_handoff_package_v3_all_in_one/continuation_runs/next_pass_outputs/causal_pair_replay_pass.py)
- [RUN_LEDGER_V4.csv](RUN_LEDGER_V4.csv)
- [EXPERIMENT_ARCHIVE_V4.md](EXPERIMENT_ARCHIVE_V4.md)

Appended experiment note:
- causal replay from logged pair channels without shell scaling
- exact onset targets preserved in the baseline ledger
- pair corrections shift corrected local times earlier

## Run 021 - exact partition closure

Status:
- missing transient run
- not present on disk during V4 consolidation

Primary record:
- [EXPERIMENT_ARCHIVE_V4.md](EXPERIMENT_ARCHIVE_V4.md)

Appended experiment note:
- exact-sum partition closure based on canonical replay
- preserved onset structure and final occupancies while balancing advanced and repaid mass exactly
- no byte-level artifacts were accessible during the V4 package build

## Run 022 - pair-memory adaptive closure

Status:
- present

Primary lab note:
- [pair_memory_adaptive_notes.md](../new_runs_v4/run_022/run_022_pair_memory_adaptive_closure/pair_memory_adaptive_notes.md)

Primary log and data record:
- [pair_memory_adaptive_history.csv](../new_runs_v4/run_022/run_022_pair_memory_adaptive_closure/pair_memory_adaptive_history.csv)
- [pair_memory_adaptive_comparison.csv](../new_runs_v4/run_022/run_022_pair_memory_adaptive_closure/pair_memory_adaptive_comparison.csv)
- [pair_memory_adaptive_ledger.csv](../new_runs_v4/run_022/run_022_pair_memory_adaptive_closure/pair_memory_adaptive_ledger.csv)

Supporting provenance:
- [pair_memory_adaptive_summary.json](../new_runs_v4/run_022/run_022_pair_memory_adaptive_closure/pair_memory_adaptive_summary.json)
- [run_022_pair_memory_adaptive_closure_pass.py](../new_runs_v4/run_022/run_022_pair_memory_adaptive_closure_pass.py)
- [run_022_pair_memory_adaptive_closure.zip](../raw_run_archives_v4/run_022_pair_memory_adaptive_closure.zip)

Appended experiment note:
- kept exact onset structure and exact final occupancies while replacing fixed support and reserve windows
- support weighting became pair-memory-guided and reserve repayment became traced-residence-guided
- advanced and repaid mass total: 0.000312612987
- closure residual: 0.000000000000e+00
- global baseline total and adaptive total: 5.519932630414

## Run 023 - parameter-correlation closure

Status:
- missing transient run

Primary record:
- [RUN_LEDGER_V4.csv](RUN_LEDGER_V4.csv)
- [EXPERIMENT_ARCHIVE_V4.md](EXPERIMENT_ARCHIVE_V4.md)

Appended experiment note:
- routing was derived from parameter-correlation structure rather than manual coefficients
- chat-reported closure residual: 0.0
- chat-reported transport increase with dominant support deficit x local slope behavior
- no disk artifacts were accessible during V4 consolidation

## Run 024 - correlation-derived zero-point closure

Status:
- missing transient run

Primary record:
- [RUN_LEDGER_V4.csv](RUN_LEDGER_V4.csv)
- [EXPERIMENT_ARCHIVE_V4.md](EXPERIMENT_ARCHIVE_V4.md)

Appended experiment note:
- zero-point redistribution was derived from the same correlation tensor as closure
- chat-reported closure residual: 0.0
- chat-reported zero-point residual: approximately -3.25e-19
- no disk artifacts were accessible during V4 consolidation

## Run 025 - lag-aware temporal tensor closure

Status:
- present

Primary lab note:
- [lag_tensor_notes.md](../new_runs_v4/run_025/run_025_lag_aware_temporal_tensor_closure/lag_tensor_notes.md)

Primary log and data record:
- [lag_tensor_history.csv](../new_runs_v4/run_025/run_025_lag_aware_temporal_tensor_closure/lag_tensor_history.csv)
- [lag_tensor_comparison.csv](../new_runs_v4/run_025/run_025_lag_aware_temporal_tensor_closure/lag_tensor_comparison.csv)
- [lag_tensor_ledger.csv](../new_runs_v4/run_025/run_025_lag_aware_temporal_tensor_closure/lag_tensor_ledger.csv)
- [lag_tensor_channel_detail.csv](../new_runs_v4/run_025/run_025_lag_aware_temporal_tensor_closure/lag_tensor_channel_detail.csv)

Supporting provenance:
- [lag_tensor_summary.json](../new_runs_v4/run_025/run_025_lag_aware_temporal_tensor_closure/lag_tensor_summary.json)
- [run_025_lag_aware_temporal_tensor_closure_pass.py](../new_runs_v4/run_025/run_025_lag_aware_temporal_tensor_closure_pass.py)
- [run_025_lag_aware_temporal_tensor_closure.zip](../raw_run_archives_v4/run_025_lag_aware_temporal_tensor_closure.zip)

Appended experiment note:
- routing law became explicitly temporal instead of same-index local correlation
- support and reserve channels selected best causal lags from bounded lag sets
- advanced and repaid mass total: 0.006957864640
- closure residual: 0.000000000000e+00
- zero-point residual: 0.000000000000e+00
- support recruitment favored delayed upstream and pair structure, while reserve repayment favored immediate donor and tail structure

## Run 026 - coherence-gated lag tensor closure

Status:
- present

Primary lab note:
- [coherence_gated_notes.md](../new_runs_v4/run_026/run_026_coherence_gated_lag_tensor_closure/coherence_gated_notes.md)

Primary log and data record:
- [coherence_gated_history.csv](../new_runs_v4/run_026/run_026_coherence_gated_lag_tensor_closure/coherence_gated_history.csv)
- [coherence_gated_comparison.csv](../new_runs_v4/run_026/run_026_coherence_gated_lag_tensor_closure/coherence_gated_comparison.csv)
- [coherence_gated_ledger.csv](../new_runs_v4/run_026/run_026_coherence_gated_lag_tensor_closure/coherence_gated_ledger.csv)
- [coherence_gated_channel_detail.csv](../new_runs_v4/run_026/run_026_coherence_gated_lag_tensor_closure/coherence_gated_channel_detail.csv)

Supporting provenance:
- [coherence_gated_summary.json](../new_runs_v4/run_026/run_026_coherence_gated_lag_tensor_closure/coherence_gated_summary.json)
- [run_026_coherence_gated_lag_tensor_closure_pass.py](../new_runs_v4/run_026/run_026_coherence_gated_lag_tensor_closure_pass.py)
- [run_026_coherence_gated_lag_tensor_closure.zip](../raw_run_archives_v4/run_026_coherence_gated_lag_tensor_closure.zip)

Appended experiment note:
- kept the lag-aware tensor but attenuated jittery lag winners through a coherence gate
- gate inputs were lag persistence, winner margin, and local channel strength
- advanced and repaid mass total: 0.002432865586
- closure residual: 0.000000000000e+00
- zero-point residual: 0.000000000000e+00
- transport reduced versus run 025 while onset and final targets remained locked

## Run 027 - temporal-coupling collapse probe

Status:
- present

Primary lab note:
- [collapse_probe_notes.md](../new_runs_v4/run_027/run_027_temporal_coupling_collapse_probe/collapse_probe_notes.md)

Primary log and data record:
- [collapse_probe_history.csv](../new_runs_v4/run_027/run_027_temporal_coupling_collapse_probe/collapse_probe_history.csv)
- [collapse_probe_summary.csv](../new_runs_v4/run_027/run_027_temporal_coupling_collapse_probe/collapse_probe_summary.csv)

Supporting provenance:
- [collapse_probe_summary.json](../new_runs_v4/run_027/run_027_temporal_coupling_collapse_probe/collapse_probe_summary.json)
- [run_027_temporal_coupling_collapse_probe_pass.py](../new_runs_v4/run_027/run_027_temporal_coupling_collapse_probe_pass.py)
- [run_027_temporal_coupling_collapse_probe.zip](../raw_run_archives_v4/run_027_temporal_coupling_collapse_probe.zip)

Appended experiment note:
- this pass did not alter the replay and instead analyzed whether lag and coherence observables behave like a collapse proxy
- all three outer bands showed sharp local acceleration in the proxy at onset
- immediate onset acceleration: 3 / 3
- quarter-peak within 8 steps: 2 / 3
- half-peak within 12 steps: 2 / 3
- interpretation: onset behaves like collapse buildup, but strongest single-vector dominance arrives later in the accessible logs

## Run 028 - winner-selection collapse operator

Status:
- present

Primary lab note:
- [winner_selection_notes.md](../new_runs_v4/run_028/run_028_winner_selection_collapse_operator/winner_selection_notes.md)

Primary log and data record:
- [winner_selection_history.csv](../new_runs_v4/run_028/run_028_winner_selection_collapse_operator/winner_selection_history.csv)
- [winner_selection_leaders.csv](../new_runs_v4/run_028/run_028_winner_selection_collapse_operator/winner_selection_leaders.csv)
- [winner_selection_summary.csv](../new_runs_v4/run_028/run_028_winner_selection_collapse_operator/winner_selection_summary.csv)

Supporting provenance:
- [winner_selection_summary.json](../new_runs_v4/run_028/run_028_winner_selection_collapse_operator/winner_selection_summary.json)
- [run_028_winner_selection_collapse_operator_pass.py](../new_runs_v4/run_028/run_028_winner_selection_collapse_operator_pass.py)
- [run_028_winner_selection_collapse_operator.zip](../raw_run_archives_v4/run_028_winner_selection_collapse_operator.zip)

Appended experiment note:
- replaced the smooth collapse proxy with an explicit winner-selection operator over 20 lag and channel candidates per outer band
- threshold crossing at onset or onset plus one: 0 / 3
- threshold crossing within 4 steps: 2 / 3
- same leader before and at onset: 2 / 3
- interpretation: r3 behaves closest to immediate point-vector lock, while r5 and r6 still show a short post-onset locking interval

## Run 029 - temporal-alignment probability collapse

Status:
- present

Primary lab note:
- [probability_collapse_notes.md](../new_runs_v4/run_029/run_029_temporal_alignment_probability_collapse/probability_collapse_notes.md)

Primary log and data record:
- [probability_collapse_history.csv](../new_runs_v4/run_029/run_029_temporal_alignment_probability_collapse/probability_collapse_history.csv)
- [probability_collapse_leaders.csv](../new_runs_v4/run_029/run_029_temporal_alignment_probability_collapse/probability_collapse_leaders.csv)
- [probability_collapse_summary.csv](../new_runs_v4/run_029/run_029_temporal_alignment_probability_collapse/probability_collapse_summary.csv)

Flattened direct extract duplicate:
- [probability_collapse_notes.md](../new_runs_v4/run_029_temporal_alignment_probability_collapse_direct/probability_collapse_notes.md)
- [probability_collapse_history.csv](../new_runs_v4/run_029_temporal_alignment_probability_collapse_direct/probability_collapse_history.csv)
- [probability_collapse_leaders.csv](../new_runs_v4/run_029_temporal_alignment_probability_collapse_direct/probability_collapse_leaders.csv)
- [probability_collapse_summary.csv](../new_runs_v4/run_029_temporal_alignment_probability_collapse_direct/probability_collapse_summary.csv)

Supporting provenance:
- [probability_collapse_summary.json](../new_runs_v4/run_029/run_029_temporal_alignment_probability_collapse/probability_collapse_summary.json)
- [run_029_temporal_alignment_probability_collapse_pass.py](../new_runs_v4/run_029/run_029_temporal_alignment_probability_collapse_pass.py)
- [run_029_temporal_alignment_probability_collapse.zip](../raw_run_archives_v4/run_029_temporal_alignment_probability_collapse.zip)

Appended experiment note:
- recast collapse as a probability collapse produced by temporal alignment fields and retro-admissibility instead of a measurement event
- threshold crossing at onset or onset plus one: 1 / 3
- threshold crossing within 4 steps: 3 / 3
- same leader before and at onset: 3 / 3
- interpretation: the winner is selected by temporal-alignment probabilities, while detector-facing lock still forms across a short interval rather than a mathematically instantaneous step

## Run 030 - NIST surrogate projection sanity check

Status:
- present

Primary lab note:
- [nist_surrogate_notes.md](../new_runs_v4/run_030/nist_surrogate_notes.md)

Primary log and data record:
- [nist_surrogate_summary.csv](../new_runs_v4/run_030/nist_surrogate_summary.csv)

Flattened direct extract duplicate:
- [nist_surrogate_notes.md](../new_runs_v4/run_030_nist_surrogate_projection_direct/nist_surrogate_notes.md)
- [nist_surrogate_summary.csv](../new_runs_v4/run_030_nist_surrogate_projection_direct/nist_surrogate_summary.csv)

Supporting provenance:
- [nist_surrogate_summary.json](../new_runs_v4/run_030/nist_surrogate_summary.json)
- [run_030_nist_surrogate_projection_pass.py](../new_runs_v4/run_030/run_030_nist_surrogate_projection_pass.py)
- [run_030_nist_surrogate_projection.zip](../raw_run_archives_v4/run_030_nist_surrogate_projection.zip)

Appended experiment note:
- asked whether the current toy lock structure can honestly be described as within the uncertainty scale of NIST lithium D-line measurements
- used the r5 to r6 onset spacing as a surrogate mapping to the D1 to D2 separation
- inferred scale: 773.905 MHz per step
- conclusion: one toy step is far too coarse for a metrology claim and the current state is nowhere near NIST uncertainty scale
- next requirement: sub-step or continuous timing readout plus physically meaningful SI projection and uncertainty bars

## Run 031 - interpolated temporal-alignment collapse

Status:
- present

Primary lab note:
- [interpolated_collapse_notes.md](../new_runs_v4/run_031/run_031_interpolated_temporal_alignment_collapse/interpolated_collapse_notes.md)

Primary log and data record:
- [interpolated_collapse_history.csv](../new_runs_v4/run_031/run_031_interpolated_temporal_alignment_collapse/interpolated_collapse_history.csv)
- [interpolated_collapse_candidates.csv](../new_runs_v4/run_031/run_031_interpolated_temporal_alignment_collapse/interpolated_collapse_candidates.csv)
- [interpolated_collapse_summary.csv](../new_runs_v4/run_031/run_031_interpolated_temporal_alignment_collapse/interpolated_collapse_summary.csv)

Flattened direct extract duplicate:
- [interpolated_collapse_notes.md](../new_runs_v4/run_031_interpolated_temporal_alignment_collapse_direct/interpolated_collapse_notes.md)
- [interpolated_collapse_history.csv](../new_runs_v4/run_031_interpolated_temporal_alignment_collapse_direct/interpolated_collapse_history.csv)
- [interpolated_collapse_candidates.csv](../new_runs_v4/run_031_interpolated_temporal_alignment_collapse_direct/interpolated_collapse_candidates.csv)
- [interpolated_collapse_summary.csv](../new_runs_v4/run_031_interpolated_temporal_alignment_collapse_direct/interpolated_collapse_summary.csv)

Supporting provenance:
- [interpolated_collapse_summary.json](../new_runs_v4/run_031/run_031_interpolated_temporal_alignment_collapse/interpolated_collapse_summary.json)
- [run_031_interpolated_temporal_alignment_collapse_pass.py](../new_runs_v4/run_031/run_031_interpolated_temporal_alignment_collapse_pass.py)
- [run_031_interpolated_temporal_alignment_collapse.zip](../raw_run_archives_v4/run_031_interpolated_temporal_alignment_collapse.zip)

Appended experiment note:
- replaced hard winner collapse with a top-two branch interpolation rule when temporal coherence climbs while internal consistency breaks
- this is the current frontier documented in the V4 archive and math stack
- reported outcome from the archive: r5 and r6 fractional threshold crossings occur within about one-third step of onset, while r3 is weaker because coherence climb was not yet active at onset
- next continuation logic should preserve onset targets, final occupancy targets, exact or near-exact closure bookkeeping, and interpolation when coherent temporal branches remain admissible but internally inconsistent