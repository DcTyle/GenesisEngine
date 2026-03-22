#include "win64_host.hpp"
#include "ai_ui.hpp"

#include <string>

static EwWin64Host* g_host = nullptr;

static void ew_update_mod_keys(EwInputState& in) {
    in.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    in.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    in.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (!g_host) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        ew_win32_layout(*g_host);
        // Track viewport size for renderer swapchain recreation.
        if (g_host->hwnd_view) {
            RECT vr{};
            GetClientRect(g_host->hwnd_view, &vr);
            const int vw = vr.right - vr.left;
            const int vh = vr.bottom - vr.top;
            if (vw > 0 && vh > 0) {
                g_host->viewport_width_px = vw;
                g_host->viewport_height_px = vh;
                g_host->viewport_resize_pending = true;
            }
        }
        return 0;
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        g_host->input.mouse_dx += (x - g_host->input.mouse_x);
        g_host->input.mouse_dy += (y - g_host->input.mouse_y);
        g_host->input.mouse_x = x;
        g_host->input.mouse_y = y;
        return 0;
    }
    case WM_LBUTTONDOWN: g_host->input.lmb = true; SetCapture(hwnd); return 0;
    case WM_LBUTTONUP:   g_host->input.lmb = false; ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN: g_host->input.rmb = true; SetCapture(hwnd); return 0;
    case WM_RBUTTONUP:   g_host->input.rmb = false; ReleaseCapture(); return 0;
    case WM_MBUTTONDOWN: g_host->input.mmb = true; SetCapture(hwnd); return 0;
    case WM_MBUTTONUP:   g_host->input.mmb = false; ReleaseCapture(); return 0;
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        g_host->input.wheel_delta += (delta > 0) ? 1 : (delta < 0 ? -1 : 0);
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam < 256) g_host->input.key_down[wparam] = true;
        ew_update_mod_keys(g_host->input);
        return 0;
    case WM_KEYUP:
        if (wparam < 256) g_host->input.key_down[wparam] = false;
        ew_update_mod_keys(g_host->input);
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == 1002 || id == 1102 || id == 1101) {
            ew_ai_ui_handle_command(*g_host, id);
            return 0;
        }
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

static HWND make_child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(0, cls, text, style, 0,0,10,10, parent, (HMENU)(intptr_t)id, GetModuleHandleW(nullptr), nullptr);
}

bool ew_win32_create(EwWin64Host& host, const wchar_t* title) {
    host.hinst = GetModuleHandleW(nullptr);

    // Production default: per-monitor DPI awareness (crisp viewport + UI layout).
    // Safe no-op on older Windows.
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        using Fn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = (Fn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) {
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        FreeLibrary(user32);
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = host.hinst;
    wc.lpszClassName = L"GenesisEngineHost";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) {
        // ok if already registered
    }

    host.hwnd_main = CreateWindowExW(
        0,
        wc.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1440, 900,
        nullptr, nullptr,
        host.hinst,
        nullptr
    );

    if (!host.hwnd_main) return false;

    g_host = &host;

    // Child window for Vulkan surface.
    host.hwnd_view = make_child(host.hwnd_main, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 1001);

    // AI Panel.
    host.hwnd_ai_output = make_child(host.hwnd_main, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 1003);
    host.hwnd_ai_input  = make_child(host.hwnd_main, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 1004);
    host.hwnd_ai_send   = make_child(host.hwnd_main, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE, 1002);

    // Objects.
    host.hwnd_obj_list   = make_child(host.hwnd_main, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL, 1101);
    host.hwnd_obj_import = make_child(host.hwnd_main, L"BUTTON", L"Import OBJ", WS_CHILD | WS_VISIBLE, 1102);

    host.hwnd_transform  = make_child(host.hwnd_main, L"STATIC", L"Selected: (none)", WS_CHILD | WS_VISIBLE, 1201);

    ew_win32_layout(host);
    ew_ai_ui_init_controls(host);

    // Initialize viewport dimensions.
    RECT vr{};
    GetClientRect(host.hwnd_view, &vr);
    host.viewport_width_px = vr.right - vr.left;
    host.viewport_height_px = vr.bottom - vr.top;
    host.viewport_resize_pending = true;

    return true;
}

void ew_win32_layout(EwWin64Host& host) {
    if (!host.hwnd_main) return;

    RECT r{};
    GetClientRect(host.hwnd_main, &r);
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;

    const int panel_w = host.panel_width_px;
    const int view_w = (w > panel_w) ? (w - panel_w) : w;

    // Left: viewport
    MoveWindow(host.hwnd_view, 0, 0, view_w, h, TRUE);

    // Right panel layout.
    int x0 = view_w;
    int y = 8;

    const int pad = 8;
    const int cw = panel_w - 2*pad;

    MoveWindow(host.hwnd_obj_import, x0 + pad, y, cw, 28, TRUE);
    y += 32;

    MoveWindow(host.hwnd_obj_list, x0 + pad, y, cw, 160, TRUE);
    y += 168;

    MoveWindow(host.hwnd_transform, x0 + pad, y, cw, 22, TRUE);
    y += 28;

    MoveWindow(host.hwnd_ai_output, x0 + pad, y, cw, h - y - 70, TRUE);
    y = h - 60;

    MoveWindow(host.hwnd_ai_input, x0 + pad, y, cw - 88, 24, TRUE);
    MoveWindow(host.hwnd_ai_send, x0 + pad + (cw - 80), y, 80, 24, TRUE);
}

void ew_win32_pump(EwWin64Host& host, bool& out_quit) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            out_quit = true;
            return;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Keep modifier keys updated even if focus is on edit controls.
    ew_update_mod_keys(host.input);
}
