#include <string>

// Utility conversion function declaration
std::wstring utf8_to_wide(const std::string& s);
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <commctrl.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <utility>

#include "../../include/GE_asset_substrate.hpp"
#include "../../include/GE_control_packets.hpp"
#include "camera_controller.hpp"
#include "openxr_runtime.hpp"
#include "vk_ui_layer.hpp"

namespace ewv {

struct AppConfig {
    std::string app_title_utf8;
    int initial_width = 1280;
    int initial_height = 720;
    std::vector<std::string> startup_commands_utf8;
    std::string output_log_path_utf8;
    std::string app_user_model_id_utf8;
    bool start_live_mode = false;
    bool start_resonance_view = false;
    bool start_confinement_particles = true;
    bool start_visualization = true;
};

class App {
public:

    struct VkCtx;

    struct Scene {
        SubstrateManager sm;
        uint64_t next_object_id_u64 = 1;
        double earth_orbit_angle_rad = 0.0;
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
            uint8_t pbr_scan_u8 = 0;
            uint8_t ai_training_meta_ready_u8 = 0;
            std::string material_meta_hint_utf8;
            EwObjectDna object_dna;
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
        std::vector<EwRenderInstance> instances;
        genesis::GeResearchConfinementArchive research_confinement;

        Scene();
        void Tick();
        bool PopUiLine(std::string& out_utf8);
        void SubmitAiChatLine(const std::string& utf8, uint32_t chat_slot_u32 = 0u, uint32_t mode_u32 = SubstrateManager::EW_CHAT_MEMORY_MODE_TALK);
        bool SnapshotAiChatMemory(uint32_t prefer_mode_u32, uint32_t max_entries_u32, std::vector<SubstrateManager::EwChatMemoryEntry>& out_entries, std::string& out_summary_utf8);
        void ObserveAiChatMemory(uint32_t chat_slot_u32, uint32_t mode_u32, const std::string& utf8);
        void LinkAiChatProject(uint32_t chat_slot_u32, const std::string& root_utf8, const std::vector<std::string>& rels_utf8);
        bool SnapshotAiChatProject(uint32_t chat_slot_u32, SubstrateManager::EwProjectLinkEntry& out_entry);
        bool SnapshotAiProjectSpectrumLines(uint32_t chat_slot_u32, uint32_t max_lines_u32, std::vector<std::string>& out_lines_utf8);
        void SetLiveViewportProjection(bool enabled, int spectrum_band_i32, uint32_t lattice_sel_u32, bool volume_mode, uint32_t slice_z_u32, uint32_t stride_u32, uint32_t max_points_u32, uint8_t intensity_min_u8);
        void SetLiveViewportMode(bool enabled, int spectrum_band_i32);
        bool DuplicateSelectedObjectForEditor(float offset_x_m, float offset_y_m, float offset_z_m);
        bool SpawnProceduralEditorTemplate(const std::wstring& template_label_w);
        void ContentReindex();
        bool SnapshotContentEntries(uint32_t limit_u32, std::vector<genesis::GeAssetEntry>& out_entries, std::string* out_err);
        void ContentListAll(uint32_t limit_u32);
        void SetRepoReaderEnabled(bool enabled);
        void RescanRepoReader();
        bool SnapshotRepoReaderStatus(std::string& out_status_utf8);
        bool SnapshotRepoReaderFiles(uint32_t limit_u32, std::vector<std::string>& out_rel_paths_utf8, std::string* out_err);
        bool SnapshotRepoFilePreview(const std::string& rel_path_utf8, uint32_t max_bytes_u32, std::string& out_preview_utf8, std::string* out_err);
        bool SnapshotRepoFileCoherenceHits(const std::string& rel_path_utf8, uint32_t limit_u32, std::vector<genesis::GeCoherenceHit>& out_hits, std::string* out_err);
        bool SnapshotCoherenceStats(std::string& out_stats_utf8);
        bool SnapshotCoherenceQuery(const std::string& query_utf8, uint32_t limit_u32, std::vector<genesis::GeCoherenceHit>& out_hits, std::string* out_err);
        bool SnapshotCoherenceRenamePlan(const std::string& old_ident_ascii, const std::string& new_ident_ascii, uint32_t limit_u32, std::vector<genesis::GeCoherenceHit>& out_hits, std::string* out_err);
        bool SnapshotCoherenceRenamePatch(const std::string& old_ident_ascii, const std::string& new_ident_ascii, uint32_t limit_u32, std::string& out_patch_utf8, std::string* out_err);
        bool SnapshotCoherenceSelftest(bool& out_ok, std::string& out_report_utf8);
        void EmitCoherenceStats();
        void EmitCoherenceQuery(const std::string& query_utf8, uint32_t limit_u32);
        void EmitCoherenceRenamePlan(const std::string& old_ident_ascii, const std::string& new_ident_ascii, uint32_t limit_u32);
        void SetCoherenceHighlightQuery(const std::string& query_utf8, uint32_t limit_u32);
        void SetCoherenceHighlightPath(const std::string& rel_path_utf8);
        bool SnapshotVaultEntries(uint32_t limit_u32, std::vector<std::string>& out_rel_paths_utf8, std::string* out_err);
        bool SnapshotVaultEntryPreview(const std::string& rel_path_utf8, uint32_t max_bytes_u32, std::string& out_preview_utf8, std::string* out_err);
        bool ImportVaultEntry(const std::string& rel_path_utf8, std::string& out_written_path_utf8, std::string* out_err);
        void EmitVaultListAll(uint32_t limit_u32);
        void RequestGameBootstrap(const std::string& request_utf8);
        void EmitAiModelTrainReady(const std::string& base_name_utf8);
    };

    explicit App(const AppConfig& cfg);
    int Run(HINSTANCE hInst);

private:
    AppConfig cfg_;
    std::unique_ptr<VkUiLayer> ui_layer_;

