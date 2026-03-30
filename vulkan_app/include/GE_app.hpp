#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "camera_controller.hpp"
#include "openxr_runtime.hpp"

namespace ewv {

struct AppConfig {
    std::string app_title_utf8;
    int initial_width = 1280;
    int initial_height = 720;
    bool stov_mode = false;
    std::string stov_data_log_path_utf8;
    std::string stov_audio_out_path_utf8;
};

class App {
public:
    struct VkCtx;
    struct Scene;

    explicit App(const AppConfig& cfg);
    int Run(HINSTANCE hInst);
    void SetSTOVMode(bool enable);
    void LogSTOVData(const std::vector<float>& phase, const std::vector<float>& oam, const std::vector<int>& winding);
    void OutputSTOVAudio(const std::vector<float>& phase, const std::vector<float>& oam, const std::vector<int>& winding);

private:
    AppConfig cfg_;

    // Win64
    HWND hwnd_main_ = nullptr;
    HWND hwnd_viewport_ = nullptr;
    HWND hwnd_panel_ = nullptr;

    // UI controls
    HWND hwnd_input_ = nullptr;
    HWND hwnd_send_ = nullptr;
    HWND hwnd_output_ = nullptr;
    HWND hwnd_import_ = nullptr;
    HWND hwnd_bootstrap_ = nullptr;
    HWND hwnd_objlist_ = nullptr;

    // Transform UI (authoritative in anchors; UI emits control packets)
    HWND hwnd_posx_ = nullptr;
    HWND hwnd_posy_ = nullptr;
    HWND hwnd_posz_ = nullptr;
    HWND hwnd_apply_xform_ = nullptr;

    // Editor interaction UI
    HWND hwnd_mode_translate_ = nullptr;
    HWND hwnd_mode_rotate_ = nullptr;
    HWND hwnd_frame_sel_ = nullptr;
    HWND hwnd_axis_none_ = nullptr;
    HWND hwnd_axis_x_ = nullptr;
    HWND hwnd_axis_y_ = nullptr;
    HWND hwnd_axis_z_ = nullptr;
    HWND hwnd_undo_ = nullptr;
    HWND hwnd_redo_ = nullptr;
    HWND hwnd_snap_enable_ = nullptr;
    HWND hwnd_grid_step_ = nullptr;
    HWND hwnd_angle_step_ = nullptr;
    HWND hwnd_mode_freq_ = nullptr;
    HWND hwnd_mode_vector_ = nullptr;
    HWND hwnd_stov_toggle_ = nullptr;
    HWND hwnd_param_a_ = nullptr;
    HWND hwnd_param_f_ = nullptr;
    HWND hwnd_param_i_ = nullptr;
    HWND hwnd_param_v_ = nullptr;
    HWND hwnd_apply_sim_params_ = nullptr;
    HWND hwnd_lattice_edge_ = nullptr;
    HWND hwnd_apply_lattice_edge_ = nullptr;
    HWND hwnd_stream_hz_ = nullptr;
    HWND hwnd_apply_stream_hz_ = nullptr;

    bool running_ = true;
    bool resized_ = false;
    int client_w_ = 0;
    int client_h_ = 0;

    // Subsystems
    VkCtx* vk_ = nullptr;
    Scene* scene_ = nullptr;
    EwOpenXRRuntime xr_{};

    // Viewport observer camera + input
    EwCamera cam_{};
    EwInputState input_{};

    // Editor interaction state (UI-side). Canonical copy is stored in substrate
    // via EW_ANCHOR_KIND_EDITOR and updated by control packets.
    uint64_t editor_selected_object_id_u64_ = 0;
    uint32_t editor_selection_count_u32_ = 0;
    uint64_t editor_selection_object_id_u64_[16] = {0};

    uint8_t editor_gizmo_mode_u8_ = 1; // Translate
    uint8_t editor_gizmo_space_u8_ = 0; // World
    uint8_t editor_snap_enabled_u8_ = 0;
    uint8_t editor_axis_constraint_u8_ = 0; // 0 none, 1 X, 2 Y, 3 Z
    int32_t editor_grid_step_m_q16_16_ = (int32_t)(1 * 65536);
    int32_t editor_angle_step_deg_q16_16_ = (int32_t)(15 * 65536);

    // Camera rig (UI-side) for orbit/pan/dolly/fly; emitted as CameraSet packets.
    float cam_target_[3] = {0.0f, 0.0f, 0.0f};
    float cam_yaw_rad_ = 0.0f;
    float cam_pitch_rad_ = 0.0f;
    float cam_dist_m_ = 5.0f;

    // Gizmo drag state
    bool gizmo_drag_active_ = false;
    int32_t drag_start_mouse_x_ = 0;
    int32_t drag_start_mouse_y_ = 0;
    int32_t drag_start_pos_q16_16_[3] = {0,0,0};
    int32_t drag_last_pos_q16_16_[3] = {0,0,0};
    int32_t drag_start_rot_q16_16_[4] = {0,0,0,65536};
    int32_t drag_last_rot_q16_16_[4] = {0,0,0,65536};

    // Visualization toggle: when false, the app runs headless (no continuous presentation)
    // but simulation and verification continue.
    bool visualize_enabled_ = true;
    bool stov_mode_ = false;
    std::string stov_data_log_path_utf8_;
    std::string stov_audio_out_path_utf8_;

// View modes
    bool immersion_mode_ = false; // Standard vs Immersion
    float eye_offset_local_[3] = {0.0f, 0.0f, 1.65f};

    // Win64 plumbing
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void CreateMainWindow(HINSTANCE hInst);
    void CreateChildWindows();
    void LayoutChildren(int w, int h);

    void Tick();
    void Render();

    void OnSend();
    void OnImportObj();
    void OnBootstrapGame();
    void OnApplyTransform();

    void EmitEditorSelection(uint64_t object_id_u64);
    void EmitEditorToggleSelection(uint64_t object_id_u64);
    void EmitEditorAxisConstraint();
    void EmitEditorCommitTransformTxn(uint64_t object_id_u64,
                                       const int32_t before_pos_q16_16[3],
                                       const int32_t before_rot_q16_16[4],
                                       const int32_t after_pos_q16_16[3],
                                       const int32_t after_rot_q16_16[4]);
    void EmitEditorUndo();
    void EmitEditorRedo();
    void EmitEditorGizmo();
    void EmitEditorSnap();
    void EmitCameraSetFromRig();

    void AppendOutputUtf8(const std::string& line);
};

} // namespace ewv
