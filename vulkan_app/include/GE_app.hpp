#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>

#include <utility>

#include "camera_controller.hpp"
#include "openxr_runtime.hpp"

namespace ewv {

struct AppConfig {
    std::string app_title_utf8;
    int initial_width = 1280;
    int initial_height = 720;
};

class App {
public:
    explicit App(const AppConfig& cfg);
    int Run(HINSTANCE hInst);

private:
    AppConfig cfg_;

    // Win64
    HWND hwnd_main_ = nullptr;
    HWND hwnd_viewport_ = nullptr;
    HWND hwnd_panel_ = nullptr;

    // Right dock (Unreal-style): tab strip + panel registry
    HWND hwnd_rdock_tab_ = nullptr;
    HWND hwnd_rdock_outliner_ = nullptr;
    HWND hwnd_rdock_details_ = nullptr;
    HWND hwnd_rdock_asset_ = nullptr;
    HWND hwnd_rdock_voxel_ = nullptr;
    HWND hwnd_rdock_node_ = nullptr;
    HWND hwnd_rdock_sequencer_ = nullptr;
    uint32_t rdock_tab_index_u32_ = 0u;
    bool rdock_panel_visible_[6] = {true, true, true, true, true, true};
    bool rdock_panel_locked_[6] = {false, false, false, false, false, false};

    // Tab-local toolbars (editor-grade feel). Pure UI; no core logic.
    HWND hwnd_tb_outliner_ = nullptr;
    HWND hwnd_tb_details_  = nullptr;
    HWND hwnd_tb_asset_    = nullptr;
    HWND hwnd_tb_voxel_    = nullptr;
    HWND hwnd_tb_node_     = nullptr;
    HWND hwnd_tb_sequencer_ = nullptr;

    // Toolbar controls
    HWND hwnd_outliner_search_ = nullptr;
    HWND hwnd_outliner_clear_  = nullptr;
    HWND hwnd_voxel_preset_    = nullptr;
    HWND hwnd_voxel_apply_     = nullptr;
    HWND hwnd_voxel_presets_list_ = nullptr;
    HWND hwnd_voxel_viewport_resonance_ = nullptr;
    HWND hwnd_voxel_atom_nodes_ = nullptr;
    HWND hwnd_voxel_summary_   = nullptr;
    int  voxel_atom_node_selected_i32_ = 0;
    HWND hwnd_asset_selected_  = nullptr;
    HWND hwnd_asset_gate_      = nullptr;
    HWND hwnd_asset_builder_status_ = nullptr;
    HWND hwnd_asset_review_refs_ = nullptr;
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
    HWND hwnd_seq_play_ = nullptr;
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

    void RebuildOutlinerList();
    void RebuildContentBrowserViews();
    void RefreshContentBrowserFromRuntime(uint32_t limit_u32);
    void RefreshNodePanel();
    void RefreshAssetDesignerPanel();
    void RefreshVoxelDesignerPanel();
    void SyncVoxelAtomNodeList();
    void RefreshViewportResonanceOverlay();
    void RefreshSequencerPanel();
    void RefreshContentBrowserChrome();
    void RefreshContent3DBrowserSurface();
    void SetContentBrowserViewMode(uint32_t mode_u32);
    bool SelectContentRelativePath(const std::string& rel_utf8);
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
    bool content_visible_ = true;

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

    // Selection mirror between list/thumb surfaces (tracks rel path).
    std::string content_selected_rel_utf8_;
    bool content_selection_sync_guard_ = false;
    std::string asset_last_review_rel_utf8_;
    uint64_t asset_last_review_revision_u64_ = 0ull;
    // Content view toggles + surfaces (List / Thumb / 3D stub).
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
    uint32_t content_view_mode_u32_ = 0; // 0=list,1=thumb,2=3d

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
    HWND hwnd_ai_toggle_learning_ = nullptr;
    HWND hwnd_ai_toggle_crawling_ = nullptr;
    // Chat controls
    HWND hwnd_ai_chat_list_ = nullptr;
    HWND hwnd_ai_chat_input_ = nullptr;
    HWND hwnd_ai_chat_send_ = nullptr;
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
    HWND hwnd_ai_coh_old_ = nullptr;
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
    void RefreshAiChatCortex(uint32_t chat_idx_u32);
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
    void ResetEditorSelectionLocal();
    void SetEditorSelectionLocal(uint64_t object_id_u64);
    void ToggleEditorSelectionLocal(uint64_t object_id_u64);

    bool sim_play_enabled_ = false;
    bool live_mode_enabled_ = false;
    bool ai_enabled_ = true;
    bool ai_learning_enabled_ = false;
    bool ai_crawling_enabled_ = false;
    bool ai_repo_reader_enabled_ = false;
    bool ai_safe_mode_enabled_ = false; // stub hook (menu-only; no behavior yet)

    bool running_ = true;
    bool resized_ = false;
    int client_w_ = 0;
    int client_h_ = 0;

    // Subsystems
    struct VkCtx;
    struct Scene;
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
    bool duplicate_drag_consumed_ = false;

    // Visualization toggle: when false, the app runs headless (no continuous presentation)
    // but simulation and verification continue.
    bool visualize_enabled_ = true;

// View modes
    bool immersion_mode_ = false; // Standard vs Immersion
    // Resonance viewport mode: render resonance carriers only (black background).
    // Toggle: ` (VK_OEM_3). Hold ` + mouse wheel to shift spectrum band.
    bool resonance_view_ = false;
    int  spectrum_band_i32_ = 0; // 0=visible baseline, +down toward sonar, -up toward UV/X.
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