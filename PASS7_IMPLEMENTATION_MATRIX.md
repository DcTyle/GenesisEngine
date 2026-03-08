# Pass 7 Implementation Matrix — Content / Repo / Coherence UX Tightening

Inherited line: `GenesisEngine_repo_inherited_patch_v52_pass6_asset_builder_unification.zip`

Execution contract source: `Genesis_Engine_Surgical_Patch_Execution_Contract.md`

## Pass 7 scouting classification

### F1. Tighten navigation from AI patch explanation into repo/coherence surfaces
Status before patch: **partial**

Repo evidence before patch:
- `BuildAiNavigationSpineText(...)` already existed.
- `AiChatShowPatchView(...)` already tried to sync repo/coherence/content selection.
- The navigation text was useful but still thin: it did not clearly surface repo-preview route, coherence-hit route, and apply/validation route as one compact spine.
- The navigation action itself was inlined in `AiChatShowPatchView(...)` rather than exposed as one canonical navigation helper.

Patch intent:
- Add one canonical navigation helper for AI patch explanation routing.
- Strengthen the navigation spine text so it explicitly shows session record, target file/region, repo-preview route, coherence-hit route, and apply/validation result.
- Keep navigation derived from the same canonical target path instead of scattering ad-hoc jumps.

### F2. Improve rename/reference review consistency across surfaces
Status before patch: **partial**

Repo evidence before patch:
- Content browser, repository pane, and asset review already used `BuildCanonicalReferenceSummaryForPath(...)` / `CommitCanonicalReferenceSummary(...)` in some places.
- Coherence query and rename-plan UI paths were still formatting their own result lists directly.
- Patch metadata did not explicitly embed the same canonical reference summary used elsewhere.

Patch intent:
- Expand the canonical reference summary model to carry source label, query/plan key, and ranking revision.
- Route coherence query and rename-plan review through the same canonical summary builder/commit path.
- Embed canonical repo/coherence ranking and navigation spine in patch metadata so patch prep speaks the same language as repo/coherence/content surfaces.

### F3. Remove duplicate repo-index-style logic drift
Status before patch: **partial**

Repo evidence before patch:
- Path-specific reference review was using a coherence query against the file path text rather than the dedicated repo-file coherence snapshot.
- Coherence query, rename review, repo pane, and patch prep were building similar ranked hit views through separate formatting logic.
- That drift risked each surface telling a slightly different story about ranked references.

Patch intent:
- Switch canonical path-based reference review to `SnapshotRepoFileCoherenceHits(...)`.
- Introduce one canonical hit-to-summary formatter used by path review, coherence query review, and rename-plan review.
- Keep UI surfaces consuming the same ranked summary object instead of independently re-scoring or reformatting references.

## File-by-file implementation matrix

### 1. `vulkan_app/include/GE_app.hpp`
Change type: **surgical extension**

Why touched:
- Extend the existing canonical reference-summary structure and declare the minimal helpers needed to remove repo/coherence ranking drift.

Changes:
- Extended `CanonicalReferenceSummary` with:
  - `source_key_utf8`
  - `query_utf8`
  - `source_label_w`
  - `ranking_revision_u64`
- Declared:
  - `BuildCanonicalReferenceSummaryFromHits(...)`
  - `BuildCanonicalReferenceSummaryForRename(...)`
  - `NavigateAiChatReferenceSpine(...)`

Why this is canonical:
- Keeps the summary model in the existing editor app state.
- Avoids a second reference-review structure or side navigation model.

### 2. `vulkan_app/src/GE_app.cpp`
Change type: **primary Pass 7 implementation**

Why touched:
- Consolidate reference-ranking formatting and AI navigation into one canonical path.
- Remove duplicate repo/coherence hit rendering drift.

Changes:
- Added `BuildCanonicalReferenceSummaryFromHits(...)` as the single ranked-hit formatter.
- Reworked `BuildCanonicalReferenceSummaryForPath(...)` to use `SnapshotRepoFileCoherenceHits(...)` instead of a free-form coherence query.
- Added `BuildCanonicalReferenceSummaryForRename(...)` for rename/reference review.
- Strengthened `CommitCanonicalReferenceSummary(...)` status reporting with canonical source metadata.
- Added `NavigateAiChatReferenceSpine(...)` and routed `AiChatShowPatchView(...)` through it.
- Expanded `BuildAiNavigationSpineText(...)` to include:
  - patch session record
  - target file/region
  - repo preview route
  - coherence hit route
  - apply/validation result
- Updated `RefreshAiNavigationSpine(...)` to pull canonical reference review from the shared summary path.
- Embedded navigation spine + canonical repo/coherence ranking into `BuildAiChatPatchMetadata(...)`.
- Updated AI coherence query button handling to commit a canonical ranked summary instead of formatting a custom list.
- Updated rename-plan review button handling to commit a canonical ranked summary instead of formatting a separate impact list.
- Updated rename-patch preparation to refresh the canonical rename/reference summary used by surrounding surfaces.

Why this is canonical:
- Uses one ranked-summary builder for repo path review, coherence query review, rename-plan review, and patch-prep context.
- Uses one navigation helper for AI explanation routing instead of per-surface ad-hoc jumps.
- Reuses existing repo/content/coherence surfaces rather than adding a new panel or indexer.

## Definition-of-done check for Pass 7

### F1 — AI explanation navigation spine
Result: **satisfied for current pass scope**
- Patch view now routes through one canonical navigation helper.
- Navigation text explicitly calls out repo preview route, coherence hit route, target file/region, session record, and apply/validation result.
- Repo/coherence/content selection are synchronized from the same canonical target path.

### F2 — Shared rename/reference summary model
Result: **satisfied for current pass scope**
- Coherence query, rename plan, repo selection, content review, and patch metadata now share the same canonical reference-summary model.
- Rename review no longer tells a separate story from repo/coherence review.

### F3 — Duplicate repo-index drift reduction
Result: **satisfied for current pass scope**
- Path-based review now uses the dedicated repo-file coherence snapshot.
- Ranked-hit formatting is centralized in one helper.
- Patch prep and coherence review no longer independently format their own ranking logic for the main UX path.

## Validation performed

Structural validation:
- Verified the new canonical helper declarations exist in `vulkan_app/include/GE_app.hpp`.
- Verified the active implementation line now routes path review through `SnapshotRepoFileCoherenceHits(...)`.
- Verified coherence query and rename-plan button handlers now call the canonical summary path.
- Verified patch metadata now includes the navigation spine and canonical repo/coherence ranking.

Build validation limits in this container:
- Native syntax/build validation for `vulkan_app/src/GE_app.cpp` is still not runnable here because `windows.h` is unavailable in the Linux container.

## Touched files only
- `vulkan_app/include/GE_app.hpp`
- `vulkan_app/src/GE_app.cpp`
