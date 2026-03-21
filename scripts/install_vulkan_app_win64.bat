@echo off
setlocal enabledelayedexpansion

set PRESET=win64-vs2022-clang-vulkan
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

echo [GENESIS] Configuring...
cmake --preset %PRESET%
if errorlevel 1 (
  popd >nul
  exit /b 10
)

echo [GENESIS] Building editor, runtime, and remote control...
cmake --build --preset %PRESET% --config %CONFIG% --target genesis_app genesis_runtime genesis_remote
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

set APP_EXE=%INSTALL_DIR%\GenesisEngineState\Binaries\Win64\Editor\genesis_app.exe
set REMOTE_EXE=%INSTALL_DIR%\GenesisEngineState\Binaries\Win64\Runtime\genesis_remote.exe
if not exist "%APP_EXE%" (
  echo [GENESIS] ERROR: install did not produce genesis_app.exe
  popd >nul
  exit /b 13
)
if not exist "%REMOTE_EXE%" (
  echo [GENESIS] ERROR: install did not produce genesis_remote.exe
  popd >nul
  exit /b 14
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$install='%INSTALL_DIR%';" ^
  "$app=Join-Path $install 'GenesisEngineState\Binaries\Win64\Editor\genesis_app.exe';" ^
  "$desktop=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Genesis Engine Viewport.lnk';" ^
  "$startMenu=Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Genesis Engine Viewport.lnk';" ^
  "$shell=New-Object -ComObject WScript.Shell;" ^
  "foreach($path in @($desktop,$startMenu)) {" ^
  "  $sc=$shell.CreateShortcut($path);" ^
  "  $sc.TargetPath=$app;" ^
  "  $sc.Arguments='--appid=GenesisEngine.Viewport.Editor';" ^
  "  $sc.WorkingDirectory=$install;" ^
  "  $sc.IconLocation=$app + ',0';" ^
  "  $sc.Description='Genesis Engine Vulkan editor with ChatGPT research and remote control';" ^
  "  $sc.Save();" ^
  "}"
if errorlevel 1 (
  echo [GENESIS] ERROR: failed to create shortcuts
  popd >nul
  exit /b 15
)

popd >nul

echo [GENESIS] Install complete.
echo [GENESIS] Launch shortcut: %APP_EXE%
echo [GENESIS] Remote control : %REMOTE_EXE% chat="your message here"
exit /b 0
