# Genesis Engine Coherence Patch Locator

The coherence view is the semantic targeting surface for AI-side editing. It is **not** the source-of-truth editor.

Canonical chain:

User intent
-> planner resolves target logic cluster through coherence / anchor graph
-> semantic patch locator maps that cluster to exact canonical source spans / export nodes / artifact regions
-> patch applies to canonical source/export representation
-> derived coherence view regenerates from the changed state

## Rules

1. Coherence view remains derived-only.
2. Canonical source/export representation remains authoritative.
3. Anchor-bounded regions are preferred over exact-text surgery.
4. Exact-text surgery is fallback-only when the target logic region has no usable anchor span.
5. Append remains last-resort for additive scaffolding or new-file emission.

## Current bounded implementation

The current semantic patch locator is built inside `EwCoherenceGraph`.

It:
- rebuilds from canonical inspector artifacts,
- extracts `EW_ANCHOR` regions from code / CMake artifacts,
- scores anchor-bounded regions against a request,
- returns patch targets that point back to canonical artifacts and anchor spans.

Each target carries:
- `rel_path`
- `patch_mode_u16`
- `anchor_a`
- `anchor_b`
- `reason_utf8`
- `score_u32`

## Interpretation

- `EW_PATCH_REPLACE_BETWEEN_ANCHORS` means the target logic cluster resolved to a bounded anchor region.
- `EW_PATCH_INSERT_AFTER_ANCHOR` means the locator found an anchor-bounded insertion point but not a full replacement span.
- Fallback patch modes remain legal only when the coherence/anchor graph does not expose a usable region.

## Synthesizer behavior

`EwCodeSynthesizer` must query semantic patch targets before dropping to artifact-level file ranking.
When a usable anchor target exists, the synthesizer should emit a coherence-located patch against that canonical artifact region and report the locator reason in UI/log output.

## Non-goals

- The coherence view does not directly mutate state.
- The node graph does not become the source-of-truth editor.
- The semantic patch locator does not replace compile/validate loops.


## Semantic layer split

- coherence tells what is related
- coherent patch view tells what is in scope
- canonical binding tells where to write

The full chain is: `emergent coherence -> AI infers affected logic cluster -> AI forms a coherent patch view -> patch view resolves to canonical write targets -> edit is applied -> coherence view regenerates from the new state`.
