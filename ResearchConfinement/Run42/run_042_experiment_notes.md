# Run 042 Experiment Notes

## Purpose
Extend the Run40/Run41 lattice-control sequence with a frequency-domain-only packet simulation and formal review package.

## Method
1. Reused Run41 cohort seed controls for `D_track`, `I_accum`, `L_smooth`.
2. Injected simulation-only GPU pulse quartet `(F=0.245, A=0.18, I=0.33, V=0.33)`.
3. Executed 160 temporal steps at 128 bins, 50 packets, 512 reconstruction samples.
4. Aggregated per-task trajectories and lock/coherence metrics into Run-style summaries.

## Results
1. Shared-vs-individual packet split remained mixed (23 shared / 27 individual), preserving falsifiability.
2. Temporal coherence stayed high while curvature remained non-zero across all cohorts.
3. Conservation stayed finite (non-zero drift) and therefore does not hide leakage by construction.

## Deviations / Risk Notes
1. This remains a simulation surrogate and is not a metrology-equivalent silicon atom reconstruction.
2. Scores are derived from run-internal operators, not externally fitted constants.
3. Any downstream claim must still be cross-examined against independent physical benchmarks.
