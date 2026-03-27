#pragma once

#include <windows.h>

namespace ewv {

struct EwUiTheme {
    COLORREF bg                 = RGB(8, 11, 16);
    COLORREF bg_elevated        = RGB(13, 18, 26);
    COLORREF panel              = RGB(20, 26, 36);
    COLORREF panel_alt          = RGB(27, 34, 46);
    COLORREF edit_bg            = RGB(17, 22, 31);
    COLORREF edit_bg_alt        = RGB(24, 31, 42);
    COLORREF text               = RGB(231, 236, 244);
    COLORREF muted              = RGB(182, 194, 211);
    COLORREF dim                = RGB(138, 151, 170);
    COLORREF border             = RGB(61, 79, 104);
    COLORREF border_soft        = RGB(43, 56, 74);
    COLORREF gold               = RGB(210, 171, 70);
    COLORREF gold_hot           = RGB(241, 204, 104);
    COLORREF gold_dim           = RGB(142, 115, 44);
    COLORREF gold_fill          = RGB(76, 61, 24);
    COLORREF gold_fill_hot      = RGB(98, 79, 31);
    COLORREF gold_fill_pressed  = RGB(116, 93, 38);
    COLORREF steel              = RGB(92, 124, 166);
    COLORREF steel_hot          = RGB(123, 161, 206);
    COLORREF steel_fill         = RGB(27, 36, 49);
    COLORREF highlight_bg       = RGB(35, 47, 63);
    COLORREF highlight_bg_strong= RGB(49, 64, 85);
    COLORREF badge_red          = RGB(191, 78, 56);
    COLORREF chat_user_bg       = RGB(10, 10, 10);
    COLORREF chat_assistant_bg  = RGB(0, 0, 0);
};

extern EwUiTheme g_theme;

} // namespace ewv
