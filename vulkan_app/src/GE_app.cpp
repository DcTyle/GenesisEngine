#include "GE_app.hpp"

#include <vulkan/vulkan.h>
#include <shellscalingapi.h>
#include <commdlg.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>

#include "GE_runtime.hpp" // SubstrateMicroprocessor
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
    EigenWare::SubstrateMicroprocessor* sm = nullptr;
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

static void ew_open_bindings_editor(EigenWare::SubstrateMicroprocessor* sm, const std::string& path_utf8) {
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
    EigenWare::SubstrateMicroprocessor sm;
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
// Default level: Sun + Earth (minimum viable game world)
// ------------------------------------------------------------
// Distances are represented in meters (float) for the editor viewport.
// Simulation remains particle-based internally; these are initial object
// anchor conditions feeding the substrate update.
//
// Sun at origin; Earth at 1 AU on +X with tangential velocity +Y for a
// near-circular orbit. This demo uses an analytic Kepler orbit in the
// viewport projection layer; substrate-side interaction operators can
// replace this projection rule when enabled.
{
    objects.clear();
    selected = -1;

    Object sun{};
    sun.name_utf8 = "Sun";
    sun.object_id_u64 = next_object_id_u64++;
    sun.anchor_id_u32 = 0;
    sun.xf.pos[0] = 0.0f; sun.xf.pos[1] = 0.0f; sun.xf.pos[2] = 0.0f;
    sun.xf.rot_euler_deg[0] = 0.0f; sun.xf.rot_euler_deg[1] = 0.0f; sun.xf.rot_euler_deg[2] = 0.0f;
    sun.radius_m_f32 = 696340000.0f; // meters
    sun.albedo_rgba8 = 0xFF66CCFFu;
    sun.atmosphere_rgba8 = 0x00000000u;
    sun.atmosphere_thickness_m_f32 = 0.0f;
    sun.emissive_f32 = 40.0f;
    objects.push_back(sun);

    Object earth{};
    earth.name_utf8 = "Earth";
    earth.object_id_u64 = next_object_id_u64++;
    earth.anchor_id_u32 = 0;
    const float AU = 149597870700.0f; // meters
    earth.xf.pos[0] = AU; earth.xf.pos[1] = 0.0f; earth.xf.pos[2] = 0.0f;
    earth.xf.rot_euler_deg[0] = 0.0f; earth.xf.rot_euler_deg[1] = 0.0f; earth.xf.rot_euler_deg[2] = 0.0f;
    earth.radius_m_f32 = 6371000.0f; // meters
    earth.albedo_rgba8 = 0xFF2F6B2Fu; // green-ish
    earth.atmosphere_rgba8 = 0x80FFAA55u; // bluish with alpha
    earth.atmosphere_thickness_m_f32 = 100000.0f; // ~100km
    earth.emissive_f32 = 0.0f;
    objects.push_back(earth);

    selected = 1; // Earth selected by default
}
    }

    
