# Pass 18 Reset Implementation Matrix

## Scope
Non-calculus workflow alignment only. No CPU-file encoding rollout. No Hilbert/calculus schema commitment.

## Implemented

### AI workflow queue and menu alignment
Status: implemented

Touched file:
- `vulkan_app/src/GE_app.cpp`

Changes:
- Counted `Diff Ready` chats as part of the actionable ready lane in the workflow queue summary.
- Updated queue guidance text so diff-bufferable work is grouped with ready work rather than scope-retained threads.
- Included `Diff Ready` in the AI panel ready-queue count and submenu population.
- Extended `Focus Next Ready…` and `Do Next Ready…` fallbacks so they now consider `Diff Ready` after apply/bind/preview buckets.
- Aligned tab badges so a chat with both retained scope and a detected diff shows `[D]` instead of the misleading `[S]`.

## Validation
- Structural verification of all touched workflow queue/menu branches in `vulkan_app/src/GE_app.cpp`.
- Native syntax/build validation for `vulkan_app/src/GE_app.cpp` is not runnable in this Linux container because the file depends on Win32 headers/libraries.

## Notes
- This pass stays strictly on the reset line.
- No CPU annotation/encoding work was reintroduced.
- No calculus/operator-actuation schema was added or implied.
