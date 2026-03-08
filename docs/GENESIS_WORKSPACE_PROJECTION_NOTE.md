# Genesis Engine Workspace Projection Note

On the active reset continuation line, the headless workspace-write tool and nearby operator-facing comments use **projection** language rather than **hydration** language where the operation is an explicit inspector-to-file write.

Scope of this note:
- rename the headless tool target to `ew_project_workspace`
- rename its CLI/status text to projection wording
- keep internal runtime/storage structs unchanged for now where broader type renames would create unnecessary churn
- do not change the separate `rehydrate` validation/replay terminology used for state checking

This is a vocabulary/source-of-truth cleanup only. It does not introduce a second file-write path, a compatibility alias, or any CPU-file encoding rollout.
