#include "GE_app.hpp"

#include <vulkan/vulkan.h>
#include <shellscalingapi.h>
#include <shellapi.h>
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

#ifndef EW_SHADER_OUT_DIR
#define EW_SHADER_OUT_DIR "."
#endif

#pragma comment(lib, "Shcore.lib")

namespace ewv {

// -----------------------------------------------------------------------------
// UI Theme (Win32)
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

static HFONT g_font_ui = nullptr;
static HFONT g_font_ui_bold = nullptr;
static HFONT g_font_ui_small = nullptr;

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
// Win32 Input Bindings Editor (minimal)
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
        sm.ui_submit_user_text_line("BOOT: vulkan_app");

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

    void SubmitUiLine(const std::string& utf8) {
        sm.ui_submit_user_text_line(utf8);
    }

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

    bool ImportObj(const std::string& path_utf8) {
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

        const std::string out_dir = "Draft Container/Assets/Imported/";
        CreateDirectoryW(utf8_to_wide("Draft Container/Assets").c_str(), nullptr);
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
        o.mesh = std::move(m);
        o.object_id_u64 = object_id;
        o.anchor_id_u32 = 0;
        o.refresh_fixed_cache();
        objects.push_back(std::move(o));
        selected = (int)objects.size() - 1;
        return true;
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
    pcr.size = 80; // mat4 (64) + float + padding

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
    HMENU hEdit = CreateMenu();
    HMENU hWindow = CreateMenu();
    HMENU hTools = CreateMenu();
    AppendMenuW(hFile, MF_STRING, 9001, L"Exit");
    AppendMenuW(hEdit, MF_STRING, 9101, L"Copy");
    AppendMenuW(hEdit, MF_STRING, 9102, L"Paste");
    AppendMenuW(hWindow, MF_STRING | (content_visible_ ? MF_CHECKED : 0), 9201, L"Content Browser");
    AppendMenuW(hWindow, MF_STRING, 9202, L"AI Panel");
    AppendMenuW(hTools, MF_STRING, 9301, L"Reindex Content");
    AppendMenuW(hTools, MF_STRING, 9302, L"List Content");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"File");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEdit, L"Edit");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hWindow, L"Window");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hTools, L"Tools");
    SetMenu(hwnd_main_, hMenu);

    ShowWindow(hwnd_main_, SW_SHOW);
}

