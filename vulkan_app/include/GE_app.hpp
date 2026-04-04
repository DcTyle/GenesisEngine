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
#include "GE_audio_wav.hpp"

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
    enum SidePanelTabIndex {
        SidePanelTabChatGpt = 0,
        SidePanelTabScene = 1,
        SidePanelTabResearch = 2,
        SidePanelTabCount = 3
    };

    enum AiWorkspaceTabIndex {
        AiWorkspaceTabChat = 0,
        AiWorkspaceTabImages = 1,
        AiWorkspaceTabSimulation = 2,
        AiWorkspaceTabMemory = 3,
        AiWorkspaceTabTools = 4,
        AiWorkspaceTabCount = 5
    };

    enum AiProviderModeIndex {
        AiProviderModeChatGpt = 0,
        AiProviderModeGrokStyle = 1
    };

    AppConfig cfg_;

    // Win64
    HWND hwnd_main_ = nullptr;
    HWND hwnd_viewport_ = nullptr;
    HWND hwnd_panel_ = nullptr;
    HMENU hmenu_main_ = nullptr;
    HWND hwnd_side_tabs_[SidePanelTabCount] = {nullptr, nullptr, nullptr};
    HWND hwnd_ai_workspace_ = nullptr;

    // UI controls
    HWND hwnd_input_ = nullptr;
    HWND hwnd_send_ = nullptr;
    HWND hwnd_output_ = nullptr;
    HWND hwnd_ai_status_ = nullptr;
    HWND hwnd_ai_provider_chatgpt_ = nullptr;
    HWND hwnd_ai_provider_grok_ = nullptr;
    HWND hwnd_ai_dock_toggle_ = nullptr;
    HWND hwnd_ai_new_chat_ = nullptr;
    HWND hwnd_ai_web_toggle_ = nullptr;
    HWND hwnd_ai_history_ = nullptr;
    HWND hwnd_ai_tabs_[AiWorkspaceTabCount] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    HWND hwnd_ai_primary_action_ = nullptr;
    HWND hwnd_ai_secondary_action_ = nullptr;
    HWND hwnd_ai_asset_label_ = nullptr;
    HWND hwnd_ai_asset_name_ = nullptr;
    HWND hwnd_ai_scene_action_ = nullptr;
    HWND hwnd_ai_sim_action_ = nullptr;
    HWND hwnd_ai_sandbox_action_ = nullptr;
    HWND hwnd_ai_status_action_ = nullptr;
    HWND hwnd_ai_voice_toggle_ = nullptr;
    HWND hwnd_ai_speak_last_ = nullptr;
    HWND hwnd_ai_embed_sim_ = nullptr;
    HWND hwnd_import_ = nullptr;
    HWND hwnd_bootstrap_ = nullptr;
    HWND hwnd_objlist_ = nullptr;

    // Transform UI (authoritative in anchors; UI emits control packets)
    HWND hwnd_lbl_posx_ = nullptr;
    HWND hwnd_lbl_posy_ = nullptr;
    HWND hwnd_lbl_posz_ = nullptr;
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
    HWND hwnd_lbl_grid_step_ = nullptr;
    HWND hwnd_grid_step_ = nullptr;
    HWND hwnd_lbl_angle_step_ = nullptr;
    HWND hwnd_angle_step_ = nullptr;
    HWND hwnd_lbl_photon_sim_ = nullptr;
    HWND hwnd_mode_freq_ = nullptr;
    HWND hwnd_mode_vector_ = nullptr;
    HWND hwnd_stov_toggle_ = nullptr;
    HWND hwnd_lbl_param_a_ = nullptr;
    HWND hwnd_param_a_ = nullptr;
    HWND hwnd_lbl_param_f_ = nullptr;
    HWND hwnd_param_f_ = nullptr;
    HWND hwnd_lbl_param_i_ = nullptr;
    HWND hwnd_param_i_ = nullptr;
    HWND hwnd_lbl_param_v_ = nullptr;
    HWND hwnd_param_v_ = nullptr;
    HWND hwnd_apply_sim_params_ = nullptr;
    HWND hwnd_lbl_lattice_ = nullptr;
    HWND hwnd_lattice_edge_ = nullptr;
    HWND hwnd_apply_lattice_edge_ = nullptr;
    HWND hwnd_lbl_stream_hz_ = nullptr;
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
    int side_panel_active_tab_index_ = SidePanelTabChatGpt;
    int side_panel_hover_tab_index_ = -1;
    float side_panel_tab_anim_[SidePanelTabCount] = {1.0f, 0.0f, 0.0f};
    bool ai_workspace_docked_ = true;
    int ai_workspace_active_tab_index_ = AiWorkspaceTabChat;
    int ai_provider_mode_index_ = AiProviderModeChatGpt;
    bool ai_web_search_enabled_ = true;
    bool ai_voice_enabled_ = true;
    bool ai_viewport_embedded_ = false;
    std::vector<std::string> ai_output_history_utf8_;
    std::vector<std::string> ai_prompt_history_utf8_;
    std::string ai_selected_asset_utf8_ = "default_sandbox_sim";
    std::string ai_last_response_text_utf8_;

// View modes
    bool immersion_mode_ = false; // Standard vs Immersion
    float eye_offset_local_[3] = {0.0f, 0.0f, 1.65f};

    // Win64 plumbing
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK SideTabWndProcThunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT SideTabWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void CreateMainWindow(HINSTANCE hInst);
    void CreateMainMenuBar();
    void RefreshMainMenuState();
    void CreateChildWindows();
    void LayoutChildren(int w, int h);
    void LayoutAiWorkspace(int x, int y, int w, int h);
    void SetSidePanelActiveTab(int tab_index, bool focus_tab);
    void SetSidePanelHoverTab(int tab_index);
    void StepSidePanelTabAnimations(float dt);
    void HandleSidePanelTabStep(int delta, bool focus_tab);
    void DrawSidePanelTab(HWND hwnd, HDC hdc, const RECT& rc);
    void SetAiWorkspaceDocked(bool docked);
    void SetAiWorkspaceTab(int tab_index);
    void ResetAiConversation();
    void RefreshAiWorkspaceStatus();
    void RefreshAiWorkspaceHistory();
    void RefreshAiWorkspaceOutput();
    std::string ReadWindowTextUtf8(HWND hwnd) const;
    void SetWindowTextUtf8(HWND hwnd, const std::string& utf8) const;
    bool ShouldEmbedViewportInAiWorkspace() const;
    void SetAiViewportEmbedded(bool enabled);
    void SpeakAiText(const std::string& utf8);
    void SubmitAiWorkspacePrompt();
    void SubmitAiImagePrompt(const std::string& prompt_utf8);
    void SubmitAiSimulationPrompt(const std::string& prompt_utf8);
    void SubmitAiWorkspaceCommand(const std::string& command_utf8);
    void OpenAiWorkspaceAsset(bool simulation_asset, bool sandbox_open);
    void OpenSceneAssetDialog(bool simulation_asset);
    void SaveSceneAssetDialog(bool simulation_asset);
    void ShowEditorAboutDialog() const;
    void ShowAiQuickstartDialog() const;

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
    bool SubmitChatGptPrompt(const std::string& utf8);
};

} // namespace ewv
