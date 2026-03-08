# Pass 9 / Later Series I — Vector & Spectra Identity Conversion (Tranche 1)

Canonical baseline: `GenesisEngine_repo_inherited_patch_v52_pass8_runtime_export_completion.zip`

This pass begins the later dedicated series `I1–I5` from the surgical execution contract. It does **not** start series `J*`.

## Scout classification against the active inherited line

| Item | Classification | Repo evidence | Pass 9 action |
|---|---|---|---|
| I1. Replace remaining hash/fingerprint identity fields | partial | Core determinism/runtime identity still used `state_fingerprint_*`; anchor inspection still used `harmonic_fingerprint`; multiple comments/UI strings still exposed fingerprint wording | Convert the canonical runtime/anchor identity surfaces to `signature` terminology and remove those old source-of-truth names from active code paths |
| I2. Replace summary construction logic in fanout / voxel coupling / state summary paths | partial | Several low-level summary lanes still use hash/digest wording, but many are telemetry summaries rather than source-of-truth identity | Leave low-level telemetry field migrations for a later tranche; rename project-level directory summary surface from digest to spectrum summary |
| I3. Convert coherence bus dedupe/routing to vector IDs only | missing | Coherence bus still comments and stores payload-hash based duplicate reduction | Not touched in this tranche |
| I4. Remove “fingerprint” whole-state naming and logic | partial | Whole-state determinism guardrail, selftests, and file names still centered on `fingerprint` wording | Converted whole-state naming, include/source file names, runtime fields, and selftest labels to `signature` |
| I5. Sync docs/comments/debug/UI strings after conversion | partial | UI/node strings and comments still had old fingerprint/digest wording in touched surfaces | Synced touched UI/comments/build references; large blueprint/spec upload docs remain for later documentation tranche |

## File-by-file implementation matrix

| File | Status | Reason | Pass 9 implementation |
|---|---|---|---|
| `include/GE_runtime.hpp` | partial | Runtime still exposed canonical state fingerprint fields and project digest summary names | Renamed to canonical state signature fields and project spectrum summary fields |
| `src/GE_runtime.cpp` | partial | Tick determinism guardrail and project-link summary path still used fingerprint/digest naming | Rewired guardrail + reference load path to signature naming; renamed project summary plumbing to spectrum summary |
| `include/GE_state_signature.hpp` | missing (new canonical name) | Prior active line still exposed `GE_state_fingerprint.hpp` | Introduced canonical signature header and removed old header from active include path |
| `src/GE_state_signature.cpp` | missing (new canonical name) | Prior active line still exposed `GE_state_fingerprint.cpp` | Introduced canonical signature implementation and removed old source from active build path |
| `include/harmonic_signature.hpp` | missing (new canonical name) | Prior active line still exposed `harmonic_fingerprint.hpp` | Introduced canonical harmonic signature header |
| `src/harmonic_signature.cpp` | missing (new canonical name) | Prior active line still exposed `harmonic_fingerprint.cpp` | Introduced canonical harmonic signature implementation |
| `src/GE_operator_registry.cpp` | partial | Trace/inspection path still included harmonic fingerprint naming | Switched to harmonic signature include + type/function names |
| `src/GE_ai_regression_tests.cpp` | partial | Determinism selftests still reported fingerprint terminology | Renamed selftest function/report labels to signature terminology |
| `vulkan_app/src/GE_app.cpp` | partial | Node/UI strings still surfaced project digest terminology | Renamed AI project digest surface and visible node strings to project spectrum summary |
| `CMakeLists.txt` | partial | Build list still referenced old source filenames | Updated canonical source file list to signature filenames |

## Validation plan for this pass

- Syntax-check `src/GE_runtime.cpp`
- Syntax-check `src/GE_operator_registry.cpp`
- Syntax-check `src/GE_state_signature.cpp`
- Syntax-check `src/harmonic_signature.cpp`
- Syntax-check `src/GE_ai_regression_tests.cpp`

## Explicit non-goals for this tranche

- No `J*` annotation-driven ingestion work
- No coherence bus dedupe/routing structural rewrite yet
- No repo-wide rewrite of every telemetry field named `hash` or every historical spec-upload reference
- No parallel identity system; this pass renames the canonical runtime identity path rather than adding a second one