void App::CreateChildWindows() {
    RECT rc{};
    GetClientRect(hwnd_main_, &rc);
    client_w_ = rc.right - rc.left;
    client_h_ = rc.bottom - rc.top;

    hwnd_viewport_ = CreateWindowW(L"STATIC", L"",
                                   WS_CHILD | WS_VISIBLE,
                                   0, 0, client_w_ - 420, client_h_,
                                   hwnd_main_, (HMENU)1001, GetModuleHandleW(nullptr), nullptr);

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

    rdock_tab_index_u32_ = 0u;
    TabCtrl_SetCurSel(hwnd_rdock_tab_, (int)rdock_tab_index_u32_);

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
    hwnd_content_list_ = CreateWindowW(WC_LISTVIEWW, L"",
                                       WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                       10, 40, client_w_ - 20, 210,
                                       hwnd_content_, (HMENU)1013, GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(hwnd_content_list_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
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


    // AI vault browser entry point (read-only): triggers /vault_list.
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

    // Object list (blank world on boot).
    hwnd_objlist_ = CreateWindowW(L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                                  10, 142, 400, 114,
                                  hwnd_panel_, (HMENU)2004, GetModuleHandleW(nullptr), nullptr);
    // Transform controls (position in meters). UI emits ObjectSetTransform packets.
    {
        const int X = 10, Y = 10, W = 380;
        const int ROW = 26;
        const int GAP = 8;

        HWND h_transform_hdr = CreateWindowW(L"STATIC", L"Transform", WS_CHILD | WS_VISIBLE,
                      X, Y, W, 18, details_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (h_transform_hdr && g_font_ui_bold) SendMessageW(h_transform_hdr, WM_SETFONT, (WPARAM)g_font_ui_bold, TRUE);

        int y = Y + 22;
        CreateWindowW(L"STATIC", L"Pos X", WS_CHILD | WS_VISIBLE, X, y+4, 44, 18, details_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        hwnd_posx_ = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
                                   X + 52, y, 90, ROW, details_parent, (HMENU)2010, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"Y", WS_CHILD | WS_VISIBLE, X + 150, y+4, 14, 18, details_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        hwnd_posy_ = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
                                   X + 168, y, 90, ROW, details_parent, (HMENU)2011, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"Z", WS_CHILD | WS_VISIBLE, X + 266, y+4, 14, 18, details_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        hwnd_posz_ = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
                                   X + 284, y, 90, ROW, details_parent, (HMENU)2012, GetModuleHandleW(nullptr), nullptr);

        y += ROW + GAP;
        hwnd_apply_xform_ = CreateWindowW(L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE,
                                          X, y, 80, ROW, details_parent, (HMENU)2013, GetModuleHandleW(nullptr), nullptr);
        // Property-style copy/paste for transform block.
        CreateWindowW(L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE,
                      X + 88, y, 60, ROW, details_parent, (HMENU)2090, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"BUTTON", L"Paste", WS_CHILD | WS_VISIBLE,
                      X + 154, y, 60, ROW, details_parent, (HMENU)2091, GetModuleHandleW(nullptr), nullptr);

        hwnd_mode_translate_ = CreateWindowW(L"BUTTON", L"Translate", WS_CHILD | WS_VISIBLE,
                                             X + 90, y, 90, ROW, details_parent, (HMENU)2020, GetModuleHandleW(nullptr), nullptr);
        hwnd_mode_rotate_    = CreateWindowW(L"BUTTON", L"Rotate", WS_CHILD | WS_VISIBLE,
                                             X + 186, y, 70, ROW, details_parent, (HMENU)2021, GetModuleHandleW(nullptr), nullptr);
        hwnd_frame_sel_      = CreateWindowW(L"BUTTON", L"Frame", WS_CHILD | WS_VISIBLE,
                                             X + 262, y, 60, ROW, details_parent, (HMENU)2022, GetModuleHandleW(nullptr), nullptr);

        y += ROW + GAP;
        hwnd_snap_enable_ = CreateWindowW(L"BUTTON", L"Snap", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          X, y+2, 64, 22, details_parent, (HMENU)2023, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"Grid", WS_CHILD | WS_VISIBLE, X + 72, y+4, 32, 18, details_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        hwnd_grid_step_ = CreateWindowW(L"EDIT", L"1.0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
                                        X + 108, y, 60, ROW, details_parent, (HMENU)2024, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"Angle", WS_CHILD | WS_VISIBLE, X + 176, y+4, 40, 18, details_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        hwnd_angle_step_ = CreateWindowW(L"EDIT", L"15", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
                                         X + 220, y, 50, ROW, details_parent, (HMENU)2025, GetModuleHandleW(nullptr), nullptr);

        // Axis constraint + undo/redo.
        y += ROW + GAP;
        hwnd_axis_none_ = CreateWindowW(L"BUTTON", L"Axis:None", WS_CHILD | WS_VISIBLE,
                                        X, y, 80, ROW, details_parent, (HMENU)2026, GetModuleHandleW(nullptr), nullptr);
        hwnd_axis_x_    = CreateWindowW(L"BUTTON", L"X", WS_CHILD | WS_VISIBLE,
                                        X + 86, y, 28, ROW, details_parent, (HMENU)2027, GetModuleHandleW(nullptr), nullptr);
        hwnd_axis_y_    = CreateWindowW(L"BUTTON", L"Y", WS_CHILD | WS_VISIBLE,
                                        X + 118, y, 28, ROW, details_parent, (HMENU)2028, GetModuleHandleW(nullptr), nullptr);
        hwnd_axis_z_    = CreateWindowW(L"BUTTON", L"Z", WS_CHILD | WS_VISIBLE,
                                        X + 150, y, 28, ROW, details_parent, (HMENU)2029, GetModuleHandleW(nullptr), nullptr);
        hwnd_undo_      = CreateWindowW(L"BUTTON", L"Undo", WS_CHILD | WS_VISIBLE,
                                        X + 186, y, 60, ROW, details_parent, (HMENU)2030, GetModuleHandleW(nullptr), nullptr);
        hwnd_redo_      = CreateWindowW(L"BUTTON", L"Redo", WS_CHILD | WS_VISIBLE,
                                        X + 252, y, 60, ROW, details_parent, (HMENU)2031, GetModuleHandleW(nullptr), nullptr);
        // Remember bottom of fixed controls so log/output can anchor beneath.
        details_controls_h_ = y + ROW + GAP + 12;
    }


    hwnd_output_ = CreateWindowW(L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                                 10, 350, 400, client_h_ - 360,
                                 details_parent, (HMENU)2005, GetModuleHandleW(nullptr), nullptr);

    // Voxel Designer (materials) placeholder UI: pure authoring surface for future Material Mixer wiring.
    {
        const int X = 10, Y = 10, W = 380;
        HWND h_vox_hdr = CreateWindowW(L"STATIC", L"Voxel Designer (Materials)", WS_CHILD | WS_VISIBLE,
                      X, Y, W, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (h_vox_hdr && g_font_ui_bold) SendMessageW(h_vox_hdr, WM_SETFONT, (WPARAM)g_font_ui_bold, TRUE);
        CreateWindowW(L"STATIC", L"This panel will host the Material Mixer + voxelization controls.",
                      WS_CHILD | WS_VISIBLE, X, Y + 22, W, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"(UI only for now — backend wiring comes next)",
                      WS_CHILD | WS_VISIBLE, X, Y + 42, W, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);

        // A few representative knobs (placeholders).
        CreateWindowW(L"STATIC", L"Density", WS_CHILD | WS_VISIBLE, X, Y + 78, 60, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                      X + 70, Y + 72, W - 80, 30, voxel_parent, (HMENU)2601, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"Hardness", WS_CHILD | WS_VISIBLE, X, Y + 112, 60, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                      X + 70, Y + 106, W - 80, 30, voxel_parent, (HMENU)2602, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(L"STATIC", L"Roughness", WS_CHILD | WS_VISIBLE, X, Y + 146, 60, 18, voxel_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                      X + 70, Y + 140, W - 80, 30, voxel_parent, (HMENU)2603, GetModuleHandleW(nullptr), nullptr);
    }

    // Apply consistent editor fonts (Segoe UI) across UI controls.
    ew_apply_editor_fonts(hwnd_panel_);
    if (hwnd_content_) ew_apply_editor_fonts(hwnd_content_);
    LayoutChildren(client_w_, client_h_);

}


// -----------------------------------------------------------------------------
// AI Panel Window (Crawler progress + Experiments)
// - Separate window so editor controls remain uncluttered.
// - Always boots with Learning/Crawling OFF.
// - Bell badge indicates unseen experiments (committed count delta).
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

void App::CreateAiPanelWindow() {
    if (hwnd_ai_panel_) return;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    hwnd_ai_panel_ = CreateWindowW(L"STATIC", L"AI",
                                   WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 560, 460,
                                   hwnd_main_, nullptr, GetModuleHandleW(nullptr), nullptr);

    // Learning/Crawling toggles inside the AI panel.
    CreateWindowW(L"STATIC", L"Learning", WS_CHILD | WS_VISIBLE, 12, 10, 58, 18,
                  hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_toggle_learning_ = CreateWindowW(L"BUTTON", L"",
                                             WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                             74, 6, 56, 24,
                                             hwnd_ai_panel_, (HMENU)4055, GetModuleHandleW(nullptr), nullptr);

    CreateWindowW(L"STATIC", L"Crawling", WS_CHILD | WS_VISIBLE, 140, 10, 58, 18,
                  hwnd_ai_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_ai_toggle_crawling_ = CreateWindowW(L"BUTTON", L"",
                                             WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                             202, 6, 56, 24,
                                             hwnd_ai_panel_, (HMENU)4056, GetModuleHandleW(nullptr), nullptr);

    // Bell (owner-drawn) top-right.
    hwnd_ai_bell_ = CreateWindowW(L"BUTTON", L"",
                                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                  520, 10, 24, 24,
                                  hwnd_ai_panel_, (HMENU)4050, GetModuleHandleW(nullptr), nullptr);

    // Tabs.
    hwnd_ai_tab_ = CreateWindowW(WC_TABCONTROLW, L"",
                                 WS_CHILD | WS_VISIBLE,
                                 10, 40, 530, 370,
                                 hwnd_ai_panel_, (HMENU)4051, GetModuleHandleW(nullptr), nullptr);

    TCITEMW ti{};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)L"Crawler";
    TabCtrl_InsertItem(hwnd_ai_tab_, 0, &ti);
    ti.pszText = (LPWSTR)L"Experiments";
    TabCtrl_InsertItem(hwnd_ai_tab_, 1, &ti);

    // Crawler tab controls
    hwnd_ai_progress_overall_ = CreateWindowW(PROGRESS_CLASSW, L"",
                                              WS_CHILD | WS_VISIBLE,
                                              20, 74, 510, 18,
                                              hwnd_ai_panel_, (HMENU)4052, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hwnd_ai_progress_overall_, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    hwnd_ai_domain_list_ = CreateWindowW(WC_LISTVIEWW, L"",
                                         WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                         20, 98, 510, 300,
                                         hwnd_ai_panel_, (HMENU)4053, GetModuleHandleW(nullptr), nullptr);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.cx = 260; col.pszText = (LPWSTR)L"Domain"; ListView_InsertColumn(hwnd_ai_domain_list_, 0, &col);
    col.cx = 90;  col.pszText = (LPWSTR)L"Pages";  col.iSubItem = 1; ListView_InsertColumn(hwnd_ai_domain_list_, 1, &col);
    col.cx = 140; col.pszText = (LPWSTR)L"Progress"; col.iSubItem = 2; ListView_InsertColumn(hwnd_ai_domain_list_, 2, &col);

    // Experiments tab control: listbox over same area (we show/hide based on tab)
    hwnd_ai_experiment_list_ = CreateWindowW(L"LISTBOX", L"",
                                             WS_CHILD | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
                                             20, 74, 510, 324,
                                             hwnd_ai_panel_, (HMENU)4054, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(hwnd_ai_experiment_list_, SW_HIDE);

    // Default: crawler tab visible.
    ai_tab_index_u32_ = 0u;
    ShowWindow(hwnd_ai_progress_overall_, SW_SHOW);
    ShowWindow(hwnd_ai_domain_list_, SW_SHOW);

    // Populate both views once so the first show is instant.
    RefreshAiDomainProgressList();
    RefreshAiExperimentList();
    UpdateAiPanel();

    // Start hidden; user opens via UI.
    ShowWindow(hwnd_ai_panel_, SW_HIDE);
}

void App::ToggleAiPanel() {
    CreateAiPanelWindow();
    if (!hwnd_ai_panel_) return;
    const BOOL vis = IsWindowVisible(hwnd_ai_panel_);
    ShowWindow(hwnd_ai_panel_, vis ? SW_HIDE : SW_SHOW);
    if (!vis) {
        RefreshAiExperimentList();
        RefreshAiDomainProgressList();
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

void App::RefreshAiExperimentList() {
    if (!hwnd_ai_experiment_list_) return;
    SendMessageW(hwnd_ai_experiment_list_, LB_RESETCONTENT, 0, 0);

    // Mirror vault artifacts into Draft Container/AssetSubstrate/AI.
    // Experiments are stored as .geassetref files.
    const std::wstring base = L"Draft Container\\AssetSubstrate\\AI\\experiments\\metrics";

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((base + L"\\*.geassetref").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                SendMessageW(hwnd_ai_experiment_list_, LB_ADDSTRING, 0, (LPARAM)fd.cFileName);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // Also list failures.
    const std::wstring base2 = L"Draft Container\\AssetSubstrate\\AI\\experiments\\metrics_failures";
    h = FindFirstFileW((base2 + L"\\*.geassetref").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring name = L"(fail) ";
                name += fd.cFileName;
                SendMessageW(hwnd_ai_experiment_list_, LB_ADDSTRING, 0, (LPARAM)name.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

void App::RefreshAiDomainProgressList() {
    if (!hwnd_ai_domain_list_ || !scene_) return;

    ListView_DeleteAllItems(hwnd_ai_domain_list_);

    // Pull deterministic stats from substrate manager. These are cheap counters
    // updated by the crawler/vault commits.
    const uint32_t n = scene_->sm.domain_crawl_stats_n_u32;
    uint64_t total_seen = 0ull;
    uint64_t total_target = 0ull;

    // Sort domains by ASCII name for a stable, user-friendly display.
    uint32_t idx[DOMAIN_CRAWL_STATS_MAX_U32];
    for (uint32_t i = 0u; i < n; ++i) idx[i] = i;
    for (uint32_t i = 0u; i + 1u < n; ++i) {
        uint32_t best = i;
        for (uint32_t j = i + 1u; j < n; ++j) {
            const auto& a = scene_->sm.domain_crawl_stats[idx[j]].domain_ascii;
            const auto& b = scene_->sm.domain_crawl_stats[idx[best]].domain_ascii;
            if (a < b) best = j;
        }
        uint32_t tmp = idx[i];
        idx[i] = idx[best];
        idx[best] = tmp;
    }

    for (uint32_t row = 0u; row < n; ++row) {
        const uint32_t i = idx[row];
        const auto& ds = scene_->sm.domain_crawl_stats[i];
        total_seen += (uint64_t)ds.pages_seen_u32;
        total_target += (uint64_t)ds.pages_target_u32;

        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = (int)row;
        std::wstring dom = ew_utf8_to_wide_lossy(ds.domain_ascii);
        it.pszText = dom.empty() ? (LPWSTR)L"(unknown)" : (LPWSTR)dom.c_str();
        ListView_InsertItem(hwnd_ai_domain_list_, &it);

        wchar_t pages[32];
        _snwprintf(pages, 32, L"%u/%u", (unsigned)ds.pages_seen_u32, (unsigned)ds.pages_target_u32);
        ListView_SetItemText(hwnd_ai_domain_list_, (int)row, 1, pages);

        uint32_t pct = 0u;
        if (ds.pages_target_u32 > 0u) {
            pct = (uint32_t)(((uint64_t)ds.pages_seen_u32 * 100ull) / (uint64_t)ds.pages_target_u32);
            if (pct > 100u) pct = 100u;
        }
        wchar_t prog[32];
        _snwprintf(prog, 32, L"%u%%", (unsigned)pct);
        ListView_SetItemText(hwnd_ai_domain_list_, (int)row, 2, prog);
    }

    // Overall progress.
    uint32_t pct_all = 0u;
    if (total_target > 0ull) {
        pct_all = (uint32_t)((total_seen * 100ull) / total_target);
        if (pct_all > 100u) pct_all = 100u;
    }
    if (hwnd_ai_progress_overall_) {
        SendMessageW(hwnd_ai_progress_overall_, PBM_SETPOS, (WPARAM)pct_all, 0);
    }
}

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

    // Keep progress list fresh while visible.
    if (hwnd_ai_panel_ && IsWindowVisible(hwnd_ai_panel_)) {
        RefreshAiDomainProgressList();
    }
}

void App::LayoutChildren(int w, int h) {
    const int panel_w = 420;
    const int content_h = (content_visible_ ? 260 : 0);
    const int top_h = (h > content_h) ? (h - content_h) : h;

    MoveWindow(hwnd_viewport_, 0, 0, w - panel_w, top_h, TRUE);
    MoveWindow(hwnd_panel_, w - panel_w, 0, panel_w, top_h, TRUE);

    // Bottom content browser.
    if (hwnd_content_) {
        MoveWindow(hwnd_content_, 0, top_h, w, content_h, TRUE);
        if (hwnd_content_list_) MoveWindow(hwnd_content_list_, 10, 40, w - 20, content_h - 50, TRUE);
    }

    // Right dock layout (tab strip + panel roots).
    const int PAD = 12;
    const int TAB_H = 30;
    int tab_x = PAD;
    int tab_y = PAD;
    int tab_w = panel_w - 2 * PAD;

    int panel_x = PAD;
    int panel_y = PAD + TAB_H + 8;
    int panel_w_in = panel_w - 2 * PAD;
    int panel_h_in = top_h - panel_y - PAD;
    if (panel_h_in < 64) panel_h_in = 64;

    if (hwnd_rdock_tab_) MoveWindow(hwnd_rdock_tab_, tab_x, tab_y, tab_w, TAB_H, TRUE);
    if (hwnd_rdock_outliner_) MoveWindow(hwnd_rdock_outliner_, panel_x, panel_y, panel_w_in, panel_h_in, TRUE);
    if (hwnd_rdock_details_)  MoveWindow(hwnd_rdock_details_,  panel_x, panel_y, panel_w_in, panel_h_in, TRUE);
    if (hwnd_rdock_asset_)    MoveWindow(hwnd_rdock_asset_,    panel_x, panel_y, panel_w_in, panel_h_in, TRUE);
    if (hwnd_rdock_voxel_)    MoveWindow(hwnd_rdock_voxel_,    panel_x, panel_y, panel_w_in, panel_h_in, TRUE);
    if (hwnd_objlist_ && hwnd_rdock_outliner_) {
        MoveWindow(hwnd_objlist_, 10, 10, panel_w_in - 20, panel_h_in - 20, TRUE);
    }

    // Asset panel (command + basic tools) layout.
    if (hwnd_rdock_asset_) {
        const int X = 10;
        int y = 10;
        const int ROW = 26;
        const int GAP = 8;
        const int W = panel_w_in - 20;

        if (hwnd_input_) MoveWindow(hwnd_input_, X, y, W, ROW, TRUE);
        y += ROW + GAP;
        // Buttons row
        if (hwnd_send_) MoveWindow(hwnd_send_, X, y, 80, ROW, TRUE);
        if (hwnd_import_) MoveWindow(hwnd_import_, X + 88, y, 110, ROW, TRUE);
        if (hwnd_bootstrap_) MoveWindow(hwnd_bootstrap_, X + 206, y, W - 206, ROW, TRUE);
        y += ROW + GAP;

        // Toggle rows (the labels are static windows without handles; they remain positioned from creation).
        if (hwnd_toggle_play_) MoveWindow(hwnd_toggle_play_, X + 32, y, 86, 24, TRUE);
        if (hwnd_toggle_ai_) MoveWindow(hwnd_toggle_ai_, X + 152, y, 86, 24, TRUE);
        y += 26;
        if (hwnd_toggle_learning_) MoveWindow(hwnd_toggle_learning_, X + 42, y, 86, 24, TRUE);
        if (hwnd_toggle_crawling_) MoveWindow(hwnd_toggle_crawling_, X + 176, y, 86, 24, TRUE);
        if (hwnd_vault_) MoveWindow(hwnd_vault_, X + W - 120, y, 56, 24, TRUE);
        if (hwnd_ai_panel_) { /* separate window */ }
        y += 28;
        if (hwnd_ai_status_) MoveWindow(hwnd_ai_status_, X, y, W, 18, TRUE);
    }

    // Details panel: log/output fills remaining space below the fixed controls.
    if (hwnd_output_ && hwnd_rdock_details_) {
        const int out_x = 10;
        const int out_y = details_controls_h_;
        const int out_w = panel_w_in - 20;
        int out_h = panel_h_in - out_y - 10;
        if (out_h < 64) out_h = 64;
        MoveWindow(hwnd_output_, out_x, out_y, out_w, out_h, TRUE);
    }

    // Content browser children already handled in their own WM_SIZE path.
}


int App::Run(HINSTANCE hInst) {
    // Win64 enforcement at runtime too (belt + suspenders)
    static_assert(sizeof(void*) == 8, "Win64 required");

    CreateMainWindow(hInst);
    CreateChildWindows();

    scene_ = new Scene();

    // Populate content browser after substrate/runtime is live.
    scene_->SubmitUiLine("/content_list part=all limit=200");

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

    // Create AI panel window (hidden by default).
    CreateAiPanelWindow();


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
if (uz_now && !prev_uz) { EmitEditorUndo(); }
if (uy_now && !prev_uy) { EmitEditorRedo(); }
prev_uz = uz_now;
prev_uy = uy_now;

    // Camera rig controls (Alt + mouse):
    //   Alt+LMB orbit, Alt+MMB pan, wheel dolly.
    if (mouse_in_view) {
        if (input_.alt && input_.lmb) {
            cam_yaw_rad_   += (float)input_.mouse_dx * 0.005f;
            cam_pitch_rad_ += (float)-input_.mouse_dy * 0.005f;
            cam_pitch_rad_ = ew_clampf(cam_pitch_rad_, -1.5f, 1.5f);
        }
        if (input_.alt && input_.mmb) {
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
            const float s = (steps > 0) ? 0.9f : 1.1111111f;
            cam_dist_m_ *= s;
            if (cam_dist_m_ < 0.1f) cam_dist_m_ = 0.1f;
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

    // Viewport click selection (no Alt): ray-sphere pick against cached object bounds.
    static bool prev_lmb = false;
    const bool lmb_edge = (input_.lmb && !prev_lmb);
    prev_lmb = input_.lmb;
    if (mouse_in_view && lmb_edge && !input_.alt && !input_.ctrl) {
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
            if (input_.shift) {
                EmitEditorToggleSelection(editor_selected_object_id_u64_);
            } else {
                EmitEditorSelection(editor_selected_object_id_u64_);
            }
        }
    }

    // Gizmo drag (no Alt):
    // Translate: drag in camera plane (approx)
    // Rotate: drag yaw about world up
    if (scene_ && scene_->selected >= 0 && scene_->selected < (int)scene_->objects.size()) {
        auto& o = scene_->objects[(size_t)scene_->selected];
        const bool can_drag = mouse_in_view && !input_.alt;
        if (can_drag && input_.lmb && !gizmo_drag_active_) {
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
        if (gizmo_drag_active_ && !input_.lmb) {
            // Commit one undo step for the whole drag.
            EmitEditorCommitTransformTxn(o.object_id_u64,
                                       drag_start_pos_q16_16_,
                                       drag_start_rot_q16_16_,
                                       drag_last_pos_q16_16_,
                                       drag_last_rot_q16_16_);
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
            EwControlPacket cp{};
            cp.kind = EwControlPacketKind::InputAxis;
            cp.source_u16 = 1;
            cp.tick_u64 = scene_->sm.canonical_tick;
            cp.payload.input_axis.axis_id_u32 = 0x1002u; // wheel
            cp.payload.input_axis.value_q16_16 = (int32_t)(input_.wheel_delta << 16);
            (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
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
            struct Push { float proj[16]; float sunPosCam[3]; float pointSize; } push{};
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
    // Deterministic visual cue: vary clear color with tick.
    const uint64_t t = scene_ ? scene_->sm.canonical_tick : 0;
    float r = (float)((t % 256) / 255.0);
    float g = (float)(((t / 7) % 256) / 255.0);
    float b = (float)(((t / 19) % 256) / 255.0);
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

    struct Push { float proj[16]; float sunPosCam[3]; float pointSize; } push{};
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
        scene_->SubmitUiLine(utf8);
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
        if (scene_ && scene_->ImportObj(path)) {
            // update list
            SendMessageW(hwnd_objlist_, LB_RESETCONTENT, 0, 0);
            for (auto& o : scene_->objects) {
                SendMessageW(hwnd_objlist_, LB_ADDSTRING, 0, (LPARAM)utf8_to_wide(o.name_utf8).c_str());
            }
        } else {
            MessageBoxA(hwnd_main_, "Failed to import OBJ.", "GenesisEngineVulkan", MB_ICONWARNING | MB_OK);
        }
    }
}


void App::OnBootstrapGame() {
    if (!scene_) return;
    scene_->SubmitUiLine("GAMEBOOT: editor_bootstrap");
    AppendOutputUtf8("EDITOR: GAMEBOOT submitted");
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

void App::EmitEditorSelection(uint64_t object_id_u64) {
    if (!scene_) return;
    EwControlPacket cp{};
    cp.kind = EwControlPacketKind::EditorSetSelection;
    cp.source_u16 = 1;
    cp.tick_u64 = scene_->sm.canonical_tick;
    cp.payload.editor_set_selection.selected_object_id_u64 = object_id_u64;
    (void)ew_runtime_submit_control_packet(&scene_->sm, &cp);
void App::EmitEditorToggleSelection(uint64_t object_id_u64) {
    if (!scene_) return;
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

    // Content browser capture (deterministic): parse /content_list output.
    if (line.rfind("CONTENT_LIST", 0) == 0) {
        content_items_.clear();
        if (hwnd_content_list_) ListView_DeleteAllItems(hwnd_content_list_);
        return;
    }
    if (line.rfind("CONTENT_ITEM ", 0) == 0) {
        auto find_tok = [&](const char* key) -> std::string {
            const std::string k = std::string(key) + "=";
            size_t p = line.find(k);
            if (p == std::string::npos) return "";
            p += k.size();
            size_t e = line.find(' ', p);
            if (e == std::string::npos) e = line.size();
            return line.substr(p, e - p);
        };
        std::string rel = find_tok("rel");
        std::string label = find_tok("label");
        if (!rel.empty()) {
            content_items_.push_back({rel, label});
            bool show = true;
            if (!content_search_utf8_.empty()) {
                const std::string& q = content_search_utf8_;
                if (rel.find(q) == std::string::npos && label.find(q) == std::string::npos) show = false;
            }
            if (show && hwnd_content_list_) {
                LVITEMW it{};
                it.mask = LVIF_TEXT;
                it.iItem = ListView_GetItemCount(hwnd_content_list_);
                std::wstring wrel = utf8_to_wide(rel);
                it.pszText = (LPWSTR)wrel.c_str();
                int row = ListView_InsertItem(hwnd_content_list_, &it);
                std::wstring wlab = utf8_to_wide(label);
                ListView_SetItemText(hwnd_content_list_, row, 1, (LPWSTR)wlab.c_str());
            }
        }
        return;
    }
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
            RECT rc{}; GetClientRect(hwnd_main_, &rc);
            client_w_ = rc.right - rc.left;
            client_h_ = rc.bottom - rc.top;
            LayoutChildren(client_w_, client_h_);
            resized_ = true;
        } break;
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);

            // Menu actions (HIWORD==0).
            if (code == 0) {
                if (id == 9001) { PostQuitMessage(0); return 0; }
                if (id == 9201) {
                    content_visible_ = !content_visible_;
                    if (hwnd_content_) ShowWindow(hwnd_content_, content_visible_ ? SW_SHOW : SW_HIDE);
                    LayoutChildren(client_w_, client_h_);
                    return 0;
                }
                if (id == 9202) { ToggleAiPanel(); return 0; }
                if (id == 9301 && scene_) { scene_->SubmitUiLine("/content_reindex"); return 0; }
                if (id == 9302 && scene_) { scene_->SubmitUiLine("/content_list part=all limit=200"); return 0; }
                if (id == 9101 || id == 2090) {
                    // Copy transform block (pos only for now) to clipboard.
                    auto getw = [&](HWND h)->std::wstring { wchar_t b[128]; GetWindowTextW(h, b, 127); b[127]=0; return std::wstring(b); };
                    std::wstring txt = L"pos " + getw(hwnd_posx_) + L" " + getw(hwnd_posy_) + L" " + getw(hwnd_posz_);
                    (void)ew_clip_set_text_utf16(hwnd_main_, txt);
                    return 0;
                }
                if (id == 9102 || id == 2091) {
                    // Paste transform block (expects: 'pos x y z' or 'x y z').
                    std::wstring txt = ew_clip_get_text_utf16(hwnd_main_);
                    if (!txt.empty()) {
                        // Simple parse.
                        std::wstringstream ss(txt);
                        std::wstring t0; ss >> t0;
                        double x=0,y=0,z=0;
                        if (t0 == L"pos" || t0 == L"Pos") { ss >> x >> y >> z; }
                        else { try { x = std::stod(t0); } catch(...) { x = 0; } ss >> y >> z; }
                        wchar_t b[64];
                        swprintf(b, 64, L"%.6f", x); SetWindowTextW(hwnd_posx_, b);
                        swprintf(b, 64, L"%.6f", y); SetWindowTextW(hwnd_posy_, b);
                        swprintf(b, 64, L"%.6f", z); SetWindowTextW(hwnd_posz_, b);
                    }
                    return 0;
                }
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
            if (code == BN_CLICKED) {
                if (id == 2090) { SendMessageW(hwnd_main_, WM_COMMAND, MAKEWPARAM(9101,0), 0); return 0; }
                if (id == 2091) { SendMessageW(hwnd_main_, WM_COMMAND, MAKEWPARAM(9102,0), 0); return 0; }

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
                }
            }

            if (id == 2045 && code == BN_CLICKED) {
                if (scene_) {
                    scene_->SubmitUiLine("/vault_list cat=all limit=20");
                    AppendOutputUtf8("VAULT: list requested");
                }
            }

            if (id == 2046 && code == BN_CLICKED) {
                ToggleAiPanel();
            }

            // Content browser refresh
            if (id == 1012 && code == BN_CLICKED) {
                if (scene_) scene_->SubmitUiLine("/content_list part=all limit=200");
                return 0;
            }

            // Content search (simple substring filter; deterministic rebuild from cached list)
            if (id == 1011 && code == EN_CHANGE) {
                wchar_t buf[256];
                GetWindowTextW(hwnd_content_search_, buf, 256);
                content_search_utf8_ = wide_to_utf8(std::wstring(buf));
                if (hwnd_content_list_) ListView_DeleteAllItems(hwnd_content_list_);
                for (const auto& e : content_items_) {
                    bool show = true;
                    if (!content_search_utf8_.empty()) {
                        const std::string& q = content_search_utf8_;
                        if (e.rel_utf8.find(q) == std::string::npos && e.label_utf8.find(q) == std::string::npos) show = false;
                    }
                    if (!show) continue;
                    LVITEMW it{};
                    it.mask = LVIF_TEXT;
                    it.iItem = ListView_GetItemCount(hwnd_content_list_);
                    std::wstring wrel = utf8_to_wide(e.rel_utf8);
                    it.pszText = (LPWSTR)wrel.c_str();
                    int row = ListView_InsertItem(hwnd_content_list_, &it);
                    std::wstring wlab = utf8_to_wide(e.label_utf8);
                    ListView_SetItemText(hwnd_content_list_, row, 1, (LPWSTR)wlab.c_str());
                }
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
                    std::wstring full;
                    if (fname.rfind(L"(fail) ", 0) == 0) {
                        fname = fname.substr(7);
                        full = L"Draft Container\\AssetSubstrate\\AI\\experiments\\metrics_failures\" + fname;
                    } else {
                        full = L"Draft Container\\AssetSubstrate\\AI\\experiments\\metrics\" + fname;
                    }
                    // Let Windows pick the default handler (usually opens in the editor).
                    ShellExecuteW(hwnd, L"open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                MarkAiExperimentsSeen();
            }

            if (id == 2002 && code == BN_CLICKED) { OnSend(); }
            if (id == 2003 && code == BN_CLICKED) { OnImportObj(); }
            if (id == 2006 && code == BN_CLICKED) { OnBootstrapGame(); }
            if (id == 2013 && code == BN_CLICKED) { OnApplyTransform(); }
            if (id == 2020 && code == BN_CLICKED) {
                editor_gizmo_mode_u8_ = 1;
                EmitEditorGizmo();
                AppendOutputUtf8("EDITOR: gizmo=Translate");
            }
            if (id == 2021 && code == BN_CLICKED) {
                editor_gizmo_mode_u8_ = 2;
                EmitEditorGizmo();
                AppendOutputUtf8("EDITOR: gizmo=Rotate");
            }
            if (id == 2022 && code == BN_CLICKED) {
                // Frame selection handled in Tick() using current cached object state.
                // We set a one-shot key to reuse the existing framing logic (F key).
                input_.key_down['F'] = true;

if (id == 2026 && code == BN_CLICKED) {
    editor_axis_constraint_u8_ = 0;
    EmitEditorAxisConstraint();
    AppendOutputUtf8("EDITOR: axis=None");
}
if (id == 2027 && code == BN_CLICKED) {
    editor_axis_constraint_u8_ = 1;
    EmitEditorAxisConstraint();
    AppendOutputUtf8("EDITOR: axis=X");
}
if (id == 2028 && code == BN_CLICKED) {
    editor_axis_constraint_u8_ = 2;
    EmitEditorAxisConstraint();
    AppendOutputUtf8("EDITOR: axis=Y");
}
if (id == 2029 && code == BN_CLICKED) {
    editor_axis_constraint_u8_ = 3;
    EmitEditorAxisConstraint();
    AppendOutputUtf8("EDITOR: axis=Z");
}
if (id == 2030 && code == BN_CLICKED) { EmitEditorUndo(); AppendOutputUtf8("EDITOR: undo"); }
if (id == 2031 && code == BN_CLICKED) { EmitEditorRedo(); AppendOutputUtf8("EDITOR: redo"); }

            }
            if (id == 2023 && code == BN_CLICKED) {
                const LRESULT v = SendMessageW(hwnd_snap_enable_, BM_GETCHECK, 0, 0);
                editor_snap_enabled_u8_ = (v == BST_CHECKED) ? 1 : 0;
                EmitEditorSnap();
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
                if (scene_) scene_->selected = sel;
                // Populate position fields from last-known projected state.
                if (scene_ && sel >= 0 && sel < (int)scene_->objects.size()) {
                    const auto& o = scene_->objects[(size_t)sel];
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
                }
            }
        } break;
        case WM_NOTIFY: {
            const NMHDR* nh = (const NMHDR*)lparam;
            if (nh && nh->code == NM_CUSTOMDRAW) {
                LRESULT res = 0;
                if (nh->hwndFrom == hwnd_rdock_tab_ || nh->hwndFrom == hwnd_ai_tab_) {
                    if (ew_tab_custom_draw(nh, &res)) return res;
                }
            }
            if (nh && hwnd_rdock_tab_ && nh->hwndFrom == hwnd_rdock_tab_ && nh->code == TCN_SELCHANGE) {
                const int sel = TabCtrl_GetCurSel(hwnd_rdock_tab_);
                if (sel >= 0) {
                    rdock_tab_index_u32_ = (uint32_t)sel;
                    if (hwnd_rdock_outliner_) ShowWindow(hwnd_rdock_outliner_, (sel == 0) ? SW_SHOW : SW_HIDE);
                    if (hwnd_rdock_details_)  ShowWindow(hwnd_rdock_details_,  (sel == 1) ? SW_SHOW : SW_HIDE);
                    if (hwnd_rdock_asset_)    ShowWindow(hwnd_rdock_asset_,    (sel == 2) ? SW_SHOW : SW_HIDE);
                    if (hwnd_rdock_voxel_)    ShowWindow(hwnd_rdock_voxel_,    (sel == 3) ? SW_SHOW : SW_HIDE);
                    LayoutChildren(client_w_, client_h_);
                }
                return 0;
            }
            if (nh && hwnd_ai_tab_ && nh->hwndFrom == hwnd_ai_tab_ && nh->code == TCN_SELCHANGE) {
                const int sel = TabCtrl_GetCurSel(hwnd_ai_tab_);
                if (sel >= 0) {
                    ai_tab_index_u32_ = (uint32_t)sel;
                    if (sel == 0) {
                        if (hwnd_ai_experiment_list_) ShowWindow(hwnd_ai_experiment_list_, SW_HIDE);
                        if (hwnd_ai_progress_overall_) ShowWindow(hwnd_ai_progress_overall_, SW_SHOW);
                        if (hwnd_ai_domain_list_) ShowWindow(hwnd_ai_domain_list_, SW_SHOW);
                        RefreshAiDomainProgressList();
                    } else {
                        if (hwnd_ai_progress_overall_) ShowWindow(hwnd_ai_progress_overall_, SW_HIDE);
                        if (hwnd_ai_domain_list_) ShowWindow(hwnd_ai_domain_list_, SW_HIDE);
                        if (hwnd_ai_experiment_list_) ShowWindow(hwnd_ai_experiment_list_, SW_SHOW);
                        RefreshAiExperimentList();
                        MarkAiExperimentsSeen();
                    }
                }
                return 0;
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
            if (id == 4055) { ew_draw_toggle_switch(dis, ai_learning_enabled_); return TRUE; }
            if (id == 4056) { ew_draw_toggle_switch(dis, ai_crawling_enabled_); return TRUE; }
        } break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (wparam < 256) input_.key_down[wparam] = true;
            input_.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            input_.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            input_.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            // Toggle immersion mode (Standard <-> Immersion)
            if (wparam == VK_F10) immersion_mode_ = !immersion_mode_;
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
        case WM_MBUTTONUP: input_.mmb = false; ReleaseCapture(); break;
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
