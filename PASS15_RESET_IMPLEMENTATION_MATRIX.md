# PASS 15 RESET IMPLEMENTATION MATRIX

Active line: `GenesisEngine_repo_inherited_patch_v52_pass14_reset_editor_history_ui.zip`

Constraint guardrails applied in this pass:
- stayed on the reset / non-encoder branch
- did **not** advance CPU annotation / encoding rollout
- did **not** commit to Hilbert / calculus actuation schema
- used surgical edits only on the active inherited line

## Pass goal
Fix the AI workflow queue/action mismatch where chats classified as **Diff Ready** still resolved their primary action as **Patch/Scope** instead of promoting the detected assistant diff into the canonical patch buffer.

## File-by-file matrix

| File | Status | Change |
|---|---|---|
| `vulkan_app/include/GE_app.hpp` | Updated | Added one canonical helper declaration for buffering the detected assistant diff into the patch buffer. |
| `vulkan_app/src/GE_app.cpp` | Updated | Added `BufferAiChatDetectedDiff(...)`; rewired **Use Diff** button path to use the helper; corrected primary-action label/reason selection for diff-ready chats; corrected primary-action execution so diff-ready chats promote the assistant diff into the patch buffer before preview/apply; enriched patch-view text for unbuffered detected diffs with touched-file count and next structural action. |

## Validation performed
- Structural verification that all former inline "Use last detected Assistant patch" behavior now routes through `BufferAiChatDetectedDiff(...)`
- Structural verification that `BuildAiChatPrimaryActionLabel(...)` now returns `Use Diff` for diff-ready chats without a buffered patch
- Structural verification that `ExecuteAiChatPrimaryAction(...)` now promotes diff-ready chats into the patch buffer instead of falling through to `Patch/Scope`

## Validation not runnable in this container
- Native syntax/build validation for `vulkan_app/src/GE_app.cpp` is not runnable here because the UI target depends on Win32 headers/libraries not available in the Linux container.

## Net effect
The workflow queue, bucket text, tab badges, and primary-action execution are now aligned for diff-ready chats. The AI workflow no longer tells the user a diff is ready and then routes the main action into a scope viewer like a confused bureaucrat in a fake moustache.
