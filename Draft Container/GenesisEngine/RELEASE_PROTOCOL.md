# Release Protocol (Deterministic, Inherit-From-Previous)

Rule: Every release zip MUST be produced by expanding the previous release zip and applying a surgical patch (file adds/overwrites only). No "fresh export" releases.

This repo includes two guardrails:

1) **Delta verifier**: prevents accidental file loss.
- `scripts/verify_release_delta.py`
- `scripts/verify_release_delta.ps1`

2) **Release-from-base builder**: enforces inherit-from-previous.
- `scripts/release_from_base.ps1`

## Standard Release Steps (Win64)

1. Put your edits in a patch directory that mirrors repo root.

2. Build the new zip from the previous zip:

```powershell
.\Draft Container\GenesisEngine\scripts\release_from_base.ps1 `
  -BaseZip .\Eigenware_U166_VulkanIntegrated.zip `
  -PatchDir .\_patch_U167 `
  -OutZip  .\Eigenware_U167.zip
```

This will:
- Expand the base zip
- Apply patch files (overwrite/add)
- Generate `release_manifest.json`
- Verify no unexpected removals (only allowed removals listed in `scripts/allowed_removed_paths.txt`)
- Pack the new zip

## Notes
- No Unicode identifiers in code. UTF-8 is allowed for content strings and docs.
- Symbol compaction (λ, φ, …) is allowed only in docs and only outside code fences.
- The verifier is the "no missing files" gate. If you intentionally remove something new, add its prefix to `scripts/allowed_removed_paths.txt`.
