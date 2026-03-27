#pragma once
#include <vector>
#include <cstdint>

// PhotonConfinement: C++ interface for photon confinement simulation (GPU-accelerated)
// This will be integrated with substrate microprocessor anchors in a later phase.

class PhotonConfinement {
public:
    enum class SimulationMode {
        TemporalDynamicsOnLattice, // Mode 1: Apply temporal dynamics to silicon lattice characteristics
        Baseline9DStabilization    // Mode 2: Baseline 9D accounting, output parameters for temporal dynamics
    };
public:
        float x, y, z;      // Position
        float px, py, pz;   // Momentum
        float energy;
        float time;
        // New: record voltage, amperage, frequency, amplitude for each photon
        float voltage;
        float amperage;
        float frequency;
        float amplitude;
    };

    // Simulate a step of photon confinement (calls CUDA kernel)
    // states: input/output photon states
    // dt: time step
    // field_strength: confinement field parameter
    // voltage, amperage, frequency, amplitude: new physical parameters
    static void simulate_step(std::vector<PhotonState>& states, float dt, float field_strength,
                             float voltage, float amperage, float frequency, float amplitude);

    // Log detected temporal coupling events with all metrics
    static void log_temporal_coupling_event(const PhotonState& state, int tick);
};
    struct PhotonState {
        float x, y, z;      // Position
        float px, py, pz;   // Momentum
        float energy;
        float time;         // Continuous time (high-res)
        float voltage;
        float amperage;
        float frequency;
        float amplitude;
        float temperature;  // Environmental temperature (K)
        float ambient_freq; // Dominant ambient frequency (Hz)
        // Optionally: observed_energy, observed_time (after NIST convolution)
        float observed_energy;
        float observed_time;
    };

    // Simulate a step of photon confinement (calls CUDA kernel)
    // Now includes environmental noise and continuous timing
    // states: input/output photon states
    // dt: time step (can be sub-step)
    // field_strength: confinement field parameter
    // voltage, amperage, frequency, amplitude: physical parameters
    // temperature: environmental temperature (K)
    // ambient_freq: dominant ambient frequency (Hz)
    // mode: selects which simulation mode to use
    static void simulate_step(std::vector<PhotonState>& states, float dt, float field_strength,
                             float voltage, float amperage, float frequency, float amplitude,
                             float temperature, float ambient_freq,
                             SimulationMode mode);

    // Log detected temporal coupling events with all metrics, including environment and observed values
    static void log_temporal_coupling_event(const PhotonState& state, int tick);

    // Convolve photon state with NIST instrument response (linewidth, uncertainty)
    static void apply_nist_observation(std::vector<PhotonState>& states, float linewidth_MHz, float uncertainty_kHz);
};
