@echo off
setlocal enabledelayedexpansion

set PRESET=win64-vs2022-clang-vulkan
set BUILD_PRESET=%PRESET%
set CONFIG=Release
set ROOT=%~dp0\..
for %%I in ("%ROOT%") do set ROOT=%%~fI
set BUILDDIR=%ROOT%\out\build\%PRESET%
set STAGEDIR=%BUILDDIR%\package_stage\%CONFIG%
set LOGDIR=%ROOT%\GenesisEngineState\Logs
set LOG_STEP=%ROOT%\scripts\invoke_logged_step.ps1
set NOTEBOOK_LOGGER=%ROOT%\scripts\update_research_notebook.py
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set CONFIGURE_LOG=%LOGDIR%\package_vulkan_app_win64_configure.log
set BUILD_LOG=%LOGDIR%\package_vulkan_app_win64_build.log
set STAGE_LOG=%LOGDIR%\package_vulkan_app_win64_stage.log
set VALIDATE_LOG=%LOGDIR%\package_vulkan_app_win64_validate.log
set PACKAGE_LOG=%LOGDIR%\package_vulkan_app_win64_package.log
set NOTEBOOK_STATUS=unknown

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
powershell -NoProfile -ExecutionPolicy Bypass -File "%LOG_STEP%" -WorkingDirectory "%ROOT%" -LogPath "%CONFIGURE_LOG%" -CommandArgs 'cmake','--preset','%PRESET%'
if errorlevel 1 (
  set NOTEBOOK_STATUS=configure_failed
  call :capture_notebook
  popd >nul
  exit /b 10
)

echo [GENESIS] Building distributable targets...
powershell -NoProfile -ExecutionPolicy Bypass -File "%LOG_STEP%" -WorkingDirectory "%ROOT%" -LogPath "%BUILD_LOG%" -CommandArgs 'cmake','--build','--preset','%BUILD_PRESET%'
if errorlevel 1 (
  set NOTEBOOK_STATUS=build_failed
  call :capture_notebook
  popd >nul
  exit /b 11
)

if exist "%STAGEDIR%" rmdir /s /q "%STAGEDIR%"

echo [GENESIS] Staging install tree...
powershell -NoProfile -ExecutionPolicy Bypass -File "%LOG_STEP%" -WorkingDirectory "%ROOT%" -LogPath "%STAGE_LOG%" -CommandArgs 'cmake','--install','%BUILDDIR%','--config','%CONFIG%','--prefix','%STAGEDIR%'
if errorlevel 1 (
  set NOTEBOOK_STATUS=stage_failed
  call :capture_notebook
  popd >nul
  exit /b 12
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%LOG_STEP%" -WorkingDirectory "%ROOT%" -LogPath "%VALIDATE_LOG%" -CommandArgs "%ROOT%\scripts\validate_package_win64.bat" "%STAGEDIR%"
if errorlevel 1 (
  set NOTEBOOK_STATUS=validate_failed
  call :capture_notebook
  popd >nul
  exit /b 13
)

echo [GENESIS] Packaging archives/installers...
powershell -NoProfile -ExecutionPolicy Bypass -File "%LOG_STEP%" -WorkingDirectory "%ROOT%" -LogPath "%PACKAGE_LOG%" -CommandArgs 'cpack','--preset','%PRESET%'
if errorlevel 1 (
  set NOTEBOOK_STATUS=package_failed
  call :capture_notebook
  popd >nul
  exit /b 14
)

echo [GENESIS] Package artifacts:
dir /b "%BUILDDIR%\packages"
>> "%PACKAGE_LOG%" echo [GENESIS] Package artifacts:
for /f "delims=" %%I in ('dir /b "%BUILDDIR%\packages"') do >> "%PACKAGE_LOG%" echo %%I

if exist "%BUILDDIR%\packages\*.exe" (
  echo [GENESIS] NSIS installer generated.
  >> "%PACKAGE_LOG%" echo [GENESIS] NSIS installer generated.
) else (
  echo [GENESIS] ZIP package generated. Install NSIS with scripts\bootstrap_windows.ps1 to also emit a Windows installer .exe.
  >> "%PACKAGE_LOG%" echo [GENESIS] ZIP package generated. Install NSIS with scripts\bootstrap_windows.ps1 to also emit a Windows installer .exe.
)

set NOTEBOOK_STATUS=success
call :capture_notebook
popd >nul
exit /b 0

:capture_notebook
if not exist "%NOTEBOOK_LOGGER%" exit /b 0
where python >nul 2>nul
if errorlevel 1 exit /b 0
python "%NOTEBOOK_LOGGER%" capture --repo-root "%ROOT%" --event "package_vulkan_app_win64" --status "%NOTEBOOK_STATUS%" --script "scripts/package_vulkan_app_win64.bat" --log "%CONFIGURE_LOG%" --log "%BUILD_LOG%" --log "%STAGE_LOG%" --log "%VALIDATE_LOG%" --log "%PACKAGE_LOG%" --also-known-logs >nul 2>nul
exit /b 0
