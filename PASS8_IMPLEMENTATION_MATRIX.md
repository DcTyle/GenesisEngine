# PASS 8 IMPLEMENTATION MATRIX

Scope constrained to contract Pass 8 only:
- G2 motion/character hook integration
- H1 runtime/editor split validation
- H2 export-language cleanup
- H3 whole-repo continuation materialization

## Classification against inherited active line

### G2 — Motion / character hook integration
Status before patch: **partial**

Already present in the active line:
- Character Tools controls existed in the asset authoring lane.
- Sequencer transport, stress overlay, and one motion-hook button existed.
- Bounded control-packet emission already existed for character bind / pose hook / motion hook.

Missing before patch:
- No canonical `RefreshSequencerPanel()` implementation was present on the active line.
- Sequencer UI did not present a coherent summary tying the selected lane to character settings, selected object/anchor, content-review state, and bounded packet semantics.
- Sequencer and character systems were wired by controls, but not surfaced as one readable operator-facing contract.

Implementation result:
- Added a canonical sequencer refresh path.
- Sequencer now exposes lane meaning, character motion parameters, selected object binding, content-review state, viewport mode, and the bounded control-packet contract in one summary.

Touched file:
- `vulkan_app/src/GE_app.cpp`

---

### H1 — Runtime / editor split validation
Status before patch: **partial**

Already present in the active line:
- Separate editor/runtime targets already existed in build/install wiring.
- Multiple UI summaries already stated that authoring panels remain editor-only.

Missing before patch:
- Export staging did not emit an explicit runtime/editor audit for staged target sets.
- Whole-repo / repo export preview did not clearly report when editor-facing paths were present and must remain excluded from runtime packaging.
- AI/status surfaces did not surface that audit as first-class staged state.

Implementation result:
- Added canonical staged-bundle audit fields for runtime-capable vs editor-facing targets.
- Export staging now emits an explicit runtime/editor audit summary.
- AI/chat and graph preview surfaces now expose that audit.

Touched files:
- `include/GE_runtime.hpp`
- `src/GE_runtime.cpp`
- `vulkan_app/src/GE_app.cpp`

---

### H2 — Export-language cleanup
Status before patch: **partial**

Already present in the active line:
- Node graph docs already preferred export terminology.
- Some UI summaries already used export/apply phrasing.

Missing before patch:
- Remaining user-facing AI/app strings still referred to hydration where export/apply or write-target wording was the correct operator-facing dialect.

Implementation result:
- Replaced remaining user-facing hydration wording in the AI panel/apply flow that was still incorrect for export/apply semantics.
- Reinforced export dialect in the node-graph doc addendum.

Touched files:
- `vulkan_app/src/GE_app.cpp`
- `docs/GENESIS_NODE_GRAPH_NATIVE_NODES.md`

---

### H3 — Whole-repo continuation materialization
Status before patch: **partial**

Already present in the active line:
- `export_whole_repo` node/palette terminology already existed.
- Backend staging already recognized whole-repo scope as a scope kind.

Missing before patch:
- Staged bundles did not expose a first-class operation label for continuation semantics.
- Whole-repo staging did not emit a continuation summary.
- AI panel/metadata did not present whole-repo continuation as a first-class staged operation.
- Graph export preview did not surface whole-repo continuation semantics cleanly.

Implementation result:
- Added canonical staged-bundle operation/continuation fields.
- Whole-repo staging now materializes as `whole_repo_continuation` with bounded continuation summary.
- AI panel state and patch metadata now surface the staged whole-repo continuation.
- Graph export preview now shows operation, continuation, and runtime/editor audit details.

Touched files:
- `include/GE_runtime.hpp`
- `src/GE_runtime.cpp`
- `vulkan_app/src/GE_app.cpp`
- `docs/GENESIS_NODE_GRAPH_NATIVE_NODES.md`

---

## File-by-file change list

### `include/GE_runtime.hpp`
- Extended `EwStagedExportBundle` with:
  - runtime/editor audit counts
  - whole-repo continuation flag
  - export dialect cleanliness flag
  - operation label
  - continuation summary
  - runtime/editor audit summary

### `src/GE_runtime.cpp`
- Added canonical helper classification for editor-facing staged paths.
- Added canonical operation labels for staged export scope kinds.
- Upgraded bundle staging to populate:
  - operation label
  - whole-repo continuation marker
  - runtime/editor audit counts and summary
  - continuation summary by scope kind
- Upgraded stage summary text to include continuation semantics.

### `vulkan_app/src/GE_app.cpp`
- Added canonical `RefreshSequencerPanel()` implementation.
- Sequencer panel now reports lane meaning, character motion binding, selected object/anchor, content-review state, viewport/live context, and runtime/editor split semantics.
- AI panel state now surfaces staged export / whole-repo continuation status.
- Patch metadata now includes staged export/continuation audit information.
- Node export preview now surfaces operation, continuation, and runtime/editor audit details.
- Replaced remaining user-facing hydration wording with export/apply wording where appropriate.

### `docs/GENESIS_NODE_GRAPH_NATIVE_NODES.md`
- Added Pass 8 addendum for whole-repo continuation staging and runtime/editor audit expectations.

---

## Validation

Validated in container:
- `g++ -std=c++17 -fsyntax-only -Iinclude src/GE_runtime.cpp`

Not runnable in this Linux container:
- Native validation for `vulkan_app/src/GE_app.cpp` depends on Win32 headers/libraries not present here.

Structural/manual validation completed:
- confirmed `RefreshSequencerPanel()` now exists on the active inherited line
- confirmed staged export bundle now carries operation/continuation/runtime-audit state
- confirmed AI panel state text surfaces staged whole-repo continuation
- confirmed user-facing export/apply wording replaced the remaining hydration strings in the affected flow
