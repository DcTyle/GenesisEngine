// AppConfig: Application configuration struct for ewv::App
// STL includes must be outside namespace
#include <string>
#include <vector>
namespace ewv {
struct AppConfig {
    std::string app_title_utf8 = "GenesisEngine";
    int initial_width = 1280;
    int initial_height = 720;
    std::vector<std::string> startup_commands_utf8;
    bool start_live_mode = false;
    bool start_resonance_view = false;
    bool start_confinement_particles = false;
    bool start_visualization = true;
    std::string output_log_path_utf8;
    std::string app_user_model_id_utf8;
};
}
// Only include once, outside struct definitions
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "include/Mesh.hpp"
#include "include/Object.hpp"
#include "include/ObjectStore.hpp"
#include "GE_runtime.hpp"
#include "include/UITheme.hpp"

namespace ewv {
struct App {
    // App constructor
    App(const AppConfig& cfg);
    // Main run loop
    int Run(HINSTANCE hInst);
    struct VkCtx {
        VkDevice dev = VK_NULL_HANDLE;
        VkQueue gfxq = VK_NULL_HANDLE;
        uint32_t gfxq_family = 0;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSwapchainKHR swap = VK_NULL_HANDLE;
        VkFormat swap_format = VK_FORMAT_B8G8R8A8_UNORM;
        VkExtent2D swap_extent{};
        std::vector<VkImage> swap_images;
        std::vector<VkImageView> swap_views;
        VkCommandPool cmdpool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> cmdbufs;
        VkSemaphore sem_image = VK_NULL_HANDLE;
        VkSemaphore sem_render = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkBuffer instance_buf = VK_NULL_HANDLE;
        VkDeviceMemory instance_mem = VK_NULL_HANDLE;
        void* instance_mapped = nullptr;
        VkDeviceSize instance_capacity = 0;
        VkDescriptorSetLayout ds_layout = VK_NULL_HANDLE;
        VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
        VkPipeline pipe = VK_NULL_HANDLE;
        VkDescriptorPool ds_pool = VK_NULL_HANDLE;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        VkImage depth_image = VK_NULL_HANDLE;
        VkDeviceMemory depth_mem = VK_NULL_HANDLE;
        VkImageView depth_view = VK_NULL_HANDLE;
        VkSampler depth_sampler = VK_NULL_HANDLE;
        VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
        VkDescriptorSetLayout cam_ds_layout = VK_NULL_HANDLE;
        VkPipelineLayout cam_pipe_layout = VK_NULL_HANDLE;
        VkPipeline cam_hist_pipe = VK_NULL_HANDLE;
        VkPipeline cam_median_pipe = VK_NULL_HANDLE;
        VkDescriptorPool cam_ds_pool = VK_NULL_HANDLE;
        VkDescriptorSet cam_hist_ds = VK_NULL_HANDLE;
        VkDescriptorSet cam_median_ds = VK_NULL_HANDLE;
        VkBuffer cam_hist_buf = VK_NULL_HANDLE;
        VkDeviceMemory cam_hist_mem = VK_NULL_HANDLE;
        VkBuffer cam_out_buf = VK_NULL_HANDLE;
        VkDeviceMemory cam_out_mem = VK_NULL_HANDLE;
        void* cam_out_mapped = nullptr;
        VkImage vt_atlas_image = VK_NULL_HANDLE;
        VkDeviceMemory vt_atlas_mem = VK_NULL_HANDLE;
        VkImageView vt_atlas_view = VK_NULL_HANDLE;
        VkSampler vt_atlas_sampler = VK_NULL_HANDLE;
        VkBuffer vt_pagetable_buf = VK_NULL_HANDLE;
        VkDeviceMemory vt_pagetable_mem = VK_NULL_HANDLE;
        uint32_t vt_virtual_dim = 32768;
        uint32_t vt_tile_size = 128;
        uint32_t vt_atlas_dim = 4096;
        uint32_t vt_mip_count = 1;
        uint32_t vt_mip_offset[16]{};
        uint32_t vt_mip_tiles_per_row[16]{};
        std::vector<uint32_t> vt_entries;
        struct VtSlot { uint32_t last_use_tick = 0; uint32_t virt_key = 0; bool used = false; };
        std::vector<VtSlot> vt_slots;
        uint32_t vt_tiles_per_row_atlas = 0;
        bool enable_validation = false;
        VkDebugUtilsMessengerEXT dbg = VK_NULL_HANDLE;
        void DestroySwap();
        void DestroyAll();
    };
    // ... other App members ...
};
} // namespace ewv
