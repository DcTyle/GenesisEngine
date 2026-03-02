#include "win64_host.hpp"
#include "GE_scene_api.hpp"
#include "obj_import.hpp"

#include <commdlg.h>
#include <vector>

static std::wstring ew_utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out; out.resize((size_t)len);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
    return out;
}

static std::string ew_wide_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out; out.resize((size_t)len);
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), out.data(), len, nullptr, nullptr);
    return out;
}

void ew_win32_ai_append_line(EwWin64Host& host, const std::string& line_utf8) {
    if (!host.hwnd_ai_output) return;

    std::wstring w = ew_utf8_to_wide(line_utf8);
    w.push_back(L'\r');
    w.push_back(L'\n');

    const int len = GetWindowTextLengthW(host.hwnd_ai_output);
    SendMessageW(host.hwnd_ai_output, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(host.hwnd_ai_output, EM_REPLACESEL, FALSE, (LPARAM)w.c_str());
}

std::string ew_win32_ai_take_input(EwWin64Host& host) {
    if (!host.hwnd_ai_input) return {};
    const int len = GetWindowTextLengthW(host.hwnd_ai_input);
    if (len <= 0) return {};
    std::wstring ws; ws.resize((size_t)len);
    GetWindowTextW(host.hwnd_ai_input, ws.data(), len+1);
    SetWindowTextW(host.hwnd_ai_input, L"");
    return ew_wide_to_utf8(ws);
}

bool ew_win32_open_file_dialog_obj(EwWin64Host& host, std::string& out_path_utf8) {
    wchar_t file_buf[MAX_PATH] = {0};

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = host.hwnd_main;
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Wavefront OBJ (*.obj)\0*.obj\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return false;

    out_path_utf8 = ew_wide_to_utf8(file_buf);
    return !out_path_utf8.empty();
}

static void ew_ui_refresh_object_list(EwWin64Host& host) {
    if (!host.hwnd_obj_list) return;
    SendMessageW(host.hwnd_obj_list, LB_RESETCONTENT, 0, 0);

    auto& scene = ew_scene_singleton();
    for (auto& o : scene.objects) {
        std::wstring w = ew_utf8_to_wide(o.name_utf8);
        const int idx = (int)SendMessageW(host.hwnd_obj_list, LB_ADDSTRING, 0, (LPARAM)w.c_str());
        SendMessageW(host.hwnd_obj_list, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)o.id);
    }

    // Select current
    if (scene.selected_id != 0) {
        for (int i=0;i<(int)scene.objects.size();i++) {
            const uint32_t id = (uint32_t)SendMessageW(host.hwnd_obj_list, LB_GETITEMDATA, (WPARAM)i, 0);
            if (id == scene.selected_id) {
                SendMessageW(host.hwnd_obj_list, LB_SETCURSEL, (WPARAM)i, 0);
                break;
            }
        }
    }
}

static void ew_ui_update_transform_label(EwWin64Host& host) {
    if (!host.hwnd_transform) return;
    const uint32_t sid = ew_scene_selected();
    auto* o = ew_scene_find(sid);
    if (!o) {
        SetWindowTextW(host.hwnd_transform, L"Selected: (none)");
        return;
    }
    wchar_t buf[512];
    swprintf_s(buf, L"Selected: %u  Pos(%.3f, %.3f, %.3f)",
        o->id,
        o->xform.position.x,
        o->xform.position.y,
        o->xform.position.z);
    SetWindowTextW(host.hwnd_transform, buf);
}

// Called by window procedure via WM_COMMAND.
void ew_ai_ui_handle_command(EwWin64Host& host, int control_id) {
    if (control_id == 1002) { // send
        host.ui_send_clicked = true;
        return;
    }
    if (control_id == 1102) { // import
        std::string path;
        if (!ew_win32_open_file_dialog_obj(host, path)) return;

        EwObjMesh mesh;
        if (!ew_obj_load_utf8(path, mesh)) {
            ew_win32_ai_append_line(host, "IMPORT_FAILED: could not parse OBJ");
            return;
        }

        const std::string name = ew_obj_basename_utf8(path);
        const uint32_t id = ew_scene_add_object(name, path);

        ew_ui_refresh_object_list(host);
        ew_scene_select(id);
        ew_ui_update_transform_label(host);

        ew_win32_ai_append_line(host, "IMPORTED_OBJ: " + name + "  vtx=" + std::to_string(mesh.vertices.size()) + "  tri=" + std::to_string(mesh.indices.size()/3));
    }
    if (control_id == 1101) { // list selection
        const int sel = (int)SendMessageW(host.hwnd_obj_list, LB_GETCURSEL, 0, 0);
        if (sel != LB_ERR) {
            const uint32_t id = (uint32_t)SendMessageW(host.hwnd_obj_list, LB_GETITEMDATA, (WPARAM)sel, 0);
            ew_scene_select(id);
            ew_ui_update_transform_label(host);
        }
    }
}

// Expose a safe init for controls.
void ew_ai_ui_init_controls(EwWin64Host& host) {
    // Populate list once at startup.
    ew_ui_refresh_object_list(host);
    ew_ui_update_transform_label(host);
}

// Update transform label each frame (after manipulation).
void ew_ai_ui_tick(EwWin64Host& host) {
    ew_ui_update_transform_label(host);
}