    // Win64
    HWND hwnd_main_ = nullptr;
    HWND hwnd_viewport_ = nullptr;
    HWND hwnd_primary_tab_ = nullptr;
    HWND hwnd_panel_ = nullptr;
    HWND hwnd_dock_splitter_ = nullptr;
    HWND hwnd_topbar_ = nullptr;
    HWND hwnd_topbar_menu_ = nullptr;
    HWND hwnd_topbar_menu_edit_ = nullptr;
    HWND hwnd_topbar_menu_window_ = nullptr;
    HWND hwnd_topbar_menu_tools_ = nullptr;
    HWND hwnd_topbar_menu_build_ = nullptr;
    HWND hwnd_topbar_menu_platforms_ = nullptr;
    HWND hwnd_topbar_menu_select_ = nullptr;
    HWND hwnd_topbar_menu_actor_ = nullptr;
    HWND hwnd_topbar_menu_help_ = nullptr;
    HWND hwnd_topbar_workspace_level_ = nullptr;
    HWND hwnd_topbar_workspace_asset_ = nullptr;
    HWND hwnd_topbar_workspace_voxel_ = nullptr;
    HWND hwnd_topbar_title_ = nullptr;
    HWND hwnd_topbar_project_ = nullptr;
    HWND hwnd_topbar_status_ = nullptr;
    HWND hwnd_topbar_select_mode_ = nullptr;
    HWND hwnd_topbar_add_actor_ = nullptr;
    HWND hwnd_topbar_tool_translate_ = nullptr;
    HWND hwnd_topbar_tool_rotate_ = nullptr;
    HWND hwnd_topbar_tool_frame_ = nullptr;
    HWND hwnd_topbar_view_perspective_ = nullptr;
    HWND hwnd_topbar_view_lit_ = nullptr;
    HWND hwnd_topbar_view_show_ = nullptr;
    HWND hwnd_topbar_transport_back_ = nullptr;
    HWND hwnd_topbar_transport_start_ = nullptr;
    HWND hwnd_topbar_sim_ = nullptr;
    HWND hwnd_topbar_pause_ = nullptr;
    HWND hwnd_topbar_stop_ = nullptr;
    HWND hwnd_topbar_replay_ = nullptr;
    HWND hwnd_topbar_ai_ = nullptr;
    HWND hwnd_topbar_live_ = nullptr;
    HWND hwnd_topbar_photon_ = nullptr;
    HWND hwnd_topbar_content_ = nullptr;
    HWND hwnd_topbar_assistant_ = nullptr;
    HWND hwnd_topbar_minimize_ = nullptr;
    HWND hwnd_topbar_maximize_ = nullptr;
    HWND hwnd_topbar_close_ = nullptr;

    // Left dock (Unreal-style Asset Vault panel).
    HWND hwnd_left_panel_ = nullptr;
    HWND hwnd_left_title_ = nullptr;
    HWND hwnd_left_search_ = nullptr;
    HWND hwnd_left_category_ = nullptr;
    HWND hwnd_left_actor_thumb_ = nullptr;
    HWND hwnd_left_actor_list_ = nullptr;
    std::vector<std::string> left_vault_visible_paths_utf8_;
    bool left_place_actor_sync_guard_ = false;

    // Right dock (Unreal-style): tab strip + panel registry
    static constexpr uint32_t RDockPanelCount = 7u;
    HWND hwnd_rdock_tab_ = nullptr;
    HWND hwnd_rdock_outliner_ = nullptr;
    HWND hwnd_rdock_details_ = nullptr;
    HWND hwnd_rdock_asset_ = nullptr;
    HWND hwnd_rdock_voxel_ = nullptr;
    HWND hwnd_rdock_node_ = nullptr;
    HWND hwnd_rdock_sequencer_ = nullptr;
    HWND hwnd_rdock_ai_ = nullptr;
    uint32_t rdock_tab_index_u32_ = 0u;
    bool rdock_panel_visible_[RDockPanelCount] = {true, true, true, true, true, true, true};
    bool rdock_panel_locked_[RDockPanelCount] = {false, false, false, false, false, false, false};
    bool rdock_panel_workspace_enabled_[RDockPanelCount] = {true, true, true, true, true, true, true};
    uint32_t primary_workspace_tab_index_u32_ = 0u; // 0=Level Sim, 1=Asset Builder, 2=Voxel Builder

    // Tab-local toolbars (editor-grade feel). Pure UI; no core logic.
    HWND hwnd_tb_outliner_ = nullptr;
    HWND hwnd_tb_details_  = nullptr;
    HWND hwnd_tb_asset_    = nullptr;
    HWND hwnd_tb_voxel_    = nullptr;
    HWND hwnd_tb_node_     = nullptr;
    HWND hwnd_tb_sequencer_ = nullptr;
    HWND hwnd_tb_ai_ = nullptr;

