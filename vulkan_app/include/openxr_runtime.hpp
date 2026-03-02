#pragma once

#include <cstdint>

// Production OpenXR integration layer for the Vulkan viewport.
//
// - Optional: compiled only when GENESIS_HAS_OPENXR=1 (OpenXR headers+loader present).
// - Deterministic: gaze ray is used only as an input signal to the existing
//   focus/LOD policy (no nondeterministic simulation side-effects).
// - Vulkan binding: uses XR_KHR_vulkan_enable2.

struct EwOpenXRGazeRay {
    bool valid = false;
    float origin[3]{0,0,0};
    float dir[3]{0,0,-1};
};

class EwOpenXRRuntime {
public:
    EwOpenXRRuntime();
    ~EwOpenXRRuntime();

    // Attempt to initialize OpenXR. Safe to call even if OpenXR is not present;
    // will return false and remain disabled.
    bool Init();

    // Vulkan extension requirements (queried from OpenXR). Valid after Init().
    // Strings are owned by the runtime and remain valid for its lifetime.
    const char* VulkanInstanceExtensions() const;
    const char* VulkanDeviceExtensions() const;

    bool HasOpenXR() const;
    bool HasEyeGaze() const;

    // Bind the already-created Vulkan instance/device to OpenXR and create a session.
    // Must be called after Vulkan init.
    bool BindVulkan(void* vk_instance, void* vk_phys, void* vk_device, uint32_t queue_family, uint32_t queue_index);

    // Per-frame: poll OpenXR events and advance session state.
    void PollEvents();

    // Per-frame: begin/end frame to obtain predicted display time for pose.
    // These are no-ops if the session isn't running.
    void BeginFrame();
    void EndFrame();

    // Rendering (XR presentation): create stereo swapchains and acquire images.
    // - Must be called after BindVulkan().
    // - If OpenXR is unavailable or session is not running, returns false.
    // - preferred_vk_format should match the main pipeline color format when possible.
    bool EnsureSwapchains(uint64_t preferred_vk_format);

    // Query current stereo view count (0 if not ready).
    uint32_t ViewCount() const;

    // Per-eye acquired image for the current BeginFrame() call.
    // Valid only when ViewCount()>0.
    void* AcquiredImage(uint32_t view_index) const;      // VkImage
    void* AcquiredImageView(uint32_t view_index) const;  // VkImageView
    uint32_t ViewWidth(uint32_t view_index) const;
    uint32_t ViewHeight(uint32_t view_index) const;

    // Per-eye pose + fov for the current frame (OpenXR local space).
    // out_fov4 = {angleLeft, angleRight, angleUp, angleDown}
    bool GetViewPoseFov(uint32_t view_index, float out_pos3[3], float out_quat4[4], float out_fov4[4]) const;

    // Update gaze ray. Returns last gaze ray (valid flag included).
    EwOpenXRGazeRay GetGazeRay() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
