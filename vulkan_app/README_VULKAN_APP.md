Genesis Engine Vulkan Native Viewport

This replaces the previous Unreal Engine plugin adapter with a standalone Win64 + Vulkan application.

Build prerequisites (Windows)

- Vulkan SDK installed (provides Vulkan loader + headers + libraries).
- Visual Studio 2022 with ClangCL toolset enabled.
  - EigenWareCore uses __int128 for fixed-point determinism; MSVC is not supported for that path.

Configure + build

From `Draft Container/GenesisEngine/`:

1) Configure (Visual Studio generator with ClangCL):

  cmake -S . -B build_vulkan -G "Visual Studio 17 2022" -A x64 -T ClangCL -DEW_BUILD_VULKAN_APP=ON

2) Build:

  cmake --build build_vulkan --config Release

The executable will appear under:

  build_vulkan/bin/Release/GenesisEngineVulkan.exe

Runtime behavior

- Left: Vulkan viewport (currently clears to a color derived from camera yaw/pitch).
- Right: Objects + AI panel.

Viewport controls (Unreal-like)

- RMB fly: WASD + QE + mouse look (Shift = faster)
- Alt + LMB orbit
- Alt + MMB pan
- Alt + RMB dolly
- Mouse wheel dolly

Object editing (MVP)

- Import OBJ: click "Import OBJ" and choose a `.obj` file.
- Select object: click in the object list.
- Translate: hold W and drag with LMB in the viewport.
- Rotate: operator site is reserved (E + drag), but visual gizmo rendering is deferred until the first shader-based renderer pass.

AI interface

- Type a line and click "Send".
- The line is injected into the substrate microprocessor as a UTF-8 observation.
- Substrate outputs are streamed into the output pane.

Common deterministic commands:

- QUERY: <terms>
- WEBSEARCH: <terms>
- OPEN: <result_index>
- ANSWER: <question>

Note on rendering

This pass intentionally avoids shader dependencies by using Vulkan transfer clears on the swapchain.
The next rendering pass should introduce a shader pipeline (SPIR-V baked into source or generated deterministically at build time) to visualize:

- radiance / flux slices as points or a volume
- imported mesh preview (solid/wire)
- transform gizmos

Ancillabit pulse loop (GPU pulse -> substrate -> GPU)

The viewport includes a minimal “pulse capture” loop using GPU timestamps.

- Each frame writes a begin/end timestamp around the dynamic-rendering block.
- At the start of the next frame (after the fence wait), the timestamp delta is read.
- That delta evolves a tiny ancillabit-facing state (phase + amplitude).
- The ancillabit state can feed back immediately into GPU-visible parameters.

Baseline projection is implemented as a clear-color modulation that requires no shaders.
Enable it by setting:

  EW_ANCILLABIT_CLEAR=1

This establishes the immediate loop update response without committing to any shader toolchain yet.

