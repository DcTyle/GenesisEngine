# Vulkan GPU Calibration Kernels

This package contains toy Vulkan compute kernels and a C++ host scaffold for the pulse-calibration model developed in the prior simulation passes.

What is included:
- `shaders/gpu_calibration.comp` — computes environment coupling, task affinity, calibrated pulse values, persistence, leakage, and actuation gain.
- `shaders/trajectory_update.comp` — applies the calibrated pulse signal to trajectory state vectors.
- `include/pulse_types.hpp` — shared C++ structs that mirror the shader-side storage layout.
- `src/main.cpp` — minimal Vulkan host scaffold showing buffer setup, descriptor bindings, push constants, and dispatch order.
- `CMakeLists.txt` — simple project file.
- `compile_shaders.bat` / `compile_shaders.sh` — helper scripts to compile GLSL compute shaders to SPIR-V using `glslangValidator`.

Important scope note:
This is a normalized toy control model. It is not a validated hardware law for silicon carriers, particles, or GPU firmware. The kernels are intended to operationalize the calibration logic inside a Vulkan compute workflow.

Dispatch order:
1. Fill input buffers with `PulseInput`, `FeedbackInput`, `EnvironmentInput`, and `TrajectoryState`.
2. Dispatch `gpu_calibration.comp`.
3. Insert a compute-to-compute memory barrier.
4. Dispatch `trajectory_update.comp`.
5. Read back `TrajectoryState` and `CalibrationOutput` as needed.

Control centers encoded from the prior runs:
- Tracking: frequency ≈ 0.2450, amplitude ≈ 0.18, voltage ≈ 0.36, current ≈ 0.36
- Accumulation: frequency ≈ 0.1775, amplitude ≈ 0.18, voltage ≈ 0.36, current ≈ 0.44
- Smoothing: frequency ≈ 0.1750, amplitude ≈ 0.18, voltage ≈ 0.36, current ≈ 0.36
