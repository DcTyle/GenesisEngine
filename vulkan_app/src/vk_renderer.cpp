#include "vk_renderer.hpp"
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstdio>

static bool ew_env_truthy(const char* name) {
    const char* v = std::getenv(name);
    if (!v) return false;
    if (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T') return true;
    return false;
}

static bool vk_check(VkResult r) { return r == VK_SUCCESS; }

static VKAPI_ATTR VkBool32 VKAPI_CALL ew_vk_debug_cb(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    // Keep output minimal but actionable.
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && data && data->pMessage) {
        OutputDebugStringA("[GenesisEngine] ");
        OutputDebugStringA(data->pMessage);
        OutputDebugStringA("\n");
    }
    return VK_FALSE;
}

static VkInstance create_instance(bool enable_validation, VkDebugUtilsMessengerEXT* out_dbg) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "GenesisEngine";
    app.applicationVersion = VK_MAKE_VERSION(1,0,0);
    app.pEngineName = "GenesisEngine";
    app.engineVersion = VK_MAKE_VERSION(1,0,0);
    app.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> exts;
    exts.push_back("VK_KHR_surface");
    exts.push_back("VK_KHR_win32_surface");
    if (enable_validation) {
        exts.push_back("VK_EXT_debug_utils");
    }

    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    if (enable_validation) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = layers;
    }

    VkInstance inst = VK_NULL_HANDLE;
    if (!vk_check(vkCreateInstance(&ci, nullptr, &inst))) return VK_NULL_HANDLE;

    if (enable_validation && out_dbg) {
        auto fpCreate = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT");
        if (fpCreate) {
            VkDebugUtilsMessengerCreateInfoEXT di{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            di.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            di.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            di.pfnUserCallback = ew_vk_debug_cb;
            fpCreate(inst, &di, nullptr, out_dbg);
        }
    }
    return inst;
}

static bool pick_device(EwVulkanRenderer& r) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(r.instance, &count, nullptr);
    if (count == 0) return false;
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(r.instance, &count, devs.data());

    // Prefer discrete if present.
    VkPhysicalDevice best = devs[0];
    for (auto d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { best = d; break; }
    }
    r.phys = best;

    uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(r.phys, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> qprops(qcount);
    vkGetPhysicalDeviceQueueFamilyProperties(r.phys, &qcount, qprops.data());

    for (uint32_t i=0;i<qcount;i++) {
        if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            r.queue_family = i;
            return true;
        }
    }
    return false;
}

static bool create_device(EwVulkanRenderer& r) {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = r.queue_family;
    q.queueCount = 1;
    q.pQueuePriorities = &prio;

    const char* exts[] = { "VK_KHR_swapchain" };

    // Vulkan 1.3 core features required by the viewport app (dynamic rendering + sync2).
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = &f2;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &q;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = exts;

    if (!vk_check(vkCreateDevice(r.phys, &ci, nullptr, &r.device))) return false;
    vkGetDeviceQueue(r.device, r.queue_family, 0, &r.queue);

    // Timestamp period for GPU pulse capture.
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(r.phys, &props);
    r.ts_period_ns = props.limits.timestampPeriod; // ns per timestamp tick

    // Texture capability snapshot. Genesis targets 32k textures when available.
    r.max_image_dim_2d = props.limits.maxImageDimension2D;
    // Fail-closed policy: do not assume 32k support; clamp at runtime.
    // (No allocation occurs here; this is a capability record for later loaders.)
    return true;
}

static bool create_timestamp_pool(EwVulkanRenderer& r) {
    // Two timestamps per frame: begin + end.
    VkQueryPoolCreateInfo qci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qci.queryCount = 2;
    return vk_check(vkCreateQueryPool(r.device, &qci, nullptr, &r.ts_query_pool));
}

static bool create_surface(EwVulkanRenderer& r, HWND hwnd) {
    // Vulkan on 64-bit Windows still uses the Win32 surface extension types.
    VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    ci.hinstance = GetModuleHandleW(nullptr);
    ci.hwnd = hwnd;
    return vk_check(vkCreateWin32SurfaceKHR(r.instance, &ci, nullptr, &r.surface));
}

