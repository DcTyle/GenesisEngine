@echo off
setlocal enabledelayedexpansion

set PRESET=win64-vs2022-clang-vulkan
set BUILD_PRESET=%PRESET%
set CONFIG=Release
set ROOT=%~dp0\..
for %%I in ("%ROOT%") do set ROOT=%%~fI
set LOGDIR=%ROOT%\GenesisEngineState\Logs
set LOG_STEP=%ROOT%\scripts\invoke_logged_step.ps1
set NOTEBOOK_LOGGER=%ROOT%\scripts\update_research_notebook.py
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set CONFIGURE_LOG=%LOGDIR%\install_vulkan_app_win64_configure.log
set BUILD_LOG=%LOGDIR%\install_vulkan_app_win64_build.log
set INSTALL_LOG=%LOGDIR%\install_vulkan_app_win64_install.log
set NOTEBOOK_STATUS=unknown

set INSTALL_DIR=%LOCALAPPDATA%\Programs\GenesisEngine
if not "%~1"=="" set INSTALL_DIR=%~1

echo [GENESIS] Repo root   : %ROOT%
echo [GENESIS] Preset      : %PRESET%
echo [GENESIS] Config      : %CONFIG%
echo [GENESIS] Install dir : %INSTALL_DIR%

pushd "%ROOT%" >nul

where cmake >nul 2>nul
if errorlevel 1 (
  echo [GENESIS] ERROR: cmake was not found. Run scripts\bootstrap_windows.ps1 first.
  popd >nul
  exit /b 2
)

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

echo [GENESIS] Building editor, runtime, and remote control...
powershell -NoProfile -ExecutionPolicy Bypass -File "%LOG_STEP%" -WorkingDirectory "%ROOT%" -LogPath "%BUILD_LOG%" -CommandArgs 'cmake','--build','--preset','%BUILD_PRESET%'
if errorlevel 1 (
  set NOTEBOOK_STATUS=build_failed
  call :capture_notebook
  popd >nul
  exit /b 11
)

echo [GENESIS] Stopping running Genesis processes (if any)...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-Process GenesisEngine,GenesisRuntime,GenesisRemote -ErrorAction SilentlyContinue | Stop-Process -Force"

echo [GENESIS] Installing CMake artifacts...
powershell -NoProfile -ExecutionPolicy Bypass -File "%LOG_STEP%" -WorkingDirectory "%ROOT%" -LogPath "%INSTALL_LOG%" -CommandArgs 'cmake','--install','%ROOT%\out\build\%PRESET%','--config','%CONFIG%','--prefix','%INSTALL_DIR%'
if errorlevel 1 (
  set NOTEBOOK_STATUS=install_failed
  call :capture_notebook
  popd >nul
  exit /b 12
)

if exist "%ROOT%\chatgptAPI.txt" (
  copy /Y "%ROOT%\chatgptAPI.txt" "%INSTALL_DIR%\chatgptAPI.txt" >nul
  echo [GENESIS] Staged chatgptAPI.txt into install root.
  >> "%INSTALL_LOG%" echo [GENESIS] Staged chatgptAPI.txt into install root.
) else (
  echo [GENESIS] WARN: chatgptAPI.txt was not found in the repo root. ChatGPT research will stay disabled until the file is added.
  >> "%INSTALL_LOG%" echo [GENESIS] WARN: chatgptAPI.txt was not found in the repo root. ChatGPT research will stay disabled until the file is added.
)

