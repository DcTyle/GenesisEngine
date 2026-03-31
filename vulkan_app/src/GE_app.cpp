#include "GE_app.hpp"

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#include <vulkan/vulkan.h>
#include <shellscalingapi.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>

#include "GE_runtime.hpp" // SubstrateManager
#include "GE_research_confinement.hpp"
#include "ew_runtime.h"      // ew_runtime_project_points
#include "GE_object_memory.hpp"
#include "field_lattice_cpu.hpp"
#include "obj_import.hpp"
#include "GE_render_instance.hpp"
#include "ewmesh_voxelizer.hpp"
#include "carrier_bundle_cuda.hpp"
#include "GE_control_packets.hpp"
#include "VirtualStateDrive.hpp"
#include "ew_id9.hpp"

#include <fstream>

#ifndef EW_SHADER_OUT_DIR
#define EW_SHADER_OUT_DIR "."
#endif

#pragma comment(lib, "Shcore.lib")

namespace ewv {

extern "C" {
// Hint hybrid-GPU systems to prefer the high-performance adapter.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

static std::wstring utf8_to_wide(const std::string& s);
static std::string wide_to_utf8(const std::wstring& s);

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
    SubstrateManager* sm = nullptr;
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

static void ew_open_bindings_editor(SubstrateManager* sm, const std::string& path_utf8) {
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
    SubstrateManager sm;
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
    genesis::GeResearchConfinementArchive research_archive;
    genesis::GeResearchAudioFrame research_audio_frame;
    std::vector<genesis::GeResearchParticleVizPoint> research_points;
    std::vector<std::string> local_output_lines;
    std::filesystem::path research_watch_file;
    std::filesystem::path research_cmd_file;
    std::filesystem::file_time_type research_cmd_write_time{};
    std::filesystem::file_time_type research_watch_write_time{};
    uint64_t research_cmd_read_offset_u64 = 0u;
    uint64_t research_reload_probe_tick_u64 = 0u;
    uint64_t research_cmd_probe_tick_u64 = 0u;
    bool research_visual_stream_enabled = true;
    bool research_use_compute_audio = true;
    uint32_t research_visual_stream_hz_u32 = 360u;
    size_t research_visual_max_points = 20000u;
    uint64_t research_visual_last_tick_u64 = 0u;
    uint64_t research_guidance_last_pulse_tick_u64 = 0u;
    uint64_t research_gpu_prediction_last_tick_u64 = 0u;
    uint8_t research_output_mode_u8 = 1u; // 1=frequency/all, 2=vector-only extrapolation
    bool research_stov_mode = false;
    bool research_live_boot_enabled = true;
    bool research_guidance_enabled = true;
    bool research_live_compute_enabled = true;
    bool research_live_compute_active = false;
    bool research_basic_trial_enabled = false;
    bool research_basic_trial_expired = false;
    bool research_basic_trial_expiry_announced = false;
    bool research_guidance_has_pulse_history = false;
    bool research_guidance_watchdog_fault = false;
    float research_param_a = 0.19f;
    float research_param_f = 0.245f;
    float research_param_i = 0.35f;
    float research_param_v = 0.35f;
    uint32_t research_lattice_edge_u32 = 256u;
    uint32_t research_gpu_prediction_axis_resolution_u32 = 6u;
    uint32_t research_gpu_prediction_refresh_ticks_u32 = 120u;
    uint32_t research_basic_trial_minutes_u32 = 60u;
    uint32_t research_guidance_watchdog_fault_count_u32 = 0u;
    uint64_t research_guidance_watchdog_last_fault_tick_u64 = 0u;
    uint64_t research_live_compute_activation_tick_u64 = 0u;
    uint64_t research_basic_trial_start_tick_u64 = 0u;
    uint64_t research_basic_trial_end_tick_u64 = 0u;
    genesis::VirtualStateDrive research_runtime_vsd;
    genesis::GeResearchRuntimeGuidance research_guidance;
    genesis::GeResearchGpuAdaptiveCalibration research_gpu_calibration;
    genesis::GeResearchLiveComputePlan research_live_compute_plan;
    std::vector<genesis::GeResearchInterferencePredictionCell> research_gpu_predictions;
    uint64_t research_sx_q32_32_u64 = 1ull << 32u;
    uint64_t research_sy_q32_32_u64 = 1ull << 32u;
    uint64_t research_sz_q32_32_u64 = 1ull << 32u;
    double research_lattice_probe_energy_norm = 0.0;
    double research_lattice_probe_flux_norm = 0.0;
    double research_lattice_probe_interference_norm = 0.0;
    double research_lattice_probe_temporal_coupling_norm = 0.0;
    std::chrono::steady_clock::time_point research_basic_trial_start_time{};
    bool research_run_log_boot_emitted = false;
    bool research_run_log_last_live_compute_active = false;
    uint64_t research_run_log_last_prediction_tick_u64 = 0u;
    uint64_t research_verification_emit_last_tick_u64 = 0u;
    EwId9 research_last_state_sig9{};

    void PushLocalUiLine(const std::string& line_utf8) {
        if (line_utf8.empty()) return;
        if (local_output_lines.size() >= 64u) {
            local_output_lines.erase(local_output_lines.begin());
        }
        local_output_lines.push_back(line_utf8);
    }

    static uint32_t ClampLatticeEdge(uint32_t edge_u32) {
        if (edge_u32 < 256u) return 256u;
        if (edge_u32 > 20000u) return 20000u;
        return edge_u32;
    }

    static uint32_t LatticeEdgeFromScaleStep(uint32_t step_u32) {
        // Requested policy: 3((D1)n^2) growth; we model this as 256*n^2 for isotropic 3D scaling.
        const uint64_t n = (uint64_t)std::max<uint32_t>(step_u32, 1u);
        const uint64_t edge = 256ull * n * n;
        return ClampLatticeEdge((uint32_t)std::min<uint64_t>(edge, 20000ull));
    }

    static double ClampNorm01(double v) {
        if (v < 0.0) return 0.0;
        if (v > 1.0) return 1.0;
        return v;
    }

    static uint16_t PackNormU16(double v) {
        const double n = ClampNorm01(v);
        return (uint16_t)std::lround(n * 65535.0);
    }

    static int32_t PackFreqCode(double v) {
        const double n = ClampNorm01(v);
        return (int32_t)std::lround(n * 4096.0);
    }

    static uint64_t PackFreqHz(double v) {
        const uint64_t hz = (uint64_t)std::llround(ClampNorm01(v) * 4096.0);
        return std::max<uint64_t>(hz, 1u);
    }

    static double NormFromAbs(double v) {
        const double av = std::abs(v);
        return av / (1.0 + av);
    }

    static uint64_t PackPositiveQ32_32(double v) {
        const double clamped = std::max(0.0, v);
        const long double scaled = (long double)clamped * 4294967296.0L;
        if (scaled >= (long double)UINT64_MAX) return UINT64_MAX;
        return (uint64_t)std::llround((double)scaled);
    }

    static double UnpackPositiveQ32_32(uint64_t v) {
        return (double)v / 4294967296.0;
    }

    static uint32_t ClampResearchBasicTrialMinutes(uint32_t minutes_u32) {
        return std::max<uint32_t>(1u, std::min<uint32_t>(minutes_u32, 240u));
    }

    static uint32_t ResearchBasicTrialMaxStreamHz() {
        return 120u;
    }

    static uint32_t ResearchBasicTrialMaxLatticeEdge() {
        return 1024u;
    }

    double ResearchBasicTrialRemainingSeconds() const {
        if (!research_basic_trial_enabled || research_basic_trial_expired) return 0.0;
        if (research_basic_trial_start_tick_u64 == 0u && research_basic_trial_end_tick_u64 == 0u) return 0.0;
        const auto now = std::chrono::steady_clock::now();
        const double elapsed_seconds =
            std::chrono::duration<double>(now - research_basic_trial_start_time).count();
        const double total_seconds = (double)research_basic_trial_minutes_u32 * 60.0;
        return std::max(0.0, total_seconds - elapsed_seconds);
    }

    bool ResearchBasicTrialAllowsMutation() const {
        return !research_basic_trial_enabled || !research_basic_trial_expired;
    }

    static bool ResearchCommandMatchesPrefix(const std::string& utf8, const char* prefix_ascii) {
        return utf8.rfind(prefix_ascii, 0) == 0;
    }

    static bool ResearchCommandLooksLikeStatus(const std::string& utf8) {
        return ResearchCommandMatchesPrefix(utf8, "/basic_trial_status") ||
               ResearchCommandMatchesPrefix(utf8, "basic_trial_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/meta_basic_mode_status") ||
               ResearchCommandMatchesPrefix(utf8, "meta_basic_mode_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/sim_freq_status") ||
               ResearchCommandMatchesPrefix(utf8, "sim_freq_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/research_status") ||
               ResearchCommandMatchesPrefix(utf8, "research_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/research_guidance_status") ||
               ResearchCommandMatchesPrefix(utf8, "research_guidance_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/research_gpu_calibration_status") ||
               ResearchCommandMatchesPrefix(utf8, "research_gpu_calibration_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/research_live_compute_status") ||
               ResearchCommandMatchesPrefix(utf8, "research_live_compute_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/process_substrate_status") ||
               ResearchCommandMatchesPrefix(utf8, "process_substrate_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/subsystem_substrate_status") ||
               ResearchCommandMatchesPrefix(utf8, "subsystem_substrate_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/ai_status") ||
               ResearchCommandMatchesPrefix(utf8, "ai_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/ai_substrate_status") ||
               ResearchCommandMatchesPrefix(utf8, "ai_substrate_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/ai_data_status") ||
               ResearchCommandMatchesPrefix(utf8, "ai_data_status:") ||
               ResearchCommandMatchesPrefix(utf8, "/ai_data_substrate_status") ||
               ResearchCommandMatchesPrefix(utf8, "ai_data_substrate_status:");
    }

    bool ResearchBasicTrialShouldBlockAdvancedCommand(const std::string& utf8) const {
        if (!research_basic_trial_enabled) return false;
        if (ResearchCommandLooksLikeStatus(utf8)) return false;
        return ResearchCommandMatchesPrefix(utf8, "/research_reload") ||
               ResearchCommandMatchesPrefix(utf8, "research_reload:") ||
               ResearchCommandMatchesPrefix(utf8, "/sim_freq_reload") ||
               ResearchCommandMatchesPrefix(utf8, "sim_freq_reload:") ||
               ResearchCommandMatchesPrefix(utf8, "/research_live_mode") ||
               ResearchCommandMatchesPrefix(utf8, "research_live_mode:") ||
               ResearchCommandMatchesPrefix(utf8, "/research_live_compute") ||
               ResearchCommandMatchesPrefix(utf8, "research_live_compute:") ||
               ResearchCommandMatchesPrefix(utf8, "/research_gpu_prediction_axis") ||
               ResearchCommandMatchesPrefix(utf8, "research_gpu_prediction_axis:") ||
               ResearchCommandMatchesPrefix(utf8, "/research_gpu_prediction_refresh") ||
               ResearchCommandMatchesPrefix(utf8, "research_gpu_prediction_refresh:") ||
               ResearchCommandMatchesPrefix(utf8, "/sim_freq_run") ||
               ResearchCommandMatchesPrefix(utf8, "sim_freq_run:");
    }

    void PushResearchBasicTrialBlocked(const std::string& target_utf8,
                                       const std::string& reason_utf8) {
        std::ostringstream oss;
        oss << "META_BASIC_TRIAL:blocked target=" << target_utf8
            << " reason=" << reason_utf8
            << " remaining_s=" << ResearchBasicTrialRemainingSeconds();
        PushLocalUiLine(oss.str());
    }

    static bool IsFiniteResearchQuartet(const genesis::GeResearchPulseQuartet& q) {
        return std::isfinite(q.F) &&
               std::isfinite(q.A) &&
               std::isfinite(q.I) &&
               std::isfinite(q.V);
    }

    void UpdateResearchAxisScaleState(const genesis::GeResearchPulseQuartet& quartet) {
        const double sx = std::max(0.125, 0.50 + 1.50 * ClampNorm01(quartet.F));
        const double sy = std::max(0.125, 0.50 + 1.50 * ClampNorm01(quartet.A));
        const double sz = std::max(0.125, 0.45 + 0.85 * ClampNorm01(quartet.F) + 0.70 * ClampNorm01(quartet.A));
        research_sx_q32_32_u64 = PackPositiveQ32_32(sx);
        research_sy_q32_32_u64 = PackPositiveQ32_32(sy);
        research_sz_q32_32_u64 = PackPositiveQ32_32(sz);
    }

    void UpdateResearchPredictionLatticeProbe(const EwProcessSubstrateTelemetry* telemetry_opt,
                                              const genesis::GeResearchPulseQuartet& desired) {
        UpdateResearchAxisScaleState(desired);

        const double gpu_freq_norm =
            telemetry_opt ? ((double)telemetry_opt->gpu_freq_norm_q15 / 65535.0) : desired.F;
        const double gpu_amp_norm =
            telemetry_opt ? ((double)telemetry_opt->gpu_amp_norm_q15 / 65535.0) : desired.A;
        const double gpu_curr_norm =
            telemetry_opt ? ((double)telemetry_opt->last_i_code_u16 / 65535.0) : desired.I;
        const double gpu_volt_norm =
            telemetry_opt ? ((double)telemetry_opt->gpu_volt_norm_q15 / 65535.0) : desired.V;
        const double interference_norm =
            telemetry_opt ? ((double)telemetry_opt->interference_norm_q15 / 65535.0) : 0.0;
        const double coherence_norm =
            telemetry_opt ? ((double)telemetry_opt->coherence_norm_q15 / 65535.0) : 0.0;
        const double source_vibration_norm =
            telemetry_opt ? ((double)telemetry_opt->source_vibration_q15 / 65535.0) : 0.0;

        const double sx = UnpackPositiveQ32_32(research_sx_q32_32_u64);
        const double sy = UnpackPositiveQ32_32(research_sy_q32_32_u64);
        const double sz = UnpackPositiveQ32_32(research_sz_q32_32_u64);
        const double text_drive_n =
            ClampNorm01(0.62 * ClampNorm01(desired.F * sx) + 0.20 * gpu_freq_norm + 0.18 * interference_norm);
        const double image_drive_n =
            ClampNorm01(0.60 * ClampNorm01(desired.A * sy) + 0.22 * gpu_amp_norm + 0.18 * gpu_volt_norm);
        const double audio_drive_n =
            ClampNorm01(0.56 * ClampNorm01(desired.I * sz) + 0.24 * gpu_curr_norm + 0.20 * source_vibration_norm);

        lattice.inject_text_amplitude_q32_32(q32_32_from_f32((float)text_drive_n));
        lattice.inject_image_amplitude_q32_32(q32_32_from_f32((float)image_drive_n));
        lattice.inject_audio_amplitude_q32_32(q32_32_from_f32((float)audio_drive_n));
        lattice.step_one_tick();

        const std::vector<float>& e_curr = lattice.E_curr();
        const std::vector<float>& flux = lattice.flux();
        if (e_curr.empty() || flux.empty() || e_curr.size() != flux.size()) {
            research_lattice_probe_energy_norm = 0.0;
            research_lattice_probe_flux_norm = 0.0;
            research_lattice_probe_interference_norm = 0.0;
            research_lattice_probe_temporal_coupling_norm = 0.0;
            return;
        }

        constexpr int gx = 128;
        constexpr int gy = 128;
        constexpr int gz = 128;
        const auto idx = [](int x, int y, int z) -> size_t {
            return (size_t)((z * gy + y) * gx + x);
        };
        const int cx = gx / 2;
        const int cy = gy / 2;
        const int cz = gz / 2;
        double sum_energy = 0.0;
        double sum_flux = 0.0;
        double sum_gradient = 0.0;
        size_t sample_count = 0u;
        for (int z = cz - 1; z <= cz + 1; ++z) {
            for (int y = cy - 1; y <= cy + 1; ++y) {
                for (int x = cx - 1; x <= cx + 1; ++x) {
                    const size_t i = idx(x, y, z);
                    const size_t ixm = idx(x - 1, y, z);
                    const size_t ixp = idx(x + 1, y, z);
                    const size_t iym = idx(x, y - 1, z);
                    const size_t iyp = idx(x, y + 1, z);
                    const size_t izm = idx(x, y, z - 1);
                    const size_t izp = idx(x, y, z + 1);
                    const double e_abs = std::abs((double)e_curr[i]);
                    const double f_abs = std::abs((double)flux[i]);
                    const double gradient =
                        std::abs((double)e_curr[ixp] - (double)e_curr[ixm]) +
                        std::abs((double)e_curr[iyp] - (double)e_curr[iym]) +
                        std::abs((double)e_curr[izp] - (double)e_curr[izm]);
                    sum_energy += e_abs;
                    sum_flux += f_abs;
                    sum_gradient += gradient;
                    ++sample_count;
                }
            }
        }

        const double inv_count = (sample_count > 0u) ? (1.0 / (double)sample_count) : 0.0;
        research_lattice_probe_energy_norm = NormFromAbs(sum_energy * inv_count);
        research_lattice_probe_flux_norm = NormFromAbs(sum_flux * inv_count);
        const double gradient_norm = NormFromAbs(sum_gradient * inv_count);
        research_lattice_probe_interference_norm =
            ClampNorm01(0.34 * research_lattice_probe_energy_norm +
                        0.30 * gradient_norm +
                        0.20 * research_lattice_probe_flux_norm +
                        0.10 * coherence_norm +
                        0.06 * source_vibration_norm);
        research_lattice_probe_temporal_coupling_norm =
            ClampNorm01(0.40 * research_lattice_probe_interference_norm +
                        0.24 * source_vibration_norm +
                        0.18 * research_lattice_probe_flux_norm +
                        0.10 * coherence_norm +
                        0.08 * interference_norm);
    }

    std::filesystem::path ResearchRootDir() const {
        std::filesystem::path root = research_archive.root_dir;
        if (root.empty()) root = std::filesystem::current_path() / "ResearchConfinement";
        return root;
    }

    static std::string JsonEscapeUtf8(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8u);
        for (char c : s) {
            switch (c) {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if ((unsigned char)c < 0x20u) out += '?';
                    else out.push_back(c);
                    break;
            }
        }
        return out;
    }

    static std::string Sig9CompactUtf8(const EwId9& sig9) {
        std::ostringstream oss;
        for (size_t i = 0u; i < sig9.u32.size(); ++i) {
            if (i != 0u) oss << ',';
            oss << sig9.u32[i];
        }
        return oss.str();
    }

    void AppendResearchRunLogLine(const std::string& line_utf8) const {
        const std::filesystem::path root = ResearchRootDir();
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        std::ofstream f(root / "run.log", std::ios::out | std::ios::app);
        if (!f.good()) return;
        f << line_utf8 << '\n';
    }

    static bool WritePpmRgb(const std::filesystem::path& out_path,
                            uint32_t width_u32,
                            uint32_t height_u32,
                            const std::vector<uint8_t>& rgb) {
        if (width_u32 == 0u || height_u32 == 0u) return false;
        if (rgb.size() < (size_t)width_u32 * (size_t)height_u32 * 3u) return false;
        std::ofstream f(out_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!f.good()) return false;
        f << "P6\n" << width_u32 << ' ' << height_u32 << "\n255\n";
        f.write(reinterpret_cast<const char*>(rgb.data()),
                (std::streamsize)((size_t)width_u32 * (size_t)height_u32 * 3u));
        return f.good();
    }

    void PersistResearchLatticeProjectionImage() {
        EwFieldFrameHeader hdr{};
        std::vector<uint8_t> bgra;
        lattice.get_radiance_slice_bgra8(64u, bgra, hdr);
        const size_t pixel_count = (size_t)hdr.grid_x * (size_t)hdr.grid_y;
        if (pixel_count == 0u || bgra.size() < pixel_count * 4u) return;

        std::vector<uint8_t> rgb(pixel_count * 3u, 0u);
        for (size_t i = 0u; i < pixel_count; ++i) {
            const size_t src = i * 4u;
            const size_t dst = i * 3u;
            rgb[dst + 0u] = bgra[src + 2u];
            rgb[dst + 1u] = bgra[src + 1u];
            rgb[dst + 2u] = bgra[src + 0u];
        }
        (void)WritePpmRgb(ResearchRootDir() / "lattice_projection.ppm", hdr.grid_x, hdr.grid_y, rgb);
    }

    void PersistResearchObjectReferenceImage() const {
        std::vector<uint64_t> object_ids;
        sm.object_store.list_object_ids_sorted(object_ids);

        const uint32_t tile_w_u32 = 64u;
        const uint32_t tile_h_u32 = 64u;
        const uint32_t count_u32 = object_ids.empty() ? 1u : (uint32_t)object_ids.size();
        const uint32_t cols_u32 = (uint32_t)std::max(1.0, std::ceil(std::sqrt((double)count_u32)));
        const uint32_t rows_u32 = (count_u32 + cols_u32 - 1u) / cols_u32;
        const uint32_t width_u32 = cols_u32 * tile_w_u32;
        const uint32_t height_u32 = rows_u32 * tile_h_u32;
        std::vector<uint8_t> rgb((size_t)width_u32 * (size_t)height_u32 * 3u, 12u);

        auto set_pixel = [&](uint32_t x_u32, uint32_t y_u32, uint8_t r_u8, uint8_t g_u8, uint8_t b_u8) {
            if (x_u32 >= width_u32 || y_u32 >= height_u32) return;
            const size_t idx = ((size_t)y_u32 * (size_t)width_u32 + (size_t)x_u32) * 3u;
            rgb[idx + 0u] = r_u8;
            rgb[idx + 1u] = g_u8;
            rgb[idx + 2u] = b_u8;
        };

        for (uint32_t oi = 0u; oi < count_u32; ++oi) {
            const uint64_t object_id_u64 = object_ids.empty() ? 0u : object_ids[oi];
            const uint32_t tile_x_u32 = (oi % cols_u32) * tile_w_u32;
            const uint32_t tile_y_u32 = (oi / cols_u32) * tile_h_u32;

            for (uint32_t y = 0u; y < tile_h_u32; ++y) {
                for (uint32_t x = 0u; x < tile_w_u32; ++x) {
                    set_pixel(tile_x_u32 + x, tile_y_u32 + y, 18u, 18u, 22u);
                }
            }

            const uint8_t id_r_u8 = (uint8_t)(object_id_u64 & 0xFFu);
            const uint8_t id_g_u8 = (uint8_t)((object_id_u64 >> 8u) & 0xFFu);
            const uint8_t id_b_u8 = (uint8_t)((object_id_u64 >> 16u) & 0xFFu);
            bool drew_reference = false;

            uint32_t atlas_w_u32 = 0u;
            uint32_t atlas_h_u32 = 0u;
            uint32_t atlas_format_u32 = 0u;
            const uint8_t* atlas_bytes = nullptr;
            size_t atlas_byte_count = 0u;
            if (!object_ids.empty() &&
                sm.object_store.view_uv_atlas(object_id_u64,
                                              atlas_w_u32,
                                              atlas_h_u32,
                                              atlas_format_u32,
                                              atlas_bytes,
                                              atlas_byte_count) &&
                atlas_format_u32 == 1u &&
                atlas_bytes != nullptr &&
                atlas_byte_count >= (size_t)atlas_w_u32 * (size_t)atlas_h_u32 * 4u &&
                atlas_w_u32 != 0u &&
                atlas_h_u32 != 0u) {
                for (uint32_t y = 0u; y < tile_h_u32; ++y) {
                    const uint32_t sy_u32 = (uint32_t)(((uint64_t)y * atlas_h_u32) / tile_h_u32);
                    for (uint32_t x = 0u; x < tile_w_u32; ++x) {
                        const uint32_t sx_u32 = (uint32_t)(((uint64_t)x * atlas_w_u32) / tile_w_u32);
                        const size_t src = ((size_t)sy_u32 * (size_t)atlas_w_u32 + (size_t)sx_u32) * 4u;
                        set_pixel(tile_x_u32 + x,
                                  tile_y_u32 + y,
                                  atlas_bytes[src + 0u],
                                  atlas_bytes[src + 1u],
                                  atlas_bytes[src + 2u]);
                    }
                }
                drew_reference = true;
            }

            EwVoxelVolumeView vv{};
            if (!drew_reference &&
                !object_ids.empty() &&
                sm.object_store.view_voxel_volume(object_id_u64, vv) &&
                vv.bytes != nullptr &&
                vv.byte_count >= (size_t)vv.grid_x_u32 * (size_t)vv.grid_y_u32 * (size_t)vv.grid_z_u32 &&
                vv.grid_x_u32 != 0u &&
                vv.grid_y_u32 != 0u &&
                vv.grid_z_u32 != 0u) {
                const uint32_t slice_z_u32 = vv.grid_z_u32 / 2u;
                for (uint32_t y = 0u; y < tile_h_u32; ++y) {
                    const uint32_t sy_u32 = (uint32_t)(((uint64_t)y * vv.grid_y_u32) / tile_h_u32);
                    for (uint32_t x = 0u; x < tile_w_u32; ++x) {
                        const uint32_t sx_u32 = (uint32_t)(((uint64_t)x * vv.grid_x_u32) / tile_w_u32);
                        const size_t src = ((size_t)slice_z_u32 * (size_t)vv.grid_y_u32 + (size_t)sy_u32) *
                                               (size_t)vv.grid_x_u32 +
                                           (size_t)sx_u32;
                        const uint8_t occ_u8 = vv.bytes[src];
                        set_pixel(tile_x_u32 + x,
                                  tile_y_u32 + y,
                                  occ_u8,
                                  (uint8_t)((uint16_t)occ_u8 / 2u + (uint16_t)id_g_u8 / 2u),
                                  (uint8_t)((uint16_t)occ_u8 / 2u + (uint16_t)id_b_u8 / 2u));
                    }
                }
                drew_reference = true;
            }

            if (!drew_reference) {
                for (uint32_t y = 0u; y < tile_h_u32; ++y) {
                    for (uint32_t x = 0u; x < tile_w_u32; ++x) {
                        const uint8_t shade_u8 =
                            (uint8_t)(((uint64_t)(x + 1u) * (uint64_t)(y + 3u) + object_id_u64) & 0x3Fu);
                        set_pixel(tile_x_u32 + x,
                                  tile_y_u32 + y,
                                  (uint8_t)std::min<uint32_t>(255u, (uint32_t)id_r_u8 + shade_u8),
                                  (uint8_t)std::min<uint32_t>(255u, (uint32_t)id_g_u8 + shade_u8 / 2u),
                                  (uint8_t)std::min<uint32_t>(255u, (uint32_t)id_b_u8 + shade_u8));
                    }
                }
            }

            for (uint32_t x = 0u; x < tile_w_u32; ++x) {
                set_pixel(tile_x_u32 + x, tile_y_u32 + 0u, 255u, 255u, 255u);
                set_pixel(tile_x_u32 + x, tile_y_u32 + tile_h_u32 - 1u, 255u, 255u, 255u);
            }
            for (uint32_t y = 0u; y < tile_h_u32; ++y) {
                set_pixel(tile_x_u32 + 0u, tile_y_u32 + y, 255u, 255u, 255u);
                set_pixel(tile_x_u32 + tile_w_u32 - 1u, tile_y_u32 + y, 255u, 255u, 255u);
            }
        }

        (void)WritePpmRgb(ResearchRootDir() / "object_reference.ppm", width_u32, height_u32, rgb);
    }

    std::string BuildResearchRuntimeStateCore(const EwProcessSubstrateTelemetry& process,
                                              const EwSubsystemSubstrateTelemetry& subsystem,
                                              const EwAiSubstrateTelemetry& ai,
                                              const EwAiDataSubstrateTelemetry& ai_data,
                                              const std::vector<uint64_t>& object_ids) const {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(6);
        oss << "tick=" << sm.canonical_tick_u64() << '\n'
            << "anchors=" << sm.anchors.size() << '\n'
            << "objects=" << object_ids.size() << '\n'
            << "live_boot=" << (research_live_boot_enabled ? 1 : 0) << '\n'
            << "guidance=" << (research_guidance_enabled ? 1 : 0) << '\n'
            << "live_compute_enabled=" << (research_live_compute_enabled ? 1 : 0) << '\n'
            << "live_compute_active=" << (research_live_compute_active ? 1 : 0) << '\n'
            << "basic_trial=" << (research_basic_trial_enabled ? 1 : 0) << '\n'
            << "params=" << research_param_f << ',' << research_param_a << ',' << research_param_i << ',' << research_param_v << '\n'
            << "guidance=" << research_guidance.certainty_norm << ',' << research_guidance.exactness_norm << ','
            << research_guidance.temporal_coupling_norm << ',' << research_guidance.correction_authority_norm << '\n'
            << "gpu_calibration=" << research_gpu_calibration.prediction_confidence_norm << ','
            << research_gpu_calibration.best_interference_norm << ','
            << research_gpu_calibration.best_lattice_interference_norm << ','
            << research_gpu_calibration.best_temporal_coupling_norm << '\n'
            << "live_plan=" << research_live_compute_plan.readiness_norm << ','
            << research_live_compute_plan.interference_ledger_norm << ','
            << research_live_compute_plan.encoded_extrapolation_norm << '\n'
            << "process=" << process.valid_u32 << ',' << process.tick_u64 << ','
            << process.residual_norm_q15 << ',' << process.controller_authority_q15 << ','
            << process.interference_norm_q15 << ',' << process.coherence_norm_q15 << ','
            << process.source_vibration_q15 << ',' << process.calibration_authority_q15 << '\n'
            << "subsystem=" << subsystem.valid_u32 << ',' << subsystem.tick_u64 << ','
            << subsystem.aggregate_residual_norm_q15 << ',' << subsystem.aggregate_spin_norm_q15 << ','
            << subsystem.aggregate_coupling_norm_q15 << ',' << subsystem.aggregate_controller_authority_q15 << ','
            << subsystem.dominant_subsystem_id_u32 << '\n'
            << "ai=" << ai.valid_u32 << ',' << ai.tick_u64 << ',' << ai.sig9_u64 << ','
            << ai.command_count_u32 << ',' << ai.action_log_count_u32 << ','
            << ai.pending_external_api_u32 << ',' << ai.inflight_external_api_u32 << '\n'
            << "ai_data=" << ai_data.valid_u32 << ',' << ai_data.tick_u64 << ','
            << ai_data.corpus_artifact_count_u32 << ',' << ai_data.pending_external_api_u32 << ','
            << ai_data.inflight_external_api_u32 << ',' << ai_data.residual_norm_q15;
        for (uint64_t object_id_u64 : object_ids) {
            oss << '\n' << "object_id=" << object_id_u64;
        }
        return oss.str();
    }

    void PersistResearchVerificationArtifacts() {
        const uint64_t tick_u64 = sm.canonical_tick_u64();
        const bool should_emit =
            (research_verification_emit_last_tick_u64 == 0u) ||
            (tick_u64 == research_gpu_prediction_last_tick_u64) ||
            (tick_u64 >= research_verification_emit_last_tick_u64 + 360u) ||
            (research_run_log_last_live_compute_active != research_live_compute_active) ||
            research_guidance_watchdog_fault;
        if (!should_emit) return;

        EwProcessSubstrateTelemetry process{};
        EwSubsystemSubstrateTelemetry subsystem{};
        EwAiSubstrateTelemetry ai{};
        EwAiDataSubstrateTelemetry ai_data{};
        (void)sm.get_process_substrate_telemetry(&process);
        (void)sm.get_subsystem_substrate_telemetry(&subsystem);
        (void)sm.get_ai_substrate_telemetry(&ai);
        (void)sm.get_ai_data_substrate_telemetry(&ai_data);

        std::vector<uint64_t> object_ids;
        sm.object_store.list_object_ids_sorted(object_ids);
        const std::string core_state_utf8 =
            BuildResearchRuntimeStateCore(process, subsystem, ai, ai_data, object_ids);
        const EwId9 state_sig9 =
            EigenWare::coord_sig9(EigenWare::ew_namespace_sig9_ascii(),
                                  reinterpret_cast<const uint8_t*>(core_state_utf8.data()),
                                  core_state_utf8.size());

        const std::filesystem::path root = ResearchRootDir();
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        {
            std::ofstream f(root / "state.json", std::ios::out | std::ios::trunc);
            if (f.good()) {
                f.setf(std::ios::fixed);
                f.precision(6);
                f << "{\n";
                f << "  \"tick\": " << tick_u64 << ",\n";
                f << "  \"anchor_count\": " << sm.anchors.size() << ",\n";
                f << "  \"object_count\": " << object_ids.size() << ",\n";
                f << "  \"state_sig9\": [";
                for (size_t i = 0u; i < state_sig9.u32.size(); ++i) {
                    if (i != 0u) f << ", ";
                    f << state_sig9.u32[i];
                }
                f << "],\n";
                f << "  \"state_byte_len\": " << core_state_utf8.size() << ",\n";
                f << "  \"runtime\": {\n";
                f << "    \"live_boot_enabled\": " << (research_live_boot_enabled ? 1 : 0) << ",\n";
                f << "    \"guidance_enabled\": " << (research_guidance_enabled ? 1 : 0) << ",\n";
                f << "    \"live_compute_enabled\": " << (research_live_compute_enabled ? 1 : 0) << ",\n";
                f << "    \"live_compute_active\": " << (research_live_compute_active ? 1 : 0) << ",\n";
                f << "    \"guidance_watchdog_fault\": " << (research_guidance_watchdog_fault ? 1 : 0) << ",\n";
                f << "    \"guidance_watchdog_fault_count\": " << research_guidance_watchdog_fault_count_u32 << ",\n";
                f << "    \"quartet\": {\"F\": " << research_param_f
                  << ", \"A\": " << research_param_a
                  << ", \"I\": " << research_param_i
                  << ", \"V\": " << research_param_v << "}\n";
                f << "  },\n";
                f << "  \"guidance\": {\n";
                f << "    \"certainty_norm\": " << research_guidance.certainty_norm << ",\n";
                f << "    \"exactness_norm\": " << research_guidance.exactness_norm << ",\n";
                f << "    \"temporal_coupling_norm\": " << research_guidance.temporal_coupling_norm << ",\n";
                f << "    \"correction_authority_norm\": " << research_guidance.correction_authority_norm << ",\n";
                f << "    \"corrected_quartet\": {\"F\": " << research_guidance.corrected_quartet.F
                  << ", \"A\": " << research_guidance.corrected_quartet.A
                  << ", \"I\": " << research_guidance.corrected_quartet.I
                  << ", \"V\": " << research_guidance.corrected_quartet.V << "}\n";
                f << "  },\n";
                f << "  \"gpu_calibration\": {\n";
                f << "    \"valid\": " << (research_gpu_calibration.valid ? 1 : 0) << ",\n";
                f << "    \"prediction_tick\": " << research_gpu_prediction_last_tick_u64 << ",\n";
                f << "    \"prediction_confidence_norm\": " << research_gpu_calibration.prediction_confidence_norm << ",\n";
                f << "    \"best_interference_norm\": " << research_gpu_calibration.best_interference_norm << ",\n";
                f << "    \"best_lattice_interference_norm\": " << research_gpu_calibration.best_lattice_interference_norm << ",\n";
                f << "    \"best_temporal_coupling_norm\": " << research_gpu_calibration.best_temporal_coupling_norm << ",\n";
                f << "    \"next_pulse_quartet\": {\"F\": " << research_gpu_calibration.next_pulse_quartet.F
                  << ", \"A\": " << research_gpu_calibration.next_pulse_quartet.A
                  << ", \"I\": " << research_gpu_calibration.next_pulse_quartet.I
                  << ", \"V\": " << research_gpu_calibration.next_pulse_quartet.V << "}\n";
                f << "  },\n";
                f << "  \"process_substrate\": {\n";
                f << "    \"valid\": " << process.valid_u32 << ",\n";
                f << "    \"tick\": " << process.tick_u64 << ",\n";
                f << "    \"residual_norm_q15\": " << process.residual_norm_q15 << ",\n";
                f << "    \"controller_authority_q15\": " << process.controller_authority_q15 << ",\n";
                f << "    \"interference_norm_q15\": " << process.interference_norm_q15 << ",\n";
                f << "    \"coherence_norm_q15\": " << process.coherence_norm_q15 << ",\n";
                f << "    \"source_vibration_q15\": " << process.source_vibration_q15 << ",\n";
                f << "    \"calibration_authority_q15\": " << process.calibration_authority_q15 << "\n";
                f << "  },\n";
                f << "  \"subsystem_substrate\": {\n";
                f << "    \"valid\": " << subsystem.valid_u32 << ",\n";
                f << "    \"tick\": " << subsystem.tick_u64 << ",\n";
                f << "    \"subsystem_count_u32\": " << subsystem.subsystem_count_u32 << ",\n";
                f << "    \"active_subsystem_count_u32\": " << subsystem.active_subsystem_count_u32 << ",\n";
                f << "    \"dominant_subsystem_id_u32\": " << subsystem.dominant_subsystem_id_u32 << ",\n";
                f << "    \"aggregate_residual_norm_q15\": " << subsystem.aggregate_residual_norm_q15 << ",\n";
                f << "    \"aggregate_spin_norm_q15\": " << subsystem.aggregate_spin_norm_q15 << ",\n";
                f << "    \"aggregate_coupling_norm_q15\": " << subsystem.aggregate_coupling_norm_q15 << ",\n";
                f << "    \"aggregate_controller_authority_q15\": " << subsystem.aggregate_controller_authority_q15 << "\n";
                f << "  },\n";
                f << "  \"ai_substrate\": {\n";
                f << "    \"valid\": " << ai.valid_u32 << ",\n";
                f << "    \"tick\": " << ai.tick_u64 << ",\n";
                f << "    \"sig9_u64\": " << ai.sig9_u64 << ",\n";
                f << "    \"command_count_u32\": " << ai.command_count_u32 << ",\n";
                f << "    \"action_log_count_u32\": " << ai.action_log_count_u32 << ",\n";
                f << "    \"pending_external_api_u32\": " << ai.pending_external_api_u32 << ",\n";
                f << "    \"inflight_external_api_u32\": " << ai.inflight_external_api_u32 << "\n";
                f << "  },\n";
                f << "  \"ai_data_substrate\": {\n";
                f << "    \"valid\": " << ai_data.valid_u32 << ",\n";
                f << "    \"tick\": " << ai_data.tick_u64 << ",\n";
                f << "    \"corpus_artifact_count_u32\": " << ai_data.corpus_artifact_count_u32 << ",\n";
                f << "    \"manifest_record_count_u32\": " << ai_data.manifest_record_count_u32 << ",\n";
                f << "    \"pending_external_api_u32\": " << ai_data.pending_external_api_u32 << ",\n";
                f << "    \"inflight_external_api_u32\": " << ai_data.inflight_external_api_u32 << ",\n";
                f << "    \"residual_norm_q15\": " << ai_data.residual_norm_q15 << "\n";
                f << "  },\n";
                f << "  \"artifacts\": {\n";
                f << "    \"run_log\": \"run.log\",\n";
                f << "    \"state_json\": \"state.json\",\n";
                f << "    \"lattice_projection_image\": \"lattice_projection.ppm\",\n";
                f << "    \"object_reference_image\": \"object_reference.ppm\"\n";
                f << "  }\n";
                f << "}\n";
            }
        }

        PersistResearchLatticeProjectionImage();
        PersistResearchObjectReferenceImage();

        if (!research_run_log_boot_emitted) {
            std::ostringstream oss;
            oss << "RUN_START tick=" << tick_u64
                << " live_boot=" << (research_live_boot_enabled ? 1 : 0)
                << " guidance=" << (research_guidance_enabled ? 1 : 0)
                << " live_compute_enabled=" << (research_live_compute_enabled ? 1 : 0)
                << " state_sig9=" << Sig9CompactUtf8(state_sig9);
            AppendResearchRunLogLine(oss.str());
            research_run_log_boot_emitted = true;
        }
        if (research_run_log_last_live_compute_active != research_live_compute_active) {
            std::ostringstream oss;
            oss << "LIVE_COMPUTE active=" << (research_live_compute_active ? 1 : 0)
                << " tick=" << tick_u64
                << " readiness=" << research_live_compute_plan.readiness_norm
                << " spectral_id=" << research_live_compute_plan.trajectory_spectral_id_u64;
            AppendResearchRunLogLine(oss.str());
            research_run_log_last_live_compute_active = research_live_compute_active;
        }
        if (research_run_log_last_prediction_tick_u64 != research_gpu_prediction_last_tick_u64 &&
            research_gpu_calibration.valid) {
            std::ostringstream oss;
            oss << "GPU_CALIBRATION tick=" << research_gpu_prediction_last_tick_u64
                << " confidence=" << research_gpu_calibration.prediction_confidence_norm
                << " best_interference=" << research_gpu_calibration.best_interference_norm
                << " best_lattice_interference=" << research_gpu_calibration.best_lattice_interference_norm
                << " best_temporal_coupling=" << research_gpu_calibration.best_temporal_coupling_norm
                << " next_pulse="
                << research_gpu_calibration.next_pulse_quartet.F << ','
                << research_gpu_calibration.next_pulse_quartet.A << ','
                << research_gpu_calibration.next_pulse_quartet.I << ','
                << research_gpu_calibration.next_pulse_quartet.V;
            AppendResearchRunLogLine(oss.str());
            research_run_log_last_prediction_tick_u64 = research_gpu_prediction_last_tick_u64;
        }
        if (research_last_state_sig9 != state_sig9 || research_verification_emit_last_tick_u64 != tick_u64) {
            std::ostringstream oss;
            oss << "STATE_SNAPSHOT tick=" << tick_u64
                << " sig9=" << Sig9CompactUtf8(state_sig9)
                << " residual_q15=" << process.residual_norm_q15
                << " certainty=" << research_guidance.certainty_norm
                << " readiness=" << research_live_compute_plan.readiness_norm
                << " objects=" << object_ids.size();
            AppendResearchRunLogLine(oss.str());
            research_last_state_sig9 = state_sig9;
        }

        research_verification_emit_last_tick_u64 = tick_u64;
    }

    void PersistResearchRuntimeState() {
        (void)research_runtime_vsd.put_u64("photon/runtime/lattice_edge",
                                           "runtime lattice edge",
                                           (uint64_t)research_lattice_edge_u32);
        (void)research_runtime_vsd.put_f64("photon/runtime/param_a",
                                           "a_code amplitude driver",
                                           (double)research_param_a);
        (void)research_runtime_vsd.put_f64("photon/runtime/param_f",
                                           "f_code frequency driver",
                                           (double)research_param_f);
        (void)research_runtime_vsd.put_f64("photon/runtime/param_i",
                                           "i_code amperage compression",
                                           (double)research_param_i);
        (void)research_runtime_vsd.put_f64("photon/runtime/param_v",
                                           "v_code voltage phase tilt",
                                           (double)research_param_v);
        (void)research_runtime_vsd.put_u64("photon/runtime/output_mode",
                                           "1=freq 2=vector",
                                           (uint64_t)research_output_mode_u8);
        (void)research_runtime_vsd.put_u64("photon/runtime/stov_mode",
                                           "runtime stov toggle",
                                           research_stov_mode ? 1u : 0u);
        (void)research_runtime_vsd.put_u64("photon/runtime/live_boot_mode",
                                           "1=auto boot from research archive",
                                           research_live_boot_enabled ? 1u : 0u);
        (void)research_runtime_vsd.put_u64("photon/runtime/guidance_enabled",
                                           "1=apply research guidance correction loop",
                                           research_guidance_enabled ? 1u : 0u);
        (void)research_runtime_vsd.put_u64("photon/runtime/live_compute_enabled",
                                           "1=allow substrate to go live after calibration readiness",
                                           research_live_compute_enabled ? 1u : 0u);
        (void)research_runtime_vsd.put_u64("photon/runtime/live_compute_active",
                                           "1=substrate is actively running live compute mode",
                                           research_live_compute_active ? 1u : 0u);
        (void)research_runtime_vsd.put_u64("photon/runtime/basic_trial_enabled",
                                           "1=basic trial mode is enabled for limited play access",
                                           research_basic_trial_enabled ? 1u : 0u);
        (void)research_runtime_vsd.put_u64("photon/runtime/basic_trial_expired",
                                           "1=basic trial play window has expired",
                                           research_basic_trial_expired ? 1u : 0u);
        (void)research_runtime_vsd.put_u64("photon/runtime/basic_trial_minutes",
                                           "configured basic trial duration in minutes",
                                           (uint64_t)research_basic_trial_minutes_u32);
        (void)research_runtime_vsd.put_u64("photon/runtime/basic_trial_start_tick",
                                           "tick when basic trial mode was enabled",
                                           research_basic_trial_start_tick_u64);
        (void)research_runtime_vsd.put_u64("photon/runtime/basic_trial_end_tick",
                                           "tick when basic trial mode should expire",
                                           research_basic_trial_end_tick_u64);
        (void)research_runtime_vsd.put_f64("photon/runtime/basic_trial_remaining_seconds",
                                           "remaining wall-clock seconds in the active basic trial",
                                           ResearchBasicTrialRemainingSeconds());
        (void)research_runtime_vsd.put_u64("photon/runtime/live_compute_activation_tick",
                                           "tick when live compute mode last activated",
                                           research_live_compute_activation_tick_u64);
        (void)research_runtime_vsd.put_f64("photon/runtime/live_compute_readiness_norm",
                                           "live compute readiness score",
                                           research_live_compute_plan.readiness_norm);
        (void)research_runtime_vsd.put_f64("photon/runtime/live_compute_interference_ledger_norm",
                                           "compact interference ledger strength",
                                           research_live_compute_plan.interference_ledger_norm);
        (void)research_runtime_vsd.put_f64("photon/runtime/live_compute_extrapolation_norm",
                                           "encoding-schema extrapolation strength",
                                           research_live_compute_plan.encoded_extrapolation_norm);
        (void)research_runtime_vsd.put_u64("photon/runtime/live_compute_spectral_id",
                                           "active live compute trajectory spectral id",
                                           research_live_compute_plan.trajectory_spectral_id_u64);
        (void)research_runtime_vsd.put_u64("photon/runtime/guidance_watchdog_fault",
                                           "1=guidance watchdog faulted on last evaluation",
                                           research_guidance_watchdog_fault ? 1u : 0u);
        (void)research_runtime_vsd.put_u64("photon/runtime/guidance_watchdog_fault_count",
                                           "count of guidance watchdog faults",
                                           (uint64_t)research_guidance_watchdog_fault_count_u32);
        (void)research_runtime_vsd.put_u64("photon/runtime/gpu_prediction_axis_resolution",
                                           "axis resolution for gpu adaptive prediction sweep",
                                           (uint64_t)research_gpu_prediction_axis_resolution_u32);
        (void)research_runtime_vsd.put_u64("photon/runtime/gpu_prediction_refresh_ticks",
                                           "tick cadence for gpu adaptive prediction refresh",
                                           (uint64_t)research_gpu_prediction_refresh_ticks_u32);
        (void)research_runtime_vsd.put_u64("photon/runtime/gpu_prediction_count",
                                           "cached gpu adaptive prediction count",
                                           (uint64_t)research_gpu_predictions.size());
        (void)research_runtime_vsd.put_u64("photon/runtime/gpu_prediction_last_tick",
                                           "last tick gpu adaptive predictions were rebuilt",
                                           research_gpu_prediction_last_tick_u64);
        (void)research_runtime_vsd.put_u64("photon/runtime/sx_q32_32",
                                           "x-axis scale derived from pulse frequency",
                                           research_sx_q32_32_u64);
        (void)research_runtime_vsd.put_u64("photon/runtime/sy_q32_32",
                                           "y-axis scale derived from pulse amplitude",
                                           research_sy_q32_32_u64);
        (void)research_runtime_vsd.put_u64("photon/runtime/sz_q32_32",
                                           "z-axis scale derived from joint frequency and amplitude",
                                           research_sz_q32_32_u64);
        (void)research_runtime_vsd.put_f64("photon/runtime/prediction_lattice_energy_norm",
                                           "center-lattice energy norm used by prediction probe",
                                           research_lattice_probe_energy_norm);
        (void)research_runtime_vsd.put_f64("photon/runtime/prediction_lattice_flux_norm",
                                           "center-lattice flux norm used by prediction probe",
                                           research_lattice_probe_flux_norm);
        (void)research_runtime_vsd.put_f64("photon/runtime/prediction_lattice_interference_norm",
                                           "lattice-backed interference norm fed into gpu prediction",
                                           research_lattice_probe_interference_norm);
        (void)research_runtime_vsd.put_f64("photon/runtime/prediction_lattice_temporal_coupling_norm",
                                           "lattice-backed temporal coupling norm fed into next pulse correction",
                                           research_lattice_probe_temporal_coupling_norm);
        (void)research_runtime_vsd.put_f64("photon/runtime/gpu_prediction_best_lattice_interference_norm",
                                           "best lattice interference norm from prediction surface",
                                           research_gpu_calibration.best_lattice_interference_norm);
        (void)research_runtime_vsd.put_f64("photon/runtime/gpu_prediction_best_temporal_coupling_norm",
                                           "best lattice temporal coupling norm from prediction surface",
                                           research_gpu_calibration.best_temporal_coupling_norm);
        (void)research_runtime_vsd.put_f64("photon/runtime/gpu_prediction_next_pulse_correction_norm",
                                           "temporal correction strength applied to the next gpu pulse",
                                           research_gpu_calibration.next_pulse_correction_norm);
        (void)research_runtime_vsd.put_f64("photon/runtime/gpu_prediction_next_pulse_f",
                                           "next gpu pulse frequency derived from predicted interference",
                                           research_gpu_calibration.next_pulse_quartet.F);
        (void)research_runtime_vsd.put_f64("photon/runtime/gpu_prediction_next_pulse_a",
                                           "next gpu pulse amplitude derived from predicted interference",
                                           research_gpu_calibration.next_pulse_quartet.A);
        (void)research_runtime_vsd.put_f64("photon/runtime/gpu_prediction_next_pulse_i",
                                           "next gpu pulse amperage derived from predicted interference",
                                           research_gpu_calibration.next_pulse_quartet.I);
        (void)research_runtime_vsd.put_f64("photon/runtime/gpu_prediction_next_pulse_v",
                                           "next gpu pulse voltage derived from predicted interference",
                                           research_gpu_calibration.next_pulse_quartet.V);
        (void)research_runtime_vsd.put_u64("photon/runtime/stream_hz",
                                           "visual stream hz",
                                           (uint64_t)research_visual_stream_hz_u32);

        std::vector<uint8_t> blob;
        if (!research_runtime_vsd.serialize_binary(blob)) return;
        std::filesystem::path root = research_archive.root_dir;
        if (root.empty()) root = std::filesystem::current_path() / "ResearchConfinement";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        const std::filesystem::path out = root / "photon_volume_expansion.gevsd";
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f.good()) return;
        f.write(reinterpret_cast<const char*>(blob.data()), (std::streamsize)blob.size());
        PersistResearchVerificationArtifacts();
    }

