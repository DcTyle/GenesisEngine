# PASS 20 RESET IMPLEMENTATION MATRIX

## Scope
Non-calculus cleanup on the active reset line only. No CPU encoding rollout. No Hilbert/calculus schema commitment.

## Files
- `CMakeLists.txt` — replaced the headless workspace tool target name with `ew_project_workspace` and pointed it at the renamed source file.
- `src/tools/project_workspace_main.cpp` — renamed the former hydrate tool source and updated CLI/status strings and comments from hydration wording to projection wording.
- `include/GE_runtime.hpp` — cleaned the observable receipt/projection comments to describe explicit workspace projection semantics.
- `src/GE_runtime.cpp` — cleaned nearby gameboot/projection comments so the active line stops describing the explicit workspace write step as hydration.
- `vulkan_app/src/GE_app.cpp` — cleaned nearby operator-facing comments about disk writes for AI/crawler artifacts to use projection wording.
- `docs/GENESIS_WORKSPACE_PROJECTION_NOTE.md` — pinned the active-line terminology rule and scope boundaries.

## Validation
- `g++ -std=c++17 -fsyntax-only -Iinclude src/tools/project_workspace_main.cpp`
- `g++ -std=c++17 -fsyntax-only -Iinclude src/GE_runtime.cpp`
