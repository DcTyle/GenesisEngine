# Pass 17 Reset Implementation Matrix

Status: implemented on reset line

Scope for this pass
- Clean editor-facing apply-target command language on the active line
- Remove the remaining direct `HYDRATE:` user command path from runtime command parsing
- Rename the emitted hint artifact to apply-target terminology
- Do not modify projection internals, CPU encoding, or Hilbert/calculus actuation semantics

Files touched
- `include/code_artifact_ops.hpp`
  - Renamed the public helper declaration from `code_emit_hydration_hint(...)` to `code_emit_apply_target_hint(...)`
  - Updated surrounding operator comments from hydrator wording to apply-projection wording
- `src/code_artifact_ops.cpp`
  - Renamed the helper definition to `code_emit_apply_target_hint(...)`
  - Renamed emitted artifact path from `AI/hydration_hint.txt` to `AI/apply_target_hint.txt`
  - Renamed emitted payload key from `HYDRATE_ROOT=` to `APPLY_TARGET_ROOT=`
- `src/GE_runtime.cpp`
  - Replaced the direct command parser branch `HYDRATE:` with canonical `APPLY_TARGET:`
  - Routed apply-target command handling to `code_emit_apply_target_hint(...)`
  - Updated nearby comments to speak in apply-projection language rather than hydrator wording
- `docs/GENESIS_APPLY_TARGET_COMMAND_NOTE.md`
  - Added a repo-local note pinning the reset-line terminology and scope

Validation
- `g++ -std=c++17 -fsyntax-only -Iinclude src/code_artifact_ops.cpp`
- `g++ -std=c++17 -fsyntax-only -Iinclude src/GE_runtime.cpp`

Deferred intentionally
- projection internals and CLI tool renaming
- CPU annotation/encoding rollout
- Hilbert/calculus operator-system schema
