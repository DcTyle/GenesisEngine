@echo off
setlocal enabledelayedexpansion

REM Deterministic build script for the Vulkan editor target.

set ROOT=%~dp0\..
pushd "%ROOT%" >nul

set PRESET=win64-vs2022-clang-vulkan
set BUILD_PRESET=%PRESET%-editor

if "%VULKAN_SDK%"=="" (
  for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$root=Join-Path $env:LOCALAPPDATA 'Programs\VulkanSDK'; if (Test-Path $root) { Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName }"`) do set VULKAN_SDK=%%I
)
if not "%VULKAN_SDK%"=="" (
  set VK_SDK_PATH=%VULKAN_SDK%
  set "PATH=%VULKAN_SDK%\Bin;!PATH!"
)

echo [GENESIS] Configure preset: %PRESET%
cmake --preset %PRESET%
if errorlevel 1 (
  echo [GENESIS] ERROR: cmake configure failed
  popd >nul
  exit /b 1
)

echo [GENESIS] Build preset: %BUILD_PRESET%
cmake --build --preset %BUILD_PRESET%
if errorlevel 1 (
  echo [GENESIS] ERROR: build failed
  popd >nul
  exit /b 2
)

echo [GENESIS] OK: genesis_app built
popd >nul
exit /b 0