static bool create_swapchain(EwVulkanRenderer& r) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r.phys, r.surface, &caps);

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(r.phys, r.surface, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(r.phys, r.surface, &fmt_count, fmts.data());

    VkSurfaceFormatKHR chosen = fmts[0];
    for (auto f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }
    }
    r.swapchain_format = chosen.format;

    uint32_t pm_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(r.phys, r.surface, &pm_count, nullptr);
    std::vector<VkPresentModeKHR> pms(pm_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(r.phys, r.surface, &pm_count, pms.data());

    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : pms) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) { pm = m; break; }
    }

    // Extent
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFF) {
        extent.width = 1280;
        extent.height = 720;
    }
    r.extent = extent;

    const uint32_t desired_images = (caps.minImageCount + 1 <= caps.maxImageCount || caps.maxImageCount == 0) ? (caps.minImageCount + 1) : caps.minImageCount;

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = r.surface;
    ci.minImageCount = desired_images;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = pm;
    ci.clipped = VK_TRUE;

    if (!vk_check(vkCreateSwapchainKHR(r.device, &ci, nullptr, &r.swapchain))) return false;

    r.image_count = 0;
    vkGetSwapchainImagesKHR(r.device, r.swapchain, &r.image_count, nullptr);
    if (r.image_count > 8) r.image_count = 8;
    vkGetSwapchainImagesKHR(r.device, r.swapchain, &r.image_count, r.images);

    // Create swapchain image views for dynamic rendering.
    for (uint32_t i = 0; i < r.image_count; ++i) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = r.images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = r.swapchain_format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel = 0;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount = 1;
        if (!vk_check(vkCreateImageView(r.device, &vci, nullptr, &r.image_views[i]))) return false;
    }


    for (uint32_t i = 0; i < r.image_count; ++i) {
        r.image_valid_layout[i] = false;
    }

    return true;
}

static bool create_cmd(EwVulkanRenderer& r) {
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = r.queue_family;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (!vk_check(vkCreateCommandPool(r.device, &pci, nullptr, &r.cmd_pool))) return false;

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = r.cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (!vk_check(vkAllocateCommandBuffers(r.device, &ai, &r.cmd))) return false;

    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(r.device, &sci, nullptr, &r.sem_acquire);
    vkCreateSemaphore(r.device, &sci, nullptr, &r.sem_present);

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(r.device, &fci, nullptr, &r.fence);
    return true;
}

static void destroy_swapchain(EwVulkanRenderer& r) {
    if (r.swapchain) {
        for (uint32_t i = 0; i < r.image_count; ++i) {
            if (r.image_views[i]) { vkDestroyImageView(r.device, r.image_views[i], nullptr); r.image_views[i] = VK_NULL_HANDLE; }
            r.images[i] = VK_NULL_HANDLE;
            r.image_valid_layout[i] = false;
        }
        r.image_count = 0;
        vkDestroySwapchainKHR(r.device, r.swapchain, nullptr);
        r.swapchain = VK_NULL_HANDLE;
    }
}

bool ew_vk_init(EwVulkanRenderer& r, HWND hwnd_surface) {
    r = EwVulkanRenderer{};
    r.enable_validation = ew_env_truthy("EW_VK_VALIDATION");
    r.instance = create_instance(r.enable_validation, &r.debug_messenger);
    if (!r.instance) return false;
    if (!pick_device(r)) return false;
    if (!create_device(r)) return false;
    if (!create_surface(r, hwnd_surface)) return false;

    // Ensure queue family supports present.
    VkBool32 present_ok = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(r.phys, r.queue_family, r.surface, &present_ok);
    if (!present_ok) return false;

    if (!create_swapchain(r)) return false;
    if (!create_cmd(r)) return false;

    // GPU pulse capture (timestamps). If creation fails, we still run.
    create_timestamp_pool(r);

    r.initialized = true;
    return true;
}

