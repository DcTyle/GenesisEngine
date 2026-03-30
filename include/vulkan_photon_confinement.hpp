#pragma once
#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>

// VulkanPhotonConfinement: Vulkan compute backend for photon-native DMT simulation
class VulkanPhotonConfinement {
public:
    enum class SimulationMode {
        TemporalDynamicsOnLattice,
        Baseline9DStabilization,
        STOV
    };
    struct STOVData {
        std::vector<float> phase;
        std::vector<float> oam_density;
        std::vector<int> winding_number;
        std::vector<float> amplitude;
    };

    // Run the simulation for a number of steps, with all new parameters and mode
    // If mode == STOV, fills stov_data with per-cell results
    void simulate(std::vector<PhotonState>& states, int steps, float dt, float field_strength,
                 float voltage, float amperage, float frequency, float amplitude,
                 float temperature, float ambient_freq, SimulationMode mode,
                 STOVData* stov_data = nullptr);

    struct PhotonState {
        float x, y, z;
        float px, py, pz;
        float energy;
        float time;
        float voltage;
        float amperage;
        float frequency;
        float amplitude;
        float temperature;
        float ambient_freq;
        float observed_energy;
        float observed_time;
    };

    VulkanPhotonConfinement(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue computeQueue, uint32_t queueFamilyIndex);
    ~VulkanPhotonConfinement();

    // Run the simulation for a number of steps, with all new parameters and mode
    void simulate(std::vector<PhotonState>& states, int steps, float dt, float field_strength,
                 float voltage, float amperage, float frequency, float amplitude,
                 float temperature, float ambient_freq, SimulationMode mode);

private:
    // Vulkan handles and resources
    VkDevice device_;
    VkPhysicalDevice physicalDevice_;
    VkQueue computeQueue_;
    uint32_t queueFamilyIndex_;
    // ... (buffers, pipeline, etc.)
    void initVulkanResources();
    void cleanupVulkanResources();
};
