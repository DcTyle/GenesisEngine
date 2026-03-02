#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "camera_controller.hpp"
#include "openxr_runtime.hpp"

namespace ewv {

struct AppConfig {
    std::string app_title_utf8;
    int initial_width = 1280;
    int initial_height = 720;
};

class App {
public:
    explicit App(const AppConfig& cfg);
    int Run(HINSTANCE hInst);

private:
    AppConfig cfg_;

    // Win64
    HWND hwnd_main_ = nullptr;
    HWND hwnd_viewport_ = nullptr;
    HWND hwnd_panel_ = nullptr;

    // UI controls
    HWND hwnd_input_ = nullptr;
    HWND hwnd_send_ = nullptr;
    HWND hwnd_output_ = nullptr;
    HWND hwnd_import_ = nullptr;
    HWND hwnd_bootstrap_ = nullptr;
    HWND hwnd_objlist_ = nullptr;

    bool running_ = true;
    bool resized_ = false;
    int client_w_ = 0;
    int client_h_ = 0;

    // Subsystems
    struct VkCtx;
    struct Scene;
    VkCtx* vk_ = nullptr;
    Scene* scene_ = nullptr;
    EwOpenXRRuntime xr_{};

    // Viewport observer camera + input
    EwCamera cam_{};
    EwInputState input_{};

    // Visualization toggle: when false, the app runs headless (no continuous presentation)
    // but simulation and verification continue.
    bool visualize_enabled_ = true;

// View modes
    bool immersion_mode_ = false; // Standard vs Immersion
    float eye_offset_local_[3] = {0.0f, 0.0f, 1.65f};

    // Win64 plumbing
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void CreateMainWindow(HINSTANCE hInst);
    void CreateChildWindows();
    void LayoutChildren(int w, int h);

    void Tick();
    void Render();

    void OnSend();
    void OnImportObj();
    void OnBootstrapGame();

    void AppendOutputUtf8(const std::string& line);
};

} // namespace ewv
