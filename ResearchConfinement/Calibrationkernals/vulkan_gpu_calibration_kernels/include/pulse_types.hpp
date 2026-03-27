#pragma once

#include <cstdint>

namespace vkcal {

struct alignas(16) Vec4 {
    float x;
    float y;
    float z;
    float w;
};

struct alignas(16) PulseInput {
    float frequencyNorm;
    float amplitudeNorm;
    float voltageNorm;
    float currentNorm;
    float phaseRad;
    float pad0;
    float pad1;
    float pad2;
};

struct alignas(16) FeedbackInput {
    float sentSignal;
    float measuredSignal;
    float integratedFeedback;
    float derivativeSignal;
};

struct alignas(16) EnvironmentInput {
    float latticeClosure;
    float phaseClosure;
    float recurrenceAlignment;
    float conservationAlignment;
    float thermalNoise;
    float fieldNoise;
    float pad0;
    float pad1;
};

struct alignas(16) CalibrationOutput {
    float environmentCoupling;
    float trackingAffinity;
    float accumulationAffinity;
    float smoothingAffinity;

    float calibratedFrequency;
    float calibratedAmplitude;
    float calibratedVoltage;
    float calibratedCurrent;

    float persistence;
    float leakage;
    float actuationGain;
    float pulseSignal;
};

struct alignas(16) TrajectoryState {
    Vec4 position;
    Vec4 velocity;
};

struct alignas(16) DispatchConfig {
    uint32_t taskMode; // 0 tracking, 1 accumulation, 2 smoothing
    uint32_t elementCount;
    float dt;
    float phaseScale;
};

} // namespace vkcal
