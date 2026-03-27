#include "photon_confinement.hpp"
#include <stdexcept>

namespace {

void photon_confinement_kernel_launcher(PhotonConfinement::PhotonState* states,
                                        int n,
                                        float dt,
                                        float field_strength,
                                        float voltage,
                                        float amperage,
                                        float frequency,
                                        float amplitude) {
    if (!states || n <= 0) return;
    // Physical constants
    const float e = 1.602176634e-19f; // elementary charge (C)
    const float epsilon_0 = 8.854187817e-12f; // vacuum permittivity (F/m)
    const float c = 2.99792458e8f; // speed of light (m/s)
    // Silicon constants (from nist_silicon_reference.json)
    const float lattice_constant = 5.431020511e-10f; // meters
    const float density = 2.33f; // g/cm^3
    const float atomic_weight = 28.0855f; // u
    const float mean_excitation_energy = 173.0f; // eV
    for (int i = 0; i < n; ++i) {
        // Add environmental noise (temperature, ambient frequency)
        float temp_noise = 0.001f * (temperature - 300.0f) * ((rand() % 2001 - 1000) / 1000.0f); // ±0.1% per 100K
        float ambient_noise = 0.001f * ambient_freq * ((rand() % 2001 - 1000) / 1000.0f); // ±0.1%
        // Store parameters for logging
        states[i].voltage = voltage;
        states[i].amperage = amperage;
        states[i].frequency = frequency;
        states[i].amplitude = amplitude;
        states[i].temperature = temperature;
        states[i].ambient_freq = ambient_freq;
        if (mode == PhotonConfinement::SimulationMode::TemporalDynamicsOnLattice) {
            // Mode 1: Temporal dynamics applied to silicon lattice characteristics
            float current = amperage;
            float resistance = 1.0f;
            float voltage_drop = current * resistance;
            float r = std::sqrt(states[i].x * states[i].x + states[i].y * states[i].y + states[i].z * states[i].z) + 1e-8f;
            float k_e = 1.0f / (4.0f * 3.14159265359f * epsilon_0);
            float coulomb_force = k_e * e * e / (r * r);
            float omega = 2.0f * 3.14159265359f * frequency;
            float amplitude_confinement = 1.0f / (1.0f + 0.1f * amplitude);
            float electric_field = voltage / r;
            states[i].x += (states[i].px * dt * amplitude_confinement + voltage_drop * dt + temp_noise);
            states[i].y += (states[i].py * dt * amplitude_confinement + voltage_drop * dt + temp_noise);
            states[i].z += (states[i].pz * dt * amplitude_confinement + voltage_drop * dt + temp_noise);
            states[i].px += (electric_field + coulomb_force + ambient_noise) * dt;
            states[i].py += (electric_field + coulomb_force + ambient_noise) * dt;
            states[i].pz += (electric_field + coulomb_force + ambient_noise) * dt;
            states[i].energy += omega * dt + ambient_noise;
            float damp = 1.0f - field_strength * dt;
            float safe_damp = (damp < 0.0f) ? 0.0f : damp;
            states[i].px *= safe_damp;
            states[i].py *= safe_damp;
            states[i].pz *= safe_damp;
            states[i].time += dt;
        } else if (mode == PhotonConfinement::SimulationMode::Baseline9DStabilization) {
            // Mode 2: Baseline 9D accounting, output parameters for temporal dynamics
            // Only stabilize the silicon lattice and output the parameters
            // (No direct temporal force application)
            float lattice_stabilizer = 1.0f / (1.0f + 0.01f * lattice_constant);
            states[i].x *= lattice_stabilizer;
            states[i].y *= lattice_stabilizer;
            states[i].z *= lattice_stabilizer;
            states[i].px *= lattice_stabilizer;
            states[i].py *= lattice_stabilizer;
            states[i].pz *= lattice_stabilizer;
            // Output parameters for temporal dynamics (could be logged or passed to next phase)
            states[i].energy = mean_excitation_energy;
            states[i].time += dt;
        }
    }
}
    }
    // Silicon constants (from nist_silicon_reference.json)
    const float lattice_constant = 5.431020511e-10f; // meters
    const float density = 2.33f; // g/cm^3
    const float atomic_weight = 28.0855f; // u
    const float mean_excitation_energy = 173.0f; // eV
}