    // Toolbar controls
    HWND hwnd_outliner_search_ = nullptr;
    HWND hwnd_outliner_clear_  = nullptr;
    HWND hwnd_outliner_header_item_ = nullptr;
    HWND hwnd_outliner_header_type_ = nullptr;
    HWND hwnd_voxel_preset_    = nullptr;
    HWND hwnd_voxel_apply_     = nullptr;
    HWND hwnd_voxel_presets_list_ = nullptr;
    HWND hwnd_voxel_viewport_resonance_ = nullptr;
    HWND hwnd_voxel_lattice_volume_ = nullptr;
    HWND hwnd_voxel_lattice_apply_ = nullptr;
    HWND hwnd_voxel_vector_visualization_ = nullptr;
    HWND hwnd_voxel_vector_gain_ = nullptr;
    HWND hwnd_voxel_field_depth_ = nullptr;
    HWND hwnd_voxel_focal_length_ = nullptr;
    HWND hwnd_voxel_atom_nodes_ = nullptr;
    HWND hwnd_voxel_summary_   = nullptr;
    int  voxel_atom_node_selected_i32_ = 0;
    HWND hwnd_asset_label_run_ = nullptr;
    HWND hwnd_asset_label_ai_ = nullptr;
    HWND hwnd_asset_label_learning_ = nullptr;
    HWND hwnd_asset_label_crawling_ = nullptr;
    HWND hwnd_asset_selected_  = nullptr;
    HWND hwnd_asset_gate_      = nullptr;
    HWND hwnd_asset_builder_status_ = nullptr;
    HWND hwnd_asset_preview_title_ = nullptr;
    HWND hwnd_asset_preview_caption_ = nullptr;
    HWND hwnd_asset_review_refs_ = nullptr;
    HWND hwnd_asset_ai_panel_button_ = nullptr;
    HWND hwnd_asset_label_tool_mode_ = nullptr;
    HWND hwnd_asset_label_planet_atmo_ = nullptr;
    HWND hwnd_asset_label_planet_iono_ = nullptr;
    HWND hwnd_asset_label_planet_magneto_ = nullptr;
    HWND hwnd_asset_label_character_archetype_ = nullptr;
    HWND hwnd_asset_label_character_height_ = nullptr;
    HWND hwnd_asset_label_character_rigidity_ = nullptr;
    HWND hwnd_asset_label_character_gait_ = nullptr;
    HWND hwnd_asset_tool_mode_ = nullptr;
    HWND hwnd_asset_planet_atmo_ = nullptr;
    HWND hwnd_asset_planet_iono_ = nullptr;
    HWND hwnd_asset_planet_magneto_ = nullptr;
    HWND hwnd_asset_planet_apply_ = nullptr;
    HWND hwnd_asset_planet_sculpt_ = nullptr;
    HWND hwnd_asset_planet_paint_ = nullptr;
    HWND hwnd_asset_character_archetype_ = nullptr;
    HWND hwnd_asset_character_height_ = nullptr;
    HWND hwnd_asset_character_rigidity_ = nullptr;
    HWND hwnd_asset_character_gait_ = nullptr;
    HWND hwnd_asset_character_bind_ = nullptr;
    HWND hwnd_asset_character_pose_ = nullptr;
    HWND hwnd_asset_tool_summary_ = nullptr;
    HWND hwnd_asset_thumb_ = nullptr;
    HWND hwnd_node_summary_ = nullptr;
    HWND hwnd_node_open_coh_ = nullptr;
    HWND hwnd_node_graph_ = nullptr;
    HWND hwnd_node_search_ = nullptr;
    HWND hwnd_node_search_spawn_ = nullptr;
    HWND hwnd_node_results_ = nullptr;
    HWND hwnd_node_export_preview_ = nullptr;
    HWND hwnd_node_connect_selected_ = nullptr;
    HWND hwnd_node_disconnect_selected_ = nullptr;
    HWND hwnd_node_expand_ = nullptr;
    HWND hwnd_node_play_ = nullptr;
    HWND hwnd_node_mode_local_ = nullptr;
    HWND hwnd_node_mode_propagate_ = nullptr;
    HWND hwnd_node_rc_band_ = nullptr;
    HWND hwnd_node_rc_drive_ = nullptr;
    HWND hwnd_node_rc_apply_ = nullptr;
    HWND hwnd_viewport_resonance_overlay_ = nullptr;
    int  node_source_pin_selected_i32_ = 0;
    int  node_target_pin_selected_i32_ = 0;
    int  node_source_pin_hover_i32_ = -1;
    HWND hwnd_seq_timeline_ = nullptr;
    HWND hwnd_seq_summary_ = nullptr;
    HWND hwnd_seq_add_key_ = nullptr;
    HWND hwnd_seq_back_ = nullptr;
    HWND hwnd_seq_start_ = nullptr;
    HWND hwnd_seq_play_ = nullptr;
    HWND hwnd_seq_pause_ = nullptr;
    HWND hwnd_seq_stop_ = nullptr;
    HWND hwnd_seq_replay_ = nullptr;
    HWND hwnd_seq_loop_ = nullptr;
    HWND hwnd_seq_motion_match_ = nullptr;
    HWND hwnd_seq_stress_overlay_ = nullptr;
    bool seq_play_enabled_ = false;
    bool seq_loop_builder_enabled_ = false;
    bool seq_stress_overlay_enabled_ = true;
    int  seq_selected_i32_ = 0;
    bool node_graph_expanded_ = false;
    bool node_play_excitation_ = false;
    bool node_rename_propagate_ = false;
    int  node_graph_selected_i32_ = 0;
    std::vector<std::wstring> node_graph_items_w_;
    std::vector<int> node_graph_item_level_i32_;
    std::vector<int> node_graph_parent_i32_;
    std::vector<int> node_graph_strength_pct_i32_;
    std::vector<uint32_t> node_graph_anchor_id_u32_;
    std::vector<std::wstring> node_graph_operator_path_w_;
    std::vector<std::wstring> node_graph_lookup_name_w_;
    std::vector<std::wstring> node_graph_interconnect_w_;
    std::vector<std::wstring> node_graph_effect_w_;
    std::vector<std::wstring> node_graph_placement_w_;
    std::vector<std::wstring> node_graph_language_hint_w_;
    std::vector<std::wstring> node_graph_export_scope_w_;
    std::vector<std::wstring> node_graph_language_policy_w_;
    std::vector<std::wstring> node_graph_input_pins_w_;
    std::vector<std::wstring> node_graph_output_pins_w_;
    std::vector<std::wstring> node_graph_doc_key_w_;
    std::vector<uint8_t> node_graph_contract_ready_u8_;
    std::vector<std::wstring> node_graph_compatibility_w_;
    std::vector<int> node_graph_edge_in_i32_;
    std::vector<int> node_graph_edge_out_i32_;
    std::vector<std::wstring> node_graph_edge_label_w_;
    std::vector<int> node_graph_sequence_i32_;
    std::vector<int> node_graph_divergence_i32_;
    std::vector<uint8_t> node_graph_is_ancilla_u8_;
    std::vector<uint8_t> node_graph_language_locked_u8_;
    struct NodeSpawnedItem {
        std::wstring label_w;
        std::wstring lookup_name_w;
        std::wstring operator_path_w;
        std::wstring interconnect_w;
        std::wstring effect_w;
        std::wstring placement_w;
        std::wstring language_hint_w;
        std::wstring export_scope_w;
        std::wstring language_policy_w;
        std::wstring input_pins_w;
        std::wstring output_pins_w;
        std::wstring doc_key_w;
        bool contract_ready = false;
        std::wstring compatibility_w;
        uint32_t parent_anchor_id_u32 = 0u;
        uint32_t anchor_id_u32 = 0u;
        int strength_pct_i32 = 56;
        int sequence_i32 = 0;
        int divergence_i32 = 0;
        bool ancilla_anchor = true;
        bool coherence_hint = false;
        bool language_locked = false;
    };
    struct NodePaletteEntry {
        std::wstring label_w;
        std::wstring lookup_name_w;
        std::wstring operator_path_w;
        std::wstring interconnect_w;
        std::wstring effect_w;
        std::wstring placement_w;
        std::wstring language_hint_w;
        std::wstring export_scope_w;
        std::wstring language_policy_w;
        std::wstring input_pins_w;
        std::wstring output_pins_w;
        std::wstring doc_key_w;
        bool contract_ready = false;
        std::wstring compatibility_w;
        int strength_pct_i32 = 52;
        bool ancilla_anchor = true;
        bool coherence_hint = false;
        bool language_locked = false;
    };
    struct NodeEdgeRecord {
        int parent_i32 = -1;
        int child_i32 = -1;
        uint32_t parent_anchor_id_u32 = 0u;
        uint32_t child_anchor_id_u32 = 0u;
        std::wstring edge_label_w;
    };
    std::vector<NodeSpawnedItem> node_spawned_items_;
    std::vector<NodeEdgeRecord> node_edge_records_;
    std::vector<NodePaletteEntry> node_palette_entries_;
    std::wstring node_export_preview_w_;

