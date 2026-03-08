- v43: AI panel embedded Repository view now preserves selection across refreshes and opens selected files on double-click via the OS handler.
## 2026-03-06 — v41 AI panel section polish

- tightened embedded AI-panel Repository/Coherence tool presentation with explicit section headers
- added edit-control inner margins for chat/tool text surfaces to improve readability
- updated embedded tool layout spacing so labels and actions read like editor sections instead of stacked raw controls
- switched Safe Mode menu feedback from chat spam to the AI-panel tool status strip
- focused the active control when switching AI-panel views (chat input, repo list, coherence query)

- v40: AI panel embedded tools polish. Added shared AI-panel tool status strip for Chat/Repository/Coherence views, stopped app-control toggles from appending non-conversational status spam into chat, and removed dead legacy coherence menu command branches now superseded by embedded tool views.
## 2026-03-06 — v37 coherence tools GUI consolidation
- Added a dedicated GUI-only **Coherence Tools** dialog launched from the AI panel menu.
- Consolidated coherence stats/search/rename-plan/rename-patch/selftest/highlight actions into one widget-driven tool surface instead of scattering results through chat prompts.
- Kept repository browsing and coherence highlighting on structured runtime snapshots; chat remains conversational while tool control stays in GUI panels.

- v36: Repository Browser GUI now includes structured coherence details for the selected file plus a direct "Highlight in Coherence" action; no chat/log scraping for this panel path.
## v35 - repository browser GUI panel
- Added structured repo-browser runtime helpers for rescan, status, bounded file listing, and bounded preview.
- Added a GUI-only Repository Browser dialog from the AI panel menu.
- Repository browsing now uses direct structured runtime access instead of command/log flows.

- v34: replaced vault chat-dump button flow with a structured GUI vault browser dialog; added structured vault preview/import helpers and removed app-surface dependence on VAULT_* text lines for this tool surface.
## v32 — direct bootstrap/train-ready runtime cleanup

- Removed obsolete `GAMEBOOT:` observation-string handling from the runtime loop; the Win64 Vulkan host already uses `ui_request_game_bootstrap(...)` directly.
- Kept AI chat conversational while reducing command-era echo: `ui_emit_ai_model_train_ready(...)` now emits a user-facing status line without pretending the action itself is a text command.
- Preserved `GenesisEngine/` as the only deliverable zip root.

## 2026-03-06 — v31 corpus/tool root-path cleanup
- Removed remaining active `Draft Container/...` path assumptions from runtime corpus, simulation, determinism, and code-generation surfaces.
- Normalized runtime inspector/corpus artifacts to `Corpus/`, simulation persistence to `Sim/`, determinism reference loading to `Determinism/`, generated tool outputs to `Generated/`, and source tool scaffolding to repo-root relative paths.
- Kept `GenesisEngine/` as the only deliverable zip root and continued pruning live wrapper-layout fossils that could leak into GUI/AI workflows.

## 2026-03-06 — v30 GUI-facing asset path cleanup
- Fixed AI experiment double-click open paths in the Vulkan editor host to respect the configured asset substrate root instead of hardcoding `Draft Container/AssetSubstrate/...`.
- Fixed vault import output handles to write under `<asset_substrate_root>/Assets/AIImported/...` instead of `Draft Container/AssetSubstrate/Assets/AIImported/...`.
- Preserved `GenesisEngine/` as the sole archive root and kept the cleanup focused on active GUI/editor-facing surfaces.

# Genesis Engine Changelog

## 2026-03-06 — Asset substrate + vault taxonomy scaffold

- Added deterministic `Vault/` partition to the asset substrate scaffold.
- Moved AI mirror surface under `AssetSubstrate/Vault/AI`.
- Expanded asset substrate schema for authored assets:
  - `Assets/Objects`
  - `Assets/Meshes`
  - `Assets/Textures`
  - `Assets/UV`
  - `Assets/Voxels`
  - `Assets/Materials/Mixer`
  - `Assets/Materials/Designer`
  - `Assets/Materials/SurfaceProfiles`
  - `Assets/Materials/Compositions`
  - `Assets/Materials/PeriodicTable/{Particles,Atoms,Compounds,DNA}`
- Expanded vault schema for AI mirrors and material reference lanes.
- Asset index rebuild now includes `.geassetref` mirror artifacts.
- Runtime asset substrate initialization now honors project settings roots instead of hard-coded paths.
- Added roadmap note: `docs/roadmaps/AssetSubstrate_Vault_Taxonomy_Phase1.md`.


