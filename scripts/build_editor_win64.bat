@echo off
setlocal enabledelayedexpansion

REM Deterministic build script for the Vulkan editor target.

set ROOT=%~dp0\..
pushd "%ROOT%" >nul

set PRESET=win64-vs2022-clang-vulkan

echo [GENESIS] Configure preset: %PRESET%
cmake --preset %PRESET%
if errorlevel 1 (
  echo [GENESIS] ERROR: cmake configure failed
  popd >nul
  exit /b 1
)

echo [GENESIS] Build target: genesis_app (Release)
cmake --build --preset %PRESET% --config Release --target genesis_app
if errorlevel 1 (
  echo [GENESIS] ERROR: build failed
  popd >nul
  exit /b 2
)

echo [GENESIS] OK: genesis_app built
popd >nul
exit /b 0
