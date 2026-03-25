@echo off
setlocal enabledelayedexpansion

REM Deterministic build script for the packaged Vulkan runtime target.
REM This script is intentionally simple and fail-closed.

set ROOT=%~dp0\..
pushd "%ROOT%" >nul

set PRESET=win64-vs2022-clang-vulkan
set BUILD_PRESET=%PRESET%-runtime
set LOGDIR=%ROOT%\GenesisEngineState\Logs
set LOG_STEP=%ROOT%\scripts\invoke_logged_step.ps1
set NOTEBOOK_LOGGER=%ROOT%\scripts\update_research_notebook.py
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set CONFIGURE_LOG=%LOGDIR%\build_game_win64_configure.log
set BUILD_LOG=%LOGDIR%\build_game_win64_build.log
set NOTEBOOK_STATUS=unknown

if "%VULKAN_SDK%"=="" (
  for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$root=Join-Path $env:LOCALAPPDATA 'Programs\VulkanSDK'; if (Test-Path $root) { Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName }"`) do set VULKAN_SDK=%%I
)
if not "%VULKAN_SDK%"=="" (
  set VK_SDK_PATH=%VULKAN_SDK%
  set "PATH=%VULKAN_SDK%\Bin;!PATH!"
)

echo [GENESIS] Configure preset: %PRESET%
powershell -NoProfile -ExecutionPolicy Bypass -File "%LOG_STEP%" -WorkingDirectory "%ROOT%" -LogPath "%CONFIGURE_LOG%" -CommandArgs 'cmake','--preset','%PRESET%'
if errorlevel 1 (
  echo [GENESIS] ERROR: cmake configure failed
  set NOTEBOOK_STATUS=configure_failed
  call :capture_notebook
  popd >nul
  exit /b 1
)

echo [GENESIS] Build preset: %BUILD_PRESET%
powershell -NoProfile -ExecutionPolicy Bypass -File "%LOG_STEP%" -WorkingDirectory "%ROOT%" -LogPath "%BUILD_LOG%" -CommandArgs 'cmake','--build','--preset','%BUILD_PRESET%'
if errorlevel 1 (
  echo [GENESIS] ERROR: build failed
  set NOTEBOOK_STATUS=build_failed
  call :capture_notebook
  popd >nul
  exit /b 2
)

echo [GENESIS] OK: genesis_runtime built
>> "%BUILD_LOG%" echo [GENESIS] OK: genesis_runtime built
set NOTEBOOK_STATUS=success
call :capture_notebook
popd >nul
exit /b 0

:capture_notebook
if not exist "%NOTEBOOK_LOGGER%" exit /b 0
where python >nul 2>nul
if errorlevel 1 exit /b 0
python "%NOTEBOOK_LOGGER%" capture --repo-root "%ROOT%" --event "build_game_win64" --status "%NOTEBOOK_STATUS%" --script "scripts/build_game_win64.bat" --log "%CONFIGURE_LOG%" --log "%BUILD_LOG%" --also-known-logs >nul 2>nul
exit /b 0
