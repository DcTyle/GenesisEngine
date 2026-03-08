# Pass 6 Implementation Matrix — Asset / Voxel / Builder Unification

Inherited line: `GenesisEngine_repo_inherited_patch_v52_pass5_node_pin_resonance.zip`

Execution contract source: `Genesis_Engine_Surgical_Patch_Execution_Contract.md`

## Pass 6 scouting classification

### E1. Deepen Voxel Designer as atom/resonance authoring lane
Status before patch: **partial**

Repo evidence before patch:
- Voxel Designer panel existed with material presets, density/hardness/roughness sliders, atom-node list, and a summary surface.
- Resonance overlay hookup already existed.
- The summary was present but thin; it did not clearly state the shared authoring contract across asset/planet/character lanes, and the edit-summary language was still relatively lightweight.

Patch intent:
- Strengthen the voxel summary to explicitly surface edit-packet semantics and the shared authoring contract.
- Keep the existing voxel lane canonical instead of creating a second designer model.

### E2. Unify builder/designer flows across asset families
Status before patch: **missing / weak partial**

Repo evidence before patch:
- Asset Builder, Planet Builder, Character Tools, and Voxel Designer UI controls existed.
- `RefreshAssetDesignerPanel()` was declared but not materially present on the active inherited line.
- The asset lane did not have a single shared summary describing common authoring concepts, linked content target, project/work substrate, coherence review status, and viewport synchronization.
- Mode-specific controls existed but were not being surfaced through one canonical refresh path.

Patch intent:
- Implement `RefreshAssetDesignerPanel()` as the single UI refresh path for the asset authoring lane.
- Unify status reporting for Asset Builder / Planet Builder / Character Tools.
- Surface project/work substrate linkage, content-browser linkage, coherence review status, and viewport sync in one summary.
- Hide/show mode-specific controls through the same canonical panel refresh.

### E3. Keep coherence/reference safety checks on the canonical path
Status before patch: **partial**

Repo evidence before patch:
- Canonical reference review already existed via `ReviewReferencesForPath(...)`.
- The AI/coherence panel could already display related references.
- High-impact apply actions in the asset lane were not gated through a current reference review for the linked content target.

Patch intent:
- Record the last reviewed content target and highlight revision on the canonical asset panel state.
- Route Planet Apply and Character Bind through the existing coherence/reference review path before allowing mutation when review is stale or missing.
- Keep this on the existing review path instead of inventing a side safety checker.

## File-by-file implementation matrix

### 1. `vulkan_app/include/GE_app.hpp`
Change type: **surgical extension**

Why touched:
- Add minimal state required to remember the last reviewed content target and revision for the asset lane.

Changes:
- Added `asset_last_review_rel_utf8_`
- Added `asset_last_review_revision_u64_`

Why this is canonical:
- Keeps review-state memory inside the existing editor application state.
- Avoids creating a second review tracker or side cache.

### 2. `vulkan_app/src/GE_app.cpp`
Change type: **primary Pass 6 implementation**

Why touched:
- Implement the missing canonical asset-lane refresh function.
- Route high-impact apply actions through the existing coherence/reference review path.
- Tighten summary surfaces and viewport/context feedback.

Changes:
- Added `ew_read_trackbar_pos_safe(...)`
- Added `ew_combo_selected_text_safe(...)`
- Implemented `App::RefreshAssetDesignerPanel()`
- Updated `ReviewReferencesForPath(...)` to cache the reviewed target + revision and refresh the asset lane immediately
- Updated `SelectContentRelativePath(...)` to refresh the asset lane when content linkage changes
- Gated `Planet Apply` through current coherence/reference review
- Gated `Character Bind` through current coherence/reference review
- Strengthened `RefreshVoxelDesignerPanel()` summary text with shared-authoring-contract language

Why this is canonical:
- Uses the already-declared asset refresh function instead of adding a parallel summary system.
- Uses the existing `ReviewReferencesForPath(...)` coherence path instead of inventing a second safety path.
- Keeps builder/designer unification on the current editor surfaces.

## Definition-of-done check for Pass 6

### E1 — Voxel Designer deeper summary
Result: **satisfied enough for this pass**
- The existing lane is preserved.
- Summary now states edit-packet semantics and shared authoring contract.
- Resonance linkage and viewport sync remain on the canonical path.

### E2 — Unified builder/designer flow
Result: **satisfied for current pass scope**
- One implemented `RefreshAssetDesignerPanel()` now drives shared status and summary behavior.
- Asset/Planet/Character lanes expose shared substrate/content/review/viewport concepts through the same summary surface.
- Mode-specific controls are shown/hidden by the same canonical refresh path.

### E3 — Canonical coherence/reference safety path
Result: **satisfied for current pass scope**
- Planet Apply and Character Bind now pause until the current linked content target has passed coherence/reference review.
- Review state is tracked against the linked content target and highlight revision.
- No new safety subsystem was introduced.

## Validation performed

Structural validation:
- Verified `RefreshAssetDesignerPanel()` now exists exactly once in `vulkan_app/src/GE_app.cpp`
- Verified the new review-state members exist in `vulkan_app/include/GE_app.hpp`
- Verified Planet Apply and Character Bind now check the canonical review state before mutation
- Verified the voxel summary includes the new shared-authoring-contract surface

Build validation limits in this container:
- Native syntax/build validation for `vulkan_app/src/GE_app.cpp` is not runnable here because the file depends on Win32 headers/libraries unavailable in the Linux container.

## Touched files only
- `vulkan_app/include/GE_app.hpp`
- `vulkan_app/src/GE_app.cpp`
