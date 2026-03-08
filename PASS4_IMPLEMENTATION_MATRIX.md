# Pass 4 Implementation Matrix

Scope executed from the contract:
- C2 connection-preview clarity
- C3 export lane backend materialization
- D1 live mode integration polish

## include/GE_runtime.hpp
Status: patched

Changes:
- Extended `EwProjectLinkEntry` with bounded retained relative-path storage so export staging can bind to the linked project/work substrate instead of floating as UI text.
- Added one canonical staged-export model:
  - `EwStagedExportTarget`
  - `EwStagedExportBundle`
- Added one bounded staged-export history ring in the runtime state.
- Added canonical runtime APIs:
  - `ui_stage_node_export_bundle(...)`
  - `ui_snapshot_latest_export_bundle(...)`

Why:
- C3 required staged export objects, not just summaries.
- This keeps export staging inside the existing runtime/source-of-truth path.

## src/GE_runtime.cpp
Status: patched

Changes:
- Persisted linked-project relative paths during `ui_link_chat_project(...)`.
- Added deterministic export-scope classification.
- Added deterministic language/lock resolution from file role + extension + policy hints.
- Implemented canonical export staging bound to the linked project substrate.
- Implemented latest-bundle snapshot retrieval.

What is now materialized:
- file artifact export
- repo patch export
- whole-repo export
- AI language auto-suggestion policy staging
- per-file language override staging
- locked-language constraint enforcement

Why:
- This closes C3 without adding a second export system.

## vulkan_app/src/GE_app.cpp
Status: patched

Changes for C2:
- Invalid connection attempts now surface the actual incompatibility explanation instead of a generic refusal.
- Successful connect/spawn status now carries the derived edge label and explicit disconnect affordance.
- Node summary now includes pre-commit edge preview, derived-effect summary, and export/language implications before commit.
- Disconnect feedback now states exactly what edge was cleared.

Changes for C3:
- `Preview Export` now stages a real bounded export bundle through runtime APIs.
- Export preview now reports:
  - stage id
  - stage status
  - bound substrate root
  - staged targets
  - effective language per target
  - locked-language reasons where applicable

Changes for D1:
- Live mode now writes a unified status line into the tool-status surface.
- Node summary now reflects coherent live-mode context rather than only the viewport mode label.

Why:
- C2 required deliberate, inspectable connection behavior.
- C3 required backend materialization visible from the graph.
- D1 required live mode to feel integrated rather than menu-toggled.

## Validation
Passed:
- `g++ -std=c++17 -fsyntax-only -Iinclude src/GE_runtime.cpp`

Not runnable in this container:
- `vulkan_app/src/GE_app.cpp` syntax check requires Win32 headers (`windows.h`), which are unavailable in the Linux container.

## Pass-4 contract outcome
Implemented:
- C2 pre-commit connection clarity
- C3 staged export bundle backend materialization
- D1 live-mode integration polish

Not started:
- D2+