void photon_confinement_kernel_launcher(PhotonConfinement::PhotonState* states,
                                        int n,
                                        float dt,
                                        float field_strength,
                                        float voltage,
                                        float amperage,
                                        float frequency,
                                        float amplitude,
                                        float temperature,
                                        float ambient_freq) {
    if (!states || n <= 0) return;
    const float damp = 1.0f - field_strength * dt;
    const float safe_damp = (damp < 0.0f) ? 0.0f : damp;
    // Physical constants
    const float e = 1.602176634e-19f; // elementary charge (C)
    const float epsilon_0 = 8.854187817e-12f; // vacuum permittivity (F/m)
    const float c = 2.99792458e8f; // speed of light (m/s)
    for (int i = 0; i < n; ++i) {
        // Ohm's Law: V = I * R, assume R = 1 for normalized units
        float current = amperage;
        float resistance = 1.0f;
        float voltage_drop = current * resistance;
        // Coulomb's Law: F = k_e * q1 * q2 / r^2, assume q1 = q2 = e, r = |x|
        float r = std::sqrt(states[i].x * states[i].x + states[i].y * states[i].y + states[i].z * states[i].z) + 1e-8f;
        float k_e = 1.0f / (4.0f * 3.14159265359f * epsilon_0);
        float coulomb_force = k_e * e * e / (r * r);
        // Frequency as angular frequency (omega)
        float omega = 2.0f * 3.14159265359f * frequency;
        // Amplitude as spatial confinement (reduces spread)
        float amplitude_confinement = 1.0f / (1.0f + 0.1f * amplitude);
        // Field theory: E = V/d, assume d = r
        float electric_field = voltage / r;
        // Add environmental noise (temperature, ambient frequency)
        float temp_noise = 0.001f * (temperature - 300.0f) * ((rand() % 2001 - 1000) / 1000.0f); // ±0.1% per 100K
        float ambient_noise = 0.001f * ambient_freq * ((rand() % 2001 - 1000) / 1000.0f); // ±0.1%
        // Update position and momentum (continuous timing)
        states[i].x += (states[i].px * dt * amplitude_confinement + voltage_drop * dt + temp_noise);
        states[i].y += (states[i].py * dt * amplitude_confinement + voltage_drop * dt + temp_noise);
        states[i].z += (states[i].pz * dt * amplitude_confinement + voltage_drop * dt + temp_noise);
        states[i].px += (electric_field + coulomb_force + ambient_noise) * dt;
        states[i].py += (electric_field + coulomb_force + ambient_noise) * dt;
        states[i].pz += (electric_field + coulomb_force + ambient_noise) * dt;
        // Frequency modulates energy
        states[i].energy += omega * dt + ambient_noise;
        // Dampen momentum to simulate confinement
        float damp = 1.0f - field_strength * dt;
        float safe_damp = (damp < 0.0f) ? 0.0f : damp;
        states[i].px *= safe_damp;
        states[i].py *= safe_damp;
        states[i].pz *= safe_damp;
        states[i].time += dt;
        // Store parameters for logging
        states[i].voltage = voltage;
        states[i].amperage = amperage;
        states[i].frequency = frequency;
        states[i].amplitude = amplitude;
        states[i].temperature = temperature;
        states[i].ambient_freq = ambient_freq;
    }
}

} // namespace

void PhotonConfinement::simulate_step(std::vector<PhotonState>& states, float dt, float field_strength,
                                      float voltage, float amperage, float frequency, float amplitude) {
    if (states.empty()) return;
    photon_confinement_kernel_launcher(states.data(), static_cast<int>(states.size()), dt, field_strength,
                                       voltage, amperage, frequency, amplitude, temperature, ambient_freq, mode);
}

// Log detected temporal coupling event with all metrics
void PhotonConfinement::log_temporal_coupling_event(const PhotonState& state, int tick) {
    static FILE* logf = nullptr;
    if (!logf) {
        logf = fopen("photon_temporal_coupling_events.csv", "w");
        if (logf) {
            fprintf(logf, "tick,x,y,z,px,py,pz,energy,time,voltage,amperage,frequency,amplitude\n");
        }
    }
    if (logf) {
        fprintf(logf, "%d,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g\n",
                tick,
                state.x, state.y, state.z,
                state.px, state.py, state.pz,
                state.energy, state.time,
                state.voltage, state.amperage, state.frequency, state.amplitude);
    }
    // Log detected temporal coupling event with all metrics, including environment and observed values
    fprintf(logf, "%d,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g\n",
            tick,
            state.x, state.y, state.z,
            state.px, state.py, state.pz,
            state.energy, state.time,
            state.voltage, state.amperage, state.frequency, state.amplitude,
            state.temperature, state.ambient_freq,
            state.observed_energy, state.observed_time);
}

// Convolve photon state with NIST instrument response (linewidth, uncertainty)
void PhotonConfinement::apply_nist_observation(std::vector<PhotonState>& states, float linewidth_MHz, float uncertainty_kHz) {
    // For each photon, convolve energy/time with a Gaussian of linewidth/uncertainty
    // (Stub: replace with real convolution as needed)
    for (auto& state : states) {
        // Simple Gaussian noise model for observed values
        float sigma_energy = linewidth_MHz * 1e6f * 6.62607015e-34f; // J (E=h*f)
        float sigma_time = uncertainty_kHz * 1e3f * 1e-9f; // ns
        state.observed_energy = state.energy + sigma_energy * ((rand() % 2001 - 1000) / 1000.0f);
        state.observed_time = state.time + sigma_time * ((rand() % 2001 - 1000) / 1000.0f);
    }
}
}
