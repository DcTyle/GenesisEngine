#include "openxr_runtime.hpp"

#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(GENESIS_HAS_OPENXR) && GENESIS_HAS_OPENXR

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_VULKAN

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

// Vulkan handles are passed as void* to avoid including vulkan headers here.
#include <vulkan/vulkan.h>

static bool xr_ok(XrResult r) { return r == XR_SUCCESS; }

struct EwOpenXRRuntime::Impl {
    bool enabled = false;
    bool has_eye_gaze = false;

    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system_id = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local_space = XR_NULL_HANDLE;

    XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
    bool session_running = false;

    // Eye gaze action
    XrActionSet action_set = XR_NULL_HANDLE;
    XrAction gaze_action = XR_NULL_HANDLE;
    XrSpace gaze_space = XR_NULL_HANDLE;

    // Vulkan enable2 extension fns
    PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR = nullptr;
    PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensionsKHR = nullptr;
    PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR = nullptr;

    // Cached extension strings (space-separated per OpenXR spec)
    char vk_instance_exts[4096]{};
    char vk_device_exts[4096]{};

    // Frame timing
    XrFrameState frame_state{XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo wait_info{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameBeginInfo begin_info{XR_TYPE_FRAME_BEGIN_INFO};
    XrFrameEndInfo end_info{XR_TYPE_FRAME_END_INFO};

    EwOpenXRGazeRay last_gaze{};

    // Stereo rendering
    bool swapchains_ready = false;
    int64_t chosen_vk_format = 0;
    uint32_t view_count = 0;
    std::vector<XrViewConfigurationView> view_cfg_views;
    std::vector<XrView> views;

    struct SwapchainPerView {
        XrSwapchain sc = XR_NULL_HANDLE;
        uint32_t w = 0;
        uint32_t h = 0;
        std::vector<XrSwapchainImageVulkanKHR> images;
        std::vector<VkImageView> image_views;
        std::vector<bool> layout_valid;
        uint32_t acquired_index = 0;
        bool acquired = false;
    };
    std::vector<SwapchainPerView> sc_views;
    VkDevice bound_vk_device = VK_NULL_HANDLE;

    void shutdown() {
        if (session != XR_NULL_HANDLE) {
            for (auto& sv : sc_views) {
                for (auto v : sv.image_views) if (v) vkDestroyImageView(bound_vk_device, v, nullptr);
                sv.image_views.clear();
                sv.images.clear();
                sv.layout_valid.clear();
                if (sv.sc != XR_NULL_HANDLE) xrDestroySwapchain(sv.sc);
                sv.sc = XR_NULL_HANDLE;
            }
            sc_views.clear();
            swapchains_ready = false;
            bound_vk_device = VK_NULL_HANDLE;
            if (gaze_space != XR_NULL_HANDLE) { xrDestroySpace(gaze_space); gaze_space = XR_NULL_HANDLE; }
            if (local_space != XR_NULL_HANDLE) { xrDestroySpace(local_space); local_space = XR_NULL_HANDLE; }
            if (gaze_action != XR_NULL_HANDLE) { xrDestroyAction(gaze_action); gaze_action = XR_NULL_HANDLE; }
            if (action_set != XR_NULL_HANDLE) { xrDestroyActionSet(action_set); action_set = XR_NULL_HANDLE; }
            xrDestroySession(session);
            session = XR_NULL_HANDLE;
        }
        if (instance != XR_NULL_HANDLE) {
            xrDestroyInstance(instance);
            instance = XR_NULL_HANDLE;
        }
        enabled = false;
        has_eye_gaze = false;
        session_running = false;
        session_state = XR_SESSION_STATE_UNKNOWN;
        std::memset(vk_instance_exts, 0, sizeof(vk_instance_exts));
        std::memset(vk_device_exts, 0, sizeof(vk_device_exts));
    }
};

EwOpenXRRuntime::EwOpenXRRuntime() : impl_(new Impl()) {}
EwOpenXRRuntime::~EwOpenXRRuntime() {
    if (impl_) {
        impl_->shutdown();
        delete impl_;
        impl_ = nullptr;
    }
}

bool EwOpenXRRuntime::Init() {
    if (!impl_) return false;
    if (impl_->enabled) return true;

    // Enumerate extensions.
    uint32_t ext_count = 0;
    if (!xr_ok(xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr))) return false;
    if (ext_count == 0 || ext_count > 1024) return false;
    std::vector<XrExtensionProperties> exts(ext_count, {XR_TYPE_EXTENSION_PROPERTIES});
    if (!xr_ok(xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, exts.data()))) return false;

