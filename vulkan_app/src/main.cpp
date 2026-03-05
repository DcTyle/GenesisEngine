#include "GE_app.hpp"

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    ewv::AppConfig cfg;
    cfg.app_title_utf8 = "Genesis Engine Viewport";
    cfg.initial_width = 1600;
    cfg.initial_height = 900;

    ewv::App app(cfg);
    return app.Run(hInst);
}
