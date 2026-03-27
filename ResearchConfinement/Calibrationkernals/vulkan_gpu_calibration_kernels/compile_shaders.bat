@echo off
if not exist shaders\bin mkdir shaders\bin
glslangValidator -V shaders\gpu_calibration.comp -o shaders\bin\gpu_calibration.spv
if errorlevel 1 exit /b 1
glslangValidator -V shaders\trajectory_update.comp -o shaders\bin\trajectory_update.spv
if errorlevel 1 exit /b 1
echo Compiled shaders to shaders\bin
