@echo off
setlocal enabledelayedexpansion

set PRESET=win64-vs2022-clang-vulkan
set BUILD_PRESET=%PRESET%
set CONFIG=Release
set ROOT=%~dp0\..
for %%I in ("%ROOT%") do set ROOT=%%~fI
set BUILDDIR=%ROOT%\out\build\%PRESET%
set STAGEDIR=%BUILDDIR%\package_stage\%CONFIG%

echo [GENESIS] Repo root : %ROOT%
echo [GENESIS] Preset    : %PRESET%
echo [GENESIS] Config    : %CONFIG%
echo [GENESIS] Build dir : %BUILDDIR%
echo [GENESIS] Stage dir : %STAGEDIR%

pushd "%ROOT%" >nul

where cmake >nul 2>nul
if errorlevel 1 (
  echo [GENESIS] ERROR: cmake was not found. Run scripts\bootstrap_windows.ps1 first.
  popd >nul
  exit /b 2
)

where cpack >nul 2>nul
if errorlevel 1 (
  echo [GENESIS] ERROR: cpack was not found. Reinstall CMake with CPack support.
  popd >nul
  exit /b 3
)

set "NSIS_EXE_C=C:\Program Files (x86)\NSIS\makensis.exe"
set "NSIS_EXE_D=D:\Program Files (x86)\NSIS\makensis.exe"
if exist "!NSIS_EXE_C!" set "PATH=C:\Program Files (x86)\NSIS;!PATH!"
if exist "!NSIS_EXE_D!" set "PATH=D:\Program Files (x86)\NSIS;!PATH!"

if "%VULKAN_SDK%"=="" (
  for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$root=Join-Path $env:LOCALAPPDATA 'Programs\VulkanSDK'; if (Test-Path $root) { Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName }"`) do set VULKAN_SDK=%%I
)
if not "%VULKAN_SDK%"=="" (
  set VK_SDK_PATH=%VULKAN_SDK%
  set "PATH=%VULKAN_SDK%\Bin;!PATH!"
  echo [GENESIS] Using Vulkan SDK: %VULKAN_SDK%
)

echo [GENESIS] Configuring...
cmake --preset %PRESET%
if errorlevel 1 (
  popd >nul
  exit /b 10
)

echo [GENESIS] Building distributable targets...
cmake --build --preset %BUILD_PRESET%
if errorlevel 1 (
  popd >nul
  exit /b 11
)

if exist "%STAGEDIR%" rmdir /s /q "%STAGEDIR%"

echo [GENESIS] Staging install tree...
cmake --install "%BUILDDIR%" --config %CONFIG% --prefix "%STAGEDIR%"
if errorlevel 1 (
  popd >nul
  exit /b 12
)

call "%ROOT%\scripts\validate_package_win64.bat" "%STAGEDIR%"
if errorlevel 1 (
  popd >nul
  exit /b 13
)

echo [GENESIS] Packaging archives/installers...
cpack --preset %PRESET%
if errorlevel 1 (
  popd >nul
  exit /b 14
)

echo [GENESIS] Package artifacts:
dir /b "%BUILDDIR%\packages"

if exist "%BUILDDIR%\packages\*.exe" (
  echo [GENESIS] NSIS installer generated.
) else (
  echo [GENESIS] ZIP package generated. Install NSIS with scripts\bootstrap_windows.ps1 to also emit a Windows installer .exe.
)

popd >nul
exit /b 0
