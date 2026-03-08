# Genesis Engine — Workspace Projection Receipt Note

On the active reset continuation line, the runtime-facing observable fields and helper used for explicit inspector-to-file writes use **projection** naming rather than **hydration** naming.

This pass does **not** change the separate deterministic rehydration machinery used for replay/state validation. It only aligns the runtime-visible workspace-write surface with the operator-facing projection/apply vocabulary already adopted by the tool and UI cleanup passes.

Canonical runtime-facing names on this line:
- `project_workspace_to(...)`
- `last_projection_receipt`
- `last_projection_error_code_u32`

Do not regress these runtime-facing projection surfaces back to hydration wording unless the operator model itself changes.