    void SetResearchOutputMode(uint8_t mode_u8, bool announce = true) {
        research_output_mode_u8 = (mode_u8 == 2u) ? 2u : 1u;
        PersistResearchRuntimeState();
        if (announce) {
            PushLocalUiLine(research_output_mode_u8 == 2u
                                ? "RESEARCH_FREQ:mode=vector"
                                : "RESEARCH_FREQ:mode=frequency");
        }
    }

    void SetResearchStovMode(bool enable, bool announce = true) {
        research_stov_mode = enable;
        PersistResearchRuntimeState();
        if (announce) {
            PushLocalUiLine(research_stov_mode ? "RESEARCH_FREQ:stov=on" : "RESEARCH_FREQ:stov=off");
        }
    }

    void SetResearchParams(float a, float f, float i, float v, bool announce = true) {
        research_param_a = std::max(0.0f, a);
        research_param_f = std::max(0.0f, f);
        research_param_i = std::max(0.0f, i);
        research_param_v = std::max(0.0f, v);
        genesis::GeResearchPulseQuartet quartet{};
        quartet.F = (double)research_param_f;
        quartet.A = (double)research_param_a;
        quartet.I = (double)research_param_i;
        quartet.V = (double)research_param_v;
        UpdateResearchAxisScaleState(quartet);
        PersistResearchRuntimeState();
        if (announce) {
            std::ostringstream oss;
            oss << "RESEARCH_FREQ:params A=" << research_param_a
                << " F=" << research_param_f
                << " I=" << research_param_i
                << " V=" << research_param_v;
            PushLocalUiLine(oss.str());
        }
    }

