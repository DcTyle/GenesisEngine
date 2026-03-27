#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

#include "EwGlyphIcon.hpp"

struct EwIconButtonInfo {
    EwGlyphIcon icon;
};

extern std::unordered_map<HWND, EwIconButtonInfo> g_icon_button_registry;

struct Theme {
    COLORREF bg = RGB(30,30,30);
    COLORREF panel = RGB(40,40,40);
    COLORREF panel_alt = RGB(50,50,50);
    COLORREF edit_bg = RGB(60,60,60);
    COLORREF border_soft = RGB(70,70,70);
    COLORREF border = RGB(100,100,100);
    COLORREF text = RGB(220,220,220);
    COLORREF gold = RGB(255,215,0);
    COLORREF gold_dim = RGB(200,170,60);
    COLORREF gold_hot = RGB(255,230,100);
    COLORREF highlight_bg = RGB(60,60,90);
    COLORREF bg_elevated = RGB(80,80,80);
};
extern Theme g_theme;

extern HBRUSH g_brush_bg;
extern HBRUSH g_brush_panel;
extern HBRUSH g_brush_panel_alt;
extern HBRUSH g_brush_edit;
extern HBRUSH g_brush_splitter;
extern HBRUSH g_brush_topbar;
extern HFONT g_font_ui;
extern HFONT g_font_ui_bold;
extern HFONT g_font_ui_small;
extern HFONT g_font_icon;

COLORREF ew_theme_button_fill(bool, bool, bool, bool);
COLORREF ew_theme_button_border(bool, bool, bool);
COLORREF ew_theme_button_text(bool, bool);
void ew_trace_renderer_step(const char*);
std::string wide_to_utf8(const std::wstring&);