    auto has_ext = [&](const char* name) {
        for (const auto& e : exts) if (std::strcmp(e.extensionName, name) == 0) return true;
        return false;
    };

    // We require Vulkan enable2 to be production-grade on Vulkan.
    if (!has_ext(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)) return false;

    // Eye gaze is optional.
    impl_->has_eye_gaze = has_ext(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);

    std::vector<const char*> enable_exts;
    enable_exts.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
    if (impl_->has_eye_gaze) enable_exts.push_back(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);

    XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
    ici.enabledExtensionCount = (uint32_t)enable_exts.size();
    ici.enabledExtensionNames = enable_exts.data();
    std::snprintf(ici.applicationInfo.applicationName, sizeof(ici.applicationInfo.applicationName), "%s", "GenesisEngine");
    ici.applicationInfo.applicationVersion = 1;
    std::snprintf(ici.applicationInfo.engineName, sizeof(ici.applicationInfo.engineName), "%s", "GenesisEngine");
    ici.applicationInfo.engineVersion = 1;
    ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

    if (!xr_ok(xrCreateInstance(&ici, &impl_->instance))) {
        impl_->shutdown();
        return false;
    }

    // Load required function pointers.
    xrGetInstanceProcAddr(impl_->instance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction*)&impl_->xrGetVulkanInstanceExtensionsKHR);
    xrGetInstanceProcAddr(impl_->instance, "xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction*)&impl_->xrGetVulkanDeviceExtensionsKHR);
    xrGetInstanceProcAddr(impl_->instance, "xrGetVulkanGraphicsRequirements2KHR", (PFN_xrVoidFunction*)&impl_->xrGetVulkanGraphicsRequirements2KHR);

    if (!impl_->xrGetVulkanInstanceExtensionsKHR || !impl_->xrGetVulkanDeviceExtensionsKHR || !impl_->xrGetVulkanGraphicsRequirements2KHR) {
        impl_->shutdown();
        return false;
    }

    // Acquire system.
    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!xr_ok(xrGetSystem(impl_->instance, &sgi, &impl_->system_id))) {
        impl_->shutdown();
        return false;
    }

    // Query Vulkan extension strings.
    uint32_t inst_sz = 0;
    impl_->xrGetVulkanInstanceExtensionsKHR(impl_->instance, impl_->system_id, 0, &inst_sz, nullptr);
    if (inst_sz > sizeof(impl_->vk_instance_exts) - 1) inst_sz = (uint32_t)(sizeof(impl_->vk_instance_exts) - 1);
    if (!xr_ok(impl_->xrGetVulkanInstanceExtensionsKHR(impl_->instance, impl_->system_id, (uint32_t)sizeof(impl_->vk_instance_exts), &inst_sz, impl_->vk_instance_exts))) {
        impl_->shutdown();
        return false;
    }

    uint32_t dev_sz = 0;
    impl_->xrGetVulkanDeviceExtensionsKHR(impl_->instance, impl_->system_id, 0, &dev_sz, nullptr);
    if (dev_sz > sizeof(impl_->vk_device_exts) - 1) dev_sz = (uint32_t)(sizeof(impl_->vk_device_exts) - 1);
    if (!xr_ok(impl_->xrGetVulkanDeviceExtensionsKHR(impl_->instance, impl_->system_id, (uint32_t)sizeof(impl_->vk_device_exts), &dev_sz, impl_->vk_device_exts))) {
        impl_->shutdown();
        return false;
    }

    impl_->enabled = true;
    return true;
}

