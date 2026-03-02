# Genesis Engine Validation Gate (Canonical)

This document defines the validation gate that keeps learning and source trust grounded in measurable reality.

## Core rule

Only simulate (or accept) objects/experiments/events that produce measurable metrics that can be validated against ingested references.

The AI MUST NOT advance the curriculum stage unless required simulation metrics match targets within tolerance.

Default tolerance: **6% relative error** (per-metric), unless a stage explicitly defines a different tolerance.

Relative error definition:
`abs(sim - target) / max(abs(target), eps) <= 0.06`

## What counts as measurable

A metric is measurable if it is:
- well-defined and unit-consistent (or reducible to a unitless ratio),
- derivable from sources (papers/specs/standards) with explicit definitions,
- reproducible in the Genesis sandbox within tolerance.

Examples:
- QM: interference fringe spacing ratios; quantized energy level ratios; transition frequencies
- Orbitals/atoms: orbital energy ratios; spectral line positions; ionization potentials (relative)
- Bonds/chemistry: bond lengths; vibrational spectra peaks; reaction enthalpies/trends
- Materials: conductivity/thermal coefficients; strength-to-weight ratios; refractive indices bands
- Cosmology/environments: orbital periods; atmospheric scale height; radiative balance parameters

## Source trust consequence

If a source produces targets that cannot be matched within tolerance after bounded inverse-solving:
- mark it as low-trust for that metric family,
- do not let it steer curriculum progression,
- require additional sources or alternative metrics.

## Deterministic workflow

Per lesson checkpoint:
1) Select target metric vector `M_target` from ingested sources.
2) Run bounded inverse-solving to find parameters `p` such that `M_sim(p)` matches `M_target`.
3) Evaluate error deterministically (fixed quantization, fixed ordering).
4) If passed: commit checkpoint and allow progression.
5) If failed: quarantine source/metric, adjust parameterization or request better targets.

## Compute budget (per-domain request window)

The learner must use **all available compute** during the request window implied by
domain politeness cadence (nominally 1 second per domain at the default policy).

Rules:
- Do **not** early-stop when reaching the 6% tolerance.
- Continue searching toward 100% agreement until the window budget is exhausted.
- At window end, accept only if the *best fit found* is within tolerance.

Exception:
- If the initial parameter-driven "reverse-calc" attempt (parameter molding) yields
  a 100% match (zero error) on the first try, accept immediately and reuse the
  remaining request window budget for additional tasks.

Implementation note:
- The budget is expressed as conceptual "tries" (object updates) and consumed in
  batched steps to map to fan-out compression.

GPU-only requirement:
- The fitting/search loop must execute on the GPU (substrate microprocessor). The
  CPU may orchestrate network I/O and kernel dispatch, but must not perform the
  candidate iteration or evaluation loops.

