# Genesis apply-target command note

On the active reset continuation line, editor-facing artifact targeting uses `APPLY_TARGET:` wording rather than `HYDRATE:` wording.

Canonical rule:
- `APPLY_TARGET:<root>` is the user-facing command surface for selecting a write/apply root
- the emitted inspector artifact is `AI/apply_target_hint.txt`
- the emitted payload key is `APPLY_TARGET_ROOT=`

This is a terminology cleanup only. It does not change the underlying deterministic projection machinery, and it does not authorize automatic CPU encoding rollout.
