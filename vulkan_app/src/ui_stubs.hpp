#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

// Icon enum (already defined in GE_app.cpp, but redeclare for completeness)
enum class EwGlyphIcon {
    None, Refresh, Settings, Grid, List, Columns, Reference, Back, Forward, Save, Folder, Stop, Replay, Play, Pause
};

// Icon button info stub
struct EwIconButtonInfo {
    EwGlyphIcon icon;
};

// Global icon button registry stub
static std::unordered_map<HWND, EwIconButtonInfo> g_icon_button_registry;

// Theme color struct stub
struct ThemeStub {
    COLORREF bg = RGB(30,30,30);
    COLORREF panel = RGB(40,40,40);
    COLORREF panel_alt = RGB(50,50,50);
    COLORREF edit_bg = RGB(60,60,60);
    COLORREF border_soft = RGB(70,70,70);
    COLORREF bg_elevated = RGB(80,80,80);
    COLORREF gold = RGB(255,215,0);
};
static ThemeStub g_theme;

// Global brush and font stubs
static HBRUSH g_brush_bg = nullptr;
static HBRUSH g_brush_panel = nullptr;
static HBRUSH g_brush_panel_alt = nullptr;
static HBRUSH g_brush_edit = nullptr;
static HBRUSH g_brush_splitter = nullptr;
static HBRUSH g_brush_topbar = nullptr;
static HFONT g_font_ui = nullptr;
static HFONT g_font_ui_bold = nullptr;
static HFONT g_font_ui_small = nullptr;
static HFONT g_font_icon = nullptr;

// Theme button color functions
static COLORREF ew_theme_button_fill(bool, bool, bool, bool) { return RGB(100,100,100); }
static COLORREF ew_theme_button_border(bool, bool, bool) { return RGB(120,120,120); }
static COLORREF ew_theme_button_text(bool, bool) { return RGB(255,255,255); }

// Dummy ew_trace_renderer_step
static void ew_trace_renderer_step(const char*) {}

// Dummy wide_to_utf8
static std::string wide_to_utf8(const std::wstring& s) { return std::string(s.begin(), s.end()); }
