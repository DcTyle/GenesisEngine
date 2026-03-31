# Canonical Runtime Merge Report

Date: 2026-03-30
Canonical branch: `codex/canonical-runtime-merge`

## Goal

Unify `master`, `codex/model-silicon-photon-confinement`, `main`, and `unifiedpatches`
into a single canonical branch while preserving the current runtime behavior and
the subsystem/calculus integration added in the protected photonic branch.

## Precedence

Working-tree precedence was applied in this order:

1. `master`
2. `codex/model-silicon-photon-confinement`
3. `main`
4. `unifiedpatches`

Higher-precedence branches retained control of the runtime-facing tree whenever
lower-precedence branches diverged in ways that could alter the current engine
behavior, logs, or calibration path.

## Merge Strategy

- `codex/model-silicon-photon-confinement` was merged normally.
- `main` was merged with the `ours` strategy to preserve commit history without
  replacing the higher-precedence runtime tree.
- `unifiedpatches` was already reachable through the merged ancestry after the
  `main` merge, so no additional tree change was required.

## Preservation Notes

- The current runtime tree remains on the `master` plus protected photonic
  integration path.
- The divergent content from `main` and `unifiedpatches` is preserved in git
  history and is reachable from the canonical branch.
- This approach keeps branch content recoverable without applying lower-
  precedence runtime changes into the live tree.

## Recovery Examples

Use these commands from the canonical branch when a lower-precedence file needs
to be inspected or ported safely:

```powershell
git show main:path/to/file
git show unifiedpatches:path/to/file
git checkout main -- path/to/file
git checkout unifiedpatches -- path/to/file
```

Any future port should remain surgical and additive unless the runtime impact is
explicitly reviewed.
