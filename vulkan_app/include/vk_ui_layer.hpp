#pragma once

#include <windows.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "ui_theme.hpp"

namespace ewv {

class App;

struct UiPanelLayout {
    std::string name;
    RECT rect{};
    bool visible = true;
    bool locked = false;
};

class VkUiLayer {
public:
    explicit VkUiLayer(App* app);

    void Initialize(HWND main_hwnd);
    void SetTheme(const EwUiTheme& theme);
    void UpdateLayout(int width, int height, bool content_visible, int topbar_height, uint32_t right_dock_width, bool right_dock_visible);

    void BindToggle(const std::string& name, bool* state);

    void Render(VkCommandBuffer cmd, VkExtent2D extent);

private:
    App* app_ = nullptr;
    HWND hwnd_main_ = nullptr;
    EwUiTheme theme_;
    std::vector<UiPanelLayout> panels_;
    std::unordered_map<std::string, bool*> toggles_;
    int client_width_ = 0;
    int client_height_ = 0;
    bool content_visible_ = true;
    int topbar_height_ = 0;
    uint32_t right_dock_width_ = 0;
    bool right_dock_visible_ = true;
};

} // namespace ewv