    std::wstring GetNodeSearchQuery() const;
    void RebuildNodePaletteEntries();
    void ClampNodePinSelections();
    std::wstring DescribeNodeCompatibility(const std::wstring& source_output_pins, const std::wstring& target_input_pins) const;
    bool SpawnNodePaletteEntry(size_t palette_idx);
    bool ConnectNodePaletteEntry(size_t palette_idx);
    bool DisconnectSelectedNode();
    void ShowNodeSpawnMenu(POINT screen_pt);

    // Outliner filter (UI-only).
    std::string outliner_filter_utf8_;

    struct CanonicalReferenceSummary;

    void RebuildOutlinerList();
    std::wstring BuildPlaceActorsStatusLine(const std::wstring& label_w) const;
    bool OpenPlaceActorsBuilderByLabel(const std::wstring& label_w);
    bool ActivatePlaceActorsEntryByLabel(const std::wstring& label_w);
    void RebuildContentBrowserViews();
    void RebuildContentSourcesPanel();
    void PushContentNavigationPrefix(const std::string& prefix_utf8);
    void NavigateContentHistory(int direction_i32);
    void RefreshContentBrowserFromRuntime(uint32_t limit_u32);
    void RefreshNodePanel();
    void RefreshAssetDesignerPanel();
    void RefreshVoxelDesignerPanel();
    void SyncVoxelAtomNodeList();
    void RefreshViewportResonanceOverlay();
    void RefreshSequencerPanel();
    void RefreshContentBrowserChrome();
    void ConfigureContentBrowserListView();
    void RefreshContent3DBrowserSurface();
    void ShowContentBrowserSettingsMenu(HWND anchor_hwnd);
    void SetContentBrowserViewMode(uint32_t mode_u32);
    bool SelectContentRelativePath(const std::string& rel_utf8, bool honor_lock = true);
    bool ReviewReferencesForPath(const std::string& rel_utf8, const wchar_t* origin_w);
    bool SelectContentForSelectedObject();
    bool BuildCanonicalReferenceSummaryFromHits(const std::string& subject_rel_utf8,
                                               const std::string& source_key_utf8,
                                               const std::wstring& source_label_w,
                                               const std::string& query_utf8,
                                               const std::vector<genesis::GeCoherenceHit>& hits,
                                               CanonicalReferenceSummary* out_summary,
                                               std::string* out_err) const;
    bool BuildCanonicalReferenceSummaryForPath(const std::string& rel_utf8, uint32_t limit_u32, CanonicalReferenceSummary* out_summary, std::string* out_err) const;
    bool BuildCanonicalReferenceSummaryForRename(const std::string& old_ident_utf8, const std::string& new_ident_utf8, uint32_t limit_u32, CanonicalReferenceSummary* out_summary, std::string* out_err) const;
    void CommitCanonicalReferenceSummary(const CanonicalReferenceSummary& summary, bool focus_coherence, const wchar_t* origin_w);
    std::string ResolveAiChatPrimaryPathUtf8(uint32_t chat_idx_u32) const;
    bool NavigateAiChatReferenceSpine(uint32_t chat_idx_u32, bool focus_repo, bool focus_coherence);
    void RefreshAiNavigationSpine(uint32_t chat_idx_u32);
    std::wstring BuildAiNavigationSpineText(uint32_t chat_idx_u32) const;

