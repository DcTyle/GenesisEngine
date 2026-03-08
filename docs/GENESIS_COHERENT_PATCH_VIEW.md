# Genesis Coherent Patch View

This document defines the semantic scope layer between coherence relations and canonical write binding.

## Chain

Emergent coherence -> AI infers affected logic cluster -> AI forms a coherent patch view -> canonical binding resolves deterministic write targets -> edit is applied -> derived coherence view regenerates.

## Distinctions

- Coherence tells what is related.
- Coherent patch view tells what is in scope.
- Canonical binding tells where to write.

## Implementation Rules

1. The coherent patch view is not the patch itself.
2. It must preserve ranked semantic candidates before any write occurs.
3. It must record the chosen canonical binding, including file, anchors, patch mode, and rationale.
4. If no anchor-bounded target exists, fallback file ranking may be used, but that must be recorded explicitly as fallback binding.
5. The coherence view remains derived-only and is regenerated after edits.

## Current Repo Behavior

The synthesizer emits a bounded markdown artifact under `docs/ai_patch_views/` describing:

- the originating request,
- semantic patch candidates,
- fallback file ranking,
- the chosen canonical binding,
- and the contract reminder for coherence vs scope vs write target.

This artifact is canonical evidence for why a patch target was selected.
