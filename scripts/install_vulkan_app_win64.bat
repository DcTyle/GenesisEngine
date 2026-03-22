@echo off
setlocal enabledelayedexpansion

set PRESET=win64-vs2022-clang-vulkan
set BUILD_PRESET=%PRESET%
set CONFIG=Release
set ROOT=%~dp0\..
for %%I in ("%ROOT%") do set ROOT=%%~fI

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
cmake --preset %PRESET%
if errorlevel 1 (
  popd >nul
  exit /b 10
)

echo [GENESIS] Building editor, runtime, and remote control...
cmake --build --preset %BUILD_PRESET%
if errorlevel 1 (
  popd >nul
  exit /b 11
)

echo [GENESIS] Installing CMake artifacts...
cmake --install "%ROOT%\out\build\%PRESET%" --config %CONFIG% --prefix "%INSTALL_DIR%"
if errorlevel 1 (
  popd >nul
  exit /b 12
)

if exist "%ROOT%\chatgptAPI.txt" (
  copy /Y "%ROOT%\chatgptAPI.txt" "%INSTALL_DIR%\chatgptAPI.txt" >nul
  echo [GENESIS] Staged chatgptAPI.txt into install root.
) else (
  echo [GENESIS] WARN: chatgptAPI.txt was not found in the repo root. ChatGPT research will stay disabled until the file is added.
)

set APP_EXE=%INSTALL_DIR%\GenesisEngineState\Binaries\Win64\Editor\GenesisEngine.exe
set RUNTIME_EXE=%INSTALL_DIR%\GenesisEngineState\Binaries\Win64\Runtime\GenesisRuntime.exe
set REMOTE_EXE=%INSTALL_DIR%\GenesisEngineState\Binaries\Win64\Runtime\GenesisRemote.exe
if not exist "%APP_EXE%" (
  echo [GENESIS] ERROR: install did not produce GenesisEngine.exe
  popd >nul
  exit /b 13
)
if not exist "%RUNTIME_EXE%" (
  echo [GENESIS] ERROR: install did not produce GenesisRuntime.exe
  popd >nul
  exit /b 14
)
if not exist "%REMOTE_EXE%" (
  echo [GENESIS] ERROR: install did not produce GenesisRemote.exe
  popd >nul
  exit /b 15
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$install='%INSTALL_DIR%';" ^
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
  "  $sc.WorkingDirectory=$install;" ^
  "  $sc.IconLocation=$app + ',0';" ^
  "  $sc.Description='Genesis Engine Win64 Vulkan editor';" ^
  "  $sc.Save();" ^
  "}" ^
  "$remoteSc=$shell.CreateShortcut($remoteShortcut);" ^
  "$remoteSc.TargetPath=$remote;" ^
  "$remoteSc.WorkingDirectory=$install;" ^
  "$remoteSc.IconLocation=$remote + ',0';" ^
  "$remoteSc.Description='Genesis Engine remote control bridge';" ^
  "$remoteSc.Save();"
if errorlevel 1 (
  echo [GENESIS] ERROR: failed to create shortcuts
  popd >nul
  exit /b 16
)

popd >nul

echo [GENESIS] Install complete.
echo [GENESIS] Launch shortcut: %APP_EXE%
echo [GENESIS] Remote control : %REMOTE_EXE% chat="your message here"
exit /b 0