    // Editor panels (Unreal-style layout baseline)
    HWND hwnd_content_ = nullptr;          // bottom content browser container
    HWND hwnd_content_list_ = nullptr;     // list view
    HWND hwnd_content_refresh_ = nullptr;  // refresh button
    HWND hwnd_content_search_ = nullptr;   // search box
    HWND hwnd_content_tab_primary_ = nullptr;   // "Content Browser 1"
    HWND hwnd_content_tab_secondary_ = nullptr; // "Content Browser 2"
    HWND hwnd_content_settings_ = nullptr; // settings button / popup anchor
    HWND hwnd_content_add_ = nullptr;
    HWND hwnd_content_import_toolbar_ = nullptr;
    HWND hwnd_content_save_all_ = nullptr;
    HWND hwnd_content_fab_ = nullptr;
    HWND hwnd_content_breadcrumb_ = nullptr;
    HWND hwnd_content_back_ = nullptr;
    HWND hwnd_content_forward_ = nullptr;
    HWND hwnd_content_sources_ = nullptr;  // left sources tree
    bool content_secondary_tab_active_ = false;
    bool content_visible_ = true;
    bool content_sources_panel_visible_ = true;
    bool content_show_favorites_ = true;
    bool content_show_folders_ = true;
    bool content_show_empty_folders_ = true;
    bool content_organize_folders_ = true;
    bool content_show_all_folder_ = true;
    bool content_show_cpp_classes_ = true;
    bool content_show_developers_content_ = true;
    bool content_show_engine_content_ = false;
    bool content_show_plugin_content_ = false;
    bool content_show_localized_content_ = false;
    bool content_search_asset_class_names_ = true;
    bool content_search_asset_path_ = true;
    bool content_search_collection_names_ = true;
    bool content_thumbnail_edit_mode_ = false;
    bool content_realtime_thumbnails_ = true;
    bool content_always_expand_tooltips_ = true;
    bool content_locked_ = false;
    uint32_t content_thumbnail_size_u32_ = 2u; // 0 tiny .. 4 huge

    // Deterministic content browser model (populated from structured runtime snapshot)
    struct ContentItem {
        std::string rel_utf8;
        std::string label_utf8;
    };
    std::vector<ContentItem> content_items_;
    struct CanonicalReferenceSummary {
        std::string subject_rel_utf8;
        std::string source_key_utf8;
        std::string query_utf8;
        std::wstring source_label_w;
        std::vector<std::wstring> lines_w;
        std::vector<std::string> paths_utf8;
        std::wstring summary_w;
        uint32_t hit_count_u32 = 0u;
        uint64_t ranking_revision_u64 = 0ull;
        bool exact_subject_present = false;
    };
    CanonicalReferenceSummary canonical_reference_summary_;
    // Content search filter (UI-only; applied deterministically to rel/label substrings).
    std::string content_search_utf8_;

    // Visible mapping for list/thumb surfaces (because filters can hide items).
    // Each visible row i maps to content_items_[content_visible_indices_[i]].
    std::vector<size_t> content_visible_indices_;
    std::string content_source_prefix_utf8_;
    std::vector<std::string> content_source_favorites_utf8_;
    std::unordered_map<HTREEITEM, std::string> content_source_paths_by_item_;
    std::vector<std::string> content_nav_history_utf8_;
    int32_t content_nav_index_i32_ = -1;
    bool content_nav_suppress_push_ = false;

    // Selection mirror between list/thumb surfaces (tracks rel path).
    std::string content_selected_rel_utf8_;
    bool content_selection_sync_guard_ = false;
    bool content_source_sync_guard_ = false;
    std::string asset_last_review_rel_utf8_;
    uint64_t asset_last_review_revision_u64_ = 0ull;
    // Content view toggles + surfaces (List / Thumb / 3D).
    HWND hwnd_content_view_list_ = nullptr;
    HWND hwnd_content_view_thumb_ = nullptr;
    HWND hwnd_content_view_3d_ = nullptr;
    HWND hwnd_content_thumb_ = nullptr;     // icon/thumb surface
    HWND hwnd_content_3d_ = nullptr;        // 3D library surface (owner-draw list)
    HWND hwnd_content_status_ = nullptr;    // bounded status line
    HWND hwnd_content_selected_ = nullptr;  // selected item line
    HWND hwnd_content_refcheck_ = nullptr;  // coherence/reference review
    HIMAGELIST himl_content_thumbs_ = nullptr; // 64x64 thumbs
    HIMAGELIST himl_content_icons_ = nullptr;  // 16x16 list icons
    uint32_t content_view_mode_u32_ = 0; // 0=grid,1=list,2=columns

    // Coherence highlight (derived-only). Mirrors SubstrateManager::coh_highlight_paths.
    uint64_t coh_highlight_seen_revision_u64_ = 0;
    std::unordered_set<std::wstring> coh_highlight_set_w_;


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

    // Details property grid (property-grid style inspector).
    HWND hwnd_propgrid_ = nullptr;
    HWND hwnd_propedit_ = nullptr;   // in-place editor
    int  propedit_item_ = -1;
    int  propedit_subitem_ = -1;
    bool propedit_active_ = false;
    void RebuildPropertyGrid();
    void RefreshEditorHistoryUi();
    void BeginPropEdit(int item, int subitem);
    void CommitPropEdit(bool apply);
    bool CopyDetailsBlockToClipboard();
    bool PasteDetailsBlockFromClipboard();
    bool CopyFocusedSurfaceToClipboard();
    bool PasteFocusedSurfaceFromClipboard();

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

    int details_controls_h_ = 220; // UI-only: bottom of fixed Details controls area

