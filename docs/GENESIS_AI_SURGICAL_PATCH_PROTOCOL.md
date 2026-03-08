# Genesis Engine AI Surgical Patch Protocol

This document is the canonical contract for bounded AI-side file editing in the current repository.

## Priority order

1. **Coherence-anchored edits first.** If the logic region already has `EW_ANCHOR` markers, the patch path must target those anchors before attempting any text-fragment surgery.
2. **Exact-text surgery second.** Exact-match edits are allowed only when the target region does not yet expose usable coherence anchors.
3. **Append last.** Plain append is the weakest edit mode and should only be used for additive scaffolding, new file creation, or deliberately anchor-free tails.

This keeps surgical edits tied to the coherence view of where logic actually lives, instead of reducing the AI to blind string hacking.

## Supported patch directives

### Coherence-native directives

- `PATCH:<rel_path>:APPEND:<text>`
- `PATCH:<rel_path>:INSERT_AFTER:<anchor>:<text>`
- `PATCH:<rel_path>:REPLACE_BETWEEN:<anchor_a>:<anchor_b>:<text>`
- `PATCH:<rel_path>:DELETE_BETWEEN:<anchor_a>:<anchor_b>`

These directives operate on `EW_ANCHOR` markers and are the preferred path whenever anchor-bounded logic exists.

### Exact-text fallback directives

- `PATCH:<rel_path>:REPLACE_EXACT:<needle>:WITH:<text>`
- `PATCH:<rel_path>:DELETE_EXACT:<needle>`
- `PATCH:<rel_path>:INSERT_BEFORE_EXACT:<needle>:TEXT:<text>`
- `PATCH:<rel_path>:INSERT_AFTER_EXACT:<needle>:TEXT:<text>`

Exact-text edits require a **unique** match. If the match is absent or appears more than once, the patch must fail closed.

## Safety rules

- Paths must remain inside the repository root.
- Coherence anchor edits must use unique anchors.
- Exact-text edits must use unique exact matches.
- No filesystem-side patch may silently degrade from anchor edit to exact edit.
- No filesystem-side patch may silently degrade from exact edit to append.
- When coherence anchors exist for the target logic region, they are the authoritative edit boundary.

## Why this exists

The current AI stack already has deterministic routing, bounded repo reading, inspector artifacts, coherence gates, and review scaffolding. The biggest missing limb was a real surgical hand. This protocol is the minimal bounded bridge between append-only artifact emission and true exact-region editing.


## Coherence semantic locator

The coherence view is the **semantic patch locator**, not the source-of-truth editor.

Canonical chain:

`user intent -> planner resolves target logic cluster through coherence/anchor graph -> locator maps that cluster to canonical source spans / export nodes / artifact regions -> patch applies to canonical representation -> derived coherence view regenerates`

The current bounded implementation lives in `EwCoherenceGraph::query_semantic_patch_targets(...)` and is consumed by `EwCodeSynthesizer` before file-level fallback ranking.


## Semantic layer split

- coherence tells what is related
- coherent patch view tells what is in scope
- canonical binding tells where to write

The full chain is: `emergent coherence -> AI infers affected logic cluster -> AI forms a coherent patch view -> patch view resolves to canonical write targets -> edit is applied -> coherence view regenerates from the new state`.
