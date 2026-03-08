#include "GE_app.hpp"

#include <vulkan/vulkan.h>
#include <shellscalingapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>
#pragma comment(lib, "Comctl32.lib")


#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>

#include "GE_runtime.hpp" // SubstrateManager
#include "ew_runtime.h"      // ew_runtime_project_points
#include "GE_object_memory.hpp"
#include "field_lattice_cpu.hpp"
#include "obj_import.hpp"
#include "GE_render_instance.hpp"
#include "ewmesh_voxelizer.hpp"
#include "carrier_bundle_cuda.hpp"
#include "GE_control_packets.hpp"

#include <fstream>
#include <filesystem>

#ifndef EW_SHADER_OUT_DIR
#define EW_SHADER_OUT_DIR "."
#endif

#pragma comment(lib, "Shcore.lib")

namespace ewv {

// -----------------------------------------------------------------------------
// UI Theme (Windows native host, Win64 app baseline)
// - Black + Gold styling for editor panels.
// - Important: purely presentation. Does not touch simulation truth.
// -----------------------------------------------------------------------------
struct EwUiTheme {
    COLORREF bg        = RGB(16,16,16);   // main window background
    COLORREF panel     = RGB(24,24,24);   // panel background
    COLORREF edit_bg   = RGB(30,30,30);   // edit/list background
    COLORREF text      = RGB(232,232,232);
    COLORREF muted     = RGB(180,180,180);
    COLORREF border    = RGB(70,70,70);
    COLORREF gold      = RGB(212,175,55);
    COLORREF gold_dark = RGB(140,110,30);
};

static EwUiTheme g_theme;
static HBRUSH g_brush_bg    = nullptr;
static HBRUSH g_brush_panel = nullptr;
static HBRUSH g_brush_edit  = nullptr;


#ifndef GENESIS_EDITOR_BUILD
#define GENESIS_EDITOR_BUILD 1
#endif

static constexpr bool ew_editor_build_enabled = (GENESIS_EDITOR_BUILD != 0);

static HFONT g_font_ui = nullptr;
static HFONT g_font_ui_bold = nullptr;
static HFONT g_font_ui_small = nullptr;

static bool ew_content_is_highlighted(const std::wstring& rel_path_w, const SubstrateManager* sm) {
    if (!sm) return false;
    // v1: ASCII-only compare (paths in inspector are ASCII in current repo conventions).
    std::string rp;
    rp.reserve(rel_path_w.size());
    for (wchar_t wc : rel_path_w) {
        if (wc >= 0 && wc < 0x80) rp.push_back((char)wc);
        else return false;
    }
    for (const auto& p : sm->coh_highlight_paths) {
        if (p == rp) return true;
    }
    return false;
}

static void ew_theme_init_once() {
    static bool inited = false;
    if (inited) return;
    inited = true;
    g_brush_bg    = CreateSolidBrush(g_theme.bg);
    g_brush_panel = CreateSolidBrush(g_theme.panel);
    g_brush_edit  = CreateSolidBrush(g_theme.edit_bg);
    // Fonts (Segoe UI, editor-friendly)
    g_font_ui = CreateFontW(-MulDiv(10, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_ui_bold = CreateFontW(-MulDiv(10, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72), 0, 0, 0, FW_SEMIBOLD,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_ui_small = CreateFontW(-MulDiv(9, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void ew_apply_font_recursive(HWND root, HFONT font) {
    if (!root || !font) return;
    EnumChildWindows(root, [](HWND h, LPARAM lp)->BOOL {
        HFONT f = (HFONT)lp;
        SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
        return TRUE;
    }, (LPARAM)font);
}

static void ew_apply_editor_fonts(HWND root) {
    ew_theme_init_once();
    ew_apply_font_recursive(root, g_font_ui);
}
}


// -----------------------------------------------------------------------------
// Windows native input bindings editor (minimal, Win64 app baseline)
// - Exposes a UI tool to edit input_bindings.ewcfg
// - On save, writes the file and emits InputBindingsReload control packet.
// - No input mapping effects are computed here; substrate remains the only
//   place where bindings are interpreted.
// -----------------------------------------------------------------------------

struct EwBindingsEditorWin32 {
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    HWND btn_save = nullptr;
    std::string path_utf8;
    EigenWare::SubstrateManager* sm = nullptr;
};

static EwBindingsEditorWin32 g_bind_editor;

static LRESULT CALLBACK EwBindingsEditorProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_CREATE: {
            RECT rc;
            GetClientRect(h, &rc);
            const int w0 = rc.right - rc.left;
            const int h0 = rc.bottom - rc.top;
            g_bind_editor.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL,
                                                 8, 8, w0 - 16, h0 - 56,
                                                 h, (HMENU)101, GetModuleHandleW(nullptr), nullptr);
            g_bind_editor.btn_save = CreateWindowExW(0, L"BUTTON", L"Save + Reload",
                                                     WS_CHILD | WS_VISIBLE,
                                                     8, h0 - 40, 140, 28,
                                                     h, (HMENU)102, GetModuleHandleW(nullptr), nullptr);

            // Load file content.
            std::ifstream f(g_bind_editor.path_utf8.c_str(), std::ios::in | std::ios::binary);
            std::string content;
            if (f.good()) {
                f.seekg(0, std::ios::end);
                const size_t n = (size_t)f.tellg();
                f.seekg(0, std::ios::beg);
                content.resize(n);
                if (n > 0) f.read(content.data(), (std::streamsize)n);
            } else {
                content = "# input_bindings file missing\n";
            }
            const std::wstring wtxt = utf8_to_wide(content);
            SetWindowTextW(g_bind_editor.edit, wtxt.c_str());
            return 0;
        }
        case WM_SIZE: {
            RECT rc;
            GetClientRect(h, &rc);
            const int w0 = rc.right - rc.left;
            const int h0 = rc.bottom - rc.top;
            if (g_bind_editor.edit) MoveWindow(g_bind_editor.edit, 8, 8, w0 - 16, h0 - 56, TRUE);
            if (g_bind_editor.btn_save) MoveWindow(g_bind_editor.btn_save, 8, h0 - 40, 140, 28, TRUE);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(w);
            if (id == 102 && g_bind_editor.edit) {
                const int len = GetWindowTextLengthW(g_bind_editor.edit);
                std::wstring wbuf;
                wbuf.resize((size_t)len);
                GetWindowTextW(g_bind_editor.edit, wbuf.data(), len + 1);
                const std::string txt = wide_to_utf8(wbuf);

                // Deterministic write: overwrite file bytes exactly as edited.
                {
                    std::ofstream out(g_bind_editor.path_utf8.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
                    if (out.good()) {
                        out.write(txt.data(), (std::streamsize)txt.size());
                    }
                }

                // Emit reload packet into substrate.
                if (g_bind_editor.sm) {
                    EwControlPacket cp{};
                    cp.kind = EwControlPacketKind::InputBindingsReload;
                    cp.source_u16 = 1;
                    cp.tick_u64 = g_bind_editor.sm->canonical_tick;
                    (void)ew_runtime_submit_control_packet(g_bind_editor.sm, &cp);
                }

                MessageBoxW(h, L"Saved. Substrate will reload bindings next tick.", L"Genesis", MB_OK);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            g_bind_editor.hwnd = nullptr;
            g_bind_editor.edit = nullptr;
            g_bind_editor.btn_save = nullptr;
            return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

static void ew_open_bindings_editor(EigenWare::SubstrateManager* sm, const std::string& path_utf8) {
    if (g_bind_editor.hwnd) {
        SetForegroundWindow(g_bind_editor.hwnd);
        return;
    }
    g_bind_editor.sm = sm;
    g_bind_editor.path_utf8 = path_utf8;

    static bool cls = false;
    if (!cls) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = EwBindingsEditorProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"GE_BindingsEditor";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_brush_bg;
        RegisterClassW(&wc);
        cls = true;
    }

    g_bind_editor.hwnd = CreateWindowExW(0, L"GE_BindingsEditor", L"Input Bindings Editor",
                                         WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                         CW_USEDEFAULT, CW_USEDEFAULT, 720, 520,
                                         nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out;
    out.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

static std::string wide_to_utf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out;
    out.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

struct Transform {
    float pos[3] = {0,0,0};
    float rot_euler_deg[3] = {0,0,0};
};

static inline int64_t q32_32_from_f32(float v) {
    // Deterministic float->fixed conversion: round toward zero.
    const double scaled = (double)v * 4294967296.0;
    if (scaled >= (double)INT64_MAX) return INT64_MAX;
    if (scaled <= (double)INT64_MIN) return INT64_MIN;
    return (int64_t)scaled;
}

static bool write_ewmesh_v1(const std::string& out_path_utf8, const EwObjMesh& m) {
    // Binary format (little-endian):
    //  0: 'EWM1' magic
    //  4: u32 vertex_count
    //  8: u32 index_count
    // 12: packed vertices (px,py,pz,u,v,nx,ny,nz) as f32
    // ..: packed indices as u32
    FILE* f = nullptr;
    _wfopen_s(&f, utf8_to_wide(out_path_utf8).c_str(), L"wb");
    if (!f) return false;
    const uint32_t vcount = (uint32_t)m.vertices.size();
    const uint32_t icount = (uint32_t)m.indices.size();
    const uint32_t magic = 0x314D5745u; // 'EWM1'
    if (fwrite(&magic, 4, 1, f) != 1) { fclose(f); return false; }
    if (fwrite(&vcount, 4, 1, f) != 1) { fclose(f); return false; }
    if (fwrite(&icount, 4, 1, f) != 1) { fclose(f); return false; }
    if (!m.vertices.empty()) {
        if (fwrite(m.vertices.data(), sizeof(EwObjMesh::Vtx), m.vertices.size(), f) != m.vertices.size()) {
            fclose(f); return false;
        }
    }
    if (!m.indices.empty()) {
        if (fwrite(m.indices.data(), sizeof(uint32_t), m.indices.size(), f) != m.indices.size()) {
            fclose(f); return false;
        }
    }
    fclose(f);
    return true;
}

struct App::Scene {
    EigenWare::SubstrateManager sm;
    uint64_t next_object_id_u64 = 1;

    // Default solar-system demo state (Earth orbit).
    double earth_orbit_angle_rad = 0.0;

    // Genesis lattice reference field (CPU) used by the viewport app.
    // This is the canonical sink for synthesized voxel density.
    EwFieldLatticeCpu lattice;
    std::vector<uint8_t> density_mask_u8;

    struct Object {
        std::string name_utf8;
        Transform xf;
        EwObjMesh mesh;
        uint64_t object_id_u64 = 0;
        uint32_t anchor_id_u32 = 0;
        float radius_m_f32 = 1.0f;
        float atmosphere_thickness_m_f32 = 0.0f;
        uint32_t albedo_rgba8 = 0xFFFFFFFFu;
        uint32_t atmosphere_rgba8 = 0x00000000u;
        float emissive_f32 = 0.0f;
        uint8_t pbr_scan_u8 = 0; // PBR/photogrammetry flag (AI training gate)
        uint8_t ai_training_meta_ready_u8 = 0; // explicit bounded metadata acknowledgement for training eligibility
        std::string material_meta_hint_utf8; // bounded substrate-resident composition/material hint for training eligibility
// Cached fixed-point representation for render-time (no draw-time float conversions).
int32_t pos_q16_16[3] = {0,0,0};
int32_t rot_quat_q16_16[4] = {0,0,0,65536};
int32_t radius_q16_16 = 0;
int32_t atmosphere_thickness_q16_16 = 0;
int32_t emissive_q16_16 = 0;

void refresh_fixed_cache() {
    pos_q16_16[0] = (int32_t)llround((double)xf.pos[0] * 65536.0);
    pos_q16_16[1] = (int32_t)llround((double)xf.pos[1] * 65536.0);
    pos_q16_16[2] = (int32_t)llround((double)xf.pos[2] * 65536.0);
    radius_q16_16 = (int32_t)llround((double)radius_m_f32 * 65536.0);
    atmosphere_thickness_q16_16 = (int32_t)llround((double)atmosphere_thickness_m_f32 * 65536.0);
    emissive_q16_16 = (int32_t)llround((double)emissive_f32 * 65536.0);
}
    };

    std::vector<Object> objects;
    int selected = -1;

    // Shader-facing instance payload updated each frame.
    std::vector<EwRenderInstance> instances;

    Scene() : sm(128), lattice(128u, 128u, 128u) {
        // seed one anchor pulse so AI loop is alive
        sm.ui_observe_system_event_line("BOOT: vulkan_app");

        // Deterministic reservoir baseline for early import bring-up.
        sm.reservoir = (int64_t)(sm.anchors.size()) << 32;

        density_mask_u8.assign((size_t)128u * (size_t)128u * (size_t)128u, 0u);
        // Deterministic seed constant (ASCII mnemonic encoded as hex-ish).
        lattice.init(0xE16E5151ULL);

// ------------------------------------------------------------
// Default level: blank/static world
// ------------------------------------------------------------
// On boot the world is intentionally empty and visually static.
// The substrate microprocessor still ticks immediately (AI + pulse lattice).
{
    objects.clear();
    selected = -1;
}
    }

    
void Tick() {
    sm.tick();


    // Pull authoritative render-object packets from substrate (object/planet anchors).
    {
        const EwRenderObjectPacket* ptr = nullptr;
        uint32_t count = 0u;
        uint64_t tick_u64 = 0u;
        if (ew_runtime_get_render_object_packets(&sm, &ptr, &count, &tick_u64) && ptr && count > 0u) {
            // Update local scene cache for UI selection lists (not authoritative).
            for (uint32_t i = 0u; i < count; ++i) {
                const EwRenderObjectPacket& p = ptr[i];
                for (auto& o : objects) {
                    if (o.object_id_u64 == p.object_id_u64) {
                        o.anchor_id_u32 = p.anchor_id_u32;
                        o.xf.pos[0] = (float)p.pos_q16_16[0] / 65536.0f;
                        o.xf.pos[1] = (float)p.pos_q16_16[1] / 65536.0f;
                        o.xf.pos[2] = (float)p.pos_q16_16[2] / 65536.0f;
                        for (int k = 0; k < 4; ++k) o.rot_quat_q16_16[k] = p.rot_quat_q16_16[k];
                        o.radius_m_f32 = (float)p.radius_q16_16 / 65536.0f;
                        o.albedo_rgba8 = p.albedo_rgba8;
                        o.atmosphere_rgba8 = p.atmosphere_rgba8;
                        o.atmosphere_thickness_m_f32 = (float)p.atmosphere_thickness_q16_16 / 65536.0f;
                        o.emissive_f32 = (float)p.emissive_q16_16 / 65536.0f;
                        o.refresh_fixed_cache();
                        break;
                    }
                }
            }
        }
    }

    // World-play gates local viewport evolution. The substrate microprocessor
    // tick already occurred above (sm.tick()).
    if (sm.sim_world_play_u32 != 0u) {
        lattice.step_one_tick();
    }
}

    bool PopUiLine(std::string& out_utf8) {
        return sm.ui_pop_output_text(out_utf8);
    }

    void SubmitAiChatLine(const std::string& utf8, uint32_t chat_slot_u32 = 0u, uint32_t mode_u32 = SubstrateManager::EW_CHAT_MEMORY_MODE_TALK) {
        sm.ui_submit_chat_message_line(utf8, chat_slot_u32, mode_u32);
    }
    bool SnapshotAiChatMemory(uint32_t prefer_mode_u32, uint32_t max_entries_u32, std::vector<SubstrateManager::EwChatMemoryEntry>& out_entries, std::string& out_summary_utf8) {
        return sm.ui_snapshot_chat_memory(prefer_mode_u32, max_entries_u32, out_entries, out_summary_utf8);
    }
    void ObserveAiChatMemory(uint32_t chat_slot_u32, uint32_t mode_u32, const std::string& utf8) {
        sm.ui_chat_memory_observe(chat_slot_u32, mode_u32, utf8);
    }
    void LinkAiChatProject(uint32_t chat_slot_u32, const std::string& root_utf8, const std::vector<std::string>& rels_utf8) {
        sm.ui_link_chat_project(chat_slot_u32, root_utf8, rels_utf8);
    }
    bool SnapshotAiChatProject(uint32_t chat_slot_u32, SubstrateManager::EwProjectLinkEntry& out_entry) {
        return sm.ui_snapshot_chat_project(chat_slot_u32, out_entry);
    }
    bool SnapshotAiProjectSpectrumLines(uint32_t chat_slot_u32, uint32_t max_lines_u32, std::vector<std::string>& out_lines_utf8) {
        return sm.ui_snapshot_project_spectrum_lines(chat_slot_u32, max_lines_u32, out_lines_utf8);
    }
    void SetLiveViewportMode(bool enabled, int spectrum_band_i32) {
        sm.sim_world_play_u32 = enabled ? 1u : 0u;
        if (enabled) {
            uint32_t slice_z_u32 = 64u;
            if (spectrum_band_i32 > 0) {
                const uint32_t offs = (uint32_t)std::min(spectrum_band_i32 * 6, 56);
                slice_z_u32 = 64u + offs;
            } else if (spectrum_band_i32 < 0) {
                const uint32_t offs = (uint32_t)std::min((-spectrum_band_i32) * 6, 56);
                slice_z_u32 = 64u - offs;
            }
            sm.set_lattice_projection_tag_ex(0u, slice_z_u32, 1u, 65536u, 20u, true);
        } else {
            sm.set_lattice_projection_tag_ex(0u, 0u, 1u, 1u, 0u, false);
        }
    }
    bool DuplicateSelectedObjectForEditor(float offset_x_m, float offset_y_m, float offset_z_m) {
        if (selected < 0 || selected >= (int)objects.size()) return false;
        const Object& src = objects[(size_t)selected];
        Object dup = src;
        dup.object_id_u64 = next_object_id_u64++;
        dup.anchor_id_u32 = 0u;
        dup.xf.pos[0] += offset_x_m;
        dup.xf.pos[1] += offset_y_m;
        dup.xf.pos[2] += offset_z_m;
        dup.name_utf8 = src.name_utf8 + "_copy_" + std::to_string((unsigned long long)dup.object_id_u64);
        dup.refresh_fixed_cache();

        EwObjectEntry e{};
        e.object_id_u64 = dup.object_id_u64;
        e.label_utf8 = ew_obj_basename_utf8(dup.name_utf8);
        const uint32_t tricount = (uint32_t)(dup.mesh.indices.size() / 3u);
        const uint32_t vcount = (uint32_t)dup.mesh.vertices.size();
        const uint64_t cost_units = 1u + (uint64_t)(tricount / 128u) + (uint64_t)(vcount / 256u);
        e.mass_or_cost_q32_32 = (int64_t)(cost_units << 32);
        e.geomcoord9_u64x9.u64x9[0] = (uint64_t)q32_32_from_f32(dup.xf.pos[0]);
        e.geomcoord9_u64x9.u64x9[1] = (uint64_t)q32_32_from_f32(dup.xf.pos[1]);
        e.geomcoord9_u64x9.u64x9[2] = (uint64_t)q32_32_from_f32(dup.xf.pos[2]);
        e.geomcoord9_u64x9.u64x9[3] = (uint64_t)tricount;
        e.geomcoord9_u64x9.u64x9[4] = (uint64_t)vcount;
        e.geomcoord9_u64x9.u64x9[5] = (uint64_t)dup.object_id_u64;
        e.geomcoord9_u64x9.u64x9[8] = (uint64_t)dup.object_id_u64;
        const uint64_t s0 = dup.object_id_u64 * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint64_t s1 = ((uint64_t)tricount << 32) ^ (uint64_t)vcount;
        e.phase_seed_u64 = s0 ^ (s1 * 2862933555777941757ULL);
        (void)sm.object_store.upsert(e);
        const uint32_t synth_g = 32u;
        (void)genesis_synthesize_voxelize_local(sm.object_store, dup.mesh, dup.object_id_u64, synth_g, synth_g, synth_g);
        genesis_apply_object_density_to_lattice(dup.object_id_u64);

        EwControlPacket cp{};
        cp.kind = EwControlPacketKind::ObjectRegister;
        cp.source_u16 = 1;
        cp.tick_u64 = sm.canonical_tick;
        cp.payload.object_register.object_id_u64 = dup.object_id_u64;
        cp.payload.object_register.kind_u32 = EW_ANCHOR_KIND_OBJECT;
        cp.payload.object_register.pos_q16_16[0] = dup.pos_q16_16[0];
        cp.payload.object_register.pos_q16_16[1] = dup.pos_q16_16[1];
        cp.payload.object_register.pos_q16_16[2] = dup.pos_q16_16[2];
        for (int k = 0; k < 4; ++k) cp.payload.object_register.rot_quat_q16_16[k] = dup.rot_quat_q16_16[k];
        cp.payload.object_register.radius_m_q16_16 = dup.radius_q16_16;
        cp.payload.object_register.albedo_rgba8 = dup.albedo_rgba8;
        cp.payload.object_register.atmosphere_rgba8 = dup.atmosphere_rgba8;
        cp.payload.object_register.atmosphere_thickness_m_q16_16 = dup.atmosphere_thickness_q16_16;
        cp.payload.object_register.emissive_q16_16 = dup.emissive_q16_16;
        (void)ew_runtime_submit_control_packet(&sm, &cp);

        objects.push_back(std::move(dup));
        selected = (int)objects.size() - 1;
        return true;
    }

    void ContentReindex() { sm.ui_content_reindex(); }
    bool SnapshotContentEntries(uint32_t limit_u32, std::vector<genesis::GeAssetEntry>& out_entries, std::string* out_err) { return sm.ui_snapshot_content_entries(limit_u32, out_entries, out_err); }
    void ContentListAll(uint32_t limit_u32) { sm.ui_content_list_all(limit_u32); }
    void SetRepoReaderEnabled(bool enabled) { sm.ui_set_repo_reader_enabled(enabled); }
    void RescanRepoReader() { sm.ui_repo_reader_rescan(); }
    bool SnapshotRepoReaderStatus(std::string& out_status_utf8) { return sm.ui_snapshot_repo_reader_status(out_status_utf8); }
    bool SnapshotRepoReaderFiles(uint32_t limit_u32, std::vector<std::string>& out_rel_paths_utf8, std::string* out_err) { return sm.ui_snapshot_repo_reader_files(limit_u32, out_rel_paths_utf8, out_err); }
    bool SnapshotRepoFilePreview(const std::string& rel_path_utf8, uint32_t max_bytes_u32, std::string& out_preview_utf8, std::string* out_err) { return sm.ui_snapshot_repo_file_preview(rel_path_utf8, max_bytes_u32, out_preview_utf8, out_err); }
    bool SnapshotRepoFileCoherenceHits(const std::string& rel_path_utf8, uint32_t limit_u32, std::vector<genesis::GeCoherenceHit>& out_hits, std::string* out_err) { return sm.ui_snapshot_repo_file_coherence_hits(rel_path_utf8, limit_u32, out_hits, out_err); }
    bool SnapshotCoherenceStats(std::string& out_stats_utf8) { return sm.ui_snapshot_coherence_stats(out_stats_utf8); }
    bool SnapshotCoherenceQuery(const std::string& query_utf8, uint32_t limit_u32, std::vector<genesis::GeCoherenceHit>& out_hits, std::string* out_err) { return sm.ui_snapshot_coherence_query(query_utf8, limit_u32, out_hits, out_err); }
    bool SnapshotCoherenceRenamePlan(const std::string& old_ident_ascii, const std::string& new_ident_ascii, uint32_t limit_u32, std::vector<genesis::GeCoherenceHit>& out_hits, std::string* out_err) { return sm.ui_snapshot_coherence_rename_plan(old_ident_ascii, new_ident_ascii, limit_u32, out_hits, out_err); }
    bool SnapshotCoherenceRenamePatch(const std::string& old_ident_ascii, const std::string& new_ident_ascii, uint32_t limit_u32, std::string& out_patch_utf8, std::string* out_err) { return sm.ui_snapshot_coherence_rename_patch(old_ident_ascii, new_ident_ascii, limit_u32, out_patch_utf8, out_err); }
    bool SnapshotCoherenceSelftest(bool& out_ok, std::string& out_report_utf8) { return sm.ui_snapshot_coherence_selftest(out_ok, out_report_utf8); }
    void EmitCoherenceStats() { sm.ui_emit_coherence_stats(); }
    void EmitCoherenceQuery(const std::string& query_utf8, uint32_t limit_u32) { sm.ui_emit_coherence_query(query_utf8, limit_u32); }
    void EmitCoherenceRenamePlan(const std::string& old_ident_ascii, const std::string& new_ident_ascii, uint32_t limit_u32) { sm.ui_emit_coherence_rename_plan(old_ident_ascii, new_ident_ascii, limit_u32); }
        void SetCoherenceHighlightQuery(const std::string& query_utf8, uint32_t limit_u32) { sm.ui_set_coherence_highlight_query(query_utf8, limit_u32); }
    void SetCoherenceHighlightPath(const std::string& rel_path_utf8) { sm.ui_set_coherence_highlight_path(rel_path_utf8); }
    bool SnapshotVaultEntries(uint32_t limit_u32, std::vector<std::string>& out_rel_paths_utf8, std::string* out_err) { return sm.ui_snapshot_vault_entries(limit_u32, out_rel_paths_utf8, out_err); }
    bool SnapshotVaultEntryPreview(const std::string& rel_path_utf8, uint32_t max_bytes_u32, std::string& out_preview_utf8, std::string* out_err) { return sm.ui_snapshot_vault_entry_preview(rel_path_utf8, max_bytes_u32, out_preview_utf8, out_err); }
    bool ImportVaultEntry(const std::string& rel_path_utf8, std::string& out_written_path_utf8, std::string* out_err) { return sm.ui_import_vault_entry(rel_path_utf8, out_written_path_utf8, out_err); }
    void EmitVaultListAll(uint32_t limit_u32) { sm.ui_emit_vault_list_all(limit_u32); }
    void RequestGameBootstrap(const std::string& request_utf8) { sm.ui_request_game_bootstrap(request_utf8); }
    void EmitAiModelTrainReady(const std::string& base_name_utf8) { sm.ui_emit_ai_model_train_ready(base_name_utf8); }

    static bool genesis_synthesize_voxelize_local(EwObjectStore& store,
                                                  const EwObjMesh& mesh,
                                                  uint64_t object_id_u64,
                                                  uint32_t grid_x_u32,
                                                  uint32_t grid_y_u32,
                                                  uint32_t grid_z_u32) {
        const EwObjectEntry* e = store.find(object_id_u64);
        if (!e) return false;
        if (grid_x_u32 == 0 || grid_y_u32 == 0 || grid_z_u32 == 0) return false;

        genesis::EwMeshV1 m;
        m.vertices.reserve(mesh.vertices.size());
        m.indices = mesh.indices;
        for (const auto& v : mesh.vertices) {
            genesis::EwMeshV1::Vtx vv{};
            vv.px = v.px; vv.py = v.py; vv.pz = v.pz;
            vv.nx = v.nx; vv.ny = v.ny; vv.nz = v.nz;
            vv.u = v.u; vv.v = v.v;
            m.vertices.push_back(vv);
        }

        std::vector<uint8_t> occ;
        if (!genesis::ewmesh_voxelize_occupancy_u8(m, sm.materials_calib_done, grid_x_u32, grid_y_u32, grid_z_u32, occ)) return false;

        if (!store.upsert_voxel_volume_occupancy_u8(object_id_u64, grid_x_u32, grid_y_u32, grid_z_u32, occ.data(), occ.size())) {
            return false;
        }

        EwObjectEntry e2 = *e;
        e2.voxel_grid_x_u32 = grid_x_u32;
        e2.voxel_grid_y_u32 = grid_y_u32;
        e2.voxel_grid_z_u32 = grid_z_u32;
        e2.voxel_format_u32 = 1;
        e2.voxel_blob_id_u64 = object_id_u64;
        store.upsert(e2);
        return true;
    }

    void genesis_apply_object_density_to_lattice(uint64_t object_id_u64) {
        EwVoxelVolumeView vv;
        if (!sm.object_store.view_voxel_volume(object_id_u64, vv)) return;
        if (vv.format_u32 != 1 || !vv.bytes || vv.byte_count == 0) return;

        const uint32_t gx = 128u, gy = 128u, gz = 128u;
        if (density_mask_u8.size() != (size_t)gx * (size_t)gy * (size_t)gz) return;

        // Stamp the object's voxel volume into the lattice density mask.
        // Deterministic placement: center the object volume.
        const int32_t ox0 = (int32_t)((gx > vv.grid_x_u32) ? ((gx - vv.grid_x_u32) / 2u) : 0u);
        const int32_t oy0 = (int32_t)((gy > vv.grid_y_u32) ? ((gy - vv.grid_y_u32) / 2u) : 0u);
        const int32_t oz0 = (int32_t)((gz > vv.grid_z_u32) ? ((gz - vv.grid_z_u32) / 2u) : 0u);

        for (uint32_t z = 0; z < vv.grid_z_u32; ++z) {
            const int32_t zt = oz0 + (int32_t)z;
            if (zt < 0 || zt >= (int32_t)gz) continue;
            for (uint32_t y = 0; y < vv.grid_y_u32; ++y) {
                const int32_t yt = oy0 + (int32_t)y;
                if (yt < 0 || yt >= (int32_t)gy) continue;
                const size_t src_row = ((size_t)z * (size_t)vv.grid_y_u32 + (size_t)y) * (size_t)vv.grid_x_u32;
                const size_t dst_row = ((size_t)zt * (size_t)gy + (size_t)yt) * (size_t)gx;
                for (uint32_t x = 0; x < vv.grid_x_u32; ++x) {
                    const int32_t xt = ox0 + (int32_t)x;
                    if (xt < 0 || xt >= (int32_t)gx) continue;
                    const uint8_t v = vv.bytes[src_row + (size_t)x];
                    uint8_t& d = density_mask_u8[dst_row + (size_t)xt];
                    // Deterministic combine: max occupancy.
                    d = (v > d) ? v : d;
                }
            }
        }

        lattice.upload_density_mask_u8(density_mask_u8.data(), density_mask_u8.size());
    }

    bool ImportObj(const std::string& path_utf8, bool pbr_scan, const std::string& material_meta_hint_utf8) {
        EwObjMesh m;
        if (!ew_obj_load_utf8(path_utf8, m)) return false;

        float minx=0, miny=0, minz=0, maxx=0, maxy=0, maxz=0;
        if (!m.vertices.empty()) {
            minx = maxx = m.vertices[0].px;
            miny = maxy = m.vertices[0].py;
            minz = maxz = m.vertices[0].pz;
            for (const auto& v : m.vertices) {
                minx = (v.px < minx) ? v.px : minx;
                miny = (v.py < miny) ? v.py : miny;
                minz = (v.pz < minz) ? v.pz : minz;
                maxx = (v.px > maxx) ? v.px : maxx;
                maxy = (v.py > maxy) ? v.py : maxy;
                maxz = (v.pz > maxz) ? v.pz : maxz;
            }
        }
        const float cx = 0.5f * (minx + maxx);
        const float cy = 0.5f * (miny + maxy);
        const float cz = 0.5f * (minz + maxz);
        const float ex = (maxx - minx);
        const float ey = (maxy - miny);
        const float ez = (maxz - minz);
        const uint32_t vcount = (uint32_t)m.vertices.size();
        const uint32_t tricount = (uint32_t)(m.indices.size() / 3u);

        const uint64_t object_id = next_object_id_u64++;
        EwObjectEntry e{};
        e.object_id_u64 = object_id;
        e.label_utf8 = ew_obj_basename_utf8(path_utf8);
        const uint64_t cost_units = 1u + (uint64_t)(tricount / 128u) + (uint64_t)(vcount / 256u);
        e.mass_or_cost_q32_32 = (int64_t)(cost_units << 32);
        e.geomcoord9_u64x9.u64x9[0] = (uint64_t)q32_32_from_f32(cx);
        e.geomcoord9_u64x9.u64x9[1] = (uint64_t)q32_32_from_f32(cy);
        e.geomcoord9_u64x9.u64x9[2] = (uint64_t)q32_32_from_f32(cz);
        e.geomcoord9_u64x9.u64x9[3] = (uint64_t)tricount;
        e.geomcoord9_u64x9.u64x9[4] = (uint64_t)vcount;
        e.geomcoord9_u64x9.u64x9[5] = (uint64_t)q32_32_from_f32(ex);
        e.geomcoord9_u64x9.u64x9[6] = (uint64_t)q32_32_from_f32(ey);
        e.geomcoord9_u64x9.u64x9[7] = (uint64_t)q32_32_from_f32(ez);
        e.geomcoord9_u64x9.u64x9[8] = object_id;

        const uint64_t s0 = object_id * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint64_t s1 = ((uint64_t)tricount << 32) ^ (uint64_t)vcount;
        e.phase_seed_u64 = s0 ^ (s1 * 2862933555777941757ULL);

        (void)sm.object_store.upsert(e);

        // Genesis synthesis enforcement: every imported object must have a voxel volume.
        // The viewport app uses a deterministic local synthesis pass here.
        const uint32_t synth_g = 32u;
        const EwObjectEntry* e_check = sm.object_store.find(object_id);
        const bool needs_vox = (!e_check) || (e_check->voxel_format_u32 == 0u) || (e_check->voxel_grid_x_u32 == 0u);
        if (needs_vox) {
            if (!genesis_synthesize_voxelize_local(sm.object_store, m, object_id, synth_g, synth_g, synth_g)) return false;
        }

        // Bind synthesized density into the lattice (physics field).
        genesis_apply_object_density_to_lattice(object_id);

        const std::string out_root = sm.project_settings.assets.project_asset_substrate_root_utf8.empty() ? std::string("AssetSubstrate") : sm.project_settings.assets.project_asset_substrate_root_utf8;
        const std::string out_assets = out_root + "/Assets";
        const std::string out_dir = out_assets + "/Imported/";
        CreateDirectoryW(utf8_to_wide(out_root).c_str(), nullptr);
        CreateDirectoryW(utf8_to_wide(out_assets).c_str(), nullptr);
        CreateDirectoryW(utf8_to_wide(out_dir).c_str(), nullptr);
        const std::string out_mesh = out_dir + e.label_utf8 + ".ewmesh";
        (void)write_ewmesh_v1(out_mesh, m);

        // Register object anchor (authoritative transform lives in substrate).
        {
            EwControlPacket cp{};
            cp.kind = EwControlPacketKind::ObjectRegister;
            cp.source_u16 = 1;
            cp.tick_u64 = sm.canonical_tick;
            cp.payload.object_register.object_id_u64 = object_id;
            cp.payload.object_register.kind_u32 = EW_ANCHOR_KIND_OBJECT;
            cp.payload.object_register.pad0_u32 = 0u;
            cp.payload.object_register.pos_q16_16[0] = 0;
            cp.payload.object_register.pos_q16_16[1] = 0;
            cp.payload.object_register.pos_q16_16[2] = 0;
            cp.payload.object_register.rot_quat_q16_16[0] = 0;
            cp.payload.object_register.rot_quat_q16_16[1] = 0;
            cp.payload.object_register.rot_quat_q16_16[2] = 0;
            cp.payload.object_register.rot_quat_q16_16[3] = 65536;
            const float r = 0.5f * ((ex > ey) ? ((ex > ez) ? ex : ez) : ((ey > ez) ? ey : ez));
            cp.payload.object_register.radius_m_q16_16 = (int32_t)llround((double)r * 65536.0);
            cp.payload.object_register.albedo_rgba8 = 0xFFFFFFFFu;
            cp.payload.object_register.atmosphere_rgba8 = 0x00000000u;
            cp.payload.object_register.atmosphere_thickness_m_q16_16 = 0;
            cp.payload.object_register.emissive_q16_16 = 0;
            (void)ew_runtime_submit_control_packet(&sm, &cp);
        }


        Object o;
        o.name_utf8 = path_utf8;
        o.pbr_scan_u8 = pbr_scan ? 1 : 0;
        o.material_meta_hint_utf8 = material_meta_hint_utf8;
        o.ai_training_meta_ready_u8 = (!material_meta_hint_utf8.empty()) ? 1u : 0u;
        o.mesh = std::move(m);
        o.object_id_u64 = object_id;
        o.anchor_id_u32 = 0;
        o.refresh_fixed_cache();
        objects.push_back(std::move(o));
        selected = (int)objects.size() - 1;
        return true;
    }

    // Deterministic voxel synthesis for an already-imported object.
    // UI/AI can call this to validate voxelization without re-import.
    bool genesis_synthesize_object_voxel_volume_occupancy_u8(uint64_t object_id_u64) {
        for (auto& o : objects) {
            if (o.object_id_u64 != object_id_u64) continue;
            const uint32_t synth_g = 96u; // bounded resolution for training bring-up
            if (!genesis_synthesize_voxelize_local(sm.object_store, o.mesh, object_id_u64, synth_g, synth_g, synth_g)) return false;
            genesis_apply_object_density_to_lattice(object_id_u64);
            return true;
        }
        return false;
    }

};

struct App::VkCtx {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue gfxq = VK_NULL_HANDLE;
    uint32_t gfxq_family = 0;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swap = VK_NULL_HANDLE;
    VkFormat swap_format = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swap_extent{};
    std::vector<VkImage> swap_images;
    std::vector<VkImageView> swap_views;

    VkCommandPool cmdpool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdbufs;

    VkSemaphore sem_image = VK_NULL_HANDLE;
    VkSemaphore sem_render = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    // Shader call-through (projection instances)
    VkBuffer instance_buf = VK_NULL_HANDLE;
    VkDeviceMemory instance_mem = VK_NULL_HANDLE;
    void* instance_mapped = nullptr;
    VkDeviceSize instance_capacity = 0;

    // Minimal graphics pipeline (dynamic rendering)
    VkDescriptorSetLayout ds_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkDescriptorPool ds_pool = VK_NULL_HANDLE;
    VkDescriptorSet ds = VK_NULL_HANDLE;

    // Depth buffer (required for camera sensor histogram)
    VkImage depth_image = VK_NULL_HANDLE;
    VkDeviceMemory depth_mem = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkSampler depth_sampler = VK_NULL_HANDLE;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

    // Camera sensor compute pipelines
    VkDescriptorSetLayout cam_ds_layout = VK_NULL_HANDLE;
    VkPipelineLayout cam_pipe_layout = VK_NULL_HANDLE;
    VkPipeline cam_hist_pipe = VK_NULL_HANDLE;
    VkPipeline cam_median_pipe = VK_NULL_HANDLE;
    VkDescriptorPool cam_ds_pool = VK_NULL_HANDLE;
    VkDescriptorSet cam_hist_ds = VK_NULL_HANDLE;
    VkDescriptorSet cam_median_ds = VK_NULL_HANDLE;
    VkBuffer cam_hist_buf = VK_NULL_HANDLE;
    VkDeviceMemory cam_hist_mem = VK_NULL_HANDLE;
    VkBuffer cam_out_buf = VK_NULL_HANDLE;
    VkDeviceMemory cam_out_mem = VK_NULL_HANDLE;
    void* cam_out_mapped = nullptr;

    // Production virtual texture system: atlas + page table.
    VkImage vt_atlas_image = VK_NULL_HANDLE;
    VkDeviceMemory vt_atlas_mem = VK_NULL_HANDLE;
    VkImageView vt_atlas_view = VK_NULL_HANDLE;
    VkSampler vt_atlas_sampler = VK_NULL_HANDLE;

    VkBuffer vt_pagetable_buf = VK_NULL_HANDLE;
    VkDeviceMemory vt_pagetable_mem = VK_NULL_HANDLE;

    // Virtual texture constants (square for now)
    uint32_t vt_virtual_dim = 32768; // 32k ceiling (asset space)
    uint32_t vt_tile_size = 128;
    uint32_t vt_atlas_dim = 4096;
    uint32_t vt_mip_count = 1;

    // Page table header mirrors the shader layout (kept CPU-side for updates)
    uint32_t vt_mip_offset[16]{};
    uint32_t vt_mip_tiles_per_row[16]{};
    std::vector<uint32_t> vt_entries; // packed atlas coords or 0xFFFFFFFF

    // Atlas tile occupancy (simple LRU)
    struct VtSlot { uint32_t last_use_tick = 0; uint32_t virt_key = 0; bool used = false; };
    std::vector<VtSlot> vt_slots;
    uint32_t vt_tiles_per_row_atlas = 0;

    bool enable_validation = false;
    VkDebugUtilsMessengerEXT dbg = VK_NULL_HANDLE;

    void DestroySwap() {
        for (auto v : swap_views) if (v) vkDestroyImageView(dev, v, nullptr);
        swap_views.clear();
        swap_images.clear();
        if (depth_sampler) { vkDestroySampler(dev, depth_sampler, nullptr); depth_sampler = VK_NULL_HANDLE; }
        if (depth_view) { vkDestroyImageView(dev, depth_view, nullptr); depth_view = VK_NULL_HANDLE; }
        if (depth_image) { vkDestroyImage(dev, depth_image, nullptr); depth_image = VK_NULL_HANDLE; }
        if (depth_mem) { vkFreeMemory(dev, depth_mem, nullptr); depth_mem = VK_NULL_HANDLE; }
        if (swap) vkDestroySwapchainKHR(dev, swap, nullptr);
        swap = VK_NULL_HANDLE;
    }

    void DestroyAll() {
        if (dev) {
            vkDeviceWaitIdle(dev);
            DestroySwap();

            if (pipe) vkDestroyPipeline(dev, pipe, nullptr);
            if (pipe_layout) vkDestroyPipelineLayout(dev, pipe_layout, nullptr);
            if (ds_layout) vkDestroyDescriptorSetLayout(dev, ds_layout, nullptr);
            if (ds_pool) vkDestroyDescriptorPool(dev, ds_pool, nullptr);

            if (cam_hist_pipe) vkDestroyPipeline(dev, cam_hist_pipe, nullptr);
            if (cam_median_pipe) vkDestroyPipeline(dev, cam_median_pipe, nullptr);
            if (cam_pipe_layout) vkDestroyPipelineLayout(dev, cam_pipe_layout, nullptr);
            if (cam_ds_layout) vkDestroyDescriptorSetLayout(dev, cam_ds_layout, nullptr);
            if (cam_ds_pool) vkDestroyDescriptorPool(dev, cam_ds_pool, nullptr);
            if (cam_hist_buf) { vkDestroyBuffer(dev, cam_hist_buf, nullptr); cam_hist_buf = VK_NULL_HANDLE; }
            if (cam_hist_mem) { vkFreeMemory(dev, cam_hist_mem, nullptr); cam_hist_mem = VK_NULL_HANDLE; }
            if (cam_out_buf) {
                if (cam_out_mapped) { vkUnmapMemory(dev, cam_out_mem); cam_out_mapped = nullptr; }
                vkDestroyBuffer(dev, cam_out_buf, nullptr);
                cam_out_buf = VK_NULL_HANDLE;
            }
            if (cam_out_mem) { vkFreeMemory(dev, cam_out_mem, nullptr); cam_out_mem = VK_NULL_HANDLE; }

            if (vt_atlas_sampler) vkDestroySampler(dev, vt_atlas_sampler, nullptr);
            if (vt_atlas_view) vkDestroyImageView(dev, vt_atlas_view, nullptr);
            if (vt_atlas_image) { vkDestroyImage(dev, vt_atlas_image, nullptr); vt_atlas_image = VK_NULL_HANDLE; }
            if (vt_atlas_mem) { vkFreeMemory(dev, vt_atlas_mem, nullptr); vt_atlas_mem = VK_NULL_HANDLE; }
            if (vt_pagetable_buf) { vkDestroyBuffer(dev, vt_pagetable_buf, nullptr); vt_pagetable_buf = VK_NULL_HANDLE; }
            if (vt_pagetable_mem) { vkFreeMemory(dev, vt_pagetable_mem, nullptr); vt_pagetable_mem = VK_NULL_HANDLE; }

            if (instance_buf) {
                if (instance_mapped) { vkUnmapMemory(dev, instance_mem); instance_mapped = nullptr; }
                vkDestroyBuffer(dev, instance_buf, nullptr);
                vkFreeMemory(dev, instance_mem, nullptr);
                instance_buf = VK_NULL_HANDLE;
                instance_mem = VK_NULL_HANDLE;
                instance_capacity = 0;
            }

            if (cmdpool) vkDestroyCommandPool(dev, cmdpool, nullptr);
            if (sem_image) vkDestroySemaphore(dev, sem_image, nullptr);
            if (sem_render) vkDestroySemaphore(dev, sem_render, nullptr);
            if (fence) vkDestroyFence(dev, fence, nullptr);
            vkDestroyDevice(dev, nullptr);
        }
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (dbg) {
            auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
            if (fn) fn(instance, dbg, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
};

static VKAPI_ATTR VkBool32 VKAPI_CALL dbg_cb(
    VkDebugUtilsMessageSeverityFlagBitsEXT sev,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    if (!data || !data->pMessage) return VK_FALSE;
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        OutputDebugStringA("[Vulkan] ");
        OutputDebugStringA(data->pMessage);
        OutputDebugStringA("\n");
    }
    return VK_FALSE;
}

static bool env_truthy(const char* name) {
    char buf[8] = {0};
    DWORD n = GetEnvironmentVariableA(name, buf, (DWORD)sizeof(buf));
    if (n == 0) return false;
    return (buf[0] == '1' || buf[0] == 'y' || buf[0] == 'Y' || buf[0] == 't' || buf[0] == 'T');
}

static void vk_check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) {
        char b[256];
        sprintf_s(b, "Vulkan error %d at %s\n", (int)r, what);
        OutputDebugStringA(b);
        MessageBoxA(nullptr, b, "GenesisEngineVulkan", MB_ICONERROR | MB_OK);
        abort();
    }
}

static uint32_t ew_find_memory_type(VkPhysicalDevice phys, uint32_t type_bits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) && ((mp.memoryTypes[i].propertyFlags & props) == props)) return i;
    }
    return UINT32_MAX;
}

static bool ew_create_or_resize_instance_buffer(App::VkCtx& vk, VkDeviceSize needed_bytes) {
    const VkDeviceSize min_bytes = 64 * 1024;
    if (needed_bytes < min_bytes) needed_bytes = min_bytes;
    if (vk.instance_buf && vk.instance_capacity >= needed_bytes) return true;

    if (vk.instance_buf) {
        if (vk.instance_mapped) { vkUnmapMemory(vk.dev, vk.instance_mem); vk.instance_mapped = nullptr; }
        vkDestroyBuffer(vk.dev, vk.instance_buf, nullptr);
        vkFreeMemory(vk.dev, vk.instance_mem, nullptr);
        vk.instance_buf = VK_NULL_HANDLE;
        vk.instance_mem = VK_NULL_HANDLE;
        vk.instance_capacity = 0;
    }

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = needed_bytes;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_check(vkCreateBuffer(vk.dev, &bi, nullptr, &vk.instance_buf), "vkCreateBuffer(instance)");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vk.dev, vk.instance_buf, &req);
    uint32_t mt = ew_find_memory_type(vk.phys, req.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) return false;

    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    vk_check(vkAllocateMemory(vk.dev, &ai, nullptr, &vk.instance_mem), "vkAllocateMemory(instance)");
    vk_check(vkBindBufferMemory(vk.dev, vk.instance_buf, vk.instance_mem, 0), "vkBindBufferMemory(instance)");

    vk_check(vkMapMemory(vk.dev, vk.instance_mem, 0, req.size, 0, &vk.instance_mapped), "vkMapMemory(instance)");
    vk.instance_capacity = needed_bytes;
    return true;
}


static void ew_transition_image(VkCommandBuffer cmd, VkImage img, VkImageLayout oldL, VkImageLayout newL, uint32_t mip_levels) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    b.oldLayout = oldL;
    b.newLayout = newL;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = mip_levels;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

// --- Production virtual texturing -------------------------------------------------------------
// This implements a deterministic tile cache (atlas) and a virtual page table SSBO.
// Tile source is currently procedural (deterministic) but the cache/pagetable/LOD behavior is real.
// Environment hook: set GENESIS_VT_VIRTUAL_DIM to override the 32k ceiling (still clamped to <=32768).

static uint32_t ew_vt_pack_atlas_xy(uint32_t ax, uint32_t ay) {
    return (ay << 16) | (ax & 0xFFFFu);
}

static void ew_vt_build_pagetable(App::VkCtx& vk) {
    uint32_t dim = vk.vt_virtual_dim;
    uint32_t mips = 1;
    while (dim > vk.vt_tile_size) { dim >>= 1; mips++; if (mips >= 16) break; }
    vk.vt_mip_count = mips;

    uint32_t offset = 0;
    for (uint32_t m = 0; m < mips; ++m) {
        uint32_t vdim = std::max(vk.vt_virtual_dim >> m, 1u);
        uint32_t tiles = std::max(vdim / vk.vt_tile_size, 1u);
        vk.vt_mip_tiles_per_row[m] = tiles;
        vk.vt_mip_offset[m] = offset;
        offset += tiles * tiles;
    }
    vk.vt_entries.assign(offset, 0xFFFFFFFFu);
}

static void ew_vt_upload_pagetable(App::VkCtx& vk) {
    const size_t header_u32 = 4 + 16 + 16;
    const size_t total_u32 = header_u32 + vk.vt_entries.size();
    const size_t bytes = total_u32 * sizeof(uint32_t);

    std::vector<uint32_t> blob;
    blob.resize(total_u32);

    blob[0] = vk.vt_atlas_dim;
    blob[1] = vk.vt_tile_size;
    blob[2] = vk.vt_mip_count;
    blob[3] = vk.vt_virtual_dim;
    for (uint32_t i = 0; i < 16; ++i) blob[4 + i] = vk.vt_mip_offset[i];
    for (uint32_t i = 0; i < 16; ++i) blob[4 + 16 + i] = vk.vt_mip_tiles_per_row[i];
    std::memcpy(blob.data() + header_u32, vk.vt_entries.data(), vk.vt_entries.size() * sizeof(uint32_t));

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = (VkDeviceSize)bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_check(vkCreateBuffer(vk.dev, &bci, nullptr, &staging), "vkCreateBuffer(vt_pt_staging)");

    VkMemoryRequirements breq{};
    vkGetBufferMemoryRequirements(vk.dev, staging, &breq);
    uint32_t mt = ew_find_memory_type(vk.phys, breq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo bai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bai.allocationSize = breq.size;
    bai.memoryTypeIndex = mt;
    vk_check(vkAllocateMemory(vk.dev, &bai, nullptr, &staging_mem), "vkAllocateMemory(vt_pt_staging)");
    vk_check(vkBindBufferMemory(vk.dev, staging, staging_mem, 0), "vkBindBufferMemory(vt_pt_staging)");

    void* mapped = nullptr;
    vk_check(vkMapMemory(vk.dev, staging_mem, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory(vt_pt_staging)");
    std::memcpy(mapped, blob.data(), bytes);
    vkUnmapMemory(vk.dev, staging_mem);

    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = vk.cmdpool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vk_check(vkAllocateCommandBuffers(vk.dev, &cai, &cmd), "vkAllocateCommandBuffers(vt_pt_upload)");

    VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_check(vkBeginCommandBuffer(cmd, &cbi), "vkBeginCommandBuffer(vt_pt_upload)");

    VkBufferCopy cpy{};
    cpy.size = (VkDeviceSize)bytes;
    vkCmdCopyBuffer(cmd, staging, vk.vt_pagetable_buf, 1, &cpy);

    vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer(vt_pt_upload)");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vk_check(vkQueueSubmit(vk.gfxq, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit(vt_pt_upload)");
    vk_check(vkQueueWaitIdle(vk.gfxq), "vkQueueWaitIdle(vt_pt_upload)");

    vkFreeCommandBuffers(vk.dev, vk.cmdpool, 1, &cmd);
    vkDestroyBuffer(vk.dev, staging, nullptr);
    vkFreeMemory(vk.dev, staging_mem, nullptr);
}

static void ew_vt_init(App::VkCtx& vk) {
    if (vk.vt_atlas_image) return;

    char buf[64] = {0};
    DWORD n = GetEnvironmentVariableA("GENESIS_VT_VIRTUAL_DIM", buf, (DWORD)sizeof(buf));
    if (n > 0) {
        uint32_t v = (uint32_t)std::strtoul(buf, nullptr, 10);
        if (v >= 1024u && v <= 32768u) vk.vt_virtual_dim = v;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(vk.phys, &props);
    if (vk.vt_virtual_dim > 32768u) vk.vt_virtual_dim = 32768u;
    if (vk.vt_atlas_dim > props.limits.maxImageDimension2D) vk.vt_atlas_dim = props.limits.maxImageDimension2D;

    ew_vt_build_pagetable(vk);

    vk.vt_tiles_per_row_atlas = vk.vt_atlas_dim / vk.vt_tile_size;
    const uint32_t slot_count = vk.vt_tiles_per_row_atlas * vk.vt_tiles_per_row_atlas;
    vk.vt_slots.assign(slot_count, {});

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {vk.vt_atlas_dim, vk.vt_atlas_dim, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk_check(vkCreateImage(vk.dev, &ici, nullptr, &vk.vt_atlas_image), "vkCreateImage(vt_atlas)");

    VkMemoryRequirements ireq{};
    vkGetImageMemoryRequirements(vk.dev, vk.vt_atlas_image, &ireq);
    uint32_t imt = ew_find_memory_type(vk.phys, ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo iai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    iai.allocationSize = ireq.size;
    iai.memoryTypeIndex = imt;
    vk_check(vkAllocateMemory(vk.dev, &iai, nullptr, &vk.vt_atlas_mem), "vkAllocateMemory(vt_atlas)");
    vk_check(vkBindImageMemory(vk.dev, vk.vt_atlas_image, vk.vt_atlas_mem, 0), "vkBindImageMemory(vt_atlas)");

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = vk.vt_atlas_image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    vk_check(vkCreateImageView(vk.dev, &vci, nullptr, &vk.vt_atlas_view), "vkCreateImageView(vt_atlas)");

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.minLod = 0.0f;
    sci.maxLod = 0.0f;
    vk_check(vkCreateSampler(vk.dev, &sci, nullptr, &vk.vt_atlas_sampler), "vkCreateSampler(vt_atlas)");

    const size_t header_u32 = 4 + 16 + 16;
    const size_t total_u32 = header_u32 + vk.vt_entries.size();
    VkDeviceSize pt_bytes = (VkDeviceSize)(total_u32 * sizeof(uint32_t));
    VkBufferCreateInfo bc2{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc2.size = pt_bytes;
    bc2.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bc2.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_check(vkCreateBuffer(vk.dev, &bc2, nullptr, &vk.vt_pagetable_buf), "vkCreateBuffer(vt_pagetable)");

    VkMemoryRequirements breq{};
    vkGetBufferMemoryRequirements(vk.dev, vk.vt_pagetable_buf, &breq);
    uint32_t mt = ew_find_memory_type(vk.phys, breq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo bai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bai.allocationSize = breq.size;
    bai.memoryTypeIndex = mt;
    vk_check(vkAllocateMemory(vk.dev, &bai, nullptr, &vk.vt_pagetable_mem), "vkAllocateMemory(vt_pagetable)");
    vk_check(vkBindBufferMemory(vk.dev, vk.vt_pagetable_buf, vk.vt_pagetable_mem, 0), "vkBindBufferMemory(vt_pagetable)");

    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = vk.cmdpool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vk_check(vkAllocateCommandBuffers(vk.dev, &cai, &cmd), "vkAllocateCommandBuffers(vt_init)");
    VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_check(vkBeginCommandBuffer(cmd, &cbi), "vkBeginCommandBuffer(vt_init)");

    ew_transition_image(cmd, vk.vt_atlas_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);

    vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer(vt_init)");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vk_check(vkQueueSubmit(vk.gfxq, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit(vt_init)");
    vk_check(vkQueueWaitIdle(vk.gfxq), "vkQueueWaitIdle(vt_init)");
    vkFreeCommandBuffers(vk.dev, vk.cmdpool, 1, &cmd);

    ew_vt_upload_pagetable(vk);
}

static void ew_vt_upload_tile_rgba8(App::VkCtx& vk, uint32_t atlas_x_tiles, uint32_t atlas_y_tiles,
                                   const uint8_t* rgba, size_t rgba_bytes) {
    // Upload a single tile into the atlas at (atlas_x_tiles, atlas_y_tiles).
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = (VkDeviceSize)rgba_bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_check(vkCreateBuffer(vk.dev, &bci, nullptr, &staging), "vkCreateBuffer(vt_tile_staging)");

    VkMemoryRequirements breq{};
    vkGetBufferMemoryRequirements(vk.dev, staging, &breq);
    uint32_t mt = ew_find_memory_type(vk.phys, breq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo bai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bai.allocationSize = breq.size;
    bai.memoryTypeIndex = mt;
    vk_check(vkAllocateMemory(vk.dev, &bai, nullptr, &staging_mem), "vkAllocateMemory(vt_tile_staging)");
    vk_check(vkBindBufferMemory(vk.dev, staging, staging_mem, 0), "vkBindBufferMemory(vt_tile_staging)");

    void* mapped = nullptr;
    vk_check(vkMapMemory(vk.dev, staging_mem, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory(vt_tile_staging)");
    std::memcpy(mapped, rgba, rgba_bytes);
    vkUnmapMemory(vk.dev, staging_mem);

    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = vk.cmdpool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vk_check(vkAllocateCommandBuffers(vk.dev, &cai, &cmd), "vkAllocateCommandBuffers(vt_tile_upload)");

    VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_check(vkBeginCommandBuffer(cmd, &cbi), "vkBeginCommandBuffer(vt_tile_upload)");

    // Temporarily transition atlas to transfer dst.
    ew_transition_image(cmd, vk.vt_atlas_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);

    VkBufferImageCopy reg{};
    reg.bufferOffset = 0;
    reg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    reg.imageSubresource.mipLevel = 0;
    reg.imageSubresource.baseArrayLayer = 0;
    reg.imageSubresource.layerCount = 1;
    reg.imageOffset = {(int32_t)(atlas_x_tiles * vk.vt_tile_size), (int32_t)(atlas_y_tiles * vk.vt_tile_size), 0};
    reg.imageExtent = {vk.vt_tile_size, vk.vt_tile_size, 1};
    vkCmdCopyBufferToImage(cmd, staging, vk.vt_atlas_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);

    ew_transition_image(cmd, vk.vt_atlas_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);

    vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer(vt_tile_upload)");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vk_check(vkQueueSubmit(vk.gfxq, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit(vt_tile_upload)");
    vk_check(vkQueueWaitIdle(vk.gfxq), "vkQueueWaitIdle(vt_tile_upload)");

    vkFreeCommandBuffers(vk.dev, vk.cmdpool, 1, &cmd);
    vkDestroyBuffer(vk.dev, staging, nullptr);
    vkFreeMemory(vk.dev, staging_mem, nullptr);
}

static void ew_vt_generate_tile_rgba8(uint32_t mip, uint32_t tx, uint32_t ty, uint32_t tile_size, std::vector<uint8_t>& out) {
    // Deterministic procedural tile: high-frequency checker with mip-dependent smoothing.
    out.resize((size_t)tile_size * (size_t)tile_size * 4);
    const uint32_t freq = 8u >> (mip > 3 ? 3 : mip);
    const uint32_t cell = std::max(1u, tile_size / std::max(1u, freq));
    for (uint32_t y=0; y<tile_size; ++y) {
        for (uint32_t x=0; x<tile_size; ++x) {
            uint32_t gx = (tx * tile_size + x) / cell;
            uint32_t gy = (ty * tile_size + y) / cell;
            uint8_t v = ((gx ^ gy) & 1u) ? 220u : 40u;
            // Encode mip/coords subtly so residency is visually testable.
            uint8_t r = (uint8_t)std::min(255u, (uint32_t)v + (mip * 6u));
            uint8_t g = (uint8_t)std::min(255u, (uint32_t)v + ((tx ^ ty) & 15u));
            uint8_t b = v;
            size_t i = (size_t)(y*tile_size + x) * 4;
            out[i+0] = r;
            out[i+1] = g;
            out[i+2] = b;
            out[i+3] = 255;
        }
    }
}

static bool ew_vt_ensure_tile_resident(App::VkCtx& vk, uint32_t tick_u32, uint32_t mip, uint32_t tx, uint32_t ty, bool* out_pt_changed) {
    if (!vk.vt_atlas_image) return false;
    if (mip >= vk.vt_mip_count) return false;
    const uint32_t tiles = vk.vt_mip_tiles_per_row[mip];
    if (tx >= tiles || ty >= tiles) return false;
    const uint32_t idx = vk.vt_mip_offset[mip] + ty * tiles + tx;
    if (idx >= vk.vt_entries.size()) return false;

    const uint32_t virt_key = (mip << 28) ^ (tx << 14) ^ (ty & 0x3FFFu);
    uint32_t cur = vk.vt_entries[idx];
    if (cur != 0xFFFFFFFFu) {
        // Mark slot as recently used.
        uint32_t ax = cur & 0xFFFFu;
        uint32_t ay = (cur >> 16) & 0xFFFFu;
        uint32_t slot = ay * vk.vt_tiles_per_row_atlas + ax;
        if (slot < vk.vt_slots.size()) vk.vt_slots[slot].last_use_tick = tick_u32;
        return true;
    }

    // Find a free slot or evict LRU.
    uint32_t best = 0;
    bool found_free = false;
    uint32_t best_tick = 0xFFFFFFFFu;
    for (uint32_t s=0; s<(uint32_t)vk.vt_slots.size(); ++s) {
        if (!vk.vt_slots[s].used) { best = s; found_free = true; break; }
        if (vk.vt_slots[s].last_use_tick < best_tick) { best_tick = vk.vt_slots[s].last_use_tick; best = s; }
    }

    // Evict: remove old mapping(s) (linear scan; acceptable at current sizes; deterministic).
    if (!found_free && vk.vt_slots[best].used) {
        uint32_t old_key = vk.vt_slots[best].virt_key;
        // Remove from page table entries
        for (size_t i=0;i<vk.vt_entries.size();++i) {
            uint32_t e = vk.vt_entries[i];
            if (e == 0xFFFFFFFFu) continue;
            uint32_t ax = e & 0xFFFFu;
            uint32_t ay = (e >> 16) & 0xFFFFu;
            uint32_t slot = ay * vk.vt_tiles_per_row_atlas + ax;
            if (slot == best) {
                vk.vt_entries[i] = 0xFFFFFFFFu;
            }
        }
        if (out_pt_changed) *out_pt_changed = true;
        (void)old_key;
    }

    uint32_t ax = best % vk.vt_tiles_per_row_atlas;
    uint32_t ay = best / vk.vt_tiles_per_row_atlas;

    // Generate and upload tile.
    std::vector<uint8_t> tile;
    ew_vt_generate_tile_rgba8(mip, tx, ty, vk.vt_tile_size, tile);
    ew_vt_upload_tile_rgba8(vk, ax, ay, tile.data(), tile.size());

    vk.vt_entries[idx] = ew_vt_pack_atlas_xy(ax, ay);
    if (out_pt_changed) *out_pt_changed = true;

    vk.vt_slots[best].used = true;
    vk.vt_slots[best].virt_key = virt_key;
    vk.vt_slots[best].last_use_tick = tick_u32;
    return true;
}


static std::vector<uint32_t> ew_read_spv_u32(const std::wstring& path) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (n % 4) != 0) { fclose(f); return {}; }
    std::vector<uint32_t> out;
    out.resize((size_t)(n / 4));
    fread(out.data(), 4, out.size(), f);
    fclose(f);
    return out;
}

static VkShaderModule ew_make_shader(VkDevice dev, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size() * 4;
    ci.pCode = code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    vk_check(vkCreateShaderModule(dev, &ci, nullptr, &m), "vkCreateShaderModule");
    return m;
}

static void ew_append_space_separated_exts(const char* s, std::vector<std::string>& storage, std::vector<const char*>& out) {
    if (!s) return;
    const char* p = s;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ' ') ++p;
        storage.emplace_back(start, (size_t)(p - start));
    }
    for (auto& st : storage) out.push_back(st.c_str());
}

static VkInstance create_instance(bool enable_validation, VkDebugUtilsMessengerEXT* out_dbg, const char* extra_exts_space_separated) {
    std::vector<const char*> exts;
    exts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    exts.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    if (enable_validation) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    // Optional OpenXR-required Vulkan instance extensions.
    std::vector<std::string> extra_storage;
    ew_append_space_separated_exts(extra_exts_space_separated, extra_storage, exts);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "GenesisEngine";
    app.applicationVersion = VK_MAKE_VERSION(0,1,0);
    app.pEngineName = "GenesisEngine";
    app.engineVersion = VK_MAKE_VERSION(0,1,0);
    app.apiVersion = VK_API_VERSION_1_3; // dynamic rendering core

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();

    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    if (enable_validation) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = layers;

        VkDebugUtilsMessengerCreateInfoEXT dbgci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        dbgci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgci.pfnUserCallback = dbg_cb;
        ci.pNext = &dbgci;
    }

    VkInstance inst = VK_NULL_HANDLE;
    vk_check(vkCreateInstance(&ci, nullptr, &inst), "vkCreateInstance");

    if (enable_validation && out_dbg) {
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT");
        if (fn) {
            VkDebugUtilsMessengerCreateInfoEXT dbgci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            dbgci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dbgci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dbgci.pfnUserCallback = dbg_cb;
            vk_check(fn(inst, &dbgci, nullptr, out_dbg), "vkCreateDebugUtilsMessengerEXT");
        }
    }

    return inst;
}

static VkSurfaceKHR create_surface(VkInstance inst, HWND hwnd) {
    // Win64 builds still use the Win32 surface extension API.
    VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    ci.hinstance = GetModuleHandleW(nullptr);
    ci.hwnd = hwnd;
    VkSurfaceKHR s = VK_NULL_HANDLE;
    vk_check(vkCreateWin32SurfaceKHR(inst, &ci, nullptr, &s), "vkCreateWin32SurfaceKHR");
    return s;
}

static VkPhysicalDevice pick_phys(VkInstance inst) {
    uint32_t n = 0;
    vk_check(vkEnumeratePhysicalDevices(inst, &n, nullptr), "vkEnumeratePhysicalDevices(count)");
    std::vector<VkPhysicalDevice> devs(n);
    vk_check(vkEnumeratePhysicalDevices(inst, &n, devs.data()), "vkEnumeratePhysicalDevices");
    return (n > 0) ? devs[0] : VK_NULL_HANDLE;
}

static uint32_t find_gfx_queue(VkPhysicalDevice phys, VkSurfaceKHR surf) {
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, nullptr);
    std::vector<VkQueueFamilyProperties> props(n);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, props.data());

    for (uint32_t i = 0; i < n; ++i) {
        if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) continue;
        VkBool32 present = VK_FALSE;
        vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surf, &present), "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (present) return i;
    }
    return 0;
}

static VkDevice create_device(VkPhysicalDevice phys, uint32_t qfam, VkQueue* out_q, const char* extra_exts_space_separated) {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    std::vector<const char*> exts;
    exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // Optional OpenXR-required Vulkan device extensions.
    std::vector<std::string> extra_storage;
    ew_append_space_separated_exts(extra_exts_space_separated, extra_storage, exts);

    // Vulkan 1.3 core has dynamic rendering, but some drivers still prefer explicit enabling.
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (uint32_t)exts.size();
    dci.ppEnabledExtensionNames = exts.data();
    dci.pNext = &f13;

    VkDevice dev = VK_NULL_HANDLE;
    vk_check(vkCreateDevice(phys, &dci, nullptr, &dev), "vkCreateDevice");
    vkGetDeviceQueue(dev, qfam, 0, out_q);
    return dev;
}

static VkSurfaceFormatKHR choose_format(VkPhysicalDevice phys, VkSurfaceKHR surf) {
    uint32_t n = 0;
    vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surf, &n, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    std::vector<VkSurfaceFormatKHR> fmts(n);
    vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surf, &n, fmts.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return f;
    }
    return fmts[0];
}

static VkPresentModeKHR choose_present_mode(VkPhysicalDevice phys, VkSurfaceKHR surf) {
    uint32_t n = 0;
    vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surf, &n, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    std::vector<VkPresentModeKHR> modes(n);
    vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surf, &n, modes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR");
    for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

static void create_swap(App::VkCtx& vk, uint32_t w, uint32_t h) {
    VkSurfaceCapabilitiesKHR caps{};
    vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.phys, vk.surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    VkSurfaceFormatKHR fmt = choose_format(vk.phys, vk.surface);
    VkPresentModeKHR pm = choose_present_mode(vk.phys, vk.surface);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFF) {
        extent.width = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) image_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = vk.surface;
    sci.minImageCount = image_count;
    sci.imageFormat = fmt.format;
    sci.imageColorSpace = fmt.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = pm;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;

    vk_check(vkCreateSwapchainKHR(vk.dev, &sci, nullptr, &vk.swap), "vkCreateSwapchainKHR");

    uint32_t nimg = 0;
    vk_check(vkGetSwapchainImagesKHR(vk.dev, vk.swap, &nimg, nullptr), "vkGetSwapchainImagesKHR(count)");
    vk.swap_images.resize(nimg);
    vk_check(vkGetSwapchainImagesKHR(vk.dev, vk.swap, &nimg, vk.swap_images.data()), "vkGetSwapchainImagesKHR");

    vk.swap_views.resize(nimg);
    for (uint32_t i = 0; i < nimg; ++i) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = vk.swap_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt.format;
        vci.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel = 0;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount = 1;
        vk_check(vkCreateImageView(vk.dev, &vci, nullptr, &vk.swap_views[i]), "vkCreateImageView");
    }

    vk.swap_format = fmt.format;
    vk.swap_extent = extent;

    // Depth buffer for sensor histogram + proper 3D depth.
    {
        // Destroy previous if any (swap resize).
        if (vk.depth_sampler) { vkDestroySampler(vk.dev, vk.depth_sampler, nullptr); vk.depth_sampler = VK_NULL_HANDLE; }
        if (vk.depth_view) { vkDestroyImageView(vk.dev, vk.depth_view, nullptr); vk.depth_view = VK_NULL_HANDLE; }
        if (vk.depth_image) { vkDestroyImage(vk.dev, vk.depth_image, nullptr); vk.depth_image = VK_NULL_HANDLE; }
        if (vk.depth_mem) { vkFreeMemory(vk.dev, vk.depth_mem, nullptr); vk.depth_mem = VK_NULL_HANDLE; }

        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = vk.depth_format;
        ici.extent = {extent.width, extent.height, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vk_check(vkCreateImage(vk.dev, &ici, nullptr, &vk.depth_image), "vkCreateImage(depth)");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(vk.dev, vk.depth_image, &req);
        uint32_t mt = ew_find_memory_type(vk.phys, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = mt;
        vk_check(vkAllocateMemory(vk.dev, &mai, nullptr, &vk.depth_mem), "vkAllocateMemory(depth)");
        vk_check(vkBindImageMemory(vk.dev, vk.depth_image, vk.depth_mem, 0), "vkBindImageMemory(depth)");

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = vk.depth_image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = vk.depth_format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        vk_check(vkCreateImageView(vk.dev, &vci, nullptr, &vk.depth_view), "vkCreateImageView(depth)");

        VkSamplerCreateInfo sci_s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci_s.magFilter = VK_FILTER_NEAREST;
        sci_s.minFilter = VK_FILTER_NEAREST;
        sci_s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci_s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci_s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci_s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci_s.minLod = 0.0f;
        sci_s.maxLod = 0.0f;
        vk_check(vkCreateSampler(vk.dev, &sci_s, nullptr, &vk.depth_sampler), "vkCreateSampler(depth)");
    }

    // If camera sensor descriptor sets exist, refresh depth binding.
    if (vk.cam_hist_ds && vk.cam_median_ds) {
        VkDescriptorImageInfo di{};
        di.sampler = vk.depth_sampler;
        di.imageView = vk.depth_view;
        di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &di;
        w.dstSet = vk.cam_hist_ds;
        vkUpdateDescriptorSets(vk.dev, 1, &w, 0, nullptr);
        w.dstSet = vk.cam_median_ds;
        vkUpdateDescriptorSets(vk.dev, 1, &w, 0, nullptr);
    }

    // Command buffers sized to swap.
    vk.cmdbufs.resize(nimg);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = vk.cmdpool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)nimg;
    vk_check(vkAllocateCommandBuffers(vk.dev, &ai, vk.cmdbufs.data()), "vkAllocateCommandBuffers");
}

static void destroy_pipeline(App::VkCtx& vk) {
    if (!vk.dev) return;
    if (vk.pipe) { vkDestroyPipeline(vk.dev, vk.pipe, nullptr); vk.pipe = VK_NULL_HANDLE; }
    if (vk.pipe_layout) { vkDestroyPipelineLayout(vk.dev, vk.pipe_layout, nullptr); vk.pipe_layout = VK_NULL_HANDLE; }
    if (vk.ds_layout) { vkDestroyDescriptorSetLayout(vk.dev, vk.ds_layout, nullptr); vk.ds_layout = VK_NULL_HANDLE; }
    if (vk.ds_pool) { vkDestroyDescriptorPool(vk.dev, vk.ds_pool, nullptr); vk.ds_pool = VK_NULL_HANDLE; vk.ds = VK_NULL_HANDLE; }
}

static void create_pipeline(App::VkCtx& vk) {
    destroy_pipeline(vk);

    // Descriptor set:
    //  binding 0 = instances SSBO
    //  binding 1 = virtual texture atlas sampler
    //  binding 2 = virtual texture page table SSBO
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = 3;
    dlci.pBindings = bindings;
    vk_check(vkCreateDescriptorSetLayout(vk.dev, &dlci, nullptr, &vk.ds_layout), "vkCreateDescriptorSetLayout");
VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = 96; // mat4 (64) + vec3+float (16) + vec4 debug (16)

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &vk.ds_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    vk_check(vkCreatePipelineLayout(vk.dev, &plci, nullptr, &vk.pipe_layout), "vkCreatePipelineLayout");

    // Descriptor pool + set
    VkDescriptorPoolSize ps[3]{};
    ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[1].descriptorCount = 1;
    ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 3;
    dpci.pPoolSizes = ps;
    vk_check(vkCreateDescriptorPool(vk.dev, &dpci, nullptr, &vk.ds_pool), "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = vk.ds_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &vk.ds_layout;
    vk_check(vkAllocateDescriptorSets(vk.dev, &dsai, &vk.ds), "vkAllocateDescriptorSets");

    // Ensure production virtual texture system is created before first draw.
    if (!vk.vt_atlas_image) {
        ew_vt_init(vk);
    }

    VkDescriptorImageInfo di{};
    di.sampler = vk.vt_atlas_sampler;
    di.imageView = vk.vt_atlas_view;
    di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w1{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w1.dstSet = vk.ds;
    w1.dstBinding = 1;
    w1.descriptorCount = 1;
    w1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w1.pImageInfo = &di;

    VkDescriptorBufferInfo bi2{};
    bi2.buffer = vk.vt_pagetable_buf;
    bi2.offset = 0;
    bi2.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet w2{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w2.dstSet = vk.ds;
    w2.dstBinding = 2;
    w2.descriptorCount = 1;
    w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w2.pBufferInfo = &bi2;

    VkWriteDescriptorSet writes[2] = {w1, w2};
    vkUpdateDescriptorSets(vk.dev, 2, writes, 0, nullptr);

    // Load SPIR-V (built at compile time by Vulkan SDK tools)
    std::wstring vert_path = utf8_to_wide(std::string(EW_SHADER_OUT_DIR) + "\\instanced.vert.spv");
    std::wstring frag_path = utf8_to_wide(std::string(EW_SHADER_OUT_DIR) + "\\instanced.frag.spv");
    auto vert = ew_read_spv_u32(vert_path);
    auto frag = ew_read_spv_u32(frag_path);
    if (vert.empty() || frag.empty()) {
        MessageBoxA(nullptr, "Missing shader SPIR-V. Ensure Vulkan SDK is installed and shaders compiled.", "GenesisEngineVulkan", MB_ICONERROR | MB_OK);
        abort();
    }
    VkShaderModule vs = ew_make_shader(vk.dev, vert);
    VkShaderModule fs = ew_make_shader(vk.dev, frag);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = VK_TRUE;
    dss.depthWriteEnable = VK_TRUE;
    dss.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    dss.depthBoundsTestEnable = VK_FALSE;
    dss.stencilTestEnable = VK_FALSE;

    VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyns;

    VkPipelineRenderingCreateInfo pr{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pr.colorAttachmentCount = 1;
    pr.pColorAttachmentFormats = &vk.swap_format;
    pr.depthAttachmentFormat = vk.depth_format;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.pNext = &pr;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &cb;
    pci.pDepthStencilState = &dss;
    pci.pDynamicState = &ds;
    pci.layout = vk.pipe_layout;
    pci.renderPass = VK_NULL_HANDLE;
    pci.subpass = 0;

    vk_check(vkCreateGraphicsPipelines(vk.dev, VK_NULL_HANDLE, 1, &pci, nullptr, &vk.pipe), "vkCreateGraphicsPipelines");

    vkDestroyShaderModule(vk.dev, vs, nullptr);
    vkDestroyShaderModule(vk.dev, fs, nullptr);

    // -----------------------------------------------------------------
    // Camera sensor compute pipelines (depth histogram + deterministic median)
    // -----------------------------------------------------------------
    if (!vk.cam_ds_layout) {
        VkDescriptorSetLayoutBinding cb[3]{};
        // binding0: depth sampler
        cb[0].binding = 0;
        cb[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cb[0].descriptorCount = 1;
        cb[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        // binding1: histogram SSBO
        cb[1].binding = 1;
        cb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cb[1].descriptorCount = 1;
        cb[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        // binding2: out SSBO
        cb[2].binding = 2;
        cb[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cb[2].descriptorCount = 1;
        cb[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.bindingCount = 3;
        dl.pBindings = cb;
        vk_check(vkCreateDescriptorSetLayout(vk.dev, &dl, nullptr, &vk.cam_ds_layout), "vkCreateDescriptorSetLayout(cam)");

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0;
        pcr.size = 8; // width,height

        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &vk.cam_ds_layout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pcr;
        vk_check(vkCreatePipelineLayout(vk.dev, &pl, nullptr, &vk.cam_pipe_layout), "vkCreatePipelineLayout(cam)");

        // Buffers
        {
            VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bci.size = sizeof(uint32_t) * 256;
            bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            vk_check(vkCreateBuffer(vk.dev, &bci, nullptr, &vk.cam_hist_buf), "vkCreateBuffer(cam_hist)");
            VkMemoryRequirements breq{};
            vkGetBufferMemoryRequirements(vk.dev, vk.cam_hist_buf, &breq);
            uint32_t mt = ew_find_memory_type(vk.phys, breq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            mai.allocationSize = breq.size;
            mai.memoryTypeIndex = mt;
            vk_check(vkAllocateMemory(vk.dev, &mai, nullptr, &vk.cam_hist_mem), "vkAllocateMemory(cam_hist)");
            vk_check(vkBindBufferMemory(vk.dev, vk.cam_hist_buf, vk.cam_hist_mem, 0), "vkBindBufferMemory(cam_hist)");
        }
        {
            VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bci.size = sizeof(uint32_t) * 2;
            bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            vk_check(vkCreateBuffer(vk.dev, &bci, nullptr, &vk.cam_out_buf), "vkCreateBuffer(cam_out)");
            VkMemoryRequirements breq{};
            vkGetBufferMemoryRequirements(vk.dev, vk.cam_out_buf, &breq);
            uint32_t mt = ew_find_memory_type(vk.phys, breq.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            mai.allocationSize = breq.size;
            mai.memoryTypeIndex = mt;
            vk_check(vkAllocateMemory(vk.dev, &mai, nullptr, &vk.cam_out_mem), "vkAllocateMemory(cam_out)");
            vk_check(vkBindBufferMemory(vk.dev, vk.cam_out_buf, vk.cam_out_mem, 0), "vkBindBufferMemory(cam_out)");
            vk_check(vkMapMemory(vk.dev, vk.cam_out_mem, 0, VK_WHOLE_SIZE, 0, &vk.cam_out_mapped), "vkMapMemory(cam_out)");
        }

        // Descriptor pool + sets
        VkDescriptorPoolSize ps[3]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps[0].descriptorCount = 1;
        ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ps[1].descriptorCount = 2;
        ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ps[2].descriptorCount = 2;
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = 2;
        dp.poolSizeCount = 3;
        dp.pPoolSizes = ps;
        vk_check(vkCreateDescriptorPool(vk.dev, &dp, nullptr, &vk.cam_ds_pool), "vkCreateDescriptorPool(cam)");

        VkDescriptorSetLayout layouts[2] = {vk.cam_ds_layout, vk.cam_ds_layout};
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = vk.cam_ds_pool;
        ai.descriptorSetCount = 2;
        ai.pSetLayouts = layouts;
        VkDescriptorSet sets[2]{};
        vk_check(vkAllocateDescriptorSets(vk.dev, &ai, sets), "vkAllocateDescriptorSets(cam)");
        vk.cam_hist_ds = sets[0];
        vk.cam_median_ds = sets[1];

        // Update descriptors
        VkDescriptorImageInfo di{};
        di.sampler = vk.depth_sampler;
        di.imageView = vk.depth_view;
        di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo bi_hist{};
        bi_hist.buffer = vk.cam_hist_buf;
        bi_hist.offset = 0;
        bi_hist.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo bi_out{};
        bi_out.buffer = vk.cam_out_buf;
        bi_out.offset = 0;
        bi_out.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet w[3]{};
        w[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[0].dstSet = vk.cam_hist_ds;
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[0].pImageInfo = &di;
        w[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[1].dstSet = vk.cam_hist_ds;
        w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[1].pBufferInfo = &bi_hist;
        w[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[2].dstSet = vk.cam_hist_ds;
        w[2].dstBinding = 2;
        w[2].descriptorCount = 1;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[2].pBufferInfo = &bi_out;
        vkUpdateDescriptorSets(vk.dev, 3, w, 0, nullptr);

        VkWriteDescriptorSet w2[3]{};
        w2[0] = w[0]; w2[0].dstSet = vk.cam_median_ds;
        w2[1] = w[1]; w2[1].dstSet = vk.cam_median_ds;
        w2[2] = w[2]; w2[2].dstSet = vk.cam_median_ds;
        vkUpdateDescriptorSets(vk.dev, 3, w2, 0, nullptr);

        // Pipelines
        std::wstring hist_path = utf8_to_wide(std::string(EW_SHADER_OUT_DIR) + "\\cam_hist.comp.spv");
        std::wstring med_path  = utf8_to_wide(std::string(EW_SHADER_OUT_DIR) + "\\cam_median.comp.spv");
        auto hist_spv = ew_read_spv_u32(hist_path);
        auto med_spv  = ew_read_spv_u32(med_path);
        if (hist_spv.empty() || med_spv.empty()) {
            MessageBoxA(nullptr, "Missing camera sensor compute shader SPIR-V.", "GenesisEngineVulkan", MB_ICONERROR | MB_OK);
            abort();
        }
        VkShaderModule hs = ew_make_shader(vk.dev, hist_spv);
        VkShaderModule ms = ew_make_shader(vk.dev, med_spv);

        VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cpi.layout = vk.cam_pipe_layout;
        cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpi.stage.module = hs;
        cpi.stage.pName = "main";
        vk_check(vkCreateComputePipelines(vk.dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &vk.cam_hist_pipe), "vkCreateComputePipelines(hist)");
        cpi.stage.module = ms;
        vk_check(vkCreateComputePipelines(vk.dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &vk.cam_median_pipe), "vkCreateComputePipelines(median)");

        vkDestroyShaderModule(vk.dev, hs, nullptr);
        vkDestroyShaderModule(vk.dev, ms, nullptr);
    }
}

App::App(const AppConfig& cfg) : cfg_(cfg) {
    ew_camera_init(cam_);
    std::memset(&input_, 0, sizeof(input_));
    const char* v = std::getenv("EW_IMMERSION");
    if (v && (v[0]=='1' || v[0]=='y' || v[0]=='Y' || v[0]=='t' || v[0]=='T')) {
        immersion_mode_ = true;
    }
}

static void set_dpi_awareness() {
    // Per-monitor v2 where available.
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        using Fn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = (Fn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) {
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            FreeLibrary(user32);
            return;
        }
        FreeLibrary(user32);
    }
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
}

void App::CreateMainWindow(HINSTANCE hInst) {
    set_dpi_awareness();
    ew_theme_init_once();

    WNDCLASSW wc{};
    wc.lpfnWndProc = &App::WndProcThunk;
    wc.hInstance = hInst;
    wc.lpszClassName = L"GenesisEngineVulkanWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    RECT r{0,0,cfg_.initial_width,cfg_.initial_height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_main_ = CreateWindowW(
        wc.lpszClassName,
        utf8_to_wide(cfg_.app_title_utf8).c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInst, this);

    // Minimal Unreal-style menu bar. Actions only route through deterministic
    // UI commands/control packets (no new simulation semantics).
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreateMenu();
    AppendMenuW(hFile, MF_STRING, 9001, L"Exit");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"File");
    if (ew_editor_build_enabled) {
        HMENU hEdit = CreateMenu();
        HMENU hWindow = CreateMenu();
        HMENU hTools = CreateMenu();
        AppendMenuW(hEdit, MF_STRING, 9101, L"Copy");
        AppendMenuW(hEdit, MF_STRING, 9102, L"Paste");
        AppendMenuW(hWindow, MF_STRING | (content_visible_ ? MF_CHECKED : 0), 9201, L"Content Browser");
        AppendMenuW(hWindow, MF_STRING, 9202, L"AI Panel");
        AppendMenuW(hWindow, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hWindow, MF_STRING, 9203, L"Outliner");
        AppendMenuW(hWindow, MF_STRING, 9204, L"Details");
        AppendMenuW(hWindow, MF_STRING, 9205, L"Asset");
        AppendMenuW(hWindow, MF_STRING, 9206, L"Voxel");
        AppendMenuW(hWindow, MF_STRING, 9207, L"Node");
        AppendMenuW(hWindow, MF_STRING, 9208, L"Sequencer");
        AppendMenuW(hWindow, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hWindow, MF_STRING, 9209, L"Live Mode (Substrate Viewport)");
        AppendMenuW(hTools, MF_STRING, 9301, L"Reindex Content");
        AppendMenuW(hTools, MF_STRING, 9302, L"List Content");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEdit, L"Edit");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hWindow, L"Window");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hTools, L"Tools");
    }
    SetMenu(hwnd_main_, hMenu);
    if (ew_editor_build_enabled) SyncWindowMenu();

    ShowWindow(hwnd_main_, SW_SHOW);
}

void App::CreateChildWindows() {
    RECT rc{};
    GetClientRect(hwnd_main_, &rc);
    client_w_ = rc.right - rc.left;
    client_h_ = rc.bottom - rc.top;

    hwnd_viewport_ = CreateWindowW(L"STATIC", L"",
                                   WS_CHILD | WS_VISIBLE,
                                   0, 0, ew_editor_build_enabled ? (client_w_ - 420) : client_w_, client_h_,
                                   hwnd_main_, (HMENU)1001, GetModuleHandleW(nullptr), nullptr);

    if (ew_editor_build_enabled && hwnd_viewport_) {
        hwnd_viewport_resonance_overlay_ = CreateWindowW(L"EDIT", L"",
                                                         WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                                                         ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                                         12, 12, 320, 156,
                                                         hwnd_viewport_, (HMENU)1003, GetModuleHandleW(nullptr), nullptr);
        if (hwnd_viewport_resonance_overlay_ && g_font_ui) SendMessageW(hwnd_viewport_resonance_overlay_, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    }

    if (!ew_editor_build_enabled) {
        hwnd_panel_ = nullptr;
        return;
    }

    hwnd_panel_ = CreateWindowW(L"STATIC", L"",
                                WS_CHILD | WS_VISIBLE,
                                client_w_ - 420, 0, 420, client_h_,
                                hwnd_main_, (HMENU)1002, GetModuleHandleW(nullptr), nullptr);

    // Right dock: tabbed panels (Unreal-style). Pure UI layer: no core logic changes.
    {
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
        InitCommonControlsEx(&icc);
    }
    const int PAD = 12;
    const int TAB_H = 30;
    hwnd_rdock_tab_ = CreateWindowW(WC_TABCONTROLW, L"",
                                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                    PAD, PAD, 420 - 2*PAD, TAB_H,
                                    hwnd_panel_, (HMENU)1500, GetModuleHandleW(nullptr), nullptr);

    TCITEMW ti{};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)L"Outliner";
    TabCtrl_InsertItem(hwnd_rdock_tab_, 0, &ti);
    ti.pszText = (LPWSTR)L"Details";
    TabCtrl_InsertItem(hwnd_rdock_tab_, 1, &ti);
    ti.pszText = (LPWSTR)L"Asset";
    TabCtrl_InsertItem(hwnd_rdock_tab_, 2, &ti);
    ti.pszText = (LPWSTR)L"Voxel";
    TabCtrl_InsertItem(hwnd_rdock_tab_, 3, &ti);
    ti.pszText = (LPWSTR)L"Node";
    TabCtrl_InsertItem(hwnd_rdock_tab_, 4, &ti);
    ti.pszText = (LPWSTR)L"Sequencer";
    TabCtrl_InsertItem(hwnd_rdock_tab_, 5, &ti);

    // Panel roots (children of dock container). Only one visible at a time.
    int panel_x = PAD;
    int panel_y = PAD + TAB_H + 8;
    int panel_w = 420 - 2*PAD;
    int panel_h = client_h_ - panel_y - PAD;

    hwnd_rdock_outliner_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                         panel_x, panel_y, panel_w, panel_h,
                                         hwnd_panel_, (HMENU)1501, GetModuleHandleW(nullptr), nullptr);
    hwnd_rdock_details_  = CreateWindowW(L"STATIC", L"", WS_CHILD,
                                         panel_x, panel_y, panel_w, panel_h,
                                         hwnd_panel_, (HMENU)1502, GetModuleHandleW(nullptr), nullptr);
    hwnd_rdock_asset_    = CreateWindowW(L"STATIC", L"", WS_CHILD,
                                         panel_x, panel_y, panel_w, panel_h,
                                         hwnd_panel_, (HMENU)1503, GetModuleHandleW(nullptr), nullptr);
    hwnd_rdock_voxel_    = CreateWindowW(L"STATIC", L"", WS_CHILD,
                                         panel_x, panel_y, panel_w, panel_h,
                                         hwnd_panel_, (HMENU)1504, GetModuleHandleW(nullptr), nullptr);
    hwnd_rdock_node_     = CreateWindowW(L"STATIC", L"", WS_CHILD,
                                         panel_x, panel_y, panel_w, panel_h,
                                         hwnd_panel_, (HMENU)1505, GetModuleHandleW(nullptr), nullptr);
    hwnd_rdock_sequencer_= CreateWindowW(L"STATIC", L"", WS_CHILD,
                                         panel_x, panel_y, panel_w, panel_h,
                                         hwnd_panel_, (HMENU)1506, GetModuleHandleW(nullptr), nullptr);

    // ---------------------------------------------------------------------
    // Tab-local toolbars (editor ergonomics). These are UI-only surfaces.
    // ---------------------------------------------------------------------
    const int TB_H = 34;
    hwnd_tb_outliner_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                      0, 0, panel_w, TB_H,
                                      hwnd_rdock_outliner_, (HMENU)1510, GetModuleHandleW(nullptr), nullptr);
    hwnd_tb_details_  = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                      0, 0, panel_w, TB_H,
                                      hwnd_rdock_details_, (HMENU)1511, GetModuleHandleW(nullptr), nullptr);
    hwnd_tb_asset_    = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                      0, 0, panel_w, TB_H,
                                      hwnd_rdock_asset_, (HMENU)1512, GetModuleHandleW(nullptr), nullptr);
    hwnd_tb_voxel_    = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                      0, 0, panel_w, TB_H,
                                      hwnd_rdock_voxel_, (HMENU)1513, GetModuleHandleW(nullptr), nullptr);
    hwnd_tb_node_     = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                      0, 0, panel_w, TB_H,
                                      hwnd_rdock_node_, (HMENU)1516, GetModuleHandleW(nullptr), nullptr);
    hwnd_tb_sequencer_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                      0, 0, panel_w, TB_H,
                                      hwnd_rdock_sequencer_, (HMENU)1517, GetModuleHandleW(nullptr), nullptr);

    // Outliner toolbar: search + clear.
    CreateWindowW(L"STATIC", L"Search", WS_CHILD | WS_VISIBLE,
                  10, 8, 44, 18,
                  hwnd_tb_outliner_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_outliner_search_ = CreateWindowW(L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
                                          58, 5, panel_w - 58 - 66, 24,
                                          hwnd_tb_outliner_, (HMENU)1514, GetModuleHandleW(nullptr), nullptr);
    hwnd_outliner_clear_ = CreateWindowW(L"BUTTON", L"Clear",
                                         WS_CHILD | WS_VISIBLE,
                                         panel_w - 60, 5, 50, 24,
                                         hwnd_tb_outliner_, (HMENU)1515, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Lock", WS_CHILD | WS_VISIBLE,
                  panel_w - 120, 5, 54, 24,
                  hwnd_tb_outliner_, (HMENU)1530, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
                  panel_w - 180, 5, 54, 24,
                  hwnd_tb_outliner_, (HMENU)1540, GetModuleHandleW(nullptr), nullptr);

    // Details toolbar: section label (property grid v1 follows below).
    HWND h_ins = CreateWindowW(L"STATIC", L"Inspector", WS_CHILD | WS_VISIBLE,
                               10, 8, 120, 18,
                               hwnd_tb_details_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (h_ins && g_font_ui_bold) SendMessageW(h_ins, WM_SETFONT, (WPARAM)g_font_ui_bold, TRUE);

    
    // Details quick tools (gizmo + history). Compact Unreal-like toolbar.
    CreateWindowW(L"BUTTON", L"T", WS_CHILD | WS_VISIBLE,
                  140, 5, 26, 24,
                  hwnd_tb_details_, (HMENU)2020, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"R", WS_CHILD | WS_VISIBLE,
                  170, 5, 26, 24,
                  hwnd_tb_details_, (HMENU)2021, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Frame", WS_CHILD | WS_VISIBLE,
                  200, 5, 54, 24,
                  hwnd_tb_details_, (HMENU)2022, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Undo", WS_CHILD | WS_VISIBLE,
                  258, 5, 50, 24,
                  hwnd_tb_details_, (HMENU)2030, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Redo", WS_CHILD | WS_VISIBLE,
                  312, 5, 50, 24,
                  hwnd_tb_details_, (HMENU)2031, GetModuleHandleW(nullptr), nullptr);

/* Details toolbar buttons (Apply/Copy/Paste) */
    hwnd_apply_xform_ = CreateWindowW(L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE,
                                      panel_w - 240, 5, 70, 24,
                                      hwnd_tb_details_, (HMENU)2013, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE,
                  panel_w - 164, 5, 60, 24,
                  hwnd_tb_details_, (HMENU)2090, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Paste", WS_CHILD | WS_VISIBLE,
                  panel_w - 98, 5, 60, 24,
                  hwnd_tb_details_, (HMENU)2091, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Lock", WS_CHILD | WS_VISIBLE,
                  10, 5, 54, 24,
                  hwnd_tb_details_, (HMENU)1531, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
                  68, 5, 54, 24,
                  hwnd_tb_details_, (HMENU)1541, GetModuleHandleW(nullptr), nullptr);

    // Asset toolbar: section label.
    HWND h_asset = CreateWindowW(L"STATIC", L"Asset Designer", WS_CHILD | WS_VISIBLE,
                                 10, 8, 160, 18,
                                 hwnd_tb_asset_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (h_asset && g_font_ui_bold) SendMessageW(h_asset, WM_SETFONT, (WPARAM)g_font_ui_bold, TRUE);

    // Asset toolbar quick actions (UI-only hooks; core remains unchanged).
    CreateWindowW(L"BUTTON", L"Train Model", WS_CHILD | WS_VISIBLE,
                  panel_w - 120, 5, 100, 24,
                  hwnd_tb_asset_, (HMENU)2060, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Lock", WS_CHILD | WS_VISIBLE,
                  panel_w - 180, 5, 54, 24,
                  hwnd_tb_asset_, (HMENU)1532, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
                  panel_w - 240, 5, 54, 24,
                  hwnd_tb_asset_, (HMENU)1542, GetModuleHandleW(nullptr), nullptr);


    // Voxel toolbar: presets.
    CreateWindowW(L"STATIC", L"Preset", WS_CHILD | WS_VISIBLE,
                  10, 8, 44, 18,
                  hwnd_tb_voxel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_voxel_preset_ = CreateWindowW(WC_COMBOBOXW, L"",
                                       WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST,
                                       60, 5, panel_w - 60 - 96, 400,
                                       hwnd_tb_voxel_, (HMENU)2610, GetModuleHandleW(nullptr), nullptr);
    hwnd_voxel_apply_ = CreateWindowW(L"BUTTON", L"Apply",
                                      WS_CHILD | WS_VISIBLE,
                                      panel_w - 86, 5, 76, 24,
                                      hwnd_tb_voxel_, (HMENU)2611, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Lock", WS_CHILD | WS_VISIBLE,
                  panel_w - 146, 5, 54, 24,
                  hwnd_tb_voxel_, (HMENU)1533, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
                  panel_w - 206, 5, 54, 24,
                  hwnd_tb_voxel_, (HMENU)1543, GetModuleHandleW(nullptr), nullptr);

    // Populate standard material presets (AI will train against these in sandbox).
    if (hwnd_voxel_preset_) {
        const wchar_t* presets[] = {
            L"Steel", L"Stainless Steel", L"Aluminum", L"Titanium", L"Copper", L"Brass", L"Bronze",
            L"Gold", L"Silver", L"Concrete", L"Asphalt", L"Granite", L"Marble",
            L"Oak Wood", L"Pine Wood", L"Walnut Wood", L"Plywood",
            L"ABS Plastic", L"Polycarbonate", L"Rubber", L"Glass", L"Leather", L"Fabric"
        };
        for (const wchar_t* p : presets) SendMessageW(hwnd_voxel_preset_, CB_ADDSTRING, 0, (LPARAM)p);
        SendMessageW(hwnd_voxel_preset_, CB_SETCURSEL, 0, 0);
    }

    // Node toolbar + summary surface. Derived-only, coherence-facing workspace.
    HWND h_node = CreateWindowW(L"STATIC", L"Node Graph", WS_CHILD | WS_VISIBLE,
                                10, 8, 140, 18,
                                hwnd_tb_node_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (h_node && g_font_ui_bold) SendMessageW(h_node, WM_SETFONT, (WPARAM)g_font_ui_bold, TRUE);
    CreateWindowW(L"BUTTON", L"Lock", WS_CHILD | WS_VISIBLE,
                  panel_w - 180, 5, 54, 24,
                  hwnd_tb_node_, (HMENU)1534, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
                  panel_w - 240, 5, 54, 24,
                  hwnd_tb_node_, (HMENU)1544, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_open_coh_ = CreateWindowW(L"BUTTON", L"Open Coherence", WS_CHILD | WS_VISIBLE,
                                        panel_w - 136, 5, 126, 24,
                                        hwnd_tb_node_, (HMENU)2712, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_expand_ = CreateWindowW(L"BUTTON", L"Expand", WS_CHILD | WS_VISIBLE,
                                      10, 5, 72, 24,
                                      hwnd_tb_node_, (HMENU)2714, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_play_ = CreateWindowW(L"BUTTON", L"Excite", WS_CHILD | WS_VISIBLE,
                                    88, 5, 72, 24,
                                    hwnd_tb_node_, (HMENU)2715, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_mode_local_ = CreateWindowW(L"BUTTON", L"Local", WS_CHILD | WS_VISIBLE,
                                          166, 5, 66, 24,
                                          hwnd_tb_node_, (HMENU)2716, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_mode_propagate_ = CreateWindowW(L"BUTTON", L"Propagate", WS_CHILD | WS_VISIBLE,
                                              238, 5, 88, 24,
                                              hwnd_tb_node_, (HMENU)2717, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_search_ = CreateWindowW(L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                      332, 5, 150, 24,
                                      hwnd_tb_node_, (HMENU)2731, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_search_spawn_ = CreateWindowW(L"BUTTON", L"Search/Spawn", WS_CHILD | WS_VISIBLE,
                                            488, 5, 96, 24,
                                            hwnd_tb_node_, (HMENU)2732, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_graph_ = CreateWindowW(L"LISTBOX", L"",
                                     WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                                     LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                                     10, TB_H + 10, panel_w - 20, 210,
                                     hwnd_rdock_node_, (HMENU)2713, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_results_ = CreateWindowW(L"LISTBOX", L"",
                                       WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                                       LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                                       10, TB_H + 10, panel_w - 20, 146,
                                       hwnd_rdock_node_, (HMENU)2733, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_export_preview_ = CreateWindowW(L"BUTTON", L"Preview Export", WS_CHILD | WS_VISIBLE,
                                              10, TB_H + 160, 120, 28,
                                              hwnd_rdock_node_, (HMENU)2734, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_connect_selected_ = CreateWindowW(L"BUTTON", L"Connect Selected", WS_CHILD | WS_VISIBLE,
                                                10, TB_H + 192, 120, 28,
                                                hwnd_rdock_node_, (HMENU)2735, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_disconnect_selected_ = CreateWindowW(L"BUTTON", L"Disconnect Selected", WS_CHILD | WS_VISIBLE,
                                                   136, TB_H + 192, 120, 28,
                                                   hwnd_rdock_node_, (HMENU)2736, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_rc_band_ = CreateWindowW(TRACKBAR_CLASSW, L"",
                                       WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                       10, TB_H + 228, panel_w - 120, 30,
                                       hwnd_rdock_node_, (HMENU)2719, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_rc_drive_ = CreateWindowW(TRACKBAR_CLASSW, L"",
                                        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                        10, TB_H + 262, panel_w - 120, 30,
                                        hwnd_rdock_node_, (HMENU)2720, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_rc_apply_ = CreateWindowW(L"BUTTON", L"RC Apply", WS_CHILD | WS_VISIBLE,
                                        panel_w - 100, TB_H + 228, 90, 64,
                                        hwnd_rdock_node_, (HMENU)2718, GetModuleHandleW(nullptr), nullptr);
    hwnd_node_summary_ = CreateWindowW(L"EDIT", L"",
                                       WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
                                       ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                       10, TB_H + 298, panel_w - 20, panel_h - (TB_H + 308),
                                       hwnd_rdock_node_, (HMENU)2711, GetModuleHandleW(nullptr), nullptr);
    if (hwnd_node_graph_) SendMessageW(hwnd_node_graph_, LB_SETITEMHEIGHT, 0, 32);
    if (hwnd_node_rc_band_) {
        SendMessageW(hwnd_node_rc_band_, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(-4, 8));
        SendMessageW(hwnd_node_rc_band_, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)0);
    }
    if (hwnd_node_rc_drive_) {
        SendMessageW(hwnd_node_rc_drive_, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(0, 100));
        SendMessageW(hwnd_node_rc_drive_, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)48);
    }

    // Sequencer toolbar + timeline workspace. Editor-only, derived/advisory.
    HWND h_seq = CreateWindowW(L"STATIC", L"Sequencer", WS_CHILD | WS_VISIBLE,
                               10, 8, 140, 18,
                               hwnd_tb_sequencer_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (h_seq && g_font_ui_bold) SendMessageW(h_seq, WM_SETFONT, (WPARAM)g_font_ui_bold, TRUE);
    CreateWindowW(L"BUTTON", L"Lock", WS_CHILD | WS_VISIBLE,
                  panel_w - 180, 5, 54, 24,
                  hwnd_tb_sequencer_, (HMENU)1535, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
                  panel_w - 240, 5, 54, 24,
                  hwnd_tb_sequencer_, (HMENU)1545, GetModuleHandleW(nullptr), nullptr);
    hwnd_seq_play_ = CreateWindowW(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE,
                                   10, 5, 64, 24,
                                   hwnd_tb_sequencer_, (HMENU)2722, GetModuleHandleW(nullptr), nullptr);
    hwnd_seq_loop_ = CreateWindowW(L"BUTTON", L"Loop Build", WS_CHILD | WS_VISIBLE,
                                   80, 5, 82, 24,
                                   hwnd_tb_sequencer_, (HMENU)2723, GetModuleHandleW(nullptr), nullptr);
    hwnd_seq_stress_overlay_ = CreateWindowW(L"BUTTON", L"Stress Overlay", WS_CHILD | WS_VISIBLE,
                                   168, 5, 108, 24,
                                   hwnd_tb_sequencer_, (HMENU)2725, GetModuleHandleW(nullptr), nullptr);
    hwnd_seq_timeline_ = CreateWindowW(L"LISTBOX", L"",
                                       WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                                       LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                                       10, TB_H + 10, panel_w - 20, 180,
                                       hwnd_rdock_sequencer_, (HMENU)2721, GetModuleHandleW(nullptr), nullptr);
    hwnd_seq_add_key_ = CreateWindowW(L"BUTTON", L"Add Key", WS_CHILD | WS_VISIBLE,
                                      10, TB_H + 198, 84, 28,
                                      hwnd_rdock_sequencer_, (HMENU)2724, GetModuleHandleW(nullptr), nullptr);
    hwnd_seq_motion_match_ = CreateWindowW(L"BUTTON", L"Motion Hook", WS_CHILD | WS_VISIBLE,
                                           102, TB_H + 198, 110, 28,
                                           hwnd_rdock_sequencer_, (HMENU)2726, GetModuleHandleW(nullptr), nullptr);
    hwnd_seq_summary_ = CreateWindowW(L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
                                      ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                      10, TB_H + 234, panel_w - 20, panel_h - (TB_H + 244),
                                      hwnd_rdock_sequencer_, (HMENU)2727, GetModuleHandleW(nullptr), nullptr);

    rdock_tab_index_u32_ = 0u;
    TabCtrl_SetCurSel(hwnd_rdock_tab_, (int)rdock_tab_index_u32_);
    ApplyRightDockVisibility();
    RefreshNodePanel();
    RefreshAssetDesignerPanel();
    RefreshVoxelDesignerPanel();
    RefreshSequencerPanel();

    // Bottom-docked content browser (v1: list view). Toggle via Window menu.
    hwnd_content_ = CreateWindowW(L"STATIC", L"",
                                  WS_CHILD | (content_visible_ ? WS_VISIBLE : 0) | WS_BORDER,
                                  0, client_h_ - 260, client_w_, 260,
                                  hwnd_main_, (HMENU)1010, GetModuleHandleW(nullptr), nullptr);
    hwnd_content_search_ = CreateWindowW(L"EDIT", L"",
                                         WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
                                         10, 10, 260, 24,
                                         hwnd_content_, (HMENU)1011, GetModuleHandleW(nullptr), nullptr);
    hwnd_content_refresh_ = CreateWindowW(L"BUTTON", L"Refresh",
                                          WS_CHILD | WS_VISIBLE,
                                          278, 10, 80, 24,
                                          hwnd_content_, (HMENU)1012, GetModuleHandleW(nullptr), nullptr);

    // Content view toggles (v2): List / Thumbs / 3D.
    hwnd_content_view_list_ = CreateWindowW(L"BUTTON", L"List",
                                           WS_CHILD | WS_VISIBLE,
                                           366, 10, 60, 24,
                                           hwnd_content_, (HMENU)1014, GetModuleHandleW(nullptr), nullptr);
    hwnd_content_view_thumb_ = CreateWindowW(L"BUTTON", L"Thumb",
                                            WS_CHILD | WS_VISIBLE,
                                            430, 10, 70, 24,
                                            hwnd_content_, (HMENU)1015, GetModuleHandleW(nullptr), nullptr);
    hwnd_content_view_3d_ = CreateWindowW(L"BUTTON", L"3D",
                                         WS_CHILD | WS_VISIBLE,
                                         504, 10, 46, 24,
                                         hwnd_content_, (HMENU)1016, GetModuleHandleW(nullptr), nullptr);
    hwnd_content_refcheck_ = CreateWindowW(L"BUTTON", L"Refs",
                                           WS_CHILD | WS_VISIBLE,
                                           556, 10, 54, 24,
                                           hwnd_content_, (HMENU)1019, GetModuleHandleW(nullptr), nullptr);
    hwnd_content_status_ = CreateWindowW(L"STATIC", L"List mode.",
                                         WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         620, 12, 320, 18,
                                         hwnd_content_, (HMENU)1020, GetModuleHandleW(nullptr), nullptr);
    hwnd_content_selected_ = CreateWindowW(L"STATIC", L"Selected: none",
                                           WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           10, 228, client_w_ - 20, 18,
                                           hwnd_content_, (HMENU)1021, GetModuleHandleW(nullptr), nullptr);
    // Thumbnail view list (v1). Uses icon mode with a deterministic placeholder image list.
    hwnd_content_thumb_ = CreateWindowW(WC_LISTVIEWW, L"",
                                        WS_CHILD | WS_BORDER | LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                        10, 40, client_w_ - 20, 210,
                                        hwnd_content_, (HMENU)1017, GetModuleHandleW(nullptr), nullptr);
    if (hwnd_content_thumb_) ListView_SetExtendedListViewStyle(hwnd_content_thumb_, LVS_EX_DOUBLEBUFFER);

    // Deterministic placeholder thumbnail image list (solid gold tile).
    himl_content_thumbs_ = ImageList_Create(64, 64, ILC_COLOR32, 1, 0);
    if (himl_content_thumbs_) {
        HDC hdc = GetDC(hwnd_content_thumb_);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, 64, 64);
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH br = CreateSolidBrush(RGB(160, 120, 0));
        RECT r{0,0,64,64};
        FillRect(mem, &r, br);
        DeleteObject(br);
        SelectObject(mem, old);
        DeleteDC(mem);
        ReleaseDC(hwnd_content_thumb_, hdc);
        ImageList_Add(himl_content_thumbs_, bmp, nullptr);
        DeleteObject(bmp);
        ListView_SetImageList(hwnd_content_thumb_, himl_content_thumbs_, LVSIL_NORMAL);
    }
    ShowWindow(hwnd_content_thumb_, SW_HIDE);
    // Deterministic small icon image list for list mode (solid gold tile).
    himl_content_icons_ = ImageList_Create(16, 16, ILC_COLOR32, 1, 0);
    if (himl_content_icons_) {
        HDC hdc = GetDC(hwnd_content_list_);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, 16, 16);
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH br = CreateSolidBrush(RGB(160, 120, 0));
        RECT r{0,0,16,16};
        FillRect(mem, &r, br);
        DeleteObject(br);
        SelectObject(mem, old);
        DeleteDC(mem);
        ReleaseDC(hwnd_content_list_, hdc);
        ImageList_Add(himl_content_icons_, bmp, nullptr);
        DeleteObject(bmp);
        ListView_SetImageList(hwnd_content_list_, himl_content_icons_, LVSIL_SMALL);
    }


        hwnd_content_3d_ = CreateWindowW(L"LISTBOX", L"",
                                     WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                                     10, 40, client_w_ - 20, 182,
                                     hwnd_content_, (HMENU)1018, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(hwnd_content_3d_, SW_HIDE);
    ListView_SetExtendedListViewStyle(hwnd_content_list_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 560;
        col.pszText = (LPWSTR)L"Path";
        ListView_InsertColumn(hwnd_content_list_, 0, &col);
        col.cx = 240;
        col.pszText = (LPWSTR)L"Label";
        ListView_InsertColumn(hwnd_content_list_, 1, &col);
    }

    // Controls in panel
    HWND asset_parent = hwnd_rdock_asset_;
    HWND outliner_parent = hwnd_rdock_outliner_;
    HWND details_parent = hwnd_rdock_details_;
    HWND voxel_parent = hwnd_rdock_voxel_;
    hwnd_input_ = CreateWindowW(L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOVSCROLL,
                                10, 10, 400, 24,
                                asset_parent, (HMENU)2001, GetModuleHandleW(nullptr), nullptr);

    hwnd_send_ = CreateWindowW(L"BUTTON", L"Send",
                               WS_CHILD | WS_VISIBLE,
                               10, 40, 80, 26,
                               asset_parent, (HMENU)2002, GetModuleHandleW(nullptr), nullptr);

    hwnd_import_ = CreateWindowW(L"BUTTON", L"Import OBJ",
                                 WS_CHILD | WS_VISIBLE,
                                 100, 40, 110, 26,
                                 asset_parent, (HMENU)2003, GetModuleHandleW(nullptr), nullptr);

    hwnd_bootstrap_ = CreateWindowW(L"BUTTON", L"Bootstrap Engine",
                               WS_CHILD | WS_VISIBLE,
                               220, 40, 190, 26,
                               asset_parent, (HMENU)2006, GetModuleHandleW(nullptr), nullptr);

    // ------------------------------------------------------------
    // AI + sim toggles (owner-drawn switches)
    // ------------------------------------------------------------
    CreateWindowW(L"STATIC", L"Run", WS_CHILD | WS_VISIBLE, 10, 70, 28, 18, asset_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_toggle_play_ = CreateWindowW(L"BUTTON", L"",
                                      WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                      42, 66, 86, 24,
                                      asset_parent, (HMENU)2040, GetModuleHandleW(nullptr), nullptr);

    CreateWindowW(L"STATIC", L"AI", WS_CHILD | WS_VISIBLE, 138, 70, 18, 18, asset_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_toggle_ai_ = CreateWindowW(L"BUTTON", L"",
                                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    162, 66, 86, 24,
                                    asset_parent, (HMENU)2041, GetModuleHandleW(nullptr), nullptr);

    // Second row: learning + crawling.
    CreateWindowW(L"STATIC", L"Learn", WS_CHILD | WS_VISIBLE, 10, 96, 34, 18, asset_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_toggle_learning_ = CreateWindowW(L"BUTTON", L"",
                                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                          52, 92, 86, 24,
                                          asset_parent, (HMENU)2042, GetModuleHandleW(nullptr), nullptr);

    CreateWindowW(L"STATIC", L"Crawl", WS_CHILD | WS_VISIBLE, 148, 96, 34, 18, asset_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_toggle_crawling_ = CreateWindowW(L"BUTTON", L"",
                                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                          186, 92, 86, 24,
                                          asset_parent, (HMENU)2043, GetModuleHandleW(nullptr), nullptr);


    // AI vault browser entry point (read-only): snapshots deterministic vault entries into the dockable AI panel.
    hwnd_vault_ = CreateWindowW(L"BUTTON", L"Vault",
                                WS_CHILD | WS_VISIBLE,
                                280, 92, 60, 24,
                                asset_parent, (HMENU)2045, GetModuleHandleW(nullptr), nullptr);


    // AI details panel (crawler progress + experiments)
    CreateWindowW(L"BUTTON", L"AI Panel",
                  WS_CHILD | WS_VISIBLE,
                  346, 92, 64, 24,
                  asset_parent, (HMENU)2046, GetModuleHandleW(nullptr), nullptr);

    hwnd_ai_status_ = CreateWindowW(L"STATIC", L"AI:IDLE",
                                    WS_CHILD | WS_VISIBLE,
                                    10, 120, 400, 18,
                                    asset_parent, (HMENU)2044, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_selected_ = CreateWindowW(L"STATIC", L"Selected Object: -",
                                         WS_CHILD | WS_VISIBLE,
                                         10, 146, 400, 18,
                                         asset_parent, (HMENU)2063, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_gate_ = CreateWindowW(L"STATIC", L"Training Gate: no scene selection.",
                                     WS_CHILD | WS_VISIBLE,
                                     10, 168, 400, 18,
                                     asset_parent, (HMENU)2064, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_builder_status_ = CreateWindowW(L"STATIC", L"Builder: waiting for selection or imported content.",
                                               WS_CHILD | WS_VISIBLE,
                                               10, 190, 400, 18,
                                               asset_parent, (HMENU)2065, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_review_refs_ = CreateWindowW(L"BUTTON", L"Review Refs",
                                            WS_CHILD | WS_VISIBLE,
                                            10, 214, 104, 24,
                                            asset_parent, (HMENU)2061, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_tool_mode_ = CreateWindowW(WC_COMBOBOXW, L"",
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST,
                                          122, 214, 156, 240,
                                          asset_parent, (HMENU)2066, GetModuleHandleW(nullptr), nullptr);
    if (hwnd_asset_tool_mode_) {
        SendMessageW(hwnd_asset_tool_mode_, CB_ADDSTRING, 0, (LPARAM)L"Asset Builder");
        SendMessageW(hwnd_asset_tool_mode_, CB_ADDSTRING, 0, (LPARAM)L"Planet Builder");
        SendMessageW(hwnd_asset_tool_mode_, CB_ADDSTRING, 0, (LPARAM)L"Character Tools");
        SendMessageW(hwnd_asset_tool_mode_, CB_SETCURSEL, 0, 0);
    }
    hwnd_asset_planet_atmo_ = CreateWindowW(TRACKBAR_CLASSW, L"",
                                            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                            10, 246, 260, 30,
                                            asset_parent, (HMENU)2067, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_planet_iono_ = CreateWindowW(TRACKBAR_CLASSW, L"",
                                            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                            10, 278, 260, 30,
                                            asset_parent, (HMENU)2068, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_planet_magneto_ = CreateWindowW(TRACKBAR_CLASSW, L"",
                                               WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                               10, 310, 260, 30,
                                               asset_parent, (HMENU)2069, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_planet_apply_ = CreateWindowW(L"BUTTON", L"Planet Apply",
                                             WS_CHILD | WS_VISIBLE,
                                             278, 246, 112, 24,
                                             asset_parent, (HMENU)2070, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_planet_sculpt_ = CreateWindowW(L"BUTTON", L"Sculpt Hook",
                                              WS_CHILD | WS_VISIBLE,
                                              278, 276, 112, 24,
                                              asset_parent, (HMENU)2071, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_planet_paint_ = CreateWindowW(L"BUTTON", L"Paint Hook",
                                             WS_CHILD | WS_VISIBLE,
                                             278, 306, 112, 24,
                                             asset_parent, (HMENU)2072, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_character_archetype_ = CreateWindowW(WC_COMBOBOXW, L"",
                                                    WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST,
                                                    10, 344, 168, 240,
                                                    asset_parent, (HMENU)2073, GetModuleHandleW(nullptr), nullptr);
    if (hwnd_asset_character_archetype_) {
        SendMessageW(hwnd_asset_character_archetype_, CB_ADDSTRING, 0, (LPARAM)L"Biped Explorer");
        SendMessageW(hwnd_asset_character_archetype_, CB_ADDSTRING, 0, (LPARAM)L"Quadruped Runner");
        SendMessageW(hwnd_asset_character_archetype_, CB_ADDSTRING, 0, (LPARAM)L"Winged Surveyor");
        SendMessageW(hwnd_asset_character_archetype_, CB_ADDSTRING, 0, (LPARAM)L"Heavy Rig Worker");
        SendMessageW(hwnd_asset_character_archetype_, CB_SETCURSEL, 0, 0);
    }
    hwnd_asset_character_height_ = CreateWindowW(TRACKBAR_CLASSW, L"",
                                                 WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                                 10, 376, 260, 30,
                                                 asset_parent, (HMENU)2074, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_character_rigidity_ = CreateWindowW(TRACKBAR_CLASSW, L"",
                                                   WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                                   10, 408, 260, 30,
                                                   asset_parent, (HMENU)2075, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_character_gait_ = CreateWindowW(TRACKBAR_CLASSW, L"",
                                               WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                               10, 440, 260, 30,
                                               asset_parent, (HMENU)2076, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_character_bind_ = CreateWindowW(L"BUTTON", L"Bind Rig",
                                               WS_CHILD | WS_VISIBLE,
                                               278, 376, 112, 24,
                                               asset_parent, (HMENU)2077, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_character_pose_ = CreateWindowW(L"BUTTON", L"Pose Hook",
                                               WS_CHILD | WS_VISIBLE,
                                               278, 406, 112, 24,
                                               asset_parent, (HMENU)2078, GetModuleHandleW(nullptr), nullptr);
    hwnd_asset_tool_summary_ = CreateWindowW(L"EDIT", L"",
                                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                                             10, 474, 380, 110,
                                             asset_parent, (HMENU)2079, GetModuleHandleW(nullptr), nullptr);
    auto setup_tb = [&](HWND h, int lo, int hi, int pos) {
        if (!h) return;
        SendMessageW(h, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(lo, hi));
        SendMessageW(h, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)pos);
    };
    setup_tb(hwnd_asset_planet_atmo_, 0, 100, 18);
    setup_tb(hwnd_asset_planet_iono_, 0, 100, 12);
    setup_tb(hwnd_asset_planet_magneto_, 0, 100, 26);
    setup_tb(hwnd_asset_character_height_, 80, 260, 172);
    setup_tb(hwnd_asset_character_rigidity_, 0, 100, 54);
    setup_tb(hwnd_asset_character_gait_, 0, 100, 48);

    // Object list (Outliner). Parented to Outliner tab root so it behaves like a real docked panel.
    hwnd_objlist_ = CreateWindowW(L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                                  10, 44, 400, 114,
                                  outliner_parent, (HMENU)2004, GetModuleHandleW(nullptr), nullptr);
    // Details: property grid-style inspector (Unreal-like).
    {
        const int X = 10;
        const int Y = 44;
        const int W = 400 - 20;
        const int H = client_h_ - (Y + 10);

        /* Hidden backing fields for existing handlers (plumbing only). */
        hwnd_posx_ = CreateWindowW(L"EDIT", L"0", WS_CHILD, 0, 0, 0, 0, details_parent, (HMENU)2010, GetModuleHandleW(nullptr), nullptr);
        hwnd_posy_ = CreateWindowW(L"EDIT", L"0", WS_CHILD, 0, 0, 0, 0, details_parent, (HMENU)2011, GetModuleHandleW(nullptr), nullptr);
        hwnd_posz_ = CreateWindowW(L"EDIT", L"0", WS_CHILD, 0, 0, 0, 0, details_parent, (HMENU)2012, GetModuleHandleW(nullptr), nullptr);
        hwnd_grid_step_ = CreateWindowW(L"EDIT", L"1.0", WS_CHILD, 0, 0, 0, 0, details_parent, (HMENU)2024, GetModuleHandleW(nullptr), nullptr);
        hwnd_angle_step_ = CreateWindowW(L"EDIT", L"15", WS_CHILD, 0, 0, 0, 0, details_parent, (HMENU)2025, GetModuleHandleW(nullptr), nullptr);

        hwnd_propgrid_ = CreateWindowW(WC_LISTVIEWW, L"",
                                       WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                       X, Y, W, H - 12,
                                       details_parent, (HMENU)2600, GetModuleHandleW(nullptr), nullptr);
        ListView_SetExtendedListViewStyle(hwnd_propgrid_,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);

        /* Columns: Name | Value */
        {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.cx = 170;
            col.pszText = (LPWSTR)L"Property";
            ListView_InsertColumn(hwnd_propgrid_, 0, &col);
            col.cx = W - 190;
            col.pszText = (LPWSTR)L"Value";
            ListView_InsertColumn(hwnd_propgrid_, 1, &col);
        }

        /* Groups (collapsible behavior later; groups now give Unreal-like structure). */
        if (ListView_IsGroupViewEnabled(hwnd_propgrid_) == FALSE) {
            ListView_EnableGroupView(hwnd_propgrid_, TRUE);
        }
        {
            LVGROUP g{};
            g.cbSize = sizeof(g);
            g.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
            g.stateMask = LVGS_COLLAPSIBLE;

            g.pszHeader = (LPWSTR)L"Transform"; g.iGroupId = 1; ListView_InsertGroup(hwnd_propgrid_, -1, &g);
            g.pszHeader = (LPWSTR)L"Snapping";  g.iGroupId = 2; ListView_InsertGroup(hwnd_propgrid_, -1, &g);
            g.pszHeader = (LPWSTR)L"Gizmo";     g.iGroupId = 3; ListView_InsertGroup(hwnd_propgrid_, -1, &g);
            g.pszHeader = (LPWSTR)L"History";   g.iGroupId = 4; ListView_InsertGroup(hwnd_propgrid_, -1, &g);
        }

        RebuildPropertyGrid();
        details_controls_h_ = Y + 8;
    }


    hwnd_output_ = CreateWindowW(L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                                 10, 150, 400, panel_h - 160,
                                 asset_parent, (HMENU)2005, GetModuleHandleW(nullptr), nullptr);

    // Voxel Designer: atom-simulation authoring lane using a node list + resonance summary.
    {
        const int X = 10, Y = 44, W = 380;

        CreateWindowW(L"STATIC", L"Material Presets", WS_CHILD | WS_VISIBLE,
                      X, Y, 160, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        hwnd_voxel_viewport_resonance_ = CreateWindowW(L"BUTTON", L"Viewport Resonance",
                                                       WS_CHILD | WS_VISIBLE,
                                                       X + W - 150, Y - 2, 150, 24,
                                                       voxel_parent, (HMENU)2614, GetModuleHandleW(nullptr), nullptr);
        hwnd_voxel_presets_list_ = CreateWindowW(L"LISTBOX", L"",
                                                 WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                                                 X, Y + 22, W, 94,
                                                 voxel_parent, (HMENU)2612, GetModuleHandleW(nullptr), nullptr);
        if (hwnd_voxel_presets_list_) {
            const wchar_t* presets[] = {
                L"Steel", L"Stainless Steel", L"Aluminum", L"Titanium", L"Copper", L"Brass", L"Bronze",
                L"Gold", L"Silver", L"Concrete", L"Asphalt", L"Granite", L"Marble",
                L"Oak Wood", L"Pine Wood", L"Walnut Wood", L"Plywood",
                L"ABS Plastic", L"Polycarbonate", L"Rubber", L"Glass", L"Leather", L"Fabric"
            };
            for (const wchar_t* p : presets) SendMessageW(hwnd_voxel_presets_list_, LB_ADDSTRING, 0, (LPARAM)p);
            SendMessageW(hwnd_voxel_presets_list_, LB_SETCURSEL, 0, 0);
        }

        int y = Y + 22 + 94 + 12;
        CreateWindowW(L"STATIC", L"Density", WS_CHILD | WS_VISIBLE, X, y + 4, 60, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND h_dens = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                      X + 70, y, W - 80, 30, voxel_parent, (HMENU)2601, GetModuleHandleW(nullptr), nullptr);
        if (h_dens) { SendMessageW(h_dens, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(0, 100)); SendMessageW(h_dens, TBM_SETPOS, (WPARAM)TRUE, 50); }
        y += 34;
        CreateWindowW(L"STATIC", L"Hardness", WS_CHILD | WS_VISIBLE, X, y + 4, 60, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND h_hard = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                      X + 70, y, W - 80, 30, voxel_parent, (HMENU)2602, GetModuleHandleW(nullptr), nullptr);
        if (h_hard) { SendMessageW(h_hard, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(0, 100)); SendMessageW(h_hard, TBM_SETPOS, (WPARAM)TRUE, 50); }
        y += 34;
        CreateWindowW(L"STATIC", L"Roughness", WS_CHILD | WS_VISIBLE, X, y + 4, 60, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND h_rough = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                      X + 70, y, W - 80, 30, voxel_parent, (HMENU)2603, GetModuleHandleW(nullptr), nullptr);
        if (h_rough) { SendMessageW(h_rough, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(0, 100)); SendMessageW(h_rough, TBM_SETPOS, (WPARAM)TRUE, 50); }
        y += 40;

        CreateWindowW(L"STATIC", L"Atom Node Editor", WS_CHILD | WS_VISIBLE,
                      X, y, 140, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        hwnd_voxel_atom_nodes_ = CreateWindowW(L"LISTBOX", L"",
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                                               X, y + 22, W, 86,
                                               voxel_parent, (HMENU)2615, GetModuleHandleW(nullptr), nullptr);
        y += 116;
        hwnd_voxel_summary_ = CreateWindowW(L"EDIT", L"",
                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                      X, y, W, 108, voxel_parent, (HMENU)2616, GetModuleHandleW(nullptr), nullptr);
    }

    // Apply consistent editor fonts (Segoe UI) across UI controls.
    ew_apply_editor_fonts(hwnd_panel_);
    if (hwnd_content_) ew_apply_editor_fonts(hwnd_content_);
    LayoutChildren(client_w_, client_h_);
    RefreshAssetDesignerPanel();
    RefreshVoxelDesignerPanel();

}


// -----------------------------------------------------------------------------
// AI Panel Window (Chat-first, mobile-ChatGPT-like)
// - Separate window so editor controls remain uncluttered.
// - Always boots with Learning/Crawling OFF.
// - No automatic disk projection for AI/crawler artifacts.
// - Multi-chat tabs: each tab is a bounded chat session.
// -----------------------------------------------------------------------------

static void ew_draw_bell_badge(const DRAWITEMSTRUCT* dis, uint32_t badge_u32) {
    if (!dis) return;
    ew_theme_init_once();
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    FillRect(hdc, &rc, g_brush_panel);

    // Simple bell glyph (text) + red badge.
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, g_theme.gold);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old = (HFONT)SelectObject(hdc, hFont);

    // Simple bell glyph. We intentionally keep this ASCII-ish so it renders on
    // default Win32 fonts without requiring a symbol font.
    const wchar_t* bell = L"!";
    RECT tr = rc;
    DrawTextW(hdc, bell, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (badge_u32 > 0u) {
        // Draw red dot with number.
        const int dot_r = 10;
        int cx = rc.right - dot_r - 2;
        int cy = rc.top + dot_r + 2;
        HBRUSH red = CreateSolidBrush(RGB(220, 0, 0));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(220,0,0));
        HGDIOBJ oldb = SelectObject(hdc, red);
        HGDIOBJ oldp = SelectObject(hdc, pen);
        Ellipse(hdc, cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r);
        SelectObject(hdc, oldp);
        SelectObject(hdc, oldb);
        DeleteObject(pen);
        DeleteObject(red);

        wchar_t num[16];
        uint32_t shown = badge_u32;
        if (shown > 963u) shown = 963u;
        _snwprintf(num, 16, L"%u", (unsigned)shown);
        SetTextColor(hdc, RGB(255,255,255));
        RECT nr{cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r};
        DrawTextW(hdc, num, -1, &nr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hdc, g_theme.gold);
    }

    SelectObject(hdc, old);
}

static void ew_draw_compose_icon(const DRAWITEMSTRUCT* dis) {
    if (!dis) return;
    ew_theme_init_once();
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    // Background
    FillRect(hdc, &rc, g_brush_panel);

    // Slight inset for stroke
    RECT r = rc;
    InflateRect(&r, -6, -6);

    // Pen+pad icon: a square "page" with a clipped top-right corner and a pencil stroke.
    HPEN pen = CreatePen(PS_SOLID, 2, g_theme.gold);
    HGDIOBJ oldp = SelectObject(hdc, pen);

    // Page outline with cut corner
    const int cut = 6;
    MoveToEx(hdc, r.left, r.top, nullptr);
    LineTo(hdc, r.right - cut, r.top);
    LineTo(hdc, r.right, r.top + cut);
    LineTo(hdc, r.right, r.bottom);
    LineTo(hdc, r.left, r.bottom);
    LineTo(hdc, r.left, r.top);

    // Pencil: diagonal stroke across lower-right
    int px1 = r.left + (r.right - r.left) / 3;
    int py1 = r.top + (r.bottom - r.top) * 2 / 3;
    int px2 = r.right - 2;
    int py2 = r.top + 2;
    MoveToEx(hdc, px1, py1, nullptr);
    LineTo(hdc, px2, py2);

    // Small "tip" accent
    MoveToEx(hdc, px2 - 4, py2 + 6, nullptr);
    LineTo(hdc, px2, py2 + 2);

    SelectObject(hdc, oldp);
    DeleteObject(pen);

    // Hover/pressed feedback (subtle gold border)
    if (dis->itemState & (ODS_SELECTED | ODS_FOCUS | ODS_HOTLIGHT)) {
        HPEN p2 = CreatePen(PS_SOLID, 1, g_theme.gold);
        HGDIOBJ op2 = SelectObject(hdc, p2);
        Rectangle(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
        SelectObject(hdc, op2);
        DeleteObject(p2);
    }
}

static void ew_draw_apply_button(const DRAWITEMSTRUCT* dis) {
    if (!dis) return;
    HDC dc = dis->hDC;
    RECT r = dis->rcItem;

    // Background: near-black.
    HBRUSH bg = CreateSolidBrush(RGB(12, 12, 12));
    FillRect(dc, &r, bg);
    DeleteObject(bg);

    const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
    const bool down = (dis->itemState & ODS_SELECTED) != 0;
    const COLORREF gold = hot ? RGB(255, 210, 90) : RGB(220, 180, 60);

    // Border.
    HPEN pen = CreatePen(PS_SOLID, 1, gold);
    HGDIOBJ oldp = SelectObject(dc, pen);
    HGDIOBJ oldb = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, oldb);
    SelectObject(dc, oldp);
    DeleteObject(pen);

    // Text.
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT tr = r;
    if (down) { tr.left += 1; tr.top += 1; }
    DrawTextW(dc, L"Apply", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void ew_draw_menu_ellipsis_button(const DRAWITEMSTRUCT* dis) {
    ew_theme_init_once();
    if (!dis) return;
    HDC dc = dis->hDC;
    RECT r = dis->rcItem;

    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
    const COLORREF gold = hot ? RGB(255, 210, 90) : RGB(220, 180, 60);

    // Background.
    HBRUSH bg = CreateSolidBrush(g_theme.panel_bg);
    FillRect(dc, &r, bg);
    DeleteObject(bg);

    // Border.
    HPEN pen = CreatePen(PS_SOLID, 1, gold);
    HGDIOBJ oldp = SelectObject(dc, pen);
    HGDIOBJ oldb = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, oldb);
    SelectObject(dc, oldp);
    DeleteObject(pen);

    // Three dots.
    const int cy = (r.top + r.bottom) / 2 + (pressed ? 1 : 0);
    const int cx = (r.left + r.right) / 2 + (pressed ? 1 : 0);
    const int spacing = 6;
    const int rad = 2;
    HBRUSH dot = CreateSolidBrush(gold);
    HGDIOBJ old_br = SelectObject(dc, dot);
    for (int i = -1; i <= 1; ++i) {
        const int x = cx + i * spacing;
        Ellipse(dc, x - rad, cy - rad, x + rad + 1, cy + rad + 1);
    }
    SelectObject(dc, old_br);
    DeleteObject(dot);
}

static bool ew_ai_chat_is_user_line(const std::wstring& s) {
    return ew_starts_with(s, L"You:");
}

static int ew_ai_chat_measure_item_height(HWND hwnd_list, const std::wstring& text, int width_px) {
    if (!hwnd_list) return 22;
    HDC dc = GetDC(hwnd_list);
    if (!dc) return 22;
    HFONT hf = (HFONT)SendMessageW(hwnd_list, WM_GETFONT, 0, 0);
    HGDIOBJ oldf = hf ? SelectObject(dc, hf) : nullptr;

    const int bubble_pad_x = 10;
    const int bubble_pad_y = 6;
    const int outer_pad_x = 10;
    const int max_bubble_w = (width_px > 0) ? (width_px * 80 / 100) : 320;
    const int text_w = (max_bubble_w - 2 * bubble_pad_x);

    RECT tr{};
    tr.left = 0;
    tr.top = 0;
    tr.right = (text_w > 64) ? text_w : 64;
    tr.bottom = 4096;
    DrawTextW(dc, text.c_str(), (int)text.size(), &tr, DT_WORDBREAK | DT_CALCRECT);
    const int text_h = (tr.bottom - tr.top);

    if (oldf) SelectObject(dc, oldf);
    ReleaseDC(hwnd_list, dc);

    const int bubble_h = text_h + 2 * bubble_pad_y;
    const int h = bubble_h + 8; // vertical breathing room
    (void)outer_pad_x;
    return (h < 26) ? 26 : h;
}

static void ew_ai_chat_draw_bubble(const DRAWITEMSTRUCT* dis) {
    ew_theme_init_once();
    if (!dis) return;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    // Background.
    HBRUSH bg = CreateSolidBrush(g_theme.panel_bg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    wchar_t wbuf[4096];
    wbuf[0] = 0;
    SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)wbuf);
    std::wstring s(wbuf);

    const bool is_user = ew_ai_chat_is_user_line(s);
    // Strip prefix for display.
    if (ew_starts_with(s, L"You:")) {
        s.erase(0, 4);
    } else if (ew_starts_with(s, L"Assistant:")) {
        s.erase(0, 10);
    }
    while (!s.empty() && (s[0] == L' ' || s[0] == L'\t')) s.erase(0, 1);

    const int outer_pad_x = 10;
    const int bubble_pad_x = 10;
    const int bubble_pad_y = 6;
    const int w = (rc.right - rc.left);
    const int max_bubble_w = (w * 80) / 100;
    const int bubble_w = max_bubble_w;

    RECT br = rc;
    br.top += 4;
    br.bottom -= 4;
    br.left += outer_pad_x;
    br.right -= outer_pad_x;

    // Align bubble.
    if (is_user) {
        br.left = rc.right - outer_pad_x - bubble_w;
        br.right = rc.right - outer_pad_x;
    } else {
        br.left = rc.left + outer_pad_x;
        br.right = rc.left + outer_pad_x + bubble_w;
    }

    // Bubble colors.
    const COLORREF bubble_bg = is_user ? RGB(22, 22, 22) : RGB(16, 16, 16);
    const COLORREF stroke = g_theme.gold;

    // Rounded rect.
    HBRUSH bbr = CreateSolidBrush(bubble_bg);
    HPEN pen = CreatePen(PS_SOLID, 1, stroke);
    HGDIOBJ oldb = SelectObject(dc, bbr);
    HGDIOBJ oldp = SelectObject(dc, pen);
    RoundRect(dc, br.left, br.top, br.right, br.bottom, 10, 10);
    SelectObject(dc, oldp);
    SelectObject(dc, oldb);
    DeleteObject(pen);
    DeleteObject(bbr);

    // Text.
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT tr = br;
    tr.left += bubble_pad_x;
    tr.right -= bubble_pad_x;
    tr.top += bubble_pad_y;
    tr.bottom -= bubble_pad_y;
    DrawTextW(dc, s.c_str(), (int)s.size(), &tr, DT_WORDBREAK);

    // Focus ring.
    if (dis->itemState & ODS_FOCUS) {
        HPEN p2 = CreatePen(PS_SOLID, 1, RGB(255, 210, 90));
        HGDIOBJ op2 = SelectObject(dc, p2);
        HGDIOBJ ob2 = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc, br.left - 2, br.top - 2, br.right + 2, br.bottom + 2, 12, 12);
        SelectObject(dc, ob2);
        SelectObject(dc, op2);
        DeleteObject(p2);
    }
}

static void ew_draw_usepatch_button(const DRAWITEMSTRUCT* dis) {
    ew_theme_init_once();
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    COLORREF bg = g_theme.panel_bg;
    COLORREF border = g_theme.gold;
    COLORREF text = g_theme.text;
    if (disabled) {
        // Dim the border/text while keeping background consistent.
        border = RGB(80, 72, 40);
        text = RGB(140, 140, 140);
    } else if (pressed) {
        bg = g_theme.edit_bg;
    }

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(dc, &rc, br);
    DeleteObject(br);

    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldp = SelectObject(dc, pen);
    HGDIOBJ oldb = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, oldb);
    SelectObject(dc, oldp);
    DeleteObject(pen);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    const wchar_t* label = L"Use";
    DrawTextW(dc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static bool ew_starts_with(const std::wstring& s, const wchar_t* prefix) {
    if (!prefix) return false;
    const size_t n = wcslen(prefix);
    if (s.size() < n) return false;
    return _wcsnicmp(s.c_str(), prefix, n) == 0;
}

static std::vector<std::wstring> ew_split_lines_keep(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    cur.reserve(256);
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c == L'\r') continue;
        if (c == L'\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

static std::wstring ew_join_lines(const std::vector<std::wstring>& lines) {
    std::wstring out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) out += L"\n";
    }
    return out;
}


static void ew_draw_action_text_button(const DRAWITEMSTRUCT* dis) {
    ew_theme_init_once();
    if (!dis) return;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
    const COLORREF gold = disabled ? RGB(112, 102, 74) : (hot ? RGB(255, 210, 90) : RGB(220, 180, 60));
    HBRUSH bg = CreateSolidBrush(RGB(14, 14, 14));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, gold);
    HGDIOBJ oldp = SelectObject(dc, pen);
    HGDIOBJ oldb = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, oldb);
    SelectObject(dc, oldp);
    DeleteObject(pen);
    wchar_t txt[96]{};
    GetWindowTextW(dis->hwndItem, txt, 95);
    RECT tr = rc;
    if (pressed) { tr.left += 1; tr.top += 1; }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RGB(148,148,148) : RGB(255,255,255));
    DrawTextW(dc, txt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void ew_draw_ai_mode_button(const DRAWITEMSTRUCT* dis, const wchar_t* label, bool active) {
    if (!dis || !dis->hDC) return;
    ew_theme_init_once();
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    COLORREF fill = active ? RGB(44, 34, 12) : RGB(18, 18, 18);
    COLORREF border = active ? g_theme.gold : RGB(88, 74, 28);
    if (hot && !disabled) fill = active ? RGB(52, 40, 14) : RGB(24, 24, 24);
    if (pressed && !disabled) fill = active ? RGB(60, 46, 18) : RGB(28, 28, 28);
    HBRUSH br = CreateSolidBrush(fill);
    FillRect(dc, &rc, br);
    DeleteObject(br);
    HPEN pen = CreatePen(PS_SOLID, active ? 2 : 1, border);
    HGDIOBJ oldp = SelectObject(dc, pen);
    HGDIOBJ oldb = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, oldb);
    SelectObject(dc, oldp);
    DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RGB(128,128,128) : (active ? g_theme.gold : g_theme.text));
    HFONT oldf = (HFONT)SelectObject(dc, g_font_ui ? g_font_ui : GetStockObject(DEFAULT_GUI_FONT));
    RECT tr = rc;
    DrawTextW(dc, label, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldf);
}

// Patch target extractor + bounded apply report.
// Deterministic: targets are returned in first-seen order as they appear in the diff headers.
// Bounded: caps at EW_PATCH_REPORT_MAX_FILES entries.
static constexpr uint32_t EW_PATCH_REPORT_MAX_FILES = 64u;

struct EwPatchApplyReport {
    uint32_t files_touched_u32 = 0u;
    uint32_t files_rejected_u32 = 0u;
    uint32_t warnings_u32 = 0u;

    // Store relative paths as UTF-16 for UI display.
    std::wstring rel_paths_w[EW_PATCH_REPORT_MAX_FILES];

    void clear() {
        files_touched_u32 = 0u;
        files_rejected_u32 = 0u;
        warnings_u32 = 0u;
        for (uint32_t i = 0u; i < EW_PATCH_REPORT_MAX_FILES; ++i) rel_paths_w[i].clear();
    }

    bool add_path_once(const std::wstring& rel) {
        if (rel.empty()) return false;
        for (uint32_t i = 0u; i < files_touched_u32; ++i) {
            if (_wcsicmp(rel_paths_w[i].c_str(), rel.c_str()) == 0) return false;
        }
        if (files_touched_u32 >= EW_PATCH_REPORT_MAX_FILES) { warnings_u32++; return false; }
        rel_paths_w[files_touched_u32++] = rel;
        return true;
    }
};

static void ew_patch_extract_targets(const std::wstring& patch_w, EwPatchApplyReport* rep) {
    if (!rep) return;
    rep->clear();
    auto lines = ew_split_lines_keep(patch_w);
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::wstring& ln = lines[i];
        if (ln.rfind(L"diff --git ", 0) == 0) {
            // Parse "diff --git a/x b/x" and keep the b/ path as the target.
            const size_t bpos = ln.find(L" b/");
            if (bpos == std::wstring::npos) { rep->warnings_u32++; continue; }
            std::wstring rel = ln.substr(bpos + 3);
            // Defensive trim.
            while (!rel.empty() && (rel.back() == L'\r' || rel.back() == L'\n' || rel.back() == L' ' || rel.back() == L'\t')) rel.pop_back();
            rep->add_path_once(rel);
        }
    }
}

// Minimal unified-diff applier (line-based). Deterministic, bounded, fail-closed.
// Supports: diff --git a/x b/x, hunks @@ -l,s +l,s @@ with ' ', '+', '-' lines.
// New files supported when "--- /dev/null" is present.
static bool ew_apply_unified_diff_to_dir(const std::wstring& patch_w, const std::wstring& root_dir_w, std::wstring* out_err_w, EwPatchApplyReport* out_rep) {
    auto lines = ew_split_lines_keep(patch_w);
    size_t i = 0;
    const auto set_err = [&](const std::wstring& e) {
        if (out_err_w) *out_err_w = e;
        if (out_rep) {
            // Fail-closed: treat the entire patch as rejected on first error.
            out_rep->files_rejected_u32 = (out_rep->files_touched_u32 != 0u) ? out_rep->files_touched_u32 : 1u;
        }
        return false;
    };

    // Pre-scan targets for confirmation/reporting (deterministic, bounded).
    if (out_rep) ew_patch_extract_targets(patch_w, out_rep);

    auto load_file = [&](const std::wstring& path_w, std::vector<std::wstring>* out_lines) -> bool {
        if (!out_lines) return false;
        out_lines->clear();
        FILE* f = _wfopen(path_w.c_str(), L"rb");
        if (!f) return false;
        std::string bytes;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) { fclose(f); return false; }
        bytes.resize((size_t)sz);
        if (sz > 0) fread(bytes.data(), 1, (size_t)sz, f);
        fclose(f);
        // Assume UTF-8 or ASCII; validate lightly by best-effort conversion.
        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), nullptr, 0);
        std::wstring ws;
        if (wlen > 0) {
            ws.resize((size_t)wlen);
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), ws.data(), wlen);
        } else {
            // Fallback: treat as ANSI.
            wlen = MultiByteToWideChar(CP_ACP, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
            ws.resize((size_t)wlen);
            MultiByteToWideChar(CP_ACP, 0, bytes.data(), (int)bytes.size(), ws.data(), wlen);
        }
        *out_lines = ew_split_lines_keep(ws);
        return true;
    };

    auto save_file = [&](const std::wstring& path_w, const std::vector<std::wstring>& in_lines) -> bool {
        // Ensure directories.
        std::wstring dir = path_w;
        for (size_t p = dir.size(); p > 0; --p) {
            if (dir[p - 1] == L'/' || dir[p - 1] == L'\\') { dir.resize(p - 1); break; }
        }
        if (!dir.empty()) {
            // mkdir -p style (best effort).
            std::wstring tmp;
            for (size_t k = 0; k < dir.size(); ++k) {
                tmp.push_back(dir[k]);
                if (dir[k] == L'/' || dir[k] == L'\\') {
                    CreateDirectoryW(tmp.c_str(), nullptr);
                }
            }
            CreateDirectoryW(dir.c_str(), nullptr);
        }

        std::wstring ws = ew_join_lines(in_lines);
        int blen = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
        std::string bytes;
        bytes.resize((size_t)blen);
        if (blen > 0) WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), bytes.data(), blen, nullptr, nullptr);
        FILE* f = _wfopen(path_w.c_str(), L"wb");
        if (!f) return false;
        if (!bytes.empty()) fwrite(bytes.data(), 1, bytes.size(), f);
        fclose(f);
        return true;
    };

    while (i < lines.size()) {
        if (lines[i].rfind(L"diff --git ", 0) == 0) {
            // parse target path from "diff --git a/x b/x"
            std::wstring line = lines[i];
            size_t bpos = line.find(L" b/");
            if (bpos == std::wstring::npos) return set_err(L"Malformed diff header.");
            std::wstring rel = line.substr(bpos + 3);
            // advance to --- and +++
            ++i;
            bool new_file = false;
            while (i < lines.size() && lines[i].rfind(L"--- ", 0) != 0) ++i;
            if (i >= lines.size()) return set_err(L"Missing --- line.");
            if (lines[i].find(L"/dev/null") != std::wstring::npos) new_file = true;
            ++i;
            if (i >= lines.size() || lines[i].rfind(L"+++ ", 0) != 0) return set_err(L"Missing +++ line.");
            ++i;

            std::wstring full = root_dir_w;
            if (!full.empty() && full.back() != L'\\' && full.back() != L'/') full += L"\\";
            full += rel;

            std::vector<std::wstring> file_lines;
            if (!new_file) {
                load_file(full, &file_lines); // if missing, treat as empty; hunks will fail unless consistent
            }

            // Apply hunks until next diff or EOF.
            while (i < lines.size() && lines[i].rfind(L"diff --git ", 0) != 0) {
                if (lines[i].rfind(L"@@ ", 0) == 0) {
                    // parse +start
                    size_t plus = lines[i].find(L" +");
                    if (plus == std::wstring::npos) return set_err(L"Malformed hunk header.");
                    size_t comma = lines[i].find(L",", plus);
                    size_t sp = lines[i].find(L" @@", plus);
                    if (sp == std::wstring::npos) return set_err(L"Malformed hunk header.");
                    int start_new = 1;
                    {
                        std::wstring num = (comma != std::wstring::npos && comma < sp)
                                              ? lines[i].substr(plus + 2, comma - (plus + 2))
                                              : lines[i].substr(plus + 2, sp - (plus + 2));
                        start_new = _wtoi(num.c_str());
                        if (start_new < 1) start_new = 1;
                    }
                    ++i;
                    // Convert to 0-based.
                    size_t out_pos = (size_t)(start_new - 1);
                    // Build new file content into a working vector.
                    std::vector<std::wstring> out = file_lines;
                    size_t in_pos = out_pos;

                    // For new files, ensure vector large enough for in_pos
                    if (new_file && out.empty() && in_pos != 0) {
                        // new files should start at 0; fail closed.
                        return set_err(L"New file hunk start not at 1.");
                    }

                    while (i < lines.size()) {
                        if (lines[i].rfind(L"@@ ", 0) == 0 || lines[i].rfind(L"diff --git ", 0) == 0) break;
                        const std::wstring& hl = lines[i];
                        if (hl.empty()) { ++i; continue; }
                        wchar_t tag = hl[0];
                        std::wstring payload = hl.substr(1);

                        if (tag == L' ') {
                            if (in_pos >= out.size() || out[in_pos] != payload) {
                                return set_err(L"Hunk context mismatch.");
                            }
                            ++in_pos;
                        } else if (tag == L'-') {
                            if (in_pos >= out.size() || out[in_pos] != payload) {
                                return set_err(L"Hunk delete mismatch.");
                            }
                            out.erase(out.begin() + (long)in_pos);
                        } else if (tag == L'+') {
                            out.insert(out.begin() + (long)in_pos, payload);
                            ++in_pos;
                        } else if (tag == L'\\') {
                            // "\\ No newline at end of file" -> ignore deterministically.
                        } else {
                            // Unknown tag in hunk.
                            return set_err(L"Unknown hunk line tag.");
                        }
                        ++i;
                    }
                    file_lines.swap(out);
                } else {
                    ++i;
                }
            }

            if (!save_file(full, file_lines)) return set_err(L"Failed to write file during apply.");
            continue;
        }
        ++i;
    }
    return true;
}

void App::CreateAiPanelWindow() {
    if (!ew_editor_build_enabled) return;
    if (hwnd_ai_panel_) return;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    hwnd_ai_panel_ = CreateWindowW(L"STATIC", L"AI",
                                   WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 520, 560,
                                   hwnd_main_, nullptr, GetModuleHandleW(nullptr), nullptr);

    // Route child control theming (WM_CTLCOLOR*) through our editor WndProc.
    // Keep auxiliary windows layout-local (WM_SIZE handler is guarded).
    SetWindowLongPtrW(hwnd_ai_panel_, GWLP_USERDATA, (LONG_PTR)this);
    SetWindowLongPtrW(hwnd_ai_panel_, GWLP_WNDPROC, (LONG_PTR)&App::WndProcThunk);

    // Note: we apply fonts after all controls are created.

    // Compact "⋯" menu in the AI panel header (ChatGPT-mobile style).
    // NOTE: We keep the old learning/crawling toggle controls created but hidden,
    // so any existing code paths remain valid without introducing aliases.
    hwnd_ai_menu_ = CreateWindowW(L"BUTTON", L"⋯",
                                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                  12, 6, 28, 28,
                                  hwnd_ai_panel_, (HMENU)4054, GetModuleHandleW(nullptr), nullptr);

    CreateWindowW(L"STATIC", L"Learning", WS_CHILD, 12, 10, 58, 18,
                  hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_toggle_learning_ = CreateWindowW(L"BUTTON", L"",
                                             WS_CHILD | BS_OWNERDRAW,
                                             74, 6, 56, 24,
                                             hwnd_ai_panel_, (HMENU)4055, GetModuleHandleW(nullptr), nullptr);

    CreateWindowW(L"STATIC", L"Crawling", WS_CHILD, 140, 10, 58, 18,
                  hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_toggle_crawling_ = CreateWindowW(L"BUTTON", L"",
                                             WS_CHILD | BS_OWNERDRAW,
                                             202, 6, 56, 24,
                                             hwnd_ai_panel_, (HMENU)4056, GetModuleHandleW(nullptr), nullptr);

    // Compose (new chat) icon button (pen+pad) in the top-right.
    hwnd_ai_chat_compose_ = CreateWindowW(L"BUTTON", L"",
                                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                          480, 6, 28, 28,
                                          hwnd_ai_panel_, (HMENU)4061, GetModuleHandleW(nullptr), nullptr);

    // Apply button (writes to disk only after explicit approval).
    hwnd_ai_chat_apply_ = CreateWindowW(L"BUTTON", L"Apply",
                                        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        418, 6, 56, 28,
                                        hwnd_ai_panel_, (HMENU)4062, GetModuleHandleW(nullptr), nullptr);

    // Primary next-step action for the active chat, kept in lockstep with the tab workflow surface.
    hwnd_ai_chat_nextaction_ = CreateWindowW(L"BUTTON", L"Next",
                                             WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                             164, 6, 62, 28,
                                             hwnd_ai_panel_, (HMENU)4067, GetModuleHandleW(nullptr), nullptr);

    // Preview patch (read-only) before Apply.
    hwnd_ai_chat_patchview_ = CreateWindowW(L"BUTTON", L"Patch",
                                            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                            232, 6, 56, 28,
                                            hwnd_ai_panel_, (HMENU)4066, GetModuleHandleW(nullptr), nullptr);

    hwnd_ai_chat_preview_ = CreateWindowW(L"BUTTON", L"Preview",
                                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                          294, 6, 56, 28,
                                          hwnd_ai_panel_, (HMENU)4064, GetModuleHandleW(nullptr), nullptr);

    // Use detected patch from last Assistant message (one-click into patch buffer).
    hwnd_ai_chat_usepatch_ = CreateWindowW(L"BUTTON", L"Use",
                                           WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                           356, 6, 56, 28,
                                           hwnd_ai_panel_, (HMENU)4063, GetModuleHandleW(nullptr), nullptr);

    // Embedded AI-panel view selectors (chat / repo / coherence).
    hwnd_ai_view_chat_ = CreateWindowW(L"BUTTON", L"Chat",
                                       WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                       12, 40, 72, 26,
                                       hwnd_ai_panel_, (HMENU)4861, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_view_repo_ = CreateWindowW(L"BUTTON", L"Repository",
                                       WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                       90, 40, 92, 26,
                                       hwnd_ai_panel_, (HMENU)4862, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_view_coherence_ = CreateWindowW(L"BUTTON", L"Coherence",
                                            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                            188, 40, 92, 26,
                                            hwnd_ai_panel_, (HMENU)4863, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_panel_tool_status_ = CreateWindowW(L"STATIC", L"Chat view ready.",
                                               WS_CHILD | WS_VISIBLE,
                                               292, 44, 202, 18,
                                               hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_panel_chat_title_ = CreateWindowW(L"STATIC", L"Chat 1",
                                              WS_CHILD | WS_VISIBLE,
                                              52, 10, 172, 18,
                                              hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_panel_chat_state_ = CreateWindowW(L"STATIC", L"No patch buffered.",
                                              WS_CHILD | WS_VISIBLE,
                                              52, 28, 280, 18,
                                              hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_panel_action_hint_ = CreateWindowW(L"STATIC", L"Next: idle",
                                               WS_CHILD | WS_VISIBLE,
                                               52, 46, 280, 18,
                                               hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_chat_mode_talk_ = CreateWindowW(L"BUTTON", L"Talk",
                                            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                            292, 40, 54, 26,
                                            hwnd_ai_panel_, (HMENU)4898, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_chat_mode_code_ = CreateWindowW(L"BUTTON", L"Code",
                                            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                            352, 40, 54, 26,
                                            hwnd_ai_panel_, (HMENU)4899, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_chat_mode_sim_ = CreateWindowW(L"BUTTON", L"Sim",
                                           WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                           412, 40, 48, 26,
                                           hwnd_ai_panel_, (HMENU)4900, GetModuleHandleW(nullptr), nullptr);

    // Chat tabs.
    hwnd_ai_tab_ = CreateWindowW(WC_TABCONTROLW, L"",
                                 WS_CHILD | WS_VISIBLE,
                                 10, 72, 494, 448,
                                 hwnd_ai_panel_, (HMENU)4051, GetModuleHandleW(nullptr), nullptr);

    // Tab 0: Chat 1 (default).
    TCITEMW ti{};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)L"Chat 1";
    TabCtrl_InsertItem(hwnd_ai_tab_, 0, &ti);

    // Message list (fills tab area).
    // Message list: owner-drawn bubbles.
    hwnd_ai_chat_cortex_ = CreateWindowW(L"STATIC", L"Cortex: idle",
                                         WS_CHILD | WS_VISIBLE,
                                         20, 106, 474, 18,
                                         hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_chat_list_ = CreateWindowW(L"LISTBOX", L"",
                                       WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                                           LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                                       20, 128, 474, 292,
                                       hwnd_ai_panel_, (HMENU)4060, GetModuleHandleW(nullptr), nullptr);

    // Input box + Send + project-link.
    hwnd_ai_chat_link_project_ = CreateWindowW(L"BUTTON", L"+",
                                               WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                               20, 430, 26, 30,
                                               hwnd_ai_panel_, (HMENU)4065, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_chat_input_ = CreateWindowW(L"EDIT", L"",
                                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOVSCROLL | ES_MULTILINE,
                                        52, 430, 342, 78,
                                        hwnd_ai_panel_, (HMENU)4058, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_chat_send_ = CreateWindowW(L"BUTTON", L"Send",
                                       WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                       404, 430, 90, 30,
                                       hwnd_ai_panel_, (HMENU)4059, GetModuleHandleW(nullptr), nullptr);

    // Embedded repository pane (hidden until selected).
    hwnd_ai_repo_status_ = CreateWindowW(L"STATIC", L"Repo reader status: idle",
                                         WS_CHILD,
                                         20, 106, 474, 18,
                                         hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_refresh_ = CreateWindowW(L"BUTTON", L"Refresh",
                                          WS_CHILD | BS_OWNERDRAW,
                                          20, 130, 86, 28,
                                          hwnd_ai_panel_, (HMENU)4864, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_copy_ = CreateWindowW(L"BUTTON", L"Copy Preview",
                                       WS_CHILD | BS_OWNERDRAW,
                                       112, 130, 110, 28,
                                       hwnd_ai_panel_, (HMENU)4865, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_highlight_ = CreateWindowW(L"BUTTON", L"Highlight",
                                            WS_CHILD | BS_OWNERDRAW,
                                            228, 130, 92, 28,
                                            hwnd_ai_panel_, (HMENU)4866, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_label_files_ = CreateWindowW(L"STATIC", L"Repository Files",
                                              WS_CHILD,
                                              20, 166, 180, 18,
                                              hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_label_preview_ = CreateWindowW(L"STATIC", L"Preview",
                                                WS_CHILD,
                                                208, 166, 286, 18,
                                                hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_label_coherence_ = CreateWindowW(L"STATIC", L"Coherence References",
                                                  WS_CHILD,
                                                  208, 370, 286, 18,
                                                  hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_selected_ = CreateWindowW(L"STATIC", L"Selected: -",
                                           WS_CHILD,
                                           20, 510, 300, 18,
                                           hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_copy_path_ = CreateWindowW(L"BUTTON", L"Copy Path",
                                            WS_CHILD | BS_OWNERDRAW,
                                            328, 506, 92, 24,
                                            hwnd_ai_panel_, (HMENU)4886, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                         WS_CHILD | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP,
                                         20, 166, 180, 342,
                                         hwnd_ai_panel_, (HMENU)4867, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_preview_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                            WS_CHILD | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                                            208, 166, 286, 196,
                                            hwnd_ai_panel_, (HMENU)4868, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_repo_coherence_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                              WS_CHILD | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                                              208, 370, 286, 138,
                                              hwnd_ai_panel_, (HMENU)4869, GetModuleHandleW(nullptr), nullptr);

    // Embedded coherence pane (hidden until selected).
    hwnd_ai_coh_stats_ = CreateWindowW(L"STATIC", L"Coherence idle.",
                                       WS_CHILD,
                                       20, 106, 474, 18,
                                       hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_label_query_ = CreateWindowW(L"STATIC", L"Query",
                                             WS_CHILD,
                                             20, 130, 100, 18,
                                             hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_query_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                         WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                         20, 130, 300, 24,
                                         hwnd_ai_panel_, (HMENU)4870, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_label_rename_ = CreateWindowW(L"STATIC", L"Rename Old -> New",
                                              WS_CHILD,
                                              20, 160, 160, 18,
                                              hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_old_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                       20, 160, 146, 24,
                                       hwnd_ai_panel_, (HMENU)4871, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_new_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                       174, 160, 146, 24,
                                       hwnd_ai_panel_, (HMENU)4872, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Search", WS_CHILD | BS_OWNERDRAW,
                  328, 130, 80, 24, hwnd_ai_panel_, (HMENU)4873, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Highlight", WS_CHILD | BS_OWNERDRAW,
                  414, 130, 80, 24, hwnd_ai_panel_, (HMENU)4874, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Stats", WS_CHILD | BS_OWNERDRAW,
                  328, 160, 80, 24, hwnd_ai_panel_, (HMENU)4875, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Selftest", WS_CHILD | BS_OWNERDRAW,
                  414, 160, 80, 24, hwnd_ai_panel_, (HMENU)4876, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Rename Plan", WS_CHILD | BS_OWNERDRAW,
                  328, 190, 80, 24, hwnd_ai_panel_, (HMENU)4877, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", L"Prepare Patch", WS_CHILD | BS_OWNERDRAW,
                  414, 190, 80, 24, hwnd_ai_panel_, (HMENU)4878, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_label_results_ = CreateWindowW(L"STATIC", L"Results",
                                               WS_CHILD,
                                               20, 220, 100, 18,
                                               hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_results_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                           WS_CHILD | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                           20, 220, 230, 288,
                                           hwnd_ai_panel_, (HMENU)4879, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_label_patch_ = CreateWindowW(L"STATIC", L"Patch Preview",
                                             WS_CHILD,
                                             258, 220, 120, 18,
                                             hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_patch_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                         WS_CHILD | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                                         258, 220, 236, 288,
                                         hwnd_ai_panel_, (HMENU)4880, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_copy_results_ = CreateWindowW(L"BUTTON", L"Copy Results",
                                              WS_CHILD | BS_OWNERDRAW,
                                              140, 216, 110, 24,
                                              hwnd_ai_panel_, (HMENU)4891, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_copy_patch_ = CreateWindowW(L"BUTTON", L"Copy Patch",
                                            WS_CHILD | BS_OWNERDRAW,
                                            394, 216, 100, 24,
                                            hwnd_ai_panel_, (HMENU)4892, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_highlight_hit_ = CreateWindowW(L"BUTTON", L"Highlight Hit",
                                               WS_CHILD | BS_OWNERDRAW,
                                               20, 512, 110, 24,
                                               hwnd_ai_panel_, (HMENU)4893, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_copy_hit_path_ = CreateWindowW(L"BUTTON", L"Copy Hit Path",
                                               WS_CHILD | BS_OWNERDRAW,
                                               140, 512, 110, 24,
                                               hwnd_ai_panel_, (HMENU)4894, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_selected_ = CreateWindowW(L"STATIC", L"Selected Hit: -",
                                          WS_CHILD,
                                          20, 540, 220, 18,
                                          hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_coh_open_hit_ = CreateWindowW(L"BUTTON", L"Open Hit",
                                          WS_CHILD | BS_OWNERDRAW,
                                          258, 512, 90, 24,
                                          hwnd_ai_panel_, (HMENU)4895, GetModuleHandleW(nullptr), nullptr);
    ew_set_edit_margins(hwnd_ai_chat_input_);
    ew_set_edit_margins(hwnd_ai_repo_preview_);
    ew_set_edit_margins(hwnd_ai_repo_coherence_);
    ew_set_edit_margins(hwnd_ai_coh_query_);
    ew_set_edit_margins(hwnd_ai_coh_old_);
    ew_set_edit_margins(hwnd_ai_coh_new_);
    ew_set_edit_margins(hwnd_ai_coh_patch_);

    // Seed chat 0.
    ai_chat_count_u32_ = 1u;
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_msgs_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_title_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_folder_of_u32_[i] = 0u;
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_patch_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_patch_explain_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_patch_meta_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_last_workflow_event_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_apply_target_dir_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_patch_previewed_[i] = false;
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_last_detected_patch_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_last_detected_patch_valid_[i] = false;
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_mode_u32_[i] = SubstrateManager::EW_CHAT_MEMORY_MODE_TALK;
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_cortex_summary_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_project_summary_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_project_root_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_patch_scope_root_w_[i].clear();
    for (uint32_t i = 0u; i < AI_CHAT_MAX; ++i) ai_chat_patch_scope_file_count_u32_[i] = 0u;
    ai_chat_folder_id_u32_ = 0u;

    ai_chat_title_w_[0] = L"Chat 1";
    AiChatAppend(0u, L"Genesis AI ready. Learning/Crawling default OFF.");
    AiChatAppend(0u, L"Nothing learned is written to disk unless you explicitly request export/apply.");
    AiChatRenderSelected();

    ai_tab_index_u32_ = 0u;
    RefreshAiChatTabLabels();

    ew_apply_editor_fonts(hwnd_ai_panel_);
    LayoutAiPanelChildren();
    AiPanelSetView(0u);

    // Start hidden; user opens via UI.
    ShowWindow(hwnd_ai_panel_, SW_HIDE);
}

void App::LayoutAiPanelChildren() {
    if (!hwnd_ai_panel_) return;
    RECT rc{}; GetClientRect(hwnd_ai_panel_, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int left = 20;
    const int right = w - 20;
    const int header_y = 40;
    const int content_top = 106;
    const int content_bottom = h - 20;
    const int content_h = (content_bottom > content_top) ? (content_bottom - content_top) : 0;
    const int top_button_y = 6;
    if (hwnd_ai_menu_) MoveWindow(hwnd_ai_menu_, 12, top_button_y, 28, 28, TRUE);
    if (hwnd_ai_chat_nextaction_) MoveWindow(hwnd_ai_chat_nextaction_, right - 346, top_button_y, 62, 28, TRUE);
    if (hwnd_ai_chat_patchview_) MoveWindow(hwnd_ai_chat_patchview_, right - 278, top_button_y, 62, 28, TRUE);
    if (hwnd_ai_chat_preview_) MoveWindow(hwnd_ai_chat_preview_, right - 210, top_button_y, 64, 28, TRUE);
    if (hwnd_ai_chat_usepatch_) MoveWindow(hwnd_ai_chat_usepatch_, right - 140, top_button_y, 52, 28, TRUE);
    if (hwnd_ai_chat_apply_) MoveWindow(hwnd_ai_chat_apply_, right - 82, top_button_y, 64, 28, TRUE);
    if (hwnd_ai_chat_compose_) MoveWindow(hwnd_ai_chat_compose_, right - 18, top_button_y, 28, 28, TRUE);
    if (hwnd_ai_view_chat_) MoveWindow(hwnd_ai_view_chat_, 12, header_y, 72, 26, TRUE);
    if (hwnd_ai_view_repo_) MoveWindow(hwnd_ai_view_repo_, 90, header_y, 92, 26, TRUE);
    if (hwnd_ai_view_coherence_) MoveWindow(hwnd_ai_view_coherence_, 188, header_y, 92, 26, TRUE);
    if (hwnd_ai_panel_chat_title_) MoveWindow(hwnd_ai_panel_chat_title_, 52, 10, w - 320, 18, TRUE);
    if (hwnd_ai_panel_chat_state_) MoveWindow(hwnd_ai_panel_chat_state_, 52, 28, w - 320, 18, TRUE);

    const int chat_input_h = 78;
    const int chat_send_h = 30;
    const int list_top = content_top;
    const int input_top = h - 98;
    const int list_h = input_top - list_top - 8;
    if (hwnd_ai_panel_tool_status_) MoveWindow(hwnd_ai_panel_tool_status_, w - 228, 44, 208, 18, TRUE);
    if (hwnd_ai_tab_) MoveWindow(hwnd_ai_tab_, 10, 72, w - 20, h - 82, TRUE);
    if (hwnd_ai_chat_list_) MoveWindow(hwnd_ai_chat_list_, left, list_top, w - 40, list_h, TRUE);
    if (hwnd_ai_chat_link_project_) MoveWindow(hwnd_ai_chat_link_project_, left, input_top + 24, 26, 28, TRUE);
    if (hwnd_ai_chat_input_) MoveWindow(hwnd_ai_chat_input_, left + 32, input_top, w - 172, chat_input_h, TRUE);
    if (hwnd_ai_chat_send_) MoveWindow(hwnd_ai_chat_send_, w - 110, input_top + 24, 90, chat_send_h, TRUE);

    if (hwnd_ai_repo_status_) MoveWindow(hwnd_ai_repo_status_, left, content_top, w - 40, 18, TRUE);
    if (hwnd_ai_repo_refresh_) MoveWindow(hwnd_ai_repo_refresh_, left, content_top + 24, 86, 28, TRUE);
    if (hwnd_ai_repo_copy_) MoveWindow(hwnd_ai_repo_copy_, left + 92, content_top + 24, 110, 28, TRUE);
    if (hwnd_ai_repo_highlight_) MoveWindow(hwnd_ai_repo_highlight_, left + 208, content_top + 24, 92, 28, TRUE);
    const int repo_top = content_top + 82;
    const int repo_left_w = (w - 60) / 3;
    const int repo_right_x = left + repo_left_w + 8;
    const int repo_right_w = w - 40 - repo_left_w - 8;
    const int repo_preview_h = (content_bottom - repo_top - 8) * 2 / 3;
    if (hwnd_ai_repo_label_files_) MoveWindow(hwnd_ai_repo_label_files_, left, repo_top - 18, repo_left_w, 18, TRUE);
    if (hwnd_ai_repo_list_) MoveWindow(hwnd_ai_repo_list_, left, repo_top, repo_left_w, content_bottom - repo_top - 34, TRUE);
    if (hwnd_ai_repo_selected_) MoveWindow(hwnd_ai_repo_selected_, left, content_bottom - 24, repo_left_w - 98, 18, TRUE);
    if (hwnd_ai_repo_copy_path_) MoveWindow(hwnd_ai_repo_copy_path_, left + repo_left_w - 92, content_bottom - 28, 92, 24, TRUE);
    if (hwnd_ai_repo_label_preview_) MoveWindow(hwnd_ai_repo_label_preview_, repo_right_x, repo_top - 18, repo_right_w, 18, TRUE);
    if (hwnd_ai_repo_preview_) MoveWindow(hwnd_ai_repo_preview_, repo_right_x, repo_top, repo_right_w, repo_preview_h, TRUE);
    if (hwnd_ai_repo_label_coherence_) MoveWindow(hwnd_ai_repo_label_coherence_, repo_right_x, repo_top + repo_preview_h + 8 - 18, repo_right_w, 18, TRUE);
    if (hwnd_ai_repo_coherence_) MoveWindow(hwnd_ai_repo_coherence_, repo_right_x, repo_top + repo_preview_h + 8, repo_right_w, content_bottom - (repo_top + repo_preview_h + 8), TRUE);

    if (hwnd_ai_coh_stats_) MoveWindow(hwnd_ai_coh_stats_, left, content_top, w - 40, 18, TRUE);
    if (hwnd_ai_coh_label_query_) MoveWindow(hwnd_ai_coh_label_query_, left, content_top + 24, 100, 18, TRUE);
    if (hwnd_ai_coh_query_) MoveWindow(hwnd_ai_coh_query_, left, content_top + 44, w - 212, 24, TRUE);
    if (hwnd_ai_coh_label_rename_) MoveWindow(hwnd_ai_coh_label_rename_, left, content_top + 74, 180, 18, TRUE);
    if (hwnd_ai_coh_old_) MoveWindow(hwnd_ai_coh_old_, left, content_top + 94, (w - 56) / 2 - 8, 24, TRUE);
    if (hwnd_ai_coh_new_) MoveWindow(hwnd_ai_coh_new_, left + (w - 56) / 2, content_top + 94, (w - 56) / 2 - 8, 24, TRUE);
    const int bx = w - 176;
    for (int i = 4873; i <= 4878; ++i) {
        HWND hb = GetDlgItem(hwnd_ai_panel_, i);
        if (hb) {
            const int row = (i - 4873) % 2;
            const int col = (i - 4873) / 2;
            MoveWindow(hb, bx + row * 86, content_top + 44 + col * 30, 80, 24, TRUE);
        }
    }
    const int coh_top = content_top + 150;
    const int coh_left_w = (w - 56) / 2;
    if (hwnd_ai_coh_label_results_) MoveWindow(hwnd_ai_coh_label_results_, left, coh_top - 18, coh_left_w, 18, TRUE);
    if (hwnd_ai_coh_copy_results_) MoveWindow(hwnd_ai_coh_copy_results_, left + coh_left_w - 110, coh_top - 22, 110, 24, TRUE);
    if (hwnd_ai_coh_results_) MoveWindow(hwnd_ai_coh_results_, left, coh_top, coh_left_w, (content_bottom - coh_top) - 54, TRUE);
    if (hwnd_ai_coh_selected_) MoveWindow(hwnd_ai_coh_selected_, left, content_bottom - 50, coh_left_w, 18, TRUE);
    if (hwnd_ai_coh_highlight_hit_) MoveWindow(hwnd_ai_coh_highlight_hit_, left, content_bottom - 28, 110, 24, TRUE);
    if (hwnd_ai_coh_copy_hit_path_) MoveWindow(hwnd_ai_coh_copy_hit_path_, left + 118, content_bottom - 28, 110, 24, TRUE);
    if (hwnd_ai_coh_open_hit_) MoveWindow(hwnd_ai_coh_open_hit_, left + 236, content_bottom - 28, 90, 24, TRUE);
    if (hwnd_ai_coh_label_patch_) MoveWindow(hwnd_ai_coh_label_patch_, left + coh_left_w + 8, coh_top - 18, coh_left_w, 18, TRUE);
    if (hwnd_ai_coh_copy_patch_) MoveWindow(hwnd_ai_coh_copy_patch_, left + coh_left_w + 8 + coh_left_w - 100, coh_top - 22, 100, 24, TRUE);
    if (hwnd_ai_coh_patch_) MoveWindow(hwnd_ai_coh_patch_, left + coh_left_w + 8, coh_top, coh_left_w, content_bottom - coh_top, TRUE);
}

void App::RefreshAiPanelChrome() {
    if (!hwnd_ai_panel_) return;
    const uint32_t idx = (ai_tab_index_u32_ < AI_CHAT_MAX) ? ai_tab_index_u32_ : 0u;
    std::wstring title = ai_chat_title_w_[idx];
    if (title.empty()) {
        wchar_t label[32];
        _snwprintf(label, 32, L"Chat %u", (unsigned)(idx + 1u));
        title = label;
    }
    if (hwnd_ai_panel_chat_title_) {
        std::wstring folder = (ai_chat_folder_of_u32_[idx] == 0u) ? L"Root" : (std::wstring(L"Folder ") + std::to_wstring((unsigned)ai_chat_folder_of_u32_[idx]));
        std::wstring line = title + L"  ·  " + folder;
        SetWindowTextW(hwnd_ai_panel_chat_title_, line.c_str());
    }
    if (hwnd_ai_panel_chat_state_) {
        std::wstring state = (ai_chat_mode_u32_[idx] == SubstrateManager::EW_CHAT_MEMORY_MODE_CODE) ? L"Mode: code cortex." : ((ai_chat_mode_u32_[idx] == SubstrateManager::EW_CHAT_MEMORY_MODE_SIM) ? L"Mode: simulation cortex." : L"Mode: conversational cortex.");
        if (ai_chat_patch_w_[idx].empty()) {
            state += L"  No patch buffered.";
        } else if (!ai_chat_patch_previewed_[idx]) {
            state += L"  Patch buffered · preview required before apply.";
        } else {
            state += L"  Patch preview complete · explicit apply still required.";
        }
        if (!ai_chat_patch_w_[idx].empty()) {
            EwPatchApplyReport targets_rep{};
            ew_patch_extract_targets(ai_chat_patch_w_[idx], &targets_rep);
            state += L"  Files: " + std::to_wstring((unsigned long long)targets_rep.files_touched_u32);
        }
        if (!ai_chat_apply_target_dir_w_[idx].empty()) state += L"  Apply target: " + ai_chat_apply_target_dir_w_[idx];
        if (!ai_chat_project_summary_w_[idx].empty()) state += L"  Project linked.";
        EigenWare::SubstrateManager::EwStagedExportBundle staged_bundle{};
        if (scene_ && scene_->sm.ui_snapshot_latest_export_bundle(idx, staged_bundle)) {
            if (staged_bundle.whole_repo_continuation_u8 != 0u) state += L"  Whole-repo continuation staged.";
            else if (!staged_bundle.operation_label_utf8.empty()) state += L"  Export stage: " + utf8_to_wide(staged_bundle.operation_label_utf8) + L".";
        }
        if (!ai_chat_patch_explain_w_[idx].empty()) state += L"  Patch view available.";
        const std::wstring warning_headline = BuildAiChatPatchWarningHeadline(idx, nullptr);
        const std::wstring next_action = BuildAiChatPatchNextActionText(idx, nullptr);
        if (HasBlockingAiChatPatchWarnings(idx, nullptr)) {
            state += L"  Scope warnings present — apply gated.";
            if (!warning_headline.empty()) state += L"  " + warning_headline;
        } else if (!BuildAiChatPatchWarningText(idx, nullptr).empty()) {
            state += L"  Scope warnings present.";
            if (!warning_headline.empty()) state += L"  " + warning_headline;
        }
        if (!next_action.empty()) state += L"  " + next_action;
        SetWindowTextW(hwnd_ai_panel_chat_state_, state.c_str());
    }
    if (hwnd_ai_panel_action_hint_) {
        const std::wstring action = BuildAiChatPrimaryActionLabel(idx);
        const std::wstring reason = BuildAiChatPrimaryActionReasonText(idx);
        std::wstring line = L"Next";
        if (!action.empty() && action != L"Open Chat") line = L"Next: " + action;
        else line = L"Next: idle";
        if (!reason.empty()) line += L"  ·  " + reason;
        if (!ai_chat_last_workflow_event_w_[idx].empty()) line += L"  |  Last: " + ai_chat_last_workflow_event_w_[idx];
        SetWindowTextW(hwnd_ai_panel_action_hint_, line.c_str());
    }
    if (hwnd_ai_chat_cortex_) {
        std::wstring cortex = ai_chat_cortex_summary_w_[idx].empty() ? std::wstring(L"Cortex: idle") : ai_chat_cortex_summary_w_[idx];
        if (!ai_chat_project_summary_w_[idx].empty()) cortex += L"  |  " + ai_chat_project_summary_w_[idx];
        SetWindowTextW(hwnd_ai_chat_cortex_, cortex.c_str());
    }
    if (hwnd_ai_view_chat_) InvalidateRect(hwnd_ai_view_chat_, nullptr, TRUE);
    if (hwnd_ai_view_repo_) InvalidateRect(hwnd_ai_view_repo_, nullptr, TRUE);
    if (hwnd_ai_view_coherence_) InvalidateRect(hwnd_ai_view_coherence_, nullptr, TRUE);
    if (hwnd_ai_chat_mode_talk_) InvalidateRect(hwnd_ai_chat_mode_talk_, nullptr, TRUE);
    if (hwnd_ai_chat_mode_code_) InvalidateRect(hwnd_ai_chat_mode_code_, nullptr, TRUE);
    if (hwnd_ai_chat_mode_sim_) InvalidateRect(hwnd_ai_chat_mode_sim_, nullptr, TRUE);
}

void App::RefreshAiChatCortex(uint32_t chat_idx_u32) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    if (!scene_) {
        ai_chat_cortex_summary_w_[chat_idx_u32] = L"Cortex: scene unavailable.";
        RefreshAiPanelChrome();
        return;
    }
    std::vector<SubstrateManager::EwChatMemoryEntry> entries;
    std::string summary_utf8;
    (void)scene_->SnapshotAiChatMemory(ai_chat_mode_u32_[chat_idx_u32], 3u, entries, summary_utf8);
    std::wstring line = L"Cortex: " + utf8_to_wide(summary_utf8);
    if (!entries.empty()) {
        line += L"  ·  ";
        for (size_t i = 0; i < entries.size() && i < 2u; ++i) {
            if (i > 0u) line += L"  ||  ";
            line += utf8_to_wide(entries[i].text_utf8);
        }
    }
    ai_chat_cortex_summary_w_[chat_idx_u32] = line;
    SubstrateManager::EwProjectLinkEntry proj{};
    if (scene_->SnapshotAiChatProject(chat_idx_u32, proj) && !proj.project_root_utf8.empty()) {
        ai_chat_project_root_w_[chat_idx_u32] = utf8_to_wide(proj.project_root_utf8);
        ai_chat_project_summary_w_[chat_idx_u32] = L"Project: " + ai_chat_project_root_w_[chat_idx_u32] + L"  files=" + std::to_wstring((unsigned long long)proj.file_count_u32);
    } else {
        ai_chat_project_root_w_[chat_idx_u32].clear();
        ai_chat_project_summary_w_[chat_idx_u32].clear();
    }
    RefreshAiPanelChrome();
    RefreshNodePanel();
}

void App::AiPanelSetView(uint32_t view_u32) {
    ai_panel_view_u32_ = (view_u32 > 2u) ? 0u : view_u32;
    const bool chat = (ai_panel_view_u32_ == 0u);
    const bool repo = (ai_panel_view_u32_ == 1u);
    const bool coh = (ai_panel_view_u32_ == 2u);
    auto vis = [&](HWND h, bool on) { if (h) ShowWindow(h, on ? SW_SHOW : SW_HIDE); };
    vis(hwnd_ai_tab_, chat); vis(hwnd_ai_chat_cortex_, chat); vis(hwnd_ai_chat_link_project_, chat); vis(hwnd_ai_chat_list_, chat); vis(hwnd_ai_chat_input_, chat); vis(hwnd_ai_chat_send_, chat); vis(hwnd_ai_chat_mode_talk_, chat); vis(hwnd_ai_chat_mode_code_, chat); vis(hwnd_ai_chat_mode_sim_, chat);
    vis(hwnd_ai_repo_status_, repo); vis(hwnd_ai_repo_refresh_, repo); vis(hwnd_ai_repo_copy_, repo); vis(hwnd_ai_repo_highlight_, repo); vis(hwnd_ai_repo_label_files_, repo); vis(hwnd_ai_repo_label_preview_, repo); vis(hwnd_ai_repo_label_coherence_, repo); vis(hwnd_ai_repo_list_, repo); vis(hwnd_ai_repo_preview_, repo); vis(hwnd_ai_repo_coherence_, repo); vis(hwnd_ai_repo_selected_, repo); vis(hwnd_ai_repo_copy_path_, repo);
    vis(hwnd_ai_coh_stats_, coh); vis(hwnd_ai_coh_label_query_, coh); vis(hwnd_ai_coh_label_rename_, coh); vis(hwnd_ai_coh_label_results_, coh); vis(hwnd_ai_coh_label_patch_, coh); vis(hwnd_ai_coh_query_, coh); vis(hwnd_ai_coh_old_, coh); vis(hwnd_ai_coh_new_, coh); vis(hwnd_ai_coh_results_, coh); vis(hwnd_ai_coh_patch_, coh); vis(hwnd_ai_coh_copy_results_, coh); vis(hwnd_ai_coh_copy_patch_, coh); vis(hwnd_ai_coh_highlight_hit_, coh); vis(hwnd_ai_coh_copy_hit_path_, coh); vis(hwnd_ai_coh_selected_, coh); vis(hwnd_ai_coh_open_hit_, coh);
    for (int i = 4873; i <= 4878; ++i) vis(GetDlgItem(hwnd_ai_panel_, i), coh);
    if (hwnd_ai_panel_tool_status_) {
        if (chat) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Chat view ready.");
        else if (repo) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Repository tools ready.");
        else SetWindowTextW(hwnd_ai_panel_tool_status_, L"Coherence tools ready.");
    }
    if (repo) { RefreshAiRepoPane(); if (hwnd_ai_repo_list_) SetFocus(hwnd_ai_repo_list_); }
    if (coh) { RefreshAiCoherenceStats(); if (hwnd_ai_coh_query_) SetFocus(hwnd_ai_coh_query_); }
    if (chat) { RefreshAiChatCortex(ai_tab_index_u32_); if (hwnd_ai_chat_input_) SetFocus(hwnd_ai_chat_input_); }
    RefreshAiPanelChrome();
    InvalidateRect(hwnd_ai_panel_, nullptr, TRUE);
}


bool App::BuildCanonicalReferenceSummaryFromHits(const std::string& subject_rel_utf8,
                                               const std::string& source_key_utf8,
                                               const std::wstring& source_label_w,
                                               const std::string& query_utf8,
                                               const std::vector<genesis::GeCoherenceHit>& hits,
                                               CanonicalReferenceSummary* out_summary,
                                               std::string* out_err) const {
    if (out_summary) *out_summary = CanonicalReferenceSummary{};
    CanonicalReferenceSummary summary{};
    summary.subject_rel_utf8 = subject_rel_utf8;
    summary.source_key_utf8 = source_key_utf8;
    summary.query_utf8 = query_utf8;
    summary.source_label_w = source_label_w;
    summary.hit_count_u32 = (uint32_t)hits.size();
    summary.ranking_revision_u64 = coh_highlight_seen_revision_u64_;
    std::wstring subject_w = subject_rel_utf8.empty() ? std::wstring(L"(none)") : utf8_to_wide(subject_rel_utf8);
    std::wstring query_w = query_utf8.empty() ? std::wstring() : utf8_to_wide(query_utf8);
    std::wstringstream ss;
    ss << L"Canonical source: " << source_label_w << L"\r\n";
    if (!subject_rel_utf8.empty()) ss << L"Selected file: " << subject_w << L"\r\n";
    if (!query_utf8.empty()) ss << L"Query/plan key: " << query_w << L"\r\n";
    ss << L"Linked references: " << (unsigned long long)hits.size() << L"\r\n";
    if (!hits.empty()) {
        for (size_t i = 0; i < hits.size(); ++i) {
            const auto& h = hits[i];
            std::wstring line = std::to_wstring((unsigned long long)(i + 1u)) + L". " + utf8_to_wide(h.rel_path_utf8);
            line += L"  score=" + std::to_wstring((unsigned long long)h.score_u32);
            summary.lines_w.push_back(line);
            summary.paths_utf8.push_back(h.rel_path_utf8);
            ss << line << L"\r\n";
            if (!subject_rel_utf8.empty() && _stricmp(h.rel_path_utf8.c_str(), subject_rel_utf8.c_str()) == 0) summary.exact_subject_present = true;
        }
    } else {
        const std::wstring fallback = !query_utf8.empty() ? (std::wstring(L"No ranked references for query: ") + query_w + L".") : std::wstring(L"No related references found.");
        summary.lines_w.push_back(fallback);
        ss << fallback;
    }
    summary.summary_w = ss.str();
    if (out_summary) *out_summary = summary;
    if (out_err) out_err->clear();
    return true;
}

bool App::BuildCanonicalReferenceSummaryForPath(const std::string& rel_utf8, uint32_t limit_u32, CanonicalReferenceSummary* out_summary, std::string* out_err) const {
    if (out_summary) *out_summary = CanonicalReferenceSummary{};
    if (rel_utf8.empty() || !scene_) {
        if (out_err) *out_err = std::string("reference summary unavailable");
        return false;
    }
    std::vector<genesis::GeCoherenceHit> hits;
    std::string err;
    if (!scene_->SnapshotRepoFileCoherenceHits(rel_utf8, limit_u32, hits, &err)) {
        if (out_err) *out_err = err.empty() ? std::string("reference summary unavailable") : err;
        return false;
    }
    return BuildCanonicalReferenceSummaryFromHits(rel_utf8,
                                                  std::string("repo_file_refs"),
                                                  std::wstring(L"repo/coherence canonical file ranking"),
                                                  rel_utf8,
                                                  hits,
                                                  out_summary,
                                                  out_err);
}

bool App::BuildCanonicalReferenceSummaryForRename(const std::string& old_ident_utf8, const std::string& new_ident_utf8, uint32_t limit_u32, CanonicalReferenceSummary* out_summary, std::string* out_err) const {
    if (out_summary) *out_summary = CanonicalReferenceSummary{};
    if (old_ident_utf8.empty() || new_ident_utf8.empty() || !scene_) {
        if (out_err) *out_err = std::string("rename summary unavailable");
        return false;
    }
    std::vector<genesis::GeCoherenceHit> hits;
    std::string err;
    if (!scene_->SnapshotCoherenceRenamePlan(old_ident_utf8, new_ident_utf8, limit_u32, hits, &err)) {
        if (out_err) *out_err = err.empty() ? std::string("rename summary unavailable") : err;
        return false;
    }
    return BuildCanonicalReferenceSummaryFromHits(std::string(),
                                                  std::string("rename_plan"),
                                                  std::wstring(L"rename/reference canonical ranking"),
                                                  old_ident_utf8 + std::string(" -> ") + new_ident_utf8,
                                                  hits,
                                                  out_summary,
                                                  out_err);
}

void App::CommitCanonicalReferenceSummary(const CanonicalReferenceSummary& summary, bool focus_coherence, const wchar_t* origin_w) {
    canonical_reference_summary_ = summary;
    const std::string preserve_rel = !summary.subject_rel_utf8.empty() ? summary.subject_rel_utf8 : ai_repo_selected_rel_utf8_;
    SetAiCoherenceResults(summary.lines_w, summary.paths_utf8, preserve_rel.empty() ? nullptr : &preserve_rel);
    if (hwnd_ai_repo_coherence_) SetWindowTextW(hwnd_ai_repo_coherence_, summary.summary_w.c_str());
    if (hwnd_content_refcheck_) {
        std::wstring label = L"Refs";
        if (summary.hit_count_u32 != 0u) label += L" (" + std::to_wstring((unsigned long long)summary.hit_count_u32) + L")";
        SetWindowTextW(hwnd_content_refcheck_, label.c_str());
    }
    if (focus_coherence) {
        AiPanelSetView(2u);
        if (hwnd_ai_panel_ && !IsWindowVisible(hwnd_ai_panel_)) ShowWindow(hwnd_ai_panel_, SW_SHOW);
    }
    if (hwnd_ai_panel_tool_status_) {
        std::wstring status = origin_w ? std::wstring(origin_w) : std::wstring(L"Reference review");
        if (!summary.source_label_w.empty()) status += L" · " + summary.source_label_w;
        if (!summary.subject_rel_utf8.empty()) {
            status += L": ";
            status += utf8_to_wide(summary.subject_rel_utf8);
        } else if (!summary.query_utf8.empty()) {
            status += L": ";
            status += utf8_to_wide(summary.query_utf8);
        }
        status += L" (refs=";
        status += std::to_wstring((unsigned long long)summary.hit_count_u32);
        status += L")";
        SetWindowTextW(hwnd_ai_panel_tool_status_, status.c_str());
    }
    RefreshContentBrowserChrome();
}

std::string App::ResolveAiChatPrimaryPathUtf8(uint32_t chat_idx_u32) const {

    if (chat_idx_u32 >= AI_CHAT_MAX) return std::string();
    if (!ai_chat_patch_w_[chat_idx_u32].empty()) {
        EwPatchApplyReport rep{};
        ew_patch_extract_targets(ai_chat_patch_w_[chat_idx_u32], &rep);
        if (rep.files_touched_u32 != 0u && !rep.rel_paths_w[0].empty()) return wide_to_utf8(rep.rel_paths_w[0]);
    }
    if (!ai_repo_selected_rel_utf8_.empty()) return ai_repo_selected_rel_utf8_;
    if (!content_selected_rel_utf8_.empty()) return content_selected_rel_utf8_;
    if (!canonical_reference_summary_.subject_rel_utf8.empty()) return canonical_reference_summary_.subject_rel_utf8;
    return std::string();
}

bool App::NavigateAiChatReferenceSpine(uint32_t chat_idx_u32, bool focus_repo, bool focus_coherence) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return false;
    RefreshAiNavigationSpine(chat_idx_u32);
    const std::string rel = ResolveAiChatPrimaryPathUtf8(chat_idx_u32);
    bool any = false;
    if (!rel.empty()) {
        any = SelectAiRepoPath(rel, true) || any;
        any = SelectAiCoherencePath(rel, true) || any;
        any = SelectContentRelativePath(rel) || any;
    }
    if (focus_coherence && hwnd_ai_panel_) {
        AiPanelSetView(2u);
        if (!IsWindowVisible(hwnd_ai_panel_)) ShowWindow(hwnd_ai_panel_, SW_SHOW);
    } else if (focus_repo && hwnd_ai_panel_) {
        AiPanelSetView(1u);
        if (!IsWindowVisible(hwnd_ai_panel_)) ShowWindow(hwnd_ai_panel_, SW_SHOW);
    }
    return any;
}

void App::RefreshAiNavigationSpine(uint32_t chat_idx_u32) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    const std::string rel = ResolveAiChatPrimaryPathUtf8(chat_idx_u32);
    ai_chat_nav_target_rel_w_[chat_idx_u32] = rel.empty() ? std::wstring() : utf8_to_wide(rel);
    ai_chat_nav_session_w_[chat_idx_u32] = BuildAiChatWorkflowStatusText(chat_idx_u32);
    const std::wstring warning = BuildAiChatPatchWarningHeadline(chat_idx_u32, nullptr);
    const std::wstring next_action = BuildAiChatPatchNextActionText(chat_idx_u32, nullptr);
    ai_chat_nav_validation_w_[chat_idx_u32] = warning.empty() ? std::wstring(L"Apply/validation result: canonical workflow state is current.") : std::wstring(L"Apply/validation result: ") + warning;
    if (!next_action.empty()) ai_chat_nav_validation_w_[chat_idx_u32] += L"  " + next_action;
    ai_chat_nav_reference_w_[chat_idx_u32].clear();
    CanonicalReferenceSummary summary{};
    std::string err;
    bool have_summary = false;
    if (!rel.empty()) {
        have_summary = BuildCanonicalReferenceSummaryForPath(rel, 8u, &summary, &err);
    } else if (!canonical_reference_summary_.query_utf8.empty() || !canonical_reference_summary_.source_label_w.empty()) {
        summary = canonical_reference_summary_;
        have_summary = true;
    }
    if (have_summary) {
        ai_chat_nav_reference_w_[chat_idx_u32] = summary.summary_w;
    } else if (!err.empty()) {
        ai_chat_nav_reference_w_[chat_idx_u32] = utf8_to_wide(err);
    }
}

std::wstring App::BuildAiNavigationSpineText(uint32_t chat_idx_u32) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    std::wstringstream ss;
    ss << L"NAVIGATION SPINE\r\n";
    ss << L"Patch session record: ";
    ss << (ai_chat_nav_session_w_[chat_idx_u32].empty() ? BuildAiChatWorkflowStatusText(chat_idx_u32) : ai_chat_nav_session_w_[chat_idx_u32]);
    ss << L"\r\n";
    if (!ai_chat_nav_target_rel_w_[chat_idx_u32].empty()) {
        ss << L"Target file/region: " << ai_chat_nav_target_rel_w_[chat_idx_u32] << L"\r\n";
        ss << L"Repo preview route: Repository -> " << ai_chat_nav_target_rel_w_[chat_idx_u32] << L"\r\n";
        ss << L"Coherence hit route: Coherence -> " << ai_chat_nav_target_rel_w_[chat_idx_u32] << L"\r\n";
    }
    ss << (ai_chat_nav_validation_w_[chat_idx_u32].empty() ? std::wstring(L"Apply/validation result: pending canonical refresh.") : ai_chat_nav_validation_w_[chat_idx_u32]) << L"\r\n";
    if (!ai_chat_nav_reference_w_[chat_idx_u32].empty()) ss << L"\r\nCanonical reference review:\r\n" << ai_chat_nav_reference_w_[chat_idx_u32];
    return ss.str();
}

void App::RefreshAiRepoPane() {


    if (!scene_ || !hwnd_ai_repo_list_) return;
    std::string status, err;
    std::vector<std::string> rels;
    scene_->RescanRepoReader();
    (void)scene_->SnapshotRepoReaderStatus(status);
    if (!scene_->SnapshotRepoReaderFiles(128u, rels, &err)) {
        status = err.empty() ? std::string("repo read failed") : err;
        rels.clear();
    }
    const std::string preserve_rel = ai_repo_selected_rel_utf8_;
    ai_repo_rel_paths_utf8_.swap(rels);
    SetWindowTextW(hwnd_ai_repo_status_, utf8_to_wide(status).c_str());
    if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, (std::wstring(L"Repository: ") + utf8_to_wide(status)).c_str());
    SendMessageW(hwnd_ai_repo_list_, LB_RESETCONTENT, 0, 0);
    int restore_sel = -1;
    for (size_t i = 0; i < ai_repo_rel_paths_utf8_.size(); ++i) {
        const auto& s = ai_repo_rel_paths_utf8_[i];
        SendMessageW(hwnd_ai_repo_list_, LB_ADDSTRING, 0, (LPARAM)utf8_to_wide(s).c_str());
        if (!preserve_rel.empty() && s == preserve_rel && restore_sel < 0) restore_sel = (int)i;
    }
    if (!ai_repo_rel_paths_utf8_.empty()) {
        if (restore_sel < 0) restore_sel = 0;
        SendMessageW(hwnd_ai_repo_list_, LB_SETCURSEL, (WPARAM)restore_sel, 0);
        UpdateAiRepoSelection();
    } else {
        ai_repo_selected_rel_utf8_.clear();
        SetWindowTextW(hwnd_ai_repo_preview_, L"");
        SetWindowTextW(hwnd_ai_repo_coherence_, L"");
        if (hwnd_ai_repo_selected_) SetWindowTextW(hwnd_ai_repo_selected_, L"Selected: -");
    }
}

void App::UpdateAiRepoSelection() {
    if (!scene_ || !hwnd_ai_repo_list_) return;
    const int sel = (int)SendMessageW(hwnd_ai_repo_list_, LB_GETCURSEL, 0, 0);
    if (sel < 0 || (size_t)sel >= ai_repo_rel_paths_utf8_.size()) {
        ai_repo_selected_rel_utf8_.clear();
        SetWindowTextW(hwnd_ai_repo_preview_, L"");
        SetWindowTextW(hwnd_ai_repo_coherence_, L"");
        if (hwnd_ai_repo_selected_) SetWindowTextW(hwnd_ai_repo_selected_, L"Selected: -");
        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Repository: no file selected.");
        return;
    }
    const std::string rel = ai_repo_rel_paths_utf8_[(size_t)sel];
    ai_repo_selected_rel_utf8_ = rel;
    const std::wstring rel_w = utf8_to_wide(rel);
    if (hwnd_ai_repo_selected_) SetWindowTextW(hwnd_ai_repo_selected_, (std::wstring(L"Selected: ") + rel_w).c_str());
    std::string preview, err;
    if (!scene_->SnapshotRepoFilePreview(rel, 16384u, preview, &err)) preview = err.empty() ? std::string("preview unavailable") : err;
    SetWindowTextW(hwnd_ai_repo_preview_, utf8_to_wide(preview).c_str());
    CanonicalReferenceSummary summary{};
    (void)BuildCanonicalReferenceSummaryForPath(rel, 8u, &summary, &err);
    CommitCanonicalReferenceSummary(summary, false, L"Repository selection");
    if (hwnd_ai_panel_tool_status_) {
        std::wstring status = L"Repository: loaded ";
        status += rel_w;
        status += L" (coherence refs=";
        status += std::to_wstring((unsigned long long)summary.hit_count_u32);
        status += L")";
        SetWindowTextW(hwnd_ai_panel_tool_status_, status.c_str());
    }
}

void App::OpenSelectedAiRepoFile() {
    if (!hwnd_ai_repo_list_) return;
    const int sel = (int)SendMessageW(hwnd_ai_repo_list_, LB_GETCURSEL, 0, 0);
    if (sel < 0 || (size_t)sel >= ai_repo_rel_paths_utf8_.size()) return;
    const std::string rel = ai_repo_rel_paths_utf8_[(size_t)sel];
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::current_path(ec);
    if (ec || abs.empty()) return;
    abs /= rel;
    const std::wstring wabs = abs.lexically_normal().wstring();
    ShellExecuteW(hwnd_main_ ? hwnd_main_ : hwnd_ai_panel_, L"open", wabs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, (std::wstring(L"Repository: opened ") + utf8_to_wide(rel)).c_str());
}

bool App::SelectAiRepoPath(const std::string& rel_utf8, bool set_highlight) {
    if (rel_utf8.empty() || !hwnd_ai_repo_list_) return false;
    for (size_t i = 0; i < ai_repo_rel_paths_utf8_.size(); ++i) {
        if (ai_repo_rel_paths_utf8_[i] != rel_utf8) continue;
        const int cur = (int)SendMessageW(hwnd_ai_repo_list_, LB_GETCURSEL, 0, 0);
        if (cur != (int)i) SendMessageW(hwnd_ai_repo_list_, LB_SETCURSEL, (WPARAM)i, 0);
        UpdateAiRepoSelection();
        if (set_highlight && scene_) scene_->SetCoherenceHighlightPath(rel_utf8);
        return true;
    }
    return false;
}

bool App::SelectAiCoherencePath(const std::string& rel_utf8, bool set_highlight) {
    if (rel_utf8.empty() || !hwnd_ai_coh_results_) return false;
    for (size_t i = 0; i < ai_coh_result_paths_utf8_.size(); ++i) {
        if (ai_coh_result_paths_utf8_[i] != rel_utf8) continue;
        const int cur = (int)SendMessageW(hwnd_ai_coh_results_, LB_GETCURSEL, 0, 0);
        if (cur != (int)i) SendMessageW(hwnd_ai_coh_results_, LB_SETCURSEL, (WPARAM)i, 0);
        UpdateAiCoherenceSelection();
        if (set_highlight && scene_) scene_->SetCoherenceHighlightPath(rel_utf8);
        return true;
    }
    return false;
}

void App::RefreshAiCoherenceStats() {
    if (!scene_) return;
    std::string stats;
    if (scene_->SnapshotCoherenceStats(stats)) {
        SetWindowTextW(hwnd_ai_coh_stats_, utf8_to_wide(stats).c_str());
        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, (std::wstring(L"Coherence: ") + utf8_to_wide(stats)).c_str());
    }
}


void App::SetAiCoherenceResults(const std::vector<std::wstring>& lines_w, const std::vector<std::string>& paths_utf8, const std::string* preserve_rel_utf8) {
    ai_coh_result_lines_w_ = lines_w;
    ai_coh_result_paths_utf8_ = paths_utf8;
    const std::string preserve = preserve_rel_utf8 ? *preserve_rel_utf8 : std::string();
    ai_coh_selected_index_i32_ = -1;
    if (hwnd_ai_coh_results_) SendMessageW(hwnd_ai_coh_results_, LB_RESETCONTENT, 0, 0);
    int restore_sel = -1;
    for (size_t i = 0; i < ai_coh_result_lines_w_.size(); ++i) {
        if (hwnd_ai_coh_results_) SendMessageW(hwnd_ai_coh_results_, LB_ADDSTRING, 0, (LPARAM)ai_coh_result_lines_w_[i].c_str());
        if (!preserve.empty() && i < ai_coh_result_paths_utf8_.size() && ai_coh_result_paths_utf8_[i] == preserve && restore_sel < 0) restore_sel = (int)i;
    }
    if (!ai_coh_result_lines_w_.empty()) {
        if (restore_sel < 0) restore_sel = 0;
        if (hwnd_ai_coh_results_) SendMessageW(hwnd_ai_coh_results_, LB_SETCURSEL, (WPARAM)restore_sel, 0);
        UpdateAiCoherenceSelection();
    } else {
        if (hwnd_ai_coh_selected_) SetWindowTextW(hwnd_ai_coh_selected_, L"Selected Hit: -");
        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Coherence: no results.");
    }
}

void App::UpdateAiCoherenceSelection() {
    if (!hwnd_ai_coh_results_) return;
    const int sel = (int)SendMessageW(hwnd_ai_coh_results_, LB_GETCURSEL, 0, 0);
    if (sel < 0 || (size_t)sel >= ai_coh_result_lines_w_.size()) {
        ai_coh_selected_index_i32_ = -1;
        if (hwnd_ai_coh_selected_) SetWindowTextW(hwnd_ai_coh_selected_, L"Selected Hit: -");
        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Coherence: no active hit.");
        return;
    }
    ai_coh_selected_index_i32_ = sel;
    if ((size_t)sel < ai_coh_result_paths_utf8_.size() && !ai_coh_result_paths_utf8_[(size_t)sel].empty()) {
        const std::string& rel = ai_coh_result_paths_utf8_[(size_t)sel];
        std::wstring path_w = utf8_to_wide(rel);
        if (hwnd_ai_coh_selected_) SetWindowTextW(hwnd_ai_coh_selected_, (std::wstring(L"Selected Hit: ") + path_w).c_str());
        if (hwnd_ai_panel_tool_status_) {
            std::wstring status = L"Coherence result ";
            status += std::to_wstring((unsigned long long)(sel + 1));
            status += L"/";
            status += std::to_wstring((unsigned long long)ai_coh_result_lines_w_.size());
            status += L": ";
            status += path_w;
            SetWindowTextW(hwnd_ai_panel_tool_status_, status.c_str());
        }
        (void)SelectAiRepoPath(rel, false);
    } else {
        if (hwnd_ai_coh_selected_) SetWindowTextW(hwnd_ai_coh_selected_, (std::wstring(L"Selected Hit: ") + ai_coh_result_lines_w_[(size_t)sel]).c_str());
        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Coherence result selected.");
    }
}

void App::AiChatAppend(uint32_t chat_idx_u32, const std::wstring& line) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    auto& v = ai_chat_msgs_[chat_idx_u32];
    // Bounded UI ring: keep last 256 lines.
    if (v.size() >= 256u) {
        v.erase(v.begin(), v.begin() + (v.size() - 255u));
    }
    v.push_back(line);
}

static bool ew_is_ai_patch_explanation_line(const std::string& line) {
    return line.rfind("AI_PATCH_VIEW", 0) == 0 ||
           line.rfind("AI_PATCH_SCOPE", 0) == 0 ||
           line.rfind("AI_PATCH_REASON", 0) == 0 ||
           line.rfind("AI_PATCH_CANDIDATE", 0) == 0 ||
           line.rfind("AI_PATCH_FALLBACK", 0) == 0 ||
           line.rfind("AI_PATCH_BIND", 0) == 0;
}

static std::wstring ew_format_ai_patch_explanation_line(const std::string& line) {
    if (line.rfind("AI_PATCH_VIEW ", 0) == 0) return std::wstring(L"Patch View: ") + utf8_to_wide(line.substr(13));
    if (line.rfind("AI_PATCH_SCOPE ", 0) == 0) return std::wstring(L"Patch Scope: ") + utf8_to_wide(line.substr(15));
    if (line.rfind("AI_PATCH_REASON ", 0) == 0) return std::wstring(L"Patch Reason: ") + utf8_to_wide(line.substr(16));
    if (line.rfind("AI_PATCH_CANDIDATE ", 0) == 0) return std::wstring(L"Patch Candidate: ") + utf8_to_wide(line.substr(19));
    if (line.rfind("AI_PATCH_FALLBACK ", 0) == 0) return std::wstring(L"Patch Fallback: ") + utf8_to_wide(line.substr(18));
    if (line.rfind("AI_PATCH_BIND ", 0) == 0) return std::wstring(L"Patch Binding: ") + utf8_to_wide(line.substr(14));
    return utf8_to_wide(line);
}

static bool ew_extract_unified_diff_from_text(const std::wstring& text, std::wstring* out_diff) {
    if (!out_diff) return false;
    const std::wstring key = L"diff --git ";
    const size_t p = text.find(key);
    if (p == std::wstring::npos) return false;
    // Take from first diff header to end. Fail-closed unless at least one hunk exists.
    std::wstring diff = text.substr(p);
    if (diff.find(L"@@") == std::wstring::npos) return false;
    // Reasonable bound (UI-only): cap to ~256KB to avoid runaway clipboard/patch buffers.
    if (diff.size() > 256u * 1024u) diff.resize(256u * 1024u);
    *out_diff = diff;
    return true;
}

void App::AiChatAppendAssistant(uint32_t chat_idx_u32, const std::wstring& assistant_text) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    AiChatAppend(chat_idx_u32, std::wstring(L"Assistant: ") + assistant_text);
    if (scene_) {
        std::wstring clipped = assistant_text;
        if (clipped.size() > 220u) clipped.resize(220u);
        scene_->ObserveAiChatMemory(chat_idx_u32, ai_chat_mode_u32_[chat_idx_u32], std::string("assistant:") + wide_to_utf8(clipped));
        if (chat_idx_u32 == ai_tab_index_u32_) RefreshAiChatCortex(chat_idx_u32);
    }

    // Detect a unified diff in Assistant output; cache per chat for one-click "Use".
    std::wstring diff;
    if (ew_extract_unified_diff_from_text(assistant_text, &diff)) {
        ai_chat_last_detected_patch_w_[chat_idx_u32] = diff;
        ai_chat_last_detected_patch_valid_[chat_idx_u32] = true;
    }
}

static bool ew_browse_for_directory(HWND owner, const wchar_t* title_w, std::wstring& out_dir_w) {
    out_dir_w.clear();
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = title_w;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(pidl, path) ? true : false;
    CoTaskMemFree(pidl);
    if (!ok) return false;
    out_dir_w = path;
    return !out_dir_w.empty();
}

// Modal approval widget for choosing an apply/export target directory.
// Behavior:
//  - Forces directory selection on first apply (OK disabled until valid path).
//  - Remembers the last directory per chat; subsequent applies default to that directory.
//  - Provides Browse button to change.
struct EwAiApplyTargetDirDialog {
    HWND hwnd = nullptr;
    HWND hwnd_edit = nullptr;
    HWND hwnd_ok = nullptr;
    HWND hwnd_list = nullptr;
    HWND hwnd_files_label = nullptr;
    std::wstring result_dir;
    bool accepted = false;

    static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) {
            auto cs = (CREATESTRUCTW*)l;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        }
        auto* self = (EwAiApplyTargetDirDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (self) return self->WndProc(h, m, w, l);
        return DefWindowProcW(h, m, w, l);
    }

    void UpdateOkEnabled() {
        if (!hwnd_ok || !hwnd_edit) return;
        wchar_t buf[1024];
        GetWindowTextW(hwnd_edit, buf, 1024);
        std::wstring p(buf);
        while (!p.empty() && (p.back() == L' ' || p.back() == L'\t' || p.back() == L'\r' || p.back() == L'\n')) p.pop_back();
        DWORD attr = p.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(p.c_str());
        const bool ok = (!p.empty() && attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
        EnableWindow(hwnd_ok, ok ? TRUE : FALSE);
    }

    void Browse() {
        BROWSEINFOW bi{};
        bi.hwndOwner = hwnd;
        bi.lpszTitle = L"Select export/apply directory (AI writes to disk only when you approve).";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        if (!pidl) return;
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            SetWindowTextW(hwnd_edit, path);
        }
        CoTaskMemFree(pidl);
        UpdateOkEnabled();
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
            case WM_ERASEBKGND: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                RECT rc{}; GetClientRect(h, &rc);
                FillRect(dc, &rc, g_brush_bg);
                return 1;
            }
            case WM_CTLCOLORDLG: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkColor(dc, g_theme.bg);
                SetTextColor(dc, g_theme.text);
                return (INT_PTR)g_brush_bg;
            }
            case WM_CTLCOLORSTATIC: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, g_theme.text);
                SetBkColor(dc, g_theme.bg);
                return (INT_PTR)g_brush_bg;
            }
            case WM_CTLCOLOREDIT: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkColor(dc, g_theme.edit_bg);
                SetTextColor(dc, g_theme.text);
                return (INT_PTR)g_brush_edit;
            }
            case WM_COMMAND: {
                const int id = LOWORD(w);
                if (id == 1) { // OK
                    wchar_t buf[1024];
                    GetWindowTextW(hwnd_edit, buf, 1024);
                    result_dir = buf;
                    accepted = true;
                    DestroyWindow(h);
                    return 0;
                }
                if (id == 2) { // Cancel
                    accepted = false;
                    DestroyWindow(h);
                    return 0;
                }
                if (id == 3) { // Browse
                    Browse();
                    return 0;
                }
                if (HIWORD(w) == EN_CHANGE) {
                    UpdateOkEnabled();
                    return 0;
                }
            } break;
            case WM_CLOSE:
                accepted = false;
                DestroyWindow(h);
                return 0;
            case WM_DESTROY:
                return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    bool RunModal(HWND owner, const std::wstring& initial_dir, const EwPatchApplyReport* targets_rep) {
        ew_theme_init_once();
        WNDCLASSW wc{};
        wc.lpfnWndProc = WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"GE_AiApplyTargetDirDialog";
        wc.hbrBackground = g_brush_bg;
        RegisterClassW(&wc);

        hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME,
                               wc.lpszClassName,
                               L"Apply AI Patch",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, 560, 520,
                               owner, nullptr, wc.hInstance, this);
        if (!hwnd) return false;

        CreateWindowW(L"STATIC", L"Select a directory to apply this patch to:",
                      WS_CHILD | WS_VISIBLE,
                      16, 16, 520, 18,
                      hwnd, nullptr, wc.hInstance, nullptr);

        hwnd_edit = CreateWindowW(L"EDIT", initial_dir.c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
                                  16, 42, 420, 26,
                                  hwnd, (HMENU)10, wc.hInstance, nullptr);

        
        // Files to be written (derived from diff headers; no other AI artifacts are projected to disk here).
        hwnd_files_label = CreateWindowW(L"STATIC", L"Files to be written:",
                                         WS_CHILD | WS_VISIBLE,
                                         16, 78, 520, 18,
                                         hwnd, nullptr, wc.hInstance, nullptr);

        hwnd_list = CreateWindowW(L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                  16, 100, 528, 300,
                                  hwnd, (HMENU)20, wc.hInstance, nullptr);

        if (targets_rep && targets_rep->files_touched_u32) {
            const uint32_t show = (targets_rep->files_touched_u32 > 64u) ? 64u : targets_rep->files_touched_u32;
            for (uint32_t k = 0u; k < show; ++k) {
                SendMessageW(hwnd_list, LB_ADDSTRING, 0, (LPARAM)targets_rep->rel_paths_w[k].c_str());
            }
            if (targets_rep->files_touched_u32 > show) {
                SendMessageW(hwnd_list, LB_ADDSTRING, 0, (LPARAM)L"(more targets omitted — bounded list)");
            }
        } else {
            SendMessageW(hwnd_list, LB_ADDSTRING, 0, (LPARAM)L"(no targets detected — apply will be blocked)");
        }

CreateWindowW(L"BUTTON", L"Browse",
                      WS_CHILD | WS_VISIBLE,
                      446, 42, 88, 26,
                      hwnd, (HMENU)3, wc.hInstance, nullptr);

        hwnd_ok = CreateWindowW(L"BUTTON", L"OK",
                                WS_CHILD | WS_VISIBLE,
                                356, 96, 78, 28,
                                hwnd, (HMENU)1, wc.hInstance, nullptr);

        if (!targets_rep || targets_rep->files_touched_u32 == 0u) {
            EnableWindow(hwnd_ok, FALSE);
        }

        CreateWindowW(L"BUTTON", L"Cancel",
                      WS_CHILD | WS_VISIBLE,
                      446, 96, 88, 28,
                      hwnd, (HMENU)2, wc.hInstance, nullptr);

        ew_apply_editor_fonts(hwnd);
        UpdateOkEnabled();

        // Center on owner.
        RECT orc{}; GetWindowRect(owner, &orc);
        RECT rc{}; GetWindowRect(hwnd, &rc);
        int ow = orc.right - orc.left;
        int oh = orc.bottom - orc.top;
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        int x = orc.left + (ow - w) / 2;
        int y = orc.top + (oh - h) / 2;
        SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

        // Modal loop (non-destructive): never posts WM_QUIT.
        MSG msg;
        while (IsWindow(hwnd)) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    // Put it back for the main loop and exit.
                    PostQuitMessage((int)msg.wParam);
                    return false;
                }
                if (!IsDialogMessageW(hwnd, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            WaitMessage();
        }
        return accepted;
    }
};

// Minimal rename dialog for AI chat tabs (UI-only; bounded and deterministic).
struct EwAiRenameChatDialog {
    HWND hwnd = nullptr;
    HWND hwnd_edit = nullptr;
    std::wstring result;
    bool accepted = false;

    static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) {
            auto cs = (CREATESTRUCTW*)l;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        }
        auto* self = (EwAiRenameChatDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (self) return self->WndProc(h, m, w, l);
        return DefWindowProcW(h, m, w, l);
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
            case WM_ERASEBKGND: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                RECT rc{}; GetClientRect(h, &rc);
                FillRect(dc, &rc, g_brush_bg);
                return 1;
            }
            case WM_CTLCOLORDLG: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkColor(dc, g_theme.bg);
                SetTextColor(dc, g_theme.text);
                return (INT_PTR)g_brush_bg;
            }
            case WM_CTLCOLORSTATIC: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, g_theme.text);
                SetBkColor(dc, g_theme.bg);
                return (INT_PTR)g_brush_bg;
            }
            case WM_CTLCOLOREDIT: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkColor(dc, g_theme.edit_bg);
                SetTextColor(dc, g_theme.text);
                return (INT_PTR)g_brush_edit;
            }
            case WM_COMMAND: {
                const int id = LOWORD(w);
                if (id == 1) { // OK
                    wchar_t buf[256];
                    GetWindowTextW(hwnd_edit, buf, 256);
                    result = buf;
                    accepted = true;
                    DestroyWindow(h);
                    return 0;
                }
                if (id == 2) { // Cancel
                    accepted = false;
                    DestroyWindow(h);
                    return 0;
                }
            } break;
            case WM_CLOSE: accepted = false; DestroyWindow(h); return 0;
            default: break;
        }
        return DefWindowProcW(h, m, w, l);
    }

    static EwAiRenameChatDialog RunModal(HWND owner, const std::wstring& initial) {
        ew_theme_init_once();
        EwAiRenameChatDialog d;
        WNDCLASSW wc{};
        wc.lpfnWndProc = &EwAiRenameChatDialog::WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EwAiRenameChatDialog";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);

        d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"Rename Chat",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 380, 160,
                                 owner, nullptr, wc.hInstance, &d);

        CreateWindowW(L"STATIC", L"New name:", WS_CHILD | WS_VISIBLE,
                      12, 18, 120, 18, d.hwnd, nullptr, wc.hInstance, nullptr);

        d.hwnd_edit = CreateWindowW(L"EDIT", initial.c_str(),
                                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                    12, 40, 340, 24, d.hwnd, (HMENU)10, wc.hInstance, nullptr);

        CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE,
                      196, 86, 74, 28, d.hwnd, (HMENU)1, wc.hInstance, nullptr);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                      278, 86, 74, 28, d.hwnd, (HMENU)2, wc.hInstance, nullptr);

        ew_apply_editor_fonts(d.hwnd);
        ShowWindow(d.hwnd, SW_SHOW);
        SetForegroundWindow(d.hwnd);
        SetFocus(d.hwnd_edit);
        SendMessageW(d.hwnd_edit, EM_SETSEL, 0, -1);

        // Modal loop.
        MSG msg{};
        while (IsWindow(d.hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(d.hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        return d;
    }
};


// Minimal single-line text input dialog (UI-only; bounded and deterministic).
// Used for coherence search query prompt.
struct EwAiTextInputDialog {
    HWND hwnd = nullptr;
    HWND hwnd_edit = nullptr;
    std::wstring result;
    bool accepted = false;

    static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) {
            auto cs = (CREATESTRUCTW*)l;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        }
        auto* self = (EwAiTextInputDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (self) return self->WndProc(h, m, w, l);
        return DefWindowProcW(h, m, w, l);
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
            case WM_ERASEBKGND: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                RECT rc{}; GetClientRect(h, &rc);
                FillRect(dc, &rc, g_brush_bg);
                return 1;
            }
            case WM_CTLCOLORDLG: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkColor(dc, g_theme.bg);
                SetTextColor(dc, g_theme.text);
                return (INT_PTR)g_brush_bg;
            }
            case WM_CTLCOLORSTATIC: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, g_theme.text);
                SetBkColor(dc, g_theme.bg);
                return (INT_PTR)g_brush_bg;
            }
            case WM_CTLCOLOREDIT: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkColor(dc, g_theme.edit_bg);
                SetTextColor(dc, g_theme.text);
                return (INT_PTR)g_brush_edit;
            }
            case WM_COMMAND: {
                const int id = LOWORD(w);
                if (id == 1) { // OK
                    wchar_t buf[1024];
                    GetWindowTextW(hwnd_edit, buf, 1024);
                    result = buf;
                    accepted = true;
                    DestroyWindow(h);
                    return 0;
                }
                if (id == 2) { // Cancel
                    accepted = false;
                    DestroyWindow(h);
                    return 0;
                }
            } break;
            case WM_CLOSE: accepted = false; DestroyWindow(h); return 0;
            default: break;
        }
        return DefWindowProcW(h, m, w, l);
    }

    static EwAiTextInputDialog RunModal(HWND owner, const wchar_t* title, const wchar_t* prompt, const std::wstring& initial) {
        ew_theme_init_once();
        EwAiTextInputDialog d;
        WNDCLASSW wc{};
        wc.lpfnWndProc = &EwAiTextInputDialog::WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EwAiTextInputDialog";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);

        d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, title ? title : L"Input",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 520, 190,
                                 owner, nullptr, wc.hInstance, &d);

        CreateWindowW(L"STATIC", prompt ? prompt : L"Enter text:", WS_CHILD | WS_VISIBLE,
                      12, 18, 480, 18, d.hwnd, nullptr, wc.hInstance, nullptr);

        d.hwnd_edit = CreateWindowW(L"EDIT", initial.c_str(),
                                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                    12, 44, 484, 24, d.hwnd, (HMENU)10, wc.hInstance, nullptr);

        CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE,
                      338, 104, 74, 28, d.hwnd, (HMENU)1, wc.hInstance, nullptr);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                      422, 104, 74, 28, d.hwnd, (HMENU)2, wc.hInstance, nullptr);

        ew_apply_editor_fonts(d.hwnd);
        ShowWindow(d.hwnd, SW_SHOW);
        SetForegroundWindow(d.hwnd);
        SetFocus(d.hwnd_edit);
        SendMessageW(d.hwnd_edit, EM_SETSEL, 0, -1);

        MSG msg{};
        while (IsWindow(d.hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(d.hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        return d;
    }
};


// Two-field input dialog (hook point for richer multi-step forms).
// Used for bounded coherence rename planning UI.
struct EwAiTwoTextInputDialog {
    HWND hwnd = nullptr;
    HWND hwnd_edit_a = nullptr;
    HWND hwnd_edit_b = nullptr;
    std::wstring result_a;
    std::wstring result_b;
    bool accepted = false;

    static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) {
            auto cs = (CREATESTRUCTW*)l;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        }
        auto* self = (EwAiTwoTextInputDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (self) return self->WndProc(h, m, w, l);
        return DefWindowProcW(h, m, w, l);
    }

    LRESULT WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
            case WM_ERASEBKGND: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                RECT rc{}; GetClientRect(h, &rc);
                FillRect(dc, &rc, g_brush_bg);
                return 1;
            }
            case WM_CTLCOLORDLG: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkColor(dc, g_theme.bg);
                SetTextColor(dc, g_theme.text);
                return (INT_PTR)g_brush_bg;
            }
            case WM_CTLCOLORSTATIC: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, g_theme.text);
                SetBkColor(dc, g_theme.bg);
                return (INT_PTR)g_brush_bg;
            }
            case WM_CTLCOLOREDIT: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkColor(dc, g_theme.edit_bg);
                SetTextColor(dc, g_theme.text);
                return (INT_PTR)g_brush_edit;
            }
            case WM_COMMAND: {
                const int id = LOWORD(w);
                if (id == 1) { // OK
                    wchar_t buf_a[512];
                    wchar_t buf_b[512];
                    GetWindowTextW(hwnd_edit_a, buf_a, 512);
                    GetWindowTextW(hwnd_edit_b, buf_b, 512);
                    result_a = buf_a;
                    result_b = buf_b;
                    accepted = true;
                    DestroyWindow(h);
                    return 0;
                }
                if (id == 2) { // Cancel
                    accepted = false;
                    DestroyWindow(h);
                    return 0;
                }
            } break;
            case WM_CLOSE: accepted = false; DestroyWindow(h); return 0;
            default: break;
        }
        return DefWindowProcW(h, m, w, l);
    }

    static EwAiTwoTextInputDialog RunModal(HWND owner,
                                          const wchar_t* title,
                                          const wchar_t* prompt_a,
                                          const wchar_t* prompt_b,
                                          const std::wstring& initial_a,
                                          const std::wstring& initial_b) {
        ew_theme_init_once();
        EwAiTwoTextInputDialog d;
        WNDCLASSW wc{};
        wc.lpfnWndProc = &EwAiTwoTextInputDialog::WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EwAiTwoTextInputDialog";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);

        d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, title ? title : L"Input",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 560, 250,
                                 owner, nullptr, wc.hInstance, &d);

        CreateWindowW(L"STATIC", prompt_a ? prompt_a : L"Field A:", WS_CHILD | WS_VISIBLE,
                      12, 18, 520, 18, d.hwnd, nullptr, wc.hInstance, nullptr);

        d.hwnd_edit_a = CreateWindowW(L"EDIT", initial_a.c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                      12, 42, 524, 24, d.hwnd, (HMENU)10, wc.hInstance, nullptr);

        CreateWindowW(L"STATIC", prompt_b ? prompt_b : L"Field B:", WS_CHILD | WS_VISIBLE,
                      12, 76, 520, 18, d.hwnd, nullptr, wc.hInstance, nullptr);

        d.hwnd_edit_b = CreateWindowW(L"EDIT", initial_b.c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                      12, 100, 524, 24, d.hwnd, (HMENU)11, wc.hInstance, nullptr);

        CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE,
                      378, 160, 74, 28, d.hwnd, (HMENU)1, wc.hInstance, nullptr);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                      462, 160, 74, 28, d.hwnd, (HMENU)2, wc.hInstance, nullptr);

        ew_apply_editor_fonts(d.hwnd);
        ShowWindow(d.hwnd, SW_SHOW);
        SetForegroundWindow(d.hwnd);
        SetFocus(d.hwnd_edit_a);
        SendMessageW(d.hwnd_edit_a, EM_SETSEL, 0, -1);

        MSG msg{};
        while (IsWindow(d.hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(d.hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(d.hwnd, &msg);
            }
        }
        return d;
    }
};



// Dedicated coherence tools dialog (GUI-only; keeps chat conversational while
// moving coherence inspection/planning into a real editor surface).
struct EwAiCoherenceToolsDialog {
    static HWND& shared_hwnd() { static HWND s = nullptr; return s; }
    std::wstring status_w;
    std::wstring results_w;
    std::wstring patch_w;
    HWND hwnd = nullptr;
    HWND hwnd_query = nullptr;
    HWND hwnd_old = nullptr;
    HWND hwnd_new = nullptr;
    HWND hwnd_results = nullptr;
    HWND hwnd_patch = nullptr;
    HWND hwnd_status = nullptr;
    Scene* scene = nullptr;

    static std::wstring trim_copy(std::wstring s) {
        while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r' || s.back() == L'\n')) s.pop_back();
        size_t i0 = 0;
        while (i0 < s.size() && (s[i0] == L' ' || s[i0] == L'\t' || s[i0] == L'\r' || s[i0] == L'\n')) ++i0;
        if (i0 != 0) s = s.substr(i0);
        return s;
    }

    static std::string wide_to_ascii_query(const std::wstring& w) {
        std::string out;
        out.reserve(w.size());
        for (size_t i = 0; i < w.size(); ++i) {
            wchar_t wc = w[i];
            if (wc >= 32 && wc < 127) out.push_back((char)wc);
            else out.push_back(' ');
        }
        return out;
    }

    static std::string wide_to_ascii_ident(const std::wstring& w) {
        std::string out;
        out.reserve(w.size());
        for (size_t i = 0; i < w.size(); ++i) {
            wchar_t wc = w[i];
            if ((wc >= L'a' && wc <= L'z') || (wc >= L'A' && wc <= L'Z') || (wc >= L'0' && wc <= L'9') || wc == L'_') {
                out.push_back((char)wc);
            } else if (wc >= 32 && wc < 127) {
                out.push_back('_');
            } else {
                out.push_back('_');
            }
        }
        return out;
    }

    std::wstring get_text(HWND ctrl) const {
        wchar_t buf[1024];
        buf[0] = 0;
        if (ctrl) GetWindowTextW(ctrl, buf, 1024);
        return trim_copy(std::wstring(buf));
    }

    void set_status(const std::wstring& w) {
        status_w = w;
        if (hwnd_status) SetWindowTextW(hwnd_status, status_w.c_str());
    }

    void set_results(const std::wstring& w) {
        results_w = w;
        if (hwnd_results) SetWindowTextW(hwnd_results, results_w.c_str());
    }

    void set_patch(const std::wstring& w) {
        patch_w = w;
        if (hwnd_patch) SetWindowTextW(hwnd_patch, patch_w.c_str());
    }

    void do_stats() {
        if (!scene) return;
        std::string stats;
        if (scene->SnapshotCoherenceStats(stats)) {
            set_status(L"Coherence stats ready");
            set_results(utf8_to_wide(std::string("stats: ") + stats));
        } else {
            set_status(L"Coherence stats unavailable");
            set_results(L"");
        }
    }

    void do_query(bool highlight_only) {
        if (!scene) return;
        std::wstring qw = get_text(hwnd_query);
        if (qw.empty()) {
            set_status(L"Enter a coherence query first");
            if (!highlight_only) set_results(L"");
            return;
        }
        const std::string qs = wide_to_ascii_query(qw);
        if (highlight_only) {
            scene->SetCoherenceHighlightQuery(qs, 32u);
            set_status(std::wstring(L"Coherence highlight set: ") + qw);
            return;
        }
        std::vector<genesis::GeCoherenceHit> hits;
        std::string err;
        if (!scene->SnapshotCoherenceQuery(qs, 32u, hits, &err)) {
            set_status(std::wstring(L"Coherence search failed: ") + utf8_to_wide(err));
            set_results(L"");
            return;
        }
        std::wstring out = L"query: " + qw + L"\r\n";
        out += L"hits: " + std::to_wstring((unsigned long long)hits.size());
        for (size_t i = 0; i < hits.size(); ++i) {
            out += L"\r\n" + std::to_wstring((unsigned long long)i) + L": score=" +
                   std::to_wstring((unsigned long long)hits[i].score_u32) +
                   L" path=" + utf8_to_wide(hits[i].rel_path_utf8);
        }
        set_status(L"Coherence search complete");
        set_results(out);
    }

    void do_rename_plan() {
        if (!scene) return;
        std::wstring oldw = get_text(hwnd_old);
        std::wstring neww = get_text(hwnd_new);
        if (oldw.empty() || neww.empty()) {
            set_status(L"Enter old and new identifiers first");
            set_results(L"");
            return;
        }
        std::vector<genesis::GeCoherenceHit> hits;
        std::string err;
        const std::string olda = wide_to_ascii_ident(oldw);
        const std::string newa = wide_to_ascii_ident(neww);
        if (!scene->SnapshotCoherenceRenamePlan(olda, newa, 64u, hits, &err)) {
            set_status(std::wstring(L"Rename plan failed: ") + utf8_to_wide(err));
            set_results(L"");
            return;
        }
        std::wstring out = L"rename plan: " + oldw + L" -> " + neww + L"\r\n";
        out += L"hits: " + std::to_wstring((unsigned long long)hits.size());
        for (size_t i = 0; i < hits.size(); ++i) {
            out += L"\r\n" + std::to_wstring((unsigned long long)i) + L": score=" +
                   std::to_wstring((unsigned long long)hits[i].score_u32) +
                   L" path=" + utf8_to_wide(hits[i].rel_path_utf8);
        }
        set_status(L"Rename plan ready");
        set_results(out);
    }

    void do_rename_patch() {
        if (!scene) return;
        std::wstring oldw = get_text(hwnd_old);
        std::wstring neww = get_text(hwnd_new);
        if (oldw.empty() || neww.empty()) {
            set_status(L"Enter old and new identifiers first");
            set_patch(L"");
            return;
        }
        std::string patch_utf8;
        std::string err;
        const std::string olda = wide_to_ascii_ident(oldw);
        const std::string newa = wide_to_ascii_ident(neww);
        if (!scene->SnapshotCoherenceRenamePatch(olda, newa, 64u, patch_utf8, &err)) {
            set_status(std::wstring(L"Rename patch failed: ") + utf8_to_wide(err));
            set_patch(L"");
            return;
        }
        set_status(L"Rename patch prepared (copy or preview elsewhere before apply)");
        set_patch(utf8_to_wide(patch_utf8));
    }

    void do_selftest() {
        if (!scene) return;
        bool ok = false;
        std::string rep;
        if (!scene->SnapshotCoherenceSelftest(ok, rep)) {
            set_status(L"Coherence selftest unavailable");
            set_results(L"");
            return;
        }
        set_status(std::wstring(L"Coherence selftest: ") + (ok ? L"ok" : L"fail"));
        set_results(utf8_to_wide(rep));
    }

    void copy_patch() {
        if (patch_w.empty()) { set_status(L"No patch prepared yet"); return; }
        if (!OpenClipboard(hwnd)) return;
        EmptyClipboard();
        size_t bytes = (patch_w.size() + 1u) * sizeof(wchar_t);
        HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hmem) {
            void* p = GlobalLock(hmem);
            if (p) {
                memcpy(p, patch_w.c_str(), bytes);
                GlobalUnlock(hmem);
                SetClipboardData(CF_UNICODETEXT, hmem);
                hmem = nullptr;
            }
        }
        if (hmem) GlobalFree(hmem);
        CloseClipboard();
        set_status(L"Coherence patch copied to clipboard");
    }

    static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = (EwAiCoherenceToolsDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (m == WM_NCCREATE) {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
            self = (EwAiCoherenceToolsDialog*)cs->lpCreateParams;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
            if (self) self->hwnd = h;
        }
        if (!self) return DefWindowProcW(h, m, w, l);
        switch (m) {
            case WM_CREATE: {
                HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                CreateWindowW(L"STATIC", L"Query", WS_CHILD|WS_VISIBLE, 12, 12, 48, 18, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_query = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
                                                   12, 32, 250, 24, h, (HMENU)41, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"BUTTON", L"Search", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 272, 30, 86, 28, h, (HMENU)42, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"BUTTON", L"Highlight Query", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 366, 30, 118, 28, h, (HMENU)43, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"BUTTON", L"Stats", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 492, 30, 74, 28, h, (HMENU)44, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"BUTTON", L"Selftest", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 574, 30, 92, 28, h, (HMENU)45, GetModuleHandleW(nullptr), nullptr);

                CreateWindowW(L"STATIC", L"Old Identifier", WS_CHILD|WS_VISIBLE, 12, 72, 96, 18, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_old = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
                                                 12, 92, 220, 24, h, (HMENU)46, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"STATIC", L"New Identifier", WS_CHILD|WS_VISIBLE, 244, 72, 96, 18, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_new = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
                                                 244, 92, 220, 24, h, (HMENU)47, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"BUTTON", L"Rename Plan", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 476, 90, 96, 28, h, (HMENU)48, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"BUTTON", L"Prepare Patch", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 580, 90, 96, 28, h, (HMENU)49, GetModuleHandleW(nullptr), nullptr);

                CreateWindowW(L"STATIC", L"Results", WS_CHILD|WS_VISIBLE, 12, 132, 60, 18, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_results = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,
                                                     12, 152, 330, 220, h, (HMENU)50, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"STATIC", L"Patch Preview", WS_CHILD|WS_VISIBLE, 354, 132, 84, 18, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_patch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,
                                                   354, 152, 322, 220, h, (HMENU)51, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_status = CreateWindowW(L"STATIC", L"Ready", WS_CHILD|WS_VISIBLE, 12, 382, 472, 20, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"BUTTON", L"Copy Patch", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 490, 376, 90, 28, h, (HMENU)52, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"BUTTON", L"Close", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 586, 376, 90, 28, h, (HMENU)53, GetModuleHandleW(nullptr), nullptr);

                const int ids[] = {41,42,43,44,45,46,47,48,49,50,51,52,53};
                for (int i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); ++i) {
                    HWND c = GetDlgItem(h, ids[i]);
                    if (c) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
                }
                if (self->hwnd_status) SendMessageW(self->hwnd_status, WM_SETFONT, (WPARAM)font, TRUE);
                self->set_status(L"Ready");
                return 0;
            }
            case WM_COMMAND: {
                const int id = LOWORD(w);
                const int code = HIWORD(w);
                if (code == BN_CLICKED) {
                    if (id == 42) { self->do_query(false); return 0; }
                    if (id == 43) { self->do_query(true); return 0; }
                    if (id == 44) { self->do_stats(); return 0; }
                    if (id == 45) { self->do_selftest(); return 0; }
                    if (id == 48) { self->do_rename_plan(); return 0; }
                    if (id == 49) { self->do_rename_patch(); return 0; }
                    if (id == 52) { self->copy_patch(); return 0; }
                    if (id == 53) { DestroyWindow(h); return 0; }
                }
                return 0;
            }
            case WM_CLOSE: DestroyWindow(h); return 0;
            case WM_NCDESTROY: { if (shared_hwnd() == h) shared_hwnd() = nullptr; delete self; return 0; }
            case WM_DESTROY: return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
    static void OpenModeless(HWND owner, Scene* scene) {
        if (shared_hwnd() && IsWindow(shared_hwnd())) {
            ShowWindow(shared_hwnd(), SW_SHOW);
            SetForegroundWindow(shared_hwnd());
            return;
        }
        auto* d = new EwAiCoherenceToolsDialog();
        d->scene = scene;
        WNDCLASSW wc{};
        wc.lpfnWndProc = &EwAiCoherenceToolsDialog::WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EwAiCoherenceToolsDialog";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Coherence Tools",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 712, 438,
                                 owner, nullptr, wc.hInstance, d);
        shared_hwnd() = h;
        if (!h) delete d;
    }
};

// Read-only diff preview dialog (bounded).

struct EwAiRepoBrowserDialog {
    static HWND& shared_hwnd() { static HWND s = nullptr; return s; }

    std::wstring status_w;
    std::wstring preview_w;
    std::wstring coherence_w;
    std::vector<std::string> rels_utf8;
    HWND hwnd = nullptr;
    HWND hwnd_list = nullptr;
    HWND hwnd_preview = nullptr;
    HWND hwnd_status = nullptr;
    HWND hwnd_coherence = nullptr;
    HWND hwnd_btn_refresh = nullptr;
    HWND hwnd_btn_copy = nullptr;
    HWND hwnd_btn_highlight = nullptr;
    Scene* scene = nullptr;

    static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = (EwAiRepoBrowserDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (m == WM_NCCREATE) {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
            self = (EwAiRepoBrowserDialog*)cs->lpCreateParams;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
            if (self) self->hwnd = h;
        }
        if (!self) return DefWindowProcW(h, m, w, l);
        switch (m) {
            case WM_CREATE: {
                HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                CreateWindowW(L"STATIC", L"Repository Files", WS_CHILD|WS_VISIBLE, 12, 10, 160, 18, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD|WS_VISIBLE|LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP,
                                                  12, 30, 240, 300, h, (HMENU)31, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"STATIC", L"Preview", WS_CHILD|WS_VISIBLE, 264, 10, 120, 18, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_preview = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,
                                                     264, 30, 420, 190, h, (HMENU)32, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"STATIC", L"Coherence", WS_CHILD|WS_VISIBLE, 264, 226, 120, 18, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_coherence = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,
                                                       264, 246, 420, 84, h, (HMENU)36, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_status = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_VISIBLE, 12, 338, 560, 20, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_btn_refresh = CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 12, 366, 90, 28, h, (HMENU)33, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_btn_copy = CreateWindowW(L"BUTTON", L"Copy Preview", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 110, 366, 110, 28, h, (HMENU)34, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_btn_highlight = CreateWindowW(L"BUTTON", L"Highlight in Coherence", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 228, 366, 150, 28, h, (HMENU)37, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"BUTTON", L"Close", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 594, 366, 90, 28, h, (HMENU)35, GetModuleHandleW(nullptr), nullptr);
                for (HWND c : {self->hwnd_list, self->hwnd_preview, self->hwnd_coherence, self->hwnd_status, self->hwnd_btn_refresh, self->hwnd_btn_copy, self->hwnd_btn_highlight}) {
                    if (c) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
                }
                self->Refresh();
                return 0;
            }
            case WM_COMMAND: {
                int id = LOWORD(w); int code = HIWORD(w);
                if (id == 31 && code == LBN_SELCHANGE) { self->LoadSelectedPreview(); return 0; }
                if (id == 33 && code == BN_CLICKED) { self->Refresh(); return 0; }
                if (id == 34 && code == BN_CLICKED) { self->CopyPreview(); return 0; }
                if (id == 37 && code == BN_CLICKED) { self->HighlightSelected(); return 0; }
                if (id == 35 && code == BN_CLICKED) { DestroyWindow(h); return 0; }
                return 0;
            }
            case WM_CLOSE: DestroyWindow(h); return 0;
            case WM_NCDESTROY: { if (shared_hwnd() == h) shared_hwnd() = nullptr; delete self; return 0; }
            case WM_DESTROY: return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    void Refresh() {
        if (!scene || !hwnd_list) return;
        std::string err, status;
        rels_utf8.clear();
        coherence_w.clear();
        SendMessageW(hwnd_list, LB_RESETCONTENT, 0, 0);
        scene->RescanRepoReader();
        (void)scene->SnapshotRepoReaderStatus(status);
        status_w = utf8_to_wide(status);
        if (!scene->SnapshotRepoReaderFiles(128u, rels_utf8, &err)) {
            SetWindowTextW(hwnd_status, (L"Repository unavailable: " + utf8_to_wide(err)).c_str());
            SetWindowTextW(hwnd_preview, L"");
            SetWindowTextW(hwnd_coherence, L"");
            return;
        }
        for (const auto& rel : rels_utf8) {
            SendMessageW(hwnd_list, LB_ADDSTRING, 0, (LPARAM)utf8_to_wide(rel).c_str());
        }
        std::wstring status_line = status_w + L" | files shown: " + std::to_wstring((unsigned long long)rels_utf8.size());
        SetWindowTextW(hwnd_status, status_line.c_str());
        if (!rels_utf8.empty()) {
            SendMessageW(hwnd_list, LB_SETCURSEL, 0, 0);
            LoadSelectedPreview();
        } else {
            SetWindowTextW(hwnd_preview, L"");
            SetWindowTextW(hwnd_coherence, L"");
        }
    }

    void LoadSelectedPreview() {
        if (!scene || !hwnd_list) return;
        int sel = (int)SendMessageW(hwnd_list, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR || sel < 0 || (size_t)sel >= rels_utf8.size()) {
            SetWindowTextW(hwnd_preview, L"");
            SetWindowTextW(hwnd_coherence, L"");
            return;
        }
        std::string preview, err;
        if (!scene->SnapshotRepoFilePreview(rels_utf8[(size_t)sel], 16384u, preview, &err)) {
            preview_w = L"Preview unavailable: " + utf8_to_wide(err);
        } else {
            preview_w = utf8_to_wide(preview);
        }
        SetWindowTextW(hwnd_preview, preview_w.c_str());
        LoadSelectedCoherence();
        SetWindowTextW(hwnd_status, (utf8_to_wide(rels_utf8[(size_t)sel]) + L" | " + status_w).c_str());
    }

    void LoadSelectedCoherence() {
        coherence_w.clear();
        if (!scene || !hwnd_list) return;
        int sel = (int)SendMessageW(hwnd_list, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR || sel < 0 || (size_t)sel >= rels_utf8.size()) {
            SetWindowTextW(hwnd_coherence, L"");
            return;
        }
        std::vector<genesis::GeCoherenceHit> hits;
        std::string err;
        const std::string& rel = rels_utf8[(size_t)sel];
        if (!scene->SnapshotRepoFileCoherenceHits(rel, 8u, hits, &err)) {
            coherence_w = L"Coherence unavailable: " + utf8_to_wide(err);
        } else {
            coherence_w = L"Selected file: " + utf8_to_wide(rel);
            coherence_w += L"\r\nMatches: " + std::to_wstring((unsigned long long)hits.size());
            for (size_t i = 0; i < hits.size(); ++i) {
                coherence_w += L"\r\n" + std::to_wstring((unsigned long long)i) + L": score=" + std::to_wstring((unsigned long long)hits[i].score_u32) + L" path=" + utf8_to_wide(hits[i].rel_path_utf8);
            }
        }
        SetWindowTextW(hwnd_coherence, coherence_w.c_str());
    }

    void HighlightSelected() {
        if (!scene || !hwnd_list) return;
        int sel = (int)SendMessageW(hwnd_list, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR || sel < 0 || (size_t)sel >= rels_utf8.size()) return;
        scene->SetCoherenceHighlightPath(rels_utf8[(size_t)sel]);
        SetWindowTextW(hwnd_status, (L"Coherence highlight set: " + utf8_to_wide(rels_utf8[(size_t)sel])).c_str());
    }

    void CopyPreview() {
        if (preview_w.empty()) return;
        if (!OpenClipboard(hwnd)) return;
        EmptyClipboard();
        size_t bytes = (preview_w.size() + 1u) * sizeof(wchar_t);
        HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hmem) {
            void* p = GlobalLock(hmem);
            if (p) {
                memcpy(p, preview_w.c_str(), bytes);
                GlobalUnlock(hmem);
                SetClipboardData(CF_UNICODETEXT, hmem);
                hmem = nullptr;
            }
        }
        if (hmem) GlobalFree(hmem);
        CloseClipboard();
        SetWindowTextW(hwnd_status, L"Repository preview copied to clipboard");
    }
    static void OpenModeless(HWND owner, Scene* scene) {
        if (shared_hwnd() && IsWindow(shared_hwnd())) {
            ShowWindow(shared_hwnd(), SW_SHOW);
            SetForegroundWindow(shared_hwnd());
            return;
        }
        auto* d = new EwAiRepoBrowserDialog();
        d->scene = scene;
        WNDCLASSW wc{};
        wc.lpfnWndProc = &EwAiRepoBrowserDialog::WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EwAiRepoBrowserDialog";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Repository Browser",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 712, 438,
                                 owner, nullptr, wc.hInstance, d);
        shared_hwnd() = h;
        if (!h) delete d;
    }
};

struct EwAiVaultBrowserDialog {
    bool accepted = false;
    bool import_requested = false;
    std::wstring imported_path;
    std::vector<std::string> rels_utf8;
    std::wstring preview_w;
    int selected_index = -1;
    HWND hwnd = nullptr;
    HWND hwnd_list = nullptr;
    HWND hwnd_preview = nullptr;
    HWND hwnd_status = nullptr;
    HWND hwnd_btn_import = nullptr;
    HWND hwnd_btn_copy = nullptr;
    HWND hwnd_btn_refresh = nullptr;
    Scene* scene = nullptr;

    static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = (EwAiVaultBrowserDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (m == WM_NCCREATE) {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
            self = (EwAiVaultBrowserDialog*)cs->lpCreateParams;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
            if (self) self->hwnd = h;
        }
        if (!self) return DefWindowProcW(h, m, w, l);
        switch (m) {
            case WM_CREATE: {
                HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                CreateWindowW(L"STATIC", L"Vault Entries", WS_CHILD|WS_VISIBLE, 12, 10, 160, 18, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD|WS_VISIBLE|LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP,
                                                  12, 30, 240, 300, h, (HMENU)1, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"STATIC", L"Preview", WS_CHILD|WS_VISIBLE, 264, 10, 120, 18, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_preview = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,
                                                     264, 30, 420, 300, h, (HMENU)2, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_status = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_VISIBLE, 12, 338, 500, 20, h, nullptr, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_btn_refresh = CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 12, 366, 90, 28, h, (HMENU)10, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_btn_copy = CreateWindowW(L"BUTTON", L"Copy Preview", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 110, 366, 110, 28, h, (HMENU)11, GetModuleHandleW(nullptr), nullptr);
                self->hwnd_btn_import = CreateWindowW(L"BUTTON", L"Import Handle", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 228, 366, 120, 28, h, (HMENU)12, GetModuleHandleW(nullptr), nullptr);
                CreateWindowW(L"BUTTON", L"Close", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 594, 366, 90, 28, h, (HMENU)13, GetModuleHandleW(nullptr), nullptr);
                for (HWND c : {self->hwnd_list, self->hwnd_preview, self->hwnd_status, self->hwnd_btn_refresh, self->hwnd_btn_copy, self->hwnd_btn_import}) {
                    if (c) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
                }
                self->Refresh();
                return 0;
            }
            case WM_COMMAND: {
                int id = LOWORD(w); int code = HIWORD(w);
                if (id == 1 && code == LBN_SELCHANGE) {
                    self->LoadSelectedPreview();
                    return 0;
                }
                if (id == 10 && code == BN_CLICKED) { self->Refresh(); return 0; }
                if (id == 11 && code == BN_CLICKED) { self->CopyPreview(); return 0; }
                if (id == 12 && code == BN_CLICKED) { self->ImportSelected(); return 0; }
                if (id == 13 && code == BN_CLICKED) { DestroyWindow(h); return 0; }
                return 0;
            }
            case WM_CLOSE: DestroyWindow(h); return 0;
            case WM_DESTROY: return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    void Refresh() {
        if (!scene || !hwnd_list) return;
        std::string err;
        rels_utf8.clear();
        SendMessageW(hwnd_list, LB_RESETCONTENT, 0, 0);
        if (!scene->SnapshotVaultEntries(64u, rels_utf8, &err)) {
            SetWindowTextW(hwnd_status, (L"Vault unavailable: " + utf8_to_wide(err)).c_str());
            SetWindowTextW(hwnd_preview, L"");
            return;
        }
        for (const auto& rel : rels_utf8) {
            SendMessageW(hwnd_list, LB_ADDSTRING, 0, (LPARAM)utf8_to_wide(rel).c_str());
        }
        std::wstring status = L"Entries: " + std::to_wstring((unsigned long long)rels_utf8.size());
        SetWindowTextW(hwnd_status, status.c_str());
        if (!rels_utf8.empty()) {
            SendMessageW(hwnd_list, LB_SETCURSEL, 0, 0);
            LoadSelectedPreview();
        } else {
            SetWindowTextW(hwnd_preview, L"");
        }
    }

    void LoadSelectedPreview() {
        if (!scene || !hwnd_list) return;
        int sel = (int)SendMessageW(hwnd_list, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR || sel < 0 || (size_t)sel >= rels_utf8.size()) {
            selected_index = -1;
            SetWindowTextW(hwnd_preview, L"");
            return;
        }
        selected_index = sel;
        std::string preview, err;
        if (!scene->SnapshotVaultEntryPreview(rels_utf8[(size_t)sel], 8192u, preview, &err)) {
            preview_w = L"Preview unavailable: " + utf8_to_wide(err);
        } else {
            preview_w = utf8_to_wide(preview);
        }
        SetWindowTextW(hwnd_preview, preview_w.c_str());
        SetWindowTextW(hwnd_status, utf8_to_wide(rels_utf8[(size_t)sel]).c_str());
    }

    void CopyPreview() {
        if (preview_w.empty()) return;
        if (!OpenClipboard(hwnd)) return;
        EmptyClipboard();
        size_t bytes = (preview_w.size() + 1u) * sizeof(wchar_t);
        HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hmem) {
            void* p = GlobalLock(hmem);
            if (p) {
                memcpy(p, preview_w.c_str(), bytes);
                GlobalUnlock(hmem);
                SetClipboardData(CF_UNICODETEXT, hmem);
                hmem = nullptr;
            }
        }
        if (hmem) GlobalFree(hmem);
        CloseClipboard();
        SetWindowTextW(hwnd_status, L"Preview copied to clipboard");
    }

    void ImportSelected() {
        if (!scene || selected_index < 0 || (size_t)selected_index >= rels_utf8.size()) return;
        std::string out_path, err;
        if (scene->ImportVaultEntry(rels_utf8[(size_t)selected_index], out_path, &err)) {
            import_requested = true;
            imported_path = utf8_to_wide(out_path);
            SetWindowTextW(hwnd_status, (L"Imported handle: " + imported_path).c_str());
        } else {
            SetWindowTextW(hwnd_status, (L"Import failed: " + utf8_to_wide(err)).c_str());
        }
    }

    static EwAiVaultBrowserDialog RunModal(HWND owner, Scene* scene) {
        EwAiVaultBrowserDialog d;
        d.scene = scene;
        WNDCLASSW wc{};
        wc.lpfnWndProc = &EwAiVaultBrowserDialog::WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EwAiVaultBrowserDialog";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"AI Vault Browser",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 712, 438,
                                 owner, nullptr, wc.hInstance, &d);
        ShowWindow(h, SW_SHOW);
        EnableWindow(owner, FALSE);
        MSG msg;
        while (IsWindow(h) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(h, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
        return d;
    }
};

struct EwAiPatchPreviewDialog {
    HWND hwnd = nullptr;
    HWND hwnd_edit = nullptr;

    static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
        EwAiPatchPreviewDialog* self = (EwAiPatchPreviewDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (m == WM_NCCREATE) {
            auto* cs = (CREATESTRUCTW*)l;
            self = (EwAiPatchPreviewDialog*)cs->lpCreateParams;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        }
        if (!self) return DefWindowProcW(h, m, w, l);

        switch (m) {
            case WM_SIZE: {
                RECT rc{}; GetClientRect(h, &rc);
                const int pad = 16;
                const int btn_w = 86;
                const int btn_h = 28;

                const int W = (rc.right - rc.left);
                const int H = (rc.bottom - rc.top);

                // Row 1: dir edit + browse
                const int edit_h = 26;
                const int browse_w = 78;
                const int x0 = pad;
                const int y0 = 42;
                const int edit_w = (W - 3*pad - browse_w);
                if (self->hwnd_edit) MoveWindow(self->hwnd_edit, x0, y0, (edit_w > 80 ? edit_w : 80), edit_h, TRUE);
                HWND btn_browse = GetDlgItem(h, 3);
                if (btn_browse) MoveWindow(btn_browse, x0 + (edit_w > 80 ? edit_w : 80) + pad, y0, browse_w, edit_h, TRUE);

                // Targets list
                const int list_y = 100;
                const int list_h = H - list_y - (pad + btn_h + pad);
                if (self->hwnd_files_label) MoveWindow(self->hwnd_files_label, x0, 78, W - 2*pad, 18, TRUE);
                if (self->hwnd_list) MoveWindow(self->hwnd_list, x0, list_y, W - 2*pad, (list_h > 80 ? list_h : 80), TRUE);

                // Buttons
                HWND btn_ok = GetDlgItem(h, 1);
                HWND btn_cancel = GetDlgItem(h, 2);
                const int by = H - pad - btn_h;
                if (btn_cancel) MoveWindow(btn_cancel, W - pad - btn_w, by, btn_w, btn_h, TRUE);
                if (btn_ok) MoveWindow(btn_ok, W - pad - btn_w - pad - btn_w, by, btn_w, btn_h, TRUE);
                return 0;
            }
            case WM_ERASEBKGND: {
                HDC dc = (HDC)w;
                RECT rc{}; GetClientRect(h, &rc);
                FillRect(dc, &rc, g_brush_bg);
                return 1;
            }
            case WM_CTLCOLORDLG: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkColor(dc, g_theme.bg);
                SetTextColor(dc, g_theme.text);
                return (INT_PTR)g_brush_bg;
            }
            case WM_CTLCOLOREDIT: {
                ew_theme_init_once();
                HDC dc = (HDC)w;
                SetBkColor(dc, g_theme.edit_bg);
                SetTextColor(dc, g_theme.text);
                return (INT_PTR)g_brush_edit;
            }
            case WM_COMMAND: {
                if (LOWORD(w) == 1) { DestroyWindow(h); return 0; } // Close
            } break;
            case WM_CLOSE: DestroyWindow(h); return 0;
            default: break;
        }
        return DefWindowProcW(h, m, w, l);
    }

    static void RunModal(HWND owner, const std::wstring& patch_w, const std::wstring& meta_w) {
        ew_theme_init_once();
        WNDCLASSW wc{};
        wc.lpfnWndProc = &EwAiPatchPreviewDialog::WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EwAiPatchPreviewDialog";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);

        EwAiPatchPreviewDialog d;
        d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"Preview Diff",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 860, 640,
                                 owner, nullptr, wc.hInstance, &d);

        d.hwnd_edit = CreateWindowW(L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                                    WS_VSCROLL | WS_HSCROLL | ES_READONLY,
                                    10, 10, 820, 560, d.hwnd, (HMENU)10, wc.hInstance, nullptr);

        CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
                      760, 580, 90, 30, d.hwnd, (HMENU)1, wc.hInstance, nullptr);

        ew_apply_editor_fonts(d.hwnd);

        // Bound the preview payload so a huge patch can't hang the UI.
        std::wstring shown;
        if (!meta_w.empty()) {
            shown = meta_w;
            shown += L"\r\n------------------------------\r\n\r\n";
        }
        shown += patch_w;
        const size_t cap = (size_t)256 * 1024;
        if (shown.size() > cap) {
            shown.resize(cap);
            shown += L"\n\n(preview truncated — apply uses full in-memory buffer)";
        }
        SetWindowTextW(d.hwnd_edit, shown.c_str());

        ShowWindow(d.hwnd, SW_SHOW);
        SetForegroundWindow(d.hwnd);
        SetFocus(GetDlgItem(d.hwnd, 1));

        // Modal loop.
        MSG msg{};
        while (IsWindow(d.hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(d.hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
};

struct EwAiTextViewDialog {
    HWND hwnd = nullptr;
    HWND hwnd_edit = nullptr;

    static LRESULT CALLBACK WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
        EwAiTextViewDialog* self = (EwAiTextViewDialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (m == WM_NCCREATE) {
            auto* cs = (CREATESTRUCTW*)l;
            self = (EwAiTextViewDialog*)cs->lpCreateParams;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        }
        if (!self) return DefWindowProcW(h, m, w, l);
        switch (m) {
            case WM_SIZE: {
                RECT rc{}; GetClientRect(h, &rc);
                const int pad = 16;
                const int btn_w = 86;
                const int btn_h = 28;
                const int W = rc.right - rc.left;
                const int H = rc.bottom - rc.top;
                if (self->hwnd_edit) MoveWindow(self->hwnd_edit, pad, pad, W - 2*pad, H - (3*pad + btn_h), TRUE);
                HWND btn = GetDlgItem(h, 1);
                if (btn) MoveWindow(btn, W - pad - btn_w, H - pad - btn_h, btn_w, btn_h, TRUE);
                return 0;
            }
            case WM_ERASEBKGND: {
                HDC dc = (HDC)w; RECT rc{}; GetClientRect(h, &rc); FillRect(dc, &rc, g_brush_bg); return 1;
            }
            case WM_CTLCOLORDLG: {
                ew_theme_init_once(); HDC dc = (HDC)w; SetBkColor(dc, g_theme.bg); SetTextColor(dc, g_theme.text); return (INT_PTR)g_brush_bg;
            }
            case WM_CTLCOLOREDIT: {
                ew_theme_init_once(); HDC dc = (HDC)w; SetBkColor(dc, g_theme.edit_bg); SetTextColor(dc, g_theme.text); return (INT_PTR)g_brush_edit;
            }
            case WM_COMMAND: if (LOWORD(w) == 1) { DestroyWindow(h); return 0; } break;
            case WM_CLOSE: DestroyWindow(h); return 0;
            default: break;
        }
        return DefWindowProcW(h, m, w, l);
    }

    static void RunModal(HWND owner, const wchar_t* title_w, const std::wstring& text_w) {
        ew_theme_init_once();
        WNDCLASSW wc{};
        wc.lpfnWndProc = &EwAiTextViewDialog::WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EwAiTextViewDialog";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);

        EwAiTextViewDialog d;
        d.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, title_w ? title_w : L"View",
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 860, 640,
                                 owner, nullptr, wc.hInstance, &d);
        d.hwnd_edit = CreateWindowW(L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                                    WS_VSCROLL | WS_HSCROLL | ES_READONLY,
                                    10, 10, 820, 560, d.hwnd, (HMENU)10, wc.hInstance, nullptr);
        CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
                      760, 580, 90, 30, d.hwnd, (HMENU)1, wc.hInstance, nullptr);
        ew_apply_editor_fonts(d.hwnd);
        std::wstring shown = text_w;
        const size_t cap = (size_t)256 * 1024;
        if (shown.size() > cap) { shown.resize(cap); shown += L"\n\n(view truncated)"; }
        SetWindowTextW(d.hwnd_edit, shown.c_str());
        ShowWindow(d.hwnd, SW_SHOW);
        SetForegroundWindow(d.hwnd);
        SetFocus(GetDlgItem(d.hwnd, 1));
        MSG msg{};
        while (IsWindow(d.hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(d.hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
    }
};

void App::SyncLiveModeProjection() {
    if (!scene_) return;
    scene_->SetLiveViewportMode(live_mode_enabled_, spectrum_band_i32_);
    if (hwnd_ai_status_) {
        if (live_mode_enabled_) {
            wchar_t msg[256]{};
            swprintf(msg, 256, L"Live mode enabled: viewport now renders the GPU-resident substrate lattice (band=%d) with sandbox/editor tools still active.", spectrum_band_i32_);
            SetWindowTextW(hwnd_ai_status_, msg);
        } else {
            SetWindowTextW(hwnd_ai_status_, L"Live mode disabled: viewport returned to standard sandbox projection.");
        }
    }
    if (hwnd_ai_panel_tool_status_) {
        if (live_mode_enabled_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Live: viewport source = GPU-resident substrate lattice. Editor tooling remains derived-only.");
        else SetWindowTextW(hwnd_ai_panel_tool_status_, L"Live: off. Viewport source = standard sandbox projection. Editor/runtime separation preserved.");
    }
    RefreshViewportResonanceOverlay();
    RefreshNodePanel();
    RefreshSequencerPanel();
    if (hwnd_viewport_) InvalidateRect(hwnd_viewport_, nullptr, FALSE);
    SyncWindowMenu();
}


static void ew_append_bounded_multiline(std::wstring& dst, const std::wstring& line, size_t max_lines, size_t max_chars) {
    if (line.empty()) return;
    if (!dst.empty()) dst += L"\n";
    dst += line;
    if (dst.size() > max_chars) dst.erase(0, dst.size() - max_chars);
    size_t lines = 0;
    for (wchar_t ch : dst) if (ch == L'\n') ++lines;
    while (lines + 1 > max_lines) {
        size_t p = dst.find(L'\n');
        if (p == std::wstring::npos) break;
        dst.erase(0, p + 1);
        --lines;
    }
}

std::wstring App::BuildAiChatPatchViewText(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    std::wstring meta = BuildAiChatPatchMetadata(chat_idx_u32, apply_target_dir_w);
    std::wstringstream ss;
    ss << L"CANONICAL PATCH VIEW\r\n";
    ss << L"Chat: " << ai_chat_title_w_[chat_idx_u32] << L"\r\n";
    if (!ai_chat_project_root_w_[chat_idx_u32].empty()) ss << L"Linked project: " << ai_chat_project_root_w_[chat_idx_u32] << L"\r\n";
    if (!ai_chat_project_summary_w_[chat_idx_u32].empty()) ss << ai_chat_project_summary_w_[chat_idx_u32] << L"\r\n";
    if (!ai_chat_patch_w_[chat_idx_u32].empty()) {
        EwPatchApplyReport rep{};
        ew_patch_extract_targets(ai_chat_patch_w_[chat_idx_u32], &rep);
        ss << L"Buffered patch: yes\r\nFiles touched: " << (unsigned)rep.files_touched_u32 << L"\r\n";
    } else {
        ss << L"Buffered patch: no\r\n";
    }
    if (!ai_chat_patch_explain_w_[chat_idx_u32].empty()) {
        ss << L"\r\nRetained rationale:\r\n" << ai_chat_patch_explain_w_[chat_idx_u32] << L"\r\n";
    }
    const std::wstring warnings = BuildAiChatPatchWarningText(chat_idx_u32, apply_target_dir_w);
    if (!warnings.empty()) {
        ss << L"\r\nScope integrity warnings:\r\n" << warnings;
    }
    if (!meta.empty()) {
        ss << L"\r\nStructured metadata:\r\n" << meta;
    } else if (ai_chat_last_detected_patch_valid_[chat_idx_u32] && !ai_chat_last_detected_patch_w_[chat_idx_u32].empty()) {
        EwPatchApplyReport rep{};
        ew_patch_extract_targets(ai_chat_last_detected_patch_w_[chat_idx_u32], &rep);
        ss << L"\r\nDetected diff is available but not yet buffered into the canonical patch view.\r\n";
        ss << L"Detected diff files: " << (unsigned)rep.files_touched_u32 << L"\r\n";
        if (rep.files_touched_u32 > 0u) ss << L"Next structural action: Use Diff to promote the assistant diff into the patch buffer before preview/apply.\r\n";
    }
    const std::wstring nav = BuildAiNavigationSpineText(chat_idx_u32);
    if (!nav.empty()) ss << L"\r\n" << nav << L"\r\n";
    return ss.str();
}

void App::AiChatShowPatchView(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    (void)NavigateAiChatReferenceSpine(chat_idx_u32, true, false);
    SetAiChatWorkflowEvent(chat_idx_u32, L"Patch/Scope inspected", false);
    const std::wstring text = BuildAiChatPatchViewText(chat_idx_u32, apply_target_dir_w);
    if (text.empty()) {
        AiChatAppend(chat_idx_u32, L"(no retained patch view is available for this chat yet)");
        AiChatRenderSelected();
        return;
    }
    EwAiTextViewDialog::RunModal(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, L"Patch View", text);
    if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Chat: canonical patch view opened.");
}

void App::CaptureAiChatPatchScopeSnapshot(uint32_t chat_idx_u32) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    EwPatchApplyReport rep{};
    if (!ai_chat_patch_w_[chat_idx_u32].empty()) ew_patch_extract_targets(ai_chat_patch_w_[chat_idx_u32], &rep);
    ai_chat_patch_scope_root_w_[chat_idx_u32] = ai_chat_project_root_w_[chat_idx_u32];
    ai_chat_patch_scope_file_count_u32_[chat_idx_u32] = rep.files_touched_u32;
}

static constexpr uint32_t EW_AI_PATCH_WARN_SCOPE_ROOT_DRIFT = 1u << 0;
static constexpr uint32_t EW_AI_PATCH_WARN_TARGET_SET_DRIFT = 1u << 1;
static constexpr uint32_t EW_AI_PATCH_WARN_REDIRECTED_TARGET = 1u << 2;
static constexpr uint32_t EW_AI_PATCH_WARN_PREVIEW_UNBOUND = 1u << 3;

uint32_t App::BuildAiChatPatchWarningMask(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return 0u;
    if (ai_chat_patch_w_[chat_idx_u32].empty()) return 0u;
    EwPatchApplyReport rep{};
    ew_patch_extract_targets(ai_chat_patch_w_[chat_idx_u32], &rep);
    const std::wstring& linked_root = ai_chat_project_root_w_[chat_idx_u32];
    const std::wstring& scope_root = ai_chat_patch_scope_root_w_[chat_idx_u32];
    const std::wstring& prior_apply_dir = ai_chat_apply_target_dir_w_[chat_idx_u32];
    const std::wstring chosen_dir = (apply_target_dir_w && !apply_target_dir_w->empty()) ? *apply_target_dir_w : prior_apply_dir;
    uint32_t mask = 0u;
    if (!scope_root.empty() && !linked_root.empty() && (_wcsicmp(scope_root.c_str(), linked_root.c_str()) != 0)) mask |= EW_AI_PATCH_WARN_SCOPE_ROOT_DRIFT;
    if (ai_chat_patch_scope_file_count_u32_[chat_idx_u32] != 0u && ai_chat_patch_scope_file_count_u32_[chat_idx_u32] != rep.files_touched_u32) mask |= EW_AI_PATCH_WARN_TARGET_SET_DRIFT;
    if (!linked_root.empty() && !chosen_dir.empty() && (_wcsicmp(linked_root.c_str(), chosen_dir.c_str()) != 0)) mask |= EW_AI_PATCH_WARN_REDIRECTED_TARGET;
    if (ai_chat_patch_previewed_[chat_idx_u32] && chosen_dir.empty()) mask |= EW_AI_PATCH_WARN_PREVIEW_UNBOUND;
    return mask;
}

bool App::HasBlockingAiChatPatchWarnings(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const {
    const uint32_t mask = BuildAiChatPatchWarningMask(chat_idx_u32, apply_target_dir_w);
    return (mask & (EW_AI_PATCH_WARN_SCOPE_ROOT_DRIFT | EW_AI_PATCH_WARN_TARGET_SET_DRIFT | EW_AI_PATCH_WARN_REDIRECTED_TARGET)) != 0u;
}

std::wstring App::BuildAiChatPatchWarningHeadline(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    const uint32_t mask = BuildAiChatPatchWarningMask(chat_idx_u32, apply_target_dir_w);
    if (mask == 0u) return std::wstring();
    if ((mask & EW_AI_PATCH_WARN_SCOPE_ROOT_DRIFT) != 0u) return L"Scope root drift.";
    if ((mask & EW_AI_PATCH_WARN_REDIRECTED_TARGET) != 0u) return L"Write target redirected outside linked project.";
    if ((mask & EW_AI_PATCH_WARN_TARGET_SET_DRIFT) != 0u) return L"Target set drift.";
    if ((mask & EW_AI_PATCH_WARN_PREVIEW_UNBOUND) != 0u) return L"Preview completed but write target is not bound.";
    return L"Patch integrity warnings present.";
}

std::wstring App::BuildAiChatPatchNextActionText(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    if (ai_chat_patch_w_[chat_idx_u32].empty()) return L"Next action: buffer or generate a patch for this chat.";
    const uint32_t mask = BuildAiChatPatchWarningMask(chat_idx_u32, apply_target_dir_w);
    if ((mask & EW_AI_PATCH_WARN_SCOPE_ROOT_DRIFT) != 0u) return L"Next action: re-scope the patch against the current linked project, then preview again.";
    if ((mask & EW_AI_PATCH_WARN_REDIRECTED_TARGET) != 0u) return L"Next action: choose a write target inside the linked project or explicitly re-scope and preview again.";
    if ((mask & EW_AI_PATCH_WARN_TARGET_SET_DRIFT) != 0u) return L"Next action: review the target set and re-preview before apply.";
    if (!ai_chat_patch_previewed_[chat_idx_u32]) return L"Next action: preview the patch locally before any disk apply.";
    const std::wstring& prior_apply_dir = ai_chat_apply_target_dir_w_[chat_idx_u32];
    const std::wstring chosen_dir = (apply_target_dir_w && !apply_target_dir_w->empty()) ? *apply_target_dir_w : prior_apply_dir;
    if ((mask & EW_AI_PATCH_WARN_PREVIEW_UNBOUND) != 0u || chosen_dir.empty()) return L"Next action: bind an explicit write target, then apply.";
    return L"Next action: apply to the currently bound write target when ready.";
}

std::wstring App::BuildAiChatPatchWarningText(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    const uint32_t mask = BuildAiChatPatchWarningMask(chat_idx_u32, apply_target_dir_w);
    if (mask == 0u) return std::wstring();
    std::wstringstream ss;
    if ((mask & EW_AI_PATCH_WARN_SCOPE_ROOT_DRIFT) != 0u) {
        ss << L"  - Scope root drift: current linked project differs from the project root captured when this patch buffer was formed. Re-scope this patch before apply.\r\n";
    }
    if ((mask & EW_AI_PATCH_WARN_TARGET_SET_DRIFT) != 0u) {
        ss << L"  - Target-set drift: files touched now differ from the file count captured with the patch scope. Re-preview before apply.\r\n";
    }
    if ((mask & EW_AI_PATCH_WARN_REDIRECTED_TARGET) != 0u) {
        ss << L"  - Redirected write target: current bound write target is outside the linked project root for this chat. Bind a target inside the linked project or explicitly re-scope.\r\n";
    }
    if ((mask & EW_AI_PATCH_WARN_PREVIEW_UNBOUND) != 0u) {
        ss << L"  - Apply target not bound yet: preview completed, but explicit write binding has not been approved for disk apply.\r\n";
    }
    return ss.str();
}

std::wstring App::BuildAiChatPatchMetadata(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    const std::wstring& patch = ai_chat_patch_w_[chat_idx_u32];
    if (patch.empty()) return std::wstring();
    EwPatchApplyReport rep{};
    ew_patch_extract_targets(patch, &rep);
    const std::wstring& linked_root = ai_chat_project_root_w_[chat_idx_u32];
    const std::wstring& scope_root = ai_chat_patch_scope_root_w_[chat_idx_u32];
    const std::wstring& prior_apply_dir = ai_chat_apply_target_dir_w_[chat_idx_u32];
    const std::wstring chosen_dir = (apply_target_dir_w && !apply_target_dir_w->empty()) ? *apply_target_dir_w : prior_apply_dir;
    const bool has_bound_target = !chosen_dir.empty();
    const bool has_linked_root = !linked_root.empty();
    const bool has_scope_root = !scope_root.empty();
    const bool binding_changed_vs_linked = has_linked_root && has_bound_target && (_wcsicmp(linked_root.c_str(), chosen_dir.c_str()) != 0);
    const bool binding_changed_vs_prior = !prior_apply_dir.empty() && has_bound_target && (_wcsicmp(prior_apply_dir.c_str(), chosen_dir.c_str()) != 0);

    std::wstringstream ss;
    ss << L"PATCH VIEW METADATA\r\n";
    ss << L"Chat: " << ai_chat_title_w_[chat_idx_u32] << L"\r\n";
    ss << L"Files touched: " << (unsigned)rep.files_touched_u32 << L"\r\n";
    if (has_linked_root) ss << L"Linked project root: " << linked_root << L"\r\n";
    if (has_scope_root) ss << L"Patch scope root snapshot: " << scope_root << L"\r\n";
    if (ai_chat_patch_scope_file_count_u32_[chat_idx_u32] != 0u) ss << L"Patch scope file-count snapshot: " << (unsigned)ai_chat_patch_scope_file_count_u32_[chat_idx_u32] << L"\r\n";
    ss << L"\r\nTarget binding chain:\r\n";
    if (has_scope_root) ss << L"  1. Semantic scope root: " << scope_root << L"\r\n";
    else if (has_linked_root) ss << L"  1. Semantic scope root: " << linked_root << L"\r\n";
    else ss << L"  1. Semantic scope root: (no linked project bound to this chat)\r\n";
    if (!prior_apply_dir.empty()) ss << L"  2. Last approved apply target: " << prior_apply_dir << L"\r\n";
    else if (has_linked_root) ss << L"  2. Preview target basis: linked project root\r\n";
    else ss << L"  2. Preview target basis: explicit apply target still required\r\n";
    if (has_bound_target) ss << L"  3. Current bound write target: " << chosen_dir << L"\r\n";
    else ss << L"  3. Current bound write target: (not bound yet - preview scope only)\r\n";
    if (binding_changed_vs_linked) ss << L"  Delta vs linked project: redirected write target away from the linked project root\r\n";
    else if (has_linked_root && has_bound_target) ss << L"  Delta vs linked project: write target remains aligned with the linked project root\r\n";
    if (binding_changed_vs_prior) ss << L"  Delta vs last approved target: target changed for this apply\r\n";
    else if (!prior_apply_dir.empty() && has_bound_target) ss << L"  Delta vs last approved target: unchanged\r\n";
    const uint32_t warning_mask = BuildAiChatPatchWarningMask(chat_idx_u32, apply_target_dir_w);
    const std::wstring warning_headline = BuildAiChatPatchWarningHeadline(chat_idx_u32, apply_target_dir_w);
    const std::wstring warnings = BuildAiChatPatchWarningText(chat_idx_u32, apply_target_dir_w);
    const std::wstring next_action = BuildAiChatPatchNextActionText(chat_idx_u32, apply_target_dir_w);
    ss << L"\r\nApply readiness: ";
    if (!ai_chat_patch_previewed_[chat_idx_u32]) ss << L"preview required";
    else if (HasBlockingAiChatPatchWarnings(chat_idx_u32, apply_target_dir_w)) ss << L"blocked until patch integrity is restored";
    else if (!has_bound_target) ss << L"preview complete - explicit write target still required";
    else ss << L"ready for apply to bound target";
    ss << L"\r\n";
    if (!next_action.empty()) ss << next_action << L"\r\n";
    if (warning_mask != 0u) {
        ss << L"\r\nScope integrity status: ";
        ss << (HasBlockingAiChatPatchWarnings(chat_idx_u32, apply_target_dir_w) ? L"blocking warnings present" : L"warnings present");
        ss << L"\r\n";
        if (!warning_headline.empty()) ss << L"Primary warning: " << warning_headline << L"\r\n";
    }
    if (!warnings.empty()) ss << L"\r\nScope integrity warnings:\r\n" << warnings;
    if (!ai_chat_patch_explain_w_[chat_idx_u32].empty()) ss << L"\r\nPatch rationale:\r\n" << ai_chat_patch_explain_w_[chat_idx_u32] << L"\r\n";
    const std::wstring nav = BuildAiNavigationSpineText(chat_idx_u32);
    if (!nav.empty()) ss << L"\r\n" << nav << L"\r\n";
    CanonicalReferenceSummary canonical_summary{};
    std::string canonical_err;
    if (rep.files_touched_u32 > 0u && !rep.rel_paths_w[0].empty() && BuildCanonicalReferenceSummaryForPath(wide_to_utf8(rep.rel_paths_w[0]), 8u, &canonical_summary, &canonical_err)) {
        ss << L"Canonical repo/coherence ranking:\r\n" << canonical_summary.summary_w << L"\r\n";
    } else if (!canonical_err.empty()) {
        ss << L"Canonical repo/coherence ranking unavailable: " << utf8_to_wide(canonical_err) << L"\r\n";
    }
    EigenWare::SubstrateManager::EwStagedExportBundle staged_bundle{};
    if (scene_ && scene_->sm.ui_snapshot_latest_export_bundle(chat_idx_u32, staged_bundle)) {
        ss << L"\r\nExport/continuation stage:\r\n";
        if (!staged_bundle.operation_label_utf8.empty()) ss << L"  Operation: " << utf8_to_wide(staged_bundle.operation_label_utf8) << L"\r\n";
        if (!staged_bundle.export_scope_utf8.empty()) ss << L"  Scope: " << utf8_to_wide(staged_bundle.export_scope_utf8) << L"\r\n";
        if (!staged_bundle.continuation_summary_utf8.empty()) ss << L"  Continuation: " << utf8_to_wide(staged_bundle.continuation_summary_utf8) << L"\r\n";
        if (!staged_bundle.runtime_split_summary_utf8.empty()) ss << L"  Runtime/editor audit: " << utf8_to_wide(staged_bundle.runtime_split_summary_utf8) << L"\r\n";
    }
    if (rep.files_touched_u32 > 0u) {
        ss << L"\r\nTarget set:\r\n";
        const uint32_t show = (rep.files_touched_u32 > 12u) ? 12u : rep.files_touched_u32;
        for (uint32_t i = 0u; i < show; ++i) ss << L"  - " << rep.rel_paths_w[i] << L"\r\n";
        if (rep.files_touched_u32 > show) ss << L"  - (more files omitted here; full target set remains in patch buffer)\r\n";
    }
    return ss.str();
}

void App::AiChatPreviewPatchExplanation(uint32_t chat_idx_u32, const wchar_t* prefix_w) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    const std::wstring& expl = ai_chat_patch_explain_w_[chat_idx_u32];
    if (expl.empty()) return;
    if (prefix_w && *prefix_w) AiChatAppend(chat_idx_u32, prefix_w);
    std::wstringstream ss(expl);
    std::wstring ln;
    uint32_t shown = 0u;
    while (std::getline(ss, ln) && shown < 8u) {
        if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
        if (ln.empty()) continue;
        AiChatAppend(chat_idx_u32, std::wstring(L"  ") + ln);
        ++shown;
    }
    if (shown == 8u && ss.good()) AiChatAppend(chat_idx_u32, L"  (more explanation lines omitted in chat; full patch view remains in docs/ai_patch_views)");
}

void App::AiChatPreviewPatch(uint32_t chat_idx_u32) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    const std::wstring& patch = ai_chat_patch_w_[chat_idx_u32];
    if (patch.empty()) {
        SetAiChatWorkflowEvent(chat_idx_u32, L"Preview blocked: no patch buffered", false);
        AiChatAppend(chat_idx_u32, L"(no patch in buffer — nothing to preview)");
        AiChatRenderSelected();
        return;
    }

    ai_chat_patch_meta_w_[chat_idx_u32] = BuildAiChatPatchMetadata(chat_idx_u32, nullptr);

    // Open a read-only modal preview so the user can inspect the full diff before Apply.
    EwAiPatchPreviewDialog::RunModal(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, patch, ai_chat_patch_meta_w_[chat_idx_u32]);

    ai_chat_patch_previewed_[chat_idx_u32] = true;
    SetAiChatWorkflowEvent(chat_idx_u32, L"Patch preview refreshed", false);
    {
        EwPatchApplyReport targets_rep{};
        ew_patch_extract_targets(patch, &targets_rep);
        std::wstringstream ss;
        ss << L"(preview complete · files=" << (unsigned)targets_rep.files_touched_u32 << L")";
        AiChatAppend(chat_idx_u32, ss.str());
        if (!ai_chat_patch_meta_w_[chat_idx_u32].empty()) AiChatAppend(chat_idx_u32, L"(canonical patch metadata attached to preview dialog and retained for apply)");
    }
    AiChatPreviewPatchExplanation(chat_idx_u32, L"(patch rationale retained for preview)");
    {
        const std::wstring next_action = BuildAiChatPatchNextActionText(chat_idx_u32, nullptr);
        if (!next_action.empty()) AiChatAppend(chat_idx_u32, next_action);
    }
    if (scene_) scene_->ObserveAiChatMemory(chat_idx_u32, SubstrateManager::EW_CHAT_MEMORY_MODE_CODE, std::string("patch preview ready"));
    RefreshAiChatCortex(chat_idx_u32);
    if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Chat: patch preview completed locally. Apply remains gated.");
    AiChatRenderSelected();
}



bool App::AiChatApplyPatch(uint32_t chat_idx_u32) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return false;
    const std::wstring& patch = ai_chat_patch_w_[chat_idx_u32];
    if (patch.empty()) {
        SetAiChatWorkflowEvent(chat_idx_u32, L"Apply blocked: no patch buffered", false);
        AiChatAppend(chat_idx_u32, L"(no patch in buffer — use /patch <diff> or paste a unified diff)");
        AiChatRenderSelected();
        return false;
    }

    // Fail-closed: require a local Preview step before any disk write.
    if (!ai_chat_patch_previewed_[chat_idx_u32]) {
        SetAiChatWorkflowEvent(chat_idx_u32, L"Apply blocked: preview required", false);
        AiChatAppend(chat_idx_u32, L"(apply blocked: Preview required — click Preview or type /preview)");
        AiChatRenderSelected();
        return false;
    }

    if (HasBlockingAiChatPatchWarnings(chat_idx_u32, nullptr)) {
        ai_chat_patch_previewed_[chat_idx_u32] = false;
        SetAiChatWorkflowEvent(chat_idx_u32, L"Apply blocked: scope integrity drifted", false);
        AiChatAppend(chat_idx_u32, L"(apply blocked: scope integrity changed after preview — re-preview or re-scope before apply)");
        const std::wstring headline = BuildAiChatPatchWarningHeadline(chat_idx_u32, nullptr);
        if (!headline.empty()) AiChatAppend(chat_idx_u32, std::wstring(L"Primary warning: ") + headline);
        const std::wstring warnings = BuildAiChatPatchWarningText(chat_idx_u32, nullptr);
        if (!warnings.empty()) {
            std::wstringstream ws(warnings);
            std::wstring wl;
            while (std::getline(ws, wl)) {
                if (!wl.empty() && wl.back() == L'\r') wl.pop_back();
                if (!wl.empty()) AiChatAppend(chat_idx_u32, wl);
            }
        }
        {
            const std::wstring next_action = BuildAiChatPatchNextActionText(chat_idx_u32, nullptr);
            if (!next_action.empty()) AiChatAppend(chat_idx_u32, next_action);
        }
        AiChatRenderSelected();
        return false;
    }

    // Require explicit directory approval.
    EwPatchApplyReport targets_rep{};
    ew_patch_extract_targets(patch, &targets_rep);
    EwAiApplyTargetDirDialog dlg;
    const std::wstring initial = !ai_chat_apply_target_dir_w_[chat_idx_u32].empty() ? ai_chat_apply_target_dir_w_[chat_idx_u32] : ai_chat_project_root_w_[chat_idx_u32];
    if (!dlg.RunModal(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, initial, &targets_rep)) {
        SetAiChatWorkflowEvent(chat_idx_u32, L"Apply canceled", false);
        AiChatAppend(chat_idx_u32, L"(apply canceled)");
        AiChatRenderSelected();
        return false;
    }
    if (HasBlockingAiChatPatchWarnings(chat_idx_u32, &dlg.result_dir)) {
        ai_chat_patch_previewed_[chat_idx_u32] = false;
        ai_chat_patch_meta_w_[chat_idx_u32] = BuildAiChatPatchMetadata(chat_idx_u32, &dlg.result_dir);
        SetAiChatWorkflowEvent(chat_idx_u32, L"Apply blocked: selected target violated patch scope", false);
        AiChatAppend(chat_idx_u32, L"(apply blocked: selected write target does not satisfy current patch scope integrity checks)");
        const std::wstring headline = BuildAiChatPatchWarningHeadline(chat_idx_u32, &dlg.result_dir);
        if (!headline.empty()) AiChatAppend(chat_idx_u32, std::wstring(L"Primary warning: ") + headline);
        const std::wstring warnings = BuildAiChatPatchWarningText(chat_idx_u32, &dlg.result_dir);
        if (!warnings.empty()) {
            std::wstringstream ws(warnings);
            std::wstring wl;
            while (std::getline(ws, wl)) {
                if (!wl.empty() && wl.back() == L'\r') wl.pop_back();
                if (!wl.empty()) AiChatAppend(chat_idx_u32, wl);
            }
        }
        {
            const std::wstring next_action = BuildAiChatPatchNextActionText(chat_idx_u32, &dlg.result_dir);
            if (!next_action.empty()) AiChatAppend(chat_idx_u32, next_action);
        }
        AiChatAppend(chat_idx_u32, L"(preview was invalidated — review Patch/Scope and preview again before apply)");
        AiChatRenderSelected();
        return false;
    }
    ai_chat_apply_target_dir_w_[chat_idx_u32] = dlg.result_dir;
    ai_chat_patch_meta_w_[chat_idx_u32] = BuildAiChatPatchMetadata(chat_idx_u32, &dlg.result_dir);
    if (!ai_chat_patch_meta_w_[chat_idx_u32].empty()) {
        AiChatAppend(chat_idx_u32, L"(canonical patch metadata bound to apply target)");
        std::wstringstream ms(ai_chat_patch_meta_w_[chat_idx_u32]);
        std::wstring ml;
        uint32_t shown = 0u;
        while (std::getline(ms, ml) && shown < 10u) {
            if (!ml.empty() && ml.back() == L'\r') ml.pop_back();
            if (ml.empty()) continue;
            AiChatAppend(chat_idx_u32, std::wstring(L"  ") + ml);
            ++shown;
        }
        if (shown == 10u && ms.good()) AiChatAppend(chat_idx_u32, L"  (more metadata lines omitted in chat; full metadata remains attached to this patch)");
    }
    AiChatPreviewPatchExplanation(chat_idx_u32, L"(applying patch with the following retained rationale)");

    // Apply unified diff deterministically.
    std::wstring err;
    EwPatchApplyReport rep;
    if (!ew_apply_unified_diff_to_dir(patch, dlg.result_dir, &err, &rep)) {
        AiChatAppend(chat_idx_u32, std::wstring(L"(apply failed: ") + err + L")");
        AiChatRenderSelected();
        return false;
    }

    {
        std::wstring msg = L"(patch applied to disk — no other AI data was written/exported; target=" + dlg.result_dir + L")";
        AiChatAppend(chat_idx_u32, msg);
    }
    ai_chat_patch_w_[chat_idx_u32].clear();
    ai_chat_patch_previewed_[chat_idx_u32] = false;
    ai_chat_patch_explain_w_[chat_idx_u32].clear();
    ai_chat_patch_meta_w_[chat_idx_u32].clear();
    ai_chat_patch_scope_root_w_[chat_idx_u32].clear();
    ai_chat_patch_scope_file_count_u32_[chat_idx_u32] = 0u;
    ai_chat_last_detected_patch_valid_[chat_idx_u32] = false;
    ai_chat_last_detected_patch_w_[chat_idx_u32].clear();

    {
        std::wstringstream rs;
        rs << L"(apply report: files=" << (unsigned)rep.files_touched_u32
           << L" rejected=" << (unsigned)rep.files_rejected_u32
           << L" warnings=" << (unsigned)rep.warnings_u32 << L")";
        AiChatAppend(chat_idx_u32, rs.str());
        const uint32_t show = (rep.files_touched_u32 > 16u) ? 16u : rep.files_touched_u32;
        for (uint32_t k = 0u; k < show; ++k) {
            AiChatAppend(chat_idx_u32, std::wstring(L"  ") + rep.rel_paths_w[k]);
        }
        if (rep.files_touched_u32 > show) {
            AiChatAppend(chat_idx_u32, L"(more files omitted in UI; apply used full list)");
        }
    }
    SetAiChatWorkflowEvent(chat_idx_u32, std::wstring(L"Patch applied to ") + dlg.result_dir, false);
    if (scene_) scene_->ObserveAiChatMemory(chat_idx_u32, SubstrateManager::EW_CHAT_MEMORY_MODE_CODE, std::string("patch apply complete"));
    RefreshAiChatCortex(chat_idx_u32);
    if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Chat: patch applied explicitly. Patch buffer cleared until a new diff is buffered.");
    AiChatRenderSelected();
    return true;
}

std::wstring App::BuildAiChatWorkflowStatusText(uint32_t chat_idx_u32) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    std::wstring s = L"Chat workflow: ";
    const bool has_patch = !ai_chat_patch_w_[chat_idx_u32].empty();
    const bool has_scope = !ai_chat_patch_explain_w_[chat_idx_u32].empty() || !ai_chat_patch_meta_w_[chat_idx_u32].empty();
    const bool previewed = ai_chat_patch_previewed_[chat_idx_u32];
    const bool has_diff = ai_chat_last_detected_patch_valid_[chat_idx_u32] && !ai_chat_last_detected_patch_w_[chat_idx_u32].empty();
    const bool blocking = HasBlockingAiChatPatchWarnings(chat_idx_u32, nullptr);
    if (has_patch) s += L"patch buffered";
    else if (has_scope) s += L"scope retained";
    else if (has_diff) s += L"assistant diff ready";
    else s += L"chat idle";
    if (previewed) s += L"; preview complete";
    if (blocking) s += L"; apply gated";
    const std::wstring headline = BuildAiChatPatchWarningHeadline(chat_idx_u32, nullptr);
    if (!headline.empty()) s += L"; " + headline;
    const std::wstring next_action = BuildAiChatPatchNextActionText(chat_idx_u32, nullptr);
    if (!next_action.empty()) s += L"; next: " + next_action;
    const std::wstring action_reason = BuildAiChatPrimaryActionReasonText(chat_idx_u32);
    if (!action_reason.empty()) s += L"; why: " + action_reason;
    if (!ai_chat_last_workflow_event_w_[chat_idx_u32].empty()) s += L"; last: " + ai_chat_last_workflow_event_w_[chat_idx_u32];
    if (!ai_chat_nav_target_rel_w_[chat_idx_u32].empty()) s += L"; target: " + ai_chat_nav_target_rel_w_[chat_idx_u32];
    if (!ai_chat_project_root_w_[chat_idx_u32].empty()) s += L"; project linked";
    const int32_t rank = FindAiWorkflowRank(chat_idx_u32);
    if (rank > 0) s += L"; queue rank: #" + std::to_wstring((long long)rank);
    return s;
}

uint32_t App::BuildAiChatWorkflowPriorityScore(uint32_t chat_idx_u32) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return 0u;
    const bool has_patch = !ai_chat_patch_w_[chat_idx_u32].empty();
    const bool has_scope = !ai_chat_patch_explain_w_[chat_idx_u32].empty() || !ai_chat_patch_meta_w_[chat_idx_u32].empty();
    const bool previewed = ai_chat_patch_previewed_[chat_idx_u32];
    const bool has_diff = ai_chat_last_detected_patch_valid_[chat_idx_u32] && !ai_chat_last_detected_patch_w_[chat_idx_u32].empty();
    const bool linked = !ai_chat_project_root_w_[chat_idx_u32].empty();
    const uint32_t warn_mask = BuildAiChatPatchWarningMask(chat_idx_u32, nullptr);
    const bool blocking = HasBlockingAiChatPatchWarnings(chat_idx_u32, nullptr);
    uint32_t score = 0u;
    if (blocking) score += 1000u;
    if (has_patch && previewed && !blocking) score += 850u;
    else if (has_patch) score += 700u;
    else if (has_diff) score += 520u;
    else if (has_scope) score += 360u;
    else if (linked) score += 180u;
    if ((warn_mask & EW_AI_PATCH_WARN_PREVIEW_UNBOUND) != 0u) score += 140u;
    if (!ai_chat_last_workflow_event_w_[chat_idx_u32].empty()) score += 10u;
    if (chat_idx_u32 == ai_tab_index_u32_) score += 1u;
    return score;
}

std::vector<uint32_t> App::BuildAiWorkflowPriorityOrder() const {
    const uint32_t count = (ai_chat_count_u32_ <= AI_CHAT_MAX) ? ai_chat_count_u32_ : AI_CHAT_MAX;
    std::vector<uint32_t> order;
    order.reserve(count);
    for (uint32_t i = 0u; i < count; ++i) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        const uint32_t sa = BuildAiChatWorkflowPriorityScore(a);
        const uint32_t sb = BuildAiChatWorkflowPriorityScore(b);
        if (sa != sb) return sa > sb;
        if (a == ai_tab_index_u32_ && b != ai_tab_index_u32_) return true;
        if (b == ai_tab_index_u32_ && a != ai_tab_index_u32_) return false;
        return a < b;
    });
    return order;
}

int32_t App::FindAiHighestPriorityChat() const {
    const std::vector<uint32_t> order = BuildAiWorkflowPriorityOrder();
    for (uint32_t idx : order) {
        if (BuildAiChatWorkflowPriorityScore(idx) > 0u) return (int32_t)idx;
    }
    return -1;
}

int32_t App::FindAiWorkflowRank(uint32_t chat_idx_u32) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return -1;
    const std::vector<uint32_t> order = BuildAiWorkflowPriorityOrder();
    int32_t rank = 0;
    for (uint32_t idx : order) {
        if (BuildAiChatWorkflowPriorityScore(idx) == 0u) continue;
        ++rank;
        if (idx == chat_idx_u32) return rank;
    }
    return -1;
}

std::wstring App::BuildAiWorkflowOverviewText() const {
    std::wstringstream ss;
    ss << L"AI WORKFLOW OVERVIEW\r\n";
    ss << L"Chats: " << (unsigned)ai_chat_count_u32_ << L" / " << (unsigned)AI_CHAT_MAX << L"\r\n";
    const int32_t focus_idx = FindAiHighestPriorityChat();
    if (focus_idx >= 0) {
        ss << L"Focus next: [" << (unsigned)(focus_idx + 1) << L"] " << ai_chat_title_w_[focus_idx]
           << L" · " << BuildAiChatPrimaryActionLabel((uint32_t)focus_idx);
        const std::wstring why = BuildAiChatPrimaryActionReasonText((uint32_t)focus_idx);
        if (!why.empty()) ss << L" · " << why;
        ss << L"\r\n";
    }
    ss << L"\r\n";
    const std::vector<uint32_t> order = BuildAiWorkflowPriorityOrder();
    for (uint32_t idx : order) {
        const bool active = (idx == ai_tab_index_u32_);
        ss << (active ? L"> " : L"  ") << L"[" << (unsigned)(idx + 1u) << L"] " << BuildAiChatTabLabelText(idx)
           << L"  {priority " << (unsigned)BuildAiChatWorkflowPriorityScore(idx) << L"}\r\n";
        ss << L"    Title: " << ai_chat_title_w_[idx] << L"\r\n";
        const std::wstring status = BuildAiChatWorkflowStatusText(idx);
        if (!status.empty()) ss << L"    Status: " << status << L"\r\n";
        const std::wstring action = BuildAiChatPrimaryActionLabel(idx);
        const std::wstring reason = BuildAiChatPrimaryActionReasonText(idx);
        if (!action.empty()) {
            ss << L"    Next action: " << action;
            if (!reason.empty()) ss << L" · " << reason;
            ss << L"\r\n";
        }
        const std::wstring headline = BuildAiChatPatchWarningHeadline(idx, nullptr);
        if (!headline.empty()) ss << L"    Primary warning: " << headline << L"\r\n";
        if (!ai_chat_last_workflow_event_w_[idx].empty()) ss << L"    Last event: " << ai_chat_last_workflow_event_w_[idx] << L"\r\n";
        if (!ai_chat_project_root_w_[idx].empty()) ss << L"    Linked project: " << ai_chat_project_root_w_[idx] << L"\r\n";
        if (!ai_chat_patch_scope_root_w_[idx].empty()) ss << L"    Scope root snapshot: " << ai_chat_patch_scope_root_w_[idx] << L"\r\n";
        if (!ai_chat_patch_w_[idx].empty()) {
            EwPatchApplyReport rep{};
            ew_patch_extract_targets(ai_chat_patch_w_[idx], &rep);
            ss << L"    Buffered patch files: " << (unsigned)rep.files_touched_u32 << L"\r\n";
        } else if (ai_chat_last_detected_patch_valid_[idx] && !ai_chat_last_detected_patch_w_[idx].empty()) {
            ss << L"    Assistant diff: ready but not yet buffered\r\n";
        }
        ss << L"\r\n";
    }
    ss << L"Legend: [ ] idle, [D] diff ready, [S] scope retained, [P] preview pending, [A] preview complete, [!] blocking issue, * project linked.\r\n";
    ss << L"Priority favors blocking integrity issues first, then ready-to-apply work, then previewable/buffered work, then scoped/diff/project context.\r\n";
    return ss.str();
}
std::wstring App::BuildAiWorkflowQueueText() const {
    std::wstringstream ss;
    ss << L"AI WORKFLOW QUEUE\r\n";
    const std::vector<uint32_t> order = BuildAiWorkflowPriorityOrder();
    uint32_t active_total = 0u;
    uint32_t blocked_count = 0u;
    uint32_t ready_count = 0u;
    uint32_t scoped_count = 0u;
    uint32_t context_count = 0u;
    uint32_t idle_count = 0u;
    for (uint32_t idx : order) {
        const uint32_t priority = BuildAiChatWorkflowPriorityScore(idx);
        if (priority == 0u) { ++idle_count; continue; }
        ++active_total;
        const std::wstring bucket = BuildAiChatWorkflowBucketText(idx);
        if (bucket == L"Blocked") ++blocked_count;
        else if (bucket == L"Ready To Apply" || bucket == L"Ready To Bind" || bucket == L"Ready To Preview" || bucket == L"Diff Ready") ++ready_count;
        else if (bucket == L"Scope Retained") ++scoped_count;
        else ++context_count;
    }
    ss << L"Active queue items: " << (unsigned)active_total << L"\r\n";
    ss << L"Buckets: blocked=" << (unsigned)blocked_count
       << L" ready=" << (unsigned)ready_count
       << L" scoped=" << (unsigned)scoped_count
       << L" context=" << (unsigned)context_count
       << L" idle=" << (unsigned)idle_count << L"\r\n\r\n";
    const wchar_t* buckets[] = {L"Blocked", L"Ready To Apply", L"Ready To Bind", L"Ready To Preview", L"Diff Ready", L"Scope Retained", L"Project Context"};
    uint32_t ordinal = 0u;
    for (const wchar_t* bucket_w : buckets) {
        bool wrote_header = false;
        for (uint32_t idx : order) {
            if (BuildAiChatWorkflowPriorityScore(idx) == 0u) continue;
            if (BuildAiChatWorkflowBucketText(idx) != bucket_w) continue;
            if (!wrote_header) {
                ss << bucket_w << L"\r\n";
                wrote_header = true;
            }
            ++ordinal;
            ss << L"  [" << (unsigned)ordinal << L"] " << BuildAiChatTabLabelText(idx)
               << L"  {priority " << (unsigned)BuildAiChatWorkflowPriorityScore(idx) << L"}\r\n";
            ss << L"      Next: " << BuildAiChatPrimaryActionLabel(idx);
            const std::wstring reason = BuildAiChatPrimaryActionReasonText(idx);
            if (!reason.empty()) ss << L" · " << reason;
            ss << L"\r\n";
            const std::wstring headline = BuildAiChatPatchWarningHeadline(idx, nullptr);
            if (!headline.empty()) ss << L"      Warning: " << headline << L"\r\n";
            if (!ai_chat_last_workflow_event_w_[idx].empty()) ss << L"      Last: " << ai_chat_last_workflow_event_w_[idx] << L"\r\n";
            if (!ai_chat_project_root_w_[idx].empty()) ss << L"      Project: " << ai_chat_project_root_w_[idx] << L"\r\n";
        }
        if (wrote_header) ss << L"\r\n";
    }
    if (active_total == 0u) ss << L"No active workflow items.\r\n\r\n";
    ss << L"Queue ranks are ordered by blocking integrity first, then apply-ready and diff-bufferable work, then previewable work, then scoped/context threads.\r\n";
    ss << L"Use Focus Next Workflow or Do Next Workflow for the top-ranked item, or pick a bucketed chat from the queue menus.\r\n";
    return ss.str();
}

std::wstring App::BuildAiChatWorkflowBucketText(uint32_t chat_idx_u32) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    const bool has_patch = !ai_chat_patch_w_[chat_idx_u32].empty();
    const bool has_scope = !ai_chat_patch_explain_w_[chat_idx_u32].empty() || !ai_chat_patch_meta_w_[chat_idx_u32].empty();
    const bool previewed = ai_chat_patch_previewed_[chat_idx_u32];
    const bool has_diff = ai_chat_last_detected_patch_valid_[chat_idx_u32] && !ai_chat_last_detected_patch_w_[chat_idx_u32].empty();
    const bool linked = !ai_chat_project_root_w_[chat_idx_u32].empty();
    const bool blocking = HasBlockingAiChatPatchWarnings(chat_idx_u32, nullptr);
    const uint32_t warn_mask = BuildAiChatPatchWarningMask(chat_idx_u32, nullptr);
    if (blocking) return L"Blocked";
    if (has_patch && previewed) {
        if ((warn_mask & EW_AI_PATCH_WARN_PREVIEW_UNBOUND) != 0u) return L"Ready To Bind";
        return L"Ready To Apply";
    }
    if (has_patch) return L"Ready To Preview";
    if (has_diff) return L"Diff Ready";
    if (has_scope) return L"Scope Retained";
    if (linked) return L"Project Context";
    return L"Idle";
}

std::wstring App::BuildAiChatPrimaryActionLabel(uint32_t chat_idx_u32) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    const bool has_patch = !ai_chat_patch_w_[chat_idx_u32].empty();
    const bool has_scope = !ai_chat_patch_explain_w_[chat_idx_u32].empty() || !ai_chat_patch_meta_w_[chat_idx_u32].empty();
    const bool has_diff = ai_chat_last_detected_patch_valid_[chat_idx_u32] && !ai_chat_last_detected_patch_w_[chat_idx_u32].empty();
    const bool previewed = ai_chat_patch_previewed_[chat_idx_u32];
    const bool blocking_warn = HasBlockingAiChatPatchWarnings(chat_idx_u32, nullptr);
    const bool preview_unbound = (BuildAiChatPatchWarningMask(chat_idx_u32, nullptr) & EW_AI_PATCH_WARN_PREVIEW_UNBOUND) != 0u;
    if (blocking_warn) return L"Re-Preview";
    if (has_patch && previewed && preview_unbound) return L"Bind Target";
    if (has_patch && previewed) return L"Apply Now";
    if (has_patch) return L"Preview";
    if (has_diff) return L"Use Diff";
    if (has_scope) return L"Patch/Scope";
    if (!ai_chat_project_root_w_[chat_idx_u32].empty()) return L"Project";
    return L"Open Chat";
}

std::wstring App::BuildAiChatPrimaryActionReasonText(uint32_t chat_idx_u32) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    const std::wstring action = BuildAiChatPrimaryActionLabel(chat_idx_u32);
    const std::wstring headline = BuildAiChatPatchWarningHeadline(chat_idx_u32, nullptr);
    const bool has_patch = !ai_chat_patch_w_[chat_idx_u32].empty();
    const bool has_scope = !ai_chat_patch_explain_w_[chat_idx_u32].empty() || !ai_chat_patch_meta_w_[chat_idx_u32].empty();
    const bool has_diff = ai_chat_last_detected_patch_valid_[chat_idx_u32] && !ai_chat_last_detected_patch_w_[chat_idx_u32].empty();
    const bool previewed = ai_chat_patch_previewed_[chat_idx_u32];
    if (action == L"Re-Preview") return headline.empty() ? L"Patch scope drifted or target binding changed." : headline;
    if (action == L"Bind Target") return L"Preview is complete, but an explicit write target still needs binding.";
    if (action == L"Apply Now") return L"Patch is previewed and target binding is valid.";
    if (action == L"Preview") return L"Patch buffer is ready for review before any write.";
    if (action == L"Use Diff") return has_diff ? L"Assistant diff is ready to buffer into the canonical patch view." : L"Assistant diff can be promoted into the patch buffer.";
    if (action == L"Patch/Scope") return has_scope ? L"Semantic scope and canonical binding details are ready to inspect." : L"Scope information is available for this chat.";
    if (action == L"Project") return L"A project is linked, but no patch scope has been formed yet.";
    return L"No pending patch action for this chat yet.";
}

uint32_t App::CountAiWorkflowBucket(const std::wstring& bucket_w) const {
    uint32_t count = 0u;
    for (uint32_t idx : BuildAiWorkflowPriorityOrder()) {
        if (BuildAiChatWorkflowPriorityScore(idx) == 0u) continue;
        if (BuildAiChatWorkflowBucketText(idx) == bucket_w) ++count;
    }
    return count;
}

std::wstring App::BuildAiWorkflowBucketLeadText(const std::wstring& bucket_w, bool action_mode) const {
    const std::vector<uint32_t> visible = BuildAiWorkflowBucketOrder(bucket_w, 1u);
    if (visible.empty()) return std::wstring();
    const uint32_t idx = visible.front();
    std::wstring lead = L"[" + std::to_wstring((unsigned long long)(idx + 1u)) + L"] " + ai_chat_title_w_[idx];
    const std::wstring action = BuildAiChatPrimaryActionLabel(idx);
    const std::wstring reason = BuildAiChatPrimaryActionReasonText(idx);
    if (action_mode && !action.empty()) lead += std::wstring(L" -> ") + action;
    else if (!action.empty()) lead += std::wstring(L" · ") + action;
    if (!reason.empty()) lead += std::wstring(L" · ") + reason;
    return lead;
}

void App::SetAiChatWorkflowEvent(uint32_t chat_idx_u32, const std::wstring& event_w, bool append_to_chat) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    ai_chat_last_workflow_event_w_[chat_idx_u32] = event_w;
    if (append_to_chat && !event_w.empty()) AiChatAppend(chat_idx_u32, std::wstring(L"(workflow: ") + event_w + L")");
}

std::wstring App::BuildAiChatTabLabelText(uint32_t chat_idx_u32) const {
    if (chat_idx_u32 >= AI_CHAT_MAX) return std::wstring();
    std::wstring title = ai_chat_title_w_[chat_idx_u32].empty() ? (std::wstring(L"Chat ") + std::to_wstring((unsigned)(chat_idx_u32 + 1u))) : ai_chat_title_w_[chat_idx_u32];
    if (title.size() > 18u) title.resize(18u);
    const bool has_patch = !ai_chat_patch_w_[chat_idx_u32].empty();
    const bool has_scope = !ai_chat_patch_explain_w_[chat_idx_u32].empty() || !ai_chat_patch_meta_w_[chat_idx_u32].empty();
    const bool previewed = ai_chat_patch_previewed_[chat_idx_u32];
    const bool has_diff = ai_chat_last_detected_patch_valid_[chat_idx_u32] && !ai_chat_last_detected_patch_w_[chat_idx_u32].empty();
    const bool blocking = HasBlockingAiChatPatchWarnings(chat_idx_u32, nullptr);
    const bool linked = !ai_chat_project_root_w_[chat_idx_u32].empty();
    std::wstring badge = blocking ? L"[!]" : (has_patch ? (previewed ? L"[A]" : L"[P]") : (has_diff ? L"[D]" : (has_scope ? L"[S]" : L"[ ]")));
    if (linked) badge += L"*";
    return badge + L" " + title;
}

void App::ActivateAiChat(uint32_t chat_idx_u32) {
    if (chat_idx_u32 >= AI_CHAT_MAX || !hwnd_ai_tab_) return;
    ai_tab_index_u32_ = chat_idx_u32;
    TabCtrl_SetCurSel(hwnd_ai_tab_, (int)chat_idx_u32);
    SyncAiChatWorkflowState(chat_idx_u32);
    AiChatRenderSelected();
}

void App::FocusAiHighestPriorityChat(bool execute_primary_action) {
    const int32_t idx = FindAiHighestPriorityChat();
    if (idx < 0) {
        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Chat: no workflow queue items are active.");
        return;
    }
    ActivateAiChat((uint32_t)idx);
    const std::wstring action = BuildAiChatPrimaryActionLabel((uint32_t)idx);
    const std::wstring reason = BuildAiChatPrimaryActionReasonText((uint32_t)idx);
    const int32_t rank = FindAiWorkflowRank((uint32_t)idx);
    uint32_t active_count = 0u; for (uint32_t v : BuildAiWorkflowPriorityOrder()) if (BuildAiChatWorkflowPriorityScore(v) > 0u) ++active_count;
    std::wstring status = std::wstring(L"Chat: focus next -> [") + std::to_wstring((uint32_t)idx + 1u) + L"] " + ai_chat_title_w_[idx];
    if (rank > 0 && active_count > 0u) status += std::wstring(L" · queue #") + std::to_wstring((long long)rank) + L"/" + std::to_wstring((unsigned long long)active_count);
    if (!action.empty()) status += std::wstring(L" · ") + action;
    if (!reason.empty()) status += std::wstring(L" · ") + reason;
    SetAiChatWorkflowEvent((uint32_t)idx, execute_primary_action ? L"Focused from workflow queue and executed primary action" : L"Focused from workflow queue", false);
    if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, status.c_str());
    if (execute_primary_action) ExecuteAiChatPrimaryAction((uint32_t)idx);
}

void App::FocusAiWorkflowBucket(const std::wstring& bucket_w, bool execute_primary_action, const wchar_t* prefix_w) {
    const std::vector<uint32_t> visible = BuildAiWorkflowBucketOrder(bucket_w, AI_CHAT_MAX);
    if (visible.empty()) {
        if (hwnd_ai_panel_tool_status_) {
            std::wstring status = std::wstring(prefix_w ? prefix_w : L"Chat: workflow bucket") + L" no items in bucket " + bucket_w + L".";
            SetWindowTextW(hwnd_ai_panel_tool_status_, status.c_str());
        }
        return;
    }
    const uint32_t idx = visible.front();
    ActivateAiChat(idx);
    const std::wstring action = BuildAiChatPrimaryActionLabel(idx);
    const std::wstring reason = BuildAiChatPrimaryActionReasonText(idx);
    std::wstring status = std::wstring(prefix_w ? prefix_w : L"Chat: workflow bucket ->") + L" [" + std::to_wstring(idx + 1u) + L"] " + ai_chat_title_w_[idx];
    status += std::wstring(L" · bucket ") + bucket_w;
    status += std::wstring(L" · bucket #1/") + std::to_wstring((unsigned long long)visible.size());
    if (!action.empty()) status += std::wstring(L" · ") + action;
    if (!reason.empty()) status += std::wstring(L" · ") + reason;
    SetAiChatWorkflowEvent(idx, std::wstring(L"Focused from ") + bucket_w + (execute_primary_action ? L" bucket and executed primary action" : L" bucket"), false);
    if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, status.c_str());
    if (execute_primary_action) ExecuteAiChatPrimaryAction(idx);
}

bool App::BufferAiChatDetectedDiff(uint32_t chat_idx_u32, bool append_feedback) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return false;
    if (!ai_chat_last_detected_patch_valid_[chat_idx_u32] || ai_chat_last_detected_patch_w_[chat_idx_u32].empty()) return false;
    ai_chat_patch_w_[chat_idx_u32] = ai_chat_last_detected_patch_w_[chat_idx_u32];
    if (ai_chat_patch_explain_w_[chat_idx_u32].empty() && append_feedback) {
        AiChatAppend(chat_idx_u32, L"(diff copied into patch buffer — waiting for or reusing current patch rationale)");
    }
    ai_chat_patch_previewed_[chat_idx_u32] = false;
    CaptureAiChatPatchScopeSnapshot(chat_idx_u32);
    ai_chat_patch_meta_w_[chat_idx_u32] = BuildAiChatPatchMetadata(chat_idx_u32, nullptr);
    SetAiChatWorkflowEvent(chat_idx_u32, L"Patch buffered from assistant diff", false);
    ai_chat_mode_u32_[chat_idx_u32] = SubstrateManager::EW_CHAT_MEMORY_MODE_CODE;
    if (append_feedback) {
        AiChatAppend(chat_idx_u32, L"(patch buffer set from last Assistant diff — preview first, then apply explicitly)");
    }
    if (scene_) scene_->ObserveAiChatMemory(chat_idx_u32, SubstrateManager::EW_CHAT_MEMORY_MODE_CODE, std::string("assistant diff copied into patch buffer"));
    RefreshAiChatCortex(chat_idx_u32);
    if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Chat: assistant diff copied into patch buffer. Preview is required before apply.");
    return true;
}

void App::ExecuteAiChatPrimaryAction(uint32_t chat_idx_u32) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    ActivateAiChat(chat_idx_u32);
    const bool has_patch = !ai_chat_patch_w_[chat_idx_u32].empty();
    const bool has_scope = !ai_chat_patch_explain_w_[chat_idx_u32].empty() || !ai_chat_patch_meta_w_[chat_idx_u32].empty();
    const bool has_diff = ai_chat_last_detected_patch_valid_[chat_idx_u32] && !ai_chat_last_detected_patch_w_[chat_idx_u32].empty();
    const bool previewed = ai_chat_patch_previewed_[chat_idx_u32];
    const bool blocking_warn = HasBlockingAiChatPatchWarnings(chat_idx_u32, nullptr);
    const bool preview_unbound = (BuildAiChatPatchWarningMask(chat_idx_u32, nullptr) & EW_AI_PATCH_WARN_PREVIEW_UNBOUND) != 0u;
    const std::wstring action = BuildAiChatPrimaryActionLabel(chat_idx_u32);
    if (!action.empty() && action != L"Open Chat") SetAiChatWorkflowEvent(chat_idx_u32, std::wstring(L"Primary action triggered: ") + action, false);
    if (blocking_warn || (has_patch && !previewed)) { AiChatPreviewPatch(chat_idx_u32); return; }
    if (has_patch && previewed && !blocking_warn) { AiChatApplyPatch(chat_idx_u32); return; }
    if (has_diff && !has_patch) {
        if (BufferAiChatDetectedDiff(chat_idx_u32, true)) {
            AiChatRenderSelected();
            return;
        }
    }
    if (has_scope) { AiChatShowPatchView(chat_idx_u32, nullptr); return; }
    if (!ai_chat_project_root_w_[chat_idx_u32].empty()) {
        SetAiChatWorkflowEvent(chat_idx_u32, L"Project context inspected", false);
        AiChatAppend(chat_idx_u32, L"(project linked — start a request or open Patch/Scope to inspect current semantic context)");
        AiChatRenderSelected();
        return;
    }
    if (preview_unbound && has_patch) { AiChatApplyPatch(chat_idx_u32); return; }
}

void App::RefreshAiChatTabLabels() {
    if (!hwnd_ai_tab_) return;
    const uint32_t count = (ai_chat_count_u32_ <= AI_CHAT_MAX) ? ai_chat_count_u32_ : AI_CHAT_MAX;
    for (uint32_t i = 0u; i < count; ++i) {
        std::wstring label = BuildAiChatTabLabelText(i);
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = label.empty() ? (LPWSTR)L"Chat" : (LPWSTR)label.c_str();
        TabCtrl_SetItem(hwnd_ai_tab_, (int)i, &ti);
    }
    InvalidateRect(hwnd_ai_tab_, nullptr, TRUE);
}


void App::SyncAiChatWorkflowState(uint32_t chat_idx_u32) {
    if (chat_idx_u32 >= AI_CHAT_MAX) return;
    RefreshAiNavigationSpine(chat_idx_u32);
    if (!ai_chat_patch_w_[chat_idx_u32].empty() || !ai_chat_patch_explain_w_[chat_idx_u32].empty()) {
        ai_chat_patch_meta_w_[chat_idx_u32] = BuildAiChatPatchMetadata(chat_idx_u32, nullptr);
    }
    RefreshAiChatCortex(chat_idx_u32);
    if (hwnd_ai_panel_tool_status_) {
        const std::wstring status = BuildAiChatWorkflowStatusText(chat_idx_u32);
        SetWindowTextW(hwnd_ai_panel_tool_status_, status.empty() ? L"Chat: ready." : status.c_str());
    }
    RefreshAiChatTabLabels();
}

void App::AiChatRenderSelected() {
    if (!hwnd_ai_chat_list_) return;
    SendMessageW(hwnd_ai_chat_list_, LB_RESETCONTENT, 0, 0);
    const uint32_t idx = ai_tab_index_u32_;
    if (idx >= AI_CHAT_MAX) return;
    const auto& v = ai_chat_msgs_[idx];
    for (const auto& s : v) {
        SendMessageW(hwnd_ai_chat_list_, LB_ADDSTRING, 0, (LPARAM)s.c_str());
    }
    // Scroll to bottom.
    const int n = (int)SendMessageW(hwnd_ai_chat_list_, LB_GETCOUNT, 0, 0);
    if (n > 0) SendMessageW(hwnd_ai_chat_list_, LB_SETTOPINDEX, (WPARAM)(n - 1), 0);

    // Enable/disable Use-Patch button based on detected diff.
    if (hwnd_ai_chat_usepatch_) {
        const bool en = (idx < AI_CHAT_MAX) ? ai_chat_last_detected_patch_valid_[idx] : false;
        SetWindowTextW(hwnd_ai_chat_usepatch_, en ? L"Use Diff" : L"Use");
        EnableWindow(hwnd_ai_chat_usepatch_, en ? TRUE : FALSE);
        InvalidateRect(hwnd_ai_chat_usepatch_, nullptr, TRUE);
    }

    // Enable/disable Preview + Apply based on patch presence.
    if (hwnd_ai_chat_preview_) {
        const bool en = (idx < AI_CHAT_MAX) ? !ai_chat_patch_w_[idx].empty() : false;
        if (en) {
            EwPatchApplyReport targets_rep{};
            ew_patch_extract_targets(ai_chat_patch_w_[idx], &targets_rep);
            std::wstring label = L"Preview " + std::to_wstring((unsigned long long)targets_rep.files_touched_u32);
            SetWindowTextW(hwnd_ai_chat_preview_, label.c_str());
        } else {
            SetWindowTextW(hwnd_ai_chat_preview_, L"Preview");
        }
        EnableWindow(hwnd_ai_chat_preview_, en ? TRUE : FALSE);
        InvalidateRect(hwnd_ai_chat_preview_, nullptr, TRUE);
    }
    if (hwnd_ai_chat_apply_) {
        const bool has_patch = (idx < AI_CHAT_MAX) ? !ai_chat_patch_w_[idx].empty() : false;
        const bool previewed = (idx < AI_CHAT_MAX) ? ai_chat_patch_previewed_[idx] : false;
        const bool blocking_warn = (idx < AI_CHAT_MAX) ? HasBlockingAiChatPatchWarnings(idx, nullptr) : false;
        const bool preview_unbound = (idx < AI_CHAT_MAX) ? ((BuildAiChatPatchWarningMask(idx, nullptr) & EW_AI_PATCH_WARN_PREVIEW_UNBOUND) != 0u) : false;
        const bool en = has_patch && previewed && !blocking_warn;
        if (blocking_warn) SetWindowTextW(hwnd_ai_chat_apply_, L"Re-Preview");
        else if (preview_unbound) SetWindowTextW(hwnd_ai_chat_apply_, L"Bind Target");
        else SetWindowTextW(hwnd_ai_chat_apply_, en ? L"Apply Now" : L"Apply");
        EnableWindow(hwnd_ai_chat_apply_, en ? TRUE : FALSE);
        InvalidateRect(hwnd_ai_chat_apply_, nullptr, TRUE);
    }
    if (hwnd_ai_chat_patchview_) {
        const bool en = (idx < AI_CHAT_MAX) ? (!ai_chat_patch_w_[idx].empty() || !ai_chat_patch_explain_w_[idx].empty() || !ai_chat_last_detected_patch_w_[idx].empty()) : false;
        SetWindowTextW(hwnd_ai_chat_patchview_, !ai_chat_patch_w_[idx].empty() ? L"Patch" : L"Scope");
        EnableWindow(hwnd_ai_chat_patchview_, en ? TRUE : FALSE);
        InvalidateRect(hwnd_ai_chat_patchview_, nullptr, TRUE);
    }
    if (hwnd_ai_chat_nextaction_) {
        const std::wstring label = BuildAiChatPrimaryActionLabel(idx);
        const bool en = !label.empty() && label != L"Open Chat";
        SetWindowTextW(hwnd_ai_chat_nextaction_, label.empty() ? L"Next" : label.c_str());
        EnableWindow(hwnd_ai_chat_nextaction_, en ? TRUE : FALSE);
        InvalidateRect(hwnd_ai_chat_nextaction_, nullptr, TRUE);
    }
    SyncAiChatWorkflowState(idx);
    RefreshAiPanelChrome();
}

void App::AiChatRename(uint32_t chat_idx, const std::wstring& new_title) {
    if (chat_idx >= AI_CHAT_MAX) return;
    if (!hwnd_ai_tab_) return;
    std::wstring t = new_title;
    // Trim and cap.
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\t' || t.back() == L'\r' || t.back() == L'\n')) t.pop_back();
    while (!t.empty() && (t.front() == L' ' || t.front() == L'\t' || t.front() == L'\r' || t.front() == L'\n')) t.erase(t.begin());
    if (t.empty()) return;
    if (t.size() > 28) t.resize(28);

    ai_chat_title_w_[chat_idx] = t;
    TCITEMW ti{};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)ai_chat_title_w_[chat_idx].c_str();
    TabCtrl_SetItem(hwnd_ai_tab_, (int)chat_idx, &ti);
    RefreshAiChatTabLabels();
}


void App::AiChatClose(uint32_t chat_idx) {
    if (!hwnd_ai_tab_) return;
    if (ai_chat_count_u32_ <= 1u) return; // never close last
    if (chat_idx >= ai_chat_count_u32_) return;

    // Remove tab item.
    TabCtrl_DeleteItem(hwnd_ai_tab_, (int)chat_idx);

    // Shift storage down to keep indices contiguous (bounded, deterministic).
    for (uint32_t i = chat_idx; i + 1u < ai_chat_count_u32_; ++i) {
        ai_chat_msgs_[i].swap(ai_chat_msgs_[i + 1u]);
        ai_chat_title_w_[i].swap(ai_chat_title_w_[i + 1u]);
        ai_chat_patch_w_[i].swap(ai_chat_patch_w_[i + 1u]);
        ai_chat_patch_explain_w_[i].swap(ai_chat_patch_explain_w_[i + 1u]);
        ai_chat_patch_meta_w_[i].swap(ai_chat_patch_meta_w_[i + 1u]);
        ai_chat_last_workflow_event_w_[i].swap(ai_chat_last_workflow_event_w_[i + 1u]);
        ai_chat_apply_target_dir_w_[i].swap(ai_chat_apply_target_dir_w_[i + 1u]);
        ai_chat_last_detected_patch_w_[i].swap(ai_chat_last_detected_patch_w_[i + 1u]);
        ai_chat_last_detected_patch_valid_[i] = ai_chat_last_detected_patch_valid_[i + 1u];
        ai_chat_patch_previewed_[i] = ai_chat_patch_previewed_[i + 1u];
        ai_chat_mode_u32_[i] = ai_chat_mode_u32_[i + 1u];
        ai_chat_cortex_summary_w_[i].swap(ai_chat_cortex_summary_w_[i + 1u]);
        ai_chat_project_summary_w_[i].swap(ai_chat_project_summary_w_[i + 1u]);
        ai_chat_project_root_w_[i].swap(ai_chat_project_root_w_[i + 1u]);
        ai_chat_patch_scope_root_w_[i].swap(ai_chat_patch_scope_root_w_[i + 1u]);
        ai_chat_patch_scope_file_count_u32_[i] = ai_chat_patch_scope_file_count_u32_[i + 1u];
        ai_chat_folder_of_u32_[i] = ai_chat_folder_of_u32_[i + 1u];

        // Update displayed tab text to match shifted title.
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        const std::wstring& label = ai_chat_title_w_[i].empty() ? std::wstring(L"Chat") : ai_chat_title_w_[i];
        ti.pszText = (LPWSTR)label.c_str();
        TabCtrl_SetItem(hwnd_ai_tab_, (int)i, &ti);
    }

    // Clear last slot.
    const uint32_t last = ai_chat_count_u32_ - 1u;
    ai_chat_msgs_[last].clear();
    ai_chat_title_w_[last].clear();
    ai_chat_patch_w_[last].clear();
    ai_chat_patch_explain_w_[last].clear();
    ai_chat_patch_meta_w_[last].clear();
    ai_chat_last_workflow_event_w_[last].clear();
    ai_chat_patch_scope_root_w_[last].clear();
    ai_chat_patch_scope_file_count_u32_[last] = 0u;
    ai_chat_apply_target_dir_w_[last].clear();
    ai_chat_last_detected_patch_w_[last].clear();
    ai_chat_last_detected_patch_valid_[last] = false;
    ai_chat_patch_previewed_[last] = false;
    ai_chat_mode_u32_[last] = SubstrateManager::EW_CHAT_MEMORY_MODE_TALK;
    ai_chat_cortex_summary_w_[last].clear();
    ai_chat_project_summary_w_[last].clear();
    ai_chat_project_root_w_[last].clear();
    ai_chat_folder_of_u32_[last] = 0u;

    ai_chat_count_u32_--;

    if (ai_tab_index_u32_ >= ai_chat_count_u32_) ai_tab_index_u32_ = ai_chat_count_u32_ - 1u;
    TabCtrl_SetCurSel(hwnd_ai_tab_, (int)ai_tab_index_u32_);
    RefreshAiChatTabLabels();
    AiChatRenderSelected();
}

void App::ToggleAiPanel() {
    if (!ew_editor_build_enabled) return;
    CreateAiPanelWindow();
    if (!hwnd_ai_panel_) return;
    const BOOL vis = IsWindowVisible(hwnd_ai_panel_);
    ShowWindow(hwnd_ai_panel_, vis ? SW_HIDE : SW_SHOW);
    SyncWindowMenu();
    if (!vis) {
        UpdateAiPanel();
    }
}

void App::MarkAiExperimentsSeen() {
    if (!scene_) return;
    ai_experiments_seen_u64_ = scene_->sm.vault_experiments_committed_u64;
    ai_unseen_experiments_u32_ = 0u;
    if (hwnd_ai_bell_) InvalidateRect(hwnd_ai_bell_, nullptr, TRUE);
}

static std::wstring ew_utf8_to_wide_lossy(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out; out.resize((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

// NOTE: AI progress/experiment lists previously scanned disk directly.
// Per design: AI learning/crawling artifacts remain substrate-resident unless the
// user explicitly requests disk apply/export via console/AI interactions. Therefore the
// chat-first AI panel does not enumerate disk artifacts.


void App::UpdateAiPanel() {
    if (!scene_) return;

    const uint64_t committed = scene_->sm.vault_experiments_committed_u64;
    if (committed >= ai_experiments_seen_u64_) {
        uint64_t delta = committed - ai_experiments_seen_u64_;
        if (delta > 0ull) {
            uint64_t capped = (delta > 963ull) ? 963ull : delta;
            ai_unseen_experiments_u32_ = (uint32_t)capped;
        } else {
            ai_unseen_experiments_u32_ = 0u;
        }
    } else {
        // Counter reset should not happen, but fail closed.
        ai_experiments_seen_u64_ = committed;
        ai_unseen_experiments_u32_ = 0u;
    }

    if (hwnd_ai_bell_) InvalidateRect(hwnd_ai_bell_, nullptr, TRUE);
}

void App::LayoutChildren(int w, int h) {
    const int panel_w = 420;
    const int content_h = (content_visible_ && ew_editor_build_enabled ? 260 : 0);
    const int top_h = (h > content_h) ? (h - content_h) : h;

    if (!ew_editor_build_enabled) {
        if (hwnd_viewport_) MoveWindow(hwnd_viewport_, 0, 0, w, h, TRUE);
        return;
    }

    if (hwnd_viewport_) MoveWindow(hwnd_viewport_, 0, 0, w - panel_w, top_h, TRUE);
    if (hwnd_panel_) MoveWindow(hwnd_panel_, w - panel_w, 0, panel_w, top_h, TRUE);
    if (hwnd_viewport_resonance_overlay_) {
        const int view_w = std::max(0, w - panel_w);
        const int overlay_w = std::min(360, std::max(220, view_w / 3));
        const int overlay_x = std::max(12, view_w - overlay_w - 12);
        const int overlay_h = std::min(186, std::max(136, top_h / 3));
        MoveWindow(hwnd_viewport_resonance_overlay_, overlay_x, 12, overlay_w, overlay_h, TRUE);
    }

    // Bottom content browser.
    if (hwnd_content_) {
        MoveWindow(hwnd_content_, 0, top_h, w, content_h, TRUE);
        if (hwnd_content_search_) MoveWindow(hwnd_content_search_, 10, 10, 260, 24, TRUE);
        if (hwnd_content_refresh_) MoveWindow(hwnd_content_refresh_, 278, 10, 80, 24, TRUE);
        if (hwnd_content_view_list_) MoveWindow(hwnd_content_view_list_, 366, 10, 60, 24, TRUE);
        if (hwnd_content_view_thumb_) MoveWindow(hwnd_content_view_thumb_, 430, 10, 70, 24, TRUE);
        if (hwnd_content_view_3d_) MoveWindow(hwnd_content_view_3d_, 504, 10, 46, 24, TRUE);
        if (hwnd_content_thumb_) {
            const UINT dpi = GetDpiForWindow(hwnd_content_thumb_);
            const int sx = MulDiv(92, (int)dpi, 96);
            const int sy = MulDiv(92, (int)dpi, 96);
            ListView_SetIconSpacing(hwnd_content_thumb_, sx, sy);
        }

        if (hwnd_content_refcheck_) MoveWindow(hwnd_content_refcheck_, 556, 10, 54, 24, TRUE);
        if (hwnd_content_status_) MoveWindow(hwnd_content_status_, 620, 12, std::max(120, w - 630), 18, TRUE);
        const int browser_h = std::max(0, content_h - 76);
        if (hwnd_content_list_) MoveWindow(hwnd_content_list_, 10, 40, w - 20, browser_h, TRUE);
        if (hwnd_content_thumb_) MoveWindow(hwnd_content_thumb_, 10, 40, w - 20, browser_h, TRUE);
        if (hwnd_content_3d_) MoveWindow(hwnd_content_3d_, 10, 40, w - 20, browser_h, TRUE);
        if (hwnd_content_selected_) MoveWindow(hwnd_content_selected_, 10, 44 + browser_h, w - 20, 18, TRUE);
    }

    // Right dock layout (tab strip + panel roots).
    const UINT dock_dpi = hwnd_panel_ ? GetDpiForWindow(hwnd_panel_) : 96u;
    const int PAD = MulDiv(12, (int)dock_dpi, 96);
    const int PAD_SM = MulDiv(10, (int)dock_dpi, 96);
    const int TAB_H = MulDiv(30, (int)dock_dpi, 96);
    const int TOOLBAR_H = MulDiv(34, (int)dock_dpi, 96);
    const int ROW_H = MulDiv(28, (int)dock_dpi, 96);
    int tab_x = PAD;
    int tab_y = PAD;
    int tab_w = panel_w - 2 * PAD;

    int panel_x = PAD;
    int panel_y = PAD + TAB_H + PAD_SM - 2;
    int panel_w_in = panel_w - 2 * PAD;
    int panel_h_in = top_h - panel_y - PAD;
    if (panel_h_in < 64) panel_h_in = 64;

    if (hwnd_rdock_tab_) MoveWindow(hwnd_rdock_tab_, tab_x, tab_y, tab_w, TAB_H, TRUE);
    if (hwnd_rdock_outliner_) MoveWindow(hwnd_rdock_outliner_, panel_x, panel_y, panel_w_in, panel_h_in, TRUE);
    if (hwnd_rdock_details_)  MoveWindow(hwnd_rdock_details_,  panel_x, panel_y, panel_w_in, panel_h_in, TRUE);
    if (hwnd_rdock_asset_)    MoveWindow(hwnd_rdock_asset_,    panel_x, panel_y, panel_w_in, panel_h_in, TRUE);
    if (hwnd_rdock_voxel_)    MoveWindow(hwnd_rdock_voxel_,    panel_x, panel_y, panel_w_in, panel_h_in, TRUE);
    if (hwnd_rdock_node_)     MoveWindow(hwnd_rdock_node_,     panel_x, panel_y, panel_w_in, panel_h_in, TRUE);
    if (hwnd_rdock_sequencer_) MoveWindow(hwnd_rdock_sequencer_, panel_x, panel_y, panel_w_in, panel_h_in, TRUE);
    // Outliner: toolbar + list.
    if (hwnd_objlist_ && hwnd_rdock_outliner_) {
        if (hwnd_tb_outliner_) MoveWindow(hwnd_tb_outliner_, 0, 0, panel_w_in, TOOLBAR_H, TRUE);
        if (hwnd_outliner_search_) MoveWindow(hwnd_outliner_search_, 58, PAD_SM / 2, panel_w_in - 58 - 176, ROW_H, TRUE);
        if (hwnd_outliner_clear_) MoveWindow(hwnd_outliner_clear_, panel_w_in - 56, PAD_SM / 2, 46, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_outliner_, 1530)) MoveWindow(h, panel_w_in - 170, PAD_SM / 2, 52, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_outliner_, 1540)) MoveWindow(h, panel_w_in - 114, PAD_SM / 2, 52, ROW_H, TRUE);
        MoveWindow(hwnd_objlist_, PAD_SM, TOOLBAR_H + PAD_SM, panel_w_in - 2 * PAD_SM, panel_h_in - (TOOLBAR_H + 2 * PAD_SM), TRUE);
    }

    // Asset panel (command + builder/tool hooks) layout.
    if (hwnd_rdock_asset_) {
        const int X = PAD_SM;
        if (hwnd_tb_asset_) MoveWindow(hwnd_tb_asset_, 0, 0, panel_w_in, TOOLBAR_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_asset_, 1542)) MoveWindow(h, panel_w_in - 170, PAD_SM / 2, 52, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_asset_, 1532)) MoveWindow(h, panel_w_in - 114, PAD_SM / 2, 52, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_asset_, 2060)) MoveWindow(h, panel_w_in - 278, PAD_SM / 2, 100, ROW_H, TRUE);
        int y = TOOLBAR_H + PAD_SM;
        const int ROW = ROW_H;
        const int GAP = PAD_SM - 2;
        const int W = panel_w_in - 2 * PAD_SM;
        const int BTN_W = 108;
        const int TRACK_W = std::max(180, W - (BTN_W + PAD_SM + 8));

        if (hwnd_input_) MoveWindow(hwnd_input_, X, y, W, ROW, TRUE);
        y += ROW + GAP;
        if (hwnd_send_) MoveWindow(hwnd_send_, X, y, 80, ROW, TRUE);
        if (hwnd_import_) MoveWindow(hwnd_import_, X + 88, y, 110, ROW, TRUE);
        if (hwnd_bootstrap_) MoveWindow(hwnd_bootstrap_, X + 206, y, W - 206, ROW, TRUE);
        y += ROW + GAP;

        if (hwnd_toggle_play_) MoveWindow(hwnd_toggle_play_, X + 32, y, 86, 24, TRUE);
        if (hwnd_toggle_ai_) MoveWindow(hwnd_toggle_ai_, X + 152, y, 86, 24, TRUE);
        y += 26;
        if (hwnd_toggle_learning_) MoveWindow(hwnd_toggle_learning_, X + 42, y, 86, 24, TRUE);
        if (hwnd_toggle_crawling_) MoveWindow(hwnd_toggle_crawling_, X + 176, y, 86, 24, TRUE);
        if (hwnd_vault_) MoveWindow(hwnd_vault_, X + W - 120, y, 56, 24, TRUE);
        if (hwnd_ai_panel_) { /* separate window */ }
        y += 28;
        if (hwnd_ai_status_) MoveWindow(hwnd_ai_status_, X, y, W, 18, TRUE);
        y += 22;
        if (hwnd_asset_selected_) MoveWindow(hwnd_asset_selected_, X, y, W, 18, TRUE);
        y += 22;
        if (hwnd_asset_gate_) MoveWindow(hwnd_asset_gate_, X, y, W, 18, TRUE);
        y += 22;
        if (hwnd_asset_builder_status_) MoveWindow(hwnd_asset_builder_status_, X, y, W, 18, TRUE);
        y += 24;
        if (hwnd_asset_review_refs_) MoveWindow(hwnd_asset_review_refs_, X, y, 104, ROW, TRUE);
        if (hwnd_asset_tool_mode_) MoveWindow(hwnd_asset_tool_mode_, X + 110, y, std::min(176, W - 110), ROW, TRUE);
        y += ROW + 4;
        if (hwnd_asset_planet_atmo_) MoveWindow(hwnd_asset_planet_atmo_, X, y, TRACK_W, 28, TRUE);
        if (hwnd_asset_planet_apply_) MoveWindow(hwnd_asset_planet_apply_, X + TRACK_W + PAD_SM, y, BTN_W, ROW, TRUE);
        y += 30;
        if (hwnd_asset_planet_iono_) MoveWindow(hwnd_asset_planet_iono_, X, y, TRACK_W, 28, TRUE);
        if (hwnd_asset_planet_sculpt_) MoveWindow(hwnd_asset_planet_sculpt_, X + TRACK_W + PAD_SM, y, BTN_W, ROW, TRUE);
        y += 30;
        if (hwnd_asset_planet_magneto_) MoveWindow(hwnd_asset_planet_magneto_, X, y, TRACK_W, 28, TRUE);
        if (hwnd_asset_planet_paint_) MoveWindow(hwnd_asset_planet_paint_, X + TRACK_W + PAD_SM, y, BTN_W, ROW, TRUE);
        y += 32;
        if (hwnd_asset_character_archetype_) MoveWindow(hwnd_asset_character_archetype_, X, y, std::min(176, W), ROW, TRUE);
        y += ROW + 4;
        if (hwnd_asset_character_height_) MoveWindow(hwnd_asset_character_height_, X, y, TRACK_W, 28, TRUE);
        if (hwnd_asset_character_bind_) MoveWindow(hwnd_asset_character_bind_, X + TRACK_W + PAD_SM, y, BTN_W, ROW, TRUE);
        y += 30;
        if (hwnd_asset_character_rigidity_) MoveWindow(hwnd_asset_character_rigidity_, X, y, TRACK_W, 28, TRUE);
        if (hwnd_asset_character_pose_) MoveWindow(hwnd_asset_character_pose_, X + TRACK_W + PAD_SM, y, BTN_W, ROW, TRUE);
        y += 30;
        if (hwnd_asset_character_gait_) MoveWindow(hwnd_asset_character_gait_, X, y, TRACK_W, 28, TRUE);
        y += 34;
        if (hwnd_asset_tool_summary_) MoveWindow(hwnd_asset_tool_summary_, X, y, W, 118, TRUE);
        y += 122;
        if (hwnd_output_) {
            int out_h = panel_h_in - y - PAD_SM;
            if (out_h < 64) out_h = 64;
            MoveWindow(hwnd_output_, X, y, W, out_h, TRUE);
        }
    }

    // Details panel: property grid fills below toolbar.
    if (hwnd_rdock_details_) {
        if (hwnd_tb_details_) MoveWindow(hwnd_tb_details_, 0, 0, panel_w_in, TOOLBAR_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_details_, 1531)) MoveWindow(h, PAD_SM, PAD_SM / 2, 52, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_details_, 1541)) MoveWindow(h, PAD_SM + 56, PAD_SM / 2, 52, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_details_, 2020)) MoveWindow(h, 120, PAD_SM / 2, 28, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_details_, 2021)) MoveWindow(h, 152, PAD_SM / 2, 28, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_details_, 2022)) MoveWindow(h, 184, PAD_SM / 2, 58, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_details_, 2030)) MoveWindow(h, 248, PAD_SM / 2, 54, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_details_, 2031)) MoveWindow(h, 306, PAD_SM / 2, 54, ROW_H, TRUE);
        if (hwnd_apply_xform_) MoveWindow(hwnd_apply_xform_, panel_w_in - 190, PAD_SM / 2, 58, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_details_, 2090)) MoveWindow(h, panel_w_in - 128, PAD_SM / 2, 54, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_details_, 2091)) MoveWindow(h, panel_w_in - 70, PAD_SM / 2, 54, ROW_H, TRUE);
        if (hwnd_propgrid_) MoveWindow(hwnd_propgrid_, PAD_SM, TOOLBAR_H + PAD_SM, panel_w_in - 2 * PAD_SM, panel_h_in - (TOOLBAR_H + 2 * PAD_SM), TRUE);
        // In-place editor is positioned dynamically by BeginPropEdit().
    }


// Voxel: toolbar anchors + atom-node workspace.
    if (hwnd_rdock_voxel_) {
        if (hwnd_tb_voxel_) MoveWindow(hwnd_tb_voxel_, 0, 0, panel_w_in, TOOLBAR_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_voxel_, 1543)) MoveWindow(h, panel_w_in - 194, PAD_SM / 2, 52, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_voxel_, 1533)) MoveWindow(h, panel_w_in - 138, PAD_SM / 2, 52, ROW_H, TRUE);
        if (hwnd_voxel_apply_) MoveWindow(hwnd_voxel_apply_, panel_w_in - 80, PAD_SM / 2, 70, ROW_H, TRUE);
        if (hwnd_voxel_preset_) MoveWindow(hwnd_voxel_preset_, 60, PAD_SM / 2, panel_w_in - 60 - 198, ROW_H, TRUE);
        int y = TOOLBAR_H + PAD_SM;
        if (hwnd_voxel_viewport_resonance_) MoveWindow(hwnd_voxel_viewport_resonance_, panel_w_in - 154, y - 2, 144, ROW_H, TRUE);
        if (hwnd_voxel_presets_list_) MoveWindow(hwnd_voxel_presets_list_, PAD_SM, y + 22, panel_w_in - 2 * PAD_SM, 94, TRUE);
        y += 128;
        if (HWND h = GetDlgItem(hwnd_rdock_voxel_, 2601)) MoveWindow(h, PAD_SM + 70, y, panel_w_in - (2 * PAD_SM) - 80, 30, TRUE);
        y += 34;
        if (HWND h = GetDlgItem(hwnd_rdock_voxel_, 2602)) MoveWindow(h, PAD_SM + 70, y, panel_w_in - (2 * PAD_SM) - 80, 30, TRUE);
        y += 34;
        if (HWND h = GetDlgItem(hwnd_rdock_voxel_, 2603)) MoveWindow(h, PAD_SM + 70, y, panel_w_in - (2 * PAD_SM) - 80, 30, TRUE);
        y += 40;
        if (hwnd_voxel_atom_nodes_) MoveWindow(hwnd_voxel_atom_nodes_, PAD_SM, y + 22, panel_w_in - 2 * PAD_SM, 86, TRUE);
        y += 116;
        if (hwnd_voxel_summary_) MoveWindow(hwnd_voxel_summary_, PAD_SM, y, panel_w_in - 2 * PAD_SM, std::max(108, panel_h_in - y - PAD_SM), TRUE);
    }

    if (hwnd_rdock_node_) {
        if (hwnd_tb_node_) MoveWindow(hwnd_tb_node_, 0, 0, panel_w_in, TOOLBAR_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_node_, 1544)) MoveWindow(h, panel_w_in - 242, PAD_SM / 2, 52, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_node_, 1534)) MoveWindow(h, panel_w_in - 186, PAD_SM / 2, 52, ROW_H, TRUE);
        if (hwnd_node_open_coh_) MoveWindow(hwnd_node_open_coh_, panel_w_in - 128, PAD_SM / 2, 118, ROW_H, TRUE);
        if (hwnd_node_expand_) MoveWindow(hwnd_node_expand_, 10, PAD_SM / 2, 72, ROW_H, TRUE);
        if (hwnd_node_play_) MoveWindow(hwnd_node_play_, 88, PAD_SM / 2, 72, ROW_H, TRUE);
        if (hwnd_node_mode_local_) MoveWindow(hwnd_node_mode_local_, 166, PAD_SM / 2, 66, ROW_H, TRUE);
        if (hwnd_node_mode_propagate_) MoveWindow(hwnd_node_mode_propagate_, 238, PAD_SM / 2, 88, ROW_H, TRUE);
        if (hwnd_node_search_) MoveWindow(hwnd_node_search_, 332, PAD_SM / 2, std::max(90, panel_w_in - 332 - 128 - 136), ROW_H, TRUE);
        if (hwnd_node_search_spawn_) MoveWindow(hwnd_node_search_spawn_, panel_w_in - 128 - 136, PAD_SM / 2, 96, ROW_H, TRUE);
        const int graph_h = ((panel_h_in - TOOLBAR_H) > 420) ? 214 : 170;
        const int graph_y = TOOLBAR_H + PAD_SM;
        const int split_w = std::max(120, panel_w_in - 3 * PAD_SM);
        const int graph_w = std::max(160, (split_w * 58) / 100);
        const int results_x = PAD_SM + graph_w + PAD_SM;
        const int results_w = std::max(120, panel_w_in - results_x - PAD_SM);
        const int slider_y = graph_y + graph_h + 10;
        if (hwnd_node_graph_) MoveWindow(hwnd_node_graph_, PAD_SM, graph_y, graph_w, graph_h, TRUE);
        if (hwnd_node_results_) MoveWindow(hwnd_node_results_, results_x, graph_y, results_w, std::max(86, graph_h - 68), TRUE);
        if (hwnd_node_export_preview_) MoveWindow(hwnd_node_export_preview_, results_x, graph_y + graph_h - 62, results_w, 28, TRUE);
        if (hwnd_node_connect_selected_) MoveWindow(hwnd_node_connect_selected_, results_x, graph_y + graph_h - 30, std::max(86, (results_w - 6) / 2), 28, TRUE);
        if (hwnd_node_disconnect_selected_) MoveWindow(hwnd_node_disconnect_selected_, results_x + std::max(86, (results_w - 6) / 2) + 6, graph_y + graph_h - 30, std::max(86, results_w - std::max(86, (results_w - 6) / 2) - 6), 28, TRUE);
        if (hwnd_node_rc_band_) MoveWindow(hwnd_node_rc_band_, PAD_SM + 56, slider_y, panel_w_in - (2 * PAD_SM + 56 + 96), 28, TRUE);
        if (hwnd_node_rc_drive_) MoveWindow(hwnd_node_rc_drive_, PAD_SM + 56, slider_y + 34, panel_w_in - (2 * PAD_SM + 56 + 96), 28, TRUE);
        if (hwnd_node_rc_apply_) MoveWindow(hwnd_node_rc_apply_, panel_w_in - PAD_SM - 90, slider_y, 90, 62, TRUE);
        if (hwnd_node_summary_) MoveWindow(hwnd_node_summary_, PAD_SM, slider_y + 70, panel_w_in - 2 * PAD_SM, panel_h_in - (slider_y + 70 + PAD_SM), TRUE);
    }

    if (hwnd_rdock_sequencer_) {
        if (hwnd_tb_sequencer_) MoveWindow(hwnd_tb_sequencer_, 0, 0, panel_w_in, TOOLBAR_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_sequencer_, 1545)) MoveWindow(h, panel_w_in - 286, PAD_SM / 2, 52, ROW_H, TRUE);
        if (HWND h = GetDlgItem(hwnd_tb_sequencer_, 1535)) MoveWindow(h, panel_w_in - 230, PAD_SM / 2, 52, ROW_H, TRUE);
        if (hwnd_seq_play_) MoveWindow(hwnd_seq_play_, 10, PAD_SM / 2, 64, ROW_H, TRUE);
        if (hwnd_seq_loop_) MoveWindow(hwnd_seq_loop_, 80, PAD_SM / 2, 82, ROW_H, TRUE);
        if (hwnd_seq_stress_overlay_) MoveWindow(hwnd_seq_stress_overlay_, 168, PAD_SM / 2, 108, ROW_H, TRUE);
        if (hwnd_seq_timeline_) MoveWindow(hwnd_seq_timeline_, PAD_SM, TOOLBAR_H + PAD_SM, panel_w_in - 2 * PAD_SM, 188, TRUE);
        if (hwnd_seq_add_key_) MoveWindow(hwnd_seq_add_key_, PAD_SM, TOOLBAR_H + PAD_SM + 196, 84, ROW_H, TRUE);
        if (hwnd_seq_motion_match_) MoveWindow(hwnd_seq_motion_match_, PAD_SM + 92, TOOLBAR_H + PAD_SM + 196, 110, ROW_H, TRUE);
        if (hwnd_seq_summary_) MoveWindow(hwnd_seq_summary_, PAD_SM, TOOLBAR_H + PAD_SM + 232, panel_w_in - 2 * PAD_SM, panel_h_in - (TOOLBAR_H + PAD_SM + 232 + PAD_SM), TRUE);
    }

    RefreshViewportResonanceOverlay();
    SyncWindowMenu();
    // Content browser children already handled in their own WM_SIZE path.
}


void App::RefreshViewportResonanceOverlay() {
    if (!hwnd_viewport_resonance_overlay_) return;
    std::wstring out;
    out += resonance_view_ ? L"Viewport Resonance Overlay [derived-only]\r\n" : L"Viewport Resonance Overlay [standby]\r\n";
    out += L"Nodes represent Fourier carrier anchors that group AI-write anchor sets by operator path.\r\n\r\n";
    wchar_t line[256]{};
    swprintf(line, 256, L"Band=%d  Phase=%.2f\r\n", spectrum_band_i32_, (double)spectrum_phase_f32_);
    out += line;
    const int sel = node_graph_selected_i32_;
    if (sel >= 0 && sel < (int)node_graph_items_w_.size()) {
        const uint32_t anchor_id = (sel < (int)node_graph_anchor_id_u32_.size()) ? node_graph_anchor_id_u32_[(size_t)sel] : 0u;
        const int strength = (sel < (int)node_graph_strength_pct_i32_.size()) ? node_graph_strength_pct_i32_[(size_t)sel] : 0;
        out += L"Selected carrier: " + node_graph_items_w_[(size_t)sel] + L"\r\n";
        if (sel < (int)node_graph_operator_path_w_.size()) out += L"Operator path: " + node_graph_operator_path_w_[(size_t)sel] + L"\r\n";
        swprintf(line, 256, L"Carrier anchor id=%u  coupling=%d%%\r\n", anchor_id, strength);
        out += line;
        const auto src_pins = ew_split_trim_pin_list((sel < (int)node_graph_output_pins_w_.size()) ? node_graph_output_pins_w_[(size_t)sel] : L"");
        if (!src_pins.empty()) {
            out += L"Viewport-selected output pin: ";
            out += src_pins[(size_t)std::max(0, std::min((int)src_pins.size() - 1, node_source_pin_selected_i32_))] + L"\r\n";
        }
        if (sel < (int)node_graph_edge_label_w_.size() && !node_graph_edge_label_w_[(size_t)sel].empty()) out += L"Resonance link focus: " + node_graph_edge_label_w_[(size_t)sel] + L"\r\n";
    }
    if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
        const auto& o = scene_->objects[(size_t)scene_->selected];
        out += L"Selected object: " + utf8_to_wide(o.name_utf8) + L"\r\n";
        swprintf(line, 256, L"Object anchor=%u  radius=%.2fm  emissive=%.2f\r\n", o.anchor_id_u32, (double)o.radius_m_f32, (double)o.emissive_f32);
        out += line;
        const int couplings = 3 + (int)(coh_highlight_set_w_.size() > 0 ? std::min<size_t>(4u, coh_highlight_set_w_.size()) : 0u);
        swprintf(line, 256, L"Viewport spheres=%d  resonance links=%d\r\n", 1 + couplings, couplings);
        out += line;
    } else {
        out += L"Selected object: none\r\nViewport spheres=0  resonance links=0\r\n";
    }
    out += node_play_excitation_ ? L"Carrier pulse overlay: active\r\n" : L"Carrier pulse overlay: idle\r\n";
    if (seq_stress_overlay_enabled_) {
        const int stress_pct = std::max(0, std::min(100, (int)std::lround(std::fabs((double)spectrum_phase_f32_) * 18.0) + (seq_play_enabled_ ? 24 : 9)));
        const int pain_pct = std::max(0, std::min(100, stress_pct / 2 + (node_play_excitation_ ? 12 : 0)));
        swprintf(line, 256, L"Stress advisory=%d%%  pain advisory=%d%%\r\n", stress_pct, pain_pct);
        out += line;
    } else {
        out += L"Stress advisory overlay: OFF\r\n";
    }
    out += L"Gold/orange links brighten and thicken with coupling strength. Selected graph lanes now project their chosen pin pair and first derived resonance link into this overlay.\r\n";
    SetWindowTextW(hwnd_viewport_resonance_overlay_, out.c_str());
}


void App::SyncWindowMenu() {
    HMENU menu = hwnd_main_ ? GetMenu(hwnd_main_) : nullptr;
    if (!menu) return;
    auto check = [&](UINT id, bool on) {
        CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    };
    check(9201u, content_visible_);
    check(9202u, hwnd_ai_panel_ && IsWindowVisible(hwnd_ai_panel_));
    for (uint32_t i = 0; i < 6u; ++i) check(9203u + i, rdock_panel_visible_[i]);
    check(9209u, live_mode_enabled_);
    DrawMenuBar(hwnd_main_);
}

int App::ResolveNextVisibleDockTab(int preferred_i32) const {
    const int count = 6;
    if (preferred_i32 >= 0 && preferred_i32 < count && rdock_panel_visible_[preferred_i32]) return preferred_i32;
    for (int i = 0; i < count; ++i) if (rdock_panel_visible_[i]) return i;
    return -1;
}

void App::ApplyRightDockVisibility() {
    const int active = ResolveNextVisibleDockTab((int)rdock_tab_index_u32_);
    if (active < 0) return;
    rdock_tab_index_u32_ = (uint32_t)active;
    if (hwnd_rdock_tab_) {
        TabCtrl_SetCurSel(hwnd_rdock_tab_, active);
        for (int i = 0; i < 6; ++i) {
            TCITEMW tie{};
            tie.mask = TCIF_STATE;
            tie.dwStateMask = TCIS_HIGHLIGHTED;
            tie.dwState = rdock_panel_visible_[i] ? 0u : TCIS_HIGHLIGHTED;
            TabCtrl_SetItem(hwnd_rdock_tab_, i, &tie);
        }
    }
    if (hwnd_rdock_outliner_) ShowWindow(hwnd_rdock_outliner_, (active == 0 && rdock_panel_visible_[0]) ? SW_SHOW : SW_HIDE);
    if (hwnd_rdock_details_)  ShowWindow(hwnd_rdock_details_,  (active == 1 && rdock_panel_visible_[1]) ? SW_SHOW : SW_HIDE);
    if (hwnd_rdock_asset_)    ShowWindow(hwnd_rdock_asset_,    (active == 2 && rdock_panel_visible_[2]) ? SW_SHOW : SW_HIDE);
    if (hwnd_rdock_voxel_)    ShowWindow(hwnd_rdock_voxel_,    (active == 3 && rdock_panel_visible_[3]) ? SW_SHOW : SW_HIDE);
    if (hwnd_rdock_node_)     ShowWindow(hwnd_rdock_node_,     (active == 4 && rdock_panel_visible_[4]) ? SW_SHOW : SW_HIDE);
    if (hwnd_rdock_sequencer_)ShowWindow(hwnd_rdock_sequencer_,(active == 5 && rdock_panel_visible_[5]) ? SW_SHOW : SW_HIDE);
    SyncWindowMenu();
}

void App::SetRightDockActiveTab(uint32_t idx_u32) {
    if (idx_u32 >= 6u || !rdock_panel_visible_[idx_u32]) return;
    rdock_tab_index_u32_ = idx_u32;
    ApplyRightDockVisibility();
    LayoutChildren(client_w_, client_h_);
}

void App::SetRightDockPanelVisible(uint32_t idx_u32, bool visible) {
    if (idx_u32 >= 6u) return;
    if (rdock_panel_locked_[idx_u32] && !visible) {
        MessageBoxW(hwnd_main_, L"This panel is locked. Unlock it before closing.", L"Genesis Engine", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!visible) {
        uint32_t remaining = 0u;
        for (uint32_t i = 0; i < 6u; ++i) if (i != idx_u32 && rdock_panel_visible_[i]) remaining++;
        if (remaining == 0u) {
            MessageBoxW(hwnd_main_, L"At least one right-dock panel must remain visible.", L"Genesis Engine", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
    rdock_panel_visible_[idx_u32] = visible;
    if (visible) rdock_tab_index_u32_ = idx_u32;
    ApplyRightDockVisibility();
    LayoutChildren(client_w_, client_h_);
}

std::wstring App::GetNodeSearchQuery() const {
    const int qlen = hwnd_node_search_ ? GetWindowTextLengthW(hwnd_node_search_) : 0;
    if (qlen <= 0 || !hwnd_node_search_) return std::wstring();
    std::vector<wchar_t> qbuf((size_t)qlen + 1u, 0);
    GetWindowTextW(hwnd_node_search_, qbuf.data(), qlen + 1);
    return std::wstring(qbuf.data());
}

namespace {
static std::vector<std::wstring> ew_split_trim_pin_list(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    auto flush = [&]() {
        size_t a = 0; while (a < cur.size() && iswspace(cur[a])) ++a;
        size_t b = cur.size(); while (b > a && iswspace(cur[b - 1])) --b;
        if (b > a) out.push_back(cur.substr(a, b - a));
        cur.clear();
    };
    for (wchar_t ch : s) { if (ch == L',') flush(); else cur.push_back(ch); }
    flush();
    return out;
}

static bool ew_pin_pair_compatible(const std::wstring& o, const std::wstring& i) {
    return (o == L"exec_out" && i == L"exec_in") ||
           (o == L"scalar_out" && i == L"scalar_in") ||
           (o == L"carrier_out" && i == L"carrier_in") ||
           (o == L"event_out" && i == L"event_in") ||
           (o == L"route_out" && i == L"route_in") ||
           (o == L"file_out" && i == L"file_in") ||
           (o == L"repo_out" && i == L"repo_in") ||
           (o == L"lang_out" && i == L"lang_in");
}

static bool ew_find_pin_pair(const std::wstring& source_output_pins, const std::wstring& target_input_pins,
                             int selected_source_idx, int selected_target_idx,
                             std::wstring* out_src, std::wstring* out_dst,
                             int* out_src_idx, int* out_dst_idx) {
    const auto outs = ew_split_trim_pin_list(source_output_pins);
    const auto ins = ew_split_trim_pin_list(target_input_pins);
    if (outs.empty() || ins.empty()) return false;
    if (selected_source_idx >= 0 && selected_source_idx < (int)outs.size() &&
        selected_target_idx >= 0 && selected_target_idx < (int)ins.size() &&
        ew_pin_pair_compatible(outs[(size_t)selected_source_idx], ins[(size_t)selected_target_idx])) {
        if (out_src) *out_src = outs[(size_t)selected_source_idx];
        if (out_dst) *out_dst = ins[(size_t)selected_target_idx];
        if (out_src_idx) *out_src_idx = selected_source_idx;
        if (out_dst_idx) *out_dst_idx = selected_target_idx;
        return true;
    }
    for (size_t oi = 0; oi < outs.size(); ++oi) for (size_t ii = 0; ii < ins.size(); ++ii) if (ew_pin_pair_compatible(outs[oi], ins[ii])) {
        if (out_src) *out_src = outs[oi];
        if (out_dst) *out_dst = ins[ii];
        if (out_src_idx) *out_src_idx = (int)oi;
        if (out_dst_idx) *out_dst_idx = (int)ii;
        return true;
    }
    return false;
}
}

void App::ClampNodePinSelections() {
    const std::wstring src = ((size_t)node_graph_selected_i32_ < node_graph_output_pins_w_.size()) ? node_graph_output_pins_w_[(size_t)node_graph_selected_i32_] : L"";
    const auto outs = ew_split_trim_pin_list(src);
    if (outs.empty()) { node_source_pin_selected_i32_ = 0; node_source_pin_hover_i32_ = -1; }
    else {
        if (node_source_pin_selected_i32_ < 0) node_source_pin_selected_i32_ = 0;
        if (node_source_pin_selected_i32_ >= (int)outs.size()) node_source_pin_selected_i32_ = (int)outs.size() - 1;
        if (node_source_pin_hover_i32_ >= (int)outs.size()) node_source_pin_hover_i32_ = -1;
    }
    const int result_sel = hwnd_node_results_ ? (int)SendMessageW(hwnd_node_results_, LB_GETCURSEL, 0, 0) : -1;
    const std::wstring dst = (result_sel >= 0 && (size_t)result_sel < node_palette_entries_.size()) ? node_palette_entries_[(size_t)result_sel].input_pins_w : L"";
    const auto ins = ew_split_trim_pin_list(dst);
    if (ins.empty()) node_target_pin_selected_i32_ = 0;
    else {
        if (node_target_pin_selected_i32_ < 0) node_target_pin_selected_i32_ = 0;
        if (node_target_pin_selected_i32_ >= (int)ins.size()) node_target_pin_selected_i32_ = (int)ins.size() - 1;
    }
    std::wstring chosen_src, chosen_dst; int src_idx = 0, dst_idx = 0;
    if (ew_find_pin_pair(src, dst, node_source_pin_selected_i32_, node_target_pin_selected_i32_, &chosen_src, &chosen_dst, &src_idx, &dst_idx)) {
        node_source_pin_selected_i32_ = src_idx;
        node_target_pin_selected_i32_ = dst_idx;
    }
}

std::wstring App::DescribeNodeCompatibility(const std::wstring& source_output_pins, const std::wstring& target_input_pins) const {
    std::wstring src_pin, dst_pin;
    if (ew_find_pin_pair(source_output_pins, target_input_pins, node_source_pin_selected_i32_, node_target_pin_selected_i32_, &src_pin, &dst_pin, nullptr, nullptr)) {
        return L"Yes — connectable from current source via " + src_pin + L" -> " + dst_pin + L".";
    }
    const auto outs = ew_split_trim_pin_list(source_output_pins);
    const auto ins = ew_split_trim_pin_list(target_input_pins);
    if (outs.empty() || ins.empty()) return L"No — current source has no compatible output/input pins.";
    return L"No — current source/output pins do not match this node's required input pins.";
}

void App::RebuildNodePaletteEntries() {
    const int node_sel = node_graph_selected_i32_;
    node_palette_entries_.clear();
    if (node_sel < 0 || (size_t)node_sel >= node_graph_items_w_.size()) return;

    std::wstring query_w = GetNodeSearchQuery();
    std::wstring query_fold = query_w;
    std::transform(query_fold.begin(), query_fold.end(), query_fold.begin(), towlower);

    auto matches_query = [&](const std::wstring& label, const std::wstring& lookup, const std::wstring& path,
                             const std::wstring& placement, const std::wstring& interconnect, const std::wstring& effect,
                             const std::wstring& language_hint, const std::wstring& export_scope,
                             const std::wstring& language_policy, const std::wstring& input_pins,
                             const std::wstring& output_pins, const std::wstring& doc_key)->bool {
        if (query_fold.empty()) return true;
        auto fold = [](std::wstring t)->std::wstring { std::transform(t.begin(), t.end(), t.begin(), towlower); return t; };
        const std::wstring blob = fold(label + L" " + lookup + L" " + path + L" " + placement + L" " + interconnect + L" " + effect + L" " + language_hint + L" " + export_scope + L" " + language_policy + L" " + input_pins + L" " + output_pins + L" " + doc_key);
        return blob.find(query_fold) != std::wstring::npos;
    };
    const std::wstring source_output_pins = ((size_t)node_sel < node_graph_output_pins_w_.size()) ? node_graph_output_pins_w_[(size_t)node_sel] : L"";
    auto add_palette = [&](const std::wstring& label,
                           const std::wstring& lookup,
                           const std::wstring& path,
                           const std::wstring& interconnect,
                           const std::wstring& effect,
                           const std::wstring& placement,
                           const std::wstring& language_hint,
                           const std::wstring& export_scope,
                           const std::wstring& language_policy,
                           const std::wstring& input_pins,
                           const std::wstring& output_pins,
                           const std::wstring& doc_key,
                           int strength,
                           bool ancilla_anchor,
                           bool coherence_hint,
                           bool language_locked) {
        if (!matches_query(label, lookup, path, placement, interconnect, effect, language_hint, export_scope, language_policy, input_pins, output_pins, doc_key)) return;
        if (node_palette_entries_.size() >= 32u) return;
        for (const auto& e : node_palette_entries_) {
            if (_wcsicmp(e.lookup_name_w.c_str(), lookup.c_str()) == 0) return;
        }
        NodePaletteEntry e{};
        e.label_w = label;
        e.lookup_name_w = lookup;
        e.operator_path_w = path;
        e.interconnect_w = interconnect;
        e.effect_w = effect;
        e.placement_w = placement;
        e.language_hint_w = language_hint;
        e.export_scope_w = export_scope;
        e.language_policy_w = language_policy;
        e.input_pins_w = input_pins;
        e.output_pins_w = output_pins;
        e.doc_key_w = doc_key;
        e.contract_ready = (!label.empty() && !lookup.empty() && !path.empty() && !input_pins.empty() && !output_pins.empty() && !doc_key.empty());
        e.compatibility_w = DescribeNodeCompatibility(source_output_pins, input_pins);
        e.strength_pct_i32 = strength;
        e.ancilla_anchor = ancilla_anchor;
        e.coherence_hint = coherence_hint;
        e.language_locked = language_locked;
        node_palette_entries_.push_back(e);
    };

    const std::wstring source_label = node_graph_items_w_[(size_t)node_sel];
    const std::wstring source_lookup = (size_t)node_sel < node_graph_lookup_name_w_.size() ? node_graph_lookup_name_w_[(size_t)node_sel] : L"";
    const bool is_root = source_lookup.find(L"carrier_root") != std::wstring::npos;
    const bool is_sched = is_root || source_lookup.find(L"scheduler") != std::wstring::npos;
    const bool is_var = is_root || source_lookup.find(L"variable") != std::wstring::npos;
    const bool is_func = is_root || source_lookup.find(L"function") != std::wstring::npos || source_lookup.find(L"operator") != std::wstring::npos;
    const bool is_events = is_root || source_lookup.find(L"event") != std::wstring::npos;
    const bool is_dispatch = is_root || source_lookup.find(L"dispatcher") != std::wstring::npos || source_lookup.find(L"route") != std::wstring::npos;
    const bool is_project = is_root || source_lookup.find(L"project") != std::wstring::npos || source_lookup.find(L"export") != std::wstring::npos || source_lookup.find(L"language") != std::wstring::npos;

    if (is_sched || source_label.find(L"Scheduler") != std::wstring::npos) {
        add_palette(L"Sequence -> Begin Tick", L"begin_tick", L"basic syntax -> scheduler -> begin tick",
            L"delay_window, tick_fanout, flow_sequence", L"establishes a bounded per-tick entry for a schedule path", L"place by dragging off Scheduler Carrier or root", L"n/a", L"none", L"language inferred from upstream lane", L"exec_in", L"exec_out", L"begin_tick", 70, true, false, false);
        add_palette(L"Gate -> Delay Window", L"delay_window", L"basic syntax -> scheduler -> delay window",
            L"begin_tick, tick_fanout, emit_control_packet", L"inserts a bounded delay/timing gate before later schedule work", L"place after Begin Tick or other scheduler nodes", L"n/a", L"none", L"language inferred from upstream lane", L"exec_in", L"exec_out", L"delay_window", 64, true, false, false);
        add_palette(L"Dispatch -> Tick Fanout", L"tick_fanout", L"basic syntax -> scheduler -> tick fanout",
            L"begin_tick, delay_window, emit_control_packet", L"fans one schedule carrier into bounded downstream anchor work", L"place after scheduler gates when routing to multiple bounded lanes", L"n/a", L"none", L"language inferred from upstream lane", L"exec_in", L"exec_out, route_out", L"tick_fanout", 66, true, false, false);
    }
    if (is_var || source_label.find(L"Variable") != std::wstring::npos) {
        add_palette(L"Variable -> Get Anchor Scalar", L"get_anchor_scalar", L"basic syntax -> variables -> get anchor scalar",
            L"set_anchor_scalar, compose_carrier_tuple, apply_substrate_op", L"reads one scalar-like value from the current anchor lane", L"place under Variable Carrier or after flow sequence nodes", L"n/a", L"none", L"language inferred from upstream lane", L"exec_in, scalar_in", L"scalar_out", L"get_anchor_scalar", 60, true, false, false);
        add_palette(L"Variable -> Set Anchor Scalar", L"set_anchor_scalar", L"basic syntax -> variables -> set anchor scalar",
            L"get_anchor_scalar, compose_carrier_tuple, emit_control_packet", L"writes one scalar-like value into the current anchor lane", L"place under Variable Carrier when actuating state or export payload fields", L"n/a", L"none", L"language inferred from upstream lane", L"exec_in, scalar_in", L"scalar_out", L"set_anchor_scalar", 62, true, false, false);
        add_palette(L"Vector -> Compose Carrier Tuple", L"compose_carrier_tuple", L"basic syntax -> variables -> compose carrier tuple",
            L"get_anchor_scalar, fourier_transform, emit_control_packet", L"packs bounded values into one carrier/vector tuple", L"place after Get/Set Anchor Scalar before transforms or export", L"n/a", L"none", L"language inferred from upstream lane", L"scalar_in", L"carrier_out", L"compose_carrier_tuple", 58, true, false, false);
    }
    if (is_func || source_label.find(L"Function") != std::wstring::npos || source_label.find(L"Operator") != std::wstring::npos) {
        add_palette(L"Operator -> Apply Substrate Op", L"apply_substrate_op", L"basic syntax -> functions -> apply substrate operator",
            L"get_anchor_scalar, set_anchor_scalar, emit_control_packet", L"applies one bounded substrate-side operation", L"place under Function Carrier or downstream of tuple/trigger nodes", L"n/a", L"none", L"language inferred from upstream lane", L"exec_in, scalar_in, carrier_in", L"scalar_out, carrier_out", L"apply_substrate_op", 72, true, false, false);
        add_palette(L"Carrier -> Fourier Transform", L"fourier_transform", L"basic syntax -> functions -> fourier carrier transform",
            L"compose_carrier_tuple, export_write_file, export_whole_repo", L"derives a carrier-frequency representation for downstream logic/export", L"place after tuple/vector nodes or before export nodes", L"auto from upstream carrier context", L"derived carrier", L"AI auto-suggest unless downstream override or lock applies", L"carrier_in", L"carrier_out", L"fourier_transform", 68, true, false, false);
    }
    if (is_events || source_label.find(L"Event") != std::wstring::npos) {
        add_palette(L"Event -> On Trigger", L"on_trigger", L"basic syntax -> events -> trigger hook",
            L"emit_control_packet, route_to_carrier, flow_sequence", L"introduces a bounded trigger source", L"place under Event Carrier or event-derived child carriers", L"n/a", L"none", L"language inferred from upstream lane", L"event_in", L"event_out", L"on_trigger", 66, true, false, false);
        add_palette(L"Event -> On Coherence Match", L"on_coherence_match", L"basic syntax -> events -> coherence match",
            L"export_repo_patch, route_to_carrier, emit_control_packet", L"fires from coherence-backed matching on the selected lane", L"place under Event Carrier or coherence-hit carriers", L"n/a", L"none", L"language inferred from upstream lane", L"event_in", L"event_out, repo_out", L"on_coherence_match", 62, true, false, false);
    }
    if (is_dispatch || source_label.find(L"Dispatch") != std::wstring::npos) {
        add_palette(L"Dispatch -> Emit Control Packet", L"emit_control_packet", L"basic syntax -> dispatchers -> emit control packet",
            L"tick_fanout, on_trigger, flow_sequence", L"emits one bounded runtime/editor control packet", L"place under Dispatcher Carrier or after trigger/tick nodes", L"engine-native packet language", L"packet dispatch", L"locked to engine packet/runtime integration", L"exec_in, route_in", L"route_out", L"emit_control_packet", 68, true, false, false);
        add_palette(L"Dispatch -> Route To Carrier", L"route_to_carrier", L"basic syntax -> dispatchers -> route to carrier",
            L"emit_control_packet, export_repo_patch, export_whole_repo", L"routes output into another carrier lane", L"place under Dispatcher Carrier when reconverging or retargeting lanes", L"n/a", L"none", L"language inferred from upstream lane", L"route_in", L"route_out", L"route_to_carrier", 64, true, false, false);
    }
    if (is_project || source_label.find(L"Project") != std::wstring::npos) {
        add_palette(L"Export -> Write File Artifact", L"export_write_file", L"basic syntax -> project substrate -> export file artifact",
            L"fourier_transform, export_repo_patch, set_file_language", L"exports one file artifact from the linked project substrate", L"place under Project Work Substrate Carrier or export child lanes", L"auto from node context unless locked by file type", L"single file artifact", L"AI auto-suggest by default; locked integrations override", L"carrier_in, file_in, lang_in", L"file_out", L"export_write_file", 76, true, false, false);
        add_palette(L"Export -> Materialize Repo Patch", L"export_repo_patch", L"basic syntax -> project substrate -> export repo patch",
            L"export_write_file, coherence hints, route_to_carrier", L"assembles an exportable repo diff from the project work substrate", L"place under Project Work Substrate Carrier when bundling multiple file changes", L"patch bundle; language derives per file", L"repo patch", L"per-file language with lock-aware override rules", L"repo_in, lang_in", L"repo_out", L"export_repo_patch", 72, true, false, false);
        add_palette(L"Export -> Whole Repo Bundle", L"export_whole_repo", L"basic syntax -> project substrate -> export whole repo",
            L"fourier_transform, auto_language, set_file_language", L"exports the whole repo from the linked project substrate", L"place under Project Work Substrate Carrier when materializing a full repository export", L"mixed per-file language; constrained files remain locked", L"whole repository", L"AI auto-suggest per file; user override only when unlocked", L"repo_in, lang_in", L"repo_out", L"export_whole_repo", 74, true, false, false);
        add_palette(L"Language -> Auto Suggest", L"auto_language", L"basic syntax -> project substrate -> auto language suggestion",
            L"export_write_file, export_whole_repo, set_file_language", L"uses node context and temporal coherence to auto-suggest per-file language where not locked by file type", L"place before export nodes when you want AI-selected language routing", L"AI auto-suggested from node context + temporal coherence", L"language planning", L"temporal coherence picks language unless file role is locked", L"lang_in", L"lang_out", L"auto_language", 64, true, false, false);
        add_palette(L"Language -> Set File Language", L"set_file_language", L"basic syntax -> project substrate -> set file language",
            L"export_write_file, export_whole_repo, auto_language", L"lets the user override per-file language unless the file type is constrained by platform/integration language rules", L"place immediately upstream of export nodes for user-selected file language", L"user-selected unless constrained by required integration language", L"language override", L"user may override only unlocked file roles", L"lang_in", L"lang_out, file_out", L"set_file_language", 60, true, false, false);
    }
    if (source_lookup.find(L"carrier_root") != std::wstring::npos || source_lookup.find(L"flow") != std::wstring::npos) {
        add_palette(L"Flow -> Sequence", L"flow_sequence", L"basic syntax -> flow -> sequence",
            L"begin_tick, tick_fanout, route_to_carrier", L"continues the current logic stream without diverging lanes", L"place between schedule, dispatch, or export steps when keeping one stream", L"n/a", L"flow control", L"language inherited from upstream/downstream lane", L"exec_in", L"exec_out", L"flow_sequence", 54, true, false, false);
        add_palette(L"Flow -> Branch", L"flow_branch", L"basic syntax -> flow -> branch",
            L"on_trigger, on_coherence_match, route_to_carrier", L"starts a divergent logic branch that may not reconverge into the same processing stream", L"place after events or condition-like trigger nodes when creating a new divergence lane", L"n/a", L"flow control", L"language inherited from upstream/downstream lane", L"exec_in", L"exec_out", L"flow_branch", 56, true, false, false);
    }
    for (size_t i = 0; i < ai_coherence_items_.size() && i < 8u; ++i) {
        std::wstring label = L"Coherence -> " + ai_coherence_items_[i].path_w;
        add_palette(label, L"coherence_hint", L"coherence -> lookup -> derived suggestion",
            L"event_carrier, export_repo_patch, route_to_carrier", L"suggests a node spawn path using the currently visible coherence result", L"place under Event/Project carriers when acting on file/reference relationships", L"inherits from matched file/integration context", L"coherence-derived", L"inherits export/language rules from matched context", L"event_in, repo_in", L"repo_out", L"coherence_hint", 44 + (int)(i % 4) * 4, true, true, false);
    }
}

bool App::ConnectNodePaletteEntry(size_t palette_idx) {
    if (palette_idx >= node_palette_entries_.size()) return false;
    const auto& entry = node_palette_entries_[palette_idx];
    std::wstring src_pin, dst_pin;
    int src_idx = 0, dst_idx = 0;
    const std::wstring source_output_pins = ((size_t)node_graph_selected_i32_ < node_graph_output_pins_w_.size()) ? node_graph_output_pins_w_[(size_t)node_graph_selected_i32_] : L"";
    const bool pair_ok = ew_find_pin_pair(source_output_pins, entry.input_pins_w, node_source_pin_selected_i32_, node_target_pin_selected_i32_, &src_pin, &dst_pin, &src_idx, &dst_idx);
    if (!pair_ok) {
        std::wstring msg = L"Node Graph: connect blocked. ";
        msg += entry.compatibility_w.empty() ? L"The selected result does not expose a compatible pin path." : entry.compatibility_w;
        if (!entry.export_scope_w.empty() && _wcsicmp(entry.export_scope_w.c_str(), L"none") != 0) {
            msg += L"  Export implication: ";
            msg += entry.export_scope_w;
            if (!entry.language_policy_w.empty()) msg += L" / " + entry.language_policy_w;
        }
        if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, msg.c_str());
        RefreshNodePanel();
        return false;
    }
    node_source_pin_selected_i32_ = src_idx;
    node_target_pin_selected_i32_ = dst_idx;
    return SpawnNodePaletteEntry(palette_idx);
}

bool App::DisconnectSelectedNode() {
    const int node_sel = node_graph_selected_i32_;
    if (node_sel < 0 || (size_t)node_sel >= node_graph_anchor_id_u32_.size()) return false;
    const uint32_t anchor_id = node_graph_anchor_id_u32_[(size_t)node_sel];
    const uint32_t parent_anchor = ((size_t)node_sel < node_graph_parent_i32_.size() && node_graph_parent_i32_[(size_t)node_sel] >= 0 && (size_t)node_graph_parent_i32_[(size_t)node_sel] < node_graph_anchor_id_u32_.size()) ? node_graph_anchor_id_u32_[(size_t)node_graph_parent_i32_[(size_t)node_sel]] : 0u;
    bool changed = false;
    for (auto& sp : node_spawned_items_) {
        if (sp.anchor_id_u32 == anchor_id) {
            sp.parent_anchor_id_u32 = 0u;
            sp.compatibility_w = L"Disconnected — no current parent/source edge.";
            changed = true;
            break;
        }
    }
    if (changed) {
        std::wstring cleared_edge = L"derived parent edge";
        for (const auto& e : node_edge_records_) {
            if (e.child_anchor_id_u32 == anchor_id) { cleared_edge = e.edge_label_w.empty() ? cleared_edge : e.edge_label_w; break; }
        }
        node_edge_records_.erase(std::remove_if(node_edge_records_.begin(), node_edge_records_.end(), [&](const NodeEdgeRecord& e){ return e.child_anchor_id_u32 == anchor_id || (e.parent_anchor_id_u32 == parent_anchor && e.child_anchor_id_u32 == anchor_id); }), node_edge_records_.end());
        if (hwnd_ai_status_) {
            std::wstring msg = L"Node Graph: disconnected selected node and cleared edge \"" + cleared_edge + L"\". Use Connect Selected to stage a new pre-commit edge preview.";
            SetWindowTextW(hwnd_ai_status_, msg.c_str());
        }
        RefreshNodePanel();
        return true;
    }
    if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Node Graph: selected node has no disconnectable derived parent edge.");
    RefreshNodePanel();
    return false;
}

bool App::SpawnNodePaletteEntry(size_t palette_idx) {
    if (palette_idx >= node_palette_entries_.size()) return false;
    const int parent_sel = node_graph_selected_i32_;
    const uint32_t parent_anchor = (parent_sel >= 0 && (size_t)parent_sel < node_graph_anchor_id_u32_.size()) ? node_graph_anchor_id_u32_[(size_t)parent_sel] : 7000u;
    const auto& entry = node_palette_entries_[palette_idx];
    NodeSpawnedItem sp{};
    sp.label_w = entry.label_w;
    sp.lookup_name_w = entry.lookup_name_w;
    sp.operator_path_w = entry.operator_path_w;
    sp.interconnect_w = entry.interconnect_w;
    sp.effect_w = entry.effect_w;
    sp.placement_w = entry.placement_w;
    sp.language_hint_w = entry.language_hint_w;
    sp.export_scope_w = entry.export_scope_w;
    sp.language_policy_w = entry.language_policy_w;
    sp.input_pins_w = entry.input_pins_w;
    sp.output_pins_w = entry.output_pins_w;
    sp.doc_key_w = entry.doc_key_w;
    sp.contract_ready = entry.contract_ready;
    sp.compatibility_w = entry.compatibility_w;
    sp.parent_anchor_id_u32 = parent_anchor;
    sp.strength_pct_i32 = std::max(18, std::min(100, entry.strength_pct_i32));
    sp.anchor_id_u32 = parent_anchor + 500u + (uint32_t)node_spawned_items_.size() * 7u + (entry.coherence_hint ? 3u : 0u);
    sp.ancilla_anchor = entry.ancilla_anchor;
    sp.coherence_hint = entry.coherence_hint;
    sp.language_locked = entry.language_locked;
    int parent_seq = 0;
    int parent_div = 0;
    std::wstring parent_path;
    if (parent_sel >= 0 && (size_t)parent_sel < node_graph_sequence_i32_.size()) parent_seq = node_graph_sequence_i32_[(size_t)parent_sel];
    if (parent_sel >= 0 && (size_t)parent_sel < node_graph_divergence_i32_.size()) parent_div = node_graph_divergence_i32_[(size_t)parent_sel];
    if (parent_sel >= 0 && (size_t)parent_sel < node_graph_operator_path_w_.size()) parent_path = node_graph_operator_path_w_[(size_t)parent_sel];
    const bool diverges = !parent_path.empty() && _wcsicmp(parent_path.c_str(), entry.operator_path_w.c_str()) != 0 && entry.operator_path_w.find(L"flow -> sequence") == std::wstring::npos;
    sp.sequence_i32 = std::max(1, parent_seq + 1);
    sp.divergence_i32 = diverges ? ((parent_div > 0 ? parent_div : 1) + (int)node_spawned_items_.size() + 1) : std::max(1, parent_div);
    node_spawned_items_.push_back(sp);
    NodeEdgeRecord edge{};
    edge.parent_i32 = parent_sel;
    edge.child_i32 = -1;
    edge.parent_anchor_id_u32 = parent_anchor;
    edge.child_anchor_id_u32 = sp.anchor_id_u32;
    std::wstring src_pin, dst_pin;
    if (ew_find_pin_pair(((size_t)parent_sel < node_graph_output_pins_w_.size()) ? node_graph_output_pins_w_[(size_t)parent_sel] : L"", entry.input_pins_w, node_source_pin_selected_i32_, node_target_pin_selected_i32_, &src_pin, &dst_pin, nullptr, nullptr)) edge.edge_label_w = src_pin + L" -> " + dst_pin;
    else {
        const size_t arrow = entry.compatibility_w.find(L"via ");
        edge.edge_label_w = (arrow != std::wstring::npos) ? entry.compatibility_w.substr(arrow + 4u) : L"derived parent edge";
    }
    node_edge_records_.push_back(edge);
    if (hwnd_ai_status_) {
        std::wstring msg = L"Node Graph: spawned \"" + entry.label_w + L"\" from carrier \"";
        if (parent_sel >= 0 && (size_t)parent_sel < node_graph_items_w_.size()) msg += node_graph_items_w_[(size_t)parent_sel];
        else msg += L"root";
        msg += L"\" via ";
        msg += edge.edge_label_w.empty() ? L"derived parent edge" : edge.edge_label_w;
        msg += L". Disconnect Selected will clear this derived edge without deleting the node.";
        SetWindowTextW(hwnd_ai_status_, msg.c_str());
    }
    RefreshNodePanel();
    return true;
}

void App::RefreshNodePanel() {
    if (!hwnd_node_summary_) return;
    node_graph_items_w_.clear();
    node_graph_item_level_i32_.clear();
    node_graph_parent_i32_.clear();
    node_graph_strength_pct_i32_.clear();
    node_graph_anchor_id_u32_.clear();
    node_graph_operator_path_w_.clear();
    node_graph_lookup_name_w_.clear();
    node_graph_interconnect_w_.clear();
    node_graph_effect_w_.clear();
    node_graph_placement_w_.clear();
    node_graph_language_hint_w_.clear();
    node_graph_export_scope_w_.clear();
    node_graph_language_policy_w_.clear();
    node_graph_input_pins_w_.clear();
    node_graph_output_pins_w_.clear();
    node_graph_doc_key_w_.clear();
    node_graph_contract_ready_u8_.clear();
    node_graph_compatibility_w_.clear();
    node_graph_edge_in_i32_.clear();
    node_graph_edge_out_i32_.clear();
    node_graph_edge_label_w_.clear();
    node_graph_sequence_i32_.clear();
    node_graph_divergence_i32_.clear();
    node_graph_is_ancilla_u8_.clear();
    node_graph_language_locked_u8_.clear();

    uint32_t selected_anchor_id_u32 = 0u;
    std::wstring selected_name_w = L"none";
    if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
        const auto& o = scene_->objects[(size_t)scene_->selected];
        selected_anchor_id_u32 = o.anchor_id_u32;
        selected_name_w = utf8_to_wide(o.name_utf8);
    }
    const uint32_t carrier_root_anchor = selected_anchor_id_u32 ? selected_anchor_id_u32 : 7000u;
    auto push_node = [&](const std::wstring& label,
                         int level,
                         int parent,
                         int strength_pct,
                         uint32_t anchor_id_u32,
                         const std::wstring& path,
                         const std::wstring& lookup_name,
                         const std::wstring& interconnect,
                         const std::wstring& effect,
                         const std::wstring& placement,
                         const std::wstring& language_hint,
                         const std::wstring& export_scope,
                         const std::wstring& language_policy,
                         const std::wstring& input_pins,
                         const std::wstring& output_pins,
                         int sequence_i32,
                         int divergence_i32,
                         bool ancilla_anchor,
                         bool language_locked)->int {
        node_graph_items_w_.push_back(label);
        node_graph_item_level_i32_.push_back(level);
        node_graph_parent_i32_.push_back(parent);
        node_graph_strength_pct_i32_.push_back(strength_pct);
        node_graph_anchor_id_u32_.push_back(anchor_id_u32);
        node_graph_operator_path_w_.push_back(path);
        node_graph_lookup_name_w_.push_back(lookup_name);
        node_graph_interconnect_w_.push_back(interconnect);
        node_graph_effect_w_.push_back(effect);
        node_graph_placement_w_.push_back(placement);
        node_graph_language_hint_w_.push_back(language_hint);
        node_graph_export_scope_w_.push_back(export_scope);
        node_graph_language_policy_w_.push_back(language_policy);
        node_graph_input_pins_w_.push_back(input_pins);
        node_graph_output_pins_w_.push_back(output_pins);
        node_graph_doc_key_w_.push_back(lookup_name);
        node_graph_contract_ready_u8_.push_back((!label.empty() && !lookup_name.empty() && !path.empty() && !input_pins.empty() && !output_pins.empty()) ? 1u : 0u);
        std::wstring compat = L"n/a";
        if (parent >= 0 && (size_t)parent < node_graph_output_pins_w_.size()) compat = DescribeNodeCompatibility(node_graph_output_pins_w_[(size_t)parent], input_pins);
        node_graph_compatibility_w_.push_back(compat);
        node_graph_sequence_i32_.push_back(sequence_i32);
        node_graph_divergence_i32_.push_back(divergence_i32);
        node_graph_is_ancilla_u8_.push_back(ancilla_anchor ? 1u : 0u);
        node_graph_language_locked_u8_.push_back(language_locked ? 1u : 0u);
        return (int)node_graph_items_w_.size() - 1;
    };

    const int root = push_node(L"Carrier Anchor Root :: AI write manifold", 0, -1, 100, carrier_root_anchor,
        L"root / carrier manifold", L"carrier_root", L"scheduler, variables, functions, events, dispatchers, export",
        L"root ancilla fanout anchor for all editor-visible write lanes",
        L"root of graph; drag-off or right-click here to search all native nodes", L"n/a", L"graph root", L"language planning comes from downstream lanes", L"none", L"exec_out, route_out, lang_out", 0, 0, true, false);
    const int scheduler = push_node(L"Scheduler Carrier f0 :: bounded schedule anchors", 1, root, 88, carrier_root_anchor + 11u,
        L"schedulers -> tick gates -> dispatch", L"scheduler_carrier", L"delay_window, tick_fanout, flow_sequence, flow_branch",
        L"bounded schedule ancilla lane", L"right-click or drag-off from root/scheduler to place bounded schedule nodes", L"n/a", L"execution control", L"language inferred from downstream emit/export nodes", L"exec_in", L"exec_out", 1, 1, true, false);
    const int vars = push_node(L"Variable Carrier f1 :: state/variable anchors", 1, root, 82, carrier_root_anchor + 17u,
        L"variables -> state vectors -> writes", L"variable_carrier", L"get_anchor_scalar, set_anchor_scalar, compose_carrier_tuple",
        L"ancilla lane for state reads and writes", L"place under variable lane or tuple-producing nodes", L"n/a", L"state shaping", L"language inferred from downstream emit/export nodes", L"scalar_in", L"scalar_out, carrier_out", 1, 2, true, false);
    const int funcs = push_node(L"Function Carrier f2 :: operator anchors", 1, root, 76, carrier_root_anchor + 23u,
        L"functions -> operators -> transforms", L"function_carrier", L"apply_substrate_op, fourier_transform, flow_branch",
        L"ancilla lane for operator transforms", L"place under function lane or transform-capable tuple nodes", L"n/a", L"operator shaping", L"language inferred from downstream emit/export nodes", L"exec_in, carrier_in", L"exec_out, carrier_out", 1, 3, true, false);
    int events = -1;
    int dispatch = -1;
    int project = -1;
    if (node_graph_expanded_) {
        events = push_node(L"Event Carrier f3 :: trigger anchors", 1, root, 68, carrier_root_anchor + 29u,
            L"events -> triggers -> fanout", L"event_carrier", L"on_trigger, on_coherence_match, emit_control_packet",
            L"ancilla lane for trigger fanout", L"place under event lane or coherence-hit carriers", L"n/a", L"event branch", L"language inferred from downstream emit/export nodes", L"event_in", L"event_out", 1, 4, true, false);
        dispatch = push_node(L"Dispatcher Carrier f4 :: routing anchors", 1, root, 64, carrier_root_anchor + 31u,
            L"dispatchers -> routing -> packet issue", L"dispatcher_carrier", L"emit_control_packet, route_to_carrier, export_repo_patch",
            L"ancilla lane for routed packet fanout", L"place under dispatcher lane or packet-emitting nodes", L"engine-native packet language", L"packet dispatch", L"locked to engine packet/runtime integration", L"route_in", L"route_out", 1, 5, true, false);
        project = push_node(L"Project Work Substrate Carrier", 1, root, 84, carrier_root_anchor + 401u,
            L"project substrate -> linked repo spectrum summary -> AI code/write context", L"project_work_substrate", L"export_write_file, export_repo_patch, export_whole_repo, auto_language, set_file_language",
            L"ancilla lane for linked project work/export context", L"place export nodes here; whole-repo and per-file export nodes branch from this carrier", L"auto from node context unless locked by file type", L"project export lane", L"AI auto-suggest per file; locked integrations stay fixed", L"repo_in, file_in, lang_in", L"repo_out, file_out, lang_out", 1, 6, true, false);
        push_node(L"Chat Memory Cortex Partition", 1, root, 72, carrier_root_anchor + 451u,
            L"chat cortex partition -> conversational context only", L"chat_memory_cortex", L"event carriers, coherence review, talk/code/sim context",
            L"conversation-only ancilla memory partition", L"read-only derived carrier; does not place export/build nodes directly", L"n/a", L"context only", L"not an export lane", L"none", L"event_out", 1, 7, true, false);
        if (selected_anchor_id_u32 != 0u) {
            push_node(L"Selected Object Carrier :: " + selected_name_w, 2, funcs, 74, selected_anchor_id_u32,
                L"selected object -> object anchor projection", L"selected_object_carrier", L"apply_substrate_op, compose_carrier_tuple, route_to_carrier",
                L"selected object ancilla projection lane", L"spawn from selected object carriers when targeting one object's anchors", L"auto from linked object/file context", L"object-scoped", L"AI auto-suggest unless downstream lock applies", L"carrier_in", L"carrier_out", 2, 3, true, false);
            push_node(L"Voxel Coupling Carrier Anchor", 2, vars, 61, selected_anchor_id_u32 + 101u,
                L"voxel designer -> coupling anchors -> substrate occupancy", L"voxel_coupling_carrier", L"get_anchor_scalar, set_anchor_scalar, compose_carrier_tuple",
                L"voxel coupling ancilla fanout lane", L"spawn from variable lane when building voxel/material logic", L"n/a", L"material logic", L"language inferred from downstream emit/export nodes", L"scalar_in", L"carrier_out", 2, 2, true, false);
            push_node(L"Collision Envelope Carrier Anchor", 2, scheduler, 57, selected_anchor_id_u32 + 151u,
                L"schedulers -> collision envelope -> guard rails", L"collision_envelope_carrier", L"delay_window, tick_fanout, flow_branch",
                L"collision guard ancilla fanout lane", L"spawn from scheduler lane when timing collision or guard work", L"n/a", L"guard logic", L"language inferred from downstream emit/export nodes", L"exec_in", L"exec_out", 2, 1, true, false);
            push_node(L"Viewport Projection Carrier Anchor", 2, dispatch, live_mode_enabled_ ? 86 : 66, selected_anchor_id_u32 + 211u,
                L"dispatchers -> viewport resonance projection", L"viewport_projection_carrier", L"route_to_carrier, emit_control_packet, fourier_transform",
                L"viewport projection ancilla lane", L"spawn from dispatcher lane when routing derived viewport overlays", L"shader/runtime constrained", L"viewport/runtime bridge", L"locked to shader/runtime integration", L"route_in", L"route_out", 2, 5, true, true);
        }
        uint32_t shown = 0u;
        for (const auto& it : coh_highlight_set_w_) {
            if (shown >= 4u) break;
            const uint32_t hit_anchor = carrier_root_anchor + 300u + shown * 13u;
            push_node(L"Coherence Hit Carrier :: " + it, 2, events, 54 - (int)(shown * 4u), hit_anchor,
                L"events -> coherence index -> highlight path", L"coherence_hit_carrier", L"on_coherence_match, export_repo_patch, route_to_carrier",
                L"coherence-derived ancilla hint lane", L"spawn from current selected carrier when you need file/repo-aware context", L"inherits upstream export language rules", L"coherence branch", L"inherits upstream export/language rules", L"event_in, repo_in", L"repo_out", 2, 4, true, false);
            ++shown;
        }
        if (shown == 0u) push_node(L"Coherence Hit Carrier :: none", 2, events, 28, carrier_root_anchor + 399u,
            L"events -> coherence index -> no active hits", L"coherence_hit_carrier", L"on_coherence_match, route_to_carrier",
            L"empty coherence ancilla hint lane", L"spawn after coherence review when no active targets exist", L"inherits upstream export language rules", L"coherence branch", L"inherits upstream export/language rules", L"event_in", L"event_out", 2, 4, true, false);

        if (scene_) {
            SubstrateManager::EwProjectLinkEntry proj{};
            if (scene_->SnapshotAiChatProject(ai_tab_index_u32_, proj) && !proj.project_root_utf8.empty()) {
                push_node(L"Project Root :: " + utf8_to_wide(proj.project_root_utf8), 2, project, 78, carrier_root_anchor + 409u,
                    L"linked project root -> export/apply targeting", L"project_root", L"export_write_file, export_repo_patch, export_whole_repo",
                    L"project-root ancilla reference lane", L"spawn export nodes under the linked project root carrier", L"mixed per-file language", L"project-root export", L"AI auto-suggest per file; overrides only when unlocked", L"repo_in, lang_in", L"repo_out, file_out", 2, 6, true, false);
                std::vector<std::string> spectrum_lines;
                (void)scene_->SnapshotAiProjectSpectrumLines(ai_tab_index_u32_, 4u, spectrum_lines);
                for (size_t i = 0; i < spectrum_lines.size(); ++i) {
                    push_node(L"Dir Spectrum f :: " + utf8_to_wide(spectrum_lines[i]), 2, project, 66 - (int)(i * 6u), carrier_root_anchor + 420u + (uint32_t)i,
                        L"linked project spectrum summary -> Fourier directory content summary", L"project_dir_digest", L"fourier_transform, export_repo_patch, export_write_file, export_whole_repo",
                        L"Fourier-style directory spectrum summary ancilla lane", L"spawn after project link when reviewing repo-wide export targets", L"mixed per-file language", L"project spectrum summary", L"AI auto-suggest per file; overrides only when unlocked", L"repo_in", L"repo_out", 2, 6, true, false);
                }
            }
        }
    } else {
        push_node(L"Viewport Projection Carrier :: derived anchor view", 1, root, live_mode_enabled_ ? 84 : 63, carrier_root_anchor + 211u,
            L"dispatchers -> viewport resonance projection", L"viewport_projection_carrier", L"route_to_carrier, emit_control_packet, fourier_transform",
            L"viewport projection ancilla lane", L"available in collapsed view for viewport-oriented spawn/search", L"shader/runtime constrained", L"viewport/runtime bridge", L"locked to shader/runtime integration", L"route_in", L"route_out", 1, 5, true, true);
    }

    std::unordered_map<uint32_t, int> parent_anchor_to_index;
    for (size_t i = 0; i < node_graph_anchor_id_u32_.size(); ++i) parent_anchor_to_index.emplace(node_graph_anchor_id_u32_[i], (int)i);
    std::unordered_map<uint32_t, int> spawned_anchor_to_index;
    for (const auto& sp : node_spawned_items_) {
        int parent_index = root;
        auto it = parent_anchor_to_index.find(sp.parent_anchor_id_u32);
        if (it != parent_anchor_to_index.end()) parent_index = it->second;
        else if (sp.parent_anchor_id_u32 == 0u) parent_index = -1;
        const int parent_level = (parent_index >= 0 && (size_t)parent_index < node_graph_item_level_i32_.size()) ? node_graph_item_level_i32_[(size_t)parent_index] : 0;
        const int child_level = std::min(4, parent_level + 1);
        const int idx = push_node(sp.label_w, child_level, parent_index, sp.strength_pct_i32, sp.anchor_id_u32, sp.operator_path_w,
            sp.lookup_name_w, sp.interconnect_w, sp.effect_w, sp.placement_w, sp.language_hint_w, sp.export_scope_w, sp.language_policy_w, sp.input_pins_w, sp.output_pins_w, sp.sequence_i32, sp.divergence_i32, sp.ancilla_anchor, sp.language_locked);
        if ((size_t)idx < node_graph_doc_key_w_.size()) node_graph_doc_key_w_[(size_t)idx] = sp.doc_key_w;
        if ((size_t)idx < node_graph_contract_ready_u8_.size()) node_graph_contract_ready_u8_[(size_t)idx] = sp.contract_ready ? 1u : 0u;
        parent_anchor_to_index[sp.anchor_id_u32] = idx;
        spawned_anchor_to_index.emplace(sp.anchor_id_u32, idx);
    }
    node_graph_edge_in_i32_.assign(node_graph_items_w_.size(), 0);
    node_graph_edge_out_i32_.assign(node_graph_items_w_.size(), 0);
    node_graph_edge_label_w_.assign(node_graph_items_w_.size(), L"");
    for (auto& edge : node_edge_records_) {
        edge.parent_i32 = -1;
        edge.child_i32 = -1;
        auto pit = parent_anchor_to_index.find(edge.parent_anchor_id_u32);
        auto cit = parent_anchor_to_index.find(edge.child_anchor_id_u32);
        if (pit != parent_anchor_to_index.end()) edge.parent_i32 = pit->second;
        if (cit != parent_anchor_to_index.end()) edge.child_i32 = cit->second;
        if (edge.child_i32 < 0) continue;
        if (edge.parent_i32 >= 0 && (size_t)edge.parent_i32 < node_graph_edge_out_i32_.size()) {
            node_graph_edge_out_i32_[(size_t)edge.parent_i32] += 1;
            if (node_graph_edge_label_w_[(size_t)edge.parent_i32].empty()) node_graph_edge_label_w_[(size_t)edge.parent_i32] = edge.edge_label_w;
        }
        if ((size_t)edge.child_i32 < node_graph_edge_in_i32_.size()) {
            node_graph_edge_in_i32_[(size_t)edge.child_i32] += 1;
            if (node_graph_edge_label_w_[(size_t)edge.child_i32].empty()) node_graph_edge_label_w_[(size_t)edge.child_i32] = edge.edge_label_w;
        }
    }
    if (node_graph_selected_i32_ >= (int)node_graph_items_w_.size()) node_graph_selected_i32_ = (int)node_graph_items_w_.size() - 1;
    if (node_graph_selected_i32_ < 0) node_graph_selected_i32_ = 0;
    if (hwnd_node_graph_) {
        SendMessageW(hwnd_node_graph_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(hwnd_node_graph_, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < node_graph_items_w_.size(); ++i) {
            const int row = (int)SendMessageW(hwnd_node_graph_, LB_ADDSTRING, 0, (LPARAM)node_graph_items_w_[i].c_str());
            if (row >= 0) SendMessageW(hwnd_node_graph_, LB_SETITEMDATA, (WPARAM)row, (LPARAM)i);
        }
        if (!node_graph_items_w_.empty()) SendMessageW(hwnd_node_graph_, LB_SETCURSEL, (WPARAM)node_graph_selected_i32_, 0);
        SendMessageW(hwnd_node_graph_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hwnd_node_graph_, nullptr, TRUE);
    }
    if (hwnd_node_expand_) SetWindowTextW(hwnd_node_expand_, node_graph_expanded_ ? L"Collapse" : L"Expand");
    if (hwnd_node_play_) SetWindowTextW(hwnd_node_play_, node_play_excitation_ ? L"Excite On" : L"Excite");
    if (hwnd_node_mode_local_) SetWindowTextW(hwnd_node_mode_local_, node_rename_propagate_ ? L"Local" : L"[Local]");
    if (hwnd_node_mode_propagate_) SetWindowTextW(hwnd_node_mode_propagate_, node_rename_propagate_ ? L"[Propagate]" : L"Propagate");

    const int node_sel = node_graph_selected_i32_;
    std::wstring query_w = GetNodeSearchQuery();
    RebuildNodePaletteEntries();
    ClampNodePinSelections();
    int result_sel = hwnd_node_results_ ? (int)SendMessageW(hwnd_node_results_, LB_GETCURSEL, 0, 0) : -1;
    if (result_sel < 0 || (size_t)result_sel >= node_palette_entries_.size()) result_sel = node_palette_entries_.empty() ? -1 : 0;
    if (hwnd_node_results_) {
        SendMessageW(hwnd_node_results_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(hwnd_node_results_, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < node_palette_entries_.size(); ++i) {
            const auto& pe = node_palette_entries_[i];
            std::wstring item = pe.label_w + L"  [" + pe.lookup_name_w + L"]";
            if (!pe.input_pins_w.empty() || !pe.output_pins_w.empty()) item += L"  (" + pe.input_pins_w + L" -> " + pe.output_pins_w + L")";
            if (!pe.export_scope_w.empty() && _wcsicmp(pe.export_scope_w.c_str(), L"none") != 0) item += L"  <" + pe.export_scope_w + L">";
            item += (pe.compatibility_w.find(L"Yes") == 0) ? L"  [connectable]" : L"  [not connectable]";
            const int row = (int)SendMessageW(hwnd_node_results_, LB_ADDSTRING, 0, (LPARAM)item.c_str());
            if (row >= 0) SendMessageW(hwnd_node_results_, LB_SETITEMDATA, (WPARAM)row, (LPARAM)i);
        }
        if (result_sel >= 0 && result_sel < (int)node_palette_entries_.size()) SendMessageW(hwnd_node_results_, LB_SETCURSEL, (WPARAM)result_sel, 0);
        ClampNodePinSelections();
        SendMessageW(hwnd_node_results_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hwnd_node_results_, nullptr, TRUE);
    }
    if (hwnd_node_search_spawn_) {
        if (result_sel >= 0 && (size_t)result_sel < node_palette_entries_.size()) SetWindowTextW(hwnd_node_search_spawn_, L"Spawn Selected");
        else if (node_palette_entries_.size() == 1u) SetWindowTextW(hwnd_node_search_spawn_, L"Spawn 1 Match");
        else if (!node_palette_entries_.empty()) SetWindowTextW(hwnd_node_search_spawn_, L"Search/Spawn");
        else SetWindowTextW(hwnd_node_search_spawn_, L"No Matches");
    }
    bool export_action_available = false;
    if (result_sel >= 0 && (size_t)result_sel < node_palette_entries_.size()) {
        const auto& pe = node_palette_entries_[(size_t)result_sel];
        export_action_available = !pe.export_scope_w.empty() && _wcsicmp(pe.export_scope_w.c_str(), L"none") != 0;
    } else if (node_sel >= 0 && node_sel < (int)node_graph_export_scope_w_.size()) {
        export_action_available = !node_graph_export_scope_w_[(size_t)node_sel].empty() && _wcsicmp(node_graph_export_scope_w_[(size_t)node_sel].c_str(), L"none") != 0;
    }
    if (hwnd_node_export_preview_) EnableWindow(hwnd_node_export_preview_, export_action_available ? TRUE : FALSE);
    bool connect_action_available = false;
    if (result_sel >= 0 && (size_t)result_sel < node_palette_entries_.size()) connect_action_available = node_palette_entries_[(size_t)result_sel].compatibility_w.find(L"Yes") == 0;
    if (hwnd_node_connect_selected_) EnableWindow(hwnd_node_connect_selected_, connect_action_available ? TRUE : FALSE);
    bool disconnect_action_available = false;
    if (node_sel >= 0 && node_sel < (int)node_graph_parent_i32_.size()) disconnect_action_available = node_graph_parent_i32_[(size_t)node_sel] >= 0;
    if (hwnd_node_disconnect_selected_) EnableWindow(hwnd_node_disconnect_selected_, disconnect_action_available ? TRUE : FALSE);

    std::wstring out = L"Nodes represent ancilla fanout anchors: each visible graph node is a Fourier carrier anchor grouping the resonant anchors that share one AI write/operator path.\r\n";
    out += L"Implementation contract: document first, then search/UI, then backend anchor semantics. The graph remains derived-only and reads from separate chat-memory and project-work substrates.\r\n";
    out += L"Right-click a carrier or use Search/Spawn to open the native node palette. Search matches label, lookup name, operator path, placement, interconnect text, logic-stream effect, pin grammar, language hints, export rules, and per-node doc keys. Connect Selected records a bounded derived edge; Disconnect Selected clears that edge without deleting the node.\r\n";
    out += L"Edge display is derived-only: node rows render minimal incoming/outgoing pin stubs and the first edge label from bounded anchor-id edge records. The graph remains advisory and is not a second execution engine. Alt+[ / Alt+] cycles the selected source output pin; Ctrl+[ / Ctrl+] cycles the selected target input pin for the active search result.\r\n\r\n";
    wchar_t line[512]{};
    swprintf(line, 512, L"Active dock tab: %u\r\n", (unsigned)rdock_tab_index_u32_);
    out += line;
    swprintf(line, 512, L"Selected object count: %u\r\n", (unsigned)editor_selection_count_u32_);
    out += line;
    swprintf(line, 512, L"Viewport mode: %ls\r\n", live_mode_enabled_ ? L"live substrate lattice" : L"sandbox object view");
    out += line;
    out += live_mode_enabled_ ? L"Live-mode status: LIVE across viewport, toolbar/menu state, and editor summaries. Viewport source is the substrate lattice while editor authoring stays derived-only.\r\n"
                              : L"Live-mode status: sandbox/editor context. Viewport source is the standard projection and runtime/live hooks remain disengaged.\r\n";
    out += L"Node search query: ";
    out += query_w.empty() ? L"<none>\r\n" : query_w + L"\r\n";
    swprintf(line, 512, L"Search matches: %u\r\n", (unsigned)node_palette_entries_.size());
    out += line;
    if (!node_palette_entries_.empty()) {
        out += L"Top matches:\r\n";
        const size_t shown = std::min<size_t>(6u, node_palette_entries_.size());
        for (size_t i = 0; i < shown; ++i) {
            const auto& pe = node_palette_entries_[i];
            out += L"  - " + pe.label_w + L" [" + pe.lookup_name_w + L"]";
            if (!pe.language_hint_w.empty()) out += L"  <" + pe.language_hint_w + L">";
            if (pe.language_locked) out += L"  [locked]";
            out += L"\r\n    place: " + pe.placement_w + L"\r\n    effect: " + pe.effect_w + L"\r\n";
        }
    }
    if (result_sel >= 0 && (size_t)result_sel < node_palette_entries_.size()) {
        const auto& pe = node_palette_entries_[(size_t)result_sel];
        out += L"Selected search result: " + pe.label_w + L" [" + pe.lookup_name_w + L"]\r\n";
        out += L"Placement preview: " + pe.placement_w + L"\r\n";
        out += L"Interconnect preview: " + pe.interconnect_w + L"\r\n";
        out += L"Logic-stream preview: " + pe.effect_w + L"\r\n";
        out += L"Language hint/rule: " + pe.language_hint_w + L"  /  " + pe.language_policy_w;
        if (pe.language_locked) out += L"  [locked]";
        out += L"\r\n";
        out += L"Export scope: " + (pe.export_scope_w.empty() ? L"none" : pe.export_scope_w) + L"\r\n";
        out += L"Doc key: " + pe.doc_key_w + L"\r\n";
        out += L"Contract lockstep: ";
        out += pe.contract_ready ? L"docs/search/backend READY\r\n" : L"missing one or more doc/search/backend fields\r\n";
    }
    if (node_sel >= 0 && node_sel < (int)node_graph_items_w_.size()) {
        out += L"Selected carrier node: " + node_graph_items_w_[(size_t)node_sel] + L"\r\n";
        if (node_sel < (int)node_graph_lookup_name_w_.size()) out += L"Lookup name: " + node_graph_lookup_name_w_[(size_t)node_sel] + L"\r\n";
        if (node_sel < (int)node_graph_operator_path_w_.size()) out += L"Operator path: " + node_graph_operator_path_w_[(size_t)node_sel] + L"\r\n";
        if (node_sel < (int)node_graph_placement_w_.size()) out += L"Placement: " + node_graph_placement_w_[(size_t)node_sel] + L"\r\n";
        if (node_sel < (int)node_graph_interconnect_w_.size()) out += L"Interconnects: " + node_graph_interconnect_w_[(size_t)node_sel] + L"\r\n";
        if (node_sel < (int)node_graph_effect_w_.size()) out += L"Logic-stream effect: " + node_graph_effect_w_[(size_t)node_sel] + L"\r\n";
        if (node_sel < (int)node_graph_language_hint_w_.size()) out += L"Language: " + node_graph_language_hint_w_[(size_t)node_sel] + (((node_sel < (int)node_graph_language_locked_u8_.size()) && node_graph_language_locked_u8_[(size_t)node_sel]) ? L"  [locked]" : L"") + L"\r\n";
        swprintf(line, 512, L"Carrier anchor id=%u  coupling=%d%%  ancilla=%ls\r\n",
                 (unsigned)((node_sel < (int)node_graph_anchor_id_u32_.size()) ? node_graph_anchor_id_u32_[(size_t)node_sel] : 0u),
                 (node_sel < (int)node_graph_strength_pct_i32_.size()) ? node_graph_strength_pct_i32_[(size_t)node_sel] : 0,
                 ((node_sel < (int)node_graph_is_ancilla_u8_.size()) && node_graph_is_ancilla_u8_[(size_t)node_sel]) ? L"yes" : L"no");
        out += line;
        swprintf(line, 512, L"Sequence=%d  Divergence=%d\r\n",
                 (node_sel < (int)node_graph_sequence_i32_.size()) ? node_graph_sequence_i32_[(size_t)node_sel] : 0,
                 (node_sel < (int)node_graph_divergence_i32_.size()) ? node_graph_divergence_i32_[(size_t)node_sel] : 0);
        out += line;
        swprintf(line, 512, L"Derived edges: in=%d  out=%d\r\n",
                 (node_sel < (int)node_graph_edge_in_i32_.size()) ? node_graph_edge_in_i32_[(size_t)node_sel] : 0,
                 (node_sel < (int)node_graph_edge_out_i32_.size()) ? node_graph_edge_out_i32_[(size_t)node_sel] : 0);
        out += line;
        if (node_sel < (int)node_graph_edge_label_w_.size() && !node_graph_edge_label_w_[(size_t)node_sel].empty()) {
            out += L"Derived edge label: " + node_graph_edge_label_w_[(size_t)node_sel] + L"\r\n";
        }
        if (node_sel < (int)node_graph_doc_key_w_.size()) out += L"Doc key: " + node_graph_doc_key_w_[(size_t)node_sel] + L"\r\n";
        if (node_sel < (int)node_graph_contract_ready_u8_.size()) out += (node_graph_contract_ready_u8_[(size_t)node_sel] ? L"Contract lockstep: docs/search/backend READY\r\n" : L"Contract lockstep: metadata incomplete\r\n");
        const auto src_pins = ew_split_trim_pin_list((node_sel < (int)node_graph_output_pins_w_.size()) ? node_graph_output_pins_w_[(size_t)node_sel] : L"");
        if (!src_pins.empty()) {
            out += L"Source output pins: ";
            for (size_t i = 0; i < src_pins.size(); ++i) {
                if (i) out += L", ";
                if ((int)i == node_source_pin_selected_i32_) out += L"[" + src_pins[i] + L"]"; else out += src_pins[i];
                if ((int)i == node_source_pin_hover_i32_) out += L"{hover}";
            }
            out += L"\r\n";
        }
    }
    if (node_sel >= 0 && node_sel < (int)node_graph_parent_i32_.size()) {
        const int parent_idx = node_graph_parent_i32_[(size_t)node_sel];
        if (parent_idx >= 0 && (size_t)parent_idx < node_graph_items_w_.size()) {
            out += L"Placement feedback: attached under \"" + node_graph_items_w_[(size_t)parent_idx] + L"\" as a child carrier.\r\n";
        }
    }
    if (result_sel >= 0 && (size_t)result_sel < node_palette_entries_.size() && node_sel >= 0 && (size_t)node_sel < node_graph_items_w_.size()) {
        const auto& pe = node_palette_entries_[(size_t)result_sel];
        const int next_seq = ((node_sel >= 0 && (size_t)node_sel < node_graph_sequence_i32_.size()) ? node_graph_sequence_i32_[(size_t)node_sel] : 0) + 1;
        const int next_div = (node_sel >= 0 && (size_t)node_sel < node_graph_divergence_i32_.size()) ? node_graph_divergence_i32_[(size_t)node_sel] : 0;
        out += L"Spawn feedback: placing \"" + pe.label_w + L"\" under \"" + node_graph_items_w_[(size_t)node_sel] + L"\" -> seq " + std::to_wstring((long long)next_seq) + L", div " + std::to_wstring((long long)next_div) + L".\r\n";
        out += L"Connectable from current source: " + pe.compatibility_w + L"\r\n";
        const std::wstring source_name = node_graph_items_w_[(size_t)node_sel];
        std::wstring chosen_src, chosen_dst;
        (void)ew_find_pin_pair((node_sel >= 0 && (size_t)node_sel < node_graph_output_pins_w_.size()) ? node_graph_output_pins_w_[(size_t)node_sel] : L"", pe.input_pins_w, node_source_pin_selected_i32_, node_target_pin_selected_i32_, &chosen_src, &chosen_dst, nullptr, nullptr);
        const std::wstring edge_label = (!chosen_src.empty() && !chosen_dst.empty()) ? (chosen_src + L" -> " + chosen_dst) : L"derived parent edge";
        out += L"Target input pins: " + pe.input_pins_w + L"\r\n";
        if (!chosen_dst.empty()) out += L"Selected pin pair: [" + chosen_src + L"] -> [" + chosen_dst + L"]\r\n";
        out += L"Pre-commit edge preview: " + source_name + L" --[" + edge_label + L"]-> " + pe.label_w + L"\r\n";
        if (pe.compatibility_w.find(L"Yes") == 0) {
            out += L"Derived effect before commit: " + pe.effect_w + L"\r\n";
            if (!pe.export_scope_w.empty() && _wcsicmp(pe.export_scope_w.c_str(), L"none") != 0) {
                out += L"Export/language implication: " + pe.export_scope_w + L"  /  " + pe.language_policy_w;
                if (pe.language_locked) out += L"  [locked]";
                out += L"\r\n";
            }
            out += L"Disconnect affordance after commit: use Disconnect Selected to clear the derived edge while keeping the node.\r\n";
        } else {
            out += L"Invalid connection explanation: " + pe.compatibility_w + L"\r\n";
        }
    }
    out += L"Rename mode: ";
    out += node_rename_propagate_ ? L"propagated via coherence/index review\r\n" : L"local only\r\n";
    out += L"Excitation overlay: ";
    out += node_play_excitation_ ? L"derived play feedback ON\r\n" : L"off\r\n";
    const int rc_band = hwnd_node_rc_band_ ? (int)SendMessageW(hwnd_node_rc_band_, TBM_GETPOS, 0, 0) : spectrum_band_i32_;
    const int rc_drive = hwnd_node_rc_drive_ ? (int)SendMessageW(hwnd_node_rc_drive_, TBM_GETPOS, 0, 0) : 48;
    swprintf(line, 512, L"RC packet hook: band=%d  drive=%d\r\n", rc_band, rc_drive);
    out += line;
    out += L"Export lane: supports file artifact export, repo patch export, whole-repo export, AI language auto-suggestion, and per-file language override when the file type is not locked by platform/integration constraints.\r\n";
    if (ai_tab_index_u32_ < AI_CHAT_MAX && !ai_chat_project_summary_w_[ai_tab_index_u32_].empty()) out += ai_chat_project_summary_w_[ai_tab_index_u32_] + L"\r\n";
    if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
        const auto& o = scene_->objects[(size_t)scene_->selected];
        out += L"Selected object anchor carrier source: " + utf8_to_wide(o.name_utf8) + L"\r\n";
    }
    if (!node_export_preview_w_.empty()) {
        out += L"\r\nExport behavior preview:\r\n";
        out += node_export_preview_w_;
        out += L"\r\n";
    }
    out += L"\r\nUse Open Coherence to review rename propagation and active-hit details in the canonical coherence tools. Node-graph export nodes use export/apply language rather than hydration language.";
    SetWindowTextW(hwnd_node_summary_, out.c_str());
    RefreshViewportResonanceOverlay();
}

void App::ShowNodeSpawnMenu(POINT screen_pt) {
    if (!hwnd_node_graph_) return;
    const int sel = hwnd_node_graph_ ? (int)SendMessageW(hwnd_node_graph_, LB_GETCURSEL, 0, 0) : -1;
    if (sel >= 0) node_graph_selected_i32_ = sel;
    const int node_sel = node_graph_selected_i32_;
    if (node_sel < 0 || (size_t)node_sel >= node_graph_items_w_.size()) return;

    RebuildNodePaletteEntries();
    if (node_palette_entries_.empty()) {
        if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Node Graph: no spawnable matches for the current search query/source carrier.");
        RefreshNodePanel();
        return;
    }
    if (node_palette_entries_.size() == 1u) {
        SpawnNodePaletteEntry(0u);
        return;
    }

    HMENU hm = CreatePopupMenu();
    for (size_t i = 0; i < node_palette_entries_.size(); ++i) {
        const auto& e = node_palette_entries_[i];
        std::wstring item = e.label_w + L"  [" + e.lookup_name_w + L"]";
        if (e.language_locked) item += L"  <locked>";
        InsertMenuW(hm, (UINT)i, MF_BYPOSITION | MF_STRING, 2760 + (UINT)i, item.c_str());
    }
    TrackPopupMenu(hm, TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd_, nullptr);
    DestroyMenu(hm);
}


static int ew_read_trackbar_pos_safe(HWND h, int fallback_i32) {
    if (!h) return fallback_i32;
    return (int)SendMessageW(h, TBM_GETPOS, 0, 0);
}

static std::wstring ew_combo_selected_text_safe(HWND h, const wchar_t* fallback_w) {
    if (!h) return fallback_w ? std::wstring(fallback_w) : std::wstring();
    const int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (sel < 0) return fallback_w ? std::wstring(fallback_w) : std::wstring();
    wchar_t buf[256]{};
    SendMessageW(h, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)buf);
    return std::wstring(buf);
}

void App::RefreshAssetDesignerPanel() {
    const std::wstring mode_w = ew_combo_selected_text_safe(hwnd_asset_tool_mode_, L"Asset Builder");
    const bool mode_planet = _wcsicmp(mode_w.c_str(), L"Planet Builder") == 0;
    const bool mode_character = _wcsicmp(mode_w.c_str(), L"Character Tools") == 0;
    const bool mode_asset = !mode_planet && !mode_character;

    auto set_visible = [&](HWND h, bool show)->void {
        if (!h) return;
        ShowWindow(h, show ? SW_SHOW : SW_HIDE);
        EnableWindow(h, show ? TRUE : FALSE);
    };
    set_visible(hwnd_asset_planet_atmo_, mode_planet);
    set_visible(hwnd_asset_planet_iono_, mode_planet);
    set_visible(hwnd_asset_planet_magneto_, mode_planet);
    set_visible(hwnd_asset_planet_apply_, mode_planet);
    set_visible(hwnd_asset_planet_sculpt_, mode_planet);
    set_visible(hwnd_asset_planet_paint_, mode_planet);
    set_visible(hwnd_asset_character_archetype_, mode_character);
    set_visible(hwnd_asset_character_height_, mode_character);
    set_visible(hwnd_asset_character_rigidity_, mode_character);
    set_visible(hwnd_asset_character_gait_, mode_character);
    set_visible(hwnd_asset_character_bind_, mode_character);
    set_visible(hwnd_asset_character_pose_, mode_character);

    bool has_selection = false;
    std::wstring selected_w = L"Selected Object: none";
    std::wstring selection_name_w;
    uint32_t selection_anchor_id_u32 = 0u;
    bool selection_pbr = false;
    float selection_radius_m = 0.0f;
    float selection_emissive = 0.0f;
    if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
        const auto& o = scene_->objects[(size_t)scene_->selected];
        has_selection = true;
        selection_name_w = utf8_to_wide(o.name_utf8);
        selection_anchor_id_u32 = o.anchor_id_u32;
        selection_pbr = (o.pbr_scan_u8 != 0u);
        selection_radius_m = o.radius_m_f32;
        selection_emissive = o.emissive_f32;
        selected_w = L"Selected Object: " + selection_name_w + L"  anchor=" + std::to_wstring((unsigned long long)selection_anchor_id_u32);
        if (selection_pbr) selected_w += L"  [PBR/scan]";
    }
    if (hwnd_asset_selected_) SetWindowTextW(hwnd_asset_selected_, selected_w.c_str());

    if (content_selected_rel_utf8_.empty()) (void)SelectContentForSelectedObject();
    const bool has_content_link = !content_selected_rel_utf8_.empty();
    const bool review_current = has_content_link && (asset_last_review_rel_utf8_ == content_selected_rel_utf8_) && (asset_last_review_revision_u64_ == coh_highlight_seen_revision_u64_);

    std::wstring gate_w;
    if (!has_selection) {
        gate_w = L"Training Gate: no scene selection.";
    } else if (!has_content_link) {
        gate_w = L"Training Gate: object selected but no linked content/repo target was resolved.";
    } else if (!review_current) {
        gate_w = L"Training Gate: coherence/reference review required before high-impact apply.";
    } else {
        gate_w = L"Training Gate: ready — selection, content link, and coherence review are aligned.";
    }
    if (hwnd_asset_gate_) SetWindowTextW(hwnd_asset_gate_, gate_w.c_str());

    std::wstring status_w = L"Builder: ";
    status_w += mode_w;
    status_w += L"  |  ";
    status_w += has_content_link ? L"content linked" : L"content pending";
    status_w += L"  |  ";
    status_w += review_current ? L"reference review current" : L"reference review needed";
    if (hwnd_asset_builder_status_) SetWindowTextW(hwnd_asset_builder_status_, status_w.c_str());

    std::wstring summary;
    summary += L"Canonical authoring lane: " + mode_w + L"
";
    summary += L"Shared review/apply path: content browser -> coherence/reference review -> apply hook -> viewport refresh
";
    summary += L"Project/work substrate: ";
    if (scene_) {
        const auto& root = scene_->runtime().project_settings.assets.project_asset_substrate_root_utf8;
        summary += root.empty() ? L"(unset)" : utf8_to_wide(root);
    } else {
        summary += L"(no scene)";
    }
    summary += L"
";
    summary += L"Content browser linkage: ";
    summary += has_content_link ? utf8_to_wide(content_selected_rel_utf8_) : L"(none)";
    summary += L"
";
    summary += L"Coherence/reference review: ";
    if (review_current) {
        summary += L"current for selected content target";
    } else if (has_content_link) {
        summary += L"stale or not yet run for the current content target";
    } else {
        summary += L"waiting for content target selection";
    }
    summary += L"
";

    if (has_selection) {
        summary += L"Selection anchor: " + std::to_wstring((unsigned long long)selection_anchor_id_u32);
        summary += L"  radius=" + std::to_wstring((double)selection_radius_m);
        summary += L"  emissive=" + std::to_wstring((double)selection_emissive);
        summary += L"
";
    }

    if (mode_asset) {
        summary += L"Asset Builder schema: source object, linked content path, coherence safety review, viewport resonance sync.
";
        if (has_selection) {
            summary += L"Current object summary: " + selection_name_w + L" remains on the canonical asset lane; destructive rename/reference-affecting work must go through Review Refs before apply.
";
        } else {
            summary += L"Current object summary: select or import an object to bind the asset lane.
";
        }
    } else if (mode_planet) {
        const int atmo = ew_read_trackbar_pos_safe(hwnd_asset_planet_atmo_, 32);
        const int iono = ew_read_trackbar_pos_safe(hwnd_asset_planet_iono_, 28);
        const int magneto = ew_read_trackbar_pos_safe(hwnd_asset_planet_magneto_, 36);
        summary += L"Planet Builder schema: atmosphere, ionosphere, magnetosphere, sculpt hook, paint hook.
";
        summary += L"Atmosphere=" + std::to_wstring((long long)atmo);
        summary += L"  Ionosphere=" + std::to_wstring((long long)iono);
        summary += L"  Magnetosphere=" + std::to_wstring((long long)magneto) + L"
";
        if (has_selection && selection_anchor_id_u32 < scene_->sm.anchors.size()) {
            const Anchor& A = scene_->sm.anchors[selection_anchor_id_u32];
            summary += L"Selected anchor resonance: f=" + std::to_wstring((long long)A.last_f_code);
            summary += L"  a=" + std::to_wstring((long long)A.last_a_code);
            summary += L"  v=" + std::to_wstring((long long)A.last_v_code);
            summary += L"  i=" + std::to_wstring((long long)A.last_i_code) + L"
";
        }
        summary += L"Apply ergonomics: Planet Apply is gated through coherence/reference review before mutating the selected proxy.
";
    } else {
        const std::wstring archetype_w = ew_combo_selected_text_safe(hwnd_asset_character_archetype_, L"Biped Explorer");
        const int h_cm = ew_read_trackbar_pos_safe(hwnd_asset_character_height_, 172);
        const int rigidity = ew_read_trackbar_pos_safe(hwnd_asset_character_rigidity_, 54);
        const int gait = ew_read_trackbar_pos_safe(hwnd_asset_character_gait_, 48);
        summary += L"Character Tools schema: archetype, height, rigidity, gait, bind rig, pose hook.
";
        summary += L"Archetype=" + archetype_w;
        summary += L"  Height(cm)=" + std::to_wstring((long long)h_cm);
        summary += L"  Rigidity=" + std::to_wstring((long long)rigidity);
        summary += L"  Gait=" + std::to_wstring((long long)gait) + L"
";
        summary += L"Bind ergonomics: Character Bind is gated through coherence/reference review before altering the selected proxy bounds.
";
    }

    summary += L"Viewport synchronization: ";
    summary += resonance_view_ ? L"resonance mode ON" : L"standard mode";
    summary += L"  band=" + std::to_wstring((long long)spectrum_band_i32_) + L"
";
    summary += L"Editor/runtime split preserved: authoring panels remain editor-only; runtime stays clean.
";
    if (hwnd_asset_tool_summary_) SetWindowTextW(hwnd_asset_tool_summary_, summary.c_str());
}


void App::SyncVoxelAtomNodeList() {
    if (!hwnd_voxel_atom_nodes_) return;
    const int keep_sel = voxel_atom_node_selected_i32_;
    SendMessageW(hwnd_voxel_atom_nodes_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hwnd_voxel_atom_nodes_, LB_RESETCONTENT, 0, 0);

    wchar_t preset[256]{};
    if (hwnd_voxel_preset_) {
        const int sel = (int)SendMessageW(hwnd_voxel_preset_, CB_GETCURSEL, 0, 0);
        if (sel >= 0) SendMessageW(hwnd_voxel_preset_, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)preset);
    }
    std::wstring material = preset[0] ? preset : L"Material";
    std::wstring base = material;
    const size_t sp = base.find(L' ');
    if (sp != std::wstring::npos) base = base.substr(0, sp);

    std::vector<std::wstring> nodes;
    nodes.push_back(L"1. Nucleus :: " + base + L" core lattice");
    nodes.push_back(L"2. Shell A :: bond density / anchor intake");
    nodes.push_back(L"3. Shell B :: roughness / leakage envelope");
    nodes.push_back(L"4. Resonance Bus :: spectral carrier fanout");
    nodes.push_back(L"5. Voxel Lattice :: substrate occupancy field");
    if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
        const auto& o = scene_->objects[(size_t)scene_->selected];
        nodes.push_back(L"6. Object Anchor :: id=" + std::to_wstring((unsigned long long)o.anchor_id_u32) + L"  name=" + utf8_to_wide(o.name_utf8));
    }
    for (const auto& n : nodes) SendMessageW(hwnd_voxel_atom_nodes_, LB_ADDSTRING, 0, (LPARAM)n.c_str());
    const int count = (int)SendMessageW(hwnd_voxel_atom_nodes_, LB_GETCOUNT, 0, 0);
    int sel = keep_sel;
    if (sel < 0 || sel >= count) sel = 0;
    voxel_atom_node_selected_i32_ = sel;
    if (count > 0) SendMessageW(hwnd_voxel_atom_nodes_, LB_SETCURSEL, (WPARAM)sel, 0);
    SendMessageW(hwnd_voxel_atom_nodes_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hwnd_voxel_atom_nodes_, nullptr, TRUE);
}

void App::RefreshVoxelDesignerPanel() {
    if (!hwnd_rdock_voxel_ || !hwnd_voxel_summary_) return;
    auto read_pos = [&](int id)->int {
        HWND h = GetDlgItem(hwnd_rdock_voxel_, id);
        if (!h) return 0;
        return (int)SendMessageW(h, TBM_GETPOS, 0, 0);
    };
    int dens = read_pos(2601);
    int hard = read_pos(2602);
    int rough = read_pos(2603);
    wchar_t preset[256]{};
    if (hwnd_voxel_preset_) {
        int sel = (int)SendMessageW(hwnd_voxel_preset_, CB_GETCURSEL, 0, 0);
        if (sel >= 0) SendMessageW(hwnd_voxel_preset_, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)preset);
    }
    SyncVoxelAtomNodeList();
    if (hwnd_voxel_atom_nodes_) {
        const int sel = (int)SendMessageW(hwnd_voxel_atom_nodes_, LB_GETCURSEL, 0, 0);
        if (sel >= 0) voxel_atom_node_selected_i32_ = sel;
    }

    std::wstring summary = L"Atom Simulation Preset: ";
    summary += (preset[0] ? preset : L"-");
    summary += L"\r\nDensity=" + std::to_wstring((long long)dens);
    summary += L"  Hardness=" + std::to_wstring((long long)hard);
    summary += L"  Roughness=" + std::to_wstring((long long)rough);

    const int energy = (dens * 3 + hard * 2 + (100 - rough)) / 6;
    const int leakage = (rough + std::max(0, 100 - hard)) / 2;
    const int node_sel = voxel_atom_node_selected_i32_;
    summary += L"\r\nNode Focus=" + std::to_wstring((long long)(node_sel + 1)) + L"  SimEnergy=" + std::to_wstring((long long)energy);
    summary += L"  Leakage=" + std::to_wstring((long long)leakage);

    if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
        const auto& o = scene_->objects[(size_t)scene_->selected];
        summary += L"\r\nSelection: ";
        summary += utf8_to_wide(o.name_utf8);
        summary += L"  anchor=" + std::to_wstring((unsigned long long)o.anchor_id_u32);
        if (o.pbr_scan_u8) summary += L"  [PBR/scan]";
        if (o.anchor_id_u32 < scene_->sm.anchors.size()) {
            const Anchor& A = scene_->sm.anchors[o.anchor_id_u32];
            summary += L"\r\nAnchor Resonance: f=" + std::to_wstring((long long)A.last_f_code);
            summary += L"  a=" + std::to_wstring((long long)A.last_a_code);
            summary += L"  v=" + std::to_wstring((long long)A.last_v_code);
            summary += L"  i=" + std::to_wstring((long long)A.last_i_code);
            summary += L"  hmean=" + std::to_wstring((long long)A.harmonics_mean_q15);
        }
        summary += L"\r\nViewport: ";
        summary += resonance_view_ ? L"resonance mode ON" : L"standard mode";
        summary += L"  band=" + std::to_wstring((long long)spectrum_band_i32_);
    } else {
        summary += L"\r\nSelection: none";
    }

    if (scene_) {
        summary += L"\r\nCanonical anchors: spectral=" + std::to_wstring((unsigned long long)scene_->sm.spectral_field_anchor_id_u32);
        summary += L"  voxel=" + std::to_wstring((unsigned long long)scene_->sm.voxel_coupling_anchor_id_u32);
        summary += L"  collision=" + std::to_wstring((unsigned long long)scene_->sm.collision_env_anchor_id_u32);
    }
    summary += L"\r\nEdit packet schema: preset -> atom node focus -> anchor resonance -> viewport resonance overlay.";
    summary += L"\r\nShared authoring contract: asset builder / planet builder / character tools / voxel designer all route through linked content and coherence review before high-impact apply.";
    summary += L"\r\nThis editor lane is authoring-only; shipping runtime builds exclude editor panels/tools.";
    SetWindowTextW(hwnd_voxel_summary_, summary.c_str());
    RefreshViewportResonanceOverlay();
}

bool App::SelectContentForSelectedObject() {
    if (!scene_ || scene_->selected < 0 || scene_->selected >= (int)scene_->objects.size()) return false;
    const auto& o = scene_->objects[(size_t)scene_->selected];
    const std::string base = ew_obj_basename_utf8(o.name_utf8);
    if (base.empty()) return false;
    for (const auto& item : content_items_) {
        if (item.rel_utf8.find(base) != std::string::npos || item.label_utf8.find(base) != std::string::npos) {
            return SelectContentRelativePath(item.rel_utf8);
        }
    }
    return false;
}

bool App::ReviewReferencesForPath(const std::string& rel_utf8, const wchar_t* origin_w) {
    if (rel_utf8.empty() || !scene_) return false;
    CanonicalReferenceSummary summary{};
    std::string err;
    (void)BuildCanonicalReferenceSummaryForPath(rel_utf8, 32u, &summary, &err);
    CommitCanonicalReferenceSummary(summary, true, origin_w);
    asset_last_review_rel_utf8_ = rel_utf8;
    asset_last_review_revision_u64_ = coh_highlight_seen_revision_u64_;
    RefreshAssetDesignerPanel();
    return true;
}


void App::RefreshSequencerPanel() {
    if (hwnd_seq_timeline_) {
        SendMessageW(hwnd_seq_timeline_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(hwnd_seq_timeline_, LB_RESETCONTENT, 0, 0);
        const wchar_t* lanes[] = {
            L"Lane 1 :: locomotion carrier / root pose",
            L"Lane 2 :: pose correction / contact recovery",
            L"Lane 3 :: character bind / rig response",
            L"Lane 4 :: stress-pain derived overlay",
            L"Lane 5 :: motion-match packet hook"
        };
        for (const wchar_t* lane : lanes) SendMessageW(hwnd_seq_timeline_, LB_ADDSTRING, 0, (LPARAM)lane);
        const int lane_count = (int)(sizeof(lanes) / sizeof(lanes[0]));
        if (seq_selected_i32_ < 0 || seq_selected_i32_ >= lane_count) seq_selected_i32_ = 0;
        SendMessageW(hwnd_seq_timeline_, LB_SETCURSEL, (WPARAM)seq_selected_i32_, 0);
        SendMessageW(hwnd_seq_timeline_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hwnd_seq_timeline_, nullptr, TRUE);
    }
    if (hwnd_seq_play_) SetWindowTextW(hwnd_seq_play_, seq_play_enabled_ ? L"Pause" : L"Play");
    if (hwnd_seq_loop_) SetWindowTextW(hwnd_seq_loop_, seq_loop_builder_enabled_ ? L"Loop Armed" : L"Loop Build");
    if (hwnd_seq_stress_overlay_) SetWindowTextW(hwnd_seq_stress_overlay_, seq_stress_overlay_enabled_ ? L"Stress On" : L"Stress Off");

    std::wstring summary;
    summary += L"Sequencer contract: editor-only derived timeline authoring; runtime consumes bounded control packets only.
";
    summary += L"Transport: ";
    summary += seq_play_enabled_ ? L"playing" : L"paused";
    summary += L"  |  Loop builder: ";
    summary += seq_loop_builder_enabled_ ? L"armed" : L"idle";
    summary += L"  |  Stress overlay: ";
    summary += seq_stress_overlay_enabled_ ? L"visible" : L"hidden";
    summary += L"
";

    const std::wstring archetype_w = ew_combo_selected_text_safe(hwnd_asset_character_archetype_, L"Biped Explorer");
    const int h_cm = ew_read_trackbar_pos_safe(hwnd_asset_character_height_, 172);
    const int rigidity = ew_read_trackbar_pos_safe(hwnd_asset_character_rigidity_, 54);
    const int gait = ew_read_trackbar_pos_safe(hwnd_asset_character_gait_, 48);
    const bool has_selection = scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size();
    std::wstring selection_name_w = L"none";
    uint32_t selection_anchor_id_u32 = 0u;
    if (has_selection) {
        const auto& o = scene_->objects[(size_t)scene_->selected];
        selection_name_w = utf8_to_wide(o.name_utf8);
        selection_anchor_id_u32 = o.anchor_id_u32;
    }
    summary += L"Character motion binding: archetype=" + archetype_w +
               L"  height(cm)=" + std::to_wstring((long long)h_cm) +
               L"  rigidity=" + std::to_wstring((long long)rigidity) +
               L"  gait=" + std::to_wstring((long long)gait) + L"
";
    summary += L"Selected object: " + selection_name_w;
    if (has_selection) summary += L"  (anchor=" + std::to_wstring((unsigned long long)selection_anchor_id_u32) + L")";
    summary += L"
";
    static const wchar_t* lane_effects[] = {
        L"Root motion and clip timing stay bounded to the selected anchor/object relationship.",
        L"Pose correction stitches contact/recovery without turning the sequencer into a physics authority.",
        L"Character-bind lane reflects the current proxy rig envelope and selected content target.",
        L"Stress/pain remains shader/overlay-only; no gameplay or body-state authority is moved here.",
        L"Motion hook emits one bounded locomotion packet path and hands execution back to the canonical runtime."
    };
    const int lane_idx = (seq_selected_i32_ >= 0 && seq_selected_i32_ < 5) ? seq_selected_i32_ : 0;
    summary += L"Selected lane: " + std::wstring(lane_effects[lane_idx]) + L"
";
    const bool review_current = !content_selected_rel_utf8_.empty() &&
                                asset_last_review_rel_utf8_ == content_selected_rel_utf8_ &&
                                asset_last_review_revision_u64_ == coh_highlight_seen_revision_u64_;
    if (!content_selected_rel_utf8_.empty()) {
        summary += L"Linked content target: " + utf8_to_wide(content_selected_rel_utf8_);
        summary += review_current ? L"  [review current]" : L"  [review stale or missing]";
        summary += L"
";
    }
    summary += L"Viewport feedback: ";
    summary += resonance_view_ ? L"resonance view ON" : L"standard view";
    summary += live_mode_enabled_ ? L"  |  live substrate source active" : L"  |  sandbox projection active";
    summary += L"
";
    summary += L"Runtime/editor split: sequencer widgets and authoring summaries stay editor-side; runtime/export paths only receive bounded packets and staged bundles.
";
    if (hwnd_seq_summary_) SetWindowTextW(hwnd_seq_summary_, summary.c_str());
}

void App::RefreshContentBrowserChrome() {
    if (hwnd_content_status_) {
        std::wstring mode = L"List";
        if (content_view_mode_u32_ == 1u) mode = L"Thumb";
        else if (content_view_mode_u32_ == 2u) mode = L"3D";
        std::wstring status = mode + L" mode. Visible items: " + std::to_wstring((unsigned long long)content_visible_indices_.size());
        if (!coh_highlight_set_w_.empty()) status += L". Coherence highlights active.";
        if (!content_selected_rel_utf8_.empty() && canonical_reference_summary_.subject_rel_utf8 == content_selected_rel_utf8_) {
            status += L". Canonical refs=" + std::to_wstring((unsigned long long)canonical_reference_summary_.hit_count_u32);
        }
        SetWindowTextW(hwnd_content_status_, status.c_str());
    }
    if (hwnd_content_refcheck_) {
        std::wstring label = L"Refs";
        if (!content_selected_rel_utf8_.empty() && canonical_reference_summary_.subject_rel_utf8 == content_selected_rel_utf8_) {
            label += L" (" + std::to_wstring((unsigned long long)canonical_reference_summary_.hit_count_u32) + L")";
        }
        SetWindowTextW(hwnd_content_refcheck_, label.c_str());
    }
    if (hwnd_content_selected_) {
        std::wstring sel = L"Selected: none";
        if (!content_selected_rel_utf8_.empty()) {
            sel = L"Selected: " + utf8_to_wide(content_selected_rel_utf8_);
        }
        SetWindowTextW(hwnd_content_selected_, sel.c_str());
    }
    if (hwnd_content_view_list_) SetWindowTextW(hwnd_content_view_list_, content_view_mode_u32_ == 0u ? L"[List]" : L"List");
    if (hwnd_content_view_thumb_) SetWindowTextW(hwnd_content_view_thumb_, content_view_mode_u32_ == 1u ? L"[Thumb]" : L"Thumb");
    if (hwnd_content_view_3d_) SetWindowTextW(hwnd_content_view_3d_, content_view_mode_u32_ == 2u ? L"[3D]" : L"3D");
}

void App::RefreshContent3DBrowserSurface() {
    if (!hwnd_content_3d_) return;
    SendMessageW(hwnd_content_3d_, LB_RESETCONTENT, 0, 0);
    for (size_t vis = 0; vis < content_visible_indices_.size(); ++vis) {
        const size_t src_index = content_visible_indices_[vis];
        if (src_index >= content_items_.size()) continue;
        const auto& e = content_items_[src_index];
        std::wstring label = utf8_to_wide(e.label_utf8.empty() ? e.rel_utf8 : e.label_utf8);
        std::wstring path = utf8_to_wide(e.rel_utf8);
        std::wstring line = label;
        if (label != path) line += L"  --  " + path;
        const LRESULT row = SendMessageW(hwnd_content_3d_, LB_ADDSTRING, 0, (LPARAM)line.c_str());
        if (row >= 0) SendMessageW(hwnd_content_3d_, LB_SETITEMDATA, (WPARAM)row, (LPARAM)src_index);
    }
    if (!content_selected_rel_utf8_.empty()) (void)SelectContentRelativePath(content_selected_rel_utf8_);
}

void App::SetContentBrowserViewMode(uint32_t mode_u32) {
    content_view_mode_u32_ = (mode_u32 <= 2u) ? mode_u32 : 0u;
    if (hwnd_content_list_) ShowWindow(hwnd_content_list_, content_view_mode_u32_ == 0u ? SW_SHOW : SW_HIDE);
    if (hwnd_content_thumb_) ShowWindow(hwnd_content_thumb_, content_view_mode_u32_ == 1u ? SW_SHOW : SW_HIDE);
    if (hwnd_content_3d_) ShowWindow(hwnd_content_3d_, content_view_mode_u32_ == 2u ? SW_SHOW : SW_HIDE);
    RefreshContentBrowserChrome();
}

bool App::SelectContentRelativePath(const std::string& rel_utf8) {
    if (rel_utf8.empty()) return false;
    content_selection_sync_guard_ = true;
    auto clear_list = [&](HWND h)->void {
        if (!h) return;
        const int n = ListView_GetItemCount(h);
        for (int i = 0; i < n; ++i) ListView_SetItemState(h, i, 0, LVIS_SELECTED | LVIS_FOCUSED);
    };
    clear_list(hwnd_content_list_);
    clear_list(hwnd_content_thumb_);
    if (hwnd_content_3d_) SendMessageW(hwnd_content_3d_, LB_SETCURSEL, (WPARAM)-1, 0);

    bool found = false;
    auto select_list = [&](HWND h)->bool {
        if (!h) return false;
        const int n = ListView_GetItemCount(h);
        for (int i = 0; i < n; ++i) {
            wchar_t w2[512]; w2[0] = 0;
            ListView_GetItemText(h, i, 0, w2, 512);
            if (wide_to_utf8(std::wstring(w2)) == rel_utf8) {
                ListView_SetItemState(h, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(h, i, FALSE);
                return true;
            }
        }
        return false;
    };
    found = select_list(hwnd_content_list_) || found;
    found = select_list(hwnd_content_thumb_) || found;
    if (hwnd_content_3d_) {
        const int n = (int)SendMessageW(hwnd_content_3d_, LB_GETCOUNT, 0, 0);
        for (int i = 0; i < n; ++i) {
            const LRESULT data = SendMessageW(hwnd_content_3d_, LB_GETITEMDATA, (WPARAM)i, 0);
            if (data >= 0 && (size_t)data < content_items_.size() && content_items_[(size_t)data].rel_utf8 == rel_utf8) {
                SendMessageW(hwnd_content_3d_, LB_SETCURSEL, (WPARAM)i, 0);
                SendMessageW(hwnd_content_3d_, LB_SETTOPINDEX, (WPARAM)i, 0);
                found = true;
                break;
            }
        }
    }
    content_selection_sync_guard_ = false;
    if (found) {
        content_selected_rel_utf8_ = rel_utf8;
        CanonicalReferenceSummary summary{};
        std::string err;
        if (BuildCanonicalReferenceSummaryForPath(rel_utf8, 8u, &summary, &err)) CommitCanonicalReferenceSummary(summary, false, L"Content selection");
    }
    RefreshContentBrowserChrome();
    RefreshAssetDesignerPanel();
    return found;
}

void App::RefreshContentBrowserFromRuntime(uint32_t limit_u32) {
    content_items_.clear();
    content_selected_rel_utf8_.clear();
    if (!scene_) {
        RebuildContentBrowserViews();
        return;
    }
    std::vector<genesis::GeAssetEntry> entries;
    std::string err;
    if (!scene_->SnapshotContentEntries(limit_u32, entries, &err)) {
        AppendOutputUtf8(std::string("CONTENT_LIST_FAIL ") + err);
        RebuildContentBrowserViews();
        return;
    }
    content_items_.reserve(entries.size());
    for (const auto& e : entries) {
        content_items_.push_back({e.relpath_utf8, e.label_utf8.empty() ? e.relpath_utf8 : e.label_utf8});
    }
    RebuildContentBrowserViews();
    RefreshContentBrowserChrome();
}

int App::Run(HINSTANCE hInst) {
    // Win64 enforcement at runtime too (belt + suspenders)
    static_assert(sizeof(void*) == 8, "Win64 required");

    CreateMainWindow(hInst);
    CreateChildWindows();

    scene_ = new Scene();

    if (ew_editor_build_enabled) {
        SetContentBrowserViewMode(0u);
        RefreshContentBrowserChrome();

        // Populate content browser after substrate/runtime is live.
        RefreshContentBrowserFromRuntime(200u);
    }

    // Ensure AI learning/crawling boot OFF (UI + substrate).
    ai_learning_enabled_ = false;
    ai_crawling_enabled_ = false;
    {
        EwControlPacket cp{};
        cp.source_u16 = 1;
        cp.tick_u64 = 0;
        cp.kind = EwControlPacketKind::AiSetLearning;
        cp.payload.ai_set_learning.enabled_u8 = 0;
        (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
        cp.kind = EwControlPacketKind::AiSetCrawling;
        cp.payload.ai_set_crawling.enabled_u8 = 0;
        (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
    }

    // Create AI panel window (hidden by default) only for editor builds.
    if (ew_editor_build_enabled) CreateAiPanelWindow();


    // Headless visualization mode: keep simulation running but skip continuous presentation.
    visualize_enabled_ = !(env_truthy("GENESIS_HEADLESS") || env_truthy("HEADLESS"));

    vk_ = new VkCtx();
    vk_->enable_validation = env_truthy("EW_VK_VALIDATION");

    // Optional OpenXR init (Vulkan path). If OpenXR is present, we pull the
    // required Vulkan instance/device extension lists and include them in the
    // Vulkan creation path. This keeps the build production-grade without
    // requiring display firmware changes.
    const bool want_xr = env_truthy("GENESIS_OPENXR") || env_truthy("GENESIS_XR") || env_truthy("OPENXR");
    bool xr_ok_init = false;
    if (want_xr) {
        xr_ok_init = xr_.Init();
    }

    vk_->instance = create_instance(vk_->enable_validation, &vk_->dbg,
                                    xr_ok_init ? xr_.VulkanInstanceExtensions() : "");
    vk_->surface = create_surface(vk_->instance, hwnd_viewport_);
    vk_->phys = pick_phys(vk_->instance);
    if (!vk_->phys) {
        MessageBoxA(hwnd_main_, "No Vulkan physical device found.", "GenesisEngineVulkan", MB_ICONERROR | MB_OK);
        return 1;
    }
    vk_->gfxq_family = find_gfx_queue(vk_->phys, vk_->surface);
    vk_->dev = create_device(vk_->phys, vk_->gfxq_family, &vk_->gfxq,
                             xr_ok_init ? xr_.VulkanDeviceExtensions() : "");

    // Bind Vulkan to OpenXR and create session. This is required for eye-gaze.
    if (xr_ok_init) {
        xr_.BindVulkan(vk_->instance, vk_->phys, vk_->dev, vk_->gfxq_family, 0);
    }

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = vk_->gfxq_family;
    vk_check(vkCreateCommandPool(vk_->dev, &pci, nullptr, &vk_->cmdpool), "vkCreateCommandPool");

    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vk_check(vkCreateSemaphore(vk_->dev, &sci, nullptr, &vk_->sem_image), "vkCreateSemaphore(image)");
    vk_check(vkCreateSemaphore(vk_->dev, &sci, nullptr, &vk_->sem_render), "vkCreateSemaphore(render)");

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vk_check(vkCreateFence(vk_->dev, &fci, nullptr, &vk_->fence), "vkCreateFence");

    // Initial swap.
    RECT vrc{}; GetClientRect(hwnd_viewport_, &vrc);
    create_swap(*vk_, (uint32_t)(vrc.right - vrc.left), (uint32_t)(vrc.bottom - vrc.top));
    create_pipeline(*vk_);

    // Message loop
    MSG msg{};
    while (running_) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running_ = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Tick();
        Render();
    }

    if (vk_) {
        vk_->DestroyAll();
        delete vk_;
        vk_ = nullptr;
    }
    delete scene_;
    scene_ = nullptr;

    return 0;
}

static inline float ew_clampf(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

static void ew_draw_toggle_switch(const DRAWITEMSTRUCT* dis, bool on) {
    if (!dis) return;
    ew_theme_init_once();
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    // Background
    FillRect(hdc, &rc, g_brush_panel);

    // Track
    RECT tr = rc;
    const int pad = 2;
    tr.left += pad; tr.top += pad; tr.right -= pad; tr.bottom -= pad;
    const int w = tr.right - tr.left;
    const int h = tr.bottom - tr.top;
    const int r = (h > 4) ? (h/2) : 2;

    COLORREF track_col = on ? g_theme.gold : g_theme.edit_bg;
    HBRUSH track = CreateSolidBrush(track_col);
    HPEN pen = CreatePen(PS_SOLID, 1, g_theme.gold);
    HGDIOBJ oldb = SelectObject(hdc, track);
    HGDIOBJ oldp = SelectObject(hdc, pen);
    RoundRect(hdc, tr.left, tr.top, tr.right, tr.bottom, r*2, r*2);
    SelectObject(hdc, oldp);
    SelectObject(hdc, oldb);
    DeleteObject(pen);
    DeleteObject(track);

    // Knob
    const int knob_d = h - 2;
    RECT kb{};
    kb.top = tr.top + 1;
    kb.bottom = kb.top + knob_d;
    if (on) {
        kb.right = tr.right - 1;
        kb.left = kb.right - knob_d;
    } else {
        kb.left = tr.left + 1;
        kb.right = kb.left + knob_d;
    }
    HBRUSH knob = CreateSolidBrush(on ? g_theme.bg : g_theme.gold);
    HGDIOBJ oldk = SelectObject(hdc, knob);
    Ellipse(hdc, kb.left, kb.top, kb.right, kb.bottom);
    SelectObject(hdc, oldk);
    DeleteObject(knob);

    // Focus rectangle
    if (dis->itemState & ODS_FOCUS) {
        DrawFocusRect(hdc, &rc);
    }
}

static bool ew_clip_set_text_utf16(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) { CloseClipboard(); return false; }
    void* p = GlobalLock(h);
    std::memcpy(p, text.c_str(), bytes);
    GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
    return true;
}

static std::wstring ew_clip_get_text_utf16(HWND owner) {
    std::wstring out;
    if (!OpenClipboard(owner)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

// Owner-drawn-ish TabControl styling via NM_CUSTOMDRAW.
static LRESULT ew_tab_custom_draw(const NMHDR* nh, LRESULT* out_res) {
    if (!nh || !out_res) return 0;
    const NMCUSTOMDRAW* cd = (const NMCUSTOMDRAW*)nh;
    ew_theme_init_once();
    switch (cd->dwDrawStage) {
        case CDDS_PREPAINT: {
            *out_res = CDRF_NOTIFYITEMDRAW;
            return 1;
        }
        case CDDS_ITEMPREPAINT: {
            HWND htab = nh->hwndFrom;
            const int i = (int)cd->dwItemSpec;
            RECT rc{};
            if (!TabCtrl_GetItemRect(htab, i, &rc)) { *out_res = CDRF_DODEFAULT; return 1; }
            const int sel = TabCtrl_GetCurSel(htab);
            const bool selected = (i == sel);
            // Background
            HBRUSH bg = CreateSolidBrush(selected ? RGB(32,32,32) : g_theme.panel);
            FillRect(cd->hdc, &rc, bg);
            DeleteObject(bg);
            // Gold underline for selected tab
            if (selected) {
                HPEN pen = CreatePen(PS_SOLID, 2, g_theme.gold);
                HGDIOBJ oldp = SelectObject(cd->hdc, pen);
                MoveToEx(cd->hdc, rc.left + 6, rc.bottom - 2, nullptr);
                LineTo(cd->hdc, rc.right - 6, rc.bottom - 2);
                SelectObject(cd->hdc, oldp);
                DeleteObject(pen);
            }
            // Text
            wchar_t buf[128] = {0};
            TCITEMW ti{}; ti.mask = TCIF_TEXT; ti.pszText = buf; ti.cchTextMax = 127;
            TabCtrl_GetItem(htab, i, &ti);
            SetBkMode(cd->hdc, TRANSPARENT);
            SetTextColor(cd->hdc, selected ? g_theme.gold : g_theme.text);
            HFONT f = selected ? g_font_ui_bold : g_font_ui;
            HGDIOBJ oldf = nullptr;
            if (f) oldf = SelectObject(cd->hdc, f);
            RECT tr = rc; tr.left += 10; tr.right -= 10; tr.top += 6;
            DrawTextW(cd->hdc, buf, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            if (oldf) SelectObject(cd->hdc, oldf);
            *out_res = CDRF_SKIPDEFAULT;
            return 1;
        }
        default: break;
    }
    *out_res = CDRF_DODEFAULT;
    return 1;
}

void App::Tick() {
    static auto last = std::chrono::high_resolution_clock::now();
    static float acc = 0.0f;
    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    // Use a fixed-step substrate tick at 360 Hz. Render cadence may differ.
    const float tick_dt = 1.0f / 360.0f;
    if (dt <= 0.f) dt = tick_dt;
    if (dt > 0.1f) dt = 0.1f;
    last = now;

    if (!scene_) return;

    // OpenXR event pump + frame begin (for predicted display time).
    // If XR is not enabled/bound, these are no-ops.
    xr_.PollEvents();
    xr_.BeginFrame();

    // Toggle visualization (headless/visible) with H key.
    // This does not affect simulation; only presentation.
    static bool prev_h = false;
    const bool h_now = input_.key_down['H'] || input_.key_down['h'];
    if (h_now && !prev_h) {
        visualize_enabled_ = !visualize_enabled_;
        AppendOutputUtf8(visualize_enabled_ ? "VISUALIZATION:ON" : "VISUALIZATION:OFF");
    }
    prev_h = h_now;

    // Open input bindings editor with F9 (editor tool).
    static bool prev_f9 = false;
    const bool f9_now = input_.key_down[VK_F9] != 0;
    if (f9_now && !prev_f9 && scene_) {
        ew_open_bindings_editor(&scene_->sm, scene_->sm.project_settings.input.bindings_path_utf8);
    }
    prev_f9 = f9_now;

    // Authoritative simulation ticks (server-truth) at fixed cadence.
    // Deterministic clamp: never execute more than N ticks per rendered frame.
    acc += dt;
    const int kMaxTicksPerFrame = 8;
    int steps = 0;
    while (acc >= tick_dt && steps < kMaxTicksPerFrame) {
        scene_->Tick();
        acc -= tick_dt;
        steps++;
    }
    // If we fell behind, drop excess accumulated time deterministically.
    if (acc > tick_dt * (float)kMaxTicksPerFrame) acc = 0.0f;

    // AI status line (deterministic telemetry from substrate).
    if (hwnd_ai_status_ && scene_) {
        const uint32_t st = scene_->sm.ai_state_u32;
        const uint32_t stage = scene_->sm.learning_curriculum_stage_u32;
        const char* stage_name = genesis::ew_curriculum_stage_name_ascii(stage);
        const uint32_t stage_missing = scene_->sm.learning_stage_missing_count_u32;
        const uint32_t stage_required = scene_->sm.learning_stage_required_count_u32;
        const uint32_t lane_max = scene_->sm.learning_stage_lane_max_u32;
        const uint32_t pend = scene_->sm.learning_gate.registry().pending_count_u32();
        const uint32_t inflight = (uint32_t)scene_->sm.external_api_inflight.size();
        const uint32_t pending = (uint32_t)scene_->sm.external_api_pending.size();
        const uint32_t autoq = scene_->sm.learning_automation.bus().size_u32();
        const uint64_t vault_exp = scene_->sm.vault_experiments_committed_u64;
        const uint64_t vault_eph = scene_->sm.vault_experiments_ephemeral_u64;
        const uint64_t vault_allow = scene_->sm.vault_allowlist_pages_u64;

        const uint16_t coh = scene_->sm.global_coherence.global_q15;
        const char* st_name = "IDLE";
        if (st == 1u) st_name = "SPEECH_BOOT";
        else if (st == 2u) st_name = "LEARNING";
        else if (st == 3u) st_name = "EXPLORING";
        else if (st == 4u) st_name = "SIM_SYNTH";
        else if (st == 5u) st_name = "VALIDATING";
        else if (st == 6u) st_name = "COMMIT";
        const uint32_t play_u32 = scene_->sm.sim_world_play_u32;
        const uint32_t ai_on_u32 = scene_->sm.ai_enabled_u32;
        const uint32_t learn_on_u32 = scene_->sm.ai_learning_enabled_u32;
        const uint32_t crawl_on_u32 = scene_->sm.ai_crawling_enabled_u32;

        std::string s = std::string("Play=") + (play_u32 ? "ON" : "OFF") +
                        " | AI=" + (ai_on_u32 ? "ON" : "OFF") +
                        " | Learn=" + (learn_on_u32 ? "ON" : "OFF") +
                        " | Crawl=" + (crawl_on_u32 ? "ON" : "OFF") +
                        " | State=" + st_name +
                        " | Stage=" + std::to_string(stage) +
                        "(" + stage_name + ")" +
                        " | Missing=" + std::to_string(stage_missing) + "/" + std::to_string(stage_required) +
                        " | LaneMax=" + std::to_string(lane_max) +
                        " | Coh_q15=" + std::to_string((uint32_t)coh) +
                        " | PendingMetrics=" + std::to_string(pend) +
                        " | AutoQ=" + std::to_string(autoq) +
                        " | VaultExp=" + std::to_string((unsigned long long)vault_exp) +
                        " | EphExp=" + std::to_string((unsigned long long)vault_eph) +
                        " | AllowPages=" + std::to_string((unsigned long long)vault_allow) +
                        " | ResPages=" + std::to_string((unsigned long long)scene_->sm.vault_resonant_pages_u64) +
                        " | Net(inflight/pending)=" + std::to_string(inflight) + "/" + std::to_string(pending);
        SetWindowTextW(hwnd_ai_status_, utf8_to_wide(s).c_str());
    }

    // ------------------------------------------------------------------
    // Editor interaction polish (production-grade feel)
    // - Orbit/Pan/Dolly camera rig emitting CameraSet packets
    // - Viewport click selection (ray-sphere pick)
    // - Translate/Rotate gizmo drag emitting ObjectSetTransform packets
    // All authoritative state remains in anchors; UI only emits control packets.
    // ------------------------------------------------------------------
    const int panel_w = 420;
    const int viewport_w = (client_w_ > panel_w) ? (client_w_ - panel_w) : client_w_;
    const int viewport_h = client_h_;
    const bool mouse_in_view = (input_.mouse_x >= 0 && input_.mouse_y >= 0 &&
                               input_.mouse_x < viewport_w && input_.mouse_y < viewport_h);

    // Hotkeys: W/E for gizmo mode, F to frame.
    static bool prev_w = false, prev_e = false, prev_f = false;
    const bool w_now = (input_.key_down['W'] || input_.key_down['w']);
    const bool e_now = (input_.key_down['E'] || input_.key_down['e']);
    const bool f_now = (input_.key_down['F'] || input_.key_down['f']);
    if (w_now && !prev_w) { editor_gizmo_mode_u8_ = 1; EmitEditorGizmo(); }
    if (e_now && !prev_e) { editor_gizmo_mode_u8_ = 2; EmitEditorGizmo(); }
    prev_w = w_now;
    prev_e = e_now;

// Axis constraint hotkeys: X/Y/Z toggles axis constraint (no Alt). Press again to clear.
static bool prev_x = false, prev_yk = false, prev_zk = false;
const bool x_now = (input_.key_down['X'] || input_.key_down['x']);
const bool y_now2 = (input_.key_down['Y'] || input_.key_down['y']);
const bool z_now2 = (input_.key_down['Z'] || input_.key_down['z']);
if (mouse_in_view && !input_.alt) {
    if (x_now && !prev_x) { editor_axis_constraint_u8_ = (editor_axis_constraint_u8_ == 1) ? 0 : 1; EmitEditorAxisConstraint(); }
    if (y_now2 && !prev_yk) { editor_axis_constraint_u8_ = (editor_axis_constraint_u8_ == 2) ? 0 : 2; EmitEditorAxisConstraint(); }
    if (z_now2 && !prev_zk) { editor_axis_constraint_u8_ = (editor_axis_constraint_u8_ == 3) ? 0 : 3; EmitEditorAxisConstraint(); }
}
prev_x = x_now;
prev_yk = y_now2;
prev_zk = z_now2;

// Undo/redo hotkeys: Ctrl+Z / Ctrl+Y.
static bool prev_uz = false, prev_uy = false;
const bool uz_now = (input_.ctrl && (input_.key_down['Z'] || input_.key_down['z']));
const bool uy_now = (input_.ctrl && (input_.key_down['Y'] || input_.key_down['y']));
if (uz_now && !prev_uz) { EmitEditorUndo(); RefreshEditorHistoryUi(); }
if (uy_now && !prev_uy) { EmitEditorRedo(); RefreshEditorHistoryUi(); }
prev_uz = uz_now;
prev_uy = uy_now;

    // Camera rig controls (Alt + mouse):
    //   Alt+LMB orbit, Alt+MMB pan, wheel dolly.
    // Ctrl+Alt+LMB is reserved for UE-style duplicate-and-drag and must not orbit the camera.
    if (mouse_in_view) {
        const bool duplicate_gesture_active = input_.ctrl && input_.alt && input_.lmb;
        if (input_.alt && input_.lmb && !input_.ctrl && !gizmo_drag_active_) {
            cam_yaw_rad_   += (float)input_.mouse_dx * 0.005f;
            cam_pitch_rad_ += (float)-input_.mouse_dy * 0.005f;
            cam_pitch_rad_ = ew_clampf(cam_pitch_rad_, -1.5f, 1.5f);
        }
        if (input_.alt && input_.mmb && !gizmo_drag_active_) {
            // Pan in camera plane (approx): move target along yaw basis.
            const float cy = cosf(cam_yaw_rad_);
            const float sy = sinf(cam_yaw_rad_);
            float right[3] = {-sy, cy, 0.0f};
            float up[3] = {0.0f, 0.0f, 1.0f};
            const float pan_scale = cam_dist_m_ * 0.0015f;
            cam_target_[0] += right[0] * (float)-input_.mouse_dx * pan_scale + up[0] * (float)input_.mouse_dy * pan_scale;
            cam_target_[1] += right[1] * (float)-input_.mouse_dx * pan_scale + up[1] * (float)input_.mouse_dy * pan_scale;
            cam_target_[2] += right[2] * (float)-input_.mouse_dx * pan_scale + up[2] * (float)input_.mouse_dy * pan_scale;
        }
        if (input_.wheel_delta != 0) {
            const int steps = (input_.wheel_delta > 0) ? 1 : -1;
            // Hold ` (tilde) and scroll to shift the viewport spectrum band (resonance view aid).
            if (input_.key_down[VK_OEM_3]) {
                spectrum_band_i32_ += steps;
                if (spectrum_band_i32_ < -4) spectrum_band_i32_ = -4;
                if (spectrum_band_i32_ >  8) spectrum_band_i32_ =  8;
                spectrum_phase_f32_ += 0.173f * (float)steps;
                if (live_mode_enabled_) SyncLiveModeProjection();
            } else if (!duplicate_gesture_active) {
                const float s = (steps > 0) ? 0.9f : 1.1111111f;
                cam_dist_m_ *= s;
                if (cam_dist_m_ < 0.1f) cam_dist_m_ = 0.1f;
            }
        }
    }

    // Frame selection (F): sets camera target to selected object and distances to radius.
    if (f_now && !prev_f) {
        if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
            const auto& o = scene_->objects[(size_t)scene_->selected];
            cam_target_[0] = (float)o.pos_q16_16[0] / 65536.0f;
            cam_target_[1] = (float)o.pos_q16_16[1] / 65536.0f;
            cam_target_[2] = (float)o.pos_q16_16[2] / 65536.0f;
            const float r = (float)o.radius_q16_16 / 65536.0f;
            cam_dist_m_ = (r > 0.001f) ? (r * 3.0f) : 5.0f;
        }
    }
    prev_f = f_now;

    // Emit camera packet from rig every tick (authoritative camera lives in anchor).
    EmitCameraSetFromRig();
    if (node_play_excitation_ && hwnd_node_graph_ && IsWindowVisible(hwnd_node_graph_)) InvalidateRect(hwnd_node_graph_, nullptr, FALSE);
    if (hwnd_viewport_resonance_overlay_ && resonance_view_ && ((scene_ ? scene_->sm.canonical_tick : 0ull) & 7ull) == 0ull) RefreshViewportResonanceOverlay();

    // Viewport click selection (Ctrl-click toggles multi-selection; Shift also toggles).
    static bool prev_lmb = false;
    const bool lmb_edge = (input_.lmb && !prev_lmb);
    prev_lmb = input_.lmb;
    if (mouse_in_view && lmb_edge && !input_.alt) {
        // Compute ray from camera rig.
        const float aspect = (viewport_h > 0) ? (float)viewport_w / (float)viewport_h : 1.0f;
        const float fov = 60.0f * 0.01745329251994329577f;
        const float tanh = tanf(fov * 0.5f);
        const float nx = ((float)input_.mouse_x / (float)viewport_w) * 2.0f - 1.0f;
        const float ny = 1.0f - ((float)input_.mouse_y / (float)viewport_h) * 2.0f;

        // Camera basis from rig.
        const float cy = cosf(cam_yaw_rad_);
        const float sy = sinf(cam_yaw_rad_);
        const float cp = cosf(cam_pitch_rad_);
        const float sp = sinf(cam_pitch_rad_);
        float cam_pos[3] = { cam_target_[0] + cy*cp*cam_dist_m_, cam_target_[1] + sy*cp*cam_dist_m_, cam_target_[2] + sp*cam_dist_m_ };
        float fwd[3] = { cam_target_[0]-cam_pos[0], cam_target_[1]-cam_pos[1], cam_target_[2]-cam_pos[2] };
        float fl = sqrtf(fwd[0]*fwd[0] + fwd[1]*fwd[1] + fwd[2]*fwd[2]);
        if (fl > 1e-6f) { fwd[0]/=fl; fwd[1]/=fl; fwd[2]/=fl; }
        float up[3] = {0,0,1};
        float right[3] = { fwd[1]*up[2]-fwd[2]*up[1], fwd[2]*up[0]-fwd[0]*up[2], fwd[0]*up[1]-fwd[1]*up[0] };
        float rl = sqrtf(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
        if (rl < 1e-6f) { right[0]=1; right[1]=0; right[2]=0; rl=1; }
        right[0]/=rl; right[1]/=rl; right[2]/=rl;
        float u2[3] = { right[1]*fwd[2]-right[2]*fwd[1], right[2]*fwd[0]-right[0]*fwd[2], right[0]*fwd[1]-right[1]*fwd[0] };

        float dir[3] = {
            fwd[0] + right[0] * (nx * aspect * tanh) + u2[0] * (ny * tanh),
            fwd[1] + right[1] * (nx * aspect * tanh) + u2[1] * (ny * tanh),
            fwd[2] + right[2] * (nx * aspect * tanh) + u2[2] * (ny * tanh),
        };
        float dl = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
        if (dl > 1e-6f) { dir[0]/=dl; dir[1]/=dl; dir[2]/=dl; }

        int best_idx = -1;
        float best_t = 1e30f;
        if (scene_) {
            for (int i = 0; i < (int)scene_->objects.size(); ++i) {
                const auto& o = scene_->objects[(size_t)i];
                const float cx = (float)o.pos_q16_16[0] / 65536.0f;
                const float cy2 = (float)o.pos_q16_16[1] / 65536.0f;
                const float cz2 = (float)o.pos_q16_16[2] / 65536.0f;
                const float r = (float)o.radius_q16_16 / 65536.0f;
                if (r <= 0.0f) continue;
                // Ray-sphere intersection (closest positive t)
                const float ox = cam_pos[0] - cx;
                const float oy = cam_pos[1] - cy2;
                const float oz = cam_pos[2] - cz2;
                const float b = ox*dir[0] + oy*dir[1] + oz*dir[2];
                const float c = ox*ox + oy*oy + oz*oz - r*r;
                const float disc = b*b - c;
                if (disc < 0.0f) continue;
                const float t0 = -b - sqrtf(disc);
                if (t0 > 0.0f && t0 < best_t) { best_t = t0; best_idx = i; }
            }
        }
        if (best_idx >= 0 && scene_) {
            scene_->selected = best_idx;
            SendMessageW(hwnd_objlist_, LB_SETCURSEL, (WPARAM)best_idx, 0);
            editor_selected_object_id_u64_ = scene_->objects[(size_t)best_idx].object_id_u64;
            if (input_.ctrl || input_.shift) {
                EmitEditorToggleSelection(editor_selected_object_id_u64_);
                if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Viewport Ctrl/Shift select toggled object membership in the editor selection.");
            } else {
                EmitEditorSelection(editor_selected_object_id_u64_);
            }
            RefreshDetailsFromSelection();
            RefreshAssetDesignerPanel();
            RefreshVoxelDesignerPanel();
            RefreshNodePanel();
            RefreshSequencerPanel();
        }
    }

    // Gizmo drag (no Alt):
    // Translate: drag in camera plane (approx)
    // Rotate: drag yaw about world up
    if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
        const bool duplicate_gesture_active = input_.ctrl && input_.alt && input_.lmb;
        const bool can_drag = mouse_in_view && (!input_.alt || duplicate_gesture_active);
        if (!input_.lmb || !input_.ctrl || !input_.alt) duplicate_drag_consumed_ = false;
        if (can_drag && input_.lmb && !gizmo_drag_active_) {
            if (duplicate_gesture_active && !duplicate_drag_consumed_ && scene_) {
                const bool duplicated = scene_->DuplicateSelectedObjectForEditor(0.20f, 0.0f, 0.20f);
                duplicate_drag_consumed_ = true;
                if (duplicated) {
                    if (scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
                        editor_selected_object_id_u64_ = scene_->objects[(size_t)scene_->selected].object_id_u64;
                        EmitEditorSelection(editor_selected_object_id_u64_);
                        SendMessageW(hwnd_objlist_, LB_SETCURSEL, (WPARAM)scene_->selected, 0);
                    }
                    if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Duplicated selected object with Ctrl+Alt drag and armed gizmo drag on the duplicate.");
                    RefreshOutlinerFromScene();
                    RefreshDetailsFromSelection();
                    RefreshAssetDesignerPanel();
                    RefreshVoxelDesignerPanel();
                    RefreshNodePanel();
                    RefreshSequencerPanel();
                }
            }
            if (scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
                auto& o = scene_->objects[(size_t)scene_->selected];
                gizmo_drag_active_ = true;
                drag_start_mouse_x_ = input_.mouse_x;
                drag_start_mouse_y_ = input_.mouse_y;
                drag_start_pos_q16_16_[0] = o.pos_q16_16[0];
                drag_start_pos_q16_16_[1] = o.pos_q16_16[1];
                drag_start_pos_q16_16_[2] = o.pos_q16_16[2];
                for (int k = 0; k < 4; ++k) drag_start_rot_q16_16_[k] = o.rot_quat_q16_16[k];
                drag_last_pos_q16_16_[0] = drag_start_pos_q16_16_[0];
                drag_last_pos_q16_16_[1] = drag_start_pos_q16_16_[1];
                drag_last_pos_q16_16_[2] = drag_start_pos_q16_16_[2];
                for (int k = 0; k < 4; ++k) drag_last_rot_q16_16_[k] = drag_start_rot_q16_16_[k];
            }
        }
        auto& o = scene_->objects[(size_t)scene_->selected];
        if (gizmo_drag_active_ && !input_.lmb) {
            // Commit one undo step for the whole drag.
            EmitEditorCommitTransformTxn(o.object_id_u64,
                                       drag_start_pos_q16_16_,
                                       drag_start_rot_q16_16_,
                                       drag_last_pos_q16_16_,
                                       drag_last_rot_q16_16_);
            RefreshEditorHistoryUi();
            gizmo_drag_active_ = false;
        }

        if (gizmo_drag_active_) {
            const int dx = input_.mouse_x - drag_start_mouse_x_;
            const int dy = input_.mouse_y - drag_start_mouse_y_;

            EwControlPacket cp{};
            cp.kind = EwControlPacketKind::ObjectSetTransform;
            cp.source_u16 = 1;
            cp.tick_u64 = scene_->sm.canonical_tick;
            cp.payload.object_set_transform.object_id_u64 = o.object_id_u64;
            cp.payload.object_set_transform.pad0_u32 = 0u;
            cp.payload.object_set_transform.pad1_u32 = 0u;

            if (editor_gizmo_mode_u8_ == 1) {
                // Translate in yaw-plane basis.
                const float cy = cosf(cam_yaw_rad_);
                const float sy = sinf(cam_yaw_rad_);
                float right[3] = {-sy, cy, 0.0f};
                float up[3] = {0.0f, 0.0f, 1.0f};
                const float scale = cam_dist_m_ * 0.0015f;
                float tx = right[0] * (float)dx * scale + up[0] * (float)-dy * scale;
                float ty = right[1] * (float)dx * scale + up[1] * (float)-dy * scale;
                float tz = right[2] * (float)dx * scale + up[2] * (float)-dy * scale;

                // Axis constraint (world axes).
                if (editor_axis_constraint_u8_ == 1) { ty = 0.0f; tz = 0.0f; }
                else if (editor_axis_constraint_u8_ == 2) { tx = 0.0f; tz = 0.0f; }
                else if (editor_axis_constraint_u8_ == 3) { tx = 0.0f; ty = 0.0f; }

                int32_t nx_q = drag_start_pos_q16_16_[0] + (int32_t)llround((double)tx * 65536.0);
                int32_t ny_q = drag_start_pos_q16_16_[1] + (int32_t)llround((double)ty * 65536.0);
                int32_t nz_q = drag_start_pos_q16_16_[2] + (int32_t)llround((double)tz * 65536.0);
                if (editor_snap_enabled_u8_) {
                    const int32_t step = (editor_grid_step_m_q16_16_ > 0) ? editor_grid_step_m_q16_16_ : (int32_t)65536;
                    auto snap = [&](int32_t v)->int32_t {
                        const int64_t half = (int64_t)step / 2;
                        const int64_t vv = (int64_t)v;
                        const int64_t s = (vv >= 0) ? (vv + half) : (vv - half);
                        return (int32_t)((s / step) * step);
                    };
                    nx_q = snap(nx_q);
                    ny_q = snap(ny_q);
                    nz_q = snap(nz_q);
                }
                cp.payload.object_set_transform.pos_q16_16[0] = nx_q;
                cp.payload.object_set_transform.pos_q16_16[1] = ny_q;
                cp.payload.object_set_transform.pos_q16_16[2] = nz_q;
                for (int k = 0; k < 4; ++k) cp.payload.object_set_transform.rot_quat_q16_16[k] = drag_start_rot_q16_16_[k];
            } else if (editor_gizmo_mode_u8_ == 2) {
                // Rotate about world up using horizontal drag.
                const float deg_per_px = 0.15f;
                float yaw_deg = (float)dx * deg_per_px;
                if (editor_snap_enabled_u8_) {
                    const float step = (float)editor_angle_step_deg_q16_16_ / 65536.0f;
                    if (step > 0.01f) {
                        const float s = (yaw_deg >= 0.0f) ? (yaw_deg + 0.5f*step) : (yaw_deg - 0.5f*step);
                        yaw_deg = floorf(s / step) * step;
                    }
                }
                const float rad = yaw_deg * 0.01745329251994329577f;
                const float sh = sinf(rad * 0.5f);
                const float ch = cosf(rad * 0.5f);
                // Axis-angle quaternion (world axis): (axis*sin(theta/2), cos(theta/2)) in Q16.16.
int32_t dq[4] = {0,0,0,(int32_t)llround((double)ch * 65536.0)};
if (editor_axis_constraint_u8_ == 1) {
    dq[0] = (int32_t)llround((double)sh * 65536.0);
} else if (editor_axis_constraint_u8_ == 2) {
    dq[1] = (int32_t)llround((double)sh * 65536.0);
} else {
    // Default Z.
    dq[2] = (int32_t)llround((double)sh * 65536.0);
}
                // Multiply dq * start (approx, not renormalized; quantization is the stability gate).
                const int64_t ax = dq[0], ay = dq[1], az = dq[2], aw = dq[3];
                const int64_t bx = drag_start_rot_q16_16_[0], by = drag_start_rot_q16_16_[1], bz = drag_start_rot_q16_16_[2], bw = drag_start_rot_q16_16_[3];
                int32_t qx = (int32_t)((aw*bx + ax*bw + ay*bz - az*by) >> 16);
                int32_t qy = (int32_t)((aw*by - ax*bz + ay*bw + az*bx) >> 16);
                int32_t qz = (int32_t)((aw*bz + ax*by - ay*bx + az*bw) >> 16);
                int32_t qw = (int32_t)((aw*bw - ax*bx - ay*by - az*bz) >> 16);
                cp.payload.object_set_transform.pos_q16_16[0] = drag_start_pos_q16_16_[0];
                cp.payload.object_set_transform.pos_q16_16[1] = drag_start_pos_q16_16_[1];
                cp.payload.object_set_transform.pos_q16_16[2] = drag_start_pos_q16_16_[2];
                cp.payload.object_set_transform.rot_quat_q16_16[0] = qx;
                cp.payload.object_set_transform.rot_quat_q16_16[1] = qy;
                cp.payload.object_set_transform.rot_quat_q16_16[2] = qz;
                cp.payload.object_set_transform.rot_quat_q16_16[3] = qw;
            } else {
                cp.payload.object_set_transform.pos_q16_16[0] = drag_start_pos_q16_16_[0];
                cp.payload.object_set_transform.pos_q16_16[1] = drag_start_pos_q16_16_[1];
                cp.payload.object_set_transform.pos_q16_16[2] = drag_start_pos_q16_16_[2];
                for (int k = 0; k < 4; ++k) cp.payload.object_set_transform.rot_quat_q16_16[k] = drag_start_rot_q16_16_[k];
            }

            // Cache last emitted transform for undo commit stability.
            drag_last_pos_q16_16_[0] = cp.payload.object_set_transform.pos_q16_16[0];
            drag_last_pos_q16_16_[1] = cp.payload.object_set_transform.pos_q16_16[1];
            drag_last_pos_q16_16_[2] = cp.payload.object_set_transform.pos_q16_16[2];
            for (int k = 0; k < 4; ++k) drag_last_rot_q16_16_[k] = cp.payload.object_set_transform.rot_quat_q16_16[k];

            (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
        }
    }

    // Immersion: camera subject-lock should be implemented as substrate ops.
    // Hard rule: no direct camera math/state mutation here.

    // Standard camera controls.
    // HARD RULE: no camera integration / derived control computation here.
    // The viewport emits raw input events only; the substrate performs all
    // input mapping effects and camera integration.
    {
        static bool prev_keys[256] = {false};
        for (int k = 0; k < 256; ++k) {
            const bool now = input_.key_down[k];
            if (now != prev_keys[k]) {
                EwControlPacket cp{};
                cp.kind = EwControlPacketKind::InputAction;
                cp.source_u16 = 1;
                cp.tick_u64 = scene_->sm.canonical_tick;
                cp.payload.input_action.action_id_u32 = (uint32_t)k;
                cp.payload.input_action.pressed_u8 = now ? 1u : 0u;
                (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
                prev_keys[k] = now;
            }
        }

        // Mouse deltas as axis packets.
        if (input_.mouse_dx != 0) {
            EwControlPacket cp{};
            cp.kind = EwControlPacketKind::InputAxis;
            cp.source_u16 = 1;
            cp.tick_u64 = scene_->sm.canonical_tick;
            cp.payload.input_axis.axis_id_u32 = 0x1000u; // mouse_dx
            cp.payload.input_axis.value_q16_16 = (int32_t)(input_.mouse_dx << 16);
            (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
        }
        if (input_.mouse_dy != 0) {
            EwControlPacket cp{};
            cp.kind = EwControlPacketKind::InputAxis;
            cp.source_u16 = 1;
            cp.tick_u64 = scene_->sm.canonical_tick;
            cp.payload.input_axis.axis_id_u32 = 0x1001u; // mouse_dy
            cp.payload.input_axis.value_q16_16 = (int32_t)(input_.mouse_dy << 16);
            (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
        }
        if (input_.wheel_delta != 0) {
            // Hold ` (tilde) and scroll to shift viewport spectrum band; do not emit wheel axis.
            if (input_.key_down[VK_OEM_3]) {
                const int steps = (input_.wheel_delta > 0) ? 1 : -1;
                spectrum_band_i32_ += steps;
                if (spectrum_band_i32_ < -4) spectrum_band_i32_ = -4;
                if (spectrum_band_i32_ >  8) spectrum_band_i32_ =  8;
                spectrum_phase_f32_ += 0.173f * (float)steps;
                if (live_mode_enabled_) SyncLiveModeProjection();
            } else {
                EwControlPacket cp{};
                cp.kind = EwControlPacketKind::InputAxis;
                cp.source_u16 = 1;
                cp.tick_u64 = scene_->sm.canonical_tick;
                cp.payload.input_axis.axis_id_u32 = 0x1002u; // wheel
                cp.payload.input_axis.value_q16_16 = (int32_t)(input_.wheel_delta << 16);
                (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
            }
        }
        ew_input_reset_deltas(input_);
    }

    // Consume render camera packet derived from CameraAnchor state.
    // Renderer must not rebuild basis from local controller state.
    EwRenderCameraPacket rp{};
    bool have_rp = ew_runtime_get_render_camera_packet(&scene_->sm, &rp);
    if (have_rp) {
        ew_camera_apply_render_packet(cam_, rp);
    }

    // Assist coefficients for focus/LOD, derived inside substrate.
    EwRenderAssistPacket ra{};
    bool have_ra = ew_runtime_get_render_assist_packet(&scene_->sm, &ra);

    // Camera-space position from projected view matrix (Q16.16), used for culling/LOD.
    auto camera_space_pos = [&](float wx, float wy, float wz, float& out_cx, float& out_cy, float& out_cz) {
        if (!have_rp) { out_cx = wx; out_cy = wy; out_cz = wz; return; }
        const int64_t x_q = (int64_t)(wx * 65536.0f);
        const int64_t y_q = (int64_t)(wy * 65536.0f);
        const int64_t z_q = (int64_t)(wz * 65536.0f);
        auto row_dot = [&](int row, int64_t bx, int64_t by, int64_t bz) -> int64_t {
            const int64_t m0 = (int64_t)rp.view_mat_q16_16[row*4 + 0];
            const int64_t m1 = (int64_t)rp.view_mat_q16_16[row*4 + 1];
            const int64_t m2 = (int64_t)rp.view_mat_q16_16[row*4 + 2];
            const int64_t m3 = (int64_t)rp.view_mat_q16_16[row*4 + 3];
            return ((m0 * bx + m1 * by + m2 * bz) >> 16) + m3;
        };
        const int64_t cx_q = row_dot(0, x_q, y_q, z_q);
        const int64_t cy_q = row_dot(1, x_q, y_q, z_q);
        const int64_t cz_q = row_dot(2, x_q, y_q, z_q);
        out_cx = (float)cx_q / 65536.0f;
        out_cy = (float)cy_q / 65536.0f;
        out_cz = (float)cz_q / 65536.0f;
    };

    // Focus control: all autofocus logic lives in substrate. The viewport is
    // permitted to provide observations (depth histogram) but not to compute
    // focus distances or derived lens scalars.

    // Projection (camera-relative instances)
    scene_->instances.clear();
    scene_->instances.reserve(scene_->objects.size());

    auto ew_clamp_i32 = [](int64_t v, int32_t lo, int32_t hi) -> int32_t {
        if (v < (int64_t)lo) return lo;
        if (v > (int64_t)hi) return hi;
        return (int32_t)v;
    };

    // Compute emergent realism carrier triples (x=leak Q16.16, y=doppler_k Q16.16, z=harm_mean Q0.15)
    // for all visible object anchors in a single deterministic batch.
    std::vector<uint32_t> carrier_anchor_ids;
    carrier_anchor_ids.reserve(scene_->objects.size());
    for (const auto& o : scene_->objects) carrier_anchor_ids.push_back(o.anchor_id_u32);
    std::vector<EwCarrierTriple> carrier_triples;
    (void)ew_compute_carrier_triples_for_anchor_ids(scene_->sm.anchors, carrier_anchor_ids, carrier_triples);

    size_t idx_obj = 0;
    for (const auto& o : scene_->objects) {
        const float dx = o.xf.pos[0] - cam_.pos[0];
        const float dy = o.xf.pos[1] - cam_.pos[1];
        const float dz = o.xf.pos[2] - cam_.pos[2];
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        camera_space_pos(o.xf.pos[0], o.xf.pos[1], o.xf.pos[2], cx, cy, cz);
        // View-driven LOD: focus assist derived in substrate.
        // Renderer avoids sqrt/fabs; uses squared-distance domain with
        // multiply-add + clamp only.
        const float dist2_m2 = dx*dx + dy*dy + dz*dz;
        float clarity = 0.0f;
        float lod_bias = 0.0f;
        if (have_ra) {
            const float near2 = (float)ra.focus_near_m2_q32_32 / 4294967296.0f;
            const float far2  = (float)ra.focus_far_m2_q32_32 / 4294967296.0f;
            float focus_w = 0.0f;
            if (dist2_m2 <= near2) focus_w = 1.0f;
            else if (dist2_m2 >= far2) focus_w = 0.0f;
            else {
                const float inv = (float)ra.inv_focus_range_m2_q16_16 / 65536.0f;
                focus_w = 1.0f - (dist2_m2 - near2) * inv;
                if (focus_w < 0.0f) focus_w = 0.0f;
                if (focus_w > 1.0f) focus_w = 1.0f;
            }

            const float nmin2 = (float)ra.near_min_m2_q32_32 / 4294967296.0f;
            const float nmax2 = (float)ra.near_max_m2_q32_32 / 4294967296.0f;
            float near_w = 0.0f;
            if (dist2_m2 <= nmin2) near_w = 1.0f;
            else if (dist2_m2 >= nmax2) near_w = 0.0f;
            else {
                const float invn = (float)ra.inv_near_range_m2_q16_16 / 65536.0f;
                near_w = 1.0f - (dist2_m2 - nmin2) * invn;
                if (near_w < 0.0f) near_w = 0.0f;
                if (near_w > 1.0f) near_w = 1.0f;
            }

            // Screen proxy in squared-distance domain (no sqrt):
            // screen_w ~= clamp01((radius^2 / dist^2) * scale).
            float screen_w = 0.0f;
            if (dist2_m2 > 1.0e-6f) {
                const float sps = (float)ra.screen_proxy_scale_q16_16 / 65536.0f;
                const float r2 = o.radius_m_f32 * o.radius_m_f32;
                screen_w = (r2 / dist2_m2) * sps;
                if (screen_w < 0.0f) screen_w = 0.0f;
                if (screen_w > 1.0f) screen_w = 1.0f;
            }

            clarity = focus_w * near_w * screen_w;
            if (clarity < 0.0f) clarity = 0.0f;
            if (clarity > 1.0f) clarity = 1.0f;
            const float lodmax = (float)ra.lod_boost_max_q16_16 / 65536.0f;
            lod_bias = -lodmax * clarity;
        }

EwRenderInstance inst{};
inst.object_id_u64 = o.object_id_u64;
inst.anchor_id_u32 = o.anchor_id_u32;
inst.rel_pos_q16_16[0] = (int32_t)(cx * 65536.0f);
inst.rel_pos_q16_16[1] = (int32_t)(cy * 65536.0f);
inst.rel_pos_q16_16[2] = (int32_t)(cz * 65536.0f);
        inst.rel_pos_q16_16[3] = 0;

// Physical parameters
inst.radius_q16_16 = (int32_t)(o.radius_m_f32 * 65536.0f);
inst.emissive_q16_16 = (int32_t)(o.emissive_f32 * 65536.0f);
inst.atmosphere_thickness_q16_16 = (int32_t)(o.atmosphere_thickness_m_f32 * 65536.0f);
inst.lod_bias_q16_16 = (int32_t)(lod_bias * 65536.0f);
inst.clarity_q16_16 = (int32_t)(clarity * 65536.0f);
inst.albedo_rgba8 = o.albedo_rgba8;
inst.atmosphere_rgba8 = o.atmosphere_rgba8;

        // Emergent realism carrier triple derived from a deterministic bundle
        // of anchors: (self, self+1, self+2). This compacts multiple anchors'
        // computations into one carrier triple sent to the shader.
        if (idx_obj < carrier_triples.size()) {
            inst.carrier_x_u32 = carrier_triples[idx_obj].x_u32;
            inst.carrier_y_u32 = carrier_triples[idx_obj].y_u32;
            inst.carrier_z_u32 = carrier_triples[idx_obj].z_u32;
        }

if (o.name_utf8 == "Sun") inst.kind_u32 = 1;
else if (o.name_utf8 == "Earth") inst.kind_u32 = 2;
else inst.kind_u32 = 0;

inst.tick_u64 = scene_->sm.canonical_tick;
scene_->instances.push_back(inst);
        ++idx_obj;
    }

    // Lattice projection: if the substrate has a lattice tag enabled, the runtime
    // will project a deterministic point cloud. Render those points as small
    // emissive instances in camera space.
    {
        const uint32_t max_pts = 20000u;
        const std::vector<EwVizPoint> pts = ew_runtime_project_points(&scene_->sm, max_pts);
        // Heuristic: if anchor points are returned, there will be very few.
        // Lattice point clouds will usually be much larger.
        if (pts.size() > scene_->objects.size()) {
            const float viz_scale_m = 50.0f;
            for (const EwVizPoint& p : pts) {
                // Convert Q16.16 unit cube to meters.
                const float wx = ((float)p.x_q16_16 / 65536.0f) * viz_scale_m;
                const float wy = ((float)p.y_q16_16 / 65536.0f) * viz_scale_m;
                const float wz = ((float)p.z_q16_16 / 65536.0f) * viz_scale_m;

                float cx = 0.0f, cy = 0.0f, cz = 0.0f;
                camera_space_pos(wx, wy, wz, cx, cy, cz);

                EwRenderInstance inst{};
                inst.object_id_u64 = 0;
                inst.anchor_id_u32 = 0;
                inst.kind_u32 = 0;
                inst.rel_pos_q16_16[0] = (int32_t)(cx * 65536.0f);
                inst.rel_pos_q16_16[1] = (int32_t)(cy * 65536.0f);
                inst.rel_pos_q16_16[2] = (int32_t)(cz * 65536.0f);
                inst.rel_pos_q16_16[3] = 0;

                inst.radius_q16_16 = (int32_t)(0.25f * 65536.0f);
                inst.emissive_q16_16 = (int32_t)(2.0f * 65536.0f);
                inst.atmosphere_thickness_q16_16 = 0;
                inst.lod_bias_q16_16 = 0;
                inst.clarity_q16_16 = (int32_t)(1.0f * 65536.0f);
                inst.albedo_rgba8 = p.rgba8;
                inst.atmosphere_rgba8 = 0;
                inst.tick_u64 = scene_->sm.canonical_tick;
                scene_->instances.push_back(inst);
            }
        }
    }

    // UI output
    std::string line;
    int guard = 64;
    while (guard-- > 0 && scene_->PopUiLine(line)) {
        AppendOutputUtf8(line);
    }


    // Coherence highlight (derived-only) sync for Content Browser tinting.
    // Deterministic: only updates when the substrate revision changes.
    if (hwnd_content_list_ && scene_) {
        const uint64_t rev = scene_->sm.coh_highlight_revision_u64;
        if (rev != coh_highlight_seen_revision_u64_) {
            coh_highlight_seen_revision_u64_ = rev;
            coh_highlight_set_w_.clear();
            coh_highlight_set_w_.reserve(scene_->sm.coh_highlight_paths.size());
            for (const std::string& p : scene_->sm.coh_highlight_paths) {
                coh_highlight_set_w_.insert(utf8_to_wide(p));
            }
            InvalidateRect(hwnd_content_list_, nullptr, TRUE);
        }
    }

    ew_input_reset_deltas(input_);
}

void App::Render() {
    // Headless mode: end XR frame (if active) but skip swapchain / heavy rendering.
    if (!visualize_enabled_) {
        xr_.EndFrame();
        return;
    }

    if (!vk_ || !vk_->swap) return;

    bool xr_ended_this_frame = false;

    // Resize handling
    if (resized_) {
        resized_ = false;
        vkDeviceWaitIdle(vk_->dev);
        vk_->DestroySwap();
        RECT vrc{}; GetClientRect(hwnd_viewport_, &vrc);
        uint32_t w = (uint32_t)std::max(1L, vrc.right - vrc.left);
        uint32_t h = (uint32_t)std::max(1L, vrc.bottom - vrc.top);
        create_swap(*vk_, w, h);
        create_pipeline(*vk_);
    }

    vk_check(vkWaitForFences(vk_->dev, 1, &vk_->fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    vk_check(vkResetFences(vk_->dev, 1, &vk_->fence), "vkResetFences");

    // Read last frame's camera histogram median (written by compute) and
    // submit as an observation ingress packet to the substrate.
    if (scene_ && vk_->cam_out_mapped) {
        const uint32_t* out_u32 = (const uint32_t*)vk_->cam_out_mapped;
        const uint32_t median_q16 = out_u32[0];
        const uint32_t total = out_u32[1];
        if (total > 0u) {
            ew_runtime_submit_camera_sensor_median_norm(&scene_->sm, (int32_t)median_q16, scene_->sm.canonical_tick);
        }
    }

    // ------------------------------------------------------------
    // OpenXR stereo presentation (Vulkan path)
    // ------------------------------------------------------------
    // If XR session is running and swapchains are ready, render the same
    // instanced projection into the XR swapchain images and submit a
    // projection layer. The desktop swapchain render remains as a mirror.
    if (xr_.HasOpenXR()) {
        // Ensure stereo swapchains exist once the session is running.
        xr_.EnsureSwapchains((uint64_t)vk_->swap_format);
    }

    const uint32_t xr_views = xr_.ViewCount();
    if (xr_views > 0 && scene_) {
        // Track per-image layout validity deterministically.
        static std::vector<VkImage> xr_known_imgs;
        static std::vector<uint8_t> xr_known_valid;
        auto xr_layout_is_valid = [&](VkImage img) -> bool {
            for (size_t i = 0; i < xr_known_imgs.size(); ++i) if (xr_known_imgs[i] == img) return xr_known_valid[i] != 0;
            xr_known_imgs.push_back(img);
            xr_known_valid.push_back(0);
            return false;
        };
        auto xr_layout_set_valid = [&](VkImage img) {
            for (size_t i = 0; i < xr_known_imgs.size(); ++i) if (xr_known_imgs[i] == img) { xr_known_valid[i] = 1; return; }
            xr_known_imgs.push_back(img);
            xr_known_valid.push_back(1);
        };

        // Helper: build camera-relative instances for a given eye pose.
        auto build_instances_for_eye = [&](uint32_t eye_index_u32) {
            // The viewport must not normalize basis from raw eye poses.
            // It submits poses as observations; the substrate projects per-eye view matrices.
            EwRenderXrEyePacket xrep{};
            const bool have_eye = ew_runtime_get_render_xr_eye_packet(&scene_->sm, eye_index_u32, &xrep);

            auto row_dot_eye = [&](int row, int64_t bx, int64_t by, int64_t bz) -> int64_t {
                const int64_t m0 = (int64_t)xrep.view_mat_q16_16[row*4 + 0];
                const int64_t m1 = (int64_t)xrep.view_mat_q16_16[row*4 + 1];
                const int64_t m2 = (int64_t)xrep.view_mat_q16_16[row*4 + 2];
                const int64_t m3 = (int64_t)xrep.view_mat_q16_16[row*4 + 3];
                return ((m0 * bx + m1 * by + m2 * bz) >> 16) + m3;
            };

            scene_->instances.clear();
            scene_->instances.reserve(scene_->objects.size());

            auto ew_clamp_i32 = [](int64_t v, int32_t lo, int32_t hi) -> int32_t {
                if (v < (int64_t)lo) return lo;
                if (v > (int64_t)hi) return hi;
                return (int32_t)v;
            };
            auto ew_doppler_k_q16_16_from_turns = [&](int64_t doppler_turns_q) -> int32_t {
                const int64_t absd = (doppler_turns_q < 0) ? -doppler_turns_q : doppler_turns_q;
                const int64_t denom = (int64_t)TURN_SCALE + absd;
                if (denom <= 0) return 0;
                const int64_t num = (doppler_turns_q << 16);
                return ew_clamp_i32(num / denom, -(int32_t)65536, (int32_t)65536);
            };
            auto ew_leak_density_q16_16_from_mass = [&](int64_t mass_turns_q) -> int32_t {
                int64_t d = (int64_t)TURN_SCALE - mass_turns_q;
                if (d < 0) d = 0;
                const int64_t num = (d << 16);
                return ew_clamp_i32(num / (int64_t)TURN_SCALE, 0, (int32_t)65536);
            };
            for (const auto& o : scene_->objects) {
                int32_t rel_q16_16[3] = {0,0,0};

                if (have_eye) {
                    const int64_t wx_q = (int64_t)o.pos_q16_16[0];
                    const int64_t wy_q = (int64_t)o.pos_q16_16[1];
                    const int64_t wz_q = (int64_t)o.pos_q16_16[2];
                    const int64_t cx_q = row_dot_eye(0, wx_q, wy_q, wz_q);
                    const int64_t cy_q = row_dot_eye(1, wx_q, wy_q, wz_q);
                    const int64_t cz_q = row_dot_eye(2, wx_q, wy_q, wz_q);
                    rel_q16_16[0] = (int32_t)cx_q;
                    rel_q16_16[1] = (int32_t)cy_q;
                    rel_q16_16[2] = (int32_t)cz_q;
                } else {
                    // Fail-closed fallback: translation only.
                    rel_q16_16[0] = (int32_t)llround((double)dx * 65536.0);
                    rel_q16_16[1] = (int32_t)llround((double)dy * 65536.0);
                    rel_q16_16[2] = (int32_t)llround((double)dz * 65536.0);
                }

                const float cx = (float)rel_q16_16[0] / 65536.0f;
                const float cy = (float)rel_q16_16[1] / 65536.0f;
                const float cz = (float)rel_q16_16[2] / 65536.0f;

                // XR instance LOD/clarity MUST NOT be computed from local app camera state.
                // Use substrate-derived RenderAssistPacket coefficients and only simple
                // fixed-point multiply-add + clamps here (no sqrt/fabs focus-band math).
                // Distance is evaluated in squared-distance domain (m^2) to avoid sqrt.
                const double dist2_m2 = (double)dx*(double)dx + (double)dy*(double)dy + (double)dz*(double)dz;
                const uint64_t dist2_m2_q32_32 = (uint64_t)std::llround(dist2_m2 * (double)(1ull<<32));

                int32_t focus_w_q16_16 = 0;
                int32_t near_w_q16_16 = 0;
                int32_t screen_w_q16_16 = 0;
                int32_t clarity_q16_16 = 0;
                int32_t lod_bias_q16_16 = 0;
                if (have_ra) {
                    // Focus weight: triangular ramp in squared-distance domain.
                    const uint64_t near2 = ra.focus_near_m2_q32_32;
                    const uint64_t far2  = ra.focus_far_m2_q32_32;
                    if (far2 > near2 && dist2_m2_q32_32 > near2 && dist2_m2_q32_32 < far2) {
                        const uint64_t center2 = near2 + ((far2 - near2) >> 1);
                        const uint64_t absd2 = (dist2_m2_q32_32 > center2) ? (dist2_m2_q32_32 - center2) : (center2 - dist2_m2_q32_32);
                        const int64_t absd2_q16_16 = (int64_t)(absd2 >> 16);
                        const int64_t inv_half_q16_16 = (int64_t)ra.inv_focus_range_m2_q16_16 * 2; // 2/(far2-near2)
                        int64_t t_q16_16 = (absd2_q16_16 * inv_half_q16_16) >> 16;
                        if (t_q16_16 < 0) t_q16_16 = 0;
                        if (t_q16_16 > 65536) t_q16_16 = 65536;
                        int64_t fw = 65536 - t_q16_16;
                        if (fw < 0) fw = 0;
                        focus_w_q16_16 = (int32_t)fw;
                    }

                    // Near boost weight in squared-distance domain.
                    const uint64_t nmin2 = ra.near_min_m2_q32_32;
                    const uint64_t nmax2 = ra.near_max_m2_q32_32;
                    if (nmax2 > nmin2) {
                        if (dist2_m2_q32_32 <= nmin2) {
                            near_w_q16_16 = 65536;
                        } else if (dist2_m2_q32_32 < nmax2) {
                            const uint64_t rem = (nmax2 - dist2_m2_q32_32);
                            const int64_t rem_q16_16 = (int64_t)(rem >> 16);
                            int64_t nw = (rem_q16_16 * (int64_t)ra.inv_near_range_m2_q16_16) >> 16;
                            if (nw < 0) nw = 0;
                            if (nw > 65536) nw = 65536;
                            near_w_q16_16 = (int32_t)nw;
                        } else {
                            near_w_q16_16 = 0;
                        }
                    }

                    // Screen proxy weight: (radius/|cz|)*scale, evaluated in fixed-point.
                    const int32_t cz_q16_16 = (int32_t)std::llround(cz * 65536.0);
                    int32_t abs_cz_q16_16 = (cz_q16_16 < 0) ? -cz_q16_16 : cz_q16_16;
                    if (abs_cz_q16_16 < (int32_t)256) abs_cz_q16_16 = (int32_t)256; // avoid blow-up
                    const int32_t radius_q16_16 = (int32_t)std::llround(o.radius_m_f32 * 65536.0);
                    const int64_t ratio_q16_16 = ((int64_t)radius_q16_16 << 16) / (int64_t)abs_cz_q16_16;
                    int64_t sw = (ratio_q16_16 * (int64_t)ra.screen_proxy_scale_q16_16) >> 16;
                    if (sw < 0) sw = 0;
                    if (sw > 65536) sw = 65536;
                    screen_w_q16_16 = (int32_t)sw;

                    // clarity = focus * near * screen
                    int64_t c = ((int64_t)focus_w_q16_16 * (int64_t)near_w_q16_16) >> 16;
                    c = (c * (int64_t)screen_w_q16_16) >> 16;
                    if (c < 0) c = 0;
                    if (c > 65536) c = 65536;
                    clarity_q16_16 = (int32_t)c;

                    // lod_bias = -lod_boost_max * clarity
                    lod_bias_q16_16 = (int32_t)(-(((int64_t)ra.lod_boost_max_q16_16 * (int64_t)clarity_q16_16) >> 16));
                }

                EwRenderInstance inst{};
                inst.object_id_u64 = o.object_id_u64;
                inst.anchor_id_u32 = o.anchor_id_u32;
                if (o.name_utf8 == "Sun") inst.kind_u32 = 1u;
                else if (o.name_utf8 == "Earth") inst.kind_u32 = 2u;
                inst.albedo_rgba8 = o.albedo_rgba8;
                inst.atmosphere_rgba8 = o.atmosphere_rgba8;
                inst.rel_pos_q16_16[0] = rel_q16_16[0];
                inst.rel_pos_q16_16[1] = rel_q16_16[1];
                inst.rel_pos_q16_16[2] = rel_q16_16[2];
                inst.radius_q16_16 = (int32_t)std::llround(o.radius_m_f32 * 65536.0);
                inst.emissive_q16_16 = (int32_t)std::llround(o.emissive_f32 * 65536.0);
                inst.atmosphere_thickness_q16_16 = (int32_t)std::llround(o.atmosphere_thickness_m_f32 * 65536.0);
                inst.lod_bias_q16_16 = lod_bias_q16_16;
                inst.clarity_q16_16 = clarity_q16_16;

                if (inst.anchor_id_u32 < scene_->sm.anchors.size()) {
                    const uint32_t n = (uint32_t)scene_->sm.anchors.size();
                    const uint32_t a0 = inst.anchor_id_u32;
                    const uint32_t a1 = (n > 0u) ? ((a0 + 1u) % n) : a0;
                    const uint32_t a2 = (n > 0u) ? ((a0 + 2u) % n) : a0;
                    const Anchor& A0 = scene_->sm.anchors[a0];
                    const Anchor& A1 = scene_->sm.anchors[a1];
                    const Anchor& A2 = scene_->sm.anchors[a2];

                    const int32_t d0 = ew_doppler_k_q16_16_from_turns(A0.doppler_q);
                    const int32_t d1 = ew_doppler_k_q16_16_from_turns(A1.doppler_q);
                    const int32_t d2 = ew_doppler_k_q16_16_from_turns(A2.doppler_q);
                    const int64_t ds = (int64_t)d0 + (int64_t)d1 + (int64_t)d2;
                    const int32_t doppler_bundled = ew_clamp_i32(ds / 3, -(int32_t)65536, (int32_t)65536);

                    const int32_t l0 = ew_leak_density_q16_16_from_mass(A0.m_q);
                    const int32_t l1 = ew_leak_density_q16_16_from_mass(A1.m_q);
                    const int32_t l2 = ew_leak_density_q16_16_from_mass(A2.m_q);
                    const int64_t ls = (int64_t)l0 + (int64_t)l1 + (int64_t)l2;
                    const int32_t leak_bundled = ew_clamp_i32(ls / 3, 0, (int32_t)65536);

                    const uint32_t h0 = (uint32_t)A0.harmonics_mean_q15;
                    const uint32_t h1 = (uint32_t)A1.harmonics_mean_q15;
                    const uint32_t h2 = (uint32_t)A2.harmonics_mean_q15;
                    uint32_t hm = (h0 + h1 + h2) / 3u;
                    hm &= 65535u;

                    inst.carrier_x_u32 = (uint32_t)(uint32_t)leak_bundled;
                    inst.carrier_y_u32 = (uint32_t)(uint32_t)doppler_bundled;
                    inst.carrier_z_u32 = hm;
                }
                inst.tick_u64 = scene_->sm.canonical_tick;
                scene_->instances.push_back(inst);
            }
        };

        // Render stereo views into XR swapchains.
        for (uint32_t vi = 0; vi < xr_views; ++vi) {
            float pos[3]{}, quat[4]{}, fov4[4]{};
            if (!xr_.GetViewPoseFov(vi, pos, quat, fov4)) continue;
            VkImage xr_img = (VkImage)xr_.AcquiredImage(vi);
            VkImageView xr_view = (VkImageView)xr_.AcquiredImageView(vi);
            if (!xr_img || !xr_view) continue;

            // Build instances for this eye.
            // Submit XR pose as observation for substrate projection.
            ew_runtime_submit_xr_eye_pose_f32(&scene_->sm, vi, pos, quat, scene_->sm.canonical_tick);
            build_instances_for_eye(vi);

            // Upload instances.
            VkDeviceSize bytes = (VkDeviceSize)(scene_->instances.size() * sizeof(EwRenderInstance));
            if (ew_create_or_resize_instance_buffer(*vk_, bytes)) {
                if (bytes > 0 && vk_->instance_mapped) {
                    std::memcpy(vk_->instance_mapped, scene_->instances.data(), (size_t)bytes);
                }
                VkDescriptorBufferInfo dbi{};
                dbi.buffer = vk_->instance_buf;
                dbi.offset = 0;
                dbi.range = VK_WHOLE_SIZE;
                VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                wds.dstSet = vk_->ds;
                wds.dstBinding = 0;
                wds.descriptorCount = 1;
                wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                wds.pBufferInfo = &dbi;
                vkUpdateDescriptorSets(vk_->dev, 1, &wds, 0, nullptr);
            }

            // Record to an existing command buffer slot.
            const uint32_t cmd_slot = (vi < vk_->cmdbufs.size()) ? vi : 0u;
            VkCommandBuffer cmd = vk_->cmdbufs[cmd_slot];
            vk_check(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer(XR)");
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            vk_check(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer(XR)");

            const bool valid_layout = xr_layout_is_valid(xr_img);
            VkImageMemoryBarrier2 bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            bar.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            bar.srcAccessMask = 0;
            bar.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            bar.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            bar.oldLayout = valid_layout ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
            bar.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            bar.image = xr_img;
            bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bar.subresourceRange.levelCount = 1;
            bar.subresourceRange.layerCount = 1;
            VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &bar;
            vkCmdPipelineBarrier2(cmd, &dep);

            // Ensure depth is in attachment layout for this frame.
            if (vk_->depth_image) {
                VkImageMemoryBarrier2 dbar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                dbar.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                dbar.srcAccessMask = 0;
                dbar.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                dbar.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                dbar.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                dbar.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                dbar.image = vk_->depth_image;
                dbar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                dbar.subresourceRange.levelCount = 1;
                dbar.subresourceRange.layerCount = 1;
                VkDependencyInfo ddep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                ddep.imageMemoryBarrierCount = 1;
                ddep.pImageMemoryBarriers = &dbar;
                vkCmdPipelineBarrier2(cmd, &ddep);
            }

            VkClearValue clear{};
            clear.color.float32[0] = 0.0f;
            clear.color.float32[1] = 0.0f;
            clear.color.float32[2] = 0.0f;
            clear.color.float32[3] = 1.0f;

            VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            color.imageView = xr_view;
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.clearValue = clear;

            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
            ri.renderArea.offset = {0,0};
            ri.renderArea.extent = { xr_.ViewWidth(vi), xr_.ViewHeight(vi) };
            ri.layerCount = 1;
            ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &color;
            VkClearValue dclear{};
            dclear.depthStencil.depth = 1.0f;
            VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depth.imageView = vk_->depth_view;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth.clearValue = dclear;
            ri.pDepthAttachment = &depth;

            vkCmdBeginRendering(cmd, &ri);
            VkViewport vp{};
            vp.x = 0; vp.y = 0;
            vp.width = (float)ri.renderArea.extent.width;
            vp.height = (float)ri.renderArea.extent.height;
            vp.minDepth = 0.0f;
            vp.maxDepth = 1.0f;
            VkRect2D sc{};
            sc.offset = {0,0};
            sc.extent = ri.renderArea.extent;
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_->pipe);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_->pipe_layout, 0, 1, &vk_->ds, 0, nullptr);

            // Build Vulkan-style projection matrix from OpenXR fov.
            const float l = std::tan(fov4[0]);
            const float r = std::tan(fov4[1]);
            const float t = std::tan(fov4[2]);
            const float b = std::tan(fov4[3]);
            const float zn = 0.1f;
            const float zf = 1.0e13f;
            struct Push { float proj[16]; float sunPosCam[3]; float pointSize; float debug[4]; } push{};
            push.proj[0] = 2.0f/(r-l); push.proj[4] = 0;           push.proj[8]  = (r+l)/(r-l);   push.proj[12] = 0;
            push.proj[1] = 0;          push.proj[5] = 2.0f/(t-b);  push.proj[9]  = (t+b)/(t-b);   push.proj[13] = 0;
            push.proj[2] = 0;          push.proj[6] = 0;           push.proj[10] = (zf)/(zn-zf);  push.proj[14] = (zf*zn)/(zn-zf);
            push.proj[3] = 0;          push.proj[7] = 0;           push.proj[11] = -1.0f;         push.proj[15] = 0;

            push.sunPosCam[0] = 0.0f; push.sunPosCam[1] = 0.0f; push.sunPosCam[2] = 0.0f;
            for (const auto& inst : scene_->instances) {
                if (inst.kind_u32 == 1u) {
                    push.sunPosCam[0] = (float)inst.rel_pos_q16_16[0] / 65536.0f;
                    push.sunPosCam[1] = (float)inst.rel_pos_q16_16[1] / 65536.0f;
                    push.sunPosCam[2] = (float)inst.rel_pos_q16_16[2] / 65536.0f;
                    break;
                }
            }
            push.pointSize = 20000.0f;
            // Render debug controls (UI-only):
            //  - resonance_view_: render resonance carriers only (black background)
            //  - spectrum_band_i32_: scroll with `+wheel to shift spectrum band
            //  - spectrum_phase_f32_: user bias; plus deterministic tick-based phase for carrier motion
            push.debug[0] = resonance_view_ ? 1.0f : 0.0f;
            push.debug[1] = (float)spectrum_band_i32_;
            const float tick_ph = (float)((scene_->sm.canonical_tick & 1023ull)) * 0.03125f;
            push.debug[2] = spectrum_phase_f32_ + tick_ph;
            push.debug[3] = 0.0f;
            vkCmdPushConstants(cmd, vk_->pipe_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &push);

            uint32_t inst_n = (uint32_t)scene_->instances.size();
            if (inst_n > 0) vkCmdDraw(cmd, 6, inst_n, 0, 0);
            vkCmdEndRendering(cmd);
            vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer(XR)");

            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            vk_check(vkQueueSubmit(vk_->gfxq, 1, &si, vk_->fence), "vkQueueSubmit(XR)");
            vk_check(vkWaitForFences(vk_->dev, 1, &vk_->fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(XR)");
            vk_check(vkResetFences(vk_->dev, 1, &vk_->fence), "vkResetFences(XR)");
            xr_layout_set_valid(xr_img);
        }

        // XR submit + release swapchain images.
        xr_.EndFrame();
        xr_ended_this_frame = true;
    }

    uint32_t image_index = 0;
    VkResult acq = vkAcquireNextImageKHR(vk_->dev, vk_->swap, UINT64_MAX, vk_->sem_image, VK_NULL_HANDLE, &image_index);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) { resized_ = true; return; }
    vk_check(acq, "vkAcquireNextImageKHR");

    // Upload instances to SSBO and update descriptor set.
    if (scene_) {
        VkDeviceSize bytes = (VkDeviceSize)(scene_->instances.size() * sizeof(EwRenderInstance));
        if (ew_create_or_resize_instance_buffer(*vk_, bytes)) {
            if (bytes > 0 && vk_->instance_mapped) {
                std::memcpy(vk_->instance_mapped, scene_->instances.data(), (size_t)bytes);
            }
            VkDescriptorBufferInfo dbi{};
            dbi.buffer = vk_->instance_buf;
            dbi.offset = 0;
            dbi.range = VK_WHOLE_SIZE;
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = vk_->ds;
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w.pBufferInfo = &dbi;
            vkUpdateDescriptorSets(vk_->dev, 1, &w, 0, nullptr);
        }
    }

    // Virtual texturing: ensure atlas/page table exist and stream a small deterministic working set.
    if (!vk_->vt_atlas_image) {
        ew_vt_init(*vk_);
        // Pipeline needs the atlas/page-table bindings. Recreate if we were missing.
        create_pipeline(*vk_);
    }
    if (scene_ && vk_->vt_atlas_image) {
        bool pt_changed = false;
        const uint32_t tick_u32 = (uint32_t)(scene_->sm.canonical_tick & 0xFFFFFFFFu);
        // Production policy: materialize only a bounded number of near-focus tiles per frame.
        // This keeps bandwidth bounded and works with the global carrier LOD scalar (clarity).
        int budget = 64;
        for (const auto& inst : scene_->instances) {
            if (budget <= 0) break;
            float clarity = (float)inst.clarity_q16_16 / 65536.0f;
            if (clarity < 0.20f) continue;
            // Higher clarity -> lower mip index (more detail).
            uint32_t mip = 3;
            if (clarity > 0.85f) mip = 0;
            else if (clarity > 0.65f) mip = 1;
            else if (clarity > 0.35f) mip = 2;

            uint32_t tiles = vk_->vt_mip_tiles_per_row[mip];
            // Request center tile (impostor UV ~ 0.5). Also request a small neighborhood at high clarity.
            uint32_t tx = tiles / 2;
            uint32_t ty = tiles / 2;
            ew_vt_ensure_tile_resident(*vk_, tick_u32, mip, tx, ty, &pt_changed);
            budget--;
            if (clarity > 0.80f && budget > 2) {
                if (tx + 1 < tiles) { ew_vt_ensure_tile_resident(*vk_, tick_u32, mip, tx+1, ty, &pt_changed); budget--; }
                if (ty + 1 < tiles) { ew_vt_ensure_tile_resident(*vk_, tick_u32, mip, tx, ty+1, &pt_changed); budget--; }
            }
        }
        if (pt_changed) {
            ew_vt_upload_pagetable(*vk_);
        }
    }

    VkCommandBuffer cmd = vk_->cmdbufs[image_index];
    vk_check(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer");

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vk_check(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");

    // Transition to COLOR_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = 0;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.image = vk_->swap_images[image_index];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkImageMemoryBarrier2 barriers[2]{};
    barriers[0] = barrier;
    // Depth to attachment layout
    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barriers[1].image = vk_->depth_image;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.layerCount = 1;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 2;
    dep.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(cmd, &dep);

    // Dynamic rendering begin
    VkClearValue clear{};
    // Deterministic visual cue: normal mode uses a bounded ambient cycle; resonance mode collapses to black+gold.
    const uint64_t t = scene_ ? scene_->sm.canonical_tick : 0;
    float r = (float)((t % 256) / 255.0);
    float g = (float)(((t / 7) % 256) / 255.0);
    float b = (float)(((t / 19) % 256) / 255.0);
    if (resonance_view_) {
        const float ph = spectrum_phase_f32_ + (float)(t & 255ull) * 0.03125f + (float)spectrum_band_i32_ * 0.21f;
        const float pulse = 0.5f + 0.5f * sinf(ph);
        r = 0.06f + 0.18f * pulse;
        g = 0.035f + 0.12f * pulse;
        b = 0.0f;
    }
    clear.color.float32[0] = r;
    clear.color.float32[1] = g;
    clear.color.float32[2] = b;
    clear.color.float32[3] = 1.0f;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = vk_->swap_views[image_index];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = clear;

    VkClearValue dclear{};
    dclear.depthStencil.depth = 1.0f;
    dclear.depthStencil.stencil = 0;

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = vk_->depth_view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue = dclear;

    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.offset = {0,0};
    ri.renderArea.extent = vk_->swap_extent;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    ri.pDepthAttachment = &depth;

    vkCmdBeginRendering(cmd, &ri);
    // Draw instanced billboards for objects.
    VkViewport vp{};
    vp.x = 0; vp.y = 0;
    vp.width = (float)vk_->swap_extent.width;
    vp.height = (float)vk_->swap_extent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D sc{};
    sc.offset = {0,0};
    sc.extent = vk_->swap_extent;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_->pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_->pipe_layout, 0, 1, &vk_->ds, 0, nullptr);

    struct Push { float proj[16]; float sunPosCam[3]; float pointSize; float debug[4]; } push{};
    const float aspect = (vk_->swap_extent.height > 0) ? (float)vk_->swap_extent.width / (float)vk_->swap_extent.height : 1.0f;
    const float fov = 60.0f * 0.01745329251994329577f;
    const float f = 1.0f / std::tan(fov * 0.5f);
    const float zn = 0.1f;
    const float zf = 1.0e13f;
    // Column-major mat4
    push.proj[0] = f/aspect; push.proj[4] = 0; push.proj[8]  = 0;                         push.proj[12] = 0;
    push.proj[1] = 0;        push.proj[5] = f; push.proj[9]  = 0;                         push.proj[13] = 0;
    push.proj[2] = 0;        push.proj[6] = 0; push.proj[10] = (zf)/(zn-zf);               push.proj[14] = (zf*zn)/(zn-zf);
    push.proj[3] = 0;        push.proj[7] = 0; push.proj[11] = -1.0f;                      push.proj[15] = 0;
    
// Sun position in camera space for natural lighting (fragment shader).
push.sunPosCam[0] = 0.0f; push.sunPosCam[1] = 0.0f; push.sunPosCam[2] = 0.0f;
if (scene_) {
    for (const auto& inst : scene_->instances) {
        if (inst.kind_u32 == 1u) { // Sun
            push.sunPosCam[0] = (float)inst.rel_pos_q16_16[0] / 65536.0f;
            push.sunPosCam[1] = (float)inst.rel_pos_q16_16[1] / 65536.0f;
            push.sunPosCam[2] = (float)inst.rel_pos_q16_16[2] / 65536.0f;
            break;
        }
    }
}

push.pointSize = 20000.0f;

    // Desktop path must mirror the XR push-constant debug state so resonance view
    // actually reaches the shaders instead of becoming UI theater.
    push.debug[0] = resonance_view_ ? 1.0f : 0.0f;
    push.debug[1] = (float)spectrum_band_i32_;
    const float tick_ph = (float)(((scene_ ? scene_->sm.canonical_tick : 0ull) & 1023ull)) * 0.03125f;
    push.debug[2] = spectrum_phase_f32_ + tick_ph;
    int selected_strength_pct = 0;
    if (node_graph_selected_i32_ >= 0 && node_graph_selected_i32_ < (int)node_graph_strength_pct_i32_.size()) {
        selected_strength_pct = node_graph_strength_pct_i32_[(size_t)node_graph_selected_i32_];
    }
    push.debug[3] = std::clamp((float)selected_strength_pct / 100.0f, 0.0f, 1.0f);

    vkCmdPushConstants(cmd, vk_->pipe_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &push);
    uint32_t inst_n = scene_ ? (uint32_t)scene_->instances.size() : 0;
    if (inst_n > 0) {
        vkCmdDraw(cmd, 6, inst_n, 0, 0);
    }
    vkCmdEndRendering(cmd);

    // ------------------------------------------------------------
    // Camera sensor histogram + deterministic median (Vulkan compute)
    // Produces median normalized depth (Q16.16) into cam_out_buf.
    // ------------------------------------------------------------
    if (vk_->cam_hist_pipe && vk_->cam_median_pipe) {
        // Depth attachment -> shader read
        VkImageMemoryBarrier2 db{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        db.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        db.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        db.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        db.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        db.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        db.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        db.image = vk_->depth_image;
        db.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        db.subresourceRange.levelCount = 1;
        db.subresourceRange.layerCount = 1;
        VkDependencyInfo ddep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        ddep.imageMemoryBarrierCount = 1;
        ddep.pImageMemoryBarriers = &db;
        vkCmdPipelineBarrier2(cmd, &ddep);

        // Clear histogram buffer deterministically.
        vkCmdFillBuffer(cmd, vk_->cam_hist_buf, 0, sizeof(uint32_t) * 256, 0u);

        VkBufferMemoryBarrier2 bb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        bb.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        bb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        bb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        bb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        bb.buffer = vk_->cam_hist_buf;
        bb.offset = 0;
        bb.size = VK_WHOLE_SIZE;
        VkDependencyInfo bdep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        bdep.bufferMemoryBarrierCount = 1;
        bdep.pBufferMemoryBarriers = &bb;
        vkCmdPipelineBarrier2(cmd, &bdep);

        // Histogram pass
        struct PC { uint32_t w; uint32_t h; } pc{vk_->swap_extent.width, vk_->swap_extent.height};
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->cam_hist_pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->cam_pipe_layout, 0, 1, &vk_->cam_hist_ds, 0, nullptr);
        vkCmdPushConstants(cmd, vk_->cam_pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PC), &pc);
        uint32_t gx = (pc.w + 15u) / 16u;
        uint32_t gy = (pc.h + 15u) / 16u;
        vkCmdDispatch(cmd, gx, gy, 1);

        // Barrier for histogram writes
        VkBufferMemoryBarrier2 bb2{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        bb2.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        bb2.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        bb2.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        bb2.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        bb2.buffer = vk_->cam_hist_buf;
        bb2.offset = 0;
        bb2.size = VK_WHOLE_SIZE;
        VkDependencyInfo bdep2{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        bdep2.bufferMemoryBarrierCount = 1;
        bdep2.pBufferMemoryBarriers = &bb2;
        vkCmdPipelineBarrier2(cmd, &bdep2);

        // Median pass
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->cam_median_pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->cam_pipe_layout, 0, 1, &vk_->cam_median_ds, 0, nullptr);
        vkCmdDispatch(cmd, 1, 1, 1);

        // Barrier for out buffer visibility to host (next frame read after fence)
        VkBufferMemoryBarrier2 bo{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        bo.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        bo.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        bo.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        bo.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        bo.buffer = vk_->cam_out_buf;
        bo.offset = 0;
        bo.size = VK_WHOLE_SIZE;
        VkDependencyInfo odep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        odep.bufferMemoryBarrierCount = 1;
        odep.pBufferMemoryBarriers = &bo;
        vkCmdPipelineBarrier2(cmd, &odep);
    }

    // Transition to PRESENT
    VkImageMemoryBarrier2 barrier2{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier2.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier2.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier2.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier2.dstAccessMask = 0;
    barrier2.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier2.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier2.image = vk_->swap_images[image_index];
    barrier2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier2.subresourceRange.levelCount = 1;
    barrier2.subresourceRange.layerCount = 1;

    VkDependencyInfo dep2{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep2.imageMemoryBarrierCount = 1;
    dep2.pImageMemoryBarriers = &barrier2;
    vkCmdPipelineBarrier2(cmd, &dep2);

    vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &vk_->sem_image;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &vk_->sem_render;
    vk_check(vkQueueSubmit(vk_->gfxq, 1, &si, vk_->fence), "vkQueueSubmit");

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &vk_->sem_render;
    pi.swapchainCount = 1;
    pi.pSwapchains = &vk_->swap;
    pi.pImageIndices = &image_index;
    VkResult pres = vkQueuePresentKHR(vk_->gfxq, &pi);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) resized_ = true;
    else vk_check(pres, "vkQueuePresentKHR");

    // If OpenXR is active but swapchains weren't ready (or no stereo views yet),
    // we still must end the OpenXR frame to keep the runtime state machine healthy.
    if (xr_.HasOpenXR() && !xr_ended_this_frame) {
        xr_.EndFrame();
    }

    // OpenXR frame end is performed above.
}

void App::OnSend() {
    wchar_t wbuf[4096];
    GetWindowTextW(hwnd_input_, wbuf, 4096);
    std::string utf8 = wide_to_utf8(wbuf);
    if (!utf8.empty() && scene_) {
        // Conversational AI text stays on the dedicated chat path.
        // Editor/app feature control belongs to GUI widgets, not the chat box.
        scene_->SubmitAiChatLine(utf8);
        SetWindowTextW(hwnd_input_, L"");
    }
}

void App::OnImportObj() {
    wchar_t file[4096] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_main_;
    ofn.lpstrFilter = L"Wavefront OBJ\0*.obj\0All Files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 4096;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        std::string path = wide_to_utf8(file);
        // PBR/photogrammetry gate: AI model-training will only use marked assets.
        const int r = MessageBoxW(hwnd_main_, L"Is this model PBR/photogrammetry (photo-scanned materials)?\r\n\r\nChoose Yes for PBR/scan. Choose No for non-PBR or unknown.", L"Import Metadata", MB_ICONQUESTION | MB_YESNO);
        const bool pbr_scan = (r == IDYES);
        std::string material_meta_hint_utf8;
        if (pbr_scan) {
            const auto meta = EwAiTextInputDialog::RunModal(
                hwnd_main_,
                L"Training Metadata",
                L"Enter a bounded material/composition hint for substrate-resident AI training eligibility.\r\nExample: anodized aluminum, basalt rock, scanned leather",
                L""
            );
            if (meta.accepted) material_meta_hint_utf8 = wide_to_utf8(meta.result);
        }
        if (scene_ && scene_->ImportObj(path, pbr_scan, material_meta_hint_utf8)) {
            // update list (respect outliner filter)
            RebuildOutlinerList();
            RefreshContentBrowserFromRuntime(200u);
            RefreshAssetDesignerPanel();
            RefreshVoxelDesignerPanel();
        } else {
            MessageBoxA(hwnd_main_, "Failed to import OBJ.", "GenesisEngineVulkan", MB_ICONWARNING | MB_OK);
        }
    }
}

void App::RebuildOutlinerList() {
    if (!hwnd_objlist_ || !scene_) return;
    SendMessageW(hwnd_objlist_, LB_RESETCONTENT, 0, 0);
    const std::string f = outliner_filter_utf8_;
    auto contains_ci = [](const std::string& s, const std::string& sub)->bool {
        if (sub.empty()) return true;
        auto lower = [](unsigned char c)->unsigned char {
            if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A' + 'a');
            return c;
        };
        std::string ss = s;
        std::string bb = sub;
        for (char& c : ss) c = (char)lower((unsigned char)c);
        for (char& c : bb) c = (char)lower((unsigned char)c);
        return ss.find(bb) != std::string::npos;
    };

    int first_sel = -1;
    for (size_t i = 0; i < scene_->objects.size(); ++i) {
        const auto& o = scene_->objects[i];
        if (!contains_ci(o.name_utf8, f)) continue;
        int idx = (int)SendMessageW(hwnd_objlist_, LB_ADDSTRING, 0, (LPARAM)utf8_to_wide(o.name_utf8).c_str());
        if (idx >= 0) {
            SendMessageW(hwnd_objlist_, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)(int)i);
            if (first_sel < 0 && (int)i == scene_->selected) first_sel = idx;
        }
    }
    if (first_sel >= 0) SendMessageW(hwnd_objlist_, LB_SETCURSEL, (WPARAM)first_sel, 0);
}



// -----------------------------------------------------------------------------
// Details Property Grid (Unreal-style)
// UI-only: values are plumbed into existing hidden controls so core handlers stay unchanged.
// -----------------------------------------------------------------------------

static void lv_set_text(HWND lv, int item, int sub, const std::wstring& s) {
    ListView_SetItemText(lv, item, sub, (LPWSTR)s.c_str());
}

void App::RebuildPropertyGrid() {
    if (!hwnd_propgrid_) return;
    ListView_DeleteAllItems(hwnd_propgrid_);

    auto add = [&](int group_id, const wchar_t* name, const std::wstring& value, bool checkbox=false, bool checked=false) -> int {
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_GROUPID;
        it.iItem = ListView_GetItemCount(hwnd_propgrid_);
        it.iSubItem = 0;
        it.pszText = (LPWSTR)name;
        it.iGroupId = group_id;
        int idx = ListView_InsertItem(hwnd_propgrid_, &it);
        if (idx >= 0) {
            lv_set_text(hwnd_propgrid_, idx, 1, value);
            if (checkbox) ListView_SetCheckState(hwnd_propgrid_, idx, checked ? TRUE : FALSE);
        }
        return idx;
    };

    // Read from existing UI-backed fields/state.
    auto getw = [&](HWND h)->std::wstring {
        wchar_t b[256]; b[0]=0; GetWindowTextW(h, b, 256); return std::wstring(b);
    };

    const std::wstring px = hwnd_posx_ ? getw(hwnd_posx_) : L"0";
    const std::wstring py = hwnd_posy_ ? getw(hwnd_posy_) : L"0";
    const std::wstring pz = hwnd_posz_ ? getw(hwnd_posz_) : L"0";

    const std::wstring grid = hwnd_grid_step_ ? getw(hwnd_grid_step_) : L"1.0";
    const std::wstring ang  = hwnd_angle_step_ ? getw(hwnd_angle_step_) : L"15";

    // Transform
    add(1, L"Position X", px);
    add(1, L"Position Y", py);
    add(1, L"Position Z", pz);

    // Snapping
    add(2, L"Snap Enabled", editor_snap_enabled_u8_ ? L"On" : L"Off", true, editor_snap_enabled_u8_ != 0);
    add(2, L"Grid Step", grid);
    add(2, L"Angle Step", ang);

    // Gizmo (read-only)
    add(3, L"Gizmo Mode", (editor_gizmo_mode_u8_==2)?L"Rotate":L"Translate");
    const wchar_t* ax = L"None";
    if (editor_axis_constraint_u8_==1) ax=L"X";
    else if (editor_axis_constraint_u8_==2) ax=L"Y";
    else if (editor_axis_constraint_u8_==3) ax=L"Z";
    add(3, L"Axis Constraint", ax);

    EwEditorAnchorState editor_state{};
    const bool has_editor_state = (scene_ != nullptr) && scene_->sm.ui_snapshot_editor_state(editor_state);
    auto history_value = [](uint32_t count_u32, uint32_t capacity_u32, const EwEditorTransformTxn* stack)->std::wstring {
        std::wstring out = std::to_wstring((unsigned long long)count_u32);
        out += L" / ";
        out += std::to_wstring((unsigned long long)capacity_u32);
        if (count_u32 > 0u && stack != nullptr) {
            out += L" | obj ";
            out += std::to_wstring((unsigned long long)stack[count_u32 - 1u].object_id_u64);
        }
        return out;
    };

    const std::wstring undo_value = has_editor_state
        ? history_value(editor_state.undo_count_u32, EW_EDITOR_UNDO_DEPTH, editor_state.undo_stack)
        : (std::wstring(L"0 / ") + std::to_wstring((unsigned long long)EW_EDITOR_UNDO_DEPTH));
    const std::wstring redo_value = has_editor_state
        ? history_value(editor_state.redo_count_u32, EW_EDITOR_UNDO_DEPTH, editor_state.redo_stack)
        : (std::wstring(L"0 / ") + std::to_wstring((unsigned long long)EW_EDITOR_UNDO_DEPTH));

    add(4, L"Undo Stack", undo_value);
    add(4, L"Redo Stack", redo_value);

    RefreshEditorHistoryUi();
}

void App::RefreshEditorHistoryUi() {
    EwEditorAnchorState editor_state{};
    const bool has_editor_state = (scene_ != nullptr) && scene_->sm.ui_snapshot_editor_state(editor_state);
    const BOOL can_undo = (has_editor_state && editor_state.undo_count_u32 > 0u) ? TRUE : FALSE;
    const BOOL can_redo = (has_editor_state && editor_state.redo_count_u32 > 0u) ? TRUE : FALSE;
    if (hwnd_undo_) EnableWindow(hwnd_undo_, can_undo);
    if (hwnd_redo_) EnableWindow(hwnd_redo_, can_redo);
}

void App::BeginPropEdit(int item, int subitem) {
    if (!hwnd_propgrid_ || subitem != 1) return;

    // Do not edit read-only rows.
    wchar_t name[128]; name[0]=0;
    ListView_GetItemText(hwnd_propgrid_, item, 0, name, 128);
    const std::wstring n(name);
    if (n==L"Snap Enabled" || n==L"Gizmo Mode" || n==L"Axis Constraint" || n==L"Undo Stack" || n==L"Redo Stack") return;

    RECT rc{};
    ListView_GetSubItemRect(hwnd_propgrid_, item, subitem, LVIR_BOUNDS, &rc);

    if (!hwnd_propedit_) {
        hwnd_propedit_ = CreateWindowW(L"EDIT", L"",
                                       WS_CHILD | WS_BORDER | ES_LEFT,
                                       0, 0, 10, 10,
                                       hwnd_rdock_details_, (HMENU)2601, GetModuleHandleW(nullptr), nullptr);
        if (g_font_ui) SendMessageW(hwnd_propedit_, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    }

    wchar_t val[256]; val[0]=0;
    ListView_GetItemText(hwnd_propgrid_, item, 1, val, 256);
    SetWindowTextW(hwnd_propedit_, val);

    // Position edit over the value cell.
    MoveWindow(hwnd_propedit_, rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, TRUE);
    ShowWindow(hwnd_propedit_, SW_SHOW);
    SetFocus(hwnd_propedit_);
    SendMessageW(hwnd_propedit_, EM_SETSEL, 0, -1);

    propedit_item_ = item;
    propedit_subitem_ = subitem;
    propedit_active_ = true;
}

void App::CommitPropEdit(bool apply) {
    if (!propedit_active_ || !hwnd_propedit_ || propedit_item_ < 0) return;

    wchar_t name[128]; name[0]=0;
    ListView_GetItemText(hwnd_propgrid_, propedit_item_, 0, name, 128);
    std::wstring n(name);

    wchar_t val[256]; val[0]=0;
    GetWindowTextW(hwnd_propedit_, val, 256);
    std::wstring v(val);

    if (apply) {
        // Plumb back into existing backing fields so all existing handlers remain valid.
        if (n==L"Position X" && hwnd_posx_) SetWindowTextW(hwnd_posx_, v.c_str());
        else if (n==L"Position Y" && hwnd_posy_) SetWindowTextW(hwnd_posy_, v.c_str());
        else if (n==L"Position Z" && hwnd_posz_) SetWindowTextW(hwnd_posz_, v.c_str());
        else if (n==L"Grid Step" && hwnd_grid_step_) SetWindowTextW(hwnd_grid_step_, v.c_str());
        else if (n==L"Angle Step" && hwnd_angle_step_) SetWindowTextW(hwnd_angle_step_, v.c_str());

        // Update displayed value.
        lv_set_text(hwnd_propgrid_, propedit_item_, 1, v);
    }

    ShowWindow(hwnd_propedit_, SW_HIDE);
    propedit_active_ = false;
    propedit_item_ = -1;
    propedit_subitem_ = -1;
}

bool App::CopyDetailsBlockToClipboard() {
    if (!hwnd_propgrid_) return false;
    std::vector<int> rows;
    for (int row = ListView_GetNextItem(hwnd_propgrid_, -1, LVNI_SELECTED);
         row >= 0;
         row = ListView_GetNextItem(hwnd_propgrid_, row, LVNI_SELECTED)) {
        rows.push_back(row);
    }
    if (rows.empty()) {
        const int count = ListView_GetItemCount(hwnd_propgrid_);
        for (int i = 0; i < count; ++i) rows.push_back(i);
    }

    std::wstring out = L"GE_DETAILS_BLOCK_V1\r\n";
    bool any = false;
    for (int row : rows) {
        wchar_t name[128]{};
        wchar_t value[256]{};
        ListView_GetItemText(hwnd_propgrid_, row, 0, name, 128);
        ListView_GetItemText(hwnd_propgrid_, row, 1, value, 256);
        if (name[0] == 0) continue;
        out += name;
        out += L"=";
        out += value;
        out += L"\r\n";
        any = true;
    }
    if (!any) return false;
    (void)ew_clip_set_text_utf16(hwnd_main_, out);
    AppendOutputUtf8("EDITOR: copied details block");
    return true;
}

bool App::PasteDetailsBlockFromClipboard() {
    const std::wstring txt = ew_clip_get_text_utf16(hwnd_main_);
    if (txt.empty()) return false;

    std::wstring pos_x, pos_y, pos_z, grid_step, angle_step;
    bool has_pos_x = false, has_pos_y = false, has_pos_z = false;
    bool has_grid = false, has_angle = false, has_snap = false;
    uint8_t snap_value = editor_snap_enabled_u8_;

    auto trim = [](const std::wstring& in) -> std::wstring {
        size_t a = 0, b = in.size();
        while (a < b && iswspace(in[a])) ++a;
        while (b > a && iswspace(in[b - 1])) --b;
        return in.substr(a, b - a);
    };
    auto lower = [](std::wstring v) -> std::wstring {
        for (wchar_t& c : v) c = (wchar_t)towlower(c);
        return v;
    };
    auto parse_bool = [&](const std::wstring& v, uint8_t& out)->bool {
        const std::wstring t = lower(trim(v));
        if (t == L"on" || t == L"true" || t == L"1" || t == L"yes") { out = 1; return true; }
        if (t == L"off" || t == L"false" || t == L"0" || t == L"no") { out = 0; return true; }
        return false;
    };

    std::wstringstream ss(txt);
    std::wstring line;
    bool recognized = false;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty() || line == L"GE_DETAILS_BLOCK_V1") continue;
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring key = trim(line.substr(0, eq));
        const std::wstring val = trim(line.substr(eq + 1));
        if (key == L"Position X") { pos_x = val; has_pos_x = true; recognized = true; }
        else if (key == L"Position Y") { pos_y = val; has_pos_y = true; recognized = true; }
        else if (key == L"Position Z") { pos_z = val; has_pos_z = true; recognized = true; }
        else if (key == L"Grid Step") { grid_step = val; has_grid = true; recognized = true; }
        else if (key == L"Angle Step") { angle_step = val; has_angle = true; recognized = true; }
        else if (key == L"Snap Enabled") { if (parse_bool(val, snap_value)) { has_snap = true; recognized = true; } }
    }
    if (!recognized) return false;

    if (has_pos_x && hwnd_posx_) SetWindowTextW(hwnd_posx_, pos_x.c_str());
    if (has_pos_y && hwnd_posy_) SetWindowTextW(hwnd_posy_, pos_y.c_str());
    if (has_pos_z && hwnd_posz_) SetWindowTextW(hwnd_posz_, pos_z.c_str());
    if (has_grid && hwnd_grid_step_) SetWindowTextW(hwnd_grid_step_, grid_step.c_str());
    if (has_angle && hwnd_angle_step_) SetWindowTextW(hwnd_angle_step_, angle_step.c_str());
    if (has_snap) {
        editor_snap_enabled_u8_ = snap_value;
        EmitEditorSnap();
    }
    if (has_grid || has_angle) {
        auto read_f32 = [&](HWND h)->float {
            wchar_t buf[128]{}; GetWindowTextW(h, buf, 128);
            wchar_t* endp = nullptr;
            double v = wcstod(buf, &endp);
            if (!endp) v = 0.0;
            return (float)v;
        };
        float grid = read_f32(hwnd_grid_step_);
        float ang = read_f32(hwnd_angle_step_);
        if (grid < 0.0001f) grid = 0.0001f;
        if (ang < 0.1f) ang = 0.1f;
        editor_grid_step_m_q16_16_ = (int32_t)llround((double)grid * 65536.0);
        editor_angle_step_deg_q16_16_ = (int32_t)llround((double)ang * 65536.0);
        EmitEditorSnap();
    }
    if (has_pos_x || has_pos_y || has_pos_z) OnApplyTransform();
    RebuildPropertyGrid();
    AppendOutputUtf8("EDITOR: pasted details block");
    return true;
}

bool App::CopyFocusedSurfaceToClipboard() {
    HWND focus = GetFocus();
    if (focus && (focus == hwnd_propgrid_ || focus == hwnd_propedit_ || IsChild(hwnd_rdock_details_, focus))) {
        return CopyDetailsBlockToClipboard();
    }
    auto copy_text_window = [&](HWND h)->bool {
        if (!h) return false;
        const int len = GetWindowTextLengthW(h);
        if (len <= 0) return false;
        std::vector<wchar_t> buf((size_t)len + 1u, 0);
        GetWindowTextW(h, buf.data(), len + 1);
        return ew_clip_set_text_utf16(hwnd_main_, std::wstring(buf.data()));
    };
    if (focus && (focus == hwnd_content_list_ || focus == hwnd_content_thumb_ || IsChild(hwnd_content_, focus))) {
        if (!content_selected_rel_utf8_.empty()) {
            (void)ew_clip_set_text_utf16(hwnd_main_, utf8_to_wide(content_selected_rel_utf8_));
            return true;
        }
    }
    if (focus && (focus == hwnd_ai_repo_list_ || focus == hwnd_ai_repo_preview_ || IsChild(hwnd_ai_view_repo_, focus))) {
        if (!ai_repo_selected_rel_utf8_.empty()) {
            (void)ew_clip_set_text_utf16(hwnd_main_, utf8_to_wide(ai_repo_selected_rel_utf8_));
            return true;
        }
        return copy_text_window(hwnd_ai_repo_preview_);
    }
    if (focus && (focus == hwnd_ai_coh_results_ || focus == hwnd_ai_coh_patch_ || IsChild(hwnd_ai_view_coherence_, focus))) {
        if (ai_coh_selected_index_i32_ >= 0 && (size_t)ai_coh_selected_index_i32_ < ai_coh_result_paths_utf8_.size() && !ai_coh_result_paths_utf8_[(size_t)ai_coh_selected_index_i32_].empty()) {
            (void)ew_clip_set_text_utf16(hwnd_main_, utf8_to_wide(ai_coh_result_paths_utf8_[(size_t)ai_coh_selected_index_i32_]));
            return true;
        }
        if (copy_text_window(hwnd_ai_coh_patch_)) return true;
        return copy_text_window(hwnd_ai_coh_results_);
    }
    if (focus) {
        wchar_t cls[32]{};
        GetClassNameW(focus, cls, 32);
        if (wcscmp(cls, L"Edit") == 0 || wcscmp(cls, L"RichEdit50W") == 0) {
            SendMessageW(focus, WM_COPY, 0, 0);
            return true;
        }
    }
    return false;
}

bool App::PasteFocusedSurfaceFromClipboard() {
    HWND focus = GetFocus();
    if (focus && (focus == hwnd_propgrid_ || focus == hwnd_propedit_ || IsChild(hwnd_rdock_details_, focus))) {
        return PasteDetailsBlockFromClipboard();
    }

    const std::wstring clip_w = ew_clip_get_text_utf16(hwnd_main_);
    auto trim_ws = [](const std::wstring& in)->std::wstring {
        size_t a = 0, b = in.size();
        while (a < b && iswspace(in[a])) ++a;
        while (b > a && iswspace(in[b - 1])) --b;
        return in.substr(a, b - a);
    };
    const std::wstring trimmed_w = trim_ws(clip_w);
    const std::string clip_utf8 = wide_to_utf8(trimmed_w);

    auto select_content_rel = [&](const std::string& rel_utf8)->bool {
        if (rel_utf8.empty()) return false;
        HWND target = (focus == hwnd_content_thumb_ || IsChild(hwnd_content_thumb_, focus)) ? hwnd_content_thumb_ : hwnd_content_list_;
        if (!target) target = hwnd_content_list_ ? hwnd_content_list_ : hwnd_content_thumb_;
        if (!target) return false;
        const int n = ListView_GetItemCount(target);
        for (int i = 0; i < n; ++i) {
            wchar_t wrel[512]{};
            ListView_GetItemText(target, i, 0, wrel, 512);
            if (wide_to_utf8(std::wstring(wrel)) != rel_utf8) continue;
            content_selection_sync_guard_ = true;
            if (hwnd_content_list_) {
                const int m = ListView_GetItemCount(hwnd_content_list_);
                for (int j = 0; j < m; ++j) ListView_SetItemState(hwnd_content_list_, j, 0, LVIS_SELECTED | LVIS_FOCUSED);
            }
            if (hwnd_content_thumb_) {
                const int m = ListView_GetItemCount(hwnd_content_thumb_);
                for (int j = 0; j < m; ++j) ListView_SetItemState(hwnd_content_thumb_, j, 0, LVIS_SELECTED | LVIS_FOCUSED);
            }
            if (hwnd_content_list_) {
                const int m = ListView_GetItemCount(hwnd_content_list_);
                for (int j = 0; j < m; ++j) {
                    wchar_t w2[512]{};
                    ListView_GetItemText(hwnd_content_list_, j, 0, w2, 512);
                    if (wide_to_utf8(std::wstring(w2)) == rel_utf8) {
                        ListView_SetItemState(hwnd_content_list_, j, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                        ListView_EnsureVisible(hwnd_content_list_, j, FALSE);
                        break;
                    }
                }
            }
            if (hwnd_content_thumb_) {
                const int m = ListView_GetItemCount(hwnd_content_thumb_);
                for (int j = 0; j < m; ++j) {
                    wchar_t w2[512]{};
                    ListView_GetItemText(hwnd_content_thumb_, j, 0, w2, 512);
                    if (wide_to_utf8(std::wstring(w2)) == rel_utf8) {
                        ListView_SetItemState(hwnd_content_thumb_, j, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                        ListView_EnsureVisible(hwnd_content_thumb_, j, FALSE);
                        break;
                    }
                }
            }
            content_selection_sync_guard_ = false;
            content_selected_rel_utf8_ = rel_utf8;
            AppendOutputUtf8("CONTENT: selected from clipboard path");
            return true;
        }
        return false;
    };

    auto select_repo_rel = [&](const std::string& rel_utf8)->bool {
        if (rel_utf8.empty() || !hwnd_ai_repo_list_) return false;
        for (size_t i = 0; i < ai_repo_rel_paths_utf8_.size(); ++i) {
            if (ai_repo_rel_paths_utf8_[i] != rel_utf8) continue;
            SendMessageW(hwnd_ai_repo_list_, LB_SETCURSEL, (WPARAM)i, 0);
            SetAiPanelView(1u);
            UpdateAiRepoSelection();
            AppendOutputUtf8("REPO: selected from clipboard path");
            return true;
        }
        return false;
    };

    auto select_coherence_rel = [&](const std::string& rel_utf8)->bool {
        if (rel_utf8.empty() || !hwnd_ai_coh_results_) return false;
        for (size_t i = 0; i < ai_coh_result_paths_utf8_.size(); ++i) {
            if (ai_coh_result_paths_utf8_[i] != rel_utf8) continue;
            SendMessageW(hwnd_ai_coh_results_, LB_SETCURSEL, (WPARAM)i, 0);
            SetAiPanelView(2u);
            UpdateAiCoherenceSelection();
            if (scene_) scene_->SetCoherenceHighlightPath(rel_utf8);
            AppendOutputUtf8("COH: selected from clipboard path");
            return true;
        }
        return false;
    };

    if (focus && (focus == hwnd_content_list_ || focus == hwnd_content_thumb_ || IsChild(hwnd_content_, focus))) {
        if (select_content_rel(clip_utf8)) return true;
    }
    if (focus && (focus == hwnd_ai_repo_list_ || focus == hwnd_ai_repo_preview_ || IsChild(hwnd_ai_view_repo_, focus))) {
        if (select_repo_rel(clip_utf8)) return true;
    }
    if (focus && (focus == hwnd_ai_coh_results_ || focus == hwnd_ai_coh_patch_ || IsChild(hwnd_ai_view_coherence_, focus))) {
        if (select_coherence_rel(clip_utf8)) return true;
    }

    if (focus) {
        wchar_t cls[32]{};
        GetClassNameW(focus, cls, 32);
        if (wcscmp(cls, L"Edit") == 0 || wcscmp(cls, L"RichEdit50W") == 0) {
            SendMessageW(focus, WM_PASTE, 0, 0);
            return true;
        }
    }
    return false;
}

void App::OnBootstrapGame() {
    if (!scene_) return;
    scene_->RequestGameBootstrap("editor_bootstrap");
    AppendOutputUtf8("EDITOR: GAMEBOOT requested");
}






void App::OnAiTrainModel() {
    if (!scene_ || scene_->selected < 0 || scene_->selected >= (int)scene_->objects.size()) {
        AppendOutputUtf8("AI_MODEL_TRAIN: no selection");
        return;
    }
    const auto& o = scene_->objects[(size_t)scene_->selected];
    if (o.pbr_scan_u8 == 0) {
        AppendOutputUtf8("AI_MODEL_TRAIN: blocked (asset not marked PBR/photogrammetry)");
        AppendOutputUtf8("AI_MODEL_TRAIN: hint: re-import and choose Yes for PBR/scan assets");
        return;
    }

    // Composition/DNA/material lookup gate:
    // Per design, AI learning artifacts are substrate-resident and must NOT be projected
    // to disk unless explicitly requested by the user. Training eligibility therefore
    // requires explicit bounded metadata provided through the GUI/import flow, not a
    // filename guess or sidecar-on-disk superstition.
    if (o.ai_training_meta_ready_u8 == 0u || o.material_meta_hint_utf8.empty()) {
        AppendOutputUtf8("AI_MODEL_TRAIN: blocked (missing bounded substrate metadata)");
        AppendOutputUtf8("AI_MODEL_TRAIN: hint: re-import the asset as PBR/scan and provide a material/composition hint when prompted");
        RefreshAssetDesignerPanel();
        return;
    }
    const std::string base = ew_obj_basename_utf8(o.name_utf8);

    // Run voxel synthesis pass (deterministic, bounded). This is the model-training substrate target.
    if (!scene_->genesis_synthesize_object_voxel_volume_occupancy_u8(o.object_id_u64)) {
        AppendOutputUtf8("AI_MODEL_TRAIN: voxel synthesis failed");
        return;
    }
    AppendOutputUtf8("AI_MODEL_TRAIN: voxel synthesis OK (eligible)");
    RefreshAssetDesignerPanel();
    RefreshVoxelDesignerPanel();

    // Future hook: the AI loop will ingest substrate-resident metadata + voxel stats and
    // produce experiment tasks. The app emits a direct runtime event instead of routing
    // through a command-style text surface.
    scene_->EmitAiModelTrainReady(base);
}
void App::OnApplyTransform() {
    if (!scene_ || scene_->selected < 0 || scene_->selected >= (int)scene_->objects.size()) return;
    const auto& obj = scene_->objects[(size_t)scene_->selected];

    auto read_f32 = [&](HWND h)->float {
        wchar_t buf[128];
        buf[0] = 0;
        GetWindowTextW(h, buf, 128);
        wchar_t* endp = nullptr;
        double v = wcstod(buf, &endp);
        if (!endp) v = 0.0;
        return (float)v;
    };

    const float x = read_f32(hwnd_posx_);
    const float y = read_f32(hwnd_posy_);
    const float z = read_f32(hwnd_posz_);

    EwControlPacket cp{};
    cp.kind = EwControlPacketKind::ObjectSetTransform;
    cp.source_u16 = 1;
    cp.tick_u64 = scene_->sm.canonical_tick;
    cp.payload.object_set_transform.object_id_u64 = obj.object_id_u64;
    cp.payload.object_set_transform.pad0_u32 = 0u;
    cp.payload.object_set_transform.pad1_u32 = 0u;
    cp.payload.object_set_transform.pos_q16_16[0] = (int32_t)llround((double)x * 65536.0);
    cp.payload.object_set_transform.pos_q16_16[1] = (int32_t)llround((double)y * 65536.0);
    cp.payload.object_set_transform.pos_q16_16[2] = (int32_t)llround((double)z * 65536.0);
    for (int i = 0; i < 4; ++i) cp.payload.object_set_transform.rot_quat_q16_16[i] = obj.rot_quat_q16_16[i];

    (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
    AppendOutputUtf8("EDITOR: ObjectSetTransform emitted");
    RebuildPropertyGrid();
}

static void ew_quat_from_lookat(float out_q[4], const float pos[3], const float target[3]) {
    // Right-handed, Z-up. Forward points from camera to target.
    float f[3] = {target[0]-pos[0], target[1]-pos[1], target[2]-pos[2]};
    const float fl = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if (fl > 1e-6f) { f[0]/=fl; f[1]/=fl; f[2]/=fl; }
    float up[3] = {0.0f, 0.0f, 1.0f};
    // Right = f x up
    float r[3] = { f[1]*up[2]-f[2]*up[1], f[2]*up[0]-f[0]*up[2], f[0]*up[1]-f[1]*up[0] };
    float rl = sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
    if (rl < 1e-6f) { r[0]=1; r[1]=0; r[2]=0; rl=1; }
    r[0]/=rl; r[1]/=rl; r[2]/=rl;
    // Recompute up = r x f
    float u[3] = { r[1]*f[2]-r[2]*f[1], r[2]*f[0]-r[0]*f[2], r[0]*f[1]-r[1]*f[0] };

    // Build rotation matrix with columns (r, u, f)
    const float m00 = r[0], m01 = u[0], m02 = f[0];
    const float m10 = r[1], m11 = u[1], m12 = f[1];
    const float m20 = r[2], m21 = u[2], m22 = f[2];

    const float tr = m00 + m11 + m22;
    float qx=0,qy=0,qz=0,qw=1;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        qw = 0.25f * s;
        qx = (m21 - m12) / s;
        qy = (m02 - m20) / s;
        qz = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        qw = (m21 - m12) / s;
        qx = 0.25f * s;
        qy = (m01 + m10) / s;
        qz = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        qw = (m02 - m20) / s;
        qx = (m01 + m10) / s;
        qy = 0.25f * s;
        qz = (m12 + m21) / s;
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        qw = (m10 - m01) / s;
        qx = (m02 + m20) / s;
        qy = (m12 + m21) / s;
        qz = 0.25f * s;
    }
    // Normalize
    const float nl = sqrtf(qx*qx + qy*qy + qz*qz + qw*qw);
    if (nl > 1e-6f) { qx/=nl; qy/=nl; qz/=nl; qw/=nl; }
    out_q[0]=qx; out_q[1]=qy; out_q[2]=qz; out_q[3]=qw;
}

void App::ResetEditorSelectionLocal() {
    editor_selected_object_id_u64_ = 0u;
    editor_selection_count_u32_ = 0u;
    for (uint32_t i = 0u; i < 16u; ++i) editor_selection_object_id_u64_[i] = 0u;
}

void App::SetEditorSelectionLocal(uint64_t object_id_u64) {
    ResetEditorSelectionLocal();
    if (object_id_u64 == 0u) return;
    editor_selected_object_id_u64_ = object_id_u64;
    editor_selection_object_id_u64_[0] = object_id_u64;
    editor_selection_count_u32_ = 1u;
}

void App::ToggleEditorSelectionLocal(uint64_t object_id_u64) {
    if (object_id_u64 == 0u) return;
    uint32_t at = editor_selection_count_u32_;
    for (uint32_t i = 0u; i < editor_selection_count_u32_; ++i) {
        if (editor_selection_object_id_u64_[i] == object_id_u64) { at = i; break; }
    }
    if (at < editor_selection_count_u32_) {
        for (uint32_t i = at + 1u; i < editor_selection_count_u32_; ++i) {
            editor_selection_object_id_u64_[i - 1u] = editor_selection_object_id_u64_[i];
        }
        if (editor_selection_count_u32_ > 0u) editor_selection_count_u32_ -= 1u;
        editor_selection_object_id_u64_[editor_selection_count_u32_] = 0u;
        editor_selected_object_id_u64_ = (editor_selection_count_u32_ > 0u) ? editor_selection_object_id_u64_[editor_selection_count_u32_ - 1u] : 0u;
    } else {
        if (editor_selection_count_u32_ >= 16u) {
            for (uint32_t i = 1u; i < 16u; ++i) editor_selection_object_id_u64_[i - 1u] = editor_selection_object_id_u64_[i];
            editor_selection_count_u32_ = 15u;
        }
        editor_selection_object_id_u64_[editor_selection_count_u32_++] = object_id_u64;
        editor_selected_object_id_u64_ = object_id_u64;
    }
}

void App::EmitEditorSelection(uint64_t object_id_u64) {
    if (!scene_) return;
    SetEditorSelectionLocal(object_id_u64);
    EwControlPacket cp{};
    cp.kind = EwControlPacketKind::EditorSetSelection;
    cp.source_u16 = 1;
    cp.tick_u64 = scene_->sm.canonical_tick;
    cp.payload.editor_set_selection.selected_object_id_u64 = object_id_u64;
    (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
}

void App::EmitEditorToggleSelection(uint64_t object_id_u64) {
    if (!scene_) return;
    ToggleEditorSelectionLocal(object_id_u64);
    EwControlPacket cp{};
    cp.kind = EwControlPacketKind::EditorToggleSelection;
    cp.source_u16 = 1;
    cp.tick_u64 = scene_->sm.canonical_tick;
    cp.payload.editor_toggle_selection.object_id_u64 = object_id_u64;
    (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
}

void App::EmitEditorAxisConstraint() {
    if (!scene_) return;
    EwControlPacket cp{};
    cp.kind = EwControlPacketKind::EditorSetAxisConstraint;
    cp.source_u16 = 1;
    cp.tick_u64 = scene_->sm.canonical_tick;
    cp.payload.editor_set_axis_constraint.axis_constraint_u8 = editor_axis_constraint_u8_;
    (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
}

void App::EmitEditorCommitTransformTxn(uint64_t object_id_u64,
                                       const int32_t before_pos_q16_16[3],
                                       const int32_t before_rot_q16_16[4],
                                       const int32_t after_pos_q16_16[3],
                                       const int32_t after_rot_q16_16[4]) {
    if (!scene_) return;
    EwControlPacket cp{};
    cp.kind = EwControlPacketKind::EditorCommitTransformTxn;
    cp.source_u16 = 1;
    cp.tick_u64 = scene_->sm.canonical_tick;
    cp.payload.editor_commit_transform_txn.object_id_u64 = object_id_u64;
    for (int k = 0; k < 3; ++k) {
        cp.payload.editor_commit_transform_txn.before_pos_q16_16[k] = before_pos_q16_16[k];
        cp.payload.editor_commit_transform_txn.after_pos_q16_16[k] = after_pos_q16_16[k];
    }
    for (int k = 0; k < 4; ++k) {
        cp.payload.editor_commit_transform_txn.before_rot_q16_16[k] = before_rot_q16_16[k];
        cp.payload.editor_commit_transform_txn.after_rot_q16_16[k] = after_rot_q16_16[k];
    }
    (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
}

void App::EmitEditorUndo() {
    if (!scene_) return;
    EwControlPacket cp{};
    cp.kind = EwControlPacketKind::EditorUndo;
    cp.source_u16 = 1;
    cp.tick_u64 = scene_->sm.canonical_tick;
    (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
}

void App::EmitEditorRedo() {
    if (!scene_) return;
    EwControlPacket cp{};
    cp.kind = EwControlPacketKind::EditorRedo;
    cp.source_u16 = 1;
    cp.tick_u64 = scene_->sm.canonical_tick;
    (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
}

}

void App::EmitEditorGizmo() {
    if (!scene_) return;
    EwControlPacket cp{};
    cp.kind = EwControlPacketKind::EditorSetGizmo;
    cp.source_u16 = 1;
    cp.tick_u64 = scene_->sm.canonical_tick;
    cp.payload.editor_set_gizmo.gizmo_mode_u8 = editor_gizmo_mode_u8_;
    cp.payload.editor_set_gizmo.gizmo_space_u8 = editor_gizmo_space_u8_;
    cp.payload.editor_set_gizmo.pad_u8[0] = 0;
    cp.payload.editor_set_gizmo.pad_u8[1] = 0;
    (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
}

void App::EmitEditorSnap() {
    if (!scene_) return;
    EwControlPacket cp{};
    cp.kind = EwControlPacketKind::EditorSetSnap;
    cp.source_u16 = 1;
    cp.tick_u64 = scene_->sm.canonical_tick;
    cp.payload.editor_set_snap.snap_enabled_u8 = editor_snap_enabled_u8_;
    cp.payload.editor_set_snap.pad_u8[0] = 0;
    cp.payload.editor_set_snap.pad_u8[1] = 0;
    cp.payload.editor_set_snap.pad_u8[2] = 0;
    cp.payload.editor_set_snap.grid_step_m_q16_16 = editor_grid_step_m_q16_16_;
    cp.payload.editor_set_snap.angle_step_deg_q16_16 = editor_angle_step_deg_q16_16_;
    (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
}

void App::EmitCameraSetFromRig() {
    if (!scene_) return;
    // Orbit rig -> camera pose.
    const float cy = cosf(cam_yaw_rad_);
    const float sy = sinf(cam_yaw_rad_);
    const float cp = cosf(cam_pitch_rad_);
    const float sp = sinf(cam_pitch_rad_);

    float offset[3] = { cy*cp*cam_dist_m_, sy*cp*cam_dist_m_, sp*cam_dist_m_ };
    float pos[3] = { cam_target_[0] + offset[0], cam_target_[1] + offset[1], cam_target_[2] + offset[2] };
    float q[4];
    ew_quat_from_lookat(q, pos, cam_target_);

    EwControlPacket cpkt{};
    cpkt.kind = EwControlPacketKind::CameraSet;
    cpkt.source_u16 = 1;
    cpkt.tick_u64 = scene_->sm.canonical_tick;
    cpkt.payload.camera_set.focus_mode_u8 = 0;
    cpkt.payload.camera_set.pad_u8[0] = 0;
    cpkt.payload.camera_set.pad_u8[1] = 0;
    cpkt.payload.camera_set.pad_u8[2] = 0;
    cpkt.payload.camera_set.manual_focus_distance_m_q32_32 = (int64_t)(5) * (1ll<<32);
    cpkt.payload.camera_set.focal_length_mm_q16_16 = (int32_t)(50 * 65536);
    cpkt.payload.camera_set.aperture_f_q16_16 = (int32_t)(28 * 65536 / 10);
    cpkt.payload.camera_set.exposure_ev_q16_16 = 0;
    cpkt.payload.camera_set.pos_xyz_q16_16[0] = (int32_t)llround((double)pos[0] * 65536.0);
    cpkt.payload.camera_set.pos_xyz_q16_16[1] = (int32_t)llround((double)pos[1] * 65536.0);
    cpkt.payload.camera_set.pos_xyz_q16_16[2] = (int32_t)llround((double)pos[2] * 65536.0);
    cpkt.payload.camera_set.rot_quat_q16_16[0] = (int32_t)llround((double)q[0] * 65536.0);
    cpkt.payload.camera_set.rot_quat_q16_16[1] = (int32_t)llround((double)q[1] * 65536.0);
    cpkt.payload.camera_set.rot_quat_q16_16[2] = (int32_t)llround((double)q[2] * 65536.0);
    cpkt.payload.camera_set.rot_quat_q16_16[3] = (int32_t)llround((double)q[3] * 65536.0);

    (void)ew_runtime_submit_control_packet(&scene_->sm, &cpkt);
}
void App::AppendOutputUtf8(const std::string& line) {
    std::wstring wline = utf8_to_wide(line);
    wline.append(L"\r\n");

    int len = GetWindowTextLengthW(hwnd_output_);
    SendMessageW(hwnd_output_, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hwnd_output_, EM_REPLACESEL, FALSE, (LPARAM)wline.c_str());

    // If the runtime emits AI chat lines, mirror them into the AI chat UI.
    // This keeps AI communication substrate-resident and UI-first.
    // Expected formats:
    //   AI_CHAT:<text>
    //   ASSIST:<text>
    if (line.rfind("AI_CHAT:", 0) == 0 || line.rfind("ASSIST:", 0) == 0) {
        const size_t off = (line.rfind("AI_CHAT:", 0) == 0) ? 8u : 7u;
        std::string payload = (line.size() > off) ? line.substr(off) : std::string();
        // Trim one leading space if present.
        if (!payload.empty() && payload[0] == ' ') payload.erase(0, 1);
        const std::wstring wp = utf8_to_wide(payload);
        const uint32_t chat_idx = ai_tab_index_u32_;
        AiChatAppendAssistant(chat_idx, wp);
        AiChatRenderSelected();
        // continue; we still keep the output log record.
    } else if (ew_is_ai_patch_explanation_line(line)) {
        const uint32_t chat_idx = ai_tab_index_u32_;
        const std::wstring wp = ew_format_ai_patch_explanation_line(line);
        ew_append_bounded_multiline(ai_chat_patch_explain_w_[chat_idx], wp, 16u, 8192u);
        ai_chat_patch_meta_w_[chat_idx].clear();
        SetAiChatWorkflowEvent(chat_idx, L"Patch rationale updated", false);
        AiChatAppend(chat_idx, std::wstring(L"Assistant: ") + wp);
        if (scene_) {
            std::wstring clipped = wp;
            if (clipped.size() > 220u) clipped.resize(220u);
            scene_->ObserveAiChatMemory(chat_idx, SubstrateManager::EW_CHAT_MEMORY_MODE_CODE, std::string("assistant:") + wide_to_utf8(clipped));
            if (chat_idx == ai_tab_index_u32_) RefreshAiChatCortex(chat_idx);
        }
        AiChatRenderSelected();
    }

    // Content Browser now projects from structured runtime snapshots, not parsed UI text.
}

LRESULT CALLBACK App::WndProcThunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto cs = (CREATESTRUCTW*)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    App* self = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self) return self->WndProc(hwnd, msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT App::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_ERASEBKGND: {
            ew_theme_init_once();
            HDC hdc = (HDC)wparam;
            RECT rc{}; GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_brush_bg);
            return 1;
        }
        case WM_CTLCOLORDLG: {
            ew_theme_init_once();
            HDC hdc = (HDC)wparam;
            SetBkColor(hdc, g_theme.bg);
            SetTextColor(hdc, g_theme.text);
            return (INT_PTR)g_brush_bg;
        }
        case WM_CTLCOLORSTATIC: {
            ew_theme_init_once();
            HDC hdc = (HDC)wparam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, g_theme.text);
            SetBkColor(hdc, g_theme.panel);
            return (INT_PTR)g_brush_panel;
        }
        case WM_CTLCOLOREDIT: {
            ew_theme_init_once();
            HDC hdc = (HDC)wparam;
            SetBkColor(hdc, g_theme.edit_bg);
            SetTextColor(hdc, g_theme.text);
            return (INT_PTR)g_brush_edit;
        }
        case WM_CTLCOLORLISTBOX: {
            ew_theme_init_once();
            HDC hdc = (HDC)wparam;
            SetBkColor(hdc, g_theme.edit_bg);
            SetTextColor(hdc, g_theme.text);
            return (INT_PTR)g_brush_edit;
        }
        case WM_CTLCOLORBTN: {
            ew_theme_init_once();
            HDC hdc = (HDC)wparam;
            SetBkColor(hdc, g_theme.panel);
            SetTextColor(hdc, g_theme.text);
            return (INT_PTR)g_brush_panel;
        }

        case WM_SIZE: {
            if (hwnd == hwnd_main_) {
                RECT rc{}; GetClientRect(hwnd_main_, &rc);
                client_w_ = rc.right - rc.left;
                client_h_ = rc.bottom - rc.top;
                LayoutChildren(client_w_, client_h_);
                resized_ = true;
            } else if (hwnd == hwnd_ai_panel_) {
                LayoutAiPanelChildren();
            }
        } break;
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);

            // Outliner toolbar search.
            if (id == 1514 && code == EN_CHANGE) {
                wchar_t buf[512]; buf[0] = 0;
                if (hwnd_outliner_search_) GetWindowTextW(hwnd_outliner_search_, buf, 512);
                outliner_filter_utf8_ = wide_to_utf8(buf);
                RebuildOutlinerList();
            }
            if (id == 1515 && code == BN_CLICKED) {
                outliner_filter_utf8_.clear();
                if (hwnd_outliner_search_) SetWindowTextW(hwnd_outliner_search_, L"");
                RebuildOutlinerList();
            }

            // Voxel presets: list -> combo sync.
            if (id == 2612 && code == LBN_SELCHANGE && hwnd_voxel_presets_list_ && hwnd_voxel_preset_) {
                const int sel = (int)SendMessageW(hwnd_voxel_presets_list_, LB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    wchar_t txt[256]; txt[0] = 0;
                    SendMessageW(hwnd_voxel_presets_list_, LB_GETTEXT, (WPARAM)sel, (LPARAM)txt);
                    int cb = (int)SendMessageW(hwnd_voxel_preset_, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)txt);
                    if (cb >= 0) SendMessageW(hwnd_voxel_preset_, CB_SETCURSEL, (WPARAM)cb, 0);
                }
            }

            if (id == 2615 && code == LBN_SELCHANGE) {
                const int sel = hwnd_voxel_atom_nodes_ ? (int)SendMessageW(hwnd_voxel_atom_nodes_, LB_GETCURSEL, 0, 0) : -1;
                if (sel >= 0) voxel_atom_node_selected_i32_ = sel;
                RefreshVoxelDesignerPanel();
                return 0;
            }
            if (id == 2713 && code == LBN_SELCHANGE) {
                const int sel = hwnd_node_graph_ ? (int)SendMessageW(hwnd_node_graph_, LB_GETCURSEL, 0, 0) : -1;
                if (sel >= 0) node_graph_selected_i32_ = sel;
                node_export_preview_w_.clear();
                ClampNodePinSelections();
                RefreshNodePanel();
                return 0;
            }
            if (id == 2733 && code == LBN_SELCHANGE) {
                node_export_preview_w_.clear();
                ClampNodePinSelections();
                RefreshNodePanel();
                return 0;
            }
            if (id == 2733 && code == LBN_DBLCLK) {
                const int sel = hwnd_node_results_ ? (int)SendMessageW(hwnd_node_results_, LB_GETCURSEL, 0, 0) : -1;
                if (sel >= 0 && (size_t)sel < node_palette_entries_.size()) {
                    (void)SpawnNodePaletteEntry((size_t)sel);
                }
                return 0;
            }
            if (id == 2721 && code == LBN_SELCHANGE) {
                const int sel = hwnd_seq_timeline_ ? (int)SendMessageW(hwnd_seq_timeline_, LB_GETCURSEL, 0, 0) : -1;
                if (sel >= 0) seq_selected_i32_ = sel;
                RefreshSequencerPanel();
                return 0;
            }
            if (id == 2614 && code == BN_CLICKED) {
                resonance_view_ = !resonance_view_;
                if (hwnd_ai_status_) {
                    SetWindowTextW(hwnd_ai_status_, resonance_view_ ? L"Viewport resonance editor view enabled." : L"Viewport returned to standard shading view.");
                }
                RefreshVoxelDesignerPanel();
                InvalidateRect(hwnd_viewport_, nullptr, FALSE);
                return 0;
            }

            // Apply voxel preset (UI-only). Emits a line and sets representative sliders.
            if (id == 2611 && code == BN_CLICKED && hwnd_voxel_preset_) {
                int sel = (int)SendMessageW(hwnd_voxel_preset_, CB_GETCURSEL, 0, 0);
                wchar_t txt[256]; txt[0] = 0;
                if (sel >= 0) SendMessageW(hwnd_voxel_preset_, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)txt);
                std::string p = wide_to_utf8(txt);
                AppendOutputUtf8(std::string("VOXEL_PRESET:") + p);

                int dens = 50, hard = 50, rough = 50;
                auto is = [&](const char* k)->bool { return p.find(k) != std::string::npos; };
                if (is("Steel") || is("Aluminum") || is("Titanium") || is("Copper") || is("Brass") || is("Bronze") || is("Gold") || is("Silver")) {
                    dens = 85; hard = 85; rough = 18;
                } else if (is("Concrete") || is("Asphalt") || is("Granite") || is("Marble")) {
                    dens = 70; hard = 80; rough = 65;
                } else if (is("Wood") || is("Plywood")) {
                    dens = 45; hard = 40; rough = 55;
                } else if (is("Plastic") || is("Polycarbonate")) {
                    dens = 35; hard = 35; rough = 35;
                } else if (is("Rubber")) {
                    dens = 30; hard = 15; rough = 60;
                } else if (is("Glass")) {
                    dens = 55; hard = 65; rough = 8;
                } else if (is("Leather") || is("Fabric")) {
                    dens = 25; hard = 15; rough = 70;
                }
                HWND h_d = GetDlgItem(hwnd_rdock_voxel_, 2601);
                HWND h_h = GetDlgItem(hwnd_rdock_voxel_, 2602);
                HWND h_r = GetDlgItem(hwnd_rdock_voxel_, 2603);
                if (h_d) SendMessageW(h_d, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)dens);
                if (h_h) SendMessageW(h_h, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)hard);
                if (h_r) SendMessageW(h_r, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)rough);
                RefreshVoxelDesignerPanel();
                return 0;
            }

            // Menu actions (HIWORD==0).
            if (code == 0) {
                if (id >= 2760 && id < 2760 + (int)node_palette_entries_.size()) {
                    const size_t palette_idx = (size_t)(id - 2760);
                    (void)SpawnNodePaletteEntry(palette_idx);
                    return 0;
                }
                if (id == 9001) { PostQuitMessage(0); return 0; }
                if (id == 9201) {
                    content_visible_ = !content_visible_;
                    if (hwnd_content_) ShowWindow(hwnd_content_, content_visible_ ? SW_SHOW : SW_HIDE);
                    LayoutChildren(client_w_, client_h_);
                    return 0;
                }
                if (id == 9202) { ToggleAiPanel(); return 0; }
                if (id == 9209) { live_mode_enabled_ = !live_mode_enabled_; SyncLiveModeProjection(); return 0; }
                if (id >= 9203 && id <= 9208) { SetRightDockPanelVisible((uint32_t)(id - 9203), !rdock_panel_visible_[id - 9203]); RefreshNodePanel(); RefreshSequencerPanel(); return 0; }
                if (id == 9301 && scene_) { scene_->ContentReindex(); return 0; }
                if (id == 9302) { RefreshContentBrowserFromRuntime(200u); return 0; }
                if (id == 9101 || id == 2090) {
                    if (!CopyFocusedSurfaceToClipboard() && id == 2090) {
                        (void)CopyDetailsBlockToClipboard();
                    }
                    return 0;
                }
                if (id == 9102 || id == 2091) {
                    if (!PasteFocusedSurfaceFromClipboard() && id == 2091) {
                        (void)PasteDetailsBlockFromClipboard();
                    }
                    return 0;
                }
            }

            if (id == 4861 && code == BN_CLICKED) { AiPanelSetView(0u); return 0; }
            if (id == 4862 && code == BN_CLICKED) { AiPanelSetView(1u); return 0; }
            if (id == 4863 && code == BN_CLICKED) { AiPanelSetView(2u); return 0; }
            if (id == 4898 && code == BN_CLICKED) { ai_chat_mode_u32_[ai_tab_index_u32_] = SubstrateManager::EW_CHAT_MEMORY_MODE_TALK; RefreshAiChatCortex(ai_tab_index_u32_); return 0; }
            if (id == 4899 && code == BN_CLICKED) { ai_chat_mode_u32_[ai_tab_index_u32_] = SubstrateManager::EW_CHAT_MEMORY_MODE_CODE; RefreshAiChatCortex(ai_tab_index_u32_); return 0; }
            if (id == 4900 && code == BN_CLICKED) { ai_chat_mode_u32_[ai_tab_index_u32_] = SubstrateManager::EW_CHAT_MEMORY_MODE_SIM; RefreshAiChatCortex(ai_tab_index_u32_); return 0; }
            if (id == 4864 && code == BN_CLICKED) { RefreshAiRepoPane(); return 0; }
            if (id == 4865 && code == BN_CLICKED) {
                std::wstring txt = ew_clip_get_text_utf16(hwnd_ai_panel_);
                (void)txt;
                int len = hwnd_ai_repo_preview_ ? GetWindowTextLengthW(hwnd_ai_repo_preview_) : 0;
                std::wstring w;
                if (len > 0 && hwnd_ai_repo_preview_) {
                    std::vector<wchar_t> buf((size_t)len + 1u, 0);
                    GetWindowTextW(hwnd_ai_repo_preview_, buf.data(), len + 1);
                    w.assign(buf.data());
                }
                (void)ew_clip_set_text_utf16(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, w);
                return 0;
            }
            if (id == 4866 && code == BN_CLICKED) {
                if (scene_ && hwnd_ai_repo_list_) {
                    const int sel = (int)SendMessageW(hwnd_ai_repo_list_, LB_GETCURSEL, 0, 0);
                    if (sel >= 0 && (size_t)sel < ai_repo_rel_paths_utf8_.size()) {
                        const std::string rel = ai_repo_rel_paths_utf8_[(size_t)sel];
                        scene_->SetCoherenceHighlightPath(rel);
                        (void)SelectAiCoherencePath(rel, false);
                        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, (std::wstring(L"Repository: highlighted ") + utf8_to_wide(rel)).c_str());
                    }
                }
                return 0;
            }
            if (id == 4867 && code == LBN_SELCHANGE) { UpdateAiRepoSelection(); return 0; }
            if (id == 4867 && code == LBN_DBLCLK) { OpenSelectedAiRepoFile(); return 0; }
            if (id == 4886 && code == BN_CLICKED) {
                if (hwnd_ai_repo_list_) {
                    const int sel = (int)SendMessageW(hwnd_ai_repo_list_, LB_GETCURSEL, 0, 0);
                    if (sel >= 0 && (size_t)sel < ai_repo_rel_paths_utf8_.size()) {
                        (void)ew_clip_set_text_utf16(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, utf8_to_wide(ai_repo_rel_paths_utf8_[(size_t)sel]));
                        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, (std::wstring(L"Repository path copied: ") + utf8_to_wide(ai_repo_rel_paths_utf8_[(size_t)sel])).c_str());
                    }
                }
                return 0;
            }
            if (id == 4873 && code == BN_CLICKED) {
                if (scene_ && hwnd_ai_coh_query_) {
                    wchar_t buf[512]{}; GetWindowTextW(hwnd_ai_coh_query_, buf, 512);
                    std::string q = wide_to_utf8(buf), err; std::vector<genesis::GeCoherenceHit> hits;
                    if (scene_->SnapshotCoherenceQuery(q, 32u, hits, &err)) {
                        CanonicalReferenceSummary summary{};
                        if (BuildCanonicalReferenceSummaryFromHits(std::string(), std::string("coherence_query"), std::wstring(L"coherence query canonical ranking"), q, hits, &summary, &err)) {
                            CommitCanonicalReferenceSummary(summary, false, L"Coherence query");
                        }
                    } else {
                        SetAiCoherenceResults(std::vector<std::wstring>{utf8_to_wide(err.empty()?std::string("Query failed."):err)}, std::vector<std::string>{}, nullptr);
                    }
                }
                return 0;
            }
            if (id == 4874 && code == BN_CLICKED) {
                if (scene_ && hwnd_ai_coh_query_) { wchar_t buf[512]{}; GetWindowTextW(hwnd_ai_coh_query_, buf, 512); scene_->SetCoherenceHighlightQuery(wide_to_utf8(buf), 32u); }
                return 0;
            }
            if (id == 4875 && code == BN_CLICKED) {
                RefreshAiCoherenceStats();
                std::vector<std::wstring> lines;
                int len = hwnd_ai_coh_stats_ ? GetWindowTextLengthW(hwnd_ai_coh_stats_) : 0;
                if (len > 0 && hwnd_ai_coh_stats_) {
                    std::vector<wchar_t> buf((size_t)len + 1u, 0);
                    GetWindowTextW(hwnd_ai_coh_stats_, buf.data(), len + 1);
                    lines.push_back(buf.data());
                }
                if (lines.empty()) lines.push_back(L"Coherence stats unavailable.");
                SetAiCoherenceResults(lines, std::vector<std::string>{}, nullptr);
                return 0;
            }
            if (id == 4876 && code == BN_CLICKED) {
                if (scene_) {
                    bool ok=false; std::string rep; std::vector<std::wstring> lines;
                    if (scene_->SnapshotCoherenceSelftest(ok, rep)) {
                        lines.push_back(ok ? L"OK" : L"FAIL");
                        std::wstring wrep = utf8_to_wide(rep);
                        size_t pos = 0;
                        while (pos < wrep.size()) {
                            size_t next = wrep.find_first_of(L"\r\n", pos);
                            std::wstring line = wrep.substr(pos, next == std::wstring::npos ? std::wstring::npos : next - pos);
                            if (!line.empty()) lines.push_back(line);
                            if (next == std::wstring::npos) break;
                            pos = next + 1;
                            while (pos < wrep.size() && (wrep[pos] == L'\r' || wrep[pos] == L'\n')) ++pos;
                        }
                    } else {
                        lines.push_back(L"Coherence selftest unavailable.");
                    }
                    SetAiCoherenceResults(lines, std::vector<std::string>{}, nullptr);
                }
                return 0;
            }
            if ((id == 4877 || id == 4878) && code == BN_CLICKED) {
                if (scene_ && hwnd_ai_coh_old_ && hwnd_ai_coh_new_) {
                    wchar_t oldb[256]{}, newb[256]{}; GetWindowTextW(hwnd_ai_coh_old_, oldb, 256); GetWindowTextW(hwnd_ai_coh_new_, newb, 256);
                    std::string olda = wide_to_utf8(oldb), newa = wide_to_utf8(newb), err;
                    if (id == 4877) {
                        CanonicalReferenceSummary summary{};
                        if (BuildCanonicalReferenceSummaryForRename(olda, newa, 64u, &summary, &err)) {
                            CommitCanonicalReferenceSummary(summary, false, L"Rename plan");
                            ai_coh_patch_preview_w_ = L"Rename review only. Use Prepare Patch to generate a patch buffer after reviewing impacted references.";
                            SetWindowTextW(hwnd_ai_coh_patch_, ai_coh_patch_preview_w_.c_str());
                        } else {
                            SetAiCoherenceResults(std::vector<std::wstring>{utf8_to_wide(err.empty()?std::string("Rename plan failed."):err)}, std::vector<std::string>{}, nullptr);
                        }
                    } else {
                        std::string patch;
                        if (scene_->SnapshotCoherenceRenamePatch(olda, newa, 64u, patch, &err)) {
                            std::vector<genesis::GeCoherenceHit> hits; std::string plan_err;
                            (void)scene_->SnapshotCoherenceRenamePlan(olda, newa, 64u, hits, &plan_err);
                            CanonicalReferenceSummary rename_summary{};
                            if (BuildCanonicalReferenceSummaryForRename(olda, newa, 64u, &rename_summary, &plan_err)) {
                                CommitCanonicalReferenceSummary(rename_summary, false, L"Rename patch prep");
                            }
                            std::wstring summary = L"Rename patch prepared (review before apply)\r\n";
                            summary += L"Old: " + utf8_to_wide(olda) + L" -> New: " + utf8_to_wide(newa) + L"\r\n";
                            summary += L"Impacted references: " + std::to_wstring((unsigned long long)hits.size()) + L"\r\n\r\n";
                            ai_chat_patch_w_[ai_tab_index_u32_] = utf8_to_wide(patch);
                            ai_chat_patch_explain_w_[ai_tab_index_u32_].clear();
                            ai_chat_patch_meta_w_[ai_tab_index_u32_].clear();
                            ai_chat_patch_previewed_[ai_tab_index_u32_] = false;
                            CaptureAiChatPatchScopeSnapshot(ai_tab_index_u32_);
                            ai_coh_patch_preview_w_ = summary + utf8_to_wide(patch);
                            SetWindowTextW(hwnd_ai_coh_patch_, ai_coh_patch_preview_w_.c_str());
                            AiChatRenderSelected();
                            if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Coherence: rename patch buffered for preview.");
                        } else {
                            SetWindowTextW(hwnd_ai_coh_patch_, utf8_to_wide(err.empty()?std::string("Patch failed."):err).c_str());
                        }
                    }
                }
                return 0;
            }
            if (id == 4879 && code == LBN_SELCHANGE) { UpdateAiCoherenceSelection(); return 0; }
            if (id == 4879 && code == LBN_DBLCLK) {
                if (scene_ && ai_coh_selected_index_i32_ >= 0 && (size_t)ai_coh_selected_index_i32_ < ai_coh_result_paths_utf8_.size() && !ai_coh_result_paths_utf8_[(size_t)ai_coh_selected_index_i32_].empty()) {
                    const std::string rel = ai_coh_result_paths_utf8_[(size_t)ai_coh_selected_index_i32_];
                    scene_->SetCoherenceHighlightPath(rel);
                    (void)SelectAiRepoPath(rel, false);
                    OpenSelectedAiRepoFile();
                }
                return 0;
            }
            if (id == 4891 && code == BN_CLICKED) {
                std::wstring joined;
                for (size_t i = 0; i < ai_coh_result_lines_w_.size(); ++i) {
                    if (i) joined += L"\r\n";
                    joined += ai_coh_result_lines_w_[i];
                }
                (void)ew_clip_set_text_utf16(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, joined);
                return 0;
            }
            if (id == 4892 && code == BN_CLICKED) {
                (void)ew_clip_set_text_utf16(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, ai_coh_patch_preview_w_);
                return 0;
            }
            if (id == 4893 && code == BN_CLICKED) {
                if (scene_ && ai_coh_selected_index_i32_ >= 0 && (size_t)ai_coh_selected_index_i32_ < ai_coh_result_paths_utf8_.size() && !ai_coh_result_paths_utf8_[(size_t)ai_coh_selected_index_i32_].empty()) {
                    const std::string rel = ai_coh_result_paths_utf8_[(size_t)ai_coh_selected_index_i32_];
                    scene_->SetCoherenceHighlightPath(rel);
                    (void)SelectAiRepoPath(rel, false);
                    if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, (std::wstring(L"Coherence: highlighted ") + utf8_to_wide(rel)).c_str());
                }
                return 0;
            }
            if (id == 4894 && code == BN_CLICKED) {
                if (ai_coh_selected_index_i32_ >= 0 && (size_t)ai_coh_selected_index_i32_ < ai_coh_result_paths_utf8_.size() && !ai_coh_result_paths_utf8_[(size_t)ai_coh_selected_index_i32_].empty()) {
                    const std::string rel = ai_coh_result_paths_utf8_[(size_t)ai_coh_selected_index_i32_];
                    (void)ew_clip_set_text_utf16(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, utf8_to_wide(rel));
                    if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, (std::wstring(L"Coherence path copied: ") + utf8_to_wide(rel)).c_str());
                }
                return 0;
            }
            if (id == 4895 && code == BN_CLICKED) {
                if (ai_coh_selected_index_i32_ >= 0 && (size_t)ai_coh_selected_index_i32_ < ai_coh_result_paths_utf8_.size() && !ai_coh_result_paths_utf8_[(size_t)ai_coh_selected_index_i32_].empty()) {
                    const std::string rel = ai_coh_result_paths_utf8_[(size_t)ai_coh_selected_index_i32_];
                    if (SelectAiRepoPath(rel, false)) {
                        OpenSelectedAiRepoFile();
                    } else {
                        std::error_code ec;
                        std::filesystem::path abs = std::filesystem::current_path(ec);
                        if (!ec && !abs.empty()) {
                            abs /= rel;
                            const std::wstring wabs = abs.lexically_normal().wstring();
                            ShellExecuteW(hwnd_main_ ? hwnd_main_ : hwnd_ai_panel_, L"open", wabs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        }
                    }
                }
                return 0;
            }

            auto send_toggle = [&](EwControlPacketKind k, bool enabled) {
                if (!scene_) return;
                EwControlPacket cp{};
                cp.kind = k;
                cp.source_u16 = 1;
                cp.tick_u64 = scene_->sm.canonical_tick;
                if (k == EwControlPacketKind::SimSetPlay) cp.payload.sim_set_play.enabled_u8 = enabled ? 1u : 0u;
                else if (k == EwControlPacketKind::AiSetEnabled) cp.payload.ai_set_enabled.enabled_u8 = enabled ? 1u : 0u;
                else if (k == EwControlPacketKind::AiSetLearning) cp.payload.ai_set_learning.enabled_u8 = enabled ? 1u : 0u;
                else if (k == EwControlPacketKind::AiSetCrawling) cp.payload.ai_set_crawling.enabled_u8 = enabled ? 1u : 0u;
                (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
            };

            // AI/sim toggle switches.
            if (id == 2731 && code == EN_CHANGE) { RefreshNodePanel(); return 0; }
            if (code == BN_CLICKED) {
                if (id == 2090) { SendMessageW(hwnd_main_, WM_COMMAND, MAKEWPARAM(9101,0), 0); return 0; }
                if (id == 2091) { SendMessageW(hwnd_main_, WM_COMMAND, MAKEWPARAM(9102,0), 0); return 0; }
                if (id == 2712) {
                    if (hwnd_ai_panel_ && !IsWindowVisible(hwnd_ai_panel_)) ToggleAiPanel();
                    AiPanelSetView(2u);
                    RefreshAiCoherenceStats();
                    if (hwnd_ai_coh_results_) SetFocus(hwnd_ai_coh_results_);
                    return 0;
                }
                if (id == 2732 && code == BN_CLICKED) {
                    RebuildNodePaletteEntries();
                    const int result_sel = hwnd_node_results_ ? (int)SendMessageW(hwnd_node_results_, LB_GETCURSEL, 0, 0) : -1;
                    if (result_sel >= 0 && (size_t)result_sel < node_palette_entries_.size()) {
                        (void)SpawnNodePaletteEntry((size_t)result_sel);
                        return 0;
                    }
                    if (node_palette_entries_.size() == 1u) {
                        (void)SpawnNodePaletteEntry(0u);
                        return 0;
                    }
                    RECT rc{};
                    if (hwnd_node_results_) GetWindowRect(hwnd_node_results_, &rc); else if (hwnd_node_graph_) GetWindowRect(hwnd_node_graph_, &rc); else if (hwnd_rdock_node_) GetWindowRect(hwnd_rdock_node_, &rc);
                    POINT pt{ rc.left + 40, rc.top + 40 };
                    ShowNodeSpawnMenu(pt);
                    return 0;
                }
                if (id == 2734 && code == BN_CLICKED) {
                    std::wstring label_w, lookup_w, scope_w, hint_w, policy_w, placement_w;
                    bool locked = false;
                    const int result_sel = hwnd_node_results_ ? (int)SendMessageW(hwnd_node_results_, LB_GETCURSEL, 0, 0) : -1;
                    if (result_sel >= 0 && (size_t)result_sel < node_palette_entries_.size()) {
                        const auto& pe = node_palette_entries_[(size_t)result_sel];
                        label_w = pe.label_w; lookup_w = pe.lookup_name_w; scope_w = pe.export_scope_w; hint_w = pe.language_hint_w; policy_w = pe.language_policy_w; placement_w = pe.placement_w; locked = pe.language_locked;
                    } else if (node_graph_selected_i32_ >= 0 && (size_t)node_graph_selected_i32_ < node_graph_items_w_.size()) {
                        const size_t idx = (size_t)node_graph_selected_i32_;
                        label_w = node_graph_items_w_[idx];
                        if (idx < node_graph_lookup_name_w_.size()) lookup_w = node_graph_lookup_name_w_[idx];
                        if (idx < node_graph_export_scope_w_.size()) scope_w = node_graph_export_scope_w_[idx];
                        if (idx < node_graph_language_hint_w_.size()) hint_w = node_graph_language_hint_w_[idx];
                        if (idx < node_graph_language_policy_w_.size()) policy_w = node_graph_language_policy_w_[idx];
                        if (idx < node_graph_placement_w_.size()) placement_w = node_graph_placement_w_[idx];
                        if (idx < node_graph_language_locked_u8_.size()) locked = node_graph_language_locked_u8_[idx] != 0;
                    }
                    if (scope_w.empty() || _wcsicmp(scope_w.c_str(), L"none") == 0) {
                        node_export_preview_w_.clear();
                        if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Node Graph: selected result/node does not expose an export behavior preview.");
                        RefreshNodePanel();
                        return 0;
                    }
                    node_export_preview_w_.clear();
                    node_export_preview_w_ += L"Node: " + label_w + L"
";
                    node_export_preview_w_ += L"Scope: " + scope_w + L"
";
                    node_export_preview_w_ += L"Placement: " + placement_w + L"
";
                    node_export_preview_w_ += L"Language hint: " + hint_w + (locked ? L"  [locked]" : L"") + L"
";
                    node_export_preview_w_ += L"Language rule: " + policy_w + L"
";
                    if (ai_tab_index_u32_ < AI_CHAT_MAX && !ai_chat_project_root_w_[ai_tab_index_u32_].empty()) {
                        node_export_preview_w_ += L"Linked project root: " + ai_chat_project_root_w_[ai_tab_index_u32_] + L"
";
                    }
                    std::string stage_summary, stage_err;
                    EigenWare::SubstrateManager::EwStagedExportBundle bundle{};
                    const bool staged = scene_ && scene_->sm.ui_stage_node_export_bundle(ai_tab_index_u32_, wide_to_utf8(lookup_w), wide_to_utf8(label_w), wide_to_utf8(scope_w), wide_to_utf8(hint_w), wide_to_utf8(policy_w), locked, stage_summary, &stage_err);
                    const bool has_bundle = scene_ && scene_->sm.ui_snapshot_latest_export_bundle(ai_tab_index_u32_, bundle);
                    if (!staged && !stage_err.empty()) {
                        node_export_preview_w_ += L"Stage status: blocked
Reason: " + utf8_to_wide(stage_err) + L"
";
                        if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Node Graph: export staging blocked. Link a project/work substrate or choose a materializable export lane.");
                    } else if (has_bundle) {
                        wchar_t line_stage[256]{};
                        swprintf(line_stage, 256, L"Stage id: %llu
", (unsigned long long)bundle.stage_id_u64);
                        node_export_preview_w_ += line_stage;
                        node_export_preview_w_ += L"Stage status: " + utf8_to_wide(bundle.stage_status_utf8) + L"
";
                        node_export_preview_w_ += L"Bundle: " + utf8_to_wide(bundle.bundle_summary_utf8) + L"
";
                        if (!bundle.operation_label_utf8.empty()) node_export_preview_w_ += L"Operation: " + utf8_to_wide(bundle.operation_label_utf8) + L"
";
                        if (!bundle.continuation_summary_utf8.empty()) node_export_preview_w_ += L"Continuation: " + utf8_to_wide(bundle.continuation_summary_utf8) + L"
";
                        if (!bundle.runtime_split_summary_utf8.empty()) node_export_preview_w_ += L"Runtime/editor audit: " + utf8_to_wide(bundle.runtime_split_summary_utf8) + L"
";
                        if (!bundle.linked_project_root_utf8.empty()) node_export_preview_w_ += L"Bound substrate root: " + utf8_to_wide(bundle.linked_project_root_utf8) + L"
";
                        node_export_preview_w_ += L"Staged targets:
";
                        const uint32_t shown = std::min<uint32_t>(bundle.target_count_u32, 6u);
                        for (uint32_t i = 0u; i < shown; ++i) {
                            const auto& tgt = bundle.targets[i];
                            node_export_preview_w_ += L"  - " + utf8_to_wide(tgt.rel_path_utf8) + L"  ::  " + utf8_to_wide(tgt.effective_language_utf8);
                            if (tgt.language_locked_u8 != 0u) node_export_preview_w_ += L"  [locked]";
                            node_export_preview_w_ += L"
    " + utf8_to_wide(tgt.constraint_reason_utf8) + L"
";
                        }
                        if (bundle.target_count_u32 > shown) {
                            node_export_preview_w_ += L"  ... +" + std::to_wstring((unsigned long long)(bundle.target_count_u32 - shown)) + L" more target(s)
";
                        }
                        if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Node Graph: export bundle staged from the linked project/work substrate.");
                    } else {
                        node_export_preview_w_ += L"Stage status: preview only
";
                        if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Node Graph: export preview refreshed, but no staged bundle was returned.");
                    }
                    node_export_preview_w_ += L"Action: export remains preview-first, but the backend now stages a bounded export bundle/policy object tied to the linked project substrate.";
                    RefreshNodePanel();
                    return 0;
                }
                if (id == 2735 && code == BN_CLICKED) {
                    const int result_sel = hwnd_node_results_ ? (int)SendMessageW(hwnd_node_results_, LB_GETCURSEL, 0, 0) : -1;
                    if (result_sel >= 0 && (size_t)result_sel < node_palette_entries_.size()) {
                        (void)ConnectNodePaletteEntry((size_t)result_sel);
                    } else if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Node Graph: select a connectable search result first.");
                    return 0;
                }
                if (id == 2736 && code == BN_CLICKED) {
                    (void)DisconnectSelectedNode();
                    return 0;
                }
                if (id == 2714) { node_graph_expanded_ = !node_graph_expanded_; RefreshNodePanel(); return 0; }
                if (id == 2715) { node_play_excitation_ = !node_play_excitation_; RefreshNodePanel(); return 0; }
                if (id == 2716) { node_rename_propagate_ = false; RefreshNodePanel(); return 0; }
                if (id == 2717) { node_rename_propagate_ = true; RefreshNodePanel(); return 0; }
                if (id == 2718) {
                    const int band = hwnd_node_rc_band_ ? (int)SendMessageW(hwnd_node_rc_band_, TBM_GETPOS, 0, 0) : spectrum_band_i32_;
                    const int drive = hwnd_node_rc_drive_ ? (int)SendMessageW(hwnd_node_rc_drive_, TBM_GETPOS, 0, 0) : 48;
                    spectrum_band_i32_ = band;
                    spectrum_phase_f32_ = (float)drive * 0.03125f;
                    resonance_view_ = true;
                    if (scene_) {
                        EwControlPacket cp{};
                        cp.source_u16 = 1;
                        cp.tick_u64 = scene_->sm.canonical_tick;
                        cp.kind = EwControlPacketKind::InputAxis;
                        cp.payload.input_axis.axis_id_u32 = 7001u;
                        cp.payload.input_axis.value_q16_16 = band * 65536;
                        (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
                        cp.kind = EwControlPacketKind::InputAxis;
                        cp.payload.input_axis.axis_id_u32 = 7002u;
                        cp.payload.input_axis.value_q16_16 = drive * 655;
                        (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
                        cp.kind = EwControlPacketKind::InputAction;
                        cp.payload.input_action.action_id_u32 = node_rename_propagate_ ? 7004u : 7003u;
                        cp.payload.input_action.pressed_u8 = 1u;
                        (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
                    }
                    if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"RC panel hook emitted bounded control packets and enabled resonance view.");
                    RefreshNodePanel();
                    InvalidateRect(hwnd_viewport_, nullptr, FALSE);
                    return 0;
                }
                if (id == 2722) { seq_play_enabled_ = !seq_play_enabled_; sequencer_scrub_t_f32_ += seq_play_enabled_ ? 0.125f : 0.0f; if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, seq_play_enabled_ ? L"Sequencer transport enabled for derived timeline preview." : L"Sequencer transport paused."); RefreshSequencerPanel(); InvalidateRect(hwnd_viewport_, nullptr, FALSE); return 0; }
                if (id == 2723) { seq_loop_builder_enabled_ = !seq_loop_builder_enabled_; if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, seq_loop_builder_enabled_ ? L"Loop builder armed for contact/recovery stitching." : L"Loop builder returned to idle."); RefreshSequencerPanel(); return 0; }
                if (id == 2724) { if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Sequencer added a bounded advisory key on the selected timeline lane."); sequencer_scrub_t_f32_ += 0.0625f; RefreshSequencerPanel(); return 0; }
                if (id == 2725) { seq_stress_overlay_enabled_ = !seq_stress_overlay_enabled_; if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, seq_stress_overlay_enabled_ ? L"Stress/pain advisory overlays enabled." : L"Stress/pain advisory overlays hidden."); RefreshSequencerPanel(); InvalidateRect(hwnd_viewport_, nullptr, FALSE); return 0; }
                if (id == 2726) { resonance_view_ = true; if (scene_) { EwControlPacket cp{}; cp.source_u16 = 1; cp.tick_u64 = scene_->sm.canonical_tick; cp.kind = EwControlPacketKind::InputAction; cp.payload.input_action.action_id_u32 = 7101u; cp.payload.input_action.pressed_u8 = 1u; (void)ew_runtime_submit_control_packet(&scene_->sm, &cp); } if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Motion-matching hook emitted a bounded locomotion packet and enabled resonance review."); RefreshSequencerPanel(); InvalidateRect(hwnd_viewport_, nullptr, FALSE); return 0; }
                if (id >= 1530 && id <= 1535) {
                    const uint32_t idx = (uint32_t)(id - 1530);
                    rdock_panel_locked_[idx] = !rdock_panel_locked_[idx];
                    RefreshNodePanel();
                    SyncWindowMenu();
                    return 0;
                }
                if (id >= 1540 && id <= 1545) {
                    const uint32_t idx = (uint32_t)(id - 1540);
                    SetRightDockPanelVisible(idx, false);
                    RefreshNodePanel();
                    return 0;
                }
                if (id == 2661) {
                    AiPanelSetView(2u);
                    if (hwnd_ai_panel_ && !IsWindowVisible(hwnd_ai_panel_)) ShowWindow(hwnd_ai_panel_, SW_SHOW);
                    if (hwnd_ai_panel_) SetForegroundWindow(hwnd_ai_panel_);
                    SyncWindowMenu();
                    return 0;
                }

                if (id == 2040) {
                    sim_play_enabled_ = !sim_play_enabled_;
                    send_toggle(EwControlPacketKind::SimSetPlay, sim_play_enabled_);
                    InvalidateRect(hwnd_toggle_play_, nullptr, TRUE);
                } else if (id == 2041) {
                    ai_enabled_ = !ai_enabled_;
                    // Turning AI off also disables learning/crawling at UI level.
                    if (!ai_enabled_) { ai_learning_enabled_ = false; ai_crawling_enabled_ = false; }
                    send_toggle(EwControlPacketKind::AiSetEnabled, ai_enabled_);
                    send_toggle(EwControlPacketKind::AiSetLearning, ai_learning_enabled_);
                    send_toggle(EwControlPacketKind::AiSetCrawling, ai_crawling_enabled_);
                    InvalidateRect(hwnd_toggle_ai_, nullptr, TRUE);
                    InvalidateRect(hwnd_toggle_learning_, nullptr, TRUE);
                    InvalidateRect(hwnd_toggle_crawling_, nullptr, TRUE);
                } else if (id == 2042) {
                    ai_learning_enabled_ = !ai_learning_enabled_;
                    if (ai_learning_enabled_) ai_enabled_ = true;
                    send_toggle(EwControlPacketKind::AiSetEnabled, ai_enabled_);
                    send_toggle(EwControlPacketKind::AiSetLearning, ai_learning_enabled_);
                    InvalidateRect(hwnd_toggle_ai_, nullptr, TRUE);
                    InvalidateRect(hwnd_toggle_learning_, nullptr, TRUE);
                } else if (id == 2043) {
                    ai_crawling_enabled_ = !ai_crawling_enabled_;
                    if (ai_crawling_enabled_) ai_enabled_ = true;
                    send_toggle(EwControlPacketKind::AiSetEnabled, ai_enabled_);
                    send_toggle(EwControlPacketKind::AiSetCrawling, ai_crawling_enabled_);
                    InvalidateRect(hwnd_toggle_ai_, nullptr, TRUE);
                    InvalidateRect(hwnd_toggle_crawling_, nullptr, TRUE);
                } else if (id == 4055) {
                    // AI panel learning toggle
                    ai_learning_enabled_ = !ai_learning_enabled_;
                    if (ai_learning_enabled_) ai_enabled_ = true;
                    send_toggle(EwControlPacketKind::AiSetEnabled, ai_enabled_);
                    send_toggle(EwControlPacketKind::AiSetLearning, ai_learning_enabled_);
                    if (hwnd_toggle_ai_) InvalidateRect(hwnd_toggle_ai_, nullptr, TRUE);
                    if (hwnd_toggle_learning_) InvalidateRect(hwnd_toggle_learning_, nullptr, TRUE);
                    if (hwnd_ai_toggle_learning_) InvalidateRect(hwnd_ai_toggle_learning_, nullptr, TRUE);
                } else if (id == 4056) {
                    // AI panel crawling toggle
                    ai_crawling_enabled_ = !ai_crawling_enabled_;
                    if (ai_crawling_enabled_) ai_enabled_ = true;
                    send_toggle(EwControlPacketKind::AiSetEnabled, ai_enabled_);
                    send_toggle(EwControlPacketKind::AiSetCrawling, ai_crawling_enabled_);
                    if (hwnd_toggle_ai_) InvalidateRect(hwnd_toggle_ai_, nullptr, TRUE);
                    if (hwnd_toggle_crawling_) InvalidateRect(hwnd_toggle_crawling_, nullptr, TRUE);
                    if (hwnd_ai_toggle_crawling_) InvalidateRect(hwnd_ai_toggle_crawling_, nullptr, TRUE);
                } else if (id == 4054) {
                    // AI panel "⋯" menu
                    HMENU menu = CreatePopupMenu();
                    AppendMenuW(menu, MF_STRING | (ai_learning_enabled_ ? MF_CHECKED : 0), 4801, L"Learning");
                    AppendMenuW(menu, MF_STRING | (ai_crawling_enabled_ ? MF_CHECKED : 0), 4802, L"Crawling");
                    AppendMenuW(menu, MF_STRING | (ai_repo_reader_enabled_ ? MF_CHECKED : 0), 4803, L"Repo reader");
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(menu, MF_STRING, 4811, L"Repository Browser…");
                    AppendMenuW(menu, MF_STRING, 4812, L"Coherence Tools…");
                    AppendMenuW(menu, MF_STRING, 4813, L"Workflow Overview…");
                    const std::vector<uint32_t> workflow_order = BuildAiWorkflowPriorityOrder();
                    uint32_t active_items = 0u;
                    for (uint32_t idx : workflow_order) if (BuildAiChatWorkflowPriorityScore(idx) > 0u) ++active_items;
                    const uint32_t blocked_total = CountAiWorkflowBucket(L"Blocked");
                    const uint32_t ready_total = CountAiWorkflowBucket(L"Ready To Apply") + CountAiWorkflowBucket(L"Ready To Bind") + CountAiWorkflowBucket(L"Ready To Preview") + CountAiWorkflowBucket(L"Diff Ready");
                    std::wstring workflow_queue_label = std::wstring(L"Workflow Queue… (") + std::to_wstring((unsigned long long)active_items) + L")";
                    std::wstring blocked_queue_label = std::wstring(L"Blocked Queue (") + std::to_wstring((unsigned long long)blocked_total) + L")";
                    std::wstring ready_queue_label = std::wstring(L"Ready Queue (") + std::to_wstring((unsigned long long)ready_total) + L")";
                    std::wstring focus_next_label = L"Focus Next Workflow…";
                    std::wstring do_next_label = L"Do Next Workflow…";
                    if (active_items > 0u) {
                        const int32_t top_idx = FindAiHighestPriorityChat();
                        if (top_idx >= 0) {
                            focus_next_label += std::wstring(L"  ") + BuildAiWorkflowBucketLeadText(BuildAiChatWorkflowBucketText((uint32_t)top_idx), false);
                            do_next_label += std::wstring(L"  ") + BuildAiWorkflowBucketLeadText(BuildAiChatWorkflowBucketText((uint32_t)top_idx), true);
                        }
                    }
                    std::wstring focus_blocked_label = L"Focus Next Blocked…";
                    std::wstring do_blocked_label = L"Do Next Blocked…";
                    const std::wstring blocked_lead_focus = BuildAiWorkflowBucketLeadText(L"Blocked", false);
                    const std::wstring blocked_lead_do = BuildAiWorkflowBucketLeadText(L"Blocked", true);
                    if (!blocked_lead_focus.empty()) focus_blocked_label += std::wstring(L"  ") + blocked_lead_focus;
                    if (!blocked_lead_do.empty()) do_blocked_label += std::wstring(L"  ") + blocked_lead_do;
                    std::wstring focus_ready_label = L"Focus Next Ready…";
                    std::wstring do_ready_label = L"Do Next Ready…";
                    std::wstring ready_focus_lead = BuildAiWorkflowBucketLeadText(L"Ready To Apply", false);
                    std::wstring ready_do_lead = BuildAiWorkflowBucketLeadText(L"Ready To Apply", true);
                    if (ready_focus_lead.empty()) ready_focus_lead = BuildAiWorkflowBucketLeadText(L"Ready To Bind", false);
                    if (ready_focus_lead.empty()) ready_focus_lead = BuildAiWorkflowBucketLeadText(L"Ready To Preview", false);
                    if (ready_focus_lead.empty()) ready_focus_lead = BuildAiWorkflowBucketLeadText(L"Diff Ready", false);
                    if (ready_do_lead.empty()) ready_do_lead = BuildAiWorkflowBucketLeadText(L"Ready To Bind", true);
                    if (ready_do_lead.empty()) ready_do_lead = BuildAiWorkflowBucketLeadText(L"Ready To Preview", true);
                    if (ready_do_lead.empty()) ready_do_lead = BuildAiWorkflowBucketLeadText(L"Diff Ready", true);
                    if (!ready_focus_lead.empty()) focus_ready_label += std::wstring(L"  ") + ready_focus_lead;
                    if (!ready_do_lead.empty()) do_ready_label += std::wstring(L"  ") + ready_do_lead;
                    AppendMenuW(menu, MF_STRING, 4816, workflow_queue_label.c_str());
                    AppendMenuW(menu, MF_STRING, 4814, focus_next_label.c_str());
                    AppendMenuW(menu, MF_STRING, 4815, do_next_label.c_str());
                    AppendMenuW(menu, MF_STRING, 4817, focus_blocked_label.c_str());
                    AppendMenuW(menu, MF_STRING, 4818, do_blocked_label.c_str());
                    AppendMenuW(menu, MF_STRING, 4819, focus_ready_label.c_str());
                    AppendMenuW(menu, MF_STRING, 4826, do_ready_label.c_str());
                    HMENU workflow_menu = CreatePopupMenu();
                    HMENU workflow_blocked_menu = CreatePopupMenu();
                    HMENU workflow_ready_menu = CreatePopupMenu();
                    uint32_t workflow_items = 0u;
                    uint32_t blocked_items = 0u;
                    uint32_t ready_items = 0u;
                    for (uint32_t idx : workflow_order) {
                        const uint32_t priority = BuildAiChatWorkflowPriorityScore(idx);
                        if (priority == 0u) continue;
                        const std::wstring bucket = BuildAiChatWorkflowBucketText(idx);
                        std::wstring label = BuildAiChatTabLabelText(idx) + L"  ->  " + BuildAiChatPrimaryActionLabel(idx);
                        const std::wstring why = BuildAiChatPrimaryActionReasonText(idx);
                        if (!why.empty()) label += std::wstring(L" · ") + why;
                        if (workflow_items < 5u) {
                            AppendMenuW(workflow_menu, MF_STRING, 4820u + workflow_items, label.c_str());
                            ++workflow_items;
                        }
                        if (bucket == L"Blocked" && blocked_items < 5u) {
                            AppendMenuW(workflow_blocked_menu, MF_STRING, 4830u + blocked_items, label.c_str());
                            ++blocked_items;
                        } else if ((bucket == L"Ready To Apply" || bucket == L"Ready To Bind" || bucket == L"Ready To Preview" || bucket == L"Diff Ready") && ready_items < 5u) {
                            AppendMenuW(workflow_ready_menu, MF_STRING, 4840u + ready_items, label.c_str());
                            ++ready_items;
                        }
                    }
                    if (workflow_items == 0u) AppendMenuW(workflow_menu, MF_STRING | MF_GRAYED, 4820, L"No active workflow items");
                    if (blocked_items == 0u) AppendMenuW(workflow_blocked_menu, MF_STRING | MF_GRAYED, 4830, L"No blocked workflow items");
                    if (ready_items == 0u) AppendMenuW(workflow_ready_menu, MF_STRING | MF_GRAYED, 4840, L"No ready workflow items");
                    AppendMenuW(menu, MF_POPUP, (UINT_PTR)workflow_menu, workflow_queue_label.c_str());
                    AppendMenuW(menu, MF_POPUP, (UINT_PTR)workflow_blocked_menu, blocked_queue_label.c_str());
                    AppendMenuW(menu, MF_POPUP, (UINT_PTR)workflow_ready_menu, ready_queue_label.c_str());
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                    // Stub hook only (no network layer enforced here yet).
                    AppendMenuW(menu, MF_STRING | (ai_safe_mode_enabled_ ? MF_CHECKED : 0), 4804, L"Safe Mode / No Network (hook)");

                    POINT pt{};
                    GetCursorPos(&pt);
                    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
                    DestroyMenu(menu);
                    if (cmd == 0) return 0;

                    if (cmd == 4801) {
                        ai_learning_enabled_ = !ai_learning_enabled_;
                        if (ai_learning_enabled_) ai_enabled_ = true;
                        send_toggle(EwControlPacketKind::AiSetEnabled, ai_enabled_);
                        send_toggle(EwControlPacketKind::AiSetLearning, ai_learning_enabled_);
                        if (hwnd_toggle_ai_) InvalidateRect(hwnd_toggle_ai_, nullptr, TRUE);
                        if (hwnd_toggle_learning_) InvalidateRect(hwnd_toggle_learning_, nullptr, TRUE);
                        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, ai_learning_enabled_ ? L"Learning enabled." : L"Learning disabled.");
                        return 0;
                    }
                    if (cmd == 4802) {
                        ai_crawling_enabled_ = !ai_crawling_enabled_;
                        if (ai_crawling_enabled_) ai_enabled_ = true;
                        send_toggle(EwControlPacketKind::AiSetEnabled, ai_enabled_);
                        send_toggle(EwControlPacketKind::AiSetCrawling, ai_crawling_enabled_);
                        if (hwnd_toggle_ai_) InvalidateRect(hwnd_toggle_ai_, nullptr, TRUE);
                        if (hwnd_toggle_crawling_) InvalidateRect(hwnd_toggle_crawling_, nullptr, TRUE);
                        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, ai_crawling_enabled_ ? L"Crawling enabled." : L"Crawling disabled.");
                        return 0;
                    }
                    if (cmd == 4803) {
                        ai_repo_reader_enabled_ = !ai_repo_reader_enabled_;
                        // Repo reader remains stage-gated in runtime; UI only flips the request.
                        if (scene_) {
                            scene_->SetRepoReaderEnabled(ai_repo_reader_enabled_);
                        }
                        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, ai_repo_reader_enabled_ ? L"Repo reader enabled." : L"Repo reader disabled.");
                        return 0;
                    }

                    if (cmd == 4811) {
                        AiPanelSetView(1u); if (hwnd_ai_panel_ && !IsWindowVisible(hwnd_ai_panel_)) ShowWindow(hwnd_ai_panel_, SW_SHOW); if (hwnd_ai_panel_) SetForegroundWindow(hwnd_ai_panel_);
                        return 0;
                    }

                    if (cmd == 4812) {
                        AiPanelSetView(2u); if (hwnd_ai_panel_ && !IsWindowVisible(hwnd_ai_panel_)) ShowWindow(hwnd_ai_panel_, SW_SHOW); if (hwnd_ai_panel_) SetForegroundWindow(hwnd_ai_panel_);
                        return 0;
                    }
                    if (cmd == 4813) {
                        const std::wstring overview = BuildAiWorkflowOverviewText();
                        EwAiTextViewDialog::RunModal(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, L"AI Workflow Overview", overview);
                        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Chat: AI workflow overview opened.");
                        return 0;
                    }
                    if (cmd == 4816) {
                        const std::wstring queue_text = BuildAiWorkflowQueueText();
                        EwAiTextViewDialog::RunModal(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, L"AI Workflow Queue", queue_text);
                        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, L"Chat: AI workflow queue opened.");
                        return 0;
                    }
                    if (cmd == 4814) {
                        FocusAiHighestPriorityChat(false);
                        return 0;
                    }
                    if (cmd == 4815) {
                        FocusAiHighestPriorityChat(true);
                        return 0;
                    }
                    if (cmd == 4817) {
                        FocusAiWorkflowBucket(L"Blocked", false, L"Chat: focus blocked ->");
                        return 0;
                    }
                    if (cmd == 4818) {
                        FocusAiWorkflowBucket(L"Blocked", true, L"Chat: do blocked ->");
                        return 0;
                    }
                    if (cmd == 4819) {
                        const std::vector<uint32_t> visible = BuildAiWorkflowBucketOrder(L"Ready To Apply", AI_CHAT_MAX);
                        if (!visible.empty()) { FocusAiWorkflowBucket(L"Ready To Apply", false, L"Chat: focus ready ->"); return 0; }
                        const std::vector<uint32_t> bindable = BuildAiWorkflowBucketOrder(L"Ready To Bind", AI_CHAT_MAX);
                        if (!bindable.empty()) { FocusAiWorkflowBucket(L"Ready To Bind", false, L"Chat: focus ready ->"); return 0; }
                        const std::vector<uint32_t> previewable = BuildAiWorkflowBucketOrder(L"Ready To Preview", AI_CHAT_MAX);
                        if (!previewable.empty()) { FocusAiWorkflowBucket(L"Ready To Preview", false, L"Chat: focus ready ->"); return 0; }
                        FocusAiWorkflowBucket(L"Diff Ready", false, L"Chat: focus ready ->");
                        return 0;
                    }
                    if (cmd == 4826) {
                        const std::vector<uint32_t> visible = BuildAiWorkflowBucketOrder(L"Ready To Apply", AI_CHAT_MAX);
                        if (!visible.empty()) { FocusAiWorkflowBucket(L"Ready To Apply", true, L"Chat: do ready ->"); return 0; }
                        const std::vector<uint32_t> bindable = BuildAiWorkflowBucketOrder(L"Ready To Bind", AI_CHAT_MAX);
                        if (!bindable.empty()) { FocusAiWorkflowBucket(L"Ready To Bind", true, L"Chat: do ready ->"); return 0; }
                        const std::vector<uint32_t> previewable = BuildAiWorkflowBucketOrder(L"Ready To Preview", AI_CHAT_MAX);
                        if (!previewable.empty()) { FocusAiWorkflowBucket(L"Ready To Preview", true, L"Chat: do ready ->"); return 0; }
                        FocusAiWorkflowBucket(L"Diff Ready", true, L"Chat: do ready ->");
                        return 0;
                    }
                    auto focus_queue_item = [&](const std::vector<uint32_t>& visible, uint32_t slot, const wchar_t* prefix_w) {
                        if (slot >= visible.size()) return;
                        const uint32_t idx = visible[slot];
                        ActivateAiChat(idx);
                        const int32_t rank = FindAiWorkflowRank(idx);
                        std::wstring status = std::wstring(prefix_w) + L" [" + std::to_wstring(idx + 1u) + L"] " + ai_chat_title_w_[idx];
                        if (rank > 0) status += std::wstring(L" · queue #") + std::to_wstring((long long)rank);
                        status += std::wstring(L" · bucket ") + BuildAiChatWorkflowBucketText(idx);
                        const std::wstring action = BuildAiChatPrimaryActionLabel(idx);
                        const std::wstring why = BuildAiChatPrimaryActionReasonText(idx);
                        if (!action.empty()) status += std::wstring(L" · ") + action;
                        if (!why.empty()) status += std::wstring(L" · ") + why;
                        SetAiChatWorkflowEvent(idx, std::wstring(L"Focused from workflow queue item: ") + BuildAiChatWorkflowBucketText(idx), false);
                        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, status.c_str());
                    };
                    if (cmd >= 4820 && cmd < 4825) {
                        const std::vector<uint32_t> workflow_order = BuildAiWorkflowPriorityOrder();
                        const uint32_t slot = (uint32_t)(cmd - 4820);
                        std::vector<uint32_t> visible;
                        visible.reserve(5u);
                        for (uint32_t idx : workflow_order) {
                            if (visible.size() >= 5u) break;
                            if (BuildAiChatWorkflowPriorityScore(idx) == 0u) continue;
                            visible.push_back(idx);
                        }
                        focus_queue_item(visible, slot, L"Chat: workflow queue focus ->");
                        return 0;
                    }
                    if (cmd >= 4830 && cmd < 4835) {
                        const uint32_t slot = (uint32_t)(cmd - 4830);
                        std::vector<uint32_t> visible;
                        visible.reserve(5u);
                        for (uint32_t idx : BuildAiWorkflowPriorityOrder()) {
                            if (visible.size() >= 5u) break;
                            if (BuildAiChatWorkflowPriorityScore(idx) == 0u) continue;
                            if (BuildAiChatWorkflowBucketText(idx) != L"Blocked") continue;
                            visible.push_back(idx);
                        }
                        focus_queue_item(visible, slot, L"Chat: blocked workflow focus ->");
                        return 0;
                    }
                    if (cmd >= 4840 && cmd < 4845) {
                        const uint32_t slot = (uint32_t)(cmd - 4840);
                        std::vector<uint32_t> visible;
                        visible.reserve(5u);
                        for (uint32_t idx : BuildAiWorkflowPriorityOrder()) {
                            if (visible.size() >= 5u) break;
                            if (BuildAiChatWorkflowPriorityScore(idx) == 0u) continue;
                            const std::wstring bucket = BuildAiChatWorkflowBucketText(idx);
                            if (!(bucket == L"Ready To Apply" || bucket == L"Ready To Bind" || bucket == L"Ready To Preview" || bucket == L"Diff Ready")) continue;
                            visible.push_back(idx);
                        }
                        focus_queue_item(visible, slot, L"Chat: ready workflow focus ->");
                        return 0;
                    }

                    if (cmd == 4804) {
                        ai_safe_mode_enabled_ = !ai_safe_mode_enabled_;
                        if (hwnd_ai_panel_tool_status_) SetWindowTextW(hwnd_ai_panel_tool_status_, ai_safe_mode_enabled_ ? L"Safe mode requested (hook only)." : L"Safe mode request cleared.");
                        return 0;
                    }
                    return 0;
                } else if (id == 4059) {
                    // AI chat send
                    if (!hwnd_ai_chat_input_ || !hwnd_ai_chat_list_) return 0;
                    wchar_t wbuf[2048];
                    GetWindowTextW(hwnd_ai_chat_input_, wbuf, 2048);
                    std::wstring ws(wbuf);
                    // Trim
                    while (!ws.empty() && (ws.back() == L'\r' || ws.back() == L'\n' || ws.back() == L' ' || ws.back() == L'\t')) ws.pop_back();
                    size_t lead = 0;
                    while (lead < ws.size() && (ws[lead] == L' ' || ws[lead] == L'\t' || ws[lead] == L'\r' || ws[lead] == L'\n')) ++lead;
                    if (lead > 0) ws.erase(0, lead);
                    if (ws.empty()) return 0;

                    const uint32_t chat_idx = ai_tab_index_u32_;
                    // Conversational chat remains free-form text.
                    // Control belongs to dedicated widgets, not slash-command parsing.
                    AiChatAppend(chat_idx, std::wstring(L"You: ") + ws);
                    AiChatRenderSelected();
                    SetWindowTextW(hwnd_ai_chat_input_, L"");

                    // Submit into substrate as conversational chat text.
                    // This bypasses app-surface command parsing so the chat box behaves like chat, not a terminal.
                    if (scene_) {
                        std::string utf8 = wide_to_utf8(ws);
                        scene_->SubmitAiChatLine(utf8, chat_idx, ai_chat_mode_u32_[chat_idx]);
                        RefreshAiChatCortex(chat_idx);
                    }
                    return 0;
                } else if (id == 4065) {
                    const uint32_t chat_idx = ai_tab_index_u32_;
                    std::wstring picked_dir;
                    if (ew_browse_for_directory(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, L"Link project to chat", picked_dir)) {
                        namespace fs = std::filesystem;
                        std::vector<std::string> rels_utf8;
                        std::error_code ec;
                        fs::path root = fs::path(wide_to_utf8(picked_dir));
                        if (fs::exists(root, ec) && fs::is_directory(root, ec)) {
                            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end && rels_utf8.size() < 128u; it.increment(ec)) {
                                if (ec) { ec.clear(); continue; }
                                if (!it->is_regular_file(ec)) continue;
                                std::error_code rc;
                                fs::path rel = fs::relative(it->path(), root, rc);
                                rels_utf8.push_back(rc ? it->path().filename().string() : rel.generic_string());
                            }
                        }
                        if (scene_) {
                            scene_->LinkAiChatProject(chat_idx, wide_to_utf8(picked_dir), rels_utf8);
                        }
                        ai_chat_project_root_w_[chat_idx] = picked_dir;
                        ai_chat_project_summary_w_[chat_idx] = L"Project: " + picked_dir + L" files=" + std::to_wstring((unsigned long long)rels_utf8.size());
                        SetAiChatWorkflowEvent(chat_idx, L"Project linked to chat", false);
                        AiChatAppend(chat_idx, L"(project linked into AI work substrate)");
                        RefreshAiChatCortex(chat_idx);
                        RefreshNodePanel();
                        AiChatRenderSelected();
                    }
                    return 0;
                } else if (id == 4061) {
                    // New chat (compose) in current folder
                    if (!hwnd_ai_tab_) return 0;
                    if (ai_chat_count_u32_ >= AI_CHAT_MAX) {
                        AiChatAppend(ai_tab_index_u32_, L"(max chats reached)");
                        AiChatRenderSelected();
                        return 0;
                    }
                    const uint32_t new_idx = ai_chat_count_u32_;
                    ai_chat_count_u32_++;
                    ai_chat_msgs_[new_idx].clear();
                    ai_chat_patch_w_[new_idx].clear();
                    ai_chat_patch_explain_w_[new_idx].clear();
                    ai_chat_apply_target_dir_w_[new_idx].clear();
                    ai_chat_last_detected_patch_w_[new_idx].clear();
                    ai_chat_last_detected_patch_valid_[new_idx] = false;
                    ai_chat_patch_previewed_[new_idx] = false;
                    ai_chat_mode_u32_[new_idx] = SubstrateManager::EW_CHAT_MEMORY_MODE_TALK;
                    ai_chat_cortex_summary_w_[new_idx].clear();
                    ai_chat_project_summary_w_[new_idx].clear();
                    ai_chat_project_root_w_[new_idx].clear();
                    ai_chat_patch_scope_root_w_[new_idx].clear();
                    ai_chat_patch_scope_file_count_u32_[new_idx] = 0u;
                    ai_chat_patch_meta_w_[new_idx].clear();
                    ai_chat_last_workflow_event_w_[new_idx].clear();
                    ai_chat_folder_of_u32_[new_idx] = ai_chat_folder_id_u32_;
                    wchar_t label[32];
                    _snwprintf(label, 32, L"Chat %u", (unsigned)(new_idx + 1u));
                    ai_chat_title_w_[new_idx] = label;
                    TCITEMW ti{};
                    ti.mask = TCIF_TEXT;
                    ti.pszText = label;
                    TabCtrl_InsertItem(hwnd_ai_tab_, (int)new_idx, &ti);
                    RefreshAiChatTabLabels();
                    TabCtrl_SetCurSel(hwnd_ai_tab_, (int)new_idx);
                    ai_tab_index_u32_ = new_idx;
                    AiChatAppend(new_idx, L"New chat started.");
                    RefreshAiPanelChrome();
                    AiChatRenderSelected();
                    return 0;
                } else if (id == 4062) {
                    // Apply patch (explicit approval required)
                    const uint32_t chat_idx = ai_tab_index_u32_;
                    AiChatApplyPatch(chat_idx);
                    return 0;
                } else if (id == 4064) {
                    // Preview patch (read-only)
                    const uint32_t chat_idx = ai_tab_index_u32_;
                    AiChatPreviewPatch(chat_idx);
                    return 0;
                } else if (id == 4063) {
                    // Use last detected Assistant patch (one-click)
                    const uint32_t chat_idx = ai_tab_index_u32_;
                    if (!BufferAiChatDetectedDiff(chat_idx, true)) {
                        AiChatAppend(chat_idx, L"(no Assistant diff detected yet)");
                    }
                    AiChatRenderSelected();
                    return 0;
                } else if (id == 4067) {
                    const uint32_t chat_idx = ai_tab_index_u32_;
                    ExecuteAiChatPrimaryAction(chat_idx);
                    AiChatRenderSelected();
                    return 0;
                }
            }

            if (id == 2045 && code == BN_CLICKED) {
                const uint32_t chat_idx = ai_tab_index_u32_;
                if (scene_) {
                    EwAiVaultBrowserDialog dlg = EwAiVaultBrowserDialog::RunModal(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, scene_);
                    if (dlg.import_requested && !dlg.imported_path.empty()) {
                        AiChatAppend(chat_idx, std::wstring(L"(vault handle imported: ") + dlg.imported_path + L")");
                        AiChatRenderSelected();
                        RefreshContentBrowserFromRuntime(200u);
                    }
                }
                return 0;
            }

            if (id == 2046 && code == BN_CLICKED) {
                ToggleAiPanel();
            }

            // Content browser refresh
            if (id == 1012 && code == BN_CLICKED) {
                RefreshContentBrowserFromRuntime(200u);
                return 0;
            }

            // Content search (simple substring filter; deterministic rebuild from cached list)
            if (id == 1011 && code == EN_CHANGE) {
                wchar_t buf[256];
                GetWindowTextW(hwnd_content_search_, buf, 256);
                content_search_utf8_ = wide_to_utf8(std::wstring(buf));
                RebuildContentBrowserViews();
                return 0;
            }
            if (id == 1014 && code == BN_CLICKED) { SetContentBrowserViewMode(0u); return 0; }
            if (id == 1015 && code == BN_CLICKED) { SetContentBrowserViewMode(1u); return 0; }
            if (id == 1016 && code == BN_CLICKED) { SetContentBrowserViewMode(2u); return 0; }
            if (id == 1018 && code == LBN_SELCHANGE && hwnd_content_3d_) {
                if (!content_selection_sync_guard_) {
                    const int sel = (int)SendMessageW(hwnd_content_3d_, LB_GETCURSEL, 0, 0);
                    if (sel >= 0) {
                        const LRESULT data = SendMessageW(hwnd_content_3d_, LB_GETITEMDATA, (WPARAM)sel, 0);
                        if (data >= 0 && (size_t)data < content_items_.size()) (void)SelectContentRelativePath(content_items_[(size_t)data].rel_utf8);
                    }
                }
                return 0;
            }
            if (id == 1019 && code == BN_CLICKED) {
                if (!content_selected_rel_utf8_.empty()) (void)ReviewReferencesForPath(content_selected_rel_utf8_, L"Content reference review");
                return 0;
            }

            // AI panel bell: jump to Experiments + clear badge.
            if (id == 4050 && code == BN_CLICKED) {
                if (hwnd_ai_panel_ && IsWindowVisible(hwnd_ai_panel_)) {
                    TabCtrl_SetCurSel(hwnd_ai_tab_, 1);
                    ai_tab_index_u32_ = 1u;
                    if (hwnd_ai_progress_overall_) ShowWindow(hwnd_ai_progress_overall_, SW_HIDE);
                    if (hwnd_ai_domain_list_) ShowWindow(hwnd_ai_domain_list_, SW_HIDE);
                    if (hwnd_ai_experiment_list_) ShowWindow(hwnd_ai_experiment_list_, SW_SHOW);
                    MarkAiExperimentsSeen();
                }
            }

            if (id == 4054 && code == LBN_DBLCLK) {
                // Open selected experiment file and mark as seen.
                int sel = (int)SendMessageW(hwnd_ai_experiment_list_, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    wchar_t item[512]; item[0] = 0;
                    SendMessageW(hwnd_ai_experiment_list_, LB_GETTEXT, (WPARAM)sel, (LPARAM)item);
                    std::wstring fname(item);
                    std::string asset_root_utf8 = scene_ ? scene_->runtime().project_settings.assets.project_asset_substrate_root_utf8 : std::string("AssetSubstrate");
                    if (asset_root_utf8.empty()) asset_root_utf8 = "AssetSubstrate";
                    std::wstring asset_root = utf8_to_wide(asset_root_utf8);
                    std::wstring full;
                    if (fname.rfind(L"(fail) ", 0) == 0) {
                        fname = fname.substr(7);
                        full = asset_root + L"\\AI\\experiments\\metrics_failures\\" + fname;
                    } else {
                        full = asset_root + L"\\AI\\experiments\\metrics\\" + fname;
                    }
                    // Let Windows pick the default handler (usually opens in the editor).
                    ShellExecuteW(hwnd, L"open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                MarkAiExperimentsSeen();
            }

            // Property grid in-place editor commit.
            if (id == 2601 && (code == EN_KILLFOCUS || code == EN_UPDATE)) {
                if (code == EN_KILLFOCUS) {
                    CommitPropEdit(true);
                    RebuildPropertyGrid();
                }
                return 0;
            }

            if (id == 2002 && code == BN_CLICKED) { OnSend(); }
            if (id == 2003 && code == BN_CLICKED) { OnImportObj(); }
            if (id == 2006 && code == BN_CLICKED) { OnBootstrapGame(); }
            if (id == 2060 && code == BN_CLICKED) { OnAiTrainModel(); return 0; }
            if (id == 2061 && code == BN_CLICKED) {
                if (content_selected_rel_utf8_.empty()) (void)SelectContentForSelectedObject();
                if (!content_selected_rel_utf8_.empty()) {
                    (void)ReviewReferencesForPath(content_selected_rel_utf8_, L"Asset reference review");
                } else {
                    AppendOutputUtf8("ASSET: no matched content path for reference review");
                }
                return 0;
            }
            if (id == 2066 && code == CBN_SELCHANGE) {
                RefreshAssetDesignerPanel();
                LayoutMain();
                return 0;
            }
            if ((id == 2073 && code == CBN_SELCHANGE)) {
                RefreshAssetDesignerPanel();
                return 0;
            }
            auto submit_axis = [&](uint32_t axis_id_u32, int value_q16_16)->void {
                if (!scene_) return;
                EwControlPacket cp{};
                cp.source_u16 = 1;
                cp.tick_u64 = scene_->sm.canonical_tick;
                cp.kind = EwControlPacketKind::InputAxis;
                cp.payload.input_axis.axis_id_u32 = axis_id_u32;
                cp.payload.input_axis.value_q16_16 = value_q16_16;
                (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
            };
            auto submit_action = [&](uint32_t action_id_u32)->void {
                if (!scene_) return;
                EwControlPacket cp{};
                cp.source_u16 = 1;
                cp.tick_u64 = scene_->sm.canonical_tick;
                cp.kind = EwControlPacketKind::InputAction;
                cp.payload.input_action.action_id_u32 = action_id_u32;
                cp.payload.input_action.pressed_u8 = 1u;
                (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
            };
            if (id == 2070 && code == BN_CLICKED) {
                if (content_selected_rel_utf8_.empty()) (void)SelectContentForSelectedObject();
                if (content_selected_rel_utf8_.empty() || asset_last_review_rel_utf8_ != content_selected_rel_utf8_ || asset_last_review_revision_u64_ != coh_highlight_seen_revision_u64_) {
                    if (!content_selected_rel_utf8_.empty()) (void)ReviewReferencesForPath(content_selected_rel_utf8_, L"Planet apply review required");
                    if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Planet Apply paused until coherence/reference review is current for the linked content target.");
                    RefreshAssetDesignerPanel();
                    return 0;
                }
                const int atmo = hwnd_asset_planet_atmo_ ? (int)SendMessageW(hwnd_asset_planet_atmo_, TBM_GETPOS, 0, 0) : 0;
                const int iono = hwnd_asset_planet_iono_ ? (int)SendMessageW(hwnd_asset_planet_iono_, TBM_GETPOS, 0, 0) : 0;
                const int magneto = hwnd_asset_planet_magneto_ ? (int)SendMessageW(hwnd_asset_planet_magneto_, TBM_GETPOS, 0, 0) : 0;
                if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
                    auto& o = scene_->objects[(size_t)scene_->selected];
                    const float atmo_scale = (float)atmo / 100.0f;
                    const float iono_scale = (float)iono / 100.0f;
                    o.atmosphere_thickness_m_f32 = std::max(0.0f, o.radius_m_f32 * atmo_scale * 0.25f);
                    o.emissive_f32 = std::max(o.emissive_f32, iono_scale * 1.5f);
                    const uint32_t alpha = (uint32_t)std::max(24, std::min(220, 24 + magneto * 2));
                    o.atmosphere_rgba8 = (alpha << 24) | 0x00A060FFu;
                    o.refresh_fixed_cache();
                }
                submit_axis(7101u, atmo * 655);
                submit_axis(7102u, iono * 655);
                submit_axis(7103u, magneto * 655);
                submit_action(7104u);
                resonance_view_ = true;
                if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Planet builder applied atmosphere/ionosphere/magnetosphere hooks to the selected anchor.");
                RefreshAssetDesignerPanel();
                RefreshViewportResonanceOverlay();
                InvalidateRect(hwnd_viewport_, nullptr, FALSE);
                return 0;
            }
            if (id == 2071 && code == BN_CLICKED) {
                submit_action(7105u);
                rdock_tab_index_u32_ = 3u; TabCtrl_SetCurSel(hwnd_rdock_tab_, 3); ApplyRightDockVisibility();
                if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Planet sculpt hook armed through the canonical voxel/asset lane.");
                RefreshAssetDesignerPanel();
                return 0;
            }
            if (id == 2072 && code == BN_CLICKED) {
                submit_action(7106u);
                resonance_view_ = true;
                if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Planet paint hook armed and resonance viewport enabled for layer review.");
                RefreshAssetDesignerPanel();
                RefreshViewportResonanceOverlay();
                InvalidateRect(hwnd_viewport_, nullptr, FALSE);
                return 0;
            }
            if (id == 2077 && code == BN_CLICKED) {
                if (content_selected_rel_utf8_.empty()) (void)SelectContentForSelectedObject();
                if (content_selected_rel_utf8_.empty() || asset_last_review_rel_utf8_ != content_selected_rel_utf8_ || asset_last_review_revision_u64_ != coh_highlight_seen_revision_u64_) {
                    if (!content_selected_rel_utf8_.empty()) (void)ReviewReferencesForPath(content_selected_rel_utf8_, L"Character bind review required");
                    if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Character Bind paused until coherence/reference review is current for the linked content target.");
                    RefreshAssetDesignerPanel();
                    return 0;
                }
                const int h_cm = hwnd_asset_character_height_ ? (int)SendMessageW(hwnd_asset_character_height_, TBM_GETPOS, 0, 0) : 172;
                const int rigidity = hwnd_asset_character_rigidity_ ? (int)SendMessageW(hwnd_asset_character_rigidity_, TBM_GETPOS, 0, 0) : 54;
                if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
                    auto& o = scene_->objects[(size_t)scene_->selected];
                    o.radius_m_f32 = std::max(0.35f, (float)h_cm / 200.0f);
                    o.emissive_f32 = std::max(o.emissive_f32, (float)rigidity * 0.01f);
                    o.refresh_fixed_cache();
                }
                submit_axis(7111u, h_cm * 655);
                submit_axis(7112u, rigidity * 655);
                submit_action(7113u);
                if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Character bind hook emitted bounded packets and updated the selected proxy bounds.");
                RefreshAssetDesignerPanel();
                return 0;
            }
            if (id == 2078 && code == BN_CLICKED) {
                const int gait = hwnd_asset_character_gait_ ? (int)SendMessageW(hwnd_asset_character_gait_, TBM_GETPOS, 0, 0) : 48;
                submit_axis(7114u, gait * 655);
                submit_action(7115u);
                if (hwnd_ai_status_) SetWindowTextW(hwnd_ai_status_, L"Character pose hook emitted a bounded locomotion/pose request for the selected anchor.");
                RefreshAssetDesignerPanel();
                return 0;
            }

            if (id == 2013 && code == BN_CLICKED) { OnApplyTransform(); }
            if (id == 2020 && code == BN_CLICKED) {
                editor_gizmo_mode_u8_ = 1;
                EmitEditorGizmo();
                AppendOutputUtf8("EDITOR: gizmo=Translate");
                RebuildPropertyGrid();
            }
            if (id == 2021 && code == BN_CLICKED) {
                editor_gizmo_mode_u8_ = 2;
                EmitEditorGizmo();
                AppendOutputUtf8("EDITOR: gizmo=Rotate");
                RebuildPropertyGrid();
            }
            if (id == 2022 && code == BN_CLICKED) {
                // Frame selection handled in Tick() using current cached object state.
                // We set a one-shot key to reuse the existing framing logic (F key).
                input_.key_down['F'] = true;

if (id == 2026 && code == BN_CLICKED) {
    editor_axis_constraint_u8_ = 0;
    EmitEditorAxisConstraint();
    AppendOutputUtf8("EDITOR: axis=None");
    RebuildPropertyGrid();
}
if (id == 2027 && code == BN_CLICKED) {
    editor_axis_constraint_u8_ = 1;
    EmitEditorAxisConstraint();
    AppendOutputUtf8("EDITOR: axis=X");
    RebuildPropertyGrid();
}
if (id == 2028 && code == BN_CLICKED) {
    editor_axis_constraint_u8_ = 2;
    EmitEditorAxisConstraint();
    AppendOutputUtf8("EDITOR: axis=Y");
    RebuildPropertyGrid();
}
if (id == 2029 && code == BN_CLICKED) {
    editor_axis_constraint_u8_ = 3;
    EmitEditorAxisConstraint();
    AppendOutputUtf8("EDITOR: axis=Z");
    RebuildPropertyGrid();
}
if (id == 2030 && code == BN_CLICKED) { EmitEditorUndo(); RefreshEditorHistoryUi(); AppendOutputUtf8("EDITOR: undo"); }
if (id == 2031 && code == BN_CLICKED) { EmitEditorRedo(); RefreshEditorHistoryUi(); AppendOutputUtf8("EDITOR: redo"); }

            }
            if (id == 2023 && code == BN_CLICKED) {
                // Snap toggle may be driven by either the legacy checkbox (if present) or the property grid.
                if (hwnd_snap_enable_) {
                    const LRESULT v = SendMessageW(hwnd_snap_enable_, BM_GETCHECK, 0, 0);
                    editor_snap_enabled_u8_ = (v == BST_CHECKED) ? 1 : 0;
                } else {
                    editor_snap_enabled_u8_ = (editor_snap_enabled_u8_ ? 0 : 1);
                }
                EmitEditorSnap();
                RebuildPropertyGrid();
            }
            if ((id == 2024 || id == 2025) && code == EN_KILLFOCUS) {
                // Parse snap config edits and emit.
                auto read_f32 = [&](HWND h)->float {
                    wchar_t buf[128]; buf[0] = 0;
                    GetWindowTextW(h, buf, 128);
                    wchar_t* endp = nullptr;
                    double v = wcstod(buf, &endp);
                    if (!endp) v = 0.0;
                    return (float)v;
                };
                float grid = read_f32(hwnd_grid_step_);
                float ang = read_f32(hwnd_angle_step_);
                if (grid < 0.0001f) grid = 0.0001f;
                if (ang < 0.1f) ang = 0.1f;
                editor_grid_step_m_q16_16_ = (int32_t)llround((double)grid * 65536.0);
                editor_angle_step_deg_q16_16_ = (int32_t)llround((double)ang * 65536.0);
                EmitEditorSnap();
            }
            if (id == 2004 && code == LBN_SELCHANGE) {
                int sel = (int)SendMessageW(hwnd_objlist_, LB_GETCURSEL, 0, 0);
                int orig = sel;
                if (sel >= 0) {
                    orig = (int)SendMessageW(hwnd_objlist_, LB_GETITEMDATA, (WPARAM)sel, 0);
                    if (orig == LB_ERR) orig = sel;
                }
                if (scene_) scene_->selected = orig;
                // Populate position fields from last-known projected state.
                if (scene_ && orig >= 0 && orig < (int)scene_->objects.size()) {
                    const auto& o = scene_->objects[(size_t)orig];
                    editor_selected_object_id_u64_ = o.object_id_u64;
                    EmitEditorSelection(editor_selected_object_id_u64_);
                    auto fmt = [&](int32_t q)->std::wstring {
                        wchar_t b[64];
                        const double v = (double)q / 65536.0;
                        swprintf(b, 64, L"%.6f", v);
                        return std::wstring(b);
                    };
                    SetWindowTextW(hwnd_posx_, fmt(o.pos_q16_16[0]).c_str());
                    SetWindowTextW(hwnd_posy_, fmt(o.pos_q16_16[1]).c_str());
                    SetWindowTextW(hwnd_posz_, fmt(o.pos_q16_16[2]).c_str());
                    RebuildPropertyGrid();
                }
                RefreshAssetDesignerPanel();
                RefreshVoxelDesignerPanel();
            }
        } break;
        case WM_HSCROLL: {
            HWND hscroll = (HWND)lparam;
            if (hscroll == GetDlgItem(hwnd_rdock_voxel_, 2601) || hscroll == GetDlgItem(hwnd_rdock_voxel_, 2602) || hscroll == GetDlgItem(hwnd_rdock_voxel_, 2603)) {
                RefreshVoxelDesignerPanel();
                return 0;
            }
            if (hscroll == hwnd_asset_planet_atmo_ || hscroll == hwnd_asset_planet_iono_ || hscroll == hwnd_asset_planet_magneto_ ||
                hscroll == hwnd_asset_character_height_ || hscroll == hwnd_asset_character_rigidity_ || hscroll == hwnd_asset_character_gait_) {
                RefreshAssetDesignerPanel();
                return 0;
            }
        } break;
        case WM_NOTIFY: {
            const NMHDR* nh = (const NMHDR*)lparam;
            if (nh && nh->code == NM_CUSTOMDRAW) {
                // Tabs and list controls use deterministic custom-draw for theme + coherence highlights.
                if (nh->hwndFrom == hwnd_rdock_tab_ || nh->hwndFrom == hwnd_ai_tab_) {
                    LRESULT res = 0;
                    if (ew_tab_custom_draw(nh, &res)) return res;
                }
                if (nh->hwndFrom == hwnd_content_list_) {
                    const NMLVCUSTOMDRAW* cd = (const NMLVCUSTOMDRAW*)lparam;
                    switch (cd->nmcd.dwDrawStage) {
                        case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
                        case CDDS_ITEMPREPAINT: {
                            const int idx_item = (int)cd->nmcd.dwItemSpec;
                            if (idx_item >= 0 && (size_t)idx_item < content_visible_indices_.size()) {
                                const size_t src = content_visible_indices_[(size_t)idx_item];
                                if (src >= content_items_.size()) return CDRF_DODEFAULT;
                                // Match against derived-only highlight set (paths).
                                const std::wstring w = utf8_to_wide(content_items_[src].rel_utf8);
                                if (coh_highlight_set_w_.find(w) != coh_highlight_set_w_.end()) {
                                    // Dark-gold tint under white text.
                                    ((NMLVCUSTOMDRAW*)cd)->clrTextBk = RGB(25, 20, 0);
                                    ((NMLVCUSTOMDRAW*)cd)->clrText   = RGB(255, 255, 255);
                                }
                            }
                            return CDRF_DODEFAULT;
                        }
                        default: break;
                    }
                }
                if (nh->hwndFrom == hwnd_content_thumb_) {
                    const NMLVCUSTOMDRAW* cd = (const NMLVCUSTOMDRAW*)lparam;
                    switch (cd->nmcd.dwDrawStage) {
                        case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
                        case CDDS_ITEMPREPAINT: {
                            const int idx_item = (int)cd->nmcd.dwItemSpec;
                            if (idx_item >= 0 && (size_t)idx_item < content_visible_indices_.size()) {
                                const size_t src = content_visible_indices_[(size_t)idx_item];
                                if (src >= content_items_.size()) return CDRF_DODEFAULT;
                                const std::wstring w = utf8_to_wide(content_items_[src].rel_utf8);
                                if (coh_highlight_set_w_.find(w) != coh_highlight_set_w_.end()) {
                                    ((NMLVCUSTOMDRAW*)cd)->clrTextBk = RGB(25, 20, 0);
                                    ((NMLVCUSTOMDRAW*)cd)->clrText   = RGB(255, 255, 255);
                                }
                            }
                            return CDRF_DODEFAULT;
                        }
                        default: break;
                    }
                }

            }
            if (nh && hwnd_rdock_tab_ && nh->hwndFrom == hwnd_rdock_tab_ && nh->code == TCN_SELCHANGE) {
                const int sel = TabCtrl_GetCurSel(hwnd_rdock_tab_);
                if (sel >= 0) {
                    if (!rdock_panel_visible_[sel]) {
                        const int fallback = ResolveNextVisibleDockTab((int)rdock_tab_index_u32_);
                        if (fallback >= 0) TabCtrl_SetCurSel(hwnd_rdock_tab_, fallback);
                    } else {
                        rdock_tab_index_u32_ = (uint32_t)sel;
                    }
                    ApplyRightDockVisibility();
                    RefreshNodePanel();
                    RefreshSequencerPanel();
                    LayoutChildren(client_w_, client_h_);
                }
                return 0;
            }
            if (nh && hwnd_ai_tab_ && nh->hwndFrom == hwnd_ai_tab_ && nh->code == TCN_SELCHANGE) {
                const int sel = TabCtrl_GetCurSel(hwnd_ai_tab_);
                if (sel >= 0) {
                    ai_tab_index_u32_ = (uint32_t)sel;
                    // Chat tabs: the message list is shared; selection is tracked.
                    SyncAiChatWorkflowState(ai_tab_index_u32_);
                    AiChatRenderSelected();
                }
                return 0;
            }
            if (nh && hwnd_ai_tab_ && nh->hwndFrom == hwnd_ai_tab_ && nh->code == NM_DBLCLK) {
                POINT pt{};
                GetCursorPos(&pt);
                POINT pt_client = pt;
                ScreenToClient(hwnd_ai_tab_, &pt_client);
                TCHITTESTINFO ht{};
                ht.pt = pt_client;
                const int hit = TabCtrl_HitTest(hwnd_ai_tab_, &ht);
                if (hit >= 0) {
                    ExecuteAiChatPrimaryAction((uint32_t)hit);
                    return 0;
                }
            }
            if (nh && hwnd_ai_tab_ && nh->hwndFrom == hwnd_ai_tab_ && nh->code == NM_CLICK) {
                if ((GetKeyState(VK_MBUTTON) & 0x8000) != 0) {
                    POINT pt{};
                    GetCursorPos(&pt);
                    POINT pt_client = pt;
                    ScreenToClient(hwnd_ai_tab_, &pt_client);
                    TCHITTESTINFO ht{};
                    ht.pt = pt_client;
                    const int hit = TabCtrl_HitTest(hwnd_ai_tab_, &ht);
                    if (hit >= 0 && ai_chat_count_u32_ > 1u) {
                        AiChatClose((uint32_t)hit);
                        return 0;
                    }
                }
            }
            if (nh && hwnd_ai_tab_ && nh->hwndFrom == hwnd_ai_tab_ && nh->code == NM_RCLICK) {
                // Per-chat workflow context menu.
                POINT pt{};
                GetCursorPos(&pt);
                POINT pt_client = pt;
                ScreenToClient(hwnd_ai_tab_, &pt_client);

                TCHITTESTINFO ht{};
                ht.pt = pt_client;
                const int hit = TabCtrl_HitTest(hwnd_ai_tab_, &ht);
                if (hit < 0) return 0;
                const uint32_t chat_idx = (uint32_t)hit;

                const bool has_patch = !ai_chat_patch_w_[chat_idx].empty();
                const bool has_scope = !ai_chat_patch_explain_w_[chat_idx].empty() || !ai_chat_patch_meta_w_[chat_idx].empty() || !ai_chat_last_detected_patch_w_[chat_idx].empty();
                const bool previewed = ai_chat_patch_previewed_[chat_idx];
                const bool blocking_warn = HasBlockingAiChatPatchWarnings(chat_idx, nullptr);
                const bool preview_unbound = (BuildAiChatPatchWarningMask(chat_idx, nullptr) & EW_AI_PATCH_WARN_PREVIEW_UNBOUND) != 0u;
                const std::wstring warning_headline = BuildAiChatPatchWarningHeadline(chat_idx, nullptr);
                const std::wstring next_action = BuildAiChatPatchNextActionText(chat_idx, nullptr);
                std::wstring apply_label = L"Apply";
                if (blocking_warn) apply_label = L"Re-Preview";
                else if (preview_unbound) apply_label = L"Bind Target";
                else if (has_patch && previewed) apply_label = L"Apply Now";

                HMENU menu = CreatePopupMenu();
                std::wstring title = ai_chat_title_w_[chat_idx].empty() ? (L"Chat " + std::to_wstring((unsigned long long)(chat_idx + 1u))) : ai_chat_title_w_[chat_idx];
                AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, title.c_str());
                if (!warning_headline.empty()) {
                    std::wstring head = L"Warning: " + warning_headline;
                    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, head.c_str());
                }
                if (!next_action.empty()) {
                    std::wstring next = L"Next: " + next_action;
                    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, next.c_str());
                }
                if (!ai_chat_last_workflow_event_w_[chat_idx].empty()) {
                    std::wstring last = L"Last: " + ai_chat_last_workflow_event_w_[chat_idx];
                    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, last.c_str());
                }
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                const std::wstring primary_action = BuildAiChatPrimaryActionLabel(chat_idx);
                AppendMenuW(menu, MF_STRING, 4899, (L"Do Next: " + primary_action).c_str());
                AppendMenuW(menu, MF_STRING, 4900, L"Open Chat");
                AppendMenuW(menu, MF_STRING | (has_scope ? 0 : MF_GRAYED), 4903, has_patch ? L"Patch/Scope..." : L"Scope...");
                AppendMenuW(menu, MF_STRING | (has_patch ? 0 : MF_GRAYED), 4904, L"Preview...");
                AppendMenuW(menu, MF_STRING | ((has_patch && previewed && !blocking_warn) ? 0 : MF_GRAYED), 4905, apply_label.c_str());
                AppendMenuW(menu, MF_STRING, 4906, L"Link Project...");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, 4901, L"Rename");
                AppendMenuW(menu, MF_STRING | ((ai_chat_count_u32_ <= 1u) ? MF_GRAYED : 0), 4902, L"Close");
                const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                if (cmd == 0) return 0;

                if (cmd == 4899) {
                    ExecuteAiChatPrimaryAction(chat_idx);
                    return 0;
                }
                if (cmd == 4900) {
                    ActivateAiChat(chat_idx);
                    return 0;
                }
                if (cmd == 4903) {
                    ActivateAiChat(chat_idx);
                    AiChatShowPatchView(chat_idx, nullptr);
                    return 0;
                }
                if (cmd == 4904) {
                    ActivateAiChat(chat_idx);
                    AiChatPreviewPatch(chat_idx);
                    return 0;
                }
                if (cmd == 4905) {
                    ActivateAiChat(chat_idx);
                    AiChatApplyPatch(chat_idx);
                    return 0;
                }
                if (cmd == 4906) {
                    ActivateAiChat(chat_idx);
                    std::wstring picked_dir;
                    if (ew_browse_for_directory(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, L"Link project to chat", picked_dir)) {
                        namespace fs = std::filesystem;
                        std::vector<std::string> rels_utf8;
                        std::error_code ec;
                        fs::path root = fs::path(wide_to_utf8(picked_dir));
                        if (fs::exists(root, ec) && fs::is_directory(root, ec)) {
                            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end && rels_utf8.size() < 128u; it.increment(ec)) {
                                if (ec) { ec.clear(); continue; }
                                if (!it->is_regular_file(ec)) continue;
                                std::error_code rc;
                                fs::path rel = fs::relative(it->path(), root, rc);
                                rels_utf8.push_back(rc ? it->path().filename().string() : rel.generic_string());
                            }
                        }
                        if (scene_) {
                            scene_->LinkAiChatProject(chat_idx, wide_to_utf8(picked_dir), rels_utf8);
                        }
                        ai_chat_project_root_w_[chat_idx] = picked_dir;
                        ai_chat_project_summary_w_[chat_idx] = L"Project: " + picked_dir + L" files=" + std::to_wstring((unsigned long long)rels_utf8.size());
                        SetAiChatWorkflowEvent(chat_idx, L"Project linked to chat", false);
                        AiChatAppend(chat_idx, L"(project linked into AI work substrate)");
                        RefreshAiChatCortex(chat_idx);
                        RefreshNodePanel();
                        AiChatRenderSelected();
                    }
                    return 0;
                }
                if (cmd == 4901) {
                    std::wstring initial = ai_chat_title_w_[chat_idx];
                    if (initial.empty()) {
                        wchar_t label[32];
                        _snwprintf(label, 32, L"Chat %u", (unsigned)(chat_idx + 1u));
                        initial = label;
                    }
                    EwAiRenameChatDialog dlg = EwAiRenameChatDialog::RunModal(hwnd_ai_panel_ ? hwnd_ai_panel_ : hwnd_main_, initial);
                    if (dlg.accepted) {
                        AiChatRename(chat_idx, dlg.result);
                    }
                    return 0;
                }
                if (cmd == 4902) {
                    AiChatClose(chat_idx);
                    return 0;
                }
                return 0;
            }
            // Details property grid interactions
            if (nh && hwnd_propgrid_ && nh->hwndFrom == hwnd_propgrid_) {
                if (nh->code == NM_DBLCLK) {
                    const NMITEMACTIVATE* ia = (const NMITEMACTIVATE*)lparam;
                    if (ia && ia->iItem >= 0) BeginPropEdit(ia->iItem, 1);
                    return 0;
                }
                if (nh->code == LVN_KEYDOWN) {
                    const NMLVKEYDOWN* kd = (const NMLVKEYDOWN*)lparam;
                    if (kd && (kd->wVKey == VK_F2 || kd->wVKey == VK_RETURN)) {
                        int sel = ListView_GetNextItem(hwnd_propgrid_, -1, LVNI_SELECTED);
                        if (sel >= 0) BeginPropEdit(sel, 1);
                    }
                    return 0;
                }
                if (nh->code == LVN_ITEMCHANGED) {
                    const NMLISTVIEW* lv = (const NMLISTVIEW*)lparam;
                    if (lv && (lv->uChanged & LVIF_STATE)) {
                        wchar_t name[128]; name[0]=0;
                        ListView_GetItemText(hwnd_propgrid_, lv->iItem, 0, name, 128);
                        if (wcscmp(name, L"Snap Enabled") == 0) {
                            const BOOL checked = ListView_GetCheckState(hwnd_propgrid_, lv->iItem);
                            editor_snap_enabled_u8_ = checked ? 1 : 0;
                            EmitEditorSnap();
                            AppendOutputUtf8(checked ? "EDITOR: snap=On" : "EDITOR: snap=Off");
                            // Also refresh the value cell for consistency.
                            ListView_SetItemText(hwnd_propgrid_, lv->iItem, 1, (LPWSTR)(checked ? L"On" : L"Off"));
                        }
                    }
                    return 0;
                }
            }

            // Content browser selection sync between List and Thumb surfaces.
            if (nh && (nh->hwndFrom == hwnd_content_list_ || nh->hwndFrom == hwnd_content_thumb_)) {
                if (nh->code == LVN_ITEMCHANGED) {
                    const NMLISTVIEW* lv = (const NMLISTVIEW*)lparam;
                    if (!lv) return 0;
                    if (content_selection_sync_guard_) return 0;
                    if ((lv->uChanged & LVIF_STATE) == 0) return 0;
                    const bool was_sel = (lv->uOldState & LVIS_SELECTED) != 0;
                    const bool now_sel = (lv->uNewState & LVIS_SELECTED) != 0;
                    if (was_sel == now_sel) return 0;
                    if (!now_sel) return 0; // we sync only on selection, not deselection.

                    wchar_t wbuf[512]; wbuf[0] = 0;
                    ListView_GetItemText(nh->hwndFrom, lv->iItem, 0, wbuf, 512);
                    std::string rel = wide_to_utf8(std::wstring(wbuf));
                    if (rel.empty()) return 0;
                    content_selected_rel_utf8_ = rel;
                    (void)SelectContentRelativePath(rel);
                    return 0;
                }


                // Right-click context menu for Content Browser items.
                if (nh->code == NM_RCLICK) {
                    // Ensure the clicked row becomes selected for predictable actions.
                    POINT pt{};
                    GetCursorPos(&pt);
                    POINT pt_client = pt;
                    ScreenToClient(nh->hwndFrom, &pt_client);
                    LVHITTESTINFO ht{};
                    ht.pt = pt_client;
                    const int hit = ListView_HitTest(nh->hwndFrom, &ht);
                    if (hit >= 0) {
                        content_selection_sync_guard_ = true;
                        const int n = ListView_GetItemCount(nh->hwndFrom);
                        for (int i = 0; i < n; ++i) ListView_SetItemState(nh->hwndFrom, i, 0, LVIS_SELECTED);
                        ListView_SetItemState(nh->hwndFrom, hit, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                        ListView_EnsureVisible(nh->hwndFrom, hit, FALSE);
                        content_selection_sync_guard_ = false;
                    }

                    int sel = ListView_GetNextItem(nh->hwndFrom, -1, LVNI_SELECTED);
                    if (sel < 0) return 0;

                    wchar_t wrel[512]; wrel[0] = 0;
                    ListView_GetItemText(nh->hwndFrom, sel, 0, wrel, 512);
                    std::string rel_utf8 = wide_to_utf8(std::wstring(wrel));
                    if (rel_utf8.empty()) return 0;

                    // Basename (display convenience only).
                    std::string name_utf8 = rel_utf8;
                    {
                        size_t slash = name_utf8.find_last_of("/\\");
                        if (slash != std::string::npos) name_utf8 = name_utf8.substr(slash + 1);
                    }

                    HMENU menu = CreatePopupMenu();
                    if (!menu) return 0;
                    AppendMenuW(menu, MF_STRING, 5601, L"Copy path");
                    AppendMenuW(menu, MF_STRING, 5602, L"Copy name");
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(menu, MF_STRING, 5603, L"Highlight in coherence");
                    AppendMenuW(menu, MF_STRING, 5604, L"Review references");

                    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_main_, nullptr);
                    DestroyMenu(menu);

                    if (cmd == 5601) {
                        ew_clip_set_text_utf16(hwnd_main_, utf8_to_wide(rel_utf8));
                        AppendOutputUtf8("CONTENT: copied path");
                        return 0;
                    }
                    if (cmd == 5602) {
                        ew_clip_set_text_utf16(hwnd_main_, utf8_to_wide(name_utf8));
                        AppendOutputUtf8("CONTENT: copied name");
                        return 0;
                    }
                    if (cmd == 5603) {
                        if (scene_) {
                            scene_->SetCoherenceHighlightPath(rel_utf8);
                            (void)SelectContentRelativePath(rel_utf8);
                            AppendOutputUtf8("COH: highlight requested");
                        }
                        return 0;
                    }
                    if (cmd == 5604) {
                        SendMessageW(hwnd_main_, WM_COMMAND, MAKEWPARAM(1019, BN_CLICKED), 0);
                        return 0;
                    }
                    return 0;
                }
            }

        } break;
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lparam;
            if (!mis) break;
            if ((int)mis->CtlID == 1018) { mis->itemHeight = 54; return TRUE; }
            if ((int)mis->CtlID == 4060) {
                if (!hwnd_ai_chat_list_) { mis->itemHeight = 26; return TRUE; }
                // Bubble height depends on wrapped text.
                RECT rc{};
                GetClientRect(hwnd_ai_chat_list_, &rc);
                wchar_t wbuf[4096];
                wbuf[0] = 0;
                if (mis->itemID != (UINT)-1) {
                    SendMessageW(hwnd_ai_chat_list_, LB_GETTEXT, mis->itemID, (LPARAM)wbuf);
                }
                std::wstring s(wbuf);
                mis->itemHeight = (UINT)ew_ai_chat_measure_item_height(hwnd_ai_chat_list_, s, (rc.right - rc.left));
                return TRUE;
            }
        } break;
        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* dis = (const DRAWITEMSTRUCT*)lparam;
            if (!dis) break;
            const int id = (int)dis->CtlID;
            if (id == 2040) { ew_draw_toggle_switch(dis, sim_play_enabled_); return TRUE; }
            if (id == 2041) { ew_draw_toggle_switch(dis, ai_enabled_); return TRUE; }
            if (id == 2042) { ew_draw_toggle_switch(dis, ai_learning_enabled_); return TRUE; }
            if (id == 2043) { ew_draw_toggle_switch(dis, ai_crawling_enabled_); return TRUE; }
            if (id == 4050) { ew_draw_bell_badge(dis, ai_unseen_experiments_u32_); return TRUE; }
            if (id == 4054) { ew_draw_menu_ellipsis_button(dis); return TRUE; }
            if (id == 4055) { ew_draw_toggle_switch(dis, ai_learning_enabled_); return TRUE; }
            if (id == 4056) { ew_draw_toggle_switch(dis, ai_crawling_enabled_); return TRUE; }
            if (id == 4861) { ew_draw_ai_mode_button(dis, L"Chat", ai_panel_view_u32_ == 0u); return TRUE; }
            if (id == 4862) { ew_draw_ai_mode_button(dis, L"Repository", ai_panel_view_u32_ == 1u); return TRUE; }
            if (id == 4863) { ew_draw_ai_mode_button(dis, L"Coherence", ai_panel_view_u32_ == 2u); return TRUE; }
            if (id == 4898) { ew_draw_ai_mode_button(dis, L"Talk", ai_chat_mode_u32_[ai_tab_index_u32_] == SubstrateManager::EW_CHAT_MEMORY_MODE_TALK); return TRUE; }
            if (id == 4899) { ew_draw_ai_mode_button(dis, L"Code", ai_chat_mode_u32_[ai_tab_index_u32_] == SubstrateManager::EW_CHAT_MEMORY_MODE_CODE); return TRUE; }
            if (id == 4900) { ew_draw_ai_mode_button(dis, L"Sim", ai_chat_mode_u32_[ai_tab_index_u32_] == SubstrateManager::EW_CHAT_MEMORY_MODE_SIM); return TRUE; }
            if (id == 4065) { ew_draw_ai_mode_button(dis, L"+", false); return TRUE; }
            if (id == 1018 && dis->hwndItem == hwnd_content_3d_) {
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                const bool sel = (dis->itemState & ODS_SELECTED) != 0;
                COLORREF bg = sel ? RGB(78, 60, 12) : RGB(10, 10, 10);
                COLORREF border = RGB(160, 120, 0);
                COLORREF fg = RGB(245, 245, 245);
                std::wstring path;
                const LRESULT data = SendMessageW(hwnd_content_3d_, LB_GETITEMDATA, dis->itemID, 0);
                if (data >= 0 && (size_t)data < content_items_.size()) path = utf8_to_wide(content_items_[(size_t)data].rel_utf8);
                if (!path.empty() && scene_ && ew_content_is_highlighted(path, &scene_->runtime().substrate_manager)) bg = sel ? RGB(110, 86, 20) : RGB(44, 34, 8);
                HBRUSH br = CreateSolidBrush(bg);
                FillRect(hdc, &rc, br);
                DeleteObject(br);
                HPEN pen = CreatePen(PS_SOLID, 1, border);
                HGDIOBJ old_pen = SelectObject(hdc, pen);
                HGDIOBJ old_br = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
                SelectObject(hdc, old_br);
                SelectObject(hdc, old_pen);
                DeleteObject(pen);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, fg);
                wchar_t buf[1024]; buf[0] = 0;
                SendMessageW(hwnd_content_3d_, LB_GETTEXT, dis->itemID, (LPARAM)buf);
                RECT txt = rc; txt.left += 12; txt.top += 8; txt.right -= 12;
                DrawTextW(hdc, buf, -1, &txt, DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
                if (sel && (dis->itemState & ODS_FOCUS)) DrawFocusRect(hdc, &rc);
                return TRUE;
            }
            if (id == 2713 && dis->hwndItem == hwnd_node_graph_) {
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                const bool sel = (dis->itemState & ODS_SELECTED) != 0;
                const LRESULT data = SendMessageW(hwnd_node_graph_, LB_GETITEMDATA, dis->itemID, 0);
                const size_t idx = (data >= 0) ? (size_t)data : 0u;
                const int level = (idx < node_graph_item_level_i32_.size()) ? node_graph_item_level_i32_[(size_t)idx] : 0;
                const int parent = (idx < node_graph_parent_i32_.size()) ? node_graph_parent_i32_[(size_t)idx] : -1;
                const int strength = (idx < node_graph_strength_pct_i32_.size()) ? node_graph_strength_pct_i32_[(size_t)idx] : 0;
                const uint32_t anchor_id = (idx < node_graph_anchor_id_u32_.size()) ? node_graph_anchor_id_u32_[(size_t)idx] : 0u;
                const std::wstring label = (idx < node_graph_items_w_.size()) ? node_graph_items_w_[(size_t)idx] : L"";
                const std::wstring op_path = (idx < node_graph_operator_path_w_.size()) ? node_graph_operator_path_w_[(size_t)idx] : L"";
                const int seq_i32 = (idx < node_graph_sequence_i32_.size()) ? node_graph_sequence_i32_[(size_t)idx] : 0;
                const int div_i32 = (idx < node_graph_divergence_i32_.size()) ? node_graph_divergence_i32_[(size_t)idx] : 0;
                const bool ancilla = (idx < node_graph_is_ancilla_u8_.size()) ? (node_graph_is_ancilla_u8_[(size_t)idx] != 0u) : false;
                const int edge_in_i32 = (idx < node_graph_edge_in_i32_.size()) ? node_graph_edge_in_i32_[(size_t)idx] : 0;
                const int edge_out_i32 = (idx < node_graph_edge_out_i32_.size()) ? node_graph_edge_out_i32_[(size_t)idx] : 0;
                const std::wstring edge_label = (idx < node_graph_edge_label_w_.size()) ? node_graph_edge_label_w_[(size_t)idx] : L"";
                const int depth_fade = std::max(0, 24 - seq_i32 * 2);
                HBRUSH br = CreateSolidBrush(sel ? RGB(32, 26, 10) : RGB(10 + depth_fade / 3, 10 + depth_fade / 4, 10));
                FillRect(hdc, &rc, br);
                DeleteObject(br);
                const int cy = (rc.top + rc.bottom) / 2;
                const int indent = 26 + level * 26;
                const int radius = (level == 0) ? 8 : (5 + strength / 28);
                const uint64_t tick = scene_ ? scene_->sm.canonical_tick : 0ull;
                const bool pulse_on = node_play_excitation_ && ((((tick / 6ull) + (uint64_t)dis->itemID) & 1ull) == 0ull);
                const int bright = std::min(255, 92 + strength * 3 / 2 + depth_fade + (pulse_on ? 22 : 0));
                const int div_mod = ((div_i32 % 6) + 6) % 6;
                COLORREF wire = RGB(bright, std::min(220, bright * 3 / 4), std::min(140, 28 + div_mod * 18));
                if (div_mod == 1) wire = RGB(std::min(255, bright), 180, 72);
                else if (div_mod == 2) wire = RGB(120, std::min(255, bright), 112);
                else if (div_mod == 3) wire = RGB(96, 156, std::min(255, bright));
                else if (div_mod == 4) wire = RGB(164, 116, std::min(255, bright));
                else if (div_mod == 5) wire = RGB(std::min(255, bright), 140, 140);
                if (sel) wire = RGB(255, 220, 96);
                const int pen_w = std::max(1, (strength + 24) / 32 + (pulse_on ? 1 : 0));
                HPEN pen = CreatePen(PS_SOLID, pen_w, wire);
                HGDIOBJ old_pen = SelectObject(hdc, pen);
                const int left_stub_len = std::min(18, 6 + edge_in_i32 * 4);
                const int right_stub_len = std::min(24, 8 + edge_out_i32 * 4);
                const int left_stub_x0 = indent - radius - 2 - left_stub_len;
                const int left_stub_x1 = indent - radius - 2;
                const int right_stub_x0 = indent + radius + 2;
                const int right_stub_x1 = indent + radius + 2 + right_stub_len;
                if (level > 0) {
                    const int trunk_x = 12 + std::max(0, parent + 1) * 2;
                    MoveToEx(hdc, trunk_x, cy, nullptr);
                    LineTo(hdc, left_stub_x0, cy);
                }
                if (edge_in_i32 > 0) {
                    MoveToEx(hdc, left_stub_x0, cy, nullptr);
                    LineTo(hdc, left_stub_x1, cy);
                    if (edge_in_i32 > 1) {
                        MoveToEx(hdc, left_stub_x0 + 2, cy - 3, nullptr);
                        LineTo(hdc, left_stub_x0 + 2, cy + 3);
                    }
                }
                if (edge_out_i32 > 0) {
                    MoveToEx(hdc, right_stub_x0, cy, nullptr);
                    LineTo(hdc, right_stub_x1, cy);
                    if (edge_out_i32 > 1) {
                        MoveToEx(hdc, right_stub_x1 - 2, cy - 3, nullptr);
                        LineTo(hdc, right_stub_x1 - 2, cy + 3);
                    }
                }
                HBRUSH node_br = CreateSolidBrush(sel ? RGB(120, 88, 18) : RGB(std::min(180, 36 + strength + div_mod * 6), std::min(150, 22 + strength / 2 + div_mod * 4), ancilla ? 18 : 8));
                HGDIOBJ old_br = SelectObject(hdc, node_br);
                Ellipse(hdc, indent - radius, cy - radius, indent + radius, cy + radius);
                SelectObject(hdc, old_br);
                DeleteObject(node_br);
                SelectObject(hdc, old_pen);
                DeleteObject(pen);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(245,245,245));
                RECT seq_rc = rc; seq_rc.left = 4; seq_rc.right = 24;
                SetTextColor(hdc, RGB(176, 176, 176));
                wchar_t seq_txt[32]{};
                swprintf(seq_txt, 32, L"%d", seq_i32);
                DrawTextW(hdc, seq_txt, -1, &seq_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT txt = rc; txt.left = indent + radius + 8; txt.right -= 172;
                SetTextColor(hdc, RGB(245,245,245));
                DrawTextW(hdc, label.c_str(), -1, &txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                SetTextColor(hdc, RGB(196, 176, 96));
                RECT badge = rc; badge.left = rc.right - 168; badge.right = rc.right - 8;
                wchar_t badge_txt[160]{};
                swprintf(badge_txt, 160, L"A%u S%d D%d  %d>%d", anchor_id, seq_i32, div_i32, edge_in_i32, edge_out_i32);
                DrawTextW(hdc, badge_txt, -1, &badge, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                if (sel) {
                    SetTextColor(hdc, RGB(176, 156, 84));
                    RECT sub = rc; sub.left = indent + radius + 8; sub.top += 15; sub.right -= 8;
                    std::wstring subline = op_path;
                    if (!edge_label.empty()) subline += L"  |  edge: " + edge_label;
                    DrawTextW(hdc, subline.c_str(), -1, &sub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                }
                if (sel && (dis->itemState & ODS_FOCUS)) DrawFocusRect(hdc, &rc);
                return TRUE;
            }
            if (id == 4060) { ew_ai_chat_draw_bubble(dis); return TRUE; }
            if (id == 4061) { ew_draw_compose_icon(dis); return TRUE; }
            if (id == 4062) { ew_draw_apply_button(dis); return TRUE; }
            if (id == 4063) { ew_draw_usepatch_button(dis); return TRUE; }
            if (id == 4064 || id == 4066 || id == 4067) { ew_draw_action_text_button(dis); return TRUE; }
        } break;
        case WM_CONTEXTMENU: {
            HWND hctx = (HWND)wparam;
            if (hctx == hwnd_node_graph_) {
                POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
                if (pt.x == -1 && pt.y == -1) {
                    RECT rc{};
                    GetWindowRect(hwnd_node_graph_, &rc);
                    pt.x = rc.left + 24;
                    pt.y = rc.top + 24;
                } else {
                    POINT client_pt = pt;
                    ScreenToClient(hwnd_node_graph_, &client_pt);
                    const DWORD res = (DWORD)SendMessageW(hwnd_node_graph_, LB_ITEMFROMPOINT, 0, MAKELPARAM(client_pt.x, client_pt.y));
                    const int row = (int)LOWORD(res);
                    const BOOL outside = HIWORD(res);
                    if (!outside && row >= 0) {
                        SendMessageW(hwnd_node_graph_, LB_SETCURSEL, (WPARAM)row, 0);
                        node_graph_selected_i32_ = row;
                    }
                }
                ShowNodeSpawnMenu(pt);
                return 0;
            }
        } break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            bool was_down = false;
            if (wparam < 256) { was_down = input_.key_down[wparam]; input_.key_down[wparam] = true; }
            input_.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            input_.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            input_.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if ((HWND)GetFocus() == hwnd_node_search_) {
                if (wparam == VK_RETURN) {
                    RebuildNodePaletteEntries();
                    if (node_palette_entries_.size() == 1u) {
                        (void)SpawnNodePaletteEntry(0u);
                    } else {
                        RECT rc{};
                        if (hwnd_node_graph_) GetWindowRect(hwnd_node_graph_, &rc); else if (hwnd_rdock_node_) GetWindowRect(hwnd_rdock_node_, &rc);
                        POINT pt{ rc.left + 40, rc.top + 40 };
                        ShowNodeSpawnMenu(pt);
                    }
                    return 0;
                }
                if (wparam == VK_ESCAPE) {
                    if (hwnd_node_search_) SetWindowTextW(hwnd_node_search_, L"");
                    RefreshNodePanel();
                    return 0;
                }
            }
            if (input_.alt && (wparam == VK_OEM_4 || wparam == VK_OEM_6) && !was_down) {
                const auto src_pins = ew_split_trim_pin_list((node_graph_selected_i32_ >= 0 && (size_t)node_graph_selected_i32_ < node_graph_output_pins_w_.size()) ? node_graph_output_pins_w_[(size_t)node_graph_selected_i32_] : L"");
                if (!src_pins.empty()) {
                    const int delta = (wparam == VK_OEM_4) ? -1 : 1;
                    node_source_pin_selected_i32_ = (node_source_pin_selected_i32_ + delta + (int)src_pins.size()) % (int)src_pins.size();
                    ClampNodePinSelections(); RefreshNodePanel();
                }
                return 0;
            }
            if (input_.ctrl && (wparam == VK_OEM_4 || wparam == VK_OEM_6) && !was_down) {
                const int result_sel = hwnd_node_results_ ? (int)SendMessageW(hwnd_node_results_, LB_GETCURSEL, 0, 0) : -1;
                const auto dst_pins = (result_sel >= 0 && (size_t)result_sel < node_palette_entries_.size()) ? ew_split_trim_pin_list(node_palette_entries_[(size_t)result_sel].input_pins_w) : std::vector<std::wstring>{};
                if (!dst_pins.empty()) {
                    const int delta = (wparam == VK_OEM_4) ? -1 : 1;
                    node_target_pin_selected_i32_ = (node_target_pin_selected_i32_ + delta + (int)dst_pins.size()) % (int)dst_pins.size();
                    ClampNodePinSelections(); RefreshNodePanel();
                }
                return 0;
            }

            // Toggle immersion mode (Standard <-> Immersion)
            if (wparam == VK_F10) immersion_mode_ = !immersion_mode_;
            // Toggle resonance viewport mode
            if (wparam == VK_OEM_3 && !was_down) { resonance_view_ = !resonance_view_; RefreshViewportResonanceOverlay(); }

            // AI compose shortcut: Ctrl+N while AI panel is visible.
            if (input_.ctrl && (wparam == 'N' || wparam == 'n')) {
                if (hwnd_ai_panel_ && IsWindowVisible(hwnd_ai_panel_)) {
                    SendMessageW(hwnd_ai_panel_, WM_COMMAND, MAKEWPARAM(4061, BN_CLICKED), 0);
                }
            }
        } break;
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            if (wparam < 256) input_.key_down[wparam] = false;
            input_.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            input_.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            input_.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        } break;
        case WM_LBUTTONDOWN: input_.lmb = true; SetCapture(hwnd); break;
        case WM_LBUTTONUP: input_.lmb = false; ReleaseCapture(); break;
        case WM_RBUTTONDOWN: input_.rmb = true; SetCapture(hwnd); break;
        case WM_RBUTTONUP: input_.rmb = false; ReleaseCapture(); break;
        case WM_MBUTTONDOWN: input_.mmb = true; SetCapture(hwnd); break;
        case WM_MBUTTONUP: {
            input_.mmb = false; ReleaseCapture();
            POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            HWND hit_hwnd = ChildWindowFromPoint(hwnd, pt);
            if (hit_hwnd == hwnd_ai_tab_ && ai_chat_count_u32_ > 1u) {
                POINT pt_tab = pt;
                ScreenToClient(hwnd_ai_tab_, &pt_tab);
                TCHITTESTINFO ht{};
                ht.pt = pt_tab;
                const int hit = TabCtrl_HitTest(hwnd_ai_tab_, &ht);
                if (hit >= 0) { AiChatClose((uint32_t)hit); return 0; }
            }
        } break;
        case WM_MOUSEWHEEL: {
            input_.wheel_delta += GET_WHEEL_DELTA_WPARAM(wparam);
        } break;
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            input_.mouse_dx += (x - input_.mouse_x);
            input_.mouse_dy += (y - input_.mouse_y);
            input_.mouse_x = x;
            input_.mouse_y = y;
            if (hwnd_node_graph_ && GetFocus() == hwnd_node_graph_) {
                const int row = (int)SendMessageW(hwnd_node_graph_, LB_GETCURSEL, 0, 0);
                if (row >= 0 && (size_t)row < node_graph_output_pins_w_.size()) {
                    const auto outs = ew_split_trim_pin_list(node_graph_output_pins_w_[(size_t)row]);
                    if (!outs.empty()) {
                        const int local_y = std::max(0, y % 32);
                        const int slot_h = std::max(1, 24 / (int)outs.size());
                        node_source_pin_hover_i32_ = std::min((int)outs.size() - 1, local_y / slot_h);
                        RefreshNodePanel();
                    }
                }
            }
        } break;
        case WM_DESTROY: {
            running_ = false;
            PostQuitMessage(0);
        } break;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace ewv