    void SetResearchLatticeEdge(uint32_t edge_u32, bool announce = true) {
        research_lattice_edge_u32 = ClampLatticeEdge(edge_u32);
        research_visual_max_points = (size_t)std::max<uint32_t>(256u, std::min<uint32_t>(20000u, research_lattice_edge_u32));
        PersistResearchRuntimeState();
        if (announce) {
            std::ostringstream oss;
            oss << "RESEARCH_FREQ:lattice_edge=" << research_lattice_edge_u32
                << " max_points=" << research_visual_max_points;
            PushLocalUiLine(oss.str());
        }
    }

    void ScaleResearchLatticeByStepDelta(int32_t step_delta_i32, bool announce = true) {
        const uint32_t current_step =
            (uint32_t)std::max<int32_t>(1, (int32_t)std::lround(std::sqrt((double)research_lattice_edge_u32 / 256.0)));
        const int32_t next_step_i32 = std::max<int32_t>(1, current_step + step_delta_i32);
        const uint32_t next_edge = LatticeEdgeFromScaleStep((uint32_t)next_step_i32);
        SetResearchLatticeEdge(next_edge, announce);
    }

    void SetResearchStreamHz(uint32_t hz_u32, bool announce = true) {
        research_visual_stream_hz_u32 = std::max<uint32_t>(60u, std::min<uint32_t>(hz_u32, 360u));
        PersistResearchRuntimeState();
        if (announce) {
            std::ostringstream oss;
            oss << "RESEARCH_FREQ:stream_hz=" << research_visual_stream_hz_u32;
            PushLocalUiLine(oss.str());
        }
    }

    bool ResearchQuartetWithinCalibratedWindow(const genesis::GeResearchPulseQuartet& q) const {
        return genesis::ge_has_research_runtime_guidance(research_archive) &&
               q.F >= research_archive.pulse_window_min_quartet.F &&
               q.F <= research_archive.pulse_window_max_quartet.F &&
               q.A >= research_archive.pulse_window_min_quartet.A &&
               q.A <= research_archive.pulse_window_max_quartet.A &&
               q.I >= research_archive.pulse_window_min_quartet.I &&
               q.I <= research_archive.pulse_window_max_quartet.I &&
               q.V >= research_archive.pulse_window_min_quartet.V &&
               q.V <= research_archive.pulse_window_max_quartet.V;
    }

    void RecordResearchGuidanceFault(uint64_t tick_u64, const std::string& reason_utf8, bool announce) {
        const bool was_faulted = research_guidance_watchdog_fault;
        research_guidance_watchdog_fault = true;
        ++research_guidance_watchdog_fault_count_u32;
        research_guidance_watchdog_last_fault_tick_u64 = tick_u64;
        PersistResearchRuntimeState();
        if (announce && !was_faulted) {
            std::ostringstream oss;
            oss << "RESEARCH_FREQ:watchdog fault=" << research_guidance_watchdog_fault_count_u32
                << " tick=" << tick_u64
                << " reason=" << reason_utf8;
            PushLocalUiLine(oss.str());
        }
    }

    void ClearResearchGuidanceFault() {
        if (!research_guidance_watchdog_fault) return;
        research_guidance_watchdog_fault = false;
        PersistResearchRuntimeState();
    }

    void EmitResearchPulseQuartet(const genesis::GeResearchPulseQuartet& quartet,
                                  const EwProcessSubstrateTelemetry* telemetry_opt) {
        sm.submit_gpu_pulse_sample_v2(PackFreqHz(quartet.F), 4096u,
                                      (uint32_t)PackNormU16(quartet.A), 65535u,
                                      (uint32_t)PackNormU16(quartet.V), 65535u);

        const uint64_t tick_u64 = sm.canonical_tick_u64();
        const uint32_t cadence_u32 = std::max<uint32_t>(1u, research_guidance.correction_cadence_ticks_u32);
        const bool emit_pulse =
            !research_guidance_has_pulse_history ||
            (tick_u64 >= research_guidance_last_pulse_tick_u64 + (uint64_t)cadence_u32);
        if (!emit_pulse) return;

        Pulse pulse{};
        pulse.anchor_id =
            (telemetry_opt && telemetry_opt->spectral_anchor_id_u32 != 0u)
                ? telemetry_opt->spectral_anchor_id_u32
                : sm.spectral_field_anchor_id_u32;
        if (pulse.anchor_id == 0u) return;

        pulse.f_code = PackFreqCode(quartet.F);
        pulse.a_code = PackNormU16(quartet.A);
        pulse.i_code = PackNormU16(quartet.I);
        pulse.v_code = PackNormU16(quartet.V);
        pulse.profile_id = EW_PROFILE_CORE_EVOLUTION;
        pulse.causal_tag = 91u;
        pulse.tick = tick_u64;
        sm.submit_pulse(pulse);
        research_guidance_last_pulse_tick_u64 = tick_u64;
        research_guidance_has_pulse_history = true;
    }

    void ApplyResearchLiveBootPreset(bool announce) {
        if (!research_live_boot_enabled) return;
        if (!genesis::ge_has_research_runtime_guidance(research_archive)) return;

        const genesis::GeResearchPulseQuartet& center = research_archive.run043_center_quartet;
        research_guidance_enabled = true;
        research_live_compute_enabled = true;
        research_live_compute_active = false;
        research_visual_stream_enabled = true;
        research_use_compute_audio = true;
        research_output_mode_u8 = 1u;
        research_stov_mode = false;
        research_param_f = (float)center.F;
        research_param_a = (float)center.A;
        research_param_i = (float)center.I;
        research_param_v = (float)center.V;
        research_visual_stream_hz_u32 = 360u;
        research_lattice_edge_u32 = ClampLatticeEdge(std::max<uint32_t>(research_lattice_edge_u32, 1024u));
        research_visual_max_points =
            (size_t)std::max<uint32_t>(256u, std::min<uint32_t>(20000u, research_lattice_edge_u32));
        research_guidance = genesis::GeResearchRuntimeGuidance{};
        research_gpu_calibration = genesis::GeResearchGpuAdaptiveCalibration{};
        research_live_compute_plan = genesis::GeResearchLiveComputePlan{};
        research_gpu_predictions.clear();
        research_guidance_last_pulse_tick_u64 = 0u;
        research_gpu_prediction_last_tick_u64 = 0u;
        research_live_compute_activation_tick_u64 = 0u;
        research_guidance_has_pulse_history = false;
        research_guidance_watchdog_fault = false;
        research_guidance_watchdog_fault_count_u32 = 0u;
        research_guidance_watchdog_last_fault_tick_u64 = 0u;
        UpdateResearchAxisScaleState(center);
        PersistResearchRuntimeState();

        if (announce) {
            std::ostringstream oss;
            oss << "RESEARCH_FREQ:live_boot=on preset=run43_center"
                << " lattice_edge=" << research_lattice_edge_u32
                << " stream_hz=" << research_visual_stream_hz_u32
                << " A=" << research_param_a
                << " F=" << research_param_f
                << " I=" << research_param_i
                << " V=" << research_param_v;
            PushLocalUiLine(oss.str());
        }
    }

    void ApplyResearchBasicTrialPreset(uint32_t minutes_u32, bool announce) {
        if (!genesis::ge_has_research_runtime_guidance(research_archive)) {
            if (announce) {
                PushLocalUiLine("META_BASIC_TRIAL:unavailable reason=guidance_missing");
            }
            return;
        }
        research_basic_trial_enabled = true;
        research_basic_trial_expired = false;
        research_basic_trial_expiry_announced = false;
        research_basic_trial_minutes_u32 = ClampResearchBasicTrialMinutes(minutes_u32);
        research_basic_trial_start_time = std::chrono::steady_clock::now();
        research_basic_trial_start_tick_u64 = sm.canonical_tick_u64();
        research_basic_trial_end_tick_u64 =
            research_basic_trial_start_tick_u64 + (uint64_t)research_basic_trial_minutes_u32 * 60u * 360u;

        research_live_boot_enabled = false;
        research_guidance_enabled = true;
        research_live_compute_enabled = false;
        research_live_compute_active = false;
        research_visual_stream_enabled = true;
        research_use_compute_audio = true;
        research_output_mode_u8 = 1u;
        research_stov_mode = false;
        research_visual_stream_hz_u32 =
            std::min<uint32_t>(research_visual_stream_hz_u32, ResearchBasicTrialMaxStreamHz());
        if (research_visual_stream_hz_u32 < 60u) research_visual_stream_hz_u32 = 60u;
        research_lattice_edge_u32 =
            ClampLatticeEdge(std::min<uint32_t>(std::max<uint32_t>(research_lattice_edge_u32, 512u),
                                                ResearchBasicTrialMaxLatticeEdge()));
        research_visual_max_points =
            (size_t)std::max<uint32_t>(256u, std::min<uint32_t>(20000u, research_lattice_edge_u32));
        research_gpu_prediction_axis_resolution_u32 =
            std::min<uint32_t>(research_gpu_prediction_axis_resolution_u32, 4u);
        research_gpu_prediction_refresh_ticks_u32 =
            std::max<uint32_t>(180u, research_gpu_prediction_refresh_ticks_u32);

        genesis::GeResearchPulseQuartet center = research_archive.run043_center_quartet;
        if (genesis::ge_has_research_runtime_guidance(research_archive)) {
            center.F = std::max(research_archive.pulse_window_min_quartet.F,
                                std::min(research_archive.pulse_window_max_quartet.F, center.F));
            center.A = std::max(research_archive.pulse_window_min_quartet.A,
                                std::min(research_archive.pulse_window_max_quartet.A, center.A));
            center.I = std::max(research_archive.pulse_window_min_quartet.I,
                                std::min(research_archive.pulse_window_max_quartet.I, center.I));
            center.V = std::max(research_archive.pulse_window_min_quartet.V,
                                std::min(research_archive.pulse_window_max_quartet.V, center.V));
        }
        research_param_f = (float)center.F;
        research_param_a = (float)center.A;
        research_param_i = (float)center.I;
        research_param_v = (float)center.V;
        research_guidance = genesis::GeResearchRuntimeGuidance{};
        research_gpu_calibration = genesis::GeResearchGpuAdaptiveCalibration{};
        research_live_compute_plan = genesis::GeResearchLiveComputePlan{};
        research_gpu_predictions.clear();
        research_guidance_last_pulse_tick_u64 = 0u;
        research_gpu_prediction_last_tick_u64 = 0u;
        research_guidance_has_pulse_history = false;
        research_guidance_watchdog_fault = false;
        research_guidance_watchdog_fault_count_u32 = 0u;
        research_guidance_watchdog_last_fault_tick_u64 = 0u;
        UpdateResearchAxisScaleState(center);
        PersistResearchRuntimeState();

        if (announce) {
            std::ostringstream oss;
            oss << "META_BASIC_TRIAL:on minutes=" << research_basic_trial_minutes_u32
                << " stream_hz=" << research_visual_stream_hz_u32
                << " lattice_edge=" << research_lattice_edge_u32
                << " live_compute=off"
                << " guidance=on"
                << " A=" << research_param_a
                << " F=" << research_param_f
                << " I=" << research_param_i
                << " V=" << research_param_v;
            PushLocalUiLine(oss.str());
        }
    }

    void DisableResearchBasicTrial(bool announce) {
        research_basic_trial_enabled = false;
        research_basic_trial_expired = false;
        research_basic_trial_expiry_announced = false;
        research_basic_trial_start_tick_u64 = 0u;
        research_basic_trial_end_tick_u64 = 0u;
        PersistResearchRuntimeState();
        if (announce) {
            PushLocalUiLine("META_BASIC_TRIAL:off");
        }
    }

    void UpdateResearchBasicTrialState() {
        if (!research_basic_trial_enabled || research_basic_trial_expired) return;
        if (ResearchBasicTrialRemainingSeconds() > 0.0) return;
        research_basic_trial_expired = true;
        research_live_compute_enabled = false;
        research_live_compute_active = false;
        PersistResearchRuntimeState();
        if (!research_basic_trial_expiry_announced) {
            research_basic_trial_expiry_announced = true;
            PushLocalUiLine("META_BASIC_TRIAL:expired mode=status_only");
        }
    }

    std::string BuildResearchBasicTrialStatusLine() const {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(2);
        oss << "META_BASIC_TRIAL enabled=" << (research_basic_trial_enabled ? 1 : 0)
            << " expired=" << (research_basic_trial_expired ? 1 : 0)
            << " minutes=" << research_basic_trial_minutes_u32
            << " start_tick=" << research_basic_trial_start_tick_u64
            << " end_tick=" << research_basic_trial_end_tick_u64
            << " remaining_s=" << ResearchBasicTrialRemainingSeconds()
            << " max_stream_hz=" << ResearchBasicTrialMaxStreamHz()
            << " max_lattice_edge=" << ResearchBasicTrialMaxLatticeEdge()
            << " live_compute=" << (research_live_compute_enabled ? 1 : 0);
        return oss.str();
    }

    std::string BuildResearchGuidanceStatusLine() const {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(6);
        oss << "RESEARCH_GUIDANCE valid=" << (research_guidance.valid ? 1 : 0)
            << " live_boot=" << (research_live_boot_enabled ? 1 : 0)
            << " enabled=" << (research_guidance_enabled ? 1 : 0)
            << " watchdog=" << (research_guidance_watchdog_fault ? 1 : 0)
            << " watchdog_count=" << research_guidance_watchdog_fault_count_u32
            << " watchdog_tick=" << research_guidance_watchdog_last_fault_tick_u64
            << " cadence=" << research_guidance.correction_cadence_ticks_u32
            << " last_pulse_tick=" << research_guidance_last_pulse_tick_u64
            << " certainty=" << research_guidance.certainty_norm
            << " exactness=" << research_guidance.exactness_norm
            << " recurrence=" << research_guidance.recurrence_norm
            << " tensor=" << research_guidance.tensor_gradient_norm
            << " packet=" << research_guidance.packet_coherence_norm
            << " observer=" << research_guidance.observer_coupling_norm
            << " temporal=" << research_guidance.temporal_coupling_norm
            << " memory=" << research_guidance.source_memory_norm
            << " ctrl=" << research_guidance.correction_authority_norm
            << " score=" << research_guidance.predicted_silicon_score
            << " trap=" << research_guidance.predicted_trap_ratio
            << " coh=" << research_guidance.predicted_coherence
            << " inertia=" << research_guidance.predicted_inertia
            << " curv=" << research_guidance.predicted_curvature
            << " desiredF=" << research_guidance.desired_quartet.F
            << " desiredA=" << research_guidance.desired_quartet.A
            << " desiredI=" << research_guidance.desired_quartet.I
            << " desiredV=" << research_guidance.desired_quartet.V
            << " corrF=" << research_guidance.corrected_quartet.F
            << " corrA=" << research_guidance.corrected_quartet.A
            << " corrI=" << research_guidance.corrected_quartet.I
            << " corrV=" << research_guidance.corrected_quartet.V
            << " gradF=" << research_guidance.gradient_quartet.F
            << " gradA=" << research_guidance.gradient_quartet.A
            << " gradI=" << research_guidance.gradient_quartet.I
            << " gradV=" << research_guidance.gradient_quartet.V;
        return oss.str();
    }

    std::string BuildResearchGpuCalibrationStatusLine() const {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(6);
        oss << "RESEARCH_GPU_CALIBRATION valid=" << (research_gpu_calibration.valid ? 1 : 0)
            << " axis=" << research_gpu_prediction_axis_resolution_u32
            << " refresh_ticks=" << research_gpu_prediction_refresh_ticks_u32
            << " pred_count=" << research_gpu_predictions.size()
            << " pred_tick=" << research_gpu_prediction_last_tick_u64
            << " gpuF=" << research_gpu_calibration.observed_gpu_freq_norm
            << " gpuA=" << research_gpu_calibration.observed_gpu_amp_norm
            << " gpuI=" << research_gpu_calibration.observed_gpu_curr_norm
            << " gpuV=" << research_gpu_calibration.observed_gpu_volt_norm
            << " intf=" << research_gpu_calibration.observed_interference_norm
            << " coh=" << research_gpu_calibration.observed_coherence_norm
            << " vib=" << research_gpu_calibration.observed_source_vibration_norm
            << " subRes=" << research_gpu_calibration.observed_subsystem_residual_norm
            << " subSpin=" << research_gpu_calibration.observed_subsystem_spin_norm
            << " subCpl=" << research_gpu_calibration.observed_subsystem_coupling_norm
            << " subCtrl=" << research_gpu_calibration.observed_subsystem_controller_norm
            << " sx=" << UnpackPositiveQ32_32(research_sx_q32_32_u64)
            << " sy=" << UnpackPositiveQ32_32(research_sy_q32_32_u64)
            << " sz=" << UnpackPositiveQ32_32(research_sz_q32_32_u64)
            << " probeE=" << research_lattice_probe_energy_norm
            << " probeFlux=" << research_lattice_probe_flux_norm
            << " probeIntf=" << research_lattice_probe_interference_norm
            << " probeTemp=" << research_lattice_probe_temporal_coupling_norm
            << " conf=" << research_gpu_calibration.prediction_confidence_norm
            << " align=" << research_gpu_calibration.best_gpu_alignment_norm
            << " vec=" << research_gpu_calibration.best_vector_coupling_norm
            << " subFB=" << research_gpu_calibration.best_subsystem_feedback_norm
            << " bestIntf=" << research_gpu_calibration.best_interference_norm
            << " bestLatIntf=" << research_gpu_calibration.best_lattice_interference_norm
            << " bestTemp=" << research_gpu_calibration.best_temporal_coupling_norm
            << " bestScore=" << research_gpu_calibration.best_silicon_score
            << " bestTrap=" << research_gpu_calibration.best_trap_ratio
            << " bestCoh=" << research_gpu_calibration.best_coherence
            << " bestInertia=" << research_gpu_calibration.best_inertia
            << " bestCurv=" << research_gpu_calibration.best_curvature
            << " bestID=" << research_gpu_calibration.best_trajectory_spectral_id_u64
            << " latX=" << research_gpu_calibration.best_lattice_x_u32
            << " latY=" << research_gpu_calibration.best_lattice_y_u32
            << " latZ=" << research_gpu_calibration.best_lattice_z_u32
            << " bestF=" << research_gpu_calibration.best_quartet.F
            << " bestA=" << research_gpu_calibration.best_quartet.A
            << " bestI=" << research_gpu_calibration.best_quartet.I
            << " bestV=" << research_gpu_calibration.best_quartet.V
            << " adaptF=" << research_gpu_calibration.adapted_quartet.F
            << " adaptA=" << research_gpu_calibration.adapted_quartet.A
            << " adaptI=" << research_gpu_calibration.adapted_quartet.I
            << " adaptV=" << research_gpu_calibration.adapted_quartet.V
            << " nextCorr=" << research_gpu_calibration.next_pulse_correction_norm
            << " nextF=" << research_gpu_calibration.next_pulse_quartet.F
            << " nextA=" << research_gpu_calibration.next_pulse_quartet.A
            << " nextI=" << research_gpu_calibration.next_pulse_quartet.I
            << " nextV=" << research_gpu_calibration.next_pulse_quartet.V;
        return oss.str();
    }