## v11 goblin pass
- Reconciled partial camera/render packet schema drift.
- Added missing runtime packet wrappers for render assist and XR eye projection.
- Began SubstrateManager/SubstrateMicroprocessor API reconciliation without aliasing behavior changes.
- Added missing project-setting fields referenced by autofocus/LOD logic.
- Patched asset substrate fallback writer with explicit hook for future exact-path serializer integration.
- Reduced a cluster of stale field-name/runtime mismatches in GE_runtime.cpp.


## v12 partial stabilization pass
- Fixed CMake option ordering so CPU-only configuration no longer forces CUDA toolkit discovery before the option is defined.
- Repaired malformed `.geasset` writer output in `GE_asset_substrate.cpp` and aligned serialized fields with the current `EwObjectEntry` schema.
- Patched voxel coupling min/clamp calls to remove missing helper dependency.
- Reconciled part of Fourier fanout drift: helper declarations added early, duplicate band helper neutralized, and stale `tick_u64` references switched to `canonical_tick`.
- Reconciled most `nbody` member references to the current `nbody_state` storage.
- Validation status: CPU-only core configure now succeeds with `-DEW_ENABLE_CUDA=OFF -DEW_BUILD_VULKAN_APP=OFF`, but build still stops on deeper AI-vault / metric-task schema drift.


## v13 metric-vault schema reconciliation
- Restored the canonical `MetricTask` claim/work fields required by the current metric template and vault commit pipeline (`has_claim_u32`, embedded `MetricClaim`, and `declared_work_units_u32`).
- Fixed malformed metric JSON serialization in `GE_ai_vault.cpp` so claim metadata now emits as valid deterministic JSON text.
- Removed leaked resonant-page event code from `AiVault::init_once()` that referenced out-of-scope symbols and broke compilation.
- Corrected metric commit event logging to use `t.target.kind` instead of a non-existent `t.kind`.
- Corrected resonant-page commit logging to report the actual committed byte length (`len_u32`) and the correct event channel.
- Validation status: `src/GE_metric_templates.cpp` and `src/GE_ai_vault.cpp` both pass `g++ -std=c++17 -Iinclude -fsyntax-only` after this patch.


## v14 surgical build-continuity pass
- Fixed namespace drift in `GE_language_foundation.cpp` and `GE_prosody_planner.cpp` so speech bootstrap / prosody sources compile again.
- Reconciled `ew_substrate_manager.cpp` to the live `nbody_state` schema.
- Patched `CMakeLists.txt` so always-needed CPU-visible sources are actually linked into `EigenWareCore`.
- Added deterministic CPU-only linkage stubs for field lattice GPU, learning-gate CUDA binding, and Assimp export unavailability so CPU-only validation can advance without CUDA/Assimp hard failures.
- Fixed `self_patch_loop_main.cpp` compile drift (`<algorithm>`, KV parsing, key normalization).
- Validation improved: `EigenWareCore`, `test_determinism`, and several tool targets now link in CPU-only mode; remaining compile blockers moved to `ge_corpus_ingest_main.cpp` tool drift.


## v15 corpus-ingest surgical reconcile
- Fixed `src/tools/ge_corpus_ingest_main.cpp` drift without changing pipeline behavior: added missing `<algorithm>` include, added the required `GE_corpus_pulse_log.hpp` include, and corrected the stale ASCII-lower call to the live `ew::ew_ascii_lower_inplace` helper.
- Kept the ingest path strict/no-fallback: CUDA-only ingest guard, existing canonicalization path, existing SpiderCode4/carrier collapse, and existing pulse-log/store serialization remain unchanged.
- Validation status: `src/tools/ge_corpus_ingest_main.cpp`, `src/GE_phase_current.cpp`, and `src/GE_voice_predictive_model.cpp` pass `g++ -std=c++17 -Iinclude -fsyntax-only`.
- CPU-only build validation progressed past the prior ingest-tool blocker; subsequent full-target builds in this environment were terminated during later heavy core compilation rather than on a new syntax/schema failure.


## v16 GPU-only build policy
- Removed CPU-only build routing from `CMakeLists.txt`; CUDA toolkit is now mandatory and the build defines `EW_ENABLE_CUDA=1` unconditionally.
- Removed the CPU-only linkage stubs added in v14 (`field_lattice_gpu_stub.cpp`, `learning_gate_cuda_stub.cpp`, `assimp_fbx_io_stub.cpp`) because the repo no longer advertises or supports non-CUDA builds.
- Kept the runtime orchestration model intact: GPU kernels remain the compute path, CPU code remains control/orchestration only, and no fallback execution path was added.
- Preserved the optional Vulkan-app toggle; this pass only removes non-CUDA build support, not editor/runtime feature gating.


