# Genesis SYNTHCODE Canonical Note

SYNTHCODE on the active continuation line must not emit placeholder function stubs.

Canonical behavior:
- complete module/tool artifacts are allowed
- coherent patch views and binding reports are allowed
- synthesized function stubs are refused fail-closed

Reason:
- the surgical patch contract requires one canonical source-of-truth path
- placeholder stubs create parallel fake progress and violate the no-stubs rule already present in runtime comments
- diff/apply flow should promote complete, reviewable edits rather than planting inert trench-coat code into canonical files

Deferred boundary:
- this note does not decide Hilbert/calculus actuation semantics
- this note does not restart CPU-file encoding/annotation rollout