    std::string BuildResearchLiveComputeStatusLine() const {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(6);
        oss << "RESEARCH_LIVE_COMPUTE valid=" << (research_live_compute_plan.valid ? 1 : 0)
            << " enabled=" << (research_live_compute_enabled ? 1 : 0)
            << " active=" << (research_live_compute_active ? 1 : 0)
            << " ready=" << (research_live_compute_plan.ready ? 1 : 0)
            << " activation_tick=" << research_live_compute_activation_tick_u64
            << " readiness=" << research_live_compute_plan.readiness_norm
            << " ledger=" << research_live_compute_plan.interference_ledger_norm
            << " extrap=" << research_live_compute_plan.encoded_extrapolation_norm
            << " score_align=" << research_live_compute_plan.score_alignment_norm
            << " trap_align=" << research_live_compute_plan.trap_alignment_norm
            << " coh_align=" << research_live_compute_plan.coherence_alignment_norm
            << " spectral_id=" << research_live_compute_plan.trajectory_spectral_id_u64
            << " computeF=" << research_live_compute_plan.compute_quartet.F
            << " computeA=" << research_live_compute_plan.compute_quartet.A
            << " computeI=" << research_live_compute_plan.compute_quartet.I
            << " computeV=" << research_live_compute_plan.compute_quartet.V;
        return oss.str();
    }

    void PersistResearchGpuPredictionSurface() {
        if (!research_gpu_calibration.valid) return;
        std::filesystem::path root = research_archive.root_dir;
        if (root.empty()) root = std::filesystem::current_path() / "ResearchConfinement";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        const std::filesystem::path out = root / "gpu_kernel_interference_prediction_surface.json";
        std::ofstream f(out, std::ios::out | std::ios::trunc);
        if (!f.good()) return;

        f << "{\n";
        f << "  \"model\": \"gpu_adaptive_lattice_interference_prediction_surface\",\n";
        f << "  \"axis_resolution\": " << research_gpu_prediction_axis_resolution_u32 << ",\n";
        f << "  \"prediction_count\": " << research_gpu_predictions.size() << ",\n";
        f << "  \"prediction_tick\": " << research_gpu_prediction_last_tick_u64 << ",\n";
        f << "  \"observed_gpu\": {\n";
        f << "    \"F\": " << research_gpu_calibration.observed_gpu_freq_norm << ",\n";
        f << "    \"A\": " << research_gpu_calibration.observed_gpu_amp_norm << ",\n";
        f << "    \"I\": " << research_gpu_calibration.observed_gpu_curr_norm << ",\n";
        f << "    \"V\": " << research_gpu_calibration.observed_gpu_volt_norm << "\n";
        f << "  },\n";
        f << "  \"observed_field\": {\n";
        f << "    \"interference\": " << research_gpu_calibration.observed_interference_norm << ",\n";
        f << "    \"coherence\": " << research_gpu_calibration.observed_coherence_norm << ",\n";
        f << "    \"source_vibration\": " << research_gpu_calibration.observed_source_vibration_norm << "\n";
        f << "  },\n";
        f << "  \"observed_subsystems\": {\n";
        f << "    \"residual\": " << research_gpu_calibration.observed_subsystem_residual_norm << ",\n";
        f << "    \"spin\": " << research_gpu_calibration.observed_subsystem_spin_norm << ",\n";
        f << "    \"coupling\": " << research_gpu_calibration.observed_subsystem_coupling_norm << ",\n";
        f << "    \"controller\": " << research_gpu_calibration.observed_subsystem_controller_norm << "\n";
        f << "  },\n";
        f << "  \"axis_scales_q32_32\": {\n";
        f << "    \"sx\": " << research_sx_q32_32_u64 << ",\n";
        f << "    \"sy\": " << research_sy_q32_32_u64 << ",\n";
        f << "    \"sz\": " << research_sz_q32_32_u64 << "\n";
        f << "  },\n";
        f << "  \"prediction_lattice_probe\": {\n";
        f << "    \"energy_norm\": " << research_lattice_probe_energy_norm << ",\n";
        f << "    \"flux_norm\": " << research_lattice_probe_flux_norm << ",\n";
        f << "    \"interference_norm\": " << research_lattice_probe_interference_norm << ",\n";
        f << "    \"temporal_coupling_norm\": " << research_lattice_probe_temporal_coupling_norm << "\n";
        f << "  },\n";
        f << "  \"best_prediction\": {\n";
        f << "    \"trajectory_spectral_id\": " << research_gpu_calibration.best_trajectory_spectral_id_u64 << ",\n";
        f << "    \"prediction_confidence\": " << research_gpu_calibration.prediction_confidence_norm << ",\n";
        f << "    \"gpu_alignment\": " << research_gpu_calibration.best_gpu_alignment_norm << ",\n";
        f << "    \"vector_coupling\": " << research_gpu_calibration.best_vector_coupling_norm << ",\n";
        f << "    \"subsystem_feedback\": " << research_gpu_calibration.best_subsystem_feedback_norm << ",\n";
        f << "    \"predicted_interference\": " << research_gpu_calibration.best_interference_norm << ",\n";
        f << "    \"lattice_interference\": " << research_gpu_calibration.best_lattice_interference_norm << ",\n";
        f << "    \"temporal_coupling\": " << research_gpu_calibration.best_temporal_coupling_norm << ",\n";
        f << "    \"predicted_score\": " << research_gpu_calibration.best_silicon_score << ",\n";
        f << "    \"predicted_trap_ratio\": " << research_gpu_calibration.best_trap_ratio << ",\n";
        f << "    \"predicted_coherence\": " << research_gpu_calibration.best_coherence << ",\n";
        f << "    \"lattice_coord\": {\"x\": " << research_gpu_calibration.best_lattice_x_u32
          << ", \"y\": " << research_gpu_calibration.best_lattice_y_u32
          << ", \"z\": " << research_gpu_calibration.best_lattice_z_u32 << "},\n";
        f << "    \"quartet\": {\n";
        f << "      \"F\": " << research_gpu_calibration.best_quartet.F << ",\n";
        f << "      \"A\": " << research_gpu_calibration.best_quartet.A << ",\n";
        f << "      \"I\": " << research_gpu_calibration.best_quartet.I << ",\n";
        f << "      \"V\": " << research_gpu_calibration.best_quartet.V << "\n";
        f << "    },\n";
        f << "    \"adapted_quartet\": {\n";
        f << "      \"F\": " << research_gpu_calibration.adapted_quartet.F << ",\n";
        f << "      \"A\": " << research_gpu_calibration.adapted_quartet.A << ",\n";
        f << "      \"I\": " << research_gpu_calibration.adapted_quartet.I << ",\n";
        f << "      \"V\": " << research_gpu_calibration.adapted_quartet.V << "\n";
        f << "    },\n";
        f << "    \"next_pulse_quartet\": {\n";
        f << "      \"F\": " << research_gpu_calibration.next_pulse_quartet.F << ",\n";
        f << "      \"A\": " << research_gpu_calibration.next_pulse_quartet.A << ",\n";
        f << "      \"I\": " << research_gpu_calibration.next_pulse_quartet.I << ",\n";
        f << "      \"V\": " << research_gpu_calibration.next_pulse_quartet.V << "\n";
        f << "    }\n";
        f << "  },\n";
        f << "  \"predictions\": [\n";
        for (size_t i = 0u; i < research_gpu_predictions.size(); ++i) {
            const auto& p = research_gpu_predictions[i];
            f << "    {\n";
            f << "      \"trajectory_spectral_id\": " << p.trajectory_spectral_id_u64 << ",\n";
            f << "      \"quartet\": {\"F\": " << p.quartet.F
              << ", \"A\": " << p.quartet.A
              << ", \"I\": " << p.quartet.I
              << ", \"V\": " << p.quartet.V << "},\n";
            f << "      \"predicted_interference\": " << p.predicted_interference_norm << ",\n";
            f << "      \"predicted_score\": " << p.predicted_score << ",\n";
            f << "      \"predicted_trap_ratio\": " << p.predicted_trap_ratio << ",\n";
            f << "      \"predicted_coherence\": " << p.predicted_coherence << ",\n";
            f << "      \"predicted_inertia\": " << p.predicted_inertia << ",\n";
            f << "      \"predicted_curvature\": " << p.predicted_curvature << ",\n";
            f << "      \"gpu_alignment\": " << p.gpu_alignment_norm << ",\n";
            f << "      \"vector_coupling\": " << p.vector_coupling_norm << ",\n";
            f << "      \"lattice_interference\": " << p.lattice_interference_norm << ",\n";
            f << "      \"lattice_temporal_coupling\": " << p.lattice_temporal_coupling_norm << ",\n";
            f << "      \"lattice_coord\": {\"x\": " << p.lattice_x_u32
              << ", \"y\": " << p.lattice_y_u32
              << ", \"z\": " << p.lattice_z_u32 << "},\n";
            f << "      \"certainty\": " << p.certainty_norm << "\n";
            f << "    }" << ((i + 1u < research_gpu_predictions.size()) ? "," : "") << "\n";
        }
        f << "  ]\n";
        f << "}\n";
    }

    void PersistResearchLiveComputeInterferenceLedger() {
        if (!research_live_compute_plan.valid) return;
        std::filesystem::path root = research_archive.root_dir;
        if (root.empty()) root = std::filesystem::current_path() / "ResearchConfinement";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        const std::filesystem::path out = root / "live_compute_interference_ledger.json";
        std::ofstream f(out, std::ios::out | std::ios::trunc);
        if (!f.good()) return;

        f << "{\n";
        f << "  \"model\": \"live_compute_interference_ledger\",\n";
        f << "  \"live_compute_enabled\": " << (research_live_compute_enabled ? 1 : 0) << ",\n";
        f << "  \"live_compute_active\": " << (research_live_compute_active ? 1 : 0) << ",\n";
        f << "  \"activation_tick\": " << research_live_compute_activation_tick_u64 << ",\n";
        f << "  \"trajectory_spectral_id\": " << research_live_compute_plan.trajectory_spectral_id_u64 << ",\n";
        f << "  \"readiness_norm\": " << research_live_compute_plan.readiness_norm << ",\n";
        f << "  \"interference_ledger_norm\": " << research_live_compute_plan.interference_ledger_norm << ",\n";
        f << "  \"encoded_extrapolation_norm\": " << research_live_compute_plan.encoded_extrapolation_norm << ",\n";
        f << "  \"score_alignment_norm\": " << research_live_compute_plan.score_alignment_norm << ",\n";
        f << "  \"trap_alignment_norm\": " << research_live_compute_plan.trap_alignment_norm << ",\n";
        f << "  \"coherence_alignment_norm\": " << research_live_compute_plan.coherence_alignment_norm << ",\n";
        f << "  \"compute_quartet\": {\n";
        f << "    \"F\": " << research_live_compute_plan.compute_quartet.F << ",\n";
        f << "    \"A\": " << research_live_compute_plan.compute_quartet.A << ",\n";
        f << "    \"I\": " << research_live_compute_plan.compute_quartet.I << ",\n";
        f << "    \"V\": " << research_live_compute_plan.compute_quartet.V << "\n";
        f << "  }\n";
        f << "}\n";
    }

    void UpdateResearchLiveComputeState() {
        const bool active_before = research_live_compute_active;
        research_live_compute_plan = genesis::GeResearchLiveComputePlan{};
        if (!research_live_compute_enabled) {
            research_live_compute_active = false;
            if (active_before != research_live_compute_active) PersistResearchRuntimeState();
            return;
        }
        if (!genesis::ge_build_research_live_compute_plan(research_archive,
                                                          research_gpu_calibration,
                                                          research_guidance,
                                                          research_live_compute_plan)) {
            research_live_compute_active = false;
            if (active_before != research_live_compute_active) PersistResearchRuntimeState();
            return;
        }

        const double activate_threshold = 0.72;
        const double release_threshold = 0.58;
        const bool can_activate =
            research_live_compute_plan.ready && !research_guidance_watchdog_fault;
        if (!research_live_compute_active) {
            if (can_activate || research_live_compute_plan.readiness_norm >= activate_threshold) {
                research_live_compute_active = true;
                research_live_compute_activation_tick_u64 = sm.canonical_tick_u64();
                PushLocalUiLine("RESEARCH_FREQ:live_compute=on");
            }
        } else if (research_guidance_watchdog_fault ||
                   research_live_compute_plan.readiness_norm < release_threshold) {
            research_live_compute_active = false;
            PushLocalUiLine("RESEARCH_FREQ:live_compute=off");
        }

        const bool should_persist =
            (active_before != research_live_compute_active) ||
            (sm.canonical_tick_u64() == research_gpu_prediction_last_tick_u64);
        if (should_persist) {
            PersistResearchLiveComputeInterferenceLedger();
            PersistResearchRuntimeState();
        }
    }

    void UpdateResearchGpuAdaptiveCalibration(const EwProcessSubstrateTelemetry* telemetry_opt,
                                              const EwSubsystemSubstrateTelemetry* subsystem_telemetry_opt) {
        if (!genesis::ge_has_research_runtime_guidance(research_archive)) {
            research_gpu_calibration = genesis::GeResearchGpuAdaptiveCalibration{};
            research_gpu_predictions.clear();
            return;
        }

        const uint64_t tick_u64 = sm.canonical_tick_u64();
        if (research_gpu_calibration.valid &&
            research_gpu_prediction_last_tick_u64 != 0u &&
            tick_u64 < research_gpu_prediction_last_tick_u64 + (uint64_t)std::max<uint32_t>(1u, research_gpu_prediction_refresh_ticks_u32)) {
            return;
        }

        const double gpu_freq_norm =
            telemetry_opt ? ((double)telemetry_opt->gpu_freq_norm_q15 / 65535.0) : (double)research_param_f;
        const double gpu_amp_norm =
            telemetry_opt ? ((double)telemetry_opt->gpu_amp_norm_q15 / 65535.0) : (double)research_param_a;
        const double gpu_curr_norm =
            telemetry_opt ? ((double)telemetry_opt->last_i_code_u16 / 65535.0) : (double)research_param_i;
        const double gpu_volt_norm =
            telemetry_opt ? ((double)telemetry_opt->gpu_volt_norm_q15 / 65535.0) : (double)research_param_v;
        const double interference_norm =
            telemetry_opt ? ((double)telemetry_opt->interference_norm_q15 / 65535.0) : 0.0;
        const double coherence_norm =
            telemetry_opt ? ((double)telemetry_opt->coherence_norm_q15 / 65535.0) : 0.0;
        const double source_vibration_norm =
            telemetry_opt ? ((double)telemetry_opt->source_vibration_q15 / 65535.0) : 0.0;
        const double subsystem_residual_norm =
            subsystem_telemetry_opt
                ? ((double)subsystem_telemetry_opt->aggregate_residual_norm_q15 / 65535.0)
                : 0.0;
        const double subsystem_spin_norm =
            subsystem_telemetry_opt
                ? ((double)subsystem_telemetry_opt->aggregate_spin_norm_q15 / 65535.0)
                : 0.0;
        const double subsystem_coupling_norm =
            subsystem_telemetry_opt
                ? ((double)subsystem_telemetry_opt->aggregate_coupling_norm_q15 / 65535.0)
                : 0.0;
        const double subsystem_controller_norm =
            subsystem_telemetry_opt
                ? ((double)subsystem_telemetry_opt->aggregate_controller_authority_q15 / 65535.0)
                : 0.0;

        genesis::GeResearchPulseQuartet desired{};
        desired.F = (double)research_param_f;
        desired.A = (double)research_param_a;
        desired.I = (double)research_param_i;
        desired.V = (double)research_param_v;
        UpdateResearchPredictionLatticeProbe(telemetry_opt, desired);

        const double prediction_interference_norm =
            ClampNorm01(0.58 * interference_norm + 0.42 * research_lattice_probe_interference_norm);
        const double prediction_coherence_norm =
            ClampNorm01(0.74 * coherence_norm + 0.26 * research_lattice_probe_temporal_coupling_norm);
        const double prediction_source_vibration_norm =
            ClampNorm01(0.68 * source_vibration_norm + 0.32 * research_lattice_probe_temporal_coupling_norm);

        genesis::GeResearchGpuAdaptiveCalibration next_calibration{};
        std::vector<genesis::GeResearchInterferencePredictionCell> next_predictions;
        if (!genesis::ge_build_research_gpu_interference_predictions(
                research_archive,
                desired,
                gpu_freq_norm,
                gpu_amp_norm,
                gpu_curr_norm,
                gpu_volt_norm,
                prediction_interference_norm,
                prediction_coherence_norm,
                prediction_source_vibration_norm,
                subsystem_residual_norm,
                subsystem_spin_norm,
                subsystem_coupling_norm,
                subsystem_controller_norm,
                research_gpu_prediction_axis_resolution_u32,
                next_calibration,
                &next_predictions)) {
            research_gpu_calibration = genesis::GeResearchGpuAdaptiveCalibration{};
            research_gpu_predictions.clear();
            return;
        }

        research_gpu_calibration = std::move(next_calibration);
        research_gpu_predictions = std::move(next_predictions);
        research_gpu_prediction_last_tick_u64 = tick_u64;
        PersistResearchGpuPredictionSurface();
        PersistResearchRuntimeState();
    }

    void UpdateResearchTemporalGuidance() {
        research_guidance = genesis::GeResearchRuntimeGuidance{};
        if (!research_guidance_enabled) return;
        if (!genesis::ge_has_research_runtime_guidance(research_archive)) return;

        EwProcessSubstrateTelemetry telemetry{};
        const bool has_process_telemetry = sm.get_process_substrate_telemetry(&telemetry);
        EwSubsystemSubstrateTelemetry subsystem_telemetry{};
        const bool has_subsystem_telemetry = sm.get_subsystem_substrate_telemetry(&subsystem_telemetry);
        const double residual_norm =
            has_process_telemetry ? ((double)telemetry.residual_norm_q15 / 65535.0) : 0.0;
        const double interference_norm =
            has_process_telemetry ? ((double)telemetry.interference_norm_q15 / 65535.0) : 0.0;
        const double coherence_norm =
            has_process_telemetry ? ((double)telemetry.coherence_norm_q15 / 65535.0) : 0.0;
        const double source_vibration_norm =
            has_process_telemetry ? ((double)telemetry.source_vibration_q15 / 65535.0) : 0.0;

        UpdateResearchGpuAdaptiveCalibration(has_process_telemetry ? &telemetry : nullptr,
                                             has_subsystem_telemetry ? &subsystem_telemetry : nullptr);

        genesis::GeResearchPulseQuartet desired{};
        desired.F = (double)research_param_f;
        desired.A = (double)research_param_a;
        desired.I = (double)research_param_i;
        desired.V = (double)research_param_v;
        if (research_gpu_calibration.valid) {
            const double blend =
                ClampNorm01(0.18 + 0.42 * research_gpu_calibration.prediction_confidence_norm +
                            0.20 * research_gpu_calibration.best_gpu_alignment_norm +
                            0.20 * research_gpu_calibration.best_interference_norm);
            desired.F = desired.F + (research_gpu_calibration.adapted_quartet.F - desired.F) * blend;
            desired.A = desired.A + (research_gpu_calibration.adapted_quartet.A - desired.A) * blend;
            desired.I = desired.I + (research_gpu_calibration.adapted_quartet.I - desired.I) * blend;
            desired.V = desired.V + (research_gpu_calibration.adapted_quartet.V - desired.V) * blend;
        }

        if (!genesis::ge_build_research_runtime_guidance(research_archive,
                                                         desired,
                                                         residual_norm,
                                                         interference_norm,
                                                         coherence_norm,
                                                         source_vibration_norm,
                                                         research_guidance)) {
            UpdateResearchLiveComputeState();
            RecordResearchGuidanceFault(sm.canonical_tick_u64(), "build_failed", true);
            EmitResearchPulseQuartet(research_archive.run043_center_quartet,
                                     has_process_telemetry ? &telemetry : nullptr);
            return;
        }

        UpdateResearchLiveComputeState();

        const bool metrics_finite =
            std::isfinite(research_guidance.predicted_silicon_score) &&
            std::isfinite(research_guidance.predicted_trap_ratio) &&
            std::isfinite(research_guidance.predicted_coherence) &&
            std::isfinite(research_guidance.predicted_inertia) &&
            std::isfinite(research_guidance.predicted_curvature);
        const genesis::GeResearchPulseQuartet& corrected = research_guidance.corrected_quartet;
        const bool corrected_valid =
            research_guidance.valid &&
            IsFiniteResearchQuartet(corrected) &&
            ResearchQuartetWithinCalibratedWindow(corrected) &&
            metrics_finite;
        if (!corrected_valid) {
            RecordResearchGuidanceFault(sm.canonical_tick_u64(), "out_of_window", true);
            research_guidance.corrected_quartet = research_archive.run043_center_quartet;
            EmitResearchPulseQuartet(research_archive.run043_center_quartet,
                                     has_process_telemetry ? &telemetry : nullptr);
            return;
        }

        ClearResearchGuidanceFault();
        genesis::GeResearchPulseQuartet emit_quartet =
            (research_live_compute_active && research_live_compute_plan.valid)
                ? research_live_compute_plan.compute_quartet
                : corrected;
        if (research_gpu_calibration.valid) {
            const double temporal_prediction_blend =
                ClampNorm01(0.14 +
                            0.28 * research_guidance.temporal_coupling_norm +
                            0.24 * research_gpu_calibration.best_temporal_coupling_norm +
                            0.22 * research_gpu_calibration.next_pulse_correction_norm +
                            0.12 * research_lattice_probe_temporal_coupling_norm);
            emit_quartet.F =
                emit_quartet.F + (research_gpu_calibration.next_pulse_quartet.F - emit_quartet.F) * temporal_prediction_blend;
            emit_quartet.A =
                emit_quartet.A + (research_gpu_calibration.next_pulse_quartet.A - emit_quartet.A) * temporal_prediction_blend;
            emit_quartet.I =
                emit_quartet.I + (research_gpu_calibration.next_pulse_quartet.I - emit_quartet.I) * temporal_prediction_blend;
            emit_quartet.V =
                emit_quartet.V + (research_gpu_calibration.next_pulse_quartet.V - emit_quartet.V) * temporal_prediction_blend;
        }
        if (!IsFiniteResearchQuartet(emit_quartet) || !ResearchQuartetWithinCalibratedWindow(emit_quartet)) {
            RecordResearchGuidanceFault(sm.canonical_tick_u64(), "next_pulse_out_of_window", true);
            EmitResearchPulseQuartet(research_archive.run043_center_quartet,
                                     has_process_telemetry ? &telemetry : nullptr);
            return;
        }
        EmitResearchPulseQuartet(emit_quartet, has_process_telemetry ? &telemetry : nullptr);
    }