## 2026-03-06 — v20 GUI-only Win64-host patch + root repack
- Repacked deliverable to use `GenesisEngine/` as the single zip root; removed redundant wrapper folders from archive layout.
- Removed transient build artifacts and build directories before packaging.
- Continued GUI-only control-surface cleanup for Phase 5.6 by replacing app-side slash-command routing with direct editor/runtime calls for content, coherence, repo-reader, and vault browser actions.
- Blocked slash-command control entry in the AI chat box; patch/apply/preview remain widget-driven.
- Preserved the Win64 Vulkan app host path instead of introducing any new parallel widget shell.

- v21: Continued GUI-only control-surface cleanup: removed app-side GAMEBOOT and AI model-train-ready command-string routing, replacing them with direct runtime GUI helpers.


- v22: Added a dedicated GUI chat-message runtime path so the dockable AI panel preserves normal conversation without app-surface slash-command parsing; app-side GUI helpers for content/coherence/vault now execute direct runtime logic instead of round-tripping through ui_submit_user_text_line.

- v23: Wired AI panel Send action to the dedicated chat-message path (`SubmitAiChatLine`) instead of the generic UI command path, preserving conversational chat while keeping app control on GUI widgets.

- v24: removed the last app-side generic UI submit wrapper from the Vulkan host, moved boot observation onto a dedicated system-observation path, and kept AI chat/control separated (chat remains conversational; control remains widget-driven).

- v25: Content Browser moved off parsed CONTENT_LIST/CONTENT_ITEM UI text and now hydrates from structured runtime snapshots; GUI refresh/search remain widget-driven.

- v26: Replaced AI-panel coherence stats/query/rename-plan result projection from runtime text output with structured runtime snapshots; the dockable chat panel now formats those GUI-requested results directly instead of scraping command-style output lines.

- v27: moved AI vault browser button to structured GUI snapshots (no runtime text spray for the app surface), normalized this surface from `Draft Container/AI_Vault` to `AI_Vault`, and kept the dockable AI panel conversational while vault browsing remains button-driven.

2026-03-06 — v28 structured path baseline cleanup
- Normalized project settings defaults away from `GenesisEngine/...` to repo-rooted `GenesisEngine/...` relative paths (`ProjectSettings`, `AssetSubstrate`, `AssetLibraryCache`).
- Updated runtime project-settings boot load path to `ProjectSettings/project_settings.ewcfg`.
- Updated AI vault root defaults to `AI_Vault` and asset-mirror root to `AssetSubstrate/Vault/AI`.
- Updated imported mesh output in the Vulkan app to use the configured asset substrate root instead of hardcoded `AssetSubstrate/Assets/Imported/`.
- Preserved GUI-only editor control posture; this pass fixes user-facing path/layout drift rather than adding new command surfaces.

- v29: Cleaned remaining active user-facing path fossils: normalized shipped `ProjectSettings/project_settings.ewcfg` away from `Draft Container/...`, fixed AI experiment opening in the dockable panel to use `AssetSubstrate/AI/experiments/...`, and removed a remaining imported-mesh lookup fallback path that still targeted `Draft Container/Assets/Imported/`.

- v33: converted dockable AI-panel coherence selftest and rename-patch actions to structured GUI snapshot paths; rename patches now fill the patch buffer directly instead of depending on runtime text emission.

- v38: made Repository Browser and Coherence Tools persistent modeless Win64 tool windows instead of modal popups; menu now focuses existing windows if already open.

- v39: Integrated Repository Browser and Coherence Tools into the dockable AI panel as embedded persistent views (Chat / Repository / Coherence), replacing modeless tool-window opening from the AI panel menu. Added structured in-panel repo refresh/preview/highlight and coherence stats/query/rename-plan/prepare-patch actions. Fixed active experiment-open path escaping while keeping GenesisEngine as the only archive root.

- v42: AI panel embedded Repository/Coherence ergonomics pass: added local selection affordances (repo selected-path strip + Copy Path, Copy Results, Copy Patch) and tightened embedded tool layout for per-selection actions.

- v42: AI panel local-actions ergonomics pass: added repo selected-path strip + Copy Path, plus Copy Results / Copy Patch controls in the embedded Coherence view for better per-selection actions.

- v44: Embedded Coherence view ergonomics pass: converted results to a selectable hit list, preserved selected hit across query/rename-plan refreshes when possible, added local Highlight Hit / Copy Hit Path actions, and made double-click on a coherence hit trigger highlight directly from the dockable AI panel.

- v45: embedded AI-panel Coherence view polish. Added Selected Hit strip, Open Hit action, and fixed Copy Results newline joining for structured coherence hit lists.
test