    // AI + sim toggles (owner-drawn switch controls)
    HWND hwnd_toggle_play_ = nullptr;
    HWND hwnd_toggle_ai_ = nullptr;
    HWND hwnd_toggle_learning_ = nullptr;
    HWND hwnd_toggle_crawling_ = nullptr;
    HWND hwnd_ai_status_ = nullptr;
    HWND hwnd_vault_ = nullptr;
    // AI panel window (Chat-first, multi-chat tabs)
    HWND hwnd_ai_panel_ = nullptr;
    HWND hwnd_ai_tab_ = nullptr;
    HWND hwnd_ai_bell_ = nullptr; // reserved (future notifications)
    HWND hwnd_ai_menu_ = nullptr; // "⋯" menu button (toggles)
    // Chat controls
    HWND hwnd_ai_chat_list_ = nullptr;
    HWND hwnd_ai_chat_input_ = nullptr;
    HWND hwnd_ai_chat_send_ = nullptr;
    HWND hwnd_ai_chat_provider_ = nullptr;
    HWND hwnd_ai_chat_model_ = nullptr;
    // New-chat (compose) button: pen+pad icon at the top-right of the AI panel.
    HWND hwnd_ai_chat_compose_ = nullptr;
    // Apply patch button (UI-only): writes changes to disk only after explicit approval.
    HWND hwnd_ai_chat_apply_ = nullptr;
    // Use detected patch from last Assistant message.
    HWND hwnd_ai_chat_usepatch_ = nullptr;
    // Preview patch (read-only) before Apply.
    HWND hwnd_ai_chat_preview_ = nullptr;

    // AI panel embedded tool views (dockable within the AI panel itself).
    HWND hwnd_ai_view_chat_ = nullptr;
    HWND hwnd_ai_view_repo_ = nullptr;
    HWND hwnd_ai_view_coherence_ = nullptr;
    HWND hwnd_ai_panel_tool_status_ = nullptr;
    HWND hwnd_ai_panel_chat_title_ = nullptr;
    HWND hwnd_ai_panel_chat_state_ = nullptr;
    HWND hwnd_ai_panel_action_hint_ = nullptr;
    HWND hwnd_ai_chat_mode_talk_ = nullptr;
    HWND hwnd_ai_chat_mode_code_ = nullptr;
    HWND hwnd_ai_chat_mode_sim_ = nullptr;
    HWND hwnd_ai_chat_cortex_ = nullptr;
    HWND hwnd_ai_chat_link_project_ = nullptr;
    HWND hwnd_ai_chat_patchview_ = nullptr;
    HWND hwnd_ai_chat_nextaction_ = nullptr;

    HWND hwnd_ai_repo_status_ = nullptr;
    HWND hwnd_ai_repo_refresh_ = nullptr;
    HWND hwnd_ai_repo_copy_ = nullptr;
    HWND hwnd_ai_repo_highlight_ = nullptr;
    HWND hwnd_ai_repo_label_files_ = nullptr;
    HWND hwnd_ai_repo_label_preview_ = nullptr;
    HWND hwnd_ai_repo_label_coherence_ = nullptr;
    HWND hwnd_ai_repo_list_ = nullptr;
    HWND hwnd_ai_repo_preview_ = nullptr;
    HWND hwnd_ai_repo_coherence_ = nullptr;
    HWND hwnd_ai_repo_selected_ = nullptr;
    HWND hwnd_ai_repo_copy_path_ = nullptr;
    std::vector<std::string> ai_repo_rel_paths_utf8_;
    std::string ai_repo_selected_rel_utf8_;

    HWND hwnd_ai_coh_label_query_ = nullptr;
    HWND hwnd_ai_coh_label_rename_ = nullptr;
    HWND hwnd_ai_coh_label_results_ = nullptr;
    HWND hwnd_ai_coh_label_patch_ = nullptr;
    HWND hwnd_ai_coh_query_ = nullptr;
    // hwnd_ai_coh_old_ removed: deprecated UI element no longer used.
    HWND hwnd_ai_coh_new_ = nullptr;
    HWND hwnd_ai_coh_results_ = nullptr;
    HWND hwnd_ai_coh_patch_ = nullptr;
    HWND hwnd_ai_coh_stats_ = nullptr;
    HWND hwnd_ai_coh_copy_results_ = nullptr;
    HWND hwnd_ai_coh_copy_patch_ = nullptr;
    HWND hwnd_ai_coh_highlight_hit_ = nullptr;
    HWND hwnd_ai_coh_copy_hit_path_ = nullptr;
    HWND hwnd_ai_coh_selected_ = nullptr;
    HWND hwnd_ai_coh_open_hit_ = nullptr;
    std::wstring ai_coh_patch_preview_w_;
    std::vector<std::wstring> ai_coh_result_lines_w_;
    std::vector<std::string> ai_coh_result_paths_utf8_;
    int ai_coh_selected_index_i32_ = -1;

    uint32_t ai_panel_view_u32_ = 0u; // 0 chat, 1 repo, 2 coherence

    static constexpr int AI_CHAT_MAX = 8;
    static constexpr uint32_t AI_CHAT_PROVIDER_NATIVE = 0u;
    static constexpr uint32_t AI_CHAT_PROVIDER_CHATGPT = 1u;
    static constexpr uint32_t AI_CHAT_PROVIDER_HYBRID = 2u;
    uint32_t ai_chat_count_u32_ = 1u;
    // UI-only: chat folder routing. Folder UI comes later, but chat creation
    // already supports "new chat in current folder" semantics.
    uint32_t ai_chat_folder_id_u32_ = 0u; // current folder (0 = root)
    uint32_t ai_chat_folder_of_u32_[AI_CHAT_MAX]{};
    // UI-only storage of messages per chat tab (bounded by UI behavior).
    std::vector<std::wstring> ai_chat_msgs_[AI_CHAT_MAX];
    // UI-only: per-chat tab titles.
    std::wstring ai_chat_title_w_[AI_CHAT_MAX];

    // UI-only: per-chat proposed patch buffer. Stored in-memory only.
    std::wstring ai_chat_patch_w_[AI_CHAT_MAX];
    // UI-only: per-chat flag indicating patch buffer was previewed since last edit.
    bool ai_chat_patch_previewed_[AI_CHAT_MAX]{};
    // UI-only: per-chat last selected apply/export target directory (disk writes only when approved).
    std::wstring ai_chat_apply_target_dir_w_[AI_CHAT_MAX];