bool ew_vk_resize(EwVulkanRenderer& r, HWND hwnd_surface) {
    if (!r.initialized) return false;
    // Some drivers report 0x0 while minimized. Defer until valid.
    RECT rc{};
    GetClientRect(hwnd_surface, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return true;

    vkDeviceWaitIdle(r.device);
    destroy_swapchain(r);
    return create_swapchain(r);
}

void ew_vk_shutdown(EwVulkanRenderer& r) {
    if (!r.initialized) return;

    vkDeviceWaitIdle(r.device);

    destroy_swapchain(r);

    if (r.ts_query_pool) { vkDestroyQueryPool(r.device, r.ts_query_pool, nullptr); r.ts_query_pool = VK_NULL_HANDLE; }

    if (r.fence) vkDestroyFence(r.device, r.fence, nullptr);
    if (r.sem_present) vkDestroySemaphore(r.device, r.sem_present, nullptr);
    if (r.sem_acquire) vkDestroySemaphore(r.device, r.sem_acquire, nullptr);

    if (r.cmd_pool) vkDestroyCommandPool(r.device, r.cmd_pool, nullptr);
    if (r.surface) vkDestroySurfaceKHR(r.instance, r.surface, nullptr);
    if (r.device) vkDestroyDevice(r.device, nullptr);

    if (r.enable_validation && r.debug_messenger && r.instance) {
        auto fpDestroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(r.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fpDestroy) {
            fpDestroy(r.instance, r.debug_messenger, nullptr);
        }
    }
    if (r.instance) vkDestroyInstance(r.instance, nullptr);

    r = EwVulkanRenderer{};
}

static uint32_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint32_t)v;
}

static uint32_t pack_argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) {
    return ((a & 0xFFu) << 24) | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}

static uint32_t ancillabit_modulate_clear(uint32_t base_argb, float amp01) {
    const int a = (int)((base_argb >> 24) & 0xFF);
    const int r = (int)((base_argb >> 16) & 0xFF);
    const int g = (int)((base_argb >> 8) & 0xFF);
    const int b = (int)((base_argb >> 0) & 0xFF);

    // Simple, deterministic modulation: move channels toward a phase-derived target.
    // This is intentionally cheap; real passes will project via shader parameter blocks.
    const float t = amp01;
    const int tr = 32 + (int)(223.0f * t);
    const int tg = 16 + (int)(239.0f * (1.0f - t));
    const int tb = 64 + (int)(191.0f * (0.5f + 0.5f * std::sin(6.2831853f * t)));

    const int or_ = (int)((1.0f - 0.35f) * r + 0.35f * tr);
    const int og_ = (int)((1.0f - 0.35f) * g + 0.35f * tg);
    const int ob_ = (int)((1.0f - 0.35f) * b + 0.35f * tb);
    return pack_argb((uint32_t)a, clamp_u8(or_), clamp_u8(og_), clamp_u8(ob_));
}

static VkClearColorValue unpack_rgba(uint32_t argb) {
    const float a = ((argb >> 24) & 0xFF) / 255.0f;
    const float r = ((argb >> 16) & 0xFF) / 255.0f;
    const float g = ((argb >> 8)  & 0xFF) / 255.0f;
    const float b = ((argb >> 0)  & 0xFF) / 255.0f;
    VkClearColorValue c{};
    c.float32[0] = r;
    c.float32[1] = g;
    c.float32[2] = b;
    c.float32[3] = a;
    return c;
}

