#include "vk_ui_layer.hpp"
#include "GE_app.hpp"

namespace ewv {

namespace {
constexpr int kContentBrowserHeight = 260;
}

VkUiLayer::VkUiLayer(App* app) : app_(app) {}

void VkUiLayer::Initialize(HWND main_hwnd) {
    hwnd_main_ = main_hwnd;
}

void VkUiLayer::SetTheme(const EwUiTheme& theme) {
    theme_ = theme;
}

void VkUiLayer::UpdateLayout(int width, int height, bool content_visible, int topbar_height, uint32_t right_dock_width, bool right_dock_visible) {
    client_width_ = width;
    client_height_ = height;
    content_visible_ = content_visible;
    topbar_height_ = topbar_height;
    right_dock_width_ = right_dock_visible ? right_dock_width : 0;
    right_dock_visible_ = right_dock_visible;

    panels_.clear();
    RECT top_bar{0, 0, width, topbar_height_};
    panels_.push_back({"Top Bar", top_bar, true, false});

    RECT viewport{0, topbar_height_, width - (int)right_dock_width_, height - (content_visible_ ? kContentBrowserHeight : 0)};
    panels_.push_back({"Viewport", viewport, true, false});

    if (right_dock_visible_ && right_dock_width_ > 0) {
        RECT right_dock{width - (int)right_dock_width_, topbar_height_, width, viewport.bottom};
        panels_.push_back({"Right Dock", right_dock, true, false});
    }

    if (content_visible_) {
        RECT content{0, viewport.bottom, width, height};
        panels_.push_back({"Content Browser", content, true, false});
    }
}

void VkUiLayer::BindToggle(const std::string& name, bool* state) {
    if (name.empty() || !state) return;
    toggles_[name] = state;
}

void VkUiLayer::Render(VkCommandBuffer cmd, VkExtent2D extent) {
    if (cmd == VK_NULL_HANDLE) return;
    (void)extent;
    (void)app_;
    // TODO: render gold-themed Unreal-style panels. For now we keep the layout alive for future hooks.
}

} // namespace ewv