    void SyncResearchWatchState() {
        research_watch_file.clear();
        research_cmd_file.clear();
        std::error_code ec;
        if (research_archive.root_dir.empty()) return;
        const std::filesystem::path probe = research_archive.root_dir / "photon_packet_trajectory_sample.json";
        if (!std::filesystem::exists(probe, ec) || !std::filesystem::is_regular_file(probe, ec)) return;
        research_watch_file = probe;
        research_cmd_file = research_archive.root_dir / "runtime_commands.txt";
        ec.clear();
        const auto write_time = std::filesystem::last_write_time(research_watch_file, ec);
        if (!ec) {
            research_watch_write_time = write_time;
        }
        ec.clear();
        const auto cmd_write_time = std::filesystem::last_write_time(research_cmd_file, ec);
        if (!ec) {
            research_cmd_write_time = cmd_write_time;
            const uint64_t cmd_size = (uint64_t)std::filesystem::file_size(research_cmd_file, ec);
            if (!ec) research_cmd_read_offset_u64 = cmd_size;
        }
    }

    void ReloadResearchArchive(bool announce) {
        genesis::GeResearchConfinementArchive next_archive = genesis::ge_load_research_confinement_archive();
        if (next_archive.loaded) {
            research_archive = std::move(next_archive);
            research_points.clear();
            research_audio_frame = genesis::GeResearchAudioFrame{};
            research_guidance = genesis::GeResearchRuntimeGuidance{};
            research_gpu_calibration = genesis::GeResearchGpuAdaptiveCalibration{};
            research_live_compute_plan = genesis::GeResearchLiveComputePlan{};
            research_gpu_predictions.clear();
            research_guidance_last_pulse_tick_u64 = 0u;
            research_gpu_prediction_last_tick_u64 = 0u;
            research_guidance_has_pulse_history = false;
            research_live_compute_active = false;
            research_live_compute_activation_tick_u64 = 0u;
            research_guidance_watchdog_fault = false;
            SyncResearchWatchState();
            if (research_basic_trial_enabled) {
                research_live_boot_enabled = false;
                research_guidance_enabled = true;
                research_live_compute_enabled = false;
                research_live_compute_active = false;
                research_visual_stream_hz_u32 =
                    std::min<uint32_t>(research_visual_stream_hz_u32, ResearchBasicTrialMaxStreamHz());
                research_lattice_edge_u32 =
                    ClampLatticeEdge(std::min<uint32_t>(research_lattice_edge_u32, ResearchBasicTrialMaxLatticeEdge()));
                if (genesis::ge_has_research_runtime_guidance(research_archive)) {
                    research_param_f = (float)std::max(research_archive.pulse_window_min_quartet.F,
                                                      std::min(research_archive.pulse_window_max_quartet.F, (double)research_param_f));
                    research_param_a = (float)std::max(research_archive.pulse_window_min_quartet.A,
                                                      std::min(research_archive.pulse_window_max_quartet.A, (double)research_param_a));
                    research_param_i = (float)std::max(research_archive.pulse_window_min_quartet.I,
                                                      std::min(research_archive.pulse_window_max_quartet.I, (double)research_param_i));
                    research_param_v = (float)std::max(research_archive.pulse_window_min_quartet.V,
                                                      std::min(research_archive.pulse_window_max_quartet.V, (double)research_param_v));
                }
                genesis::GeResearchPulseQuartet q{};
                q.F = (double)research_param_f;
                q.A = (double)research_param_a;
                q.I = (double)research_param_i;
                q.V = (double)research_param_v;
                UpdateResearchAxisScaleState(q);
                PersistResearchRuntimeState();
            } else {
                ApplyResearchLiveBootPreset(false);
            }
            if (announce) {
                std::ostringstream oss;
                oss << "RESEARCH_FREQ:reloaded packets="
                    << research_archive.packet_path_classes.size()
                    << " tensor=" << research_archive.tensor6d_cells.size()
                    << " vectors=" << research_archive.vector_excitations.size();
                PushLocalUiLine(oss.str());
            }
        } else if (announce) {
            const std::string msg = next_archive.load_error_utf8.empty()
                ? std::string("unknown_error")
                : next_archive.load_error_utf8;
            PushLocalUiLine(std::string("RESEARCH_FREQ:reload_failed reason=") + msg);
        }
    }

    void PollResearchArchiveHotReload() {
        const uint64_t tick_u64 = sm.canonical_tick;
        if (tick_u64 < research_reload_probe_tick_u64 + 120u) return;
        research_reload_probe_tick_u64 = tick_u64;

        if (research_watch_file.empty()) {
            SyncResearchWatchState();
            return;
        }

        std::error_code ec;
        const auto write_time = std::filesystem::last_write_time(research_watch_file, ec);
        if (ec) return;
        if (write_time != research_watch_write_time) {
            research_watch_write_time = write_time;
            ReloadResearchArchive(true);
        }
    }