    // UI-only: last detected unified diff from an Assistant message (per chat).
    std::wstring ai_chat_last_detected_patch_w_[AI_CHAT_MAX];
    bool ai_chat_last_detected_patch_valid_[AI_CHAT_MAX]{};
    uint32_t ai_chat_mode_u32_[AI_CHAT_MAX]{}; // 1=talk,2=code,3=sim
    uint32_t ai_chat_provider_u32_[AI_CHAT_MAX]{}; // 0=native,1=chatgpt,2=hybrid
    std::wstring ai_chat_api_model_w_[AI_CHAT_MAX];
    std::wstring ai_chat_native_model_w_[AI_CHAT_MAX];
    std::wstring ai_chat_cortex_summary_w_[AI_CHAT_MAX];
    std::wstring ai_chat_project_summary_w_[AI_CHAT_MAX];
    std::wstring ai_chat_project_root_w_[AI_CHAT_MAX];
    std::wstring ai_chat_patch_scope_root_w_[AI_CHAT_MAX];
    uint32_t ai_chat_patch_scope_file_count_u32_[AI_CHAT_MAX]{};
    std::wstring ai_chat_patch_explain_w_[AI_CHAT_MAX];
    std::wstring ai_chat_patch_meta_w_[AI_CHAT_MAX];
    std::wstring ai_chat_last_workflow_event_w_[AI_CHAT_MAX];
    std::wstring ai_chat_nav_target_rel_w_[AI_CHAT_MAX];
    std::wstring ai_chat_nav_session_w_[AI_CHAT_MAX];
    std::wstring ai_chat_nav_validation_w_[AI_CHAT_MAX];
    std::wstring ai_chat_nav_reference_w_[AI_CHAT_MAX];

    uint64_t ai_experiments_seen_u64_ = 0ull;
    uint32_t ai_unseen_experiments_u32_ = 0u;
    uint32_t ai_tab_index_u32_ = 0u;

