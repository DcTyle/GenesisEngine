# Genesis Engine Vulkan Viewport (Win64)

This is the native Win64 Vulkan viewport application that replaces the Unreal plugin surface.

## Requirements (Windows)

1. **Vulkan Runtime (GPU driver)**
   - Install the latest NVIDIA/AMD/Intel driver for your GPU.

2. **Vulkan SDK (build-time)**
   - Needed for headers, loader import library, and validation layers.
   - Recommended install path is the default.

3. **Toolchain**
   - Visual Studio 2022 + **ClangCL** toolset (required by EigenWareCore due to `__int128`).

A helper script is provided:

- `scripts/bootstrap_windows.ps1`

## Build (Win64)

From `Draft Container/GenesisEngine/`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T ClangCL
cmake --build build --config Release --target GenesisEngineVulkan
```

Binary output:

- `build/GenesisEngineState/Binaries/Win64/GenesisEngineVulkan.exe`

## Dynamic Rendering

The viewport uses **Vulkan 1.3 dynamic rendering** (`vkCmdBeginRendering` / `vkCmdEndRendering`) and does not create or use render passes.

The current renderer is a deterministic **clear-only** frame (no shaders yet), which validates the swapchain + dynamic rendering integration.

## UI (AI panel)

The right panel is implemented using Win64 controls (no third-party UI deps):

- **Input**: UTF-8 text line
- **Send**: injects into `SubstrateMicroprocessor::ui_submit_user_text_line()`
- **Output**: shows up to 64 emitted lines per tick
- **Import OBJ**: imports Wavefront `.obj` into the scene list

## OBJ import

- Supports `v` and `f` primitives (triangles).
- Face formats `f a b c` and `f a/b/c b/b/b c/c/c` are accepted (only the vertex index is used).

