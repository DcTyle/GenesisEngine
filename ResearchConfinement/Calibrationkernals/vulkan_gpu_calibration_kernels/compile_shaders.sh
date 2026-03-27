#!/usr/bin/env bash
set -euo pipefail
mkdir -p shaders/bin
glslangValidator -V shaders/gpu_calibration.comp -o shaders/bin/gpu_calibration.spv
glslangValidator -V shaders/trajectory_update.comp -o shaders/bin/trajectory_update.spv
echo "Compiled shaders to shaders/bin"
