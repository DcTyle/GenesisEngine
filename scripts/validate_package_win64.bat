@echo off
setlocal enabledelayedexpansion

if "%~1"=="" (
  echo Usage: validate_package_win64.bat ^<PackageRoot^>
  exit /b 2
)

set PKG=%~1

if not exist "%PKG%\GenesisEngineState\Binaries\Win64\Runtime\genesis_runtime.exe" (
  echo VALIDATE_FAIL: missing genesis_runtime.exe
  exit /b 3
)

if not exist "%PKG%\GenesisEngineState\Binaries\Win64\Runtime\genesis_remote.exe" (
  echo VALIDATE_FAIL: missing genesis_remote.exe
  exit /b 4
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