    void AiChatRenderSelected();
    void AiChatAppend(uint32_t chat_idx_u32, const std::wstring& line);
    void AiChatAppendAssistant(uint32_t chat_idx_u32, const std::wstring& assistant_text);
    void AiChatPreviewPatchExplanation(uint32_t chat_idx_u32, const wchar_t* prefix_w);
    std::wstring BuildAiChatPatchMetadata(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const;
    uint32_t BuildAiChatPatchWarningMask(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const;
    bool HasBlockingAiChatPatchWarnings(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const;
    std::wstring BuildAiChatPatchWarningHeadline(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const;
    std::wstring BuildAiChatPatchNextActionText(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const;
    std::wstring BuildAiChatPatchWarningText(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const;
    std::wstring BuildAiChatPatchViewText(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w) const;
    std::wstring BuildAiChatWorkflowStatusText(uint32_t chat_idx_u32) const;
    std::wstring BuildAiWorkflowOverviewText() const;
    std::wstring BuildAiWorkflowQueueText() const;
    std::wstring BuildAiChatWorkflowBucketText(uint32_t chat_idx_u32) const;
    uint32_t BuildAiChatWorkflowPriorityScore(uint32_t chat_idx_u32) const;
    std::vector<uint32_t> BuildAiWorkflowPriorityOrder() const;
    std::vector<uint32_t> BuildAiWorkflowBucketOrder(const std::wstring& bucket_w, uint32_t limit_u32 = AI_CHAT_MAX) const;
    int32_t FindAiHighestPriorityChat() const;
    int32_t FindAiWorkflowRank(uint32_t chat_idx_u32) const;
    std::wstring BuildAiChatTabLabelText(uint32_t chat_idx_u32) const;
    std::wstring BuildAiChatPrimaryActionLabel(uint32_t chat_idx_u32) const;
    std::wstring BuildAiChatPrimaryActionReasonText(uint32_t chat_idx_u32) const;
    uint32_t CountAiWorkflowBucket(const std::wstring& bucket_w) const;
    std::wstring BuildAiWorkflowBucketLeadText(const std::wstring& bucket_w, bool action_mode) const;
    void SetAiChatWorkflowEvent(uint32_t chat_idx_u32, const std::wstring& event_w, bool append_to_chat = false);
    void RefreshAiChatTabLabels();
    void SyncAiChatWorkflowState(uint32_t chat_idx_u32);
    void ActivateAiChat(uint32_t chat_idx_u32);
    void FocusAiHighestPriorityChat(bool execute_primary_action);
    void FocusAiWorkflowBucket(const std::wstring& bucket_w, bool execute_primary_action, const wchar_t* prefix_w);
    void ExecuteAiChatPrimaryAction(uint32_t chat_idx_u32);
    bool BufferAiChatDetectedDiff(uint32_t chat_idx_u32, bool append_feedback);
    void CaptureAiChatPatchScopeSnapshot(uint32_t chat_idx_u32);
    void AiChatShowPatchView(uint32_t chat_idx_u32, const std::wstring* apply_target_dir_w);
    bool AiChatApplyPatch(uint32_t chat_idx_u32);
    void AiChatPreviewPatch(uint32_t chat_idx_u32);
    void AiChatRename(uint32_t chat_idx, const std::wstring& new_title);
    void AiChatClose(uint32_t chat_idx);
    void RefreshAiPanelChrome();
    void RefreshAiChatProviderControls(uint32_t chat_idx_u32);
    void RefreshAiChatCortex(uint32_t chat_idx_u32);
    std::string ResolveAiChatApiModelUtf8(uint32_t chat_idx_u32) const;
    bool AiChatProviderRoutesNative(uint32_t chat_idx_u32) const;
    bool AiChatProviderRoutesApi(uint32_t chat_idx_u32) const;
    void LayoutAiPanelChildren();
    void AiPanelSetView(uint32_t view_u32);
    void RefreshAiRepoPane();
    void UpdateAiRepoSelection();
    void OpenSelectedAiRepoFile();
    bool SelectAiRepoPath(const std::string& rel_utf8, bool set_highlight);
    bool SelectAiCoherencePath(const std::string& rel_utf8, bool set_highlight);
    void RefreshAiCoherenceStats();
    void SetAiCoherenceResults(const std::vector<std::wstring>& lines_w, const std::vector<std::string>& paths_utf8, const std::string* preserve_rel_utf8 = nullptr);
    void UpdateAiCoherenceSelection();
    void SyncLiveModeProjection();
    void ApplyViewportLatticeProjectionFromControls(bool emit_status);
    void ApplyViewportVisualFxFromControls(bool emit_status);
    void ResetEditorSelectionLocal();
    void SetEditorSelectionLocal(uint64_t object_id_u64);
    void ToggleEditorSelectionLocal(uint64_t object_id_u64);

    bool live_mode_enabled_ = false;

    bool running_ = true;
    bool exit_requested_ = false;
    bool resized_ = false;
    int client_w_ = 0;
    int client_h_ = 0;
    int right_dock_width_px_ = 420;
    int right_dock_width_previous_px_ = 420;
    int right_dock_splitter_grab_dx_px_ = 0;
    bool right_dock_splitter_dragging_ = false;

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
    bool duplicate_drag_consumed_ = false;

    // Visualization toggle: when false, the app runs headless (no continuous presentation)
    // but simulation and verification continue.
    bool visualize_enabled_ = true;
    std::string visualization_fault_utf8_;
    bool confinement_particles_enabled_ = true;
    uint32_t viewport_projected_points_u32_ = 0u;
    uint32_t viewport_research_points_u32_ = 0u;
    uint32_t viewport_anchor_points_visible_u32_ = 0u;
    uint32_t viewport_anchor_points_hidden_u32_ = 0u;
    std::wstring output_log_path_w_;

// View modes
    bool immersion_mode_ = false; // Standard vs Immersion
    // Resonance viewport mode: render resonance carriers only (black background).
    // Toggle: ` (VK_OEM_3). Hold ` + mouse wheel to shift spectrum band.
    bool resonance_view_ = false;
    int  spectrum_band_i32_ = 0; // 0=visible baseline, +down toward sonar, -up toward UV/X.
    bool viewport_lattice_volume_mode_ = true;
    uint32_t viewport_lattice_slice_z_u32_ = 0u;
    uint32_t viewport_lattice_stride_u32_ = 2u;
    uint32_t viewport_lattice_max_points_u32_ = 32768u;
    uint8_t viewport_lattice_intensity_min_u8_ = 20u;
    bool viewport_vector_visualization_enabled_ = true;
    uint8_t viewport_vector_gain_u8_ = 78u;
    float viewport_field_depth_m_f32_ = 16.0f;
    float viewport_focal_length_mm_f32_ = 50.0f;
    float viewport_temporal_phase_f32_ = 0.0f;
    float spectrum_phase_f32_ = 0.0f; // deterministic phase for carrier visualization.
    float sequencer_scrub_t_f32_ = 0.0f;
    float eye_offset_local_[3] = {0.0f, 0.0f, 1.65f};

    // Win64 plumbing
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void CreateMainWindow(HINSTANCE hInst);
    void CreateChildWindows();
    void LayoutChildren(int w, int h);
    void SyncWindowMenu();
    int ResolveNextVisibleDockTab(int preferred_i32) const;
    void ApplyPrimaryWorkspaceProfile();
    void RefreshPrimaryWorkspaceTabs();
    void SetPrimaryWorkspaceTab(uint32_t idx_u32);
    void ApplyRightDockVisibility();
    void SetRightDockActiveTab(uint32_t idx_u32);
    void SetRightDockPanelVisible(uint32_t idx_u32, bool visible);

    void Tick();
    void Render();

    void OnSend();
    void OnImportObj();
    void OnBootstrapGame();
    void OnAiTrainModel();
    void ToggleAiPanel();
    void CreateAiPanelWindow();
    void UpdateAiPanel();
    void RefreshAiExperimentList();
    void RefreshAiDomainProgressList();
    void MarkAiExperimentsSeen();
    bool IsSimPlayEnabledCanonical() const;
    bool IsAiEnabledCanonical() const;
    bool IsAiLearningEnabledCanonical() const;
    bool IsAiCrawlingEnabledCanonical() const;
    bool IsRepoReaderEnabledCanonical() const;
    bool IsAiPanelDocked() const;
    bool IsAssistantPanelVisible() const;
    void FocusAiPanel(uint32_t view_u32);
    void SubmitToggleControlPacket(EwControlPacketKind kind, bool enabled);
    void SetAiStateCanonical(bool ai_enabled, bool learning_enabled, bool crawling_enabled);
    void SetRepoReaderEnabledCanonical(bool enabled);
    void RefreshAiToggleWidgets();
    void RefreshTopBarChrome();
    void ShowTopBarMenuForButton(uint32_t cmd_id_u32, HWND anchor_hwnd);
    void ShowTopBarWorkbenchMenu();
    void DisableVisualizationWithReason(const std::string& reason_utf8);
    void InitializeRenderer();
    bool InitializeRendererGuarded(unsigned long* out_exception_code);
    bool ExecuteFrameGuarded(unsigned long* out_exception_code);
    void RefreshPlaceActorsPanel();

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

    void SubmitAiChatTextUtf8(uint32_t chat_idx_u32, const std::string& utf8, bool append_user_line);
    void RequestChatGptResearchReply(uint32_t chat_idx_u32, const std::string& user_utf8, const std::string& model_utf8);
    std::string BuildChatGptReasoningInputUtf8(uint32_t chat_idx_u32, const std::string& user_utf8);
    void ExecuteExternalCommandUtf8(const std::string& line);
    void AppendOutputUtf8(const std::string& line);
};

} // namespace ewv
