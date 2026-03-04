@echo off
setlocal

REM Build EigenWareCore + EigenWareUEAdapter with clang-cl (Visual Studio ClangCL toolset).
REM Run from a "x64 Native Tools Command Prompt for VS 2022" or equivalent.

set ROOT=%~dp0\..
pushd "%ROOT%"

if not exist build_core mkdir build_core

cmake -S . -B build_core -G "Visual Studio 17 2022" -A x64 -T ClangCL || exit /b 1
cmake --build build_core --config Release --target EigenWareUEAdapter || exit /b 1

REM Copy DLL into the plugin-local runtime location.
set OUTDIR=%ROOT%\EigenWareState\Binaries\Win64
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

copy /Y "build_core\ue_adapter\Release\EigenWareUEAdapter.dll" "%OUTDIR%\EigenWareUEAdapter.dll" || exit /b 1

echo.
echo Built and staged: %OUTDIR%\EigenWareUEAdapter.dll
echo.

popd
endlocal