bool ew_vk_draw(EwVulkanRenderer& r, uint32_t clear_rgba) {
    if (!r.initialized) return false;

    vkWaitForFences(r.device, 1, &r.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(r.device, 1, &r.fence);

    // ------------------------------------------------------------
    // GPU pulse -> ancillabit update (CPU-side baseline)
    //
    // Because we fence-wait at frame start, prior timestamps are available without
    // additional stalling. This yields a tight “immediate” loop: GPU work from the
    // previous submit produces a pulse that updates the ancillabit state for the
    // next submit.
    // ------------------------------------------------------------
    if (r.ts_query_pool && r.frame_index > 0) {
        uint64_t ts[2]{};
        VkResult qr = vkGetQueryPoolResults(
            r.device,
            r.ts_query_pool,
            0,
            2,
            sizeof(ts),
            ts,
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (qr == VK_SUCCESS && ts[1] > ts[0]) {
            const uint64_t ticks = (ts[1] - ts[0]);
            const double dt_ns = (double)ticks * (double)r.ts_period_ns;
            r.last_gpu_dt_ns = (uint64_t)dt_ns;

            // Three-lane pulse encoding lives in EigenWareCore; the renderer keeps
            // only a tiny projection-facing state here (phase + amplitude).
            const float dt_s = (float)(dt_ns * 1.0e-9);
            // Phase speed is clamped to be stable under large dt spikes.
            const float w = 2.0f * 3.14159265f * 0.20f; // 0.20 Hz carrier
            r.anc_phase += w * dt_s;
            if (r.anc_phase > 6.2831853f) r.anc_phase = std::fmod(r.anc_phase, 6.2831853f);
            r.anc_amp = 0.5f + 0.5f * std::sin(r.anc_phase);
        }
    }

    const bool use_anc_clear = ew_env_truthy("EW_ANCILLABIT_CLEAR");
    if (use_anc_clear) {
        clear_rgba = ancillabit_modulate_clear(clear_rgba, r.anc_amp);
    }

    uint32_t image_index = 0;
    VkResult acq = vkAcquireNextImageKHR(r.device, r.swapchain, UINT64_MAX, r.sem_acquire, VK_NULL_HANDLE, &image_index);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) return false;
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) return false;

    vkResetCommandBuffer(r.cmd, 0);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(r.cmd, &bi);

    // Reset timestamp queries and stamp begin/end around the dynamic rendering block.
    if (r.ts_query_pool) {
        vkCmdResetQueryPool(r.cmd, r.ts_query_pool, 0, 2);
        vkCmdWriteTimestamp(r.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, r.ts_query_pool, 0);
    }

    VkImage img = r.images[image_index];

    // Dynamic rendering path (Vulkan 1.3):
    // - Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL
    // - BeginRendering with loadOp=CLEAR
    // - EndRendering
    // - Transition to PRESENT_SRC_KHR

    VkClearColorValue c = unpack_rgba(clear_rgba);

    // Barrier: present/undefined -> color attachment
    VkImageMemoryBarrier2 barrier_in{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier_in.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier_in.srcAccessMask = 0;
    barrier_in.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier_in.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier_in.oldLayout = r.image_valid_layout[image_index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier_in.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier_in.image = img;
    barrier_in.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier_in.subresourceRange.baseMipLevel = 0;
    barrier_in.subresourceRange.levelCount = 1;
    barrier_in.subresourceRange.baseArrayLayer = 0;
    barrier_in.subresourceRange.layerCount = 1;

    VkDependencyInfo dep_in{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep_in.imageMemoryBarrierCount = 1;
    dep_in.pImageMemoryBarriers = &barrier_in;
    vkCmdPipelineBarrier2(r.cmd, &dep_in);

    VkClearValue clear{};
    clear.color = c;

    VkRenderingAttachmentInfo color_att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color_att.imageView = r.image_views[image_index];
    color_att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.clearValue = clear;

    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.offset = {0,0};
    ri.renderArea.extent = r.extent;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color_att;

    vkCmdBeginRendering(r.cmd, &ri);
    // NOTE: Actual drawing (meshes, particles, gizmos) hooks into this block.
    vkCmdEndRendering(r.cmd);

    if (r.ts_query_pool) {
        vkCmdWriteTimestamp(r.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, r.ts_query_pool, 1);
    }

    // Barrier: color attachment -> present
    VkImageMemoryBarrier2 barrier_out{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier_out.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier_out.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier_out.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    barrier_out.dstAccessMask = 0;
    barrier_out.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier_out.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier_out.image = img;
    barrier_out.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier_out.subresourceRange.baseMipLevel = 0;
    barrier_out.subresourceRange.levelCount = 1;
    barrier_out.subresourceRange.baseArrayLayer = 0;
    barrier_out.subresourceRange.layerCount = 1;

    VkDependencyInfo dep_out{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep_out.imageMemoryBarrierCount = 1;
    dep_out.pImageMemoryBarriers = &barrier_out;
    vkCmdPipelineBarrier2(r.cmd, &dep_out);

    vkEndCommandBuffer(r.cmd);

    r.image_valid_layout[image_index] = true;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &r.sem_acquire;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &r.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &r.sem_present;

    if (!vk_check(vkQueueSubmit(r.queue, 1, &si, r.fence))) return false;

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &r.sem_present;
    pi.swapchainCount = 1;
    pi.pSwapchains = &r.swapchain;
    pi.pImageIndices = &image_index;

    VkResult pr = vkQueuePresentKHR(r.queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) return false;
    r.frame_index++;
    return pr == VK_SUCCESS;
}
