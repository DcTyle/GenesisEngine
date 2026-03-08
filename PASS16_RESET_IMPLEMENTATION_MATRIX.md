# PASS 16 RESET IMPLEMENTATION MATRIX

Scope: non-calculus structural cleanup only. No CPU-file encoding rollout. No Hilbert/calculus schema commitment.

## Classification
- `src/code_synthesizer.cpp` — partial/superseded. The active line still allowed synthesized function stubs even though runtime comments and the surgical contract already reject stub-based progress.
- `docs/GENESIS_SYNTHCODE_CANONICAL_NOTE.md` — missing. Needed a repo-local note to pin the canonical behavior on the reset line.

## Implemented in this pass
- Replaced direct synthesized-function-stub emission with one canonical refusal/reporting helper in `src/code_synthesizer.cpp`.
- Preserved coherent patch-view generation and candidate/binding reporting.
- Kept complete module/tool artifact generation intact.
- Added `docs/GENESIS_SYNTHCODE_CANONICAL_NOTE.md` to record that SYNTHCODE may emit complete artifacts and patch views, but not placeholder function stubs.

## Validation
- `g++ -std=c++17 -fsyntax-only -Iinclude src/code_synthesizer.cpp`