set APP_EXE=%INSTALL_DIR%\GenesisEngineState\Binaries\Win64\Editor\GenesisEngine.exe
set RUNTIME_EXE=%INSTALL_DIR%\GenesisEngineState\Binaries\Win64\Runtime\GenesisRuntime.exe
set REMOTE_EXE=%INSTALL_DIR%\GenesisEngineState\Binaries\Win64\Runtime\GenesisRemote.exe
set APP_WORKDIR=%INSTALL_DIR%\GenesisEngineState\Binaries\Win64\Editor
set REMOTE_WORKDIR=%INSTALL_DIR%\GenesisEngineState\Binaries\Win64\Runtime
if not exist "%APP_EXE%" (
  echo [GENESIS] ERROR: install did not produce GenesisEngine.exe
  >> "%INSTALL_LOG%" echo [GENESIS] ERROR: install did not produce GenesisEngine.exe
  set NOTEBOOK_STATUS=install_failed_missing_editor
  call :capture_notebook
  popd >nul
  exit /b 13
)
if not exist "%RUNTIME_EXE%" (
  echo [GENESIS] ERROR: install did not produce GenesisRuntime.exe
  >> "%INSTALL_LOG%" echo [GENESIS] ERROR: install did not produce GenesisRuntime.exe
  set NOTEBOOK_STATUS=install_failed_missing_runtime
  call :capture_notebook
  popd >nul
  exit /b 14
)
if not exist "%REMOTE_EXE%" (
  echo [GENESIS] ERROR: install did not produce GenesisRemote.exe
  >> "%INSTALL_LOG%" echo [GENESIS] ERROR: install did not produce GenesisRemote.exe
  set NOTEBOOK_STATUS=install_failed_missing_remote
  call :capture_notebook
  popd >nul
  exit /b 15
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$install='%INSTALL_DIR%';" ^
  "$appDir='%APP_WORKDIR%';" ^
  "$remoteDir='%REMOTE_WORKDIR%';" ^
  "$app=Join-Path $install 'GenesisEngineState\Binaries\Win64\Editor\GenesisEngine.exe';" ^
  "$remote=Join-Path $install 'GenesisEngineState\Binaries\Win64\Runtime\GenesisRemote.exe';" ^
  "$desktop=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Genesis Engine.lnk';" ^
  "$startMenuDir=Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Genesis Engine';" ^
  "New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null;" ^
  "$startMenu=Join-Path $startMenuDir 'Genesis Engine.lnk';" ^
  "$remoteShortcut=Join-Path $startMenuDir 'Genesis Remote Control.lnk';" ^
  "$shell=New-Object -ComObject WScript.Shell;" ^
  "foreach($path in @($desktop,$startMenu)) {" ^
  "  $sc=$shell.CreateShortcut($path);" ^
  "  $sc.TargetPath=$app;" ^
  "  $sc.Arguments='--appid=GenesisEngine.Editor';" ^
  "  $sc.WorkingDirectory=$appDir;" ^
  "  $sc.IconLocation=$app + ',0';" ^
  "  $sc.Description='Genesis Engine Win64 Vulkan editor';" ^
  "  $sc.Save();" ^
  "}" ^
  "$remoteSc=$shell.CreateShortcut($remoteShortcut);" ^
  "$remoteSc.TargetPath=$remote;" ^
  "$remoteSc.WorkingDirectory=$remoteDir;" ^
  "$remoteSc.IconLocation=$remote + ',0';" ^
  "$remoteSc.Description='Genesis Engine remote control bridge';" ^
  "$remoteSc.Save();"
if errorlevel 1 (
  echo [GENESIS] ERROR: failed to create shortcuts
  >> "%INSTALL_LOG%" echo [GENESIS] ERROR: failed to create shortcuts
  set NOTEBOOK_STATUS=shortcut_failed
  call :capture_notebook
  popd >nul
  exit /b 16
)

popd >nul

echo [GENESIS] Install complete.
echo [GENESIS] Launch shortcut: %APP_EXE%
echo [GENESIS] Remote control : %REMOTE_EXE% chat="your message here"
>> "%INSTALL_LOG%" echo [GENESIS] Install complete.
>> "%INSTALL_LOG%" echo [GENESIS] Launch shortcut: %APP_EXE%
>> "%INSTALL_LOG%" echo [GENESIS] Remote control : %REMOTE_EXE% chat="your message here"
set NOTEBOOK_STATUS=success
call :capture_notebook
exit /b 0

:capture_notebook
if not exist "%NOTEBOOK_LOGGER%" exit /b 0
where python >nul 2>nul
if errorlevel 1 exit /b 0
python "%NOTEBOOK_LOGGER%" capture --repo-root "%ROOT%" --event "install_vulkan_app_win64" --status "%NOTEBOOK_STATUS%" --script "scripts/install_vulkan_app_win64.bat" --log "%CONFIGURE_LOG%" --log "%BUILD_LOG%" --log "%INSTALL_LOG%" --also-known-logs >nul 2>nul
exit /b 0