    void PollResearchCommandFile() {
        const uint64_t tick_u64 = sm.canonical_tick;
        if (tick_u64 < research_cmd_probe_tick_u64 + 45u) return;
        research_cmd_probe_tick_u64 = tick_u64;
        if (research_cmd_file.empty()) return;

        std::error_code ec;
        const auto write_time = std::filesystem::last_write_time(research_cmd_file, ec);
        if (ec) return;
        if (write_time == research_cmd_write_time) return;
        research_cmd_write_time = write_time;

        std::ifstream f(research_cmd_file, std::ios::in | std::ios::binary);
        if (!f.good()) return;
        const uint64_t file_size = (uint64_t)std::filesystem::file_size(research_cmd_file, ec);
        if (ec) return;
        if (file_size < research_cmd_read_offset_u64) {
            // File rotated/truncated; restart from beginning.
            research_cmd_read_offset_u64 = 0u;
        }
        if (research_cmd_read_offset_u64 > 0u) {
            f.seekg((std::streamoff)research_cmd_read_offset_u64, std::ios::beg);
            if (!f.good()) {
                f.clear();
                f.seekg(0, std::ios::beg);
                research_cmd_read_offset_u64 = 0u;
            }
        }
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            SubmitUiLine(line);
        }
        std::streampos end_pos = f.tellg();
        if (end_pos < (std::streampos)0) {
            research_cmd_read_offset_u64 = file_size;
        } else {
            research_cmd_read_offset_u64 = (uint64_t)end_pos;
        }
    }

    void RunFrequencySimulator(bool write_root_samples) {
        std::filesystem::path repo_root = std::filesystem::current_path();
        const std::vector<std::filesystem::path> candidates = {
            repo_root / "out" / "build" / "win64-vs2022-clang-vulkan" / "Release" / "photon_frequency_domain_sim.exe",
            repo_root / "out" / "build" / "win64-vs2022-clang-vulkan" / "Debug" / "photon_frequency_domain_sim.exe",
            repo_root / "photon_frequency_domain_sim.exe"
        };
        std::filesystem::path exe;
        for (const auto& path : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec)) {
                exe = path;
                break;
            }
        }
        if (exe.empty()) {
            PushLocalUiLine("RESEARCH_FREQ:run_failed reason=sim_exe_not_found");
            return;
        }

        std::ostringstream cmd;
        cmd << "\"" << exe.string() << "\"";
        if (write_root_samples) cmd << " --write-root-samples";
        cmd << " --equivalent-grid-linear " << research_lattice_edge_u32;
        // Map runtime A/F/I/V control into simulator axes.
        const int bin_count = (int)std::max(64.0f, std::min(128.0f, 64.0f + 64.0f * research_param_f));
        const int packet_count = (int)std::max(10.0f, std::min(50.0f, 10.0f + 40.0f * research_param_a));
        const int steps = (int)std::max(24.0f, std::min(160.0f, 24.0f + 136.0f * research_param_i));
        const int recon_samples = (int)std::max(128.0f, std::min(1024.0f, 128.0f + 896.0f * research_param_v));
        cmd << " --bin-count " << bin_count;
        cmd << " --packet-count " << packet_count;
        cmd << " --steps " << steps;
        cmd << " --recon-samples " << recon_samples;
        cmd << " --pulse-frequency " << std::max(0.0f, std::min(1.0f, research_param_f));
        cmd << " --pulse-amplitude " << std::max(0.0f, std::min(1.0f, research_param_a));
        cmd << " --pulse-amperage " << std::max(0.0f, std::min(1.0f, research_param_i));
        cmd << " --pulse-voltage " << std::max(0.0f, std::min(1.0f, research_param_v));

        std::filesystem::path nist_path = research_archive.root_dir.empty()
            ? (repo_root / "ResearchConfinement" / "nist_silicon_reference.json")
            : (research_archive.root_dir / "nist_silicon_reference.json");
        std::error_code nist_ec;
        if (std::filesystem::exists(nist_path, nist_ec) && std::filesystem::is_regular_file(nist_path, nist_ec)) {
            cmd << " --nist-ref \"" << nist_path.string() << "\"";
        }
        PushLocalUiLine(std::string("RESEARCH_FREQ:run_start exe=") + exe.string());

        const int rc = std::system(cmd.str().c_str());
        if (rc == 0) {
            PushLocalUiLine("RESEARCH_FREQ:run_complete code=0");
            ReloadResearchArchive(true);
        } else {
            std::ostringstream oss;
            oss << "RESEARCH_FREQ:run_failed code=" << rc;
            PushLocalUiLine(oss.str());
        }
    }

    Scene() : sm(128), lattice(128u, 128u, 128u) {
        // seed one anchor pulse so AI loop is alive
        sm.ui_submit_user_text_line("BOOT: vulkan_app");

        // Deterministic reservoir baseline for early import bring-up.
        sm.reservoir = (int64_t)(sm.anchors.size()) << 32;

        density_mask_u8.assign((size_t)128u * (size_t)128u * (size_t)128u, 0u);
        // Deterministic seed constant (ASCII mnemonic encoded as hex-ish).
        lattice.init(0xE16E5151ULL);
        research_archive = genesis::ge_load_research_confinement_archive();
        research_points.reserve(20000u);
        SyncResearchWatchState();
        ApplyResearchLiveBootPreset(research_archive.loaded);
        PersistResearchRuntimeState();

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
        sun.refresh_fixed_cache();
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
        earth.refresh_fixed_cache();
    objects.push_back(earth);

    selected = 1; // Earth selected by default

    // Register default celestial bodies as planet anchors (authoritative transforms).
    {
        auto emit_planet = [&](const Object& o) {
            EwControlPacket cp{};
            cp.kind = EwControlPacketKind::ObjectRegister;
            cp.source_u16 = 1;
            cp.tick_u64 = sm.canonical_tick;
            cp.payload.object_register.object_id_u64 = o.object_id_u64;
            cp.payload.object_register.kind_u32 = EW_ANCHOR_KIND_PLANET;
            cp.payload.object_register.pad0_u32 = 0u;
            cp.payload.object_register.pos_q16_16[0] = (int32_t)llround((double)o.xf.pos[0] * 65536.0);
            cp.payload.object_register.pos_q16_16[1] = (int32_t)llround((double)o.xf.pos[1] * 65536.0);
            cp.payload.object_register.pos_q16_16[2] = (int32_t)llround((double)o.xf.pos[2] * 65536.0);
            cp.payload.object_register.rot_quat_q16_16[0] = 0;
            cp.payload.object_register.rot_quat_q16_16[1] = 0;
            cp.payload.object_register.rot_quat_q16_16[2] = 0;
            cp.payload.object_register.rot_quat_q16_16[3] = 65536;
            cp.payload.object_register.radius_m_q16_16 = (int32_t)llround((double)o.radius_m_f32 * 65536.0);
            cp.payload.object_register.albedo_rgba8 = o.albedo_rgba8;
            cp.payload.object_register.atmosphere_rgba8 = o.atmosphere_rgba8;
            cp.payload.object_register.atmosphere_thickness_m_q16_16 = (int32_t)llround((double)o.atmosphere_thickness_m_f32 * 65536.0);
            cp.payload.object_register.emissive_q16_16 = (int32_t)llround((double)o.emissive_f32 * 65536.0);
            (void)ew_runtime_submit_control_packet(&sm, &cp);
        };
        emit_planet(objects[0]);
        emit_planet(objects[1]);
    }
}
    }

    
    void Tick() {
    UpdateResearchBasicTrialState();
    UpdateResearchTemporalGuidance();
    sm.tick();
    PollResearchArchiveHotReload();
    PollResearchCommandFile();


    // Pull authoritative render-object packets from substrate (object/planet anchors).
    {
        const EwRenderObjectPacket* ptr = nullptr;
        size_t count = 0u;
        if (ew_runtime_get_render_object_packets(&sm, &ptr, &count) && ptr && count > 0u) {
            // Update local scene cache for UI selection lists (not authoritative).
            for (size_t i = 0u; i < count; ++i) {
                const EwRenderObjectPacket& p = ptr[i];
                for (auto& o : objects) {
                    if (o.object_id_u64 == p.object_id_u64) {
                        o.anchor_id_u32 = p.anchor_id_u32;
                        o.xf.pos[0] = (float)p.pos_q16_16[0] / 65536.0f;
                        o.xf.pos[1] = (float)p.pos_q16_16[1] / 65536.0f;
                        o.xf.pos[2] = (float)p.pos_q16_16[2] / 65536.0f;
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

    // Orbit updates are substrate-authoritative; viewport does not compute analytic motion.
    // Earth position is updated from substrate-projected object packets when available.
    (void)earth_orbit_angle_rad;

    // Frequency-domain mode does not require voxel stepping; vector mode keeps lattice stepping active.
    if (research_output_mode_u8 == 2u) {
        lattice.step_one_tick();
    }

    if (genesis::ge_has_research_realtime_outputs(research_archive)) {
        if (research_visual_stream_enabled) {
            const uint32_t hz = std::max<uint32_t>(research_visual_stream_hz_u32, 1u);
            const float frame_phase_01 = (float)((double)(sm.canonical_tick % hz) / (double)hz);
            const float temporal_coupling_01 =
                std::max(0.0f, std::min(1.0f,
                                        0.40f + 0.30f * research_param_a +
                                        0.20f * research_param_f +
                                        0.15f * std::sin(frame_phase_01 * 6.2831853f + research_param_v)));
            genesis::ge_build_research_particle_viz(research_archive,
                                                    sm.canonical_tick,
                                                    research_visual_max_points,
                                                    frame_phase_01,
                                                    temporal_coupling_01,
                                                    research_points);
            if (research_stov_mode) {
                // STOV mode: apply a paraxial helical phase ramp in the moving frame proxy.
                // This keeps emergence tied to phase/temporal coupling without injecting explicit vectors.
                for (size_t i = 0u; i < research_points.size(); ++i) {
                    auto& p = research_points[i];
                    const float phi = std::atan2(p.y, p.x);
                    const float charge_l = (float)((int)(i % 4u) - 2);
                    const float helical = std::sin(phi * charge_l + frame_phase_01 * 6.2831853f);
                    const float swirl = 0.015f * (0.5f + 0.5f * p.temporal_coupling);
                    const float tx = p.x;
                    const float ty = p.y;
                    p.x = tx - ty * swirl * helical;
                    p.y = ty + tx * swirl * helical;
                    p.phase_bias = std::max(-1.0f, std::min(1.0f, p.phase_bias + 0.35f * helical));
                    p.curvature_n = std::max(0.0f, std::min(1.0f, p.curvature_n + 0.20f * std::fabs(helical)));
                }
            }
            if (research_output_mode_u8 == 2u) {
                // Vector-only mode: show extrapolated vector + tensor glyph layers.
                research_points.erase(std::remove_if(research_points.begin(),
                                                     research_points.end(),
                                                     [](const genesis::GeResearchParticleVizPoint& p) {
                                                         return !(p.render_kind_u32 == 4u || p.render_kind_u32 == 5u);
                                                     }),
                                     research_points.end());
            }
            research_visual_last_tick_u64 = sm.canonical_tick;
        }
        if (!research_use_compute_audio) {
            genesis::ge_build_research_audio_frame(research_archive, sm.canonical_tick, 64u, research_audio_frame);
            if (!research_audio_frame.interleaved_pcm16.empty()) {
                sm.inject_audio_pcm16(research_audio_frame.interleaved_pcm16.data(),
                                      (int)(research_audio_frame.interleaved_pcm16.size() /
                                            (size_t)std::max<uint32_t>(research_audio_frame.channel_count_u32, 1u)),
                                      (int)research_audio_frame.channel_count_u32);
            }
        } else {
            research_audio_frame = genesis::GeResearchAudioFrame{};
        }
    } else {
        research_audio_frame = genesis::GeResearchAudioFrame{};
        research_points.clear();
    }
}

    bool PopUiLine(std::string& out_utf8) {
        if (!local_output_lines.empty()) {
            out_utf8 = local_output_lines.front();
            local_output_lines.erase(local_output_lines.begin());
            return true;
        }
        return sm.ui_pop_output_text(out_utf8);
    }

    void SubmitUiLine(const std::string& utf8) {
        UpdateResearchBasicTrialState();
        auto push_process_substrate_status_local = [&]() {
            EwProcessSubstrateTelemetry telemetry{};
            if (!sm.get_process_substrate_telemetry(&telemetry)) {
                std::ostringstream oss;
                oss << "PROCESS_SUBSTRATE_STATUS valid=0"
                    << " tick=" << sm.canonical_tick_u64();
            PushLocalUiLine(oss.str());
            PushLocalUiLine(BuildResearchGpuCalibrationStatusLine());
            return;
        }
            std::ostringstream oss;
            oss << "PROCESS_SUBSTRATE_STATUS valid=" << telemetry.valid_u32
                << " tick=" << telemetry.tick_u64
                << " spectral_anchor=" << telemetry.spectral_anchor_id_u32
                << " coherence_anchor=" << telemetry.coherence_anchor_id_u32
                << " fanout=" << telemetry.fanout_budget_u32
                << " intent_q15=" << telemetry.intent_norm_q15
                << " Emean_q15=" << telemetry.energy_mean_q15
                << " Epeak_q15=" << telemetry.energy_peak_q15
                << " LeakAbs_q15=" << telemetry.leakage_abs_q15
                << " residual_q15=" << telemetry.residual_norm_q15
                << " dErr_q15=" << telemetry.error_delta_norm_q15
                << " intErr_q15=" << telemetry.error_integral_norm_q15
                << " ctrl_q15=" << telemetry.controller_authority_q15
                << " dt_q32_32=" << telemetry.dt_scale_q32_32
                << " visc_q32_32=" << telemetry.viscosity_bias_q32_32
                << " err_q32_32=" << telemetry.error_q32_32
                << " derr_q32_32=" << telemetry.error_delta_q32_32
                << " ierr_q32_32=" << telemetry.error_integral_q32_32
                << " gpuF_q15=" << telemetry.gpu_freq_norm_q15
                << " gpuA_q15=" << telemetry.gpu_amp_norm_q15
                << " gpuV_q15=" << telemetry.gpu_volt_norm_q15
                << " intf_q15=" << telemetry.interference_norm_q15
                << " coh_q15=" << telemetry.coherence_norm_q15
                << " obs_q15=" << telemetry.observer_norm_q15
                << " vib_q15=" << telemetry.source_vibration_q15
                << " calctrl_q15=" << telemetry.calibration_authority_q15
                << " calerr_q32_32=" << telemetry.calibration_error_q32_32
                << " calderr_q32_32=" << telemetry.calibration_delta_q32_32
                << " calierr_q32_32=" << telemetry.calibration_integral_q32_32
                << " gain_q15=" << telemetry.op_gain_q15
                << " phase_q15=" << telemetry.op_phase_bias_q15
                << " learn_q15=" << telemetry.learning_coupling_q15
                << " phys_q15=" << telemetry.phys_coherence_q15
                << " lrncoh_q15=" << telemetry.learning_coherence_q15
                << " tempcoh_q15=" << telemetry.temporal_coherence_q15
                << " V=" << telemetry.last_v_code_u16
                << " I=" << telemetry.last_i_code_u16
                << " pending=" << (uint32_t)telemetry.residual_pending_u8
                << " hold=" << (uint32_t)telemetry.hold_tick_u8
                << " commit=" << (uint32_t)telemetry.last_commit_u8;
            PushLocalUiLine(oss.str());
        };
        auto push_subsystem_substrate_status_local = [&]() {
            EwSubsystemSubstrateTelemetry telemetry{};
            if (!sm.get_subsystem_substrate_telemetry(&telemetry)) {
                std::ostringstream oss;
                oss << "SUBSYSTEM_SUBSTRATE_STATUS valid=0"
                    << " tick=" << sm.canonical_tick_u64();
                PushLocalUiLine(oss.str());
                return;
            }
            std::ostringstream oss;
            oss << "SUBSYSTEM_SUBSTRATE_STATUS valid=" << telemetry.valid_u32
                << " tick=" << telemetry.tick_u64
                << " count=" << telemetry.subsystem_count_u32
                << " active=" << telemetry.active_subsystem_count_u32
                << " dominant=" << telemetry.dominant_subsystem_id_u32
                << " aggIntent_q15=" << telemetry.aggregate_intent_norm_q15
                << " aggMeasured_q15=" << telemetry.aggregate_measured_norm_q15
                << " aggResidual_q15=" << telemetry.aggregate_residual_norm_q15
                << " aggSpin_q15=" << telemetry.aggregate_spin_norm_q15
                << " aggCoupling_q15=" << telemetry.aggregate_coupling_norm_q15
                << " aggCtrl_q15=" << telemetry.aggregate_controller_authority_q15
                << " aggErr_q32_32=" << telemetry.aggregate_error_q32_32
                << " aggDErr_q32_32=" << telemetry.aggregate_error_delta_q32_32
                << " aggIErr_q32_32=" << telemetry.aggregate_error_integral_q32_32;
            for (uint32_t i = 0u; i < telemetry.subsystem_count_u32 &&
                                  i < EW_SUBSYSTEM_SUBSTRATE_LANE_CAP; ++i) {
                const EwSubsystemCalculusLane& lane = telemetry.lanes[i];
                if (lane.subsystem_id_u32 == EW_SUBSYSTEM_PLANE_NONE) continue;
                oss << " s" << lane.subsystem_id_u32
                    << "(r=" << lane.residual_norm_q15
                    << ",s=" << lane.spin_norm_q15
                    << ",c=" << lane.coupling_norm_q15
                    << ",a=" << lane.controller_authority_q15
                    << ")";
            }
            PushLocalUiLine(oss.str());
        };
        auto push_ai_substrate_status_local = [&]() {
            EwAiSubstrateTelemetry telemetry{};
            if (!sm.get_ai_substrate_telemetry(&telemetry)) {
                std::ostringstream oss;
                oss << "AI_SUBSTRATE_STATUS valid=0"
                    << " tick=" << sm.canonical_tick_u64();
                PushLocalUiLine(oss.str());
                return;
            }
            std::ostringstream oss;
            oss << "AI_SUBSTRATE_STATUS valid=" << telemetry.valid_u32
                << " tick=" << telemetry.tick_u64
                << " class=" << telemetry.class_id_u32
                << " sig9=" << telemetry.sig9_u64
                << " obs=" << telemetry.observation_seq_u64
                << " cmd_obs=" << telemetry.dispatched_observation_seq_u64
                << " conf_q32_32=" << telemetry.confidence_q32_32
                << " attract_q32_32=" << telemetry.attractor_strength_q32_32
                << " cmds=" << telemetry.command_count_u32
                << " actions=" << telemetry.action_log_count_u32
                << " carrier_q63=" << telemetry.carrier_pulse_q63
                << " totalw_q63=" << telemetry.carrier_total_weight_q63
                << " pending_api=" << telemetry.pending_external_api_u32
                << " inflight_api=" << telemetry.inflight_external_api_u32
                << " fanout=" << telemetry.fanout_budget_u32
                << " ant=" << telemetry.anticipation_flags_u16
                << " ctrl_q15=" << telemetry.controller_authority_q15
                << " gain_q15=" << telemetry.op_gain_q15
                << " phase_q15=" << telemetry.op_phase_bias_q15
                << " b0=" << telemetry.carrier_band_q15[0]
                << " b1=" << telemetry.carrier_band_q15[1]
                << " b2=" << telemetry.carrier_band_q15[2]
                << " b3=" << telemetry.carrier_band_q15[3]
                << " b4=" << telemetry.carrier_band_q15[4]
                << " b5=" << telemetry.carrier_band_q15[5]
                << " b6=" << telemetry.carrier_band_q15[6]
                << " b7=" << telemetry.carrier_band_q15[7]
                << " kind=" << telemetry.last_action_kind_u16
                << " profile=" << telemetry.last_action_profile_u16
                << " anchor=" << telemetry.last_target_anchor_id_u32
                << " f=" << telemetry.last_f_code_i32
                << " a=" << telemetry.last_a_code_u16
                << " v=" << telemetry.last_v_code_u16
                << " i=" << telemetry.last_i_code_u16;
            PushLocalUiLine(oss.str());
        };
        auto push_ai_data_substrate_status_local = [&]() {
            EwAiDataSubstrateTelemetry telemetry{};
            if (!sm.get_ai_data_substrate_telemetry(&telemetry)) {
                std::ostringstream oss;
                oss << "AI_DATA_SUBSTRATE_STATUS valid=0"
                    << " tick=" << sm.canonical_tick_u64();
                PushLocalUiLine(oss.str());
                return;
            }
            std::ostringstream oss;
            oss << "AI_DATA_SUBSTRATE_STATUS valid=" << telemetry.valid_u32
                << " tick=" << telemetry.tick_u64
                << " corpus=" << telemetry.corpus_artifact_count_u32
                << " corpus_txt=" << telemetry.corpus_text_artifact_count_u32
                << " manifest=" << telemetry.manifest_record_count_u32
                << " pending_metrics=" << telemetry.pending_metric_count_u32
                << " completed_metrics=" << telemetry.completed_metric_count_u32
                << " accepted_metrics=" << telemetry.accepted_metric_count_u32
                << " lex=" << telemetry.language_word_count_u32
                << " pron=" << telemetry.language_pron_count_u32
                << " rel=" << telemetry.language_relation_count_u32
                << " concept=" << telemetry.language_concept_count_u32
                << " speech=" << telemetry.language_speech_utt_count_u32
                << " pemdas=" << telemetry.math_pemdas_cases_passed_u32
                << "/" << telemetry.math_pemdas_cases_total_u32
                << " graph=" << telemetry.math_graph_packets_emitted_u32
                << " khan_pages=" << telemetry.math_khan_pages_seen_u32
                << " khan_chars=" << telemetry.math_khan_chars_ingested_u32
                << " pending_api=" << telemetry.pending_external_api_u32
                << " inflight_api=" << telemetry.inflight_external_api_u32
                << " intent_q15=" << telemetry.intent_norm_q15
                << " measured_q15=" << telemetry.measured_norm_q15
                << " residual_q15=" << telemetry.residual_norm_q15
                << " dErr_q15=" << telemetry.error_delta_norm_q15
                << " intErr_q15=" << telemetry.error_integral_norm_q15
                << " ctrl_q15=" << telemetry.controller_authority_q15
                << " err_q32_32=" << telemetry.error_q32_32
                << " derr_q32_32=" << telemetry.error_delta_q32_32
                << " ierr_q32_32=" << telemetry.error_integral_q32_32
                << " t0=" << telemetry.intent_band_q15[0]
                << " t1=" << telemetry.intent_band_q15[1]
                << " t2=" << telemetry.intent_band_q15[2]
                << " t3=" << telemetry.intent_band_q15[3]
                << " t4=" << telemetry.intent_band_q15[4]
                << " t5=" << telemetry.intent_band_q15[5]
                << " t6=" << telemetry.intent_band_q15[6]
                << " t7=" << telemetry.intent_band_q15[7]
                << " m0=" << telemetry.measured_band_q15[0]
                << " m1=" << telemetry.measured_band_q15[1]
                << " m2=" << telemetry.measured_band_q15[2]
                << " m3=" << telemetry.measured_band_q15[3]
                << " m4=" << telemetry.measured_band_q15[4]
                << " m5=" << telemetry.measured_band_q15[5]
                << " m6=" << telemetry.measured_band_q15[6]
                << " m7=" << telemetry.measured_band_q15[7];
            PushLocalUiLine(oss.str());
        };

        if (utf8.rfind("/basic_trial_status", 0) == 0 || utf8.rfind("basic_trial_status:", 0) == 0 ||
            utf8.rfind("/meta_basic_mode_status", 0) == 0 || utf8.rfind("meta_basic_mode_status:", 0) == 0) {
            PushLocalUiLine(BuildResearchBasicTrialStatusLine());
            return;
        }
        if (utf8.rfind("/basic_trial", 0) == 0 || utf8.rfind("basic_trial:", 0) == 0 ||
            utf8.rfind("/meta_basic_mode", 0) == 0 || utf8.rfind("meta_basic_mode:", 0) == 0 ||
            utf8.rfind("/meta_basic_trial", 0) == 0 || utf8.rfind("meta_basic_trial:", 0) == 0) {
            const bool off = (utf8.find("off") != std::string::npos ||
                              utf8.find("OFF") != std::string::npos ||
                              utf8.find("false") != std::string::npos ||
                              utf8.find("FALSE") != std::string::npos);
            uint32_t minutes_u32 = research_basic_trial_minutes_u32;
            const std::size_t mp = utf8.find("minutes=");
            if (mp != std::string::npos && mp + 8u < utf8.size()) {
                minutes_u32 = (uint32_t)std::strtoul(utf8.substr(mp + 8u).c_str(), nullptr, 10);
            }
            if (off) {
                DisableResearchBasicTrial(true);
            } else {
                ApplyResearchBasicTrialPreset(minutes_u32 == 0u ? 60u : minutes_u32, true);
            }
            return;
        }
        if (research_basic_trial_enabled && research_basic_trial_expired &&
            !ResearchCommandLooksLikeStatus(utf8)) {
            PushResearchBasicTrialBlocked("session", "expired");
            return;
        }
        if (ResearchBasicTrialShouldBlockAdvancedCommand(utf8)) {
            PushResearchBasicTrialBlocked("advanced", "basic_trial_mode");
            return;
        }
        if (utf8.rfind("/research_reload", 0) == 0 || utf8.rfind("research_reload:", 0) == 0 ||
            utf8.rfind("/sim_freq_reload", 0) == 0 || utf8.rfind("sim_freq_reload:", 0) == 0) {
            ReloadResearchArchive(true);
            return;
        }
        if (utf8.rfind("/research_live_mode", 0) == 0 || utf8.rfind("research_live_mode:", 0) == 0) {
            const bool on = (utf8.find("1") != std::string::npos ||
                             utf8.find("on") != std::string::npos ||
                             utf8.find("ON") != std::string::npos ||
                             utf8.find("true") != std::string::npos ||
                             utf8.find("TRUE") != std::string::npos);
            research_live_boot_enabled = on;
            research_guidance_enabled = on;
            research_live_compute_enabled = on;
            if (on) {
                ApplyResearchLiveBootPreset(true);
            } else {
                research_live_compute_active = false;
                PersistResearchRuntimeState();
                PushLocalUiLine("RESEARCH_FREQ:live_boot=off guidance=off live_compute=off");
            }
            return;
        }
        if (utf8.rfind("/research_live_compute", 0) == 0 || utf8.rfind("research_live_compute:", 0) == 0) {
            const bool on = (utf8.find("1") != std::string::npos ||
                             utf8.find("on") != std::string::npos ||
                             utf8.find("ON") != std::string::npos ||
                             utf8.find("true") != std::string::npos ||
                             utf8.find("TRUE") != std::string::npos);
            research_live_compute_enabled = on;
            if (!on) research_live_compute_active = false;
            PersistResearchRuntimeState();
            PushLocalUiLine(on ? "RESEARCH_FREQ:live_compute=armed" : "RESEARCH_FREQ:live_compute=disarmed");
            return;
        }
        if (utf8.rfind("/research_gpu_prediction_axis", 0) == 0 || utf8.rfind("research_gpu_prediction_axis:", 0) == 0) {
            const std::size_t sp = utf8.find_last_of("=:");
            if (sp != std::string::npos && sp + 1 < utf8.size()) {
                const uint32_t axis = (uint32_t)std::strtoul(utf8.substr(sp + 1).c_str(), nullptr, 10);
                research_gpu_prediction_axis_resolution_u32 = std::max<uint32_t>(3u, std::min<uint32_t>(axis, 9u));
                research_gpu_prediction_last_tick_u64 = 0u;
                research_gpu_calibration = genesis::GeResearchGpuAdaptiveCalibration{};
                research_gpu_predictions.clear();
                PersistResearchRuntimeState();
                std::ostringstream oss;
                oss << "RESEARCH_FREQ:gpu_prediction_axis=" << research_gpu_prediction_axis_resolution_u32;
                PushLocalUiLine(oss.str());
            }
            return;
        }
        if (utf8.rfind("/research_gpu_prediction_refresh", 0) == 0 || utf8.rfind("research_gpu_prediction_refresh:", 0) == 0) {
            const std::size_t sp = utf8.find_last_of("=:");
            if (sp != std::string::npos && sp + 1 < utf8.size()) {
                const uint32_t ticks = (uint32_t)std::strtoul(utf8.substr(sp + 1).c_str(), nullptr, 10);
                research_gpu_prediction_refresh_ticks_u32 = std::max<uint32_t>(1u, std::min<uint32_t>(ticks, 3600u));
                PersistResearchRuntimeState();
                std::ostringstream oss;
                oss << "RESEARCH_FREQ:gpu_prediction_refresh=" << research_gpu_prediction_refresh_ticks_u32;
                PushLocalUiLine(oss.str());
            }
            return;
        }
        if (utf8.rfind("/sim_freq_run", 0) == 0 || utf8.rfind("sim_freq_run:", 0) == 0) {
            RunFrequencySimulator(true);
            return;
        }
        if (utf8.rfind("/sim_freq_status", 0) == 0 || utf8.rfind("sim_freq_status:", 0) == 0) {
            std::ostringstream oss;
            oss << "RESEARCH_FREQ:loaded=" << (research_archive.loaded ? 1 : 0)
                << " packets=" << research_archive.packet_path_classes.size()
                << " tensor=" << research_archive.tensor6d_cells.size()
                << " vectors=" << research_archive.vector_excitations.size()
                << " audio=" << research_archive.audio_waveform.size()
                << " stream_hz=" << research_visual_stream_hz_u32
                << " stream_tick=" << research_visual_last_tick_u64
                << " stream_points=" << research_points.size()
                << " mode=" << ((research_output_mode_u8 == 2u) ? "vector" : "frequency")
                << " stov=" << (research_stov_mode ? 1 : 0)
                << " lattice_edge=" << research_lattice_edge_u32
                << " A=" << research_param_a
                << " F=" << research_param_f
                << " I=" << research_param_i
                << " V=" << research_param_v;
            PushLocalUiLine(oss.str());
            PushLocalUiLine(BuildResearchBasicTrialStatusLine());
            PushLocalUiLine(BuildResearchGuidanceStatusLine());
            PushLocalUiLine(BuildResearchGpuCalibrationStatusLine());
            PushLocalUiLine(BuildResearchLiveComputeStatusLine());
            return;
        }
        if (utf8.rfind("/process_substrate_status", 0) == 0 || utf8.rfind("process_substrate_status:", 0) == 0) {
            push_process_substrate_status_local();
            return;
        }
        if (utf8.rfind("/subsystem_substrate_status", 0) == 0 ||
            utf8.rfind("subsystem_substrate_status:", 0) == 0) {
            push_subsystem_substrate_status_local();
            return;
        }
        if (utf8.rfind("/ai_status", 0) == 0 || utf8.rfind("ai_status:", 0) == 0 ||
            utf8.rfind("/ai_substrate_status", 0) == 0 || utf8.rfind("ai_substrate_status:", 0) == 0) {
            push_ai_substrate_status_local();
            return;
        }
        if (utf8.rfind("/ai_data_status", 0) == 0 || utf8.rfind("ai_data_status:", 0) == 0 ||
            utf8.rfind("/ai_data_substrate_status", 0) == 0 || utf8.rfind("ai_data_substrate_status:", 0) == 0) {
            push_ai_data_substrate_status_local();
            return;
        }
        if (utf8.rfind("/research_status", 0) == 0 || utf8.rfind("research_status:", 0) == 0) {
            std::ostringstream oss;
            oss << "RESEARCH_FREQ:loaded=" << (research_archive.loaded ? 1 : 0)
                << " packets=" << research_archive.packet_path_classes.size()
                << " tensor=" << research_archive.tensor6d_cells.size()
                << " vectors=" << research_archive.vector_excitations.size()
                << " audio=" << research_archive.audio_waveform.size()
                << " stream_hz=" << research_visual_stream_hz_u32
                << " stream_tick=" << research_visual_last_tick_u64
                << " stream_points=" << research_points.size()
                << " mode=" << ((research_output_mode_u8 == 2u) ? "vector" : "frequency")
                << " stov=" << (research_stov_mode ? 1 : 0)
                << " lattice_edge=" << research_lattice_edge_u32
                << " A=" << research_param_a
                << " F=" << research_param_f
                << " I=" << research_param_i
                << " V=" << research_param_v;
            PushLocalUiLine(oss.str());
            PushLocalUiLine(BuildResearchBasicTrialStatusLine());
            PushLocalUiLine(BuildResearchGuidanceStatusLine());
            PushLocalUiLine(BuildResearchGpuCalibrationStatusLine());
            PushLocalUiLine(BuildResearchLiveComputeStatusLine());
            push_process_substrate_status_local();
            push_subsystem_substrate_status_local();
            push_ai_substrate_status_local();
            push_ai_data_substrate_status_local();
            return;
        }
        if (utf8.rfind("/research_guidance_status", 0) == 0 || utf8.rfind("research_guidance_status:", 0) == 0) {
            PushLocalUiLine(BuildResearchGuidanceStatusLine());
            return;
        }
        if (utf8.rfind("/research_gpu_calibration_status", 0) == 0 || utf8.rfind("research_gpu_calibration_status:", 0) == 0) {
            PushLocalUiLine(BuildResearchGpuCalibrationStatusLine());
            return;
        }
        if (utf8.rfind("/research_live_compute_status", 0) == 0 || utf8.rfind("research_live_compute_status:", 0) == 0) {
            PushLocalUiLine(BuildResearchLiveComputeStatusLine());
            return;
        }
        if (utf8.rfind("/sim_mode", 0) == 0 || utf8.rfind("sim_mode:", 0) == 0) {
            if (utf8.find("vector") != std::string::npos || utf8.find("VECTOR") != std::string::npos) {
                SetResearchOutputMode(2u, true);
            } else {
                SetResearchOutputMode(1u, true);
            }
            return;
        }
        if (utf8.rfind("/stov_mode", 0) == 0 || utf8.rfind("stov_mode:", 0) == 0) {
            const bool on = (utf8.find("1") != std::string::npos ||
                             utf8.find("on") != std::string::npos ||
                             utf8.find("ON") != std::string::npos ||
                             utf8.find("true") != std::string::npos ||
                             utf8.find("TRUE") != std::string::npos);
            SetResearchStovMode(on, true);
            return;
        }
        if (utf8.rfind("/sim_stream_hz", 0) == 0 || utf8.rfind("sim_stream_hz:", 0) == 0) {
            const std::size_t sp = utf8.find_last_of("=:");
            if (sp != std::string::npos && sp + 1 < utf8.size()) {
                const uint32_t hz = (uint32_t)std::strtoul(utf8.substr(sp + 1).c_str(), nullptr, 10);
                const uint32_t bounded_hz =
                    research_basic_trial_enabled
                        ? std::min<uint32_t>(hz, ResearchBasicTrialMaxStreamHz())
                        : hz;
                SetResearchStreamHz(bounded_hz, true);
            }
            return;
        }
        if (utf8.rfind("/sim_lattice_edge", 0) == 0 || utf8.rfind("sim_lattice_edge:", 0) == 0) {
            const std::size_t sp = utf8.find_last_of("=:");
            if (sp != std::string::npos && sp + 1 < utf8.size()) {
                const uint32_t edge = (uint32_t)std::strtoul(utf8.substr(sp + 1).c_str(), nullptr, 10);
                const uint32_t bounded_edge =
                    research_basic_trial_enabled
                        ? std::min<uint32_t>(edge, ResearchBasicTrialMaxLatticeEdge())
                        : edge;
                SetResearchLatticeEdge(bounded_edge, true);
            }
            return;
        }
        if (utf8.rfind("/sim_param", 0) == 0 || utf8.rfind("sim_param:", 0) == 0) {
            const std::size_t eq = utf8.find('=');
            if (eq != std::string::npos && eq + 1 < utf8.size()) {
                std::string payload = utf8.substr(eq + 1);
                for (char& ch : payload) {
                    if (ch == ',' || ch == ';') ch = ' ';
                }
                std::istringstream iss(payload);
                float a = research_param_a;
                float f = research_param_f;
                float i = research_param_i;
                float v = research_param_v;
                std::string token;
                while (iss >> token) {
                    const std::size_t colon = token.find(':');
                    if (colon == std::string::npos) continue;
                    const std::string k = token.substr(0, colon);
                    const float val = (float)std::atof(token.substr(colon + 1).c_str());
                    if (k == "A" || k == "a") a = val;
                    else if (k == "F" || k == "f") f = val;
                    else if (k == "I" || k == "i") i = val;
                    else if (k == "V" || k == "v") v = val;
                }
                if (research_basic_trial_enabled &&
                    genesis::ge_has_research_runtime_guidance(research_archive)) {
                    a = (float)std::max((double)research_archive.pulse_window_min_quartet.A,
                                        std::min((double)research_archive.pulse_window_max_quartet.A, (double)a));
                    f = (float)std::max((double)research_archive.pulse_window_min_quartet.F,
                                        std::min((double)research_archive.pulse_window_max_quartet.F, (double)f));
                    i = (float)std::max((double)research_archive.pulse_window_min_quartet.I,
                                        std::min((double)research_archive.pulse_window_max_quartet.I, (double)i));
                    v = (float)std::max((double)research_archive.pulse_window_min_quartet.V,
                                        std::min((double)research_archive.pulse_window_max_quartet.V, (double)v));
                }
                SetResearchParams(a, f, i, v, true);
            }
            return;
        }
        sm.ui_submit_user_text_line(utf8);
    }

    static bool genesis_synthesize_voxelize_local(EwObjectStore& store,
                                                  const EwObjMesh& mesh,
                                                  uint64_t object_id_u64,
                                                  bool materials_calib_done,
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
        if (!genesis::ewmesh_voxelize_occupancy_u8(m, materials_calib_done, grid_x_u32, grid_y_u32, grid_z_u32, occ)) return false;

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
            if (!genesis_synthesize_voxelize_local(sm.object_store, m, object_id, sm.materials_calib_done, synth_g, synth_g, synth_g)) return false;
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

    // Photon-native compute pipelines (frequency -> color/audio stream)
    VkDescriptorSetLayout photon_visual_ds_layout = VK_NULL_HANDLE;
    VkPipelineLayout photon_visual_pipe_layout = VK_NULL_HANDLE;
    VkPipeline photon_visual_pipe = VK_NULL_HANDLE;
    VkDescriptorPool photon_visual_ds_pool = VK_NULL_HANDLE;
    VkDescriptorSet photon_visual_ds = VK_NULL_HANDLE;
    VkBuffer photon_visual_in_buf = VK_NULL_HANDLE;
    VkDeviceMemory photon_visual_in_mem = VK_NULL_HANDLE;
    void* photon_visual_in_mapped = nullptr;
    VkDeviceSize photon_visual_in_capacity = 0;
    VkBuffer photon_visual_out_buf = VK_NULL_HANDLE;
    VkDeviceMemory photon_visual_out_mem = VK_NULL_HANDLE;
    void* photon_visual_out_mapped = nullptr;
    VkDeviceSize photon_visual_out_capacity = 0;

    VkDescriptorSetLayout photon_audio_ds_layout = VK_NULL_HANDLE;
    VkPipelineLayout photon_audio_pipe_layout = VK_NULL_HANDLE;
    VkPipeline photon_audio_pipe = VK_NULL_HANDLE;
    VkDescriptorPool photon_audio_ds_pool = VK_NULL_HANDLE;
    VkDescriptorSet photon_audio_ds = VK_NULL_HANDLE;
    VkBuffer photon_audio_phase_buf = VK_NULL_HANDLE;
    VkDeviceMemory photon_audio_phase_mem = VK_NULL_HANDLE;
    void* photon_audio_phase_mapped = nullptr;
    VkDeviceSize photon_audio_phase_capacity = 0;
    VkBuffer photon_audio_oam_buf = VK_NULL_HANDLE;
    VkDeviceMemory photon_audio_oam_mem = VK_NULL_HANDLE;
    void* photon_audio_oam_mapped = nullptr;
    VkDeviceSize photon_audio_oam_capacity = 0;
    VkBuffer photon_audio_out_buf = VK_NULL_HANDLE;
    VkDeviceMemory photon_audio_out_mem = VK_NULL_HANDLE;
    void* photon_audio_out_mapped = nullptr;
    VkDeviceSize photon_audio_out_capacity = 0;
    uint32_t photon_audio_last_count_u32 = 0u;

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

            if (photon_visual_pipe) { vkDestroyPipeline(dev, photon_visual_pipe, nullptr); photon_visual_pipe = VK_NULL_HANDLE; }
            if (photon_visual_pipe_layout) { vkDestroyPipelineLayout(dev, photon_visual_pipe_layout, nullptr); photon_visual_pipe_layout = VK_NULL_HANDLE; }
            if (photon_visual_ds_layout) { vkDestroyDescriptorSetLayout(dev, photon_visual_ds_layout, nullptr); photon_visual_ds_layout = VK_NULL_HANDLE; }
            if (photon_visual_ds_pool) { vkDestroyDescriptorPool(dev, photon_visual_ds_pool, nullptr); photon_visual_ds_pool = VK_NULL_HANDLE; }
            if (photon_visual_in_buf) {
                if (photon_visual_in_mapped) { vkUnmapMemory(dev, photon_visual_in_mem); photon_visual_in_mapped = nullptr; }
                vkDestroyBuffer(dev, photon_visual_in_buf, nullptr);
                photon_visual_in_buf = VK_NULL_HANDLE;
            }
            if (photon_visual_in_mem) { vkFreeMemory(dev, photon_visual_in_mem, nullptr); photon_visual_in_mem = VK_NULL_HANDLE; }
            if (photon_visual_out_buf) {
                if (photon_visual_out_mapped) { vkUnmapMemory(dev, photon_visual_out_mem); photon_visual_out_mapped = nullptr; }
                vkDestroyBuffer(dev, photon_visual_out_buf, nullptr);
                photon_visual_out_buf = VK_NULL_HANDLE;
            }
            if (photon_visual_out_mem) { vkFreeMemory(dev, photon_visual_out_mem, nullptr); photon_visual_out_mem = VK_NULL_HANDLE; }

            if (photon_audio_pipe) { vkDestroyPipeline(dev, photon_audio_pipe, nullptr); photon_audio_pipe = VK_NULL_HANDLE; }
            if (photon_audio_pipe_layout) { vkDestroyPipelineLayout(dev, photon_audio_pipe_layout, nullptr); photon_audio_pipe_layout = VK_NULL_HANDLE; }
            if (photon_audio_ds_layout) { vkDestroyDescriptorSetLayout(dev, photon_audio_ds_layout, nullptr); photon_audio_ds_layout = VK_NULL_HANDLE; }
            if (photon_audio_ds_pool) { vkDestroyDescriptorPool(dev, photon_audio_ds_pool, nullptr); photon_audio_ds_pool = VK_NULL_HANDLE; }
            if (photon_audio_phase_buf) {
                if (photon_audio_phase_mapped) { vkUnmapMemory(dev, photon_audio_phase_mem); photon_audio_phase_mapped = nullptr; }
                vkDestroyBuffer(dev, photon_audio_phase_buf, nullptr);
                photon_audio_phase_buf = VK_NULL_HANDLE;
            }
            if (photon_audio_phase_mem) { vkFreeMemory(dev, photon_audio_phase_mem, nullptr); photon_audio_phase_mem = VK_NULL_HANDLE; }
            if (photon_audio_oam_buf) {
                if (photon_audio_oam_mapped) { vkUnmapMemory(dev, photon_audio_oam_mem); photon_audio_oam_mapped = nullptr; }
                vkDestroyBuffer(dev, photon_audio_oam_buf, nullptr);
                photon_audio_oam_buf = VK_NULL_HANDLE;
            }
            if (photon_audio_oam_mem) { vkFreeMemory(dev, photon_audio_oam_mem, nullptr); photon_audio_oam_mem = VK_NULL_HANDLE; }
            if (photon_audio_out_buf) {
                if (photon_audio_out_mapped) { vkUnmapMemory(dev, photon_audio_out_mem); photon_audio_out_mapped = nullptr; }
                vkDestroyBuffer(dev, photon_audio_out_buf, nullptr);
                photon_audio_out_buf = VK_NULL_HANDLE;
            }
            if (photon_audio_out_mem) { vkFreeMemory(dev, photon_audio_out_mem, nullptr); photon_audio_out_mem = VK_NULL_HANDLE; }

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

static bool ew_create_or_resize_host_ssbo(App::VkCtx& vk,
                                          VkBuffer& io_buf,
                                          VkDeviceMemory& io_mem,
                                          void*& io_mapped,
                                          VkDeviceSize& io_capacity,
                                          VkDeviceSize needed_bytes) {
    const VkDeviceSize min_bytes = 16 * 1024;
    if (needed_bytes < min_bytes) needed_bytes = min_bytes;
    if (io_buf && io_capacity >= needed_bytes) return true;

    if (io_buf) {
        if (io_mapped) {
            vkUnmapMemory(vk.dev, io_mem);
            io_mapped = nullptr;
        }
        vkDestroyBuffer(vk.dev, io_buf, nullptr);
        vkFreeMemory(vk.dev, io_mem, nullptr);
        io_buf = VK_NULL_HANDLE;
        io_mem = VK_NULL_HANDLE;
        io_capacity = 0;
    }

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = needed_bytes;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_check(vkCreateBuffer(vk.dev, &bi, nullptr, &io_buf), "vkCreateBuffer(host_ssbo)");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(vk.dev, io_buf, &req);
    const uint32_t mt = ew_find_memory_type(vk.phys, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) return false;

    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    vk_check(vkAllocateMemory(vk.dev, &ai, nullptr, &io_mem), "vkAllocateMemory(host_ssbo)");
    vk_check(vkBindBufferMemory(vk.dev, io_buf, io_mem, 0), "vkBindBufferMemory(host_ssbo)");
    vk_check(vkMapMemory(vk.dev, io_mem, 0, req.size, 0, &io_mapped), "vkMapMemory(host_ssbo)");
    io_capacity = needed_bytes;
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

static VkPhysicalDevice pick_phys(VkInstance inst, VkSurfaceKHR surf) {
    uint32_t n = 0;
    vk_check(vkEnumeratePhysicalDevices(inst, &n, nullptr), "vkEnumeratePhysicalDevices(count)");
    std::vector<VkPhysicalDevice> devs(n);
    vk_check(vkEnumeratePhysicalDevices(inst, &n, devs.data()), "vkEnumeratePhysicalDevices");
    if (n == 0) return VK_NULL_HANDLE;

    auto has_graphics_present_queue = [&](VkPhysicalDevice pd) -> bool {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
        if (qn == 0) return false;
        std::vector<VkQueueFamilyProperties> qprops(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qprops.data());
        for (uint32_t qi = 0; qi < qn; ++qi) {
            if ((qprops[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) continue;
            VkBool32 present = VK_FALSE;
            VkResult r = vkGetPhysicalDeviceSurfaceSupportKHR(pd, qi, surf, &present);
            if (r == VK_SUCCESS && present) return true;
        }
        return false;
    };

    int best_score = -1;
    VkPhysicalDevice best = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < n; ++i) {
        VkPhysicalDevice pd = devs[i];
        if (!has_graphics_present_queue(pd)) continue;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pd, &props);

        int score = 0;
        switch (props.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score += 10000; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score += 2000; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score += 1000; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU: score += 100; break;
            default: break;
        }
        if (VK_API_VERSION_MAJOR(props.apiVersion) >= 1 && VK_API_VERSION_MINOR(props.apiVersion) >= 3) {
            score += 250;
        }
        if (props.vendorID == 0x10DE) score += 120; // NVIDIA
        if (props.vendorID == 0x1002 || props.vendorID == 0x1022) score += 100; // AMD
        if (props.vendorID == 0x8086) score += 10; // Intel

        if (score > best_score) {
            best_score = score;
            best = pd;
        }
    }

    if (best != VK_NULL_HANDLE) return best;
    return devs[0];
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
    //  binding 3 = photon-native color SSBO (compute output)
    VkDescriptorSetLayoutBinding bindings[4]{};
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

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = 4;
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
    ps[0].descriptorCount = 3;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[1].descriptorCount = 1;
    ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps[2].descriptorCount = 3;

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

    // Ensure photon compute stream buffers exist before binding the graphics descriptor set.
    // Colors are one vec4 per instance (runtime-resizable).
    (void)ew_create_or_resize_host_ssbo(vk, vk.photon_visual_out_buf, vk.photon_visual_out_mem,
                                        vk.photon_visual_out_mapped, vk.photon_visual_out_capacity,
                                        65536u * sizeof(float) * 4u);

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

    VkDescriptorBufferInfo bi3{};
    bi3.buffer = vk.photon_visual_out_buf;
    bi3.offset = 0;
    bi3.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet w3{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w3.dstSet = vk.ds;
    w3.dstBinding = 3;
    w3.descriptorCount = 1;
    w3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w3.pBufferInfo = &bi3;

    VkWriteDescriptorSet writes[3] = {w1, w2, w3};
    vkUpdateDescriptorSets(vk.dev, 3, writes, 0, nullptr);

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

    // -----------------------------------------------------------------
    // Photon-native compute pipelines (visual + audio)
    // -----------------------------------------------------------------
    {
        // Visual stream buffers.
        (void)ew_create_or_resize_host_ssbo(vk, vk.photon_visual_in_buf, vk.photon_visual_in_mem,
                                            vk.photon_visual_in_mapped, vk.photon_visual_in_capacity,
                                            65536u * sizeof(float) * 4u);
        (void)ew_create_or_resize_host_ssbo(vk, vk.photon_visual_out_buf, vk.photon_visual_out_mem,
                                            vk.photon_visual_out_mapped, vk.photon_visual_out_capacity,
                                            65536u * sizeof(float) * 4u);

        if (!vk.photon_visual_ds_layout) {
            VkDescriptorSetLayoutBinding b[2]{};
            b[0].binding = 0;
            b[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[0].descriptorCount = 1;
            b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            b[1].binding = 1;
            b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[1].descriptorCount = 1;
            b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutCreateInfo dsl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            dsl.bindingCount = 2;
            dsl.pBindings = b;
            vk_check(vkCreateDescriptorSetLayout(vk.dev, &dsl, nullptr, &vk.photon_visual_ds_layout),
                     "vkCreateDescriptorSetLayout(photon_visual)");

            VkPushConstantRange pcr{};
            pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pcr.offset = 0;
            pcr.size = sizeof(uint32_t);

            VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pl.setLayoutCount = 1;
            pl.pSetLayouts = &vk.photon_visual_ds_layout;
            pl.pushConstantRangeCount = 1;
            pl.pPushConstantRanges = &pcr;
            vk_check(vkCreatePipelineLayout(vk.dev, &pl, nullptr, &vk.photon_visual_pipe_layout),
                     "vkCreatePipelineLayout(photon_visual)");

            VkDescriptorPoolSize ps{};
            ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ps.descriptorCount = 2;
            VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dp.maxSets = 1;
            dp.poolSizeCount = 1;
            dp.pPoolSizes = &ps;
            vk_check(vkCreateDescriptorPool(vk.dev, &dp, nullptr, &vk.photon_visual_ds_pool),
                     "vkCreateDescriptorPool(photon_visual)");

            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = vk.photon_visual_ds_pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &vk.photon_visual_ds_layout;
            vk_check(vkAllocateDescriptorSets(vk.dev, &ai, &vk.photon_visual_ds),
                     "vkAllocateDescriptorSets(photon_visual)");
        }

        VkDescriptorBufferInfo vis_in{};
        vis_in.buffer = vk.photon_visual_in_buf;
        vis_in.offset = 0;
        vis_in.range = VK_WHOLE_SIZE;
        VkDescriptorBufferInfo vis_out{};
        vis_out.buffer = vk.photon_visual_out_buf;
        vis_out.offset = 0;
        vis_out.range = VK_WHOLE_SIZE;
        VkWriteDescriptorSet vw[2]{};
        vw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        vw[0].dstSet = vk.photon_visual_ds;
        vw[0].dstBinding = 0;
        vw[0].descriptorCount = 1;
        vw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vw[0].pBufferInfo = &vis_in;
        vw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        vw[1].dstSet = vk.photon_visual_ds;
        vw[1].dstBinding = 1;
        vw[1].descriptorCount = 1;
        vw[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vw[1].pBufferInfo = &vis_out;
        vkUpdateDescriptorSets(vk.dev, 2, vw, 0, nullptr);

        if (!vk.photon_visual_pipe) {
            std::wstring vis_path = utf8_to_wide(std::string(EW_SHADER_OUT_DIR) + "\\vkcompute_photon_visual.comp.spv");
            auto vis_spv = ew_read_spv_u32(vis_path);
            if (!vis_spv.empty()) {
                VkShaderModule sm = ew_make_shader(vk.dev, vis_spv);
                VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                cpi.layout = vk.photon_visual_pipe_layout;
                cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                cpi.stage.module = sm;
                cpi.stage.pName = "main";
                vk_check(vkCreateComputePipelines(vk.dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &vk.photon_visual_pipe),
                         "vkCreateComputePipelines(photon_visual)");
                vkDestroyShaderModule(vk.dev, sm, nullptr);
            }
        }
    }

    {
        // Audio stream buffers (phase/oam -> 4ch waveform).
        (void)ew_create_or_resize_host_ssbo(vk, vk.photon_audio_phase_buf, vk.photon_audio_phase_mem,
                                            vk.photon_audio_phase_mapped, vk.photon_audio_phase_capacity,
                                            4096u * sizeof(float));
        (void)ew_create_or_resize_host_ssbo(vk, vk.photon_audio_oam_buf, vk.photon_audio_oam_mem,
                                            vk.photon_audio_oam_mapped, vk.photon_audio_oam_capacity,
                                            4096u * sizeof(float));
        (void)ew_create_or_resize_host_ssbo(vk, vk.photon_audio_out_buf, vk.photon_audio_out_mem,
                                            vk.photon_audio_out_mapped, vk.photon_audio_out_capacity,
                                            4096u * sizeof(float) * 4u);

        if (!vk.photon_audio_ds_layout) {
            VkDescriptorSetLayoutBinding b[3]{};
            b[0].binding = 0;
            b[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[0].descriptorCount = 1;
            b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            b[1].binding = 1;
            b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[1].descriptorCount = 1;
            b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            b[2].binding = 2;
            b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b[2].descriptorCount = 1;
            b[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutCreateInfo dsl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            dsl.bindingCount = 3;
            dsl.pBindings = b;
            vk_check(vkCreateDescriptorSetLayout(vk.dev, &dsl, nullptr, &vk.photon_audio_ds_layout),
                     "vkCreateDescriptorSetLayout(photon_audio)");

            VkPushConstantRange pcr{};
            pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pcr.offset = 0;
            pcr.size = sizeof(uint32_t);

            VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pl.setLayoutCount = 1;
            pl.pSetLayouts = &vk.photon_audio_ds_layout;
            pl.pushConstantRangeCount = 1;
            pl.pPushConstantRanges = &pcr;
            vk_check(vkCreatePipelineLayout(vk.dev, &pl, nullptr, &vk.photon_audio_pipe_layout),
                     "vkCreatePipelineLayout(photon_audio)");

            VkDescriptorPoolSize ps{};
            ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ps.descriptorCount = 3;
            VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dp.maxSets = 1;
            dp.poolSizeCount = 1;
            dp.pPoolSizes = &ps;
            vk_check(vkCreateDescriptorPool(vk.dev, &dp, nullptr, &vk.photon_audio_ds_pool),
                     "vkCreateDescriptorPool(photon_audio)");

            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = vk.photon_audio_ds_pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &vk.photon_audio_ds_layout;
            vk_check(vkAllocateDescriptorSets(vk.dev, &ai, &vk.photon_audio_ds),
                     "vkAllocateDescriptorSets(photon_audio)");
        }

        VkDescriptorBufferInfo ab0{};
        ab0.buffer = vk.photon_audio_phase_buf;
        ab0.offset = 0;
        ab0.range = VK_WHOLE_SIZE;
        VkDescriptorBufferInfo ab1{};
        ab1.buffer = vk.photon_audio_oam_buf;
        ab1.offset = 0;
        ab1.range = VK_WHOLE_SIZE;
        VkDescriptorBufferInfo ab2{};
        ab2.buffer = vk.photon_audio_out_buf;
        ab2.offset = 0;
        ab2.range = VK_WHOLE_SIZE;
        VkWriteDescriptorSet aw[3]{};
        aw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        aw[0].dstSet = vk.photon_audio_ds;
        aw[0].dstBinding = 0;
        aw[0].descriptorCount = 1;
        aw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        aw[0].pBufferInfo = &ab0;
        aw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        aw[1].dstSet = vk.photon_audio_ds;
        aw[1].dstBinding = 1;
        aw[1].descriptorCount = 1;
        aw[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        aw[1].pBufferInfo = &ab1;
        aw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        aw[2].dstSet = vk.photon_audio_ds;
        aw[2].dstBinding = 2;
        aw[2].descriptorCount = 1;
        aw[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        aw[2].pBufferInfo = &ab2;
        vkUpdateDescriptorSets(vk.dev, 3, aw, 0, nullptr);

        if (!vk.photon_audio_pipe) {
            std::wstring audio_path = utf8_to_wide(std::string(EW_SHADER_OUT_DIR) + "\\vkcompute_photon_audio.comp.spv");
            auto audio_spv = ew_read_spv_u32(audio_path);
            if (!audio_spv.empty()) {
                VkShaderModule sm = ew_make_shader(vk.dev, audio_spv);
                VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                cpi.layout = vk.photon_audio_pipe_layout;
                cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                cpi.stage.module = sm;
                cpi.stage.pName = "main";
                vk_check(vkCreateComputePipelines(vk.dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &vk.photon_audio_pipe),
                         "vkCreateComputePipelines(photon_audio)");
                vkDestroyShaderModule(vk.dev, sm, nullptr);
            }
        }
    }
}

App::App(const AppConfig& cfg) : cfg_(cfg) {
    ew_camera_init(cam_);
    std::memset(&input_, 0, sizeof(input_));
    stov_mode_ = cfg.stov_mode;
    stov_data_log_path_utf8_ = cfg.stov_data_log_path_utf8;
    stov_audio_out_path_utf8_ = cfg.stov_audio_out_path_utf8;
    const char* v = std::getenv("EW_IMMERSION");
    if (v && (v[0]=='1' || v[0]=='y' || v[0]=='Y' || v[0]=='t' || v[0]=='T')) {
        immersion_mode_ = true;
    }
}

void App::SetSTOVMode(bool enable) {
    stov_mode_ = enable;
    if (scene_) scene_->SetResearchStovMode(enable, false);
    AppendOutputUtf8(enable ? "STOV MODE: ON" : "STOV MODE: OFF");
}

void App::LogSTOVData(const std::vector<float>& phase, const std::vector<float>& oam, const std::vector<int>& winding) {
    if (stov_data_log_path_utf8_.empty()) return;
    std::ofstream out(stov_data_log_path_utf8_, std::ios::out | std::ios::app);
    if (!out.good()) return;
    for (size_t i = 0; i < phase.size(); ++i) {
        out << i << ',' << phase[i] << ',' << oam[i] << ',' << winding[i] << '\n';
    }
}

void App::OutputSTOVAudio(const std::vector<float>& phase, const std::vector<float>& oam, const std::vector<int>& winding) {
    if (phase.empty() || oam.empty()) return;
    const size_t n = std::min<size_t>(phase.size(), oam.size());
    if (n == 0u) return;

    std::vector<int16_t> pcm;
    pcm.reserve(n * 4u);
    for (size_t i = 0u; i < n; ++i) {
        const float p = phase[i];
        const float c = oam[i];
        const float dry = std::sin(p);
        const float wet = std::sin(p + c * 1.5707963f);
        const float spread = std::max(0.0f, std::min(1.0f, std::fabs(c)));
        const float s[4] = {
            dry,
            dry * (1.0f - spread) + wet * spread,
            dry * (1.0f - spread) - wet * spread,
            wet
        };
        for (int ch = 0; ch < 4; ++ch) {
            const float v = std::max(-1.0f, std::min(1.0f, s[ch]));
            pcm.push_back((int16_t)std::lround(v * 32767.0f));
        }
    }

    if (scene_ && !pcm.empty()) {
        scene_->sm.inject_audio_pcm16(pcm.data(), (int)n, 4);
    }

    if (!stov_audio_out_path_utf8_.empty()) {
        std::ofstream out(stov_audio_out_path_utf8_, std::ios::out | std::ios::trunc);
        if (out.good()) {
            out << "idx,c0,c1,c2,c3,w\n";
            for (size_t i = 0u; i < n; ++i) {
                const int w = (i < winding.size()) ? winding[i] : 0;
                out << i << ','
                    << pcm[i * 4u + 0u] << ','
                    << pcm[i * 4u + 1u] << ','
                    << pcm[i * 4u + 2u] << ','
                    << pcm[i * 4u + 3u] << ','
                    << w << '\n';
            }
        }
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

    hwnd_bootstrap_ = CreateWindowW(L"BUTTON", L"Bootstrap Engine",
                               WS_CHILD | WS_VISIBLE,
                               220, 40, 190, 26,
                               hwnd_panel_, (HMENU)2006, GetModuleHandleW(nullptr), nullptr);

    hwnd_objlist_ = CreateWindowW(L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                                  10, 76, 400, 180,
                                  hwnd_panel_, (HMENU)2004, GetModuleHandleW(nullptr), nullptr);
    // Transform controls (position in meters). UI emits ObjectSetTransform packets.
    CreateWindowW(L"STATIC", L"Pos X", WS_CHILD | WS_VISIBLE, 10, 266, 44, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_posx_ = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 58, 262, 90, 24, hwnd_panel_, (HMENU)2010, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", L"Y", WS_CHILD | WS_VISIBLE, 154, 266, 14, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_posy_ = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 172, 262, 90, 24, hwnd_panel_, (HMENU)2011, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", L"Z", WS_CHILD | WS_VISIBLE, 268, 266, 14, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_posz_ = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 286, 262, 90, 24, hwnd_panel_, (HMENU)2012, GetModuleHandleW(nullptr), nullptr);
    hwnd_apply_xform_ = CreateWindowW(L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE, 10, 292, 80, 26, hwnd_panel_, (HMENU)2013, GetModuleHandleW(nullptr), nullptr);

    // Gizmo + snap controls.
    hwnd_mode_translate_ = CreateWindowW(L"BUTTON", L"Translate", WS_CHILD | WS_VISIBLE, 100, 292, 90, 26, hwnd_panel_, (HMENU)2020, GetModuleHandleW(nullptr), nullptr);
    hwnd_mode_rotate_    = CreateWindowW(L"BUTTON", L"Rotate", WS_CHILD | WS_VISIBLE, 196, 292, 70, 26, hwnd_panel_, (HMENU)2021, GetModuleHandleW(nullptr), nullptr);
    hwnd_frame_sel_      = CreateWindowW(L"BUTTON", L"Frame", WS_CHILD | WS_VISIBLE, 272, 292, 60, 26, hwnd_panel_, (HMENU)2022, GetModuleHandleW(nullptr), nullptr);
// Axis constraint + undo/redo.
hwnd_axis_none_ = CreateWindowW(L"BUTTON", L"Axis:None", WS_CHILD | WS_VISIBLE, 10, 350, 80, 24, hwnd_panel_, (HMENU)2026, GetModuleHandleW(nullptr), nullptr);
hwnd_axis_x_    = CreateWindowW(L"BUTTON", L"X", WS_CHILD | WS_VISIBLE, 96, 350, 28, 24, hwnd_panel_, (HMENU)2027, GetModuleHandleW(nullptr), nullptr);
hwnd_axis_y_    = CreateWindowW(L"BUTTON", L"Y", WS_CHILD | WS_VISIBLE, 128, 350, 28, 24, hwnd_panel_, (HMENU)2028, GetModuleHandleW(nullptr), nullptr);
hwnd_axis_z_    = CreateWindowW(L"BUTTON", L"Z", WS_CHILD | WS_VISIBLE, 160, 350, 28, 24, hwnd_panel_, (HMENU)2029, GetModuleHandleW(nullptr), nullptr);
hwnd_undo_      = CreateWindowW(L"BUTTON", L"Undo", WS_CHILD | WS_VISIBLE, 196, 350, 60, 24, hwnd_panel_, (HMENU)2030, GetModuleHandleW(nullptr), nullptr);
hwnd_redo_      = CreateWindowW(L"BUTTON", L"Redo", WS_CHILD | WS_VISIBLE, 262, 350, 60, 24, hwnd_panel_, (HMENU)2031, GetModuleHandleW(nullptr), nullptr);

    hwnd_snap_enable_ = CreateWindowW(L"BUTTON", L"Snap", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 338, 296, 70, 22, hwnd_panel_, (HMENU)2023, GetModuleHandleW(nullptr), nullptr);
    // Grid/angle fields
    CreateWindowW(L"STATIC", L"Grid(m)", WS_CHILD | WS_VISIBLE, 10, 322, 56, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_grid_step_ = CreateWindowW(L"EDIT", L"1.0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 68, 318, 72, 24, hwnd_panel_, (HMENU)2024, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", L"Angle(deg)", WS_CHILD | WS_VISIBLE, 150, 322, 72, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_angle_step_ = CreateWindowW(L"EDIT", L"15", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 226, 318, 60, 24, hwnd_panel_, (HMENU)2025, GetModuleHandleW(nullptr), nullptr);

    // Photon simulation controls.
    CreateWindowW(L"STATIC", L"Photon Sim", WS_CHILD | WS_VISIBLE, 10, 378, 90, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_mode_freq_ = CreateWindowW(L"BUTTON", L"Frequency", WS_CHILD | WS_VISIBLE, 10, 398, 86, 24, hwnd_panel_, (HMENU)2040, GetModuleHandleW(nullptr), nullptr);
    hwnd_mode_vector_ = CreateWindowW(L"BUTTON", L"Vector", WS_CHILD | WS_VISIBLE, 102, 398, 70, 24, hwnd_panel_, (HMENU)2041, GetModuleHandleW(nullptr), nullptr);
    hwnd_stov_toggle_ = CreateWindowW(L"BUTTON", L"STOV", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 178, 398, 60, 24, hwnd_panel_, (HMENU)2042, GetModuleHandleW(nullptr), nullptr);

    CreateWindowW(L"STATIC", L"A", WS_CHILD | WS_VISIBLE, 10, 430, 14, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_param_a_ = CreateWindowW(L"EDIT", L"0.19", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 24, 426, 64, 24, hwnd_panel_, (HMENU)2043, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", L"F", WS_CHILD | WS_VISIBLE, 92, 430, 14, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_param_f_ = CreateWindowW(L"EDIT", L"0.245", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 106, 426, 64, 24, hwnd_panel_, (HMENU)2044, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", L"I", WS_CHILD | WS_VISIBLE, 174, 430, 14, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_param_i_ = CreateWindowW(L"EDIT", L"0.35", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 188, 426, 64, 24, hwnd_panel_, (HMENU)2045, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", L"V", WS_CHILD | WS_VISIBLE, 256, 430, 14, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_param_v_ = CreateWindowW(L"EDIT", L"0.35", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 270, 426, 64, 24, hwnd_panel_, (HMENU)2046, GetModuleHandleW(nullptr), nullptr);
    hwnd_apply_sim_params_ = CreateWindowW(L"BUTTON", L"Apply Params", WS_CHILD | WS_VISIBLE, 340, 426, 70, 24, hwnd_panel_, (HMENU)2047, GetModuleHandleW(nullptr), nullptr);

    CreateWindowW(L"STATIC", L"Lattice", WS_CHILD | WS_VISIBLE, 10, 460, 44, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_lattice_edge_ = CreateWindowW(L"EDIT", L"256", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 56, 456, 72, 24, hwnd_panel_, (HMENU)2048, GetModuleHandleW(nullptr), nullptr);
    hwnd_apply_lattice_edge_ = CreateWindowW(L"BUTTON", L"Apply Edge", WS_CHILD | WS_VISIBLE, 134, 456, 76, 24, hwnd_panel_, (HMENU)2049, GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", L"StreamHz", WS_CHILD | WS_VISIBLE, 218, 460, 54, 18, hwnd_panel_, nullptr, GetModuleHandleW(nullptr), nullptr);
    hwnd_stream_hz_ = CreateWindowW(L"EDIT", L"360", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, 274, 456, 56, 24, hwnd_panel_, (HMENU)2050, GetModuleHandleW(nullptr), nullptr);
    hwnd_apply_stream_hz_ = CreateWindowW(L"BUTTON", L"Apply Hz", WS_CHILD | WS_VISIBLE, 336, 456, 74, 24, hwnd_panel_, (HMENU)2051, GetModuleHandleW(nullptr), nullptr);

    hwnd_output_ = CreateWindowW(L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                                 10, 490, 400, client_h_ - 500,
                                 hwnd_panel_, (HMENU)2005, GetModuleHandleW(nullptr), nullptr);
    if (stov_mode_) {
        SendMessageW(hwnd_stov_toggle_, BM_SETCHECK, BST_CHECKED, 0);
    }
    LayoutChildren(client_w_, client_h_);
}

void App::LayoutChildren(int w, int h) {
    int panel_w = 420;
    MoveWindow(hwnd_viewport_, 0, 0, w - panel_w, h, TRUE);
    MoveWindow(hwnd_panel_, w - panel_w, 0, panel_w, h, TRUE);

    MoveWindow(hwnd_output_, 10, 490, panel_w - 20, std::max(80, h - 500), TRUE);
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
    vk_->phys = pick_phys(vk_->instance, vk_->surface);
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
    const int kMaxTicksPerFrame = 12;
    int steps = 0;
    while (acc >= tick_dt && steps < kMaxTicksPerFrame) {
        scene_->Tick();
        acc -= tick_dt;
        steps++;
    }
    // If we fell behind, drop excess accumulated time deterministically.
    if (acc > tick_dt * (float)kMaxTicksPerFrame) acc = 0.0f;

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

    // Camera rig controls:
    //   Alt+LMB orbit, Alt+MMB pan
    //   wheel dolly
    //   Alt+wheel photon lattice scale
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
            if (input_.alt && scene_) {
                scene_->ScaleResearchLatticeByStepDelta(steps, true);
            } else {
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
    scene_->instances.reserve(scene_->objects.size() + 512u);

    auto ew_clamp_i32 = [](int64_t v, int32_t lo, int32_t hi) -> int32_t {
        if (v < (int64_t)lo) return lo;
        if (v > (int64_t)hi) return hi;
        return (int32_t)v;
    };

    auto append_research_instances = [&]() {
        if (!genesis::ge_has_research_realtime_outputs(scene_->research_archive)) return;
        if (scene_->research_points.empty()) return;
        const float lattice_constant_m =
            (float)std::max(scene_->research_archive.silicon_lattice_constant_m, 1.0e-12);
        auto normalize_axis = [&](float v) -> float {
            return (std::fabs(v) < 1.0e-6f) ? (v / lattice_constant_m) : v;
        };

        for (const genesis::GeResearchParticleVizPoint& p : scene_->research_points) {
            const float wx = normalize_axis(p.x) * 0.12f + p.observer_shift * 0.03f;
            const float wy = normalize_axis(p.y) * 0.12f + p.phase_bias * 0.02f;
            const float wz = normalize_axis(p.z) * 0.12f - p.trail_age * 0.04f;
            float cx = 0.0f;
            float cy = 0.0f;
            float cz = 0.0f;
            camera_space_pos(wx, wy, wz, cx, cy, cz);

            EwRenderInstance inst{};
            inst.object_id_u64 = 0u;
            inst.anchor_id_u32 = 0u;
            inst.kind_u32 = p.render_kind_u32;
            inst.rel_pos_q16_16[0] = (int32_t)std::llround((double)cx * 65536.0);
            inst.rel_pos_q16_16[1] = (int32_t)std::llround((double)cy * 65536.0);
            inst.rel_pos_q16_16[2] = (int32_t)std::llround((double)cz * 65536.0);
            inst.rel_pos_q16_16[3] = 0;
            inst.radius_q16_16 = (int32_t)std::llround((double)p.radius_m * 65536.0);
            inst.emissive_q16_16 = (int32_t)std::llround((double)p.emissive * 65536.0);
            inst.atmosphere_thickness_q16_16 = 0;
            inst.lod_bias_q16_16 = 0;
            inst.clarity_q16_16 = (int32_t)std::llround((1.0 - 0.35 * (double)p.trail_age) * 65536.0);
            inst.albedo_rgba8 = p.rgba8;
            inst.atmosphere_rgba8 = 0;
            inst.carrier_x_u32 = (uint32_t)ew_clamp_i32((int64_t)std::llround((double)p.temporal_coupling * 65536.0),
                                                        0, 65536);
            inst.carrier_y_u32 = (uint32_t)(int32_t)ew_clamp_i32((int64_t)std::llround((double)p.phase_bias * 65536.0),
                                                                 -65536, 65536);
            const uint32_t harm_q15 = (uint32_t)ew_clamp_i32((int64_t)std::llround((double)p.amplitude * 32768.0),
                                                             0, 32768);
            const uint32_t flux_q15 = (uint32_t)ew_clamp_i32((int64_t)std::llround((double)p.curvature_n * 32768.0),
                                                             0, 32768);
            inst.carrier_z_u32 = (flux_q15 << 16u) | (harm_q15 & 65535u);
            inst.tick_u64 = scene_->sm.canonical_tick;
            scene_->instances.push_back(inst);
        }
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

    append_research_instances();

    // UI output
    std::string line;
    int guard = 64;
    while (guard-- > 0 && scene_->PopUiLine(line)) {
        AppendOutputUtf8(line);
    }

    if (scene_) {
        stov_mode_ = scene_->research_stov_mode;
    }
    if (scene_ && scene_->research_stov_mode && !scene_->research_points.empty()) {
        std::vector<float> phase;
        std::vector<float> oam;
        std::vector<int> winding;
        const size_t n = std::min<size_t>(scene_->research_points.size(), 4096u);
        phase.reserve(n);
        oam.reserve(n);
        winding.reserve(n);
        for (size_t i = 0u; i < n; ++i) {
            const auto& p = scene_->research_points[i];
            const float ph = p.phase_bias * 3.14159265f;
            const float oa = p.curvature_n * ((p.phase_bias >= 0.0f) ? 1.0f : -1.0f);
            const int w = (oa > 0.35f) ? 1 : ((oa < -0.35f) ? -1 : 0);
            phase.push_back(ph);
            oam.push_back(oa);
            winding.push_back(w);
        }
        LogSTOVData(phase, oam, winding);
        OutputSTOVAudio(phase, oam, winding);
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
    if (scene_ && scene_->research_use_compute_audio && vk_->photon_audio_out_mapped && vk_->photon_audio_last_count_u32 > 0u) {
        const uint32_t n = std::min<uint32_t>(vk_->photon_audio_last_count_u32, 4096u);
        const float* out_f32 = reinterpret_cast<const float*>(vk_->photon_audio_out_mapped);
        std::vector<int16_t> pcm;
        pcm.reserve((size_t)n * 4u);
        for (uint32_t i = 0u; i < n; ++i) {
            const size_t k = (size_t)i * 4u;
            for (int ch = 0; ch < 4; ++ch) {
                const float v = std::max(-1.0f, std::min(1.0f, out_f32[k + (size_t)ch]));
                pcm.push_back((int16_t)std::lround(v * 32767.0f));
            }
        }
        if (!pcm.empty()) {
            scene_->sm.inject_audio_pcm16(pcm.data(), (int)n, 4);
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
            EwRenderAssistPacket ra{};
            const bool have_ra = ew_runtime_get_render_assist_packet(&scene_->sm, &ra);

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
                    rel_q16_16[0] = o.pos_q16_16[0];
                    rel_q16_16[1] = o.pos_q16_16[1];
                    rel_q16_16[2] = o.pos_q16_16[2];
                }

                const float cx = (float)rel_q16_16[0] / 65536.0f;
                const float cy = (float)rel_q16_16[1] / 65536.0f;
                const float cz = (float)rel_q16_16[2] / 65536.0f;

                // XR instance LOD/clarity MUST NOT be computed from local app camera state.
                // Use substrate-derived RenderAssistPacket coefficients and only simple
                // fixed-point multiply-add + clamps here (no sqrt/fabs focus-band math).
                // Distance is evaluated in squared-distance domain (m^2) to avoid sqrt.
                const double dist2_m2 = (double)cx*(double)cx + (double)cy*(double)cy + (double)cz*(double)cz;
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

        // Stream per-instance frequency state into photon compute input.
        const uint32_t inst_n_u32 = (uint32_t)scene_->instances.size();
        const VkDeviceSize photon_bytes = std::max<VkDeviceSize>((VkDeviceSize)inst_n_u32 * sizeof(float) * 4u,
                                                                 (VkDeviceSize)sizeof(float) * 4u);
        if (ew_create_or_resize_host_ssbo(*vk_, vk_->photon_visual_in_buf, vk_->photon_visual_in_mem,
                                          vk_->photon_visual_in_mapped, vk_->photon_visual_in_capacity,
                                          photon_bytes) &&
            ew_create_or_resize_host_ssbo(*vk_, vk_->photon_visual_out_buf, vk_->photon_visual_out_mem,
                                          vk_->photon_visual_out_mapped, vk_->photon_visual_out_capacity,
                                          photon_bytes)) {
            float* in = reinterpret_cast<float*>(vk_->photon_visual_in_mapped);
            for (uint32_t i = 0u; i < inst_n_u32; ++i) {
                const EwRenderInstance& inst = scene_->instances[i];
                const float phase = (float)((int32_t)inst.carrier_y_u32) / 65536.0f;
                const float amp = std::max(0.0f, std::min(1.0f, (float)(inst.carrier_z_u32 & 65535u) / 32768.0f));
                const float oam = std::max(-1.0f, std::min(1.0f, (float)((int32_t)inst.carrier_y_u32) / 65536.0f));
                const float coupling = std::max(0.0f, std::min(1.0f, (float)((int32_t)inst.carrier_x_u32) / 65536.0f));
                const size_t k = (size_t)i * 4u;
                in[k + 0u] = phase;
                in[k + 1u] = amp;
                in[k + 2u] = oam;
                in[k + 3u] = coupling;
            }

            VkDescriptorBufferInfo vis_in{};
            vis_in.buffer = vk_->photon_visual_in_buf;
            vis_in.offset = 0;
            vis_in.range = VK_WHOLE_SIZE;
            VkDescriptorBufferInfo vis_out{};
            vis_out.buffer = vk_->photon_visual_out_buf;
            vis_out.offset = 0;
            vis_out.range = VK_WHOLE_SIZE;
            VkWriteDescriptorSet vw[2]{};
            vw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            vw[0].dstSet = vk_->photon_visual_ds;
            vw[0].dstBinding = 0;
            vw[0].descriptorCount = 1;
            vw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            vw[0].pBufferInfo = &vis_in;
            vw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            vw[1].dstSet = vk_->photon_visual_ds;
            vw[1].dstBinding = 1;
            vw[1].descriptorCount = 1;
            vw[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            vw[1].pBufferInfo = &vis_out;
            vkUpdateDescriptorSets(vk_->dev, 2, vw, 0, nullptr);

            VkDescriptorBufferInfo color_ssbo{};
            color_ssbo.buffer = vk_->photon_visual_out_buf;
            color_ssbo.offset = 0;
            color_ssbo.range = VK_WHOLE_SIZE;
            VkWriteDescriptorSet color_w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            color_w.dstSet = vk_->ds;
            color_w.dstBinding = 3;
            color_w.descriptorCount = 1;
            color_w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            color_w.pBufferInfo = &color_ssbo;
            vkUpdateDescriptorSets(vk_->dev, 1, &color_w, 0, nullptr);
        }

        // Prepare photon audio compute inputs from research points (phase + OAM/curl).
        const uint32_t audio_n_u32 =
            (uint32_t)std::max<size_t>(64u, std::min<size_t>(scene_->research_points.size(), 4096u));
        const VkDeviceSize audio_scalar_bytes = std::max<VkDeviceSize>((VkDeviceSize)audio_n_u32 * sizeof(float),
                                                                       (VkDeviceSize)sizeof(float));
        const VkDeviceSize audio_vec4_bytes = std::max<VkDeviceSize>((VkDeviceSize)audio_n_u32 * sizeof(float) * 4u,
                                                                     (VkDeviceSize)sizeof(float) * 4u);
        if (ew_create_or_resize_host_ssbo(*vk_, vk_->photon_audio_phase_buf, vk_->photon_audio_phase_mem,
                                          vk_->photon_audio_phase_mapped, vk_->photon_audio_phase_capacity,
                                          audio_scalar_bytes) &&
            ew_create_or_resize_host_ssbo(*vk_, vk_->photon_audio_oam_buf, vk_->photon_audio_oam_mem,
                                          vk_->photon_audio_oam_mapped, vk_->photon_audio_oam_capacity,
                                          audio_scalar_bytes) &&
            ew_create_or_resize_host_ssbo(*vk_, vk_->photon_audio_out_buf, vk_->photon_audio_out_mem,
                                          vk_->photon_audio_out_mapped, vk_->photon_audio_out_capacity,
                                          audio_vec4_bytes)) {
            float* phase = reinterpret_cast<float*>(vk_->photon_audio_phase_mapped);
            float* oam = reinterpret_cast<float*>(vk_->photon_audio_oam_mapped);
            for (uint32_t i = 0u; i < audio_n_u32; ++i) {
                const size_t idx = scene_->research_points.empty()
                                     ? 0u
                                     : (size_t)(i % (uint32_t)scene_->research_points.size());
                const genesis::GeResearchParticleVizPoint& p =
                    scene_->research_points.empty()
                        ? genesis::GeResearchParticleVizPoint{}
                        : scene_->research_points[idx];
                phase[i] = p.phase_bias * 3.14159265f;
                oam[i] = p.curvature_n * ((p.phase_bias >= 0.0f) ? 1.0f : -1.0f);
            }

            VkDescriptorBufferInfo ab0{};
            ab0.buffer = vk_->photon_audio_phase_buf;
            ab0.offset = 0;
            ab0.range = VK_WHOLE_SIZE;
            VkDescriptorBufferInfo ab1{};
            ab1.buffer = vk_->photon_audio_oam_buf;
            ab1.offset = 0;
            ab1.range = VK_WHOLE_SIZE;
            VkDescriptorBufferInfo ab2{};
            ab2.buffer = vk_->photon_audio_out_buf;
            ab2.offset = 0;
            ab2.range = VK_WHOLE_SIZE;
            VkWriteDescriptorSet aw[3]{};
            aw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            aw[0].dstSet = vk_->photon_audio_ds;
            aw[0].dstBinding = 0;
            aw[0].descriptorCount = 1;
            aw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            aw[0].pBufferInfo = &ab0;
            aw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            aw[1].dstSet = vk_->photon_audio_ds;
            aw[1].dstBinding = 1;
            aw[1].descriptorCount = 1;
            aw[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            aw[1].pBufferInfo = &ab1;
            aw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            aw[2].dstSet = vk_->photon_audio_ds;
            aw[2].dstBinding = 2;
            aw[2].descriptorCount = 1;
            aw[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            aw[2].pBufferInfo = &ab2;
            vkUpdateDescriptorSets(vk_->dev, 3, aw, 0, nullptr);
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

    // Photon-native compute dispatch (visual colors + audio waveform) at render cadence.
    if (scene_ && vk_->photon_visual_pipe && vk_->photon_visual_ds) {
        const uint32_t vis_count = (uint32_t)scene_->instances.size();
        if (vis_count > 0u) {
            VkBufferMemoryBarrier2 h2c_vis[2]{};
            h2c_vis[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            h2c_vis[0].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            h2c_vis[0].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
            h2c_vis[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            h2c_vis[0].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            h2c_vis[0].buffer = vk_->photon_visual_in_buf;
            h2c_vis[0].offset = 0;
            h2c_vis[0].size = VK_WHOLE_SIZE;
            h2c_vis[1] = h2c_vis[0];
            h2c_vis[1].buffer = vk_->instance_buf;
            VkDependencyInfo vis_dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            vis_dep.bufferMemoryBarrierCount = 2;
            vis_dep.pBufferMemoryBarriers = h2c_vis;
            vkCmdPipelineBarrier2(cmd, &vis_dep);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->photon_visual_pipe);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->photon_visual_pipe_layout, 0, 1, &vk_->photon_visual_ds, 0, nullptr);
            vkCmdPushConstants(cmd, vk_->photon_visual_pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &vis_count);
            const uint32_t gx = (vis_count + 63u) / 64u;
            vkCmdDispatch(cmd, gx, 1u, 1u);

            VkBufferMemoryBarrier2 c2f{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            c2f.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            c2f.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            c2f.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            c2f.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            c2f.buffer = vk_->photon_visual_out_buf;
            c2f.offset = 0;
            c2f.size = VK_WHOLE_SIZE;
            VkDependencyInfo c2f_dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            c2f_dep.bufferMemoryBarrierCount = 1;
            c2f_dep.pBufferMemoryBarriers = &c2f;
            vkCmdPipelineBarrier2(cmd, &c2f_dep);
        }
    }
    if (scene_ && vk_->photon_audio_pipe && vk_->photon_audio_ds) {
        const uint32_t audio_count =
            (uint32_t)std::max<size_t>(64u, std::min<size_t>(scene_->research_points.size(), 4096u));
        if (audio_count > 0u) {
            vk_->photon_audio_last_count_u32 = audio_count;
            VkBufferMemoryBarrier2 h2c_audio[2]{};
            h2c_audio[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            h2c_audio[0].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            h2c_audio[0].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
            h2c_audio[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            h2c_audio[0].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            h2c_audio[0].buffer = vk_->photon_audio_phase_buf;
            h2c_audio[0].offset = 0;
            h2c_audio[0].size = VK_WHOLE_SIZE;
            h2c_audio[1] = h2c_audio[0];
            h2c_audio[1].buffer = vk_->photon_audio_oam_buf;
            VkDependencyInfo audio_dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            audio_dep.bufferMemoryBarrierCount = 2;
            audio_dep.pBufferMemoryBarriers = h2c_audio;
            vkCmdPipelineBarrier2(cmd, &audio_dep);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->photon_audio_pipe);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_->photon_audio_pipe_layout, 0, 1, &vk_->photon_audio_ds, 0, nullptr);
            vkCmdPushConstants(cmd, vk_->photon_audio_pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &audio_count);
            const uint32_t gx = (audio_count + 63u) / 64u;
            vkCmdDispatch(cmd, gx, 1u, 1u);

            VkBufferMemoryBarrier2 c2h{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            c2h.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            c2h.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            c2h.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            c2h.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            c2h.buffer = vk_->photon_audio_out_buf;
            c2h.offset = 0;
            c2h.size = VK_WHOLE_SIZE;
            VkDependencyInfo c2h_dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            c2h_dep.bufferMemoryBarrierCount = 1;
            c2h_dep.pBufferMemoryBarriers = &c2h;
            vkCmdPipelineBarrier2(cmd, &c2h_dep);
        }
    }

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
}

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
            if (id == 2013 && code == BN_CLICKED) { OnApplyTransform(); }
            auto read_u32 = [&](HWND h, uint32_t defv)->uint32_t {
                wchar_t buf[128]; buf[0] = 0;
                GetWindowTextW(h, buf, 128);
                wchar_t* endp = nullptr;
                const unsigned long v = wcstoul(buf, &endp, 10);
                if (!endp || endp == buf) return defv;
                return (uint32_t)v;
            };
            auto read_f32 = [&](HWND h, float defv)->float {
                wchar_t buf[128]; buf[0] = 0;
                GetWindowTextW(h, buf, 128);
                wchar_t* endp = nullptr;
                const double v = wcstod(buf, &endp);
                if (!endp || endp == buf) return defv;
                return (float)v;
            };
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
            if (id == 2040 && code == BN_CLICKED) {
                if (scene_) scene_->SetResearchOutputMode(1u, true);
                AppendOutputUtf8("RESEARCH: mode=frequency");
            }
            if (id == 2041 && code == BN_CLICKED) {
                if (scene_) scene_->SetResearchOutputMode(2u, true);
                AppendOutputUtf8("RESEARCH: mode=vector");
            }
            if (id == 2042 && code == BN_CLICKED) {
                const LRESULT v = SendMessageW(hwnd_stov_toggle_, BM_GETCHECK, 0, 0);
                const bool on = (v == BST_CHECKED);
                SetSTOVMode(on);
                if (scene_) scene_->SetResearchStovMode(on, false);
            }
            if (id == 2047 && code == BN_CLICKED) {
                const float a = read_f32(hwnd_param_a_, 0.19f);
                const float f = read_f32(hwnd_param_f_, 0.245f);
                const float i = read_f32(hwnd_param_i_, 0.35f);
                const float v = read_f32(hwnd_param_v_, 0.35f);
                if (scene_) scene_->SetResearchParams(a, f, i, v, true);
                AppendOutputUtf8("RESEARCH: params applied");
            }
            if (id == 2049 && code == BN_CLICKED) {
                const uint32_t edge = read_u32(hwnd_lattice_edge_, 256u);
                if (scene_) scene_->SetResearchLatticeEdge(edge, true);
                AppendOutputUtf8("RESEARCH: lattice edge applied");
            }
            if (id == 2051 && code == BN_CLICKED) {
                const uint32_t hz = read_u32(hwnd_stream_hz_, 360u);
                if (scene_) scene_->SetResearchStreamHz(hz, true);
                AppendOutputUtf8("RESEARCH: stream hz applied");
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
