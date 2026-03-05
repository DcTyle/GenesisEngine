@echo off
setlocal enabledelayedexpansion

REM Deterministic build script for the packaged GAME target.
REM This script is intentionally simple and fail-closed.

set ROOT=%~dp0\..
pushd "%ROOT%" >nul

set PRESET=win64-vs2022-cuda

echo [GENESIS] Configure preset: %PRESET%
cmake --preset %PRESET%
if errorlevel 1 (
  echo [GENESIS] ERROR: cmake configure failed
  popd >nul
  exit /b 1
)

echo [GENESIS] Build target: genesis_game (Release)
cmake --build --preset %PRESET% --config Release --target genesis_game
if errorlevel 1 (
  echo [GENESIS] ERROR: build failed
  popd >nul
  exit /b 2
)

echo [GENESIS] OK: genesis_game built
popd >nul
exit /b 0
