# Patch Policy (Surgical Edits)

- A patch is **additive** (new files) or **surgical overwrite** (replace whole file bytes) of specific targets.
- No "reformat everything" passes unless explicitly required.
- Each patch should include a short changelog in `Draft Container/GenesisEngine/CHANGELOG.md`.

## Determinism
- UTF-8 validation is strict: reject unsafe control bytes deterministically.
- Case-folding for search/tokenization remains ASCII-only for deterministic behavior.

## Packaging
- Every new zip is created from the last zip using `scripts/release_from_base.ps1`.
- `release_manifest.json` is generated for each release.
