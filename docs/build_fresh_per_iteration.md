Fresh build per iteration

This repo is configured to build out-of-tree only. Each iteration should be built from a clean build directory to guarantee that no stale objects or CMake cache state can survive across updates.

Recommended commands (Windows)

PowerShell:
  scripts\clean_build_win64.ps1 -Preset win64-vs2022-clang-vulkan -Config Release

CMD:
  scripts\clean_build_win64.bat win64-vs2022-clang-vulkan Release

Notes
- The scripts delete: out\build\<preset> before configuring and compiling.
- Windows builds require the Visual Studio 2022 ClangCL toolset and the Vulkan SDK shader tools.
- The canonical Vulkan preset disables CUDA so the viewport app can configure on non-CUDA machines.
