#include "UITheme.hpp"
#include <string>
#include <unordered_map>

std::unordered_map<HWND, EwIconButtonInfo> g_icon_button_registry;
Theme g_theme;
HBRUSH g_brush_bg = nullptr;
HBRUSH g_brush_panel = nullptr;
HBRUSH g_brush_panel_alt = nullptr;
HBRUSH g_brush_edit = nullptr;
HBRUSH g_brush_splitter = nullptr;
HBRUSH g_brush_topbar = nullptr;
HFONT g_font_ui = nullptr;
HFONT g_font_ui_bold = nullptr;
HFONT g_font_ui_small = nullptr;
HFONT g_font_icon = nullptr;

COLORREF ew_theme_button_fill(bool, bool, bool, bool) { return RGB(100,100,100); }
COLORREF ew_theme_button_border(bool, bool, bool) { return RGB(120,120,120); }
COLORREF ew_theme_button_text(bool, bool) { return RGB(255,255,255); }
void ew_trace_renderer_step(const char*) {}
std::string wide_to_utf8(const std::wstring& s) { return std::string(s.begin(), s.end()); }
