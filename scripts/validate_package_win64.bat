@echo off
setlocal enabledelayedexpansion

if "%~1"=="" (
  echo Usage: validate_package_win64.bat ^<PackageRoot^>
  exit /b 2
)

set PKG=%~1

if not exist "%PKG%\Binaries\Win64\genesis_game.exe" (
  echo VALIDATE_FAIL: missing genesis_game.exe
  exit /b 3
)

if not exist "%PKG%\Content\content_index.gecontent" (
  echo VALIDATE_FAIL: missing content_index.gecontent
  exit /b 4
)

REM Forbidden folders check (fail-closed)
for %%D in (AI Corpus Generated Sim Editor) do (
  if exist "%PKG%\%%D" (
    echo VALIDATE_FAIL: forbidden dir present %%D
    exit /b 5
  )
  if exist "%PKG%\Content\%%D" (
    echo VALIDATE_FAIL: forbidden dir present Content\%%D
    exit /b 6
  )
)

echo VALIDATE_OK: structure

REM Smoke test: run packaged game in smoke mode from the package root.
pushd "%PKG%" >NUL
"%PKG%\Binaries\Win64\genesis_game.exe" --smoke_test --ticks=600
set EC=%ERRORLEVEL%
popd >NUL

if not "%EC%"=="0" (
  echo VALIDATE_FAIL: smoke test exit code %EC%
  exit /b %EC%
)

echo VALIDATE_OK: smoke

REM Invariance test: save/load/tick equivalence check (deterministic).
pushd "%PKG%" >NUL
"%PKG%\Binaries\Win64\genesis_game.exe" --invariance_test --ticks_before_save=300 --ticks_after_load=300
set EC=%ERRORLEVEL%
popd >NUL

if not "%EC%"=="0" (
  echo VALIDATE_FAIL: invariance test exit code %EC%
  exit /b %EC%
)

REM Stability test: long-run symplectic N-body energy drift bound.
REM This is intentionally moderate so CI/validation doesn't take forever.
pushd "%PKG%" >NUL
"%PKG%\Binaries\Win64\genesis_game.exe" --stability_test --ticks=20000 --max_drift_ppm=5000
set EC=%ERRORLEVEL%
popd >NUL

if not "%EC%"=="0" (
  echo VALIDATE_FAIL: stability test exit code %EC%
  exit /b %EC%
)

echo VALIDATE_OK: stability
echo VALIDATE_OK: invariance
exit /b 0