const char* EwOpenXRRuntime::VulkanInstanceExtensions() const {
    if (!impl_ || !impl_->enabled) return "";
    return impl_->vk_instance_exts;
}

const char* EwOpenXRRuntime::VulkanDeviceExtensions() const {
    if (!impl_ || !impl_->enabled) return "";
    return impl_->vk_device_exts;
}

bool EwOpenXRRuntime::HasOpenXR() const { return impl_ && impl_->enabled; }
bool EwOpenXRRuntime::HasEyeGaze() const { return impl_ && impl_->enabled && impl_->has_eye_gaze; }

static void split_exts(const char* s, std::vector<const char*>& out, std::vector<std::string>& storage) {
    storage.clear();
    out.clear();
    if (!s) return;
    const char* p = s;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ' ') ++p;
        storage.emplace_back(start, (size_t)(p - start));
    }
    for (auto& st : storage) out.push_back(st.c_str());
}

bool EwOpenXRRuntime::BindVulkan(void* vk_instance, void* vk_phys, void* vk_device, uint32_t queue_family, uint32_t queue_index) {
    if (!impl_ || !impl_->enabled) return false;
    if (impl_->session != XR_NULL_HANDLE) return true;

    // Validate graphics requirements (best-effort).
    XrGraphicsRequirementsVulkan2KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    impl_->xrGetVulkanGraphicsRequirements2KHR(impl_->instance, impl_->system_id, &req);

    XrGraphicsBindingVulkan2KHR gb{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    gb.instance = (VkInstance)vk_instance;
    gb.physicalDevice = (VkPhysicalDevice)vk_phys;
    gb.device = (VkDevice)vk_device;
    gb.queueFamilyIndex = queue_family;
    gb.queueIndex = queue_index;

    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &gb;
    sci.systemId = impl_->system_id;
    if (!xr_ok(xrCreateSession(impl_->instance, &sci, &impl_->session))) {
        return false;
    }

    impl_->bound_vk_device = (VkDevice)vk_device;

    // Local reference space.
    XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    if (!xr_ok(xrCreateReferenceSpace(impl_->session, &rsci, &impl_->local_space))) {
        return false;
    }

    // Action set for gaze.
    XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::snprintf(asci.actionSetName, sizeof(asci.actionSetName), "%s", "genesis_actions");
    std::snprintf(asci.localizedActionSetName, sizeof(asci.localizedActionSetName), "%s", "Genesis Actions");
    asci.priority = 0;
    xr_ok(xrCreateActionSet(impl_->instance, &asci, &impl_->action_set));

    if (impl_->has_eye_gaze && impl_->action_set != XR_NULL_HANDLE) {
        XrActionCreateInfo aci{XR_TYPE_ACTION_CREATE_INFO};
        aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
        std::snprintf(aci.actionName, sizeof(aci.actionName), "%s", "eye_gaze_pose");
        std::snprintf(aci.localizedActionName, sizeof(aci.localizedActionName), "%s", "Eye Gaze Pose");
        xr_ok(xrCreateAction(impl_->action_set, &aci, &impl_->gaze_action));

        // Bind to XR_EXT_eye_gaze_interaction path.
        XrPath profile = XR_NULL_PATH;
        xrStringToPath(impl_->instance, "/interaction_profiles/ext/eye_gaze_interaction", &profile);
        XrPath binding_path = XR_NULL_PATH;
        xrStringToPath(impl_->instance, "/user/eyes_ext/input/gaze_ext/pose", &binding_path);

        XrActionSuggestedBinding bind{impl_->gaze_action, binding_path};
        XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        sb.interactionProfile = profile;
        sb.suggestedBindings = &bind;
        sb.countSuggestedBindings = 1;
        xrSuggestInteractionProfileBindings(impl_->instance, &sb);

        // Attach action sets.
        XrSessionActionSetsAttachInfo aai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        aai.countActionSets = 1;
        aai.actionSets = &impl_->action_set;
        xrAttachSessionActionSets(impl_->session, &aai);

        // Create space for the gaze action.
        XrActionSpaceCreateInfo spci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spci.action = impl_->gaze_action;
        spci.poseInActionSpace.orientation.w = 1.0f;
        xr_ok(xrCreateActionSpace(impl_->session, &spci, &impl_->gaze_space));
    }

    // Frame end info baseline.
    impl_->end_info.displayTime = 0;
    impl_->end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    impl_->end_info.layerCount = 0;
    impl_->end_info.layers = nullptr;

    return true;
}

static bool xr_pick_vk_format(EwOpenXRRuntime::Impl* impl, int64_t preferred, int64_t* out_fmt) {
    uint32_t count = 0;
    if (!xr_ok(xrEnumerateSwapchainFormats(impl->session, 0, &count, nullptr))) return false;
    if (count == 0 || count > 256) return false;
    std::vector<int64_t> fmts(count);
    if (!xr_ok(xrEnumerateSwapchainFormats(impl->session, count, &count, fmts.data()))) return false;
    // Prefer exact match.
    for (auto f : fmts) {
        if (f == preferred) { *out_fmt = f; return true; }
    }
    // Prefer common sRGB if present.
    for (auto f : fmts) {
        if (f == (int64_t)VK_FORMAT_B8G8R8A8_SRGB) { *out_fmt = f; return true; }
    }
    // Fall back to first.
    *out_fmt = fmts[0];
    return true;
}

bool EwOpenXRRuntime::EnsureSwapchains(uint64_t preferred_vk_format) {
    if (!impl_ || !impl_->enabled || impl_->session == XR_NULL_HANDLE) return false;
    if (!impl_->session_running) return false;
    if (impl_->swapchains_ready) return true;
    if (impl_->bound_vk_device == VK_NULL_HANDLE) return false;

    // Query stereo view configuration.
    uint32_t vcount = 0;
    if (!xr_ok(xrEnumerateViewConfigurationViews(impl_->instance, impl_->system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &vcount, nullptr))) return false;
    if (vcount == 0 || vcount > 4) return false;
    impl_->view_count = vcount;
    impl_->view_cfg_views.assign(vcount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (!xr_ok(xrEnumerateViewConfigurationViews(impl_->instance, impl_->system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, vcount, &vcount, impl_->view_cfg_views.data()))) return false;
    impl_->views.assign(vcount, {XR_TYPE_VIEW});

    int64_t fmt = 0;
    if (!xr_pick_vk_format(impl_, (int64_t)preferred_vk_format, &fmt)) return false;
    impl_->chosen_vk_format = fmt;

    impl_->sc_views.resize(vcount);
    for (uint32_t i = 0; i < vcount; ++i) {
        auto& sv = impl_->sc_views[i];
        sv.w = impl_->view_cfg_views[i].recommendedImageRectWidth;
        sv.h = impl_->view_cfg_views[i].recommendedImageRectHeight;
        XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sci.format = fmt;
        sci.sampleCount = impl_->view_cfg_views[i].recommendedSwapchainSampleCount;
        sci.width = sv.w;
        sci.height = sv.h;
        sci.faceCount = 1;
        sci.arraySize = 1;
        sci.mipCount = 1;
        if (!xr_ok(xrCreateSwapchain(impl_->session, &sci, &sv.sc))) return false;
        uint32_t ic = 0;
        xrEnumerateSwapchainImages(sv.sc, 0, &ic, nullptr);
        if (ic == 0 || ic > 16) return false;
        sv.images.assign(ic, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        if (!xr_ok(xrEnumerateSwapchainImages(sv.sc, ic, &ic, (XrSwapchainImageBaseHeader*)sv.images.data()))) return false;
        sv.image_views.resize(ic, VK_NULL_HANDLE);
        sv.layout_valid.assign(ic, false);
        for (uint32_t j = 0; j < ic; ++j) {
            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image = sv.images[j].image;
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = (VkFormat)fmt;
            vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vci.subresourceRange.levelCount = 1;
            vci.subresourceRange.layerCount = 1;
            if (vkCreateImageView(impl_->bound_vk_device, &vci, nullptr, &sv.image_views[j]) != VK_SUCCESS) return false;
        }
    }

    impl_->swapchains_ready = true;
    return true;
}

void EwOpenXRRuntime::PollEvents() {
    if (!impl_ || !impl_->enabled || impl_->instance == XR_NULL_HANDLE) return;
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xr_ok(xrPollEvent(impl_->instance, &ev))) {
        switch (ev.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                const auto* sc = (const XrEventDataSessionStateChanged*)&ev;
                impl_->session_state = sc->state;
                if (impl_->session != XR_NULL_HANDLE) {
                    if (sc->state == XR_SESSION_STATE_READY && !impl_->session_running) {
                        XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                        bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        if (xr_ok(xrBeginSession(impl_->session, &bi))) {
                            impl_->session_running = true;
                        }
                    } else if (sc->state == XR_SESSION_STATE_STOPPING && impl_->session_running) {
                        xrEndSession(impl_->session);
                        impl_->session_running = false;
                    }
                }
            } break;
            default: break;
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }

    // Sync actions (gaze).
    if (impl_->session_running && impl_->action_set != XR_NULL_HANDLE) {
        XrActiveActionSet aas{impl_->action_set, XR_NULL_PATH};
        XrActionsSyncInfo asi{XR_TYPE_ACTIONS_SYNC_INFO};
        asi.countActiveActionSets = 1;
        asi.activeActionSets = &aas;
        xrSyncActions(impl_->session, &asi);
    }
}

void EwOpenXRRuntime::BeginFrame() {
    if (!impl_ || !impl_->enabled || !impl_->session_running) return;
    if (!xr_ok(xrWaitFrame(impl_->session, &impl_->wait_info, &impl_->frame_state))) return;
    xrBeginFrame(impl_->session, &impl_->begin_info);

    // Locate stereo views for this frame.
    if (impl_->swapchains_ready && impl_->local_space != XR_NULL_HANDLE) {
        XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime = impl_->frame_state.predictedDisplayTime;
        vli.space = impl_->local_space;
        XrViewState vs{XR_TYPE_VIEW_STATE};
        uint32_t vc = 0;
        if (xr_ok(xrLocateViews(impl_->session, &vli, &vs, impl_->view_count, &vc, impl_->views.data())) && vc == impl_->view_count) {
            // Acquire swapchain images.
            for (uint32_t i = 0; i < impl_->view_count; ++i) {
                auto& sv = impl_->sc_views[i];
                sv.acquired = false;
                XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                uint32_t idx = 0;
                if (!xr_ok(xrAcquireSwapchainImage(sv.sc, &ai, &idx))) continue;
                XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi.timeout = XR_INFINITE_DURATION;
                if (!xr_ok(xrWaitSwapchainImage(sv.sc, &wi))) continue;
                sv.acquired = true;
                sv.acquired_index = idx;
            }
        }
    }

    // Update gaze pose using predicted display time.
    impl_->last_gaze.valid = false;
    if (impl_->has_eye_gaze && impl_->gaze_space != XR_NULL_HANDLE && impl_->local_space != XR_NULL_HANDLE) {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if (xr_ok(xrLocateSpace(impl_->gaze_space, impl_->local_space, impl_->frame_state.predictedDisplayTime, &loc))) {
            const XrSpaceLocationFlags want = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if ((loc.locationFlags & want) == want) {
                const auto& p = loc.pose.position;
                const auto& q = loc.pose.orientation;
                // Quaternion -> forward direction (-Z in OpenXR)
                // d = q * (0,0,-1) * conj(q)
                const float x = q.x, y = q.y, z = q.z, w = q.w;
                // Rotation of vector (0,0,-1)
                // Using simplified quaternion-vector rotation.
                float vx = 0.f, vy = 0.f, vz = -1.f;
                float tx = 2.f * (y*vz - z*vy);
                float ty = 2.f * (z*vx - x*vz);
                float tz = 2.f * (x*vy - y*vx);
                float rx = vx + w*tx + (y*tz - z*ty);
                float ry = vy + w*ty + (z*tx - x*tz);
                float rz = vz + w*tz + (x*ty - y*tx);

                impl_->last_gaze.valid = true;
                impl_->last_gaze.origin[0] = p.x;
                impl_->last_gaze.origin[1] = p.y;
                impl_->last_gaze.origin[2] = p.z;
                // Normalize
                float l = std::sqrt(rx*rx + ry*ry + rz*rz);
                if (l > 1e-6f) { rx/=l; ry/=l; rz/=l; }
                impl_->last_gaze.dir[0] = rx;
                impl_->last_gaze.dir[1] = ry;
                impl_->last_gaze.dir[2] = rz;
            }
        }
    }
}

void EwOpenXRRuntime::EndFrame() {
    if (!impl_ || !impl_->enabled || !impl_->session_running) return;
    impl_->end_info.displayTime = impl_->frame_state.predictedDisplayTime;
    // Release swapchain images (caller must have completed rendering).
    if (impl_->swapchains_ready) {
        for (uint32_t i = 0; i < impl_->view_count; ++i) {
            auto& sv = impl_->sc_views[i];
            if (sv.acquired) {
                XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(sv.sc, &ri);
                sv.acquired = false;
            }
        }
    }

    // Build a projection layer when swapchains are ready.
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::vector<XrCompositionLayerProjectionView> pviews;
    if (impl_->swapchains_ready && impl_->view_count == impl_->sc_views.size()) {
        pviews.assign(impl_->view_count, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
        for (uint32_t i = 0; i < impl_->view_count; ++i) {
            auto& sv = impl_->sc_views[i];
            pviews[i].pose = impl_->views[i].pose;
            pviews[i].fov = impl_->views[i].fov;
            pviews[i].subImage.swapchain = sv.sc;
            pviews[i].subImage.imageRect.offset = {0,0};
            pviews[i].subImage.imageRect.extent = {(int32_t)sv.w, (int32_t)sv.h};
        }
        layer.space = impl_->local_space;
        layer.viewCount = (uint32_t)pviews.size();
        layer.views = pviews.data();
        const XrCompositionLayerBaseHeader* layers[1] = { (const XrCompositionLayerBaseHeader*)&layer };
        impl_->end_info.layerCount = 1;
        impl_->end_info.layers = layers;
        xrEndFrame(impl_->session, &impl_->end_info);
        impl_->end_info.layerCount = 0;
        impl_->end_info.layers = nullptr;
        return;
    }
    // No layers.
    impl_->end_info.layerCount = 0;
    impl_->end_info.layers = nullptr;
    xrEndFrame(impl_->session, &impl_->end_info);
}

EwOpenXRGazeRay EwOpenXRRuntime::GetGazeRay() const {
    if (!impl_ || !impl_->enabled) return EwOpenXRGazeRay{};
    return impl_->last_gaze;
}

uint32_t EwOpenXRRuntime::ViewCount() const { return impl_ && impl_->swapchains_ready ? impl_->view_count : 0; }

void* EwOpenXRRuntime::AcquiredImage(uint32_t view_index) const {
    if (!impl_ || !impl_->swapchains_ready || view_index >= impl_->view_count) return nullptr;
    const auto& sv = impl_->sc_views[view_index];
    if (!sv.acquired) return nullptr;
    return (void*)sv.images[sv.acquired_index].image;
}

void* EwOpenXRRuntime::AcquiredImageView(uint32_t view_index) const {
    if (!impl_ || !impl_->swapchains_ready || view_index >= impl_->view_count) return nullptr;
    const auto& sv = impl_->sc_views[view_index];
    if (!sv.acquired) return nullptr;
    return (void*)sv.image_views[sv.acquired_index];
}

uint32_t EwOpenXRRuntime::ViewWidth(uint32_t view_index) const {
    if (!impl_ || !impl_->swapchains_ready || view_index >= impl_->view_count) return 0;
    return impl_->sc_views[view_index].w;
}
uint32_t EwOpenXRRuntime::ViewHeight(uint32_t view_index) const {
    if (!impl_ || !impl_->swapchains_ready || view_index >= impl_->view_count) return 0;
    return impl_->sc_views[view_index].h;
}

bool EwOpenXRRuntime::GetViewPoseFov(uint32_t view_index, float out_pos3[3], float out_quat4[4], float out_fov4[4]) const {
    if (!impl_ || !impl_->swapchains_ready || view_index >= impl_->view_count) return false;
    const auto& v = impl_->views[view_index];
    if (out_pos3) { out_pos3[0] = v.pose.position.x; out_pos3[1] = v.pose.position.y; out_pos3[2] = v.pose.position.z; }
    if (out_quat4) { out_quat4[0] = v.pose.orientation.x; out_quat4[1] = v.pose.orientation.y; out_quat4[2] = v.pose.orientation.z; out_quat4[3] = v.pose.orientation.w; }
    if (out_fov4) { out_fov4[0] = v.fov.angleLeft; out_fov4[1] = v.fov.angleRight; out_fov4[2] = v.fov.angleUp; out_fov4[3] = v.fov.angleDown; }
    return true;
}

#else

// No-OpenXR build: production desktop viewport remains functional.
struct EwOpenXRRuntime::Impl {};

EwOpenXRRuntime::EwOpenXRRuntime() : impl_(new Impl()) {}
EwOpenXRRuntime::~EwOpenXRRuntime() { delete impl_; impl_ = nullptr; }
bool EwOpenXRRuntime::Init() { return false; }
const char* EwOpenXRRuntime::VulkanInstanceExtensions() const { return ""; }
const char* EwOpenXRRuntime::VulkanDeviceExtensions() const { return ""; }
bool EwOpenXRRuntime::HasOpenXR() const { return false; }
bool EwOpenXRRuntime::HasEyeGaze() const { return false; }
bool EwOpenXRRuntime::BindVulkan(void*, void*, void*, uint32_t, uint32_t) { return false; }
void EwOpenXRRuntime::PollEvents() {}
void EwOpenXRRuntime::BeginFrame() {}
void EwOpenXRRuntime::EndFrame() {}
bool EwOpenXRRuntime::EnsureSwapchains(uint64_t) { return false; }
uint32_t EwOpenXRRuntime::ViewCount() const { return 0; }
void* EwOpenXRRuntime::AcquiredImage(uint32_t) const { return nullptr; }
void* EwOpenXRRuntime::AcquiredImageView(uint32_t) const { return nullptr; }
uint32_t EwOpenXRRuntime::ViewWidth(uint32_t) const { return 0; }
uint32_t EwOpenXRRuntime::ViewHeight(uint32_t) const { return 0; }
bool EwOpenXRRuntime::GetViewPoseFov(uint32_t, float*, float*, float*) const { return false; }
EwOpenXRGazeRay EwOpenXRRuntime::GetGazeRay() const { return EwOpenXRGazeRay{}; }

#endif
