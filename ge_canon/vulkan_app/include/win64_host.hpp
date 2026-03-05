#pragma once
#include <windows.h>
#include <string>
#include "camera_controller.hpp"

struct EwWin64Host {
    HINSTANCE hinst = nullptr;
    HWND hwnd_main = nullptr;
    HWND hwnd_view = nullptr;   // child window used as Vulkan surface

    HWND hwnd_ai_output = nullptr;
    HWND hwnd_ai_input = nullptr;
    HWND hwnd_ai_send = nullptr;

    HWND hwnd_obj_list = nullptr;
    HWND hwnd_obj_import = nullptr;

    HWND hwnd_transform = nullptr; // static label

    int32_t panel_width_px = 420;

    // UI events (latched by WM_COMMAND, consumed by app loop).
    bool ui_send_clicked = false;

    EwInputState input{};

    // Resize handling (viewport only).
    bool viewport_resize_pending = false;
    int viewport_width_px = 0;
    int viewport_height_px = 0;
};

bool ew_win32_create(EwWin64Host& host, const wchar_t* title);
void ew_win32_pump(EwWin64Host& host, bool& out_quit);
void ew_win32_layout(EwWin64Host& host);

// UI helpers
void ew_win32_ai_append_line(EwWin64Host& host, const std::string& line_utf8);
std::string ew_win32_ai_take_input(EwWin64Host& host);

// File dialog
bool ew_win32_open_file_dialog_obj(EwWin64Host& host, std::string& out_path_utf8);
