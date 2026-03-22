@echo off
setlocal enabledelayedexpansion

if "%~1"=="" (
  echo Usage: validate_package_win64.bat ^<PackageRoot^>
  exit /b 2
)

set PKG=%~1

if not exist "%PKG%\GenesisEngineState\Binaries\Win64\Runtime\GenesisRuntime.exe" (
  echo VALIDATE_FAIL: missing GenesisRuntime.exe
  exit /b 3
)

if not exist "%PKG%\GenesisEngineState\Binaries\Win64\Runtime\GenesisRemote.exe" (
  echo VALIDATE_FAIL: missing GenesisRemote.exe
  exit /b 4
)

if not exist "%PKG%\GenesisEngineState\Binaries\Win64\Editor\GenesisEngine.exe" (
  echo VALIDATE_FAIL: missing GenesisEngine.exe
  exit /b 6
)

if not exist "%PKG%\GenesisEngineState\Binaries\Win64\GENESIS_RUNTIME_EDITOR_SPLIT_MANIFEST.txt" (
  echo VALIDATE_FAIL: missing runtime/editor split manifest
  exit /b 7
)

if not exist "%PKG%\GenesisEngineState\Branding\genesis.ico" (
  echo VALIDATE_FAIL: missing application icon
  exit /b 8
)

if not exist "%PKG%\GenesisEngineState\Branding\genesis_splash.bmp" (
  echo VALIDATE_FAIL: missing splash bitmap
  exit /b 9
)

for %%F in (
  "%PKG%\GenesisEngineState\Binaries\Win64\Runtime\shaders\instanced.vert.spv"
  "%PKG%\GenesisEngineState\Binaries\Win64\Runtime\shaders\instanced.frag.spv"
) do (
  if not exist %%F (
    echo VALIDATE_FAIL: missing shader %%~fF
    exit /b 5
  )
)

echo VALIDATE_OK: structure
exit /b 0
