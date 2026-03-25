// Stubbed Win64 desktop AI UI helpers for packaged GAME builds.
// These symbols satisfy link dependencies from GE_app.cpp while ensuring
// no AI/editor tooling is active in packaged runtime.

#include "win64_host.hpp"
#include <string>

void ew_win64_ai_append_line(EwWin64Host&, const std::string&) {}
std::string ew_win64_ai_take_input(EwWin64Host&) { return {}; }
bool ew_win64_open_file_dialog_obj(EwWin64Host&, std::string&) { return false; }
void ew_ai_ui_handle_command(EwWin64Host&, int) {}
void ew_ai_ui_init_controls(EwWin64Host&) {}
void ew_ai_ui_tick(EwWin64Host&) {}
