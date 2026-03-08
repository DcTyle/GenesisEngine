# Genesis Engine Surgical Patch Contract — Scout Classification

Source of truth: `GenesisEngine_repo_inherited_patch_v52.zip` as unpacked for this pass.
Contract source: `Genesis_Engine_Surgical_Patch_Execution_Contract.md`.

## Repo-level scout summary

The inherited repo already contains meaningful work for the AI operating layer, coherence lookup, inspector artifact patching, repo/coherence browsing, node-graph documentation, substrate/viewport hooks, asset surfaces, and runtime/editor scaffolding. The main gap for Pass 1 was not absence of patching primitives; it was absence of one canonical workflow/session state above those primitives.

Concrete repo evidence used for classification:

- AI/chat/project substrate surfaces already existed in `include/GE_runtime.hpp` and `src/GE_runtime.cpp` via chat memory, project linking, repo-reader snapshots, coherence snapshots, and UI output projection.
- Semantic/coherence patch binding already existed in docs and code via `docs/GENESIS_AI_SURGICAL_PATCH_PROTOCOL.md`, `docs/GENESIS_COHERENCE_PATCH_LOCATOR.md`, `docs/GENESIS_COHERENT_PATCH_VIEW.md`, `include/coherence_graph.hpp`, and patch application in `src/code_artifact_ops.cpp`.
- A weaker/superseded code-synthesis path exists in `src/code_synthesizer.cpp`, while the active repo line uses the fail-closed `ew_synthcode_execute(...)` path in `src/GE_runtime.cpp`.
- Patch application existed, but post-apply validation feedback was limited to internal coherence gating and boolean success/failure, not canonical workflow/session reporting.

## Roadmap item classification

### Phase A — AI operating layer

- A1 canonical AI workflow state — **partial**
- A2 per-chat patch session history — **missing**
- A3 post-apply validation loop — **partial**
- A4 anchor-first targeting visibility — **partial**
- A5 planner/task-graph layer — **missing**

### Phase B — coherence-first surgical planning/binding

- B1 deterministic ambiguous bind resolution — **partial**
- B2 bounded patch-target validation artifacts — **missing**
- B3 queue/triage reintegration — **missing**

### Phase C — node graph authoring/export surface

- C1 explicit pin-choice behavior — **partial**
- C2 connection-preview clarity — **partial**
- C3 export lane backend materialization — **partial**
- C4 docs/search/UI/backend lockstep for node families — **partial**

### Phase D — live substrate / viewport polish

- D1 live mode integration polish — **partial**
- D2 resonance / anchor visual strengthening — **partial**
- D3 node-to-viewport resonance linkage — **partial**

### Phase E — asset / voxel / builder path

- E1 Voxel Designer deepening — **partial**
- E2 builder/designer unification — **partial**
- E3 canonical coherence/reference safety path — **partial**

### Phase F — content / repo / coherence UX tightening

- F1 AI explanation -> repo/coherence navigation — **partial**
- F2 rename/reference review consistency — **partial**
- F3 repo-index logic consolidation — **partial**

### Phase G — sequencer / motion systems

- G1 sequencer usable behavior — **partial**
- G2 motion/character hook integration — **partial**

### Phase H — runtime / export / build completion

- H1 editor-free runtime path validation — **partial**
- H2 export-language cleanup — **partial**
- H3 whole-repo continuation export/application concept — **missing**

### Later series I — vector/spectra identity conversion

- I1 replace remaining hash/fingerprint identity fields — **partial**
- I2 summary construction logic conversion — **partial**
- I3 coherence-bus dedupe/routing to vector IDs only — **partial**
- I4 remove fingerprint whole-state naming/logic — **partial**
- I5 sync docs/comments/debug/UI strings after conversion — **partial**

### Later series J — annotation-driven CPU-to-substrate ingestion

- J1 strict top-of-file annotation schema — **missing**
- J2 canonical ingestion runner — **missing**
- J3 canonical substrate routing table — **missing**
- J4 reuse canonical symbol/spectral encoding infrastructure — **partial**
- J5 annotate first migration tranche only — **missing**
- J6 deterministic reporting and conflict handling — **missing**
- J7 deprecation workflow, not deletion workflow — **missing**

## Supersession notes discovered during scouting

These are not roadmap items themselves, but they matter for surgical patching discipline:

- `src/code_synthesizer.cpp` contains a broader older synthesis/reporting path. The active inherited repo line is functionally centered on `ew_synthcode_execute(...)` in `src/GE_runtime.cpp`, which is stricter and fail-closed. For this pass, that older path was treated as **superseded for active workflow wiring**, not revived.
- The contract’s older manual CPU-to-substrate migration idea is already explicitly superseded by the J-series annotation-driven ingestion plan.
