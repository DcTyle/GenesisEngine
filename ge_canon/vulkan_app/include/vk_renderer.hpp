#pragma once
#include <vulkan/vulkan.h>
#include <windows.h>
#include <cstdint>

struct EwVulkanRenderer {
    VkInstance instance = VK_NULL_HANDLE;
    bool enable_validation = false;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{0,0};

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    VkSemaphore sem_acquire = VK_NULL_HANDLE;
    VkSemaphore sem_present = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VkImage images[8]{};
    VkImageView image_views[8]{};
    uint32_t image_count = 0;
    bool image_valid_layout[8]{};

    // ------------------------------------------------------------
    // Ancillabit-driven pulse loop (GPU pulse -> substrate -> GPU)
    //
    // Baseline implementation uses GPU timestamps as the pulse signal.
    // The resulting pulse delta updates a tiny ancillabit state vector
    // that can immediately influence the next frame (e.g., clear color
    // modulation, and later shader parameter blocks).
    // ------------------------------------------------------------
    VkQueryPool ts_query_pool = VK_NULL_HANDLE;
    float ts_period_ns = 0.0f;          // VkPhysicalDeviceLimits::timestampPeriod
    uint64_t last_gpu_dt_ns = 0;        // last measured GPU delta (ns)
    uint64_t frame_index = 0;
    float anc_phase = 0.0f;             // evolving phase carrier
    float anc_amp = 0.0f;               // derived amplitude (0..1)

    // Capability snapshot: maximum supported 2D texture dimension.
    // Genesis target is 32k textures (32768) where hardware allows.
    uint32_t max_image_dim_2d = 0;

    bool initialized = false;
};

bool ew_vk_init(EwVulkanRenderer& r, HWND hwnd_surface);
void ew_vk_shutdown(EwVulkanRenderer& r);
bool ew_vk_resize(EwVulkanRenderer& r, HWND hwnd_surface);

// Render one frame. clear_rgba is 0xAARRGGBB.
bool ew_vk_draw(EwVulkanRenderer& r, uint32_t clear_rgba);
