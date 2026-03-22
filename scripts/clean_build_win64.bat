@echo off
setlocal enabledelayedexpansion

set PRESET=win64-vs2022-clang-vulkan
set CONFIG=Release

if not "%1"=="" set PRESET=%1
if not "%2"=="" set CONFIG=%2

set SCRIPTDIR=%~dp0
set SOURCEDIR=%SCRIPTDIR%\..
for %%I in ("%SOURCEDIR%") do set SOURCEDIR=%%~fI

set BUILDDIR=%SOURCEDIR%\out\build\%PRESET%

if "%VULKAN_SDK%"=="" (
  for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$root=Join-Path $env:LOCALAPPDATA 'Programs\VulkanSDK'; if (Test-Path $root) { Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName }"`) do set VULKAN_SDK=%%I
)
if not "%VULKAN_SDK%"=="" (
  set VK_SDK_PATH=%VULKAN_SDK%
  set "PATH=%VULKAN_SDK%\Bin;!PATH!"
)

echo [GE] SourceDir: %SOURCEDIR%
echo [GE] BuildDir : %BUILDDIR%
echo [GE] Preset   : %PRESET%
echo [GE] Config   : %CONFIG%

if exist "%BUILDDIR%" (
  echo [GE] Removing previous build dir...
  rmdir /s /q "%BUILDDIR%"
)

echo [GE] Configuring...
cmake --preset %PRESET%
if errorlevel 1 exit /b 1

echo [GE] Building...
cmake --build --preset %PRESET% --config %CONFIG%
if errorlevel 1 exit /b 1

echo [GE] Done.