void Tick() {
    sm.tick();

    // ------------------------------------------------------------
    // Minimal solar-system demo: Earth orbiting Sun.
    // ------------------------------------------------------------
    // Deterministic analytic orbit update driven by canonical tick cadence.
    // Genesis tick cadence: 360 Hz (viewport cadence is a projection of the
    // authoritative substrate tick).
    const double dt = 1.0 / 360.0; // seconds per tick
    const double year = 365.25 * 86400.0;
    const double omega = (2.0 * 3.14159265358979323846) / year;
    earth_orbit_angle_rad += omega * dt;
    // Wrap
    if (earth_orbit_angle_rad > 2.0 * 3.14159265358979323846) earth_orbit_angle_rad -= 2.0 * 3.14159265358979323846;

    const float AU = 149597870700.0f;
    if (objects.size() >= 2) {
        Object& earth = objects[1];
        earth.xf.pos[0] = (float)(AU * std::cos(earth_orbit_angle_rad));
        earth.xf.pos[1] = (float)(AU * std::sin(earth_orbit_angle_rad));
        earth.xf.pos[2] = 0.0f;
    }

    lattice.step_one_tick();
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

        const uint32_t anchor_id = (uint32_t)objects.size();
        if (anchor_id < sm.anchors.size()) {
            sm.anchors[anchor_id].object_id_u64 = object_id;
            sm.anchors[anchor_id].object_phase_seed_u64 = e.phase_seed_u64;
        }

        Object o;
        o.name_utf8 = path_utf8;
        o.mesh = std::move(m);
        o.object_id_u64 = object_id;
        o.anchor_id_u32 = anchor_id;
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

    VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyns;

    VkPipelineRenderingCreateInfo pr{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pr.colorAttachmentCount = 1;
    pr.pColorAttachmentFormats = &vk.swap_format;

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
    pci.pDynamicState = &ds;
    pci.layout = vk.pipe_layout;
    pci.renderPass = VK_NULL_HANDLE;
    pci.subpass = 0;

    vk_check(vkCreateGraphicsPipelines(vk.dev, VK_NULL_HANDLE, 1, &pci, nullptr, &vk.pipe), "vkCreateGraphicsPipelines");

    vkDestroyShaderModule(vk.dev, vs, nullptr);
    vkDestroyShaderModule(vk.dev, fs, nullptr);
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

    // Controls in panel
    hwnd_input_ = CreateWindowW(L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOVSCROLL,
                                10, 10, 400, 24,
                                hwnd_panel_, (HMENU)2001, GetModuleHandleW(nullptr), nullptr);

    hwnd_send_ = CreateWindowW(L"BUTTON", L"Send",
                               WS_CHILD | WS_VISIBLE,
                               10, 40, 80, 26,
                               hwnd_panel_, (HMENU)2002, GetModuleHandleW(nullptr), nullptr);

    hwnd_import_ = CreateWindowW(L"BUTTON", L"Import OBJ",
                                 WS_CHILD | WS_VISIBLE,
                                 100, 40, 110, 26,
                                 hwnd_panel_, (HMENU)2003, GetModuleHandleW(nullptr), nullptr);

    hwnd_bootstrap_ = CreateWindowW(L\"BUTTON\", L\"Bootstrap Engine\",
                               WS_CHILD | WS_VISIBLE,
                               220, 40, 190, 26,
                               hwnd_panel_, (HMENU)2006, GetModuleHandleW(nullptr), nullptr);

    hwnd_objlist_ = CreateWindowW(L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                                  10, 76, 400, 180,
                                  hwnd_panel_, (HMENU)2004, GetModuleHandleW(nullptr), nullptr);

    hwnd_output_ = CreateWindowW(L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                                 10, 266, 400, client_h_ - 276,
                                 hwnd_panel_, (HMENU)2005, GetModuleHandleW(nullptr), nullptr);

    LayoutChildren(client_w_, client_h_);
}

void App::LayoutChildren(int w, int h) {
    int panel_w = 420;
    MoveWindow(hwnd_viewport_, 0, 0, w - panel_w, h, TRUE);
    MoveWindow(hwnd_panel_, w - panel_w, 0, panel_w, h, TRUE);

    MoveWindow(hwnd_output_, 10, 266, panel_w - 20, h - 276, TRUE);
}

int App::Run(HINSTANCE hInst) {
    // Win64 enforcement at runtime too (belt + suspenders)
    static_assert(sizeof(void*) == 8, "Win64 required");

    CreateMainWindow(hInst);
    CreateChildWindows();

    scene_ = new Scene();

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
        // View-driven LOD: distance + focus (deterministic, meters).
        const float dist_m = std::sqrt(dx*dx + dy*dy + dz*dz);
        const float d_focus = cam_.focal_distance_m;
        const float w_focus = (cam_.focus_band_m > 1.0e-6f) ? cam_.focus_band_m : 0.06f;
        const float focus_w = std::fmax(0.0f, 1.0f - std::fabs(dist_m - d_focus) / w_focus);
        const float d_min32k = 0.0762f;  // 0.25 ft
        const float d_max32k = 0.3048f;  // 1.0 ft
        float near_w = 0.0f;
        if (dist_m <= d_min32k) near_w = 1.0f;
        else if (dist_m < d_max32k) near_w = (d_max32k - dist_m) / (d_max32k - d_min32k);
        // Screen-space proxy: larger projected objects justify higher LOD.
        const float screen_w = std::fmin(1.0f, std::fmax(0.0f, (o.radius_m_f32 / std::fmax(dist_m, 1.0e-3f)) * 8.0f));
        const float clarity = std::fmin(1.0f, std::fmax(0.0f, focus_w * near_w * screen_w));
        const float lod_bias = -cam_.lod_boost_max * clarity;

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
        auto build_instances_for_eye = [&](const float eye_pos[3], const float eye_quat[4]) {
            // Eye forward (-Z) from quaternion.
            const float x = eye_quat[0], y = eye_quat[1], z = eye_quat[2], w = eye_quat[3];
            float vx = 0.f, vy = 0.f, vz = -1.f;
            float tx = 2.f * (y*vz - z*vy);
            float ty = 2.f * (z*vx - x*vz);
            float tz = 2.f * (x*vy - y*vx);
            float fx = vx + w*tx + (y*tz - z*ty);
            float fy = vy + w*ty + (z*tx - x*tz);
            float fz = vz + w*tz + (x*ty - y*tx);
            float fl = std::sqrt(fx*fx + fy*fy + fz*fz);
            if (fl > 1e-6f) { fx/=fl; fy/=fl; fz/=fl; }
            float right[3] = { fy, -fx, 0.f };
            float rl = std::sqrt(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
            if (rl > 1e-6f) { right[0]/=rl; right[1]/=rl; right[2]/=rl; }
            float up[3] = {
                right[1]*fz - right[2]*fy,
                right[2]*fx - right[0]*fz,
                right[0]*fy - right[1]*fx
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
                const float dx = o.xf.pos[0] - eye_pos[0];
                const float dy = o.xf.pos[1] - eye_pos[1];
                const float dz = o.xf.pos[2] - eye_pos[2];
                const float cx = dx*right[0] + dy*right[1] + dz*right[2];
                const float cy = dx*up[0]    + dy*up[1]    + dz*up[2];
                const float cz = dx*fx       + dy*fy       + dz*fz;

                // View-driven LOD uses the app camera's focus distance/band.
                const float dist_m = std::sqrt(dx*dx + dy*dy + dz*dz);
                const float d_focus = cam_.focal_distance_m;
                const float w_focus = (cam_.focus_band_m > 1.0e-6f) ? cam_.focus_band_m : 0.06f;
                const float focus_w = std::fmax(0.0f, 1.0f - std::fabs(dist_m - d_focus) / w_focus);
                const float d_min32k = 0.0762f;
                const float d_max32k = 0.3048f;
                float near_w = 0.0f;
                if (dist_m <= d_min32k) near_w = 1.0f;
                else if (dist_m < d_max32k) near_w = (d_max32k - dist_m) / (d_max32k - d_min32k);

                // Screen-space proxy based on radius and depth.
                float screen_w = 0.0f;
                if (std::fabs(cz) > 1e-3f) {
                    screen_w = std::fmin(1.0f, (o.radius_m_f32 / std::fabs(cz)) * 10.0f);
                }

                const float clarity = std::fmax(0.0f, std::fmin(1.0f, focus_w * near_w * screen_w));
                const float lod_bias = -cam_.lod_boost_max * clarity;

                EwRenderInstance inst{};
                inst.object_id_u64 = o.object_id_u64;
                inst.anchor_id_u32 = o.anchor_id_u32;
                if (o.name_utf8 == "Sun") inst.kind_u32 = 1u;
                else if (o.name_utf8 == "Earth") inst.kind_u32 = 2u;
                inst.albedo_rgba8 = o.albedo_rgba8;
                inst.atmosphere_rgba8 = o.atmosphere_rgba8;
                inst.rel_pos_q16_16[0] = (int32_t)std::llround(cx * 65536.0);
                inst.rel_pos_q16_16[1] = (int32_t)std::llround(cy * 65536.0);
                inst.rel_pos_q16_16[2] = (int32_t)std::llround(cz * 65536.0);
                inst.radius_q16_16 = (int32_t)std::llround(o.radius_m_f32 * 65536.0);
                inst.emissive_q16_16 = (int32_t)std::llround(o.emissive_f32 * 65536.0);
                inst.atmosphere_thickness_q16_16 = (int32_t)std::llround(o.atmosphere_thickness_m_f32 * 65536.0);
                inst.lod_bias_q16_16 = (int32_t)std::llround(lod_bias * 65536.0);
                inst.clarity_q16_16 = (int32_t)std::llround(clarity * 65536.0);

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
            build_instances_for_eye(pos, quat);

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

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
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

    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.offset = {0,0};
    ri.renderArea.extent = vk_->swap_extent;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;

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


void App::AppendOutputUtf8(const std::string& line) {
    std::wstring wline = utf8_to_wide(line);
    wline.append(L"\r\n");

    int len = GetWindowTextLengthW(hwnd_output_);
    SendMessageW(hwnd_output_, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hwnd_output_, EM_REPLACESEL, FALSE, (LPARAM)wline.c_str());
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
            if (id == 2002 && code == BN_CLICKED) { OnSend(); }
            if (id == 2003 && code == BN_CLICKED) { OnImportObj(); }
            if (id == 2006 && code == BN_CLICKED) { OnBootstrapGame(); }
            if (id == 2004 && code == LBN_SELCHANGE) {
                int sel = (int)SendMessageW(hwnd_objlist_, LB_GETCURSEL, 0, 0);
                if (scene_) scene_->selected = sel;
            }
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