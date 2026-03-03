#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include "ew_governor.hpp"
#include "anchor.hpp"
#include "ancilla_particle.hpp"
#include "delta_profiles.hpp"
#include "qubit_lanes.hpp"
#include "GE_object_memory.hpp"
#include "crawler_subsystem.hpp"
#include "GE_learning_checkpoint_gate.hpp"
#include "GE_learning_automation.hpp"
#include "GE_language_foundation.hpp"
#include "GE_math_foundation.hpp"
#include "GE_experiment_templates.hpp"
#include "GE_neural_phase_ai.hpp"

#include "GE_camera_anchor.hpp"

#include "GE_global_coherence.hpp"

#include "GE_phase_current.hpp"

#include "GE_corpus_allowlist.hpp"

#include "GE_domain_policy.hpp"
#include "GE_rate_limiter.hpp"
#include "GE_live_crawler.hpp"

#include "GE_project_settings.hpp"
#include "GE_control_packets.hpp"

#include "GE_curriculum_manager.hpp"
#include "GE_ai_policy.hpp"
#include "GE_ai_anticipation.hpp"
#include "ai_action_log.hpp"

#include "inspector_fields.hpp"
#include "coherence_gate.hpp"
#include "hydrator.hpp"
#include "anchor_pack.hpp"
#include "ew_id9.hpp"
#include "spec_aux_ops.hpp"

#include "field_lattice.hpp"


struct EwVizPoint {
    // Q16.16 positions for UE visualization.
    int32_t x_q16_16;
    int32_t y_q16_16;
    int32_t z_q16_16;
    uint32_t rgba8;
    uint32_t anchor_id;
};

// External API request/response packets emitted and consumed by the simulated
// substrate microprocessor. These packets carry only integration fields; the
// decision to emit them is part of substrate evolution.
struct EwExternalApiRequest {
    uint64_t request_id_u64 = 0;
    uint64_t tick_u64 = 0;
    std::string method_utf8;
    std::string url_utf8;
    std::string headers_kv_csv;
    std::vector<uint8_t> body_bytes;
    // Adapter-provided cap for response bytes.
    uint32_t response_cap_u32 = 0;
    // Substrate routing metadata (all scheduling decisions are in-substrate).
    uint32_t context_anchor_id_u32 = 0;
    uint32_t crawler_anchor_id_u32 = 0;
    uint32_t domain_anchor_id_u32 = 0;
};

struct EwExternalApiResponse {
    uint64_t request_id_u64 = 0;
    uint64_t tick_u64 = 0;
    int32_t http_status_s32 = -1;
    std::vector<uint8_t> body_bytes;
    // Routing metadata echoed from request.
    uint32_t context_anchor_id_u32 = 0;
    uint32_t crawler_anchor_id_u32 = 0;
    uint32_t domain_anchor_id_u32 = 0;
};


// Streaming ingest frames for large responses. The adapter may submit a response body
// incrementally without writing it to disk.
struct EwExternalApiIngestChunk {
    uint64_t request_id_u64 = 0;
    uint64_t tick_u64 = 0;
    int32_t http_status_s32 = -1;
    uint32_t context_anchor_id_u32 = 0;
    uint32_t crawler_anchor_id_u32 = 0;
    uint32_t domain_anchor_id_u32 = 0;
    uint32_t offset_u32 = 0;
    bool is_final = false;
    std::vector<uint8_t> bytes;
};

struct EwExternalApiIngestDoc {
    uint64_t request_id_u64 = 0;
    uint64_t tick_first_u64 = 0;
    int32_t http_status_s32 = -1;
    uint32_t context_anchor_id_u32 = 0;
    uint32_t crawler_anchor_id_u32 = 0;
    uint32_t domain_anchor_id_u32 = 0;
    bool final_seen = false;
    uint32_t expected_next_offset_u32 = 0;
    std::vector<uint8_t> bytes;
};

// Time-dilation parameters are part of the immutable anchor operator map.
// They are deterministically derived from projection_seed at init time.
struct EwTimeDilationParams {
    // Q32.32 factors.
    int64_t td_min_q32_32;
    int64_t td_max_q32_32;
    int64_t k_coh_q32_32;
    int64_t k_curv_q32_32;
    int64_t k_dop_q32_32;

    // Reference coherence (TURN_SCALE units) used to normalize chi_q.
    int64_t chi_ref_turns_q;

    // Normalization denominator for curvature/doppler (TURN_SCALE units).
    int64_t norm_turns_q;
};


// Manifest metadata for ingested artifacts (Blueprint 9.5).
struct EwManifestRecord {
    uint64_t artifact_id_u64 = 0;
    uint64_t request_id_u64 = 0;
    uint64_t tick_first_u64 = 0;
    uint64_t tick_final_u64 = 0;
    uint32_t context_anchor_id_u32 = 0;
    uint32_t crawler_anchor_id_u32 = 0;
    uint32_t domain_anchor_id_u32 = 0;

    std::string domain_utf8;
    std::string path_utf8;
    // Source organization / publisher string (UTF-8).
    std::string org_utf8;

    uint32_t doc_kind_u32 = 0;
    uint32_t retrieval_method_u32 = 0;
    uint32_t trust_class_u32 = 0;
    uint32_t license_hint_u32 = 0;
    bool trainable_admitted = false;

    // Deterministic sample text used for duplicate detection / fast scoring.
    // Lowercasing is ASCII-only; bytes outside ASCII are preserved.
    std::string sample_lc_utf8;
};

// Canonical state/inputs/ctx containers.
// These exist to implement the Blueprint-mandated candidate/accept/commit pipeline.
// The runtime still executes on CPU/GPU, but the ONLY permissible evolution is via
// deterministic operator application producing a candidate next state.


struct EwEnvelopeSample {
    // Read-path counters only (Spec 4.1.9 / Eq A.11.6.2.23).
    // Values are raw measurements provided by the integration boundary,
    // but ALL derived scalars are computed in the substrate microprocessor.
    uint64_t t_exec_ns_u64 = 0;
    uint64_t t_budget_ns_u64 = 0;

    uint64_t bytes_moved_u64 = 0;
    uint64_t bytes_budget_u64 = 0;

    uint32_t queue_backlog_u32 = 0;
    uint32_t queue_budget_u32 = 0;

    // Optional: kernel carrier frequency estimate for this window (Q32.32 Hz).
    // This is an execution envelope descriptor, not a sampled electrical signal.
    int64_t gpu_carrier_hz_q32_32 = 0;
};


struct EwInputs {
    // Pulses and modality displacements for the tick.
    std::vector<Pulse> inbound;

    // Read-path execution envelope sample for the tick (may be all zeros).
    EwEnvelopeSample envelope;

    // Pending modality displacements applied on this tick.
    int64_t pending_text_x_q = 0;
    int64_t pending_image_y_q = 0;
    int64_t pending_audio_z_q = 0;

    // Latest GPU pulse sample (raw readings; not derived factors).
    uint64_t gpu_pulse_freq_hz_u64 = 0;
    uint64_t gpu_pulse_freq_ref_hz_u64 = 1;
    uint32_t gpu_pulse_amp_u32 = 0;
    uint32_t gpu_pulse_amp_ref_u32 = 1;

    // Optional raw pulse voltage sample (for spatial discrimination budget).
    uint32_t gpu_pulse_volt_u32 = 0;
    uint32_t gpu_pulse_volt_ref_u32 = 1;
};

struct EwCtx {
    // Immutable per-tick context snapshot.
    int64_t frame_gamma_turns_q = 0;
    EwTimeDilationParams td_params;

    int32_t weights_q10[9];
    int64_t denom_q[9];

    int64_t sx_q32_32 = (1LL << 32);
    int64_t sy_q32_32 = (1LL << 32);
    int64_t sz_q32_32 = (1LL << 32);

    int64_t hubble_h0_q32_32 = 0;
    int64_t tick_dt_seconds_q32_32 = 0;
    int64_t boundary_scale_step_q32_32 = (1LL << 32);

    // Boundary scale is stateful; this is the current value at tick start.
    int64_t boundary_scale_q32_32 = (1LL << 32);

    // Per-tick effective constants (derived inside substrate microprocessor).
    EwEffectiveConstantsQ32_32 eff;

    // Envelope headroom scalar derived from read-path counters, Q32.32 in [0,1].
    int64_t envelope_headroom_q32_32 = (1LL << 32);

    // Carrier safety governor parameters (copied from SubstrateManager each tick).
    EwGovernorParams governor;

    // -----------------------------------------------------------------
    // Canonical pulse-delta sampling and phase-anchor extraction
    // -----------------------------------------------------------------
    // tau_delta is expressed as Q0.15 fraction of the tick interval.
    // Example: 0.25 => 8192.
    uint16_t tau_delta_q15 = 8192;

    // Anchor extraction parameters (all deterministic, fixed-point).
    // theta_ref is in TURN_SCALE units.
    int64_t theta_ref_turns_q = 0;

    // A_ref is Q32.32 (dimensionless). alpha_A maps ln(A/A_ref) into turns.
    int64_t A_ref_q32_32 = (1LL << 32);
    int64_t alpha_A_turns_q32_32 = (int64_t)((1LL << 32) / 8); // 0.125 turns per ln-unit

    // Optional coupling: dtheta += kappa_lnA * dlnA.
    int64_t kappa_lnA_turns_q32_32 = (int64_t)((1LL << 32) / 32); // 0.03125 turns per ln-unit

    // Optional coupling: dtheta += kappa_lnF * dlnFq.
    // NOTE: This is the Blueprint/Spec "kappa_f" term expressed as ln(|f|) ratio delta.
    int64_t kappa_lnF_turns_q32_32 = (int64_t)((1LL << 32) / 64); // 0.015625 turns per ln-unit

    // Coherence gate for dt_star output (TURN_SCALE units).
    int64_t coherence_cmin_turns_q = (TURN_SCALE / 20);

    // dt_star output model parameters (omega0 in turns/sec, Q32.32).
    int64_t omega0_turns_per_sec_q32_32 = (1LL << 32); // 1 turn/sec
    int64_t kappa_rho_q32_32 = (1LL << 30);            // 0.25

    // Delta encoding profile defaults (Spec 3.6/3.7). Inbound pulses may override.
    uint8_t default_profile_id = EW_PROFILE_CORE_EVOLUTION;
    uint16_t mode_bucket_size = 64;

    // -----------------------------------------------------------------
    // 14.2 Causality and closed-system enforcement bounds
    // -----------------------------------------------------------------
    // Injection is interpreted as a constraint selector only. These bounds
    // ensure no unconstrained phase or ledger drift can be committed.
    int64_t max_dtheta_turns_q = (TURN_SCALE / 4); // 0.25 turns
    int64_t max_paf_turns_q = (TURN_SCALE / 8);    // 0.125 turns
    int64_t max_abs_ln_q32_32 = (3LL << 32);       // ~3.0
    int64_t max_abs_dt_star_seconds_q32_32 = (2LL << 32); // 2 seconds

    // -----------------------------------------------------------------
    // Omega.3 / Omega.4: Carrier metric + constrained projection Pi_G
    // -----------------------------------------------------------------
    // Diagonal carrier metric entries (Q32.32). Default is identity.
    int64_t carrier_g_q32_32[9] = {
        (1LL << 32), (1LL << 32), (1LL << 32), (1LL << 32), (1LL << 32),
        (1LL << 32), (1LL << 32), (1LL << 32), (1LL << 32)
    };

    // -----------------------------------------------------------------
    // A.18 Dispatcher constraints for ancilla updates
    // -----------------------------------------------------------------
    int64_t pulse_current_max_mA_q32_32 = 0;
    int64_t phase_max_displacement_q32_32 = 0;
    int64_t phase_orbital_displacement_unit_mA_q32_32 = 0;
    int64_t gradient_headroom_mA_q32_32 = 0;
    uint64_t temporal_envelope_ticks_u64 = 0;
};


// -----------------------------------------------------------------------------
//  ΩA Operator Packet + Lane Store (Equations appendix ΩA)
// -----------------------------------------------------------------------------
// Packed operator templates executed inside the simulated substrate.

static constexpr size_t EW_ANCHOR_OP_PACKED_V1_BYTES = 1500;

// 9D lane identity used for addressing E9 values and buffers.
struct EwLaneId9 {
    int64_t v[9];
};

inline bool ew_lane_id9_equal(const EwLaneId9& a, const EwLaneId9& b) {
    for (int i = 0; i < 9; ++i) if (a.v[i] != b.v[i]) return false;
    return true;
}

struct EwAnchorOpPackedV1Bytes {
    uint8_t bytes[EW_ANCHOR_OP_PACKED_V1_BYTES];
};

struct EwOpLaneEntry {
    EwLaneId9 lane_id;
    bool is_buffer = false;
    Basis9 scalar_e9;
    std::vector<Basis9> buffer_e9;
};

struct EwCmbBath {
    int64_t reservoir_mass_q63 = 0;
    int64_t reservoir_energy_q63 = 0;
    int64_t leakage_accum_q63 = 0;
    int64_t entropy_accum_q63 = 0;
};

struct EwState {
    uint64_t canonical_tick = 0;
    int64_t reservoir = 0;
    EwCmbBath cmb_bath;
    uint64_t dark_mass_q63_u64 = 0;
    int64_t boundary_scale_q32_32 = (1LL << 32);
    std::vector<Anchor> anchors;
    std::vector<ancilla_particle> ancilla;
    std::vector<EwQubitLane> lanes;
    EwObjectStore object_store;

    // Materials calibration gate propagated from the substrate.
    bool materials_calib_done = false;

    // -----------------------------------------------------------------
    // Carrier governor dwell accumulators (deterministic, fixed-point).
    // These prevent sustained operation near the event-horizon / critical-mass caps
    // by forcing cooldown even when hard caps are not exceeded.
    // -----------------------------------------------------------------
    int64_t gov_dwell_tau_q32_32 = 0;
    int64_t gov_dwell_inv_q32_32 = 0;
};

struct EwLedger {
    int64_t reservoir_q = 0;
    int64_t total_mass_q = 0;
    int64_t total_mass_plus_res_q = 0;
    int64_t total_energy_q = 0;
    int64_t total_momentum_q = 0;
};

struct EwLedgerDelta {
    int64_t reservoir_delta = 0;
    int64_t total_mass_delta = 0;
};

// Deterministic 64-bit LCG (no std::random, no platform variance).
class EwLcg64 {
public:
    uint64_t state;
    explicit EwLcg64(uint64_t seed) : state(seed ? seed : 1ULL) {}

    inline uint64_t next_u64() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    }

    inline int64_t next_i64_mod(int64_t mod) {
        if (mod <= 0) return 0;
        uint64_t x = next_u64();
        return static_cast<int64_t>(x % static_cast<uint64_t>(mod));
    }
};

class SubstrateManager {
public:
    uint64_t canonical_tick = 0;
    int64_t reservoir = 0;

    // -----------------------------------------------------------------
    // Project settings (loaded at boot, deterministic, snapshot-included)
    // -----------------------------------------------------------------
    EwProjectSettings project_settings;

    // -----------------------------------------------------------------
    // Camera sensor sample (world->camera sampling only)
    // The viewport submits these read-only samples to the substrate.
    // -----------------------------------------------------------------
    struct EwCameraSensorSample {
        int64_t median_depth_m_q32_32 = (int64_t)(5) * (1ll<<32);
        uint64_t tick_u64 = 0;
    } camera_sensor;

    // -----------------------------------------------------------------
    // Projected render packets (substate -> renderer). These are computed
    // inside the substrate tick, not in the renderer.
    // -----------------------------------------------------------------
    EwRenderCameraPacket render_camera_packet;
    uint64_t render_camera_packet_tick_u64 = 0;

    // -----------------------------------------------------------------
    // Control packet inbox (UI/editor/input -> AI substrate)
    // -----------------------------------------------------------------
    static constexpr uint32_t CONTROL_INBOX_CAP = 512;
    EwControlPacket control_inbox[CONTROL_INBOX_CAP];
    uint32_t control_inbox_head_u32 = 0;
    uint32_t control_inbox_count_u32 = 0;

    bool control_packet_push(const EwControlPacket& p);
    bool control_packet_pop(EwControlPacket& out);

    // -----------------------------------------------------------------
    // Pending compute-bus requests (hard no-direct-compute rule)
    // Control packets never directly compute derived results; they only
    // stage intent that is consumed by compute-bus operator dispatch.
    // -----------------------------------------------------------------
    struct EwPendingSettingsSet {
        uint32_t valid_u32 = 0;
        uint64_t tick_u64 = 0;
        uint16_t source_u16 = 0;
        uint32_t tab_u32 = 0;
        uint32_t field_u32 = 0;
        int64_t value_q32_32 = 0;
    } pending_settings_set;

    struct EwPendingInputEvent {
        uint32_t valid_u32 = 0;
        uint64_t tick_u64 = 0;
        uint16_t source_u16 = 0;
        uint16_t kind_u16 = 0; // 1 action, 2 axis
        uint32_t id_u32 = 0;   // raw input code (key/button/axis)
        int32_t value_q16_16 = 0; // axis value or 1/0 for action
        uint8_t pressed_u8 = 0;
    } pending_input_event;

    struct EwPendingBindingSet {
        uint32_t valid_u32 = 0;
        uint64_t tick_u64 = 0;
        uint8_t is_axis_u8 = 0;
        uint8_t pad_u8[3] = {0,0,0};
        uint32_t raw_id_u32 = 0;
        uint32_t mapped_u32 = 0;
        int32_t scale_q16_16 = (int32_t)(1 * 65536);
    } pending_binding_set;

    bool input_bindings_dirty = false;

    // Project settings revision (bumps on apply). Used for deterministic
    // outward projection and snapshot inclusion.
    uint64_t project_settings_revision_u64 = 0;

    // -----------------------------------------------------------------
    // Deterministic input bindings (raw OS codes -> semantic actions)
    // Loaded from project_settings.input.bindings_path_utf8.
    // -----------------------------------------------------------------
    enum class EwMappedInputAction : uint32_t {
        None = 0,
        MoveForward = 1,
        MoveBackward = 2,
        MoveLeft = 3,
        MoveRight = 4,
        MoveUp = 5,
        MoveDown = 6,
        ZoomIn = 7,
        ZoomOut = 8,
        LookYaw = 10,
        LookPitch = 11,
    };

    struct EwInputBinding {
        uint32_t raw_id_u32 = 0;
        uint32_t mapped_u32 = 0; // EwMappedInputAction
        int32_t scale_q16_16 = (int32_t)(1 * 65536);
    };

    std::vector<EwInputBinding> input_action_bindings;
    std::vector<EwInputBinding> input_axis_bindings;
    bool input_bindings_loaded = false;

    bool load_input_bindings_if_needed(std::string* out_err);
    bool save_input_bindings_if_dirty(std::string* out_err);

    // Canonical camera anchor id.
    uint32_t camera_anchor_id_u32 = 0;

    uint32_t alloc_anchor_id();


    EwCmbBath cmb_bath;
    // Non-projecting dark excitation accumulator (Blueprint dark sink rule).
    uint64_t dark_mass_q63_u64 = 0;
    // Deterministic decoherence control surface.
    // This represents a global measurement-frame mismatch (in TURN_SCALE units)
    // applied to the phase/angle axis during projection. It does not transfer
    // energy; it is a deterministic frame offset.
    int64_t frame_gamma_turns_q = 0;
    // -----------------------------------------------------------------
    // OPK bound-operator state (packet executor internal state)
    // -----------------------------------------------------------------
    uint64_t last_tick_hysteresis_u64 = 0;
    bool opk_runtime_evolution_requested = false;

    // Last per-tick context snapshot used by operator packets and deterministic bindings.
    EwCtx ctx_snapshot;

    // Compatibility surface: some subsystems reference sm->ctx as the live
    // per-tick context. This is kept byte-identical to ctx_snapshot.
    EwCtx ctx;

    // ΩA operator packet execution surface (Equations appendix ΩA).
    std::vector<EwAnchorOpPackedV1Bytes> operator_packets_v1;
    std::vector<EwOpLaneEntry> op_lanes;

    // Latest read-path envelope sample (provided by adapter; derived scalars computed in substrate).
    EwEnvelopeSample envelope_sample;

    // Latest GPU pulse sample (raw readings; derived scaling computed in substrate).
    uint64_t gpu_pulse_freq_hz_u64 = 0;
    uint64_t gpu_pulse_freq_ref_hz_u64 = 1;
    uint32_t gpu_pulse_amp_u32 = 0;
    uint32_t gpu_pulse_amp_ref_u32 = 1;

    uint32_t gpu_pulse_volt_u32 = 0;
    uint32_t gpu_pulse_volt_ref_u32 = 1;

    // -----------------------------------------------------------------
    // Kernel-ancilla event ring (Option 3): kernel launches are treated as
    // ancilla update events. Events carry only raw pulse scalars; all derived
    // factors and phase proposals are computed inside the substrate.
    // -----------------------------------------------------------------
    struct EwKernelAncillaEvent {
        uint64_t tick_u64 = 0;
        uint32_t anchor_id_u32 = 0; // 0 => global, otherwise anchor id
        uint32_t lane_u32 = 0;      // optional lane id (0 when unused)

        uint64_t freq_hz_u64 = 0;
        uint64_t freq_ref_hz_u64 = 1;
        uint32_t amp_u32 = 0;
        uint32_t amp_ref_u32 = 1;
        uint32_t volt_u32 = 0;
        uint32_t volt_ref_u32 = 1;
    };

    static constexpr uint32_t KERNEL_EVENT_CAP = 256;
    EwKernelAncillaEvent kernel_events[KERNEL_EVENT_CAP];
    uint32_t kernel_event_head_u32 = 0;
    uint32_t kernel_event_count_u32 = 0;

    void kernel_event_push_global(uint64_t tick_u64,
                                  uint64_t freq_hz_u64, uint64_t freq_ref_hz_u64,
                                  uint32_t amp_u32, uint32_t amp_ref_u32,
                                  uint32_t volt_u32, uint32_t volt_ref_u32);
    void kernel_events_consume_for_tick(uint64_t tick_u64);

    // -----------------------------------------------------------------
    // Topology + identity transition state (Spec 4.1.8 MERGE/SPLIT routing)
    // -----------------------------------------------------------------
    std::vector<uint32_t> redirect_to;
    std::vector<uint32_t> split_child_a;
    std::vector<uint32_t> split_child_b;
    uint32_t next_anchor_id_u32 = 1;


    std::vector<Anchor> anchors;

    // Canonical mutable runtime state holders (Equations A.18).
    // One ancilla particle per anchor.
    std::vector<ancilla_particle> ancilla;

    // Blueprint 14.3: qubit lane substrate state.
    std::vector<EwQubitLane> lanes;

    // Blueprint C: Object store (immutable entries, referenced by anchors/lanes).
    EwObjectStore object_store;
    EwLanePolicy lane_policy;
    std::vector<Pulse> outbound;
    std::vector<Pulse> inbound;

    // -----------------------------------------------------------------
    // Global pulse carrier ring (fan-out propagation)
    // -----------------------------------------------------------------
    // The substrate emits outbound pulses each tick. To achieve global
    // per-tick updating through the pulse network (without viewport gating),
    // we retain a bounded snapshot of the last emitted carrier pulses and
    // re-admit them deterministically as inbound pulses on the next tick.
    // This forms the global "carrier hum" path.
    std::vector<Pulse> carrier_ring;

    // Fan-out bound for a single pulse within one tick (prevents runaway).
    // Max neighborhood fan-out for lattice injections.
    // If 0, derive per-tick from governor and current headroom (no arbitrary defaults).
    uint32_t pulse_fanout_max_u32 = 0u;

    // -----------------------------------------------------------------
    // GPU lattice authority mode
    // -----------------------------------------------------------------
    // When enabled, the CUDA field lattice is treated as the authoritative
    // per-tick evolution target for the global world substrate. Pulses are
    // merged into deterministic injection commands, uploaded to the lattice,
    // and the viewport is expected to read out projection slices from the
    // evolved lattice state.
    bool gpu_lattice_authoritative = true;

    // Readout helper for viewport/debug: returns a BGRA8 radiance slice.
    // Safe to call even when GPU lattice is disabled (returns empty).
    void lattice_get_radiance_slice_bgra8(uint32_t slice_z, std::vector<uint8_t>& out_bgra8, EwFieldFrameHeader& out_hdr);

    // External API integration queue. The substrate emits requests; the adapter
    // executes them and submits responses back to the substrate.
    bool pop_external_api_request(EwExternalApiRequest& out_req);
    void submit_external_api_response(const EwExternalApiResponse& resp);

    // Streaming ingest path: adapter may submit response bodies in chunks.
    // Returns true if accepted; false indicates backpressure (caller should pause/retry).
    bool submit_external_api_response_chunk(
        uint64_t request_id_u64,
        uint64_t tick_u64,
        int32_t http_status_s32,
        uint32_t context_anchor_id_u32,
        uint32_t crawler_anchor_id_u32,
        uint32_t domain_anchor_id_u32,
        const uint8_t* bytes,
        uint32_t bytes_len_u32,
        uint32_t offset_u32,
        bool is_final
    );

    // Manifest store for ingested corpus artifacts (provenance + license tagging).
    std::vector<EwManifestRecord> manifest_records;

    // Deterministic corpus query over derived corpus text. Emits a query result artifact.
    void corpus_query_emit_results(const std::string& query_utf8, uint32_t context_anchor_id_u32);

    // Deterministic corpus query summary: returns the best match score (0..255).
    uint32_t corpus_query_best_score(const std::string& query_utf8);

    // Deterministic extractive answer generator over derived corpus text.
    // Emits Draft Container/Corpus/answer_<tick>.txt and queues UI lines.
    void corpus_answer_emit(const std::string& query_utf8, uint32_t context_anchor_id_u32);


    int32_t weights_q10[9];
    int64_t denom_q[9];

    uint64_t projection_seed = 1;

    // Monotonic request id generator. Deterministic given tick order.
    uint64_t external_api_request_seq_u64 = 1;

    // Default response cap for adapter-executed external API calls.
    // This bounds the per-request response buffer in the adapter.
    // The substrate also enforces ingest_max_doc_bytes_u32 as a hard ceiling.
    uint32_t external_api_default_response_cap_u32 = (256u * 1024u);


    // Web browsing state (substrate-resident, deterministic).
    // Populated when a search results page is ingested.
    // OPEN:<n> uses these to fetch an indexed result without re-searching.
    //
    // Tuning:
    // - websearch_max_results_u32 bounds result extraction from a provider page.
    // - websearch_ui_emit_n_u32 bounds how many are echoed to the UI channel.
    // - websearch_auto_fetch_n_u32 bounds how many top results are fetched automatically.
    //
    // Defaults: show 10, extract up to 30, auto-fetch 10.
    uint32_t websearch_max_results_u32 = 30u;
    uint32_t websearch_ui_emit_n_u32 = 10u;
    uint32_t websearch_auto_fetch_n_u32 = 10u; // 0 disables auto-fetch.
    uint64_t last_websearch_results_request_id_u64 = 0u;
    // UTF-8 url/title arrays extracted from the most recent ingested search page.
    std::vector<std::string> last_websearch_urls_utf8;
    std::vector<std::string> last_websearch_titles_utf8;

    // Immutable time-dilation operator parameters (derived from projection_seed).
    EwTimeDilationParams td_params;

    // Pulse sampling and anchor extraction parameters.
    uint16_t tau_delta_q15 = 8192; // 0.25 tick
    int64_t theta_ref_turns_q = 0;
    int64_t A_ref_q32_32 = (1LL << 32);
    int64_t alpha_A_turns_q32_32 = (int64_t)((1LL << 32) / 8);
    int64_t kappa_lnA_turns_q32_32 = (int64_t)((1LL << 32) / 32);
    int64_t kappa_lnF_turns_q32_32 = (int64_t)((1LL << 32) / 64);
    int64_t coherence_cmin_turns_q = (TURN_SCALE / 20);
    int64_t omega0_turns_per_sec_q32_32 = (1LL << 32);
    int64_t kappa_rho_q32_32 = (1LL << 30);

    // Dispatcher constraints for ancilla updates (Equations A.18).
    // pulse_current_max_mA sets the maximum allowable current.
    // phase_max_displacement sets the phase displacement mapped to pulse_current_max.
    // phase_orbital_displacement_unit is the measurable minimum current unit.
    int64_t pulse_current_max_mA_q32_32 = (int64_t)(50LL << 32);
    int64_t phase_max_displacement_q32_32 = (int64_t)(1LL << 32);
    int64_t phase_orbital_displacement_unit_mA_q32_32 = (int64_t)(1LL << 20);
    int64_t gradient_headroom_mA_q32_32 = (int64_t)(1LL << 21);
    uint64_t temporal_envelope_ticks_u64 = 1024;
    // -----------------------------------------------------------------
    // Genesis Engine carrier safety governor (deterministic)
    // -----------------------------------------------------------------
    EwGovernorParams governor;


    // Canonical axis scaling factors (Q32.32).
    // sx: x-axis scale driver (TEXT)
    // sy: y-axis scale driver (IMAGE)
    // sz: z-axis scale driver (AUDIO)
    int64_t sx_q32_32 = (1LL << 32);
    int64_t sy_q32_32 = (1LL << 32);
    int64_t sz_q32_32 = (1LL << 32);

    // Pending external API events to be executed by adapter.
    std::deque<EwExternalApiRequest> external_api_pending;

    // Inflight request metadata for deterministic response processing (no adapter parsing).
    struct EwExternalApiInflight {
        uint64_t request_id_u64 = 0;
        uint64_t tick_u64 = 0;
        uint32_t session_idx_u32 = 0;
        uint32_t stage_u32 = 0;
        uint32_t profile_u32 = 0;
        std::string url_utf8;
        std::string host_utf8;
        std::string path_utf8;
        uint32_t context_anchor_id_u32 = 0;
        uint32_t crawler_anchor_id_u32 = 0;
        uint32_t domain_anchor_id_u32 = 0;
    };
    std::deque<EwExternalApiInflight> external_api_inflight;

    // Streaming ingest buffers for large external responses.
    std::deque<EwExternalApiIngestChunk> external_api_ingest_inbox;
    std::deque<EwExternalApiIngestDoc> external_api_ingest_docs;
    uint64_t external_api_ingest_inflight_bytes_u64 = 0;

    // Ingest throttles (all enforced inside substrate microprocessor).
    uint32_t ingest_max_inflight_bytes_u32 = (8u * 1024u * 1024u);
    uint32_t ingest_max_doc_bytes_u32 = (2u * 1024u * 1024u);
    uint32_t ingest_max_bytes_per_tick_u32 = (256u * 1024u);

    // Global cosmological expansion parameters (Q32.32).
    // The simulation boundary expands at the Hubble rate.
    // a(t) evolves as: a_{k+1} = a_k * exp(H0 * dt), with dt in seconds.
    // This is a boundary/metric expansion factor that scales the 3D spatial
    // manifold metric used for visualization and any spatial-domain operators.
    int64_t hubble_h0_q32_32 = 0;                 // H0 reference in 1/seconds (Q32.32); evolution uses ctx.eff.hubble_h0_eff_q32_32
    int64_t tick_dt_seconds_q32_32 = 0;           // dt per tick in seconds (Q32.32)
    int64_t boundary_scale_q32_32 = (1LL << 32);  // a(t) starts at 1.0
    int64_t boundary_scale_step_q32_32 = (1LL << 32); // exp(H0*dt)

    // Pending modality displacements applied on next tick.
    // These are deterministic, accumulated, and then cleared on tick.
    int64_t pending_text_x_q = 0;
    int64_t pending_image_y_q = 0;
    int64_t pending_audio_z_q = 0;

    // Spec Section 5: crawler/encoder are inside the substrate.
    // These values bound crawler segmentation and admission per tick.
    uint32_t crawler_max_pulses_per_tick_u32 = 64;
    // Live crawling is disabled by default and requires explicit consent.
    uint32_t crawler_enable_live_u32 = 0u;
    uint32_t crawler_live_consent_required_u32 = 1u;

    uint32_t crawler_max_bytes_per_segment_u32 = 2048;

    // Chunk-stream emission (crawler -> pulse stream) controls.
    // When enabled and the learning backlog is below the threshold, the crawler
    // emits both a page-level carrier pulse and a bounded stream of chunk-level
    // pulses per observation.
    uint32_t crawler_emit_chunk_stream_u32 = 1u;
    // If 0, derive from the learning window (no arbitrary constant).
    uint32_t crawler_chunk_stream_backlog_threshold_u32 = 0u;
    // If 0, derive chunk size from staging/segment budgets (no arbitrary constant).
    uint32_t crawler_chunk_bytes_u32 = 0u;
    // If 0, derive from max pulses per tick and chunk size (no arbitrary constant).
    uint32_t crawler_max_chunks_per_obs_u32 = 0u;
    // Presentation-aware throttling: by default, chunk-stream pulses are only
    // emitted in headless visualization mode. This prevents high-frequency
    // pulse streams from competing with interactive rendering.
    // Set to 1 to allow chunk streams while visible.
    uint32_t crawler_allow_chunk_stream_when_visible_u32 = 0u;

    // -----------------------------------------------------------------
    // Derived (non-arbitrary) crawler/learning budgets
    // -----------------------------------------------------------------
    // These are computed strictly from configured parameters and tick rates.
    // They avoid embedding magic constants in subsystem logic.

    inline uint64_t derived_crawler_concat_cap_bytes_u64() const {
        // Cap the per-tick concatenated observation buffer by the maximum number
        // of pulse admissions times the configured chunk size. This represents
        // the staging budget that the GPU encoder can amortize per tick.
        const uint64_t pulses = (crawler_max_pulses_per_tick_u32 == 0u) ? 1ull : (uint64_t)crawler_max_pulses_per_tick_u32;
        const uint64_t cb = (uint64_t)derived_crawler_chunk_bytes_u32();
        return pulses * cb;
    }

    inline uint32_t derived_learning_backlog_limit_u32() const {
        // Backlog is bounded by one request window worth of pulse admissions.
        const uint64_t pulses = (crawler_max_pulses_per_tick_u32 == 0u) ? 1ull : (uint64_t)crawler_max_pulses_per_tick_u32;
        const uint64_t ticks = (learning_ticks_per_tcp_request_u32 == 0u) ? 1ull : (uint64_t)learning_ticks_per_tcp_request_u32;
        const uint64_t lim = pulses * ticks;
        return (lim > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)lim;
    }

    // -----------------------------------------------------------------
    // Learning checkpoint gate (honesty gate)
    // -----------------------------------------------------------------
    // The learning loop performs best-fit form-fitting against measurable
    // targets during the one-second window available per per-domain request
    // cadence. Budget is expressed in conceptual "object updates" (tries).
    // These tries are consumed in batched steps to map to fan-out expansion.
    uint64_t learning_max_object_updates_per_request_u64 = 6500000000000ULL; // 6.5T conceptual tries
    uint32_t learning_ticks_per_tcp_request_u32 = 360u; // 1 second window at 360 Hz
    // If 0, derive from per-tick pulse and fan-out budgets.
    uint32_t learning_tries_per_step_u32 = 0u;
    // If 0, derive from tick dt and safety governor.
    uint32_t learning_max_steps_per_tick_u32 = 0u;
    // Learning probe evolution: number of micro-ticks to evolve the probe lattice
    // per engine tick while fitting. This is the "parameters mold the lattice"
    // mechanism and is kept small and deterministic.
    // Learning probe micro-ticks per engine tick.
    // If 0, derive per-tick from temporal envelope and governor (no arbitrary defaults).
    uint32_t learning_probe_micro_ticks_per_engine_tick_u32 = 0u;

    // Derived fan-out max based on available headroom.
    uint32_t derived_pulse_fanout_max_u32(const EwCtx& ctx) const;

    // Derived crawler chunk bytes.
    uint32_t derived_crawler_chunk_bytes_u32() const;

    // Derived max chunks per observation.
    uint32_t derived_crawler_max_chunks_per_obs_u32() const;

    // Derived chunk-stream backlog threshold.
    uint32_t derived_crawler_chunk_stream_backlog_threshold_u32() const;

    // Derived learning tries per step.
    uint32_t derived_learning_tries_per_step_u32(const EwCtx& ctx) const;

    // Derived max learning steps per tick.
    uint32_t derived_learning_max_steps_per_tick_u32(const EwCtx& ctx) const;

    // Derived learning micro-tick budget.
    uint32_t derived_learning_probe_micro_ticks_u32(const EwCtx& ctx) const;

    // Crawler subsystem state.
    CrawlerSubsystem crawler;

    // Corpus/live crawl policy tables (deterministic, 9D IDs)
    GE_DomainPolicyTable domain_policies;
    GE_RateLimiter rate_limiter;
    GE_LiveCrawler live_crawler;

    // Curriculum manager drives lane scheduling and stage advancement.
    GE_CurriculumManager curriculum;


    // Learning checkpoint gate state.
    genesis::LearningCheckpointGate learning_gate;

    // Learning automation scheduler (curriculum-driven parallel tasks)
    genesis::LearningAutomation learning_automation;

    // Phase-amplitude current rail (9D region current field)
    genesis::EwPhaseCurrent phase_current;
    // Stage-0 language foundations (dictionary/thesaurus/encyclopedia/speech).
    genesis::LanguageFoundation language_foundation;

    // Stage-0 math foundations (learned in parallel with language).
    genesis::MathFoundation math_foundation;

    // -----------------------------------------------------------------
    // Materials calibration gating
    // -----------------------------------------------------------------
    // Object imports are rejected until calibration tasks for materials
    // have been accepted by the learning gate at Stage>=2.
    bool materials_calib_done = false;
    uint64_t materials_calib_done_tick_u64 = 0;



    // Active crawl routing ids (created in substrate on crawl start).
    uint32_t active_crawl_context_anchor_id_u32 = 0;
    uint32_t active_crawl_syllabus_anchor_id_u32 = 0;
    uint32_t active_crawl_crawler_anchor_id_u32 = 0;

    struct EwDomainAnchorMapEntry {
        std::string domain_utf8;
        uint32_t domain_anchor_id_u32 = 0;
        // Politeness scheduling (ticks in canonical tick space).
        uint64_t next_allowed_tick_u64 = 0;
        // Token bucket in Q16.16 tokens; 1 token == one request.
        uint32_t tokens_q16_16 = (1u << 16);
        // Observed metric/topic mask inferred from ingested content (GPU keyword scan).
        uint64_t observed_topic_mask_u64 = 0ULL;
    };
    std::vector<EwDomainAnchorMapEntry> active_crawl_domains;

// -----------------------------------------------------------------
// Corpus crawl planner (viewport-triggered, adapter-executed; substrate scheduled)
// -----------------------------------------------------------------
struct EwCorpusCrawlTarget {
    uint32_t lane_u32;
    uint32_t stage_u32; // 0=robots, 1=root, 2=sitemap, 3=downloadable_doc
    uint32_t profile_u32; // 0=general, 1=publisher_textbook, 2=uspto_patent, 3=uspto_trademark, 4=usco_copyright
    std::string host_utf8;
    std::string path_utf8; // begins with '/' (or empty -> default per stage)
};

struct EwCrawlSession {
    bool active = false;
    uint32_t profile_u32 = 0;

    uint32_t context_anchor_id_u32 = 0;
    uint32_t syllabus_anchor_id_u32 = 0;
    uint32_t crawler_anchor_id_u32 = 0;

    std::vector<EwDomainAnchorMapEntry> domain_map;
    std::deque<EwCorpusCrawlTarget> q;

    // Deterministic dedupe (bounded; linear scan). Stores "https://host/path" keys.
    std::vector<std::string> seen_url_keys;
};

static constexpr uint32_t EW_LEARNING_SANDBOX_MAX = 6;
static constexpr uint32_t EW_CRAWL_SESSION_MAX = 4;
EwCrawlSession crawl_sessions[EW_CRAWL_SESSION_MAX];
    uint32_t crawl_rr_u32 = 0;

// Canonical corpus allowlist (parsed deterministically). Used both for crawl scheduling
// and for admission filtering when observations arrive.
GE_CorpusAllowlist corpus_allowlist;
bool corpus_allowlist_loaded = false;

    // User-updatable allowlist (optional). If present, it overrides the embedded default.
    // Stored as a corpus artifact and mirrored to disk for persistence.
    std::string corpus_allowlist_user_md_utf8;
    bool corpus_allowlist_user_loaded = false;

    // Corpus pipeline scheduling knobs (offline-first)
    uint32_t corpus_pipeline_enable_u32 = 1u;
    uint32_t corpus_epoch_period_ticks_u32 = 600u;

uint32_t corpus_crawl_max_pending_u32 = 8;
uint32_t corpus_crawl_max_emit_per_tick_u32 = 2;

// Politeness: default 1 request/sec/domain in canonical tick space (360 ticks/sec).
uint64_t crawl_politeness_req_interval_ticks_u64 = 360ULL;
// Curriculum stage gating for crawling (parallel within stage; monotonic stage progression).
// Curriculum stage gating for crawling (parallel within stage; monotonic stage progression).
// Stages (canonical):
// 0=Language foundations (dictionary/thesaurus/encyclopedia/speech)
// 1=Physics & quantum physics
// 2=Chemistry + orbitals/atoms/bonds foundations
// 3=Materials / physical sciences
// 4=Cosmology / atmospheres
// 5=Biology (locked until stage4 is mastered)
uint32_t learning_curriculum_stage_u32 = 0u;

// Deterministic stage advancement based on validated measurable checkpoints.
// Cursor into MetricRegistry completed list so we only process each task once.
uint32_t learning_completed_cursor_u32 = 0u;
// Accepted metric counts by stage (0..3); biology stage unlock is terminal.
// Curriculum stage advancement is driven by a deterministic checklist of
// required measurable MetricKinds, not by a raw count. Each stage declares
// a required bitmask over MetricKind IDs (see metric_kind_to_bit()) and the
// learning gate sets bits on acceptance.
//
// Stages: 0=QM, 1=Orbitals/Atoms/Bonds+Chem, 2=Materials, 3=Cosmology/Atmos,
// 4=Biology (deferred; usually locked until environments are learned).
static constexpr uint32_t GENESIS_CURRICULUM_STAGE_COUNT = 6;

// Bitmasks of required / completed checkpoint kinds.
uint64_t learning_stage_required_mask_u64[GENESIS_CURRICULUM_STAGE_COUNT] = {0,0,0,0,0};
uint64_t learning_stage_completed_mask_u64[GENESIS_CURRICULUM_STAGE_COUNT] = {0,0,0,0,0};

// Legacy counter remains for telemetry only.

// Visualization mode: headless disables continuous presentation but keeps
// verification snapshots and metric validation alive.
bool visualization_headless = false;

    // -----------------------------------------------------------------
    // Simulation snapshot + loop/live injection
    // -----------------------------------------------------------------
    bool sim_snapshot_valid = false;
    EwState sim_snapshot;
    bool sim_play_loop_enabled = false;
    uint64_t sim_play_loop_start_tick_u64 = 0;
    // For loop playback, we restore the snapshot before evolution each tick.
    // For live injection, we restore once and then disable loop.

    bool sim_save_to_file(const std::string& name_ascii);
    bool sim_load_from_file(const std::string& name_ascii, bool play_loop);
// Upper bound on allowlist lane accepted at current curriculum stage.
uint32_t crawl_allowlist_lane_max_u32 = 1u; // stage0(language) allows lanes 0-1 by default

// UI output queue (text and voice-text share the same channel).
static constexpr uint32_t UI_OUT_CAP = 256;
std::deque<std::string> ui_out_q;

    // Global coherence aggregator (q15). Gates *all* AI outputs/actions.
    genesis::GE_GlobalCoherence global_coherence;

    // Deterministic record of which object ids were advanced by the canonical
    // object ancilla update this tick. Used to drive bounded object→world
    // writeback without scanning all objects.
    std::vector<uint64_t> object_updates_last_tick_u64;

    // Threshold below which AI must fail-closed (ask/plan/quiet) rather than answer.
    uint16_t global_coherence_gate_min_q15 = 8192; // 0.25 default

    // UI chat message ring buffer (deterministic, bounded).
    static constexpr uint32_t UI_CHAT_CAP = 20;
    std::deque<std::string> ui_chat_q;
    // Index of the most recent chat anchor created from UI input.
    uint32_t ui_last_chat_anchor_idx_u32 = 0u;
    // Index of the current UI training anchor for live crawler targeting.
    uint32_t ui_livecrawl_target_anchor_idx_u32 = 0u;
    // Anchor indices flagged as UI_PARTITION (kept separate from sim/core intent).
    std::vector<uint32_t> ui_anchor_indices;

    // Create a UI chat anchor (UI_PARTITION|CHAT_MESSAGE) from a line of text.
    uint32_t ui_create_chat_anchor_from_text(const std::string& utf8_line);
    // Create a UI-derived concept/training anchor from the latest chat seed.
    uint32_t ui_create_anchor_from_chat_seed(uint32_t chat_anchor_idx_u32, uint16_t resonance_q15);
    // Extract deterministic crawl seeds from text and enqueue to live crawler when allowed.
    void ui_maybe_enqueue_crawl_seeds_from_text(const std::string& utf8_line);

    // Emit one line to the UI output channel (deterministic, bounded).
    void emit_ui_line(const std::string& utf8_line);

    // Allowlist update surface (UI command + config file). Deterministic validation.
    bool corpus_allowlist_update_from_user_text(const std::string& allowlist_md_utf8);
    bool corpus_allowlist_load_user_file_if_present();

    // Spec/Blueprint: neural phase dynamics controller.
    EwNeuralPhaseAI neural_ai;

    // Spec/Blueprint: deterministic classification -> operator -> action policy.
    EwAiPolicyTable ai_policy;

    // -----------------------------------------------------------------
    // AI anticipation (deterministic intent routing)
    // -----------------------------------------------------------------
    // When enabled, non-command user lines are deterministically routed to
    // QUERY:/WEBSEARCH:/WEBFETCH:/OPEN: based on local corpus grounding and
    // lexical cues. This is a "predictive programming" layer that stays
    // fully deterministic and testable.
    EwAiAnticipator ai_anticipator;
    bool ai_anticipation_enabled = true;
    bool ai_anticipation_auto_execute = true;   // rewrite line before observe_text_line
    bool ai_anticipation_emit_ui = true;        // emit AI_ANTICIPATE UI line

    // -----------------------------------------------------------------
    // AI Interface Layer (Blueprint Appendix BE/BF): command buffer
    // -----------------------------------------------------------------
    EwAiCommand ai_commands_fixed[EW_AI_COMMAND_MAX];
    uint32_t ai_commands_count_u32 = 0;
    int64_t ai_pulse_q63 = 0;  // aggregate command carrier amplitude
    int64_t ai_total_weight_q63 = 0;

    // Submit fixed-array commands (BF.3A). Unused slots are ignored.
    void submit_ai_commands_fixed(const EwAiCommand* cmds, uint32_t count_u32);


    // Spec/Blueprint: observable action log (fixed-size ring).
    static constexpr uint32_t AI_ACTION_LOG_CAP = 4096;
    EwAiActionEvent ai_action_log[AI_ACTION_LOG_CAP];
    uint32_t ai_action_log_head_u32 = 0;
    uint32_t ai_action_log_count_u32 = 0;

    // Blueprint engineering design: inspector fields (workspace artifacts)
    // exist inside the substrate as vector-field state.
    EwInspectorFields inspector_fields;

    // -----------------------------------------------------------------
    // AnchorPack: super-compressed process artifacts encoded into carrier
    // harmonics and installed as anchors for substrate-native processing.
    // -----------------------------------------------------------------
    std::vector<EigenWare::AnchorPackRecord> anchor_pack_records;

    // -----------------------------------------------------------------
    // Anchored code synthesis index (symbol->artifact relevance)
    // -----------------------------------------------------------------
    // This is a substrate-resident, deterministic cache derived from
    // inspector_fields. It is *not* a filesystem index.
    struct EwSynthArtifactInfo {
        std::string rel_path;
        uint32_t kind_u32 = 0;
    };
    // Symbol reference keyed purely by 9D identity (no hashing / no token ids).
    struct EwSynthSymRef {
        EwId9 sym_id9{};
        uint32_t art_index_u32 = 0;
        uint16_t weight_q15 = 0;
    };
    uint64_t synth_index_revision_u64 = 0;
    std::vector<EwSynthArtifactInfo> synth_artifacts;
    std::vector<EwSynthSymRef> synth_sym_refs;
    

    // Latest human-readable observation line (e.g., from headless tools).
    // Stored inside the substrate so phase operators can emit inspector artifacts
    // without relying on a filesystem reader.
    std::string last_observation_text;

    // Last hydration receipt (observable). Hydration is a deterministic
    // projection step from inspector fields into functioning files.
    EwHydrationReceipt last_hydration_receipt;
    uint32_t last_hydration_error_code_u32 = 0;

    // Omega.3 carrier metric (diagonal, Q32.32). Default identity.
    int64_t carrier_g_q32_32[9] = {
        (1LL << 32), (1LL << 32), (1LL << 32), (1LL << 32), (1LL << 32),
        (1LL << 32), (1LL << 32), (1LL << 32), (1LL << 32)
    };

    explicit SubstrateManager(size_t count);

    // Record an AI action event deterministically (ring buffer).
    void ai_log_event(const EwAiActionEvent& e);

    // Read AI action log snapshot (oldest->newest). Returns number copied.
    uint32_t ai_get_action_log(EwAiActionEvent* out_events, uint32_t max_events) const;

    // Deterministic inbound pulse admission used by simulated subsystems.
    void enqueue_inbound_pulse(const Pulse& p);

    // Canonical tick getter for subsystems.
    uint64_t canonical_tick_u64() const { return canonical_tick; }

    // Crawler enqueue: external worlds submit observations; crawler runs inside.
    void crawler_enqueue_observation_utf8(
        uint64_t artifact_id_u64,
        uint32_t stream_id_u32,
        uint32_t extractor_id_u32,
        uint32_t trust_class_u32,
        uint32_t causal_tag_u32,
        const std::string& domain_ascii,
        const std::string& url_ascii,
        const std::string& utf8
    );

    // Corpus allowlist pointer for subsystems (may be null if not loaded).
    const GE_CorpusAllowlist* corpus_allowlist_ptr() const;

    // Canonical headless observation entry point.
    // Writes into substrate memory, then routes the line through the crawler
    // with deterministic labels so the AI loop can act on it.
    void observe_text_line(const std::string& utf8_line);

// -----------------------------------------------------------------
// UI interaction (adapter-driven I/O; computation occurs in substrate)
// -----------------------------------------------------------------
void ui_submit_user_text_line(const std::string& utf8_line);
// Pop one UI output message into out_utf8 (returns true if one was available).
bool ui_pop_output_text(std::string& out_utf8);

// -----------------------------------------------------------------
// Deterministic export
// -----------------------------------------------------------------
// Exports a deterministic bundle for an object:
//  - Standard shell mesh topology (ewmesh)
//  - UV atlas variable map (raw rgba8 + json meta)
//  - One-keyframe material animation json (for clip association)
bool export_object_bundle(uint64_t object_id_u64,
                          const std::string& out_dir_utf8,
                          std::string* out_report_utf8) const;

// -----------------------------------------------------------------
// Corpus crawl control (button press consent)
// -----------------------------------------------------------------
// Parse an allowlist markdown/text and schedule crawl targets deterministically.
// The substrate only schedules requests; the adapter executes them via the external API callback.
void corpus_crawl_start_from_allowlist_text(const std::string& allowlist_md_utf8);
    // Start crawl using the embedded Neuralis corpus allowlist compiled into the runtime.
    // This keeps button-press usage simple: no external text needs to be passed.
    void corpus_crawl_start_neuralis_corpus_default();
void corpus_crawl_stop();

    // -----------------------------------------------------------------
    // Language foundations bootstrap (explicit user action)
    // -----------------------------------------------------------------
    // Loads dictionary/thesaurus/encyclopedia/speech corpus files from a
    // directory on disk (host machine), parses them deterministically,
    // and enqueues measurable checkpoint tasks that gate the curriculum.
    // Returns true if at least one dataset was loaded.
    bool language_bootstrap_from_dir(const std::string& root_dir_utf8);


    // Initialize immutable operator parameters from projection_seed.
    // Allowed only when canonical_tick == 0.
    void set_projection_seed(uint64_t seed);
    // Same behavior as set_projection_seed(), named to reflect that the value is derived from viewport content.
    void set_projection_viewport_basis(uint64_t basis_u64);

    // Configure global expansion from the Hubble constant.
    // This MUST be called before the first tick for the expansion to be active.
    // h0_q32_32 is H0 in 1/seconds (Q32.32). dt_seconds_q32_32 is tick dt.
    void configure_cosmic_expansion(int64_t h0_q32_32, int64_t dt_seconds_q32_32);

    void submit_pulse(const Pulse& p);
    void submit_envelope_sample(const EwEnvelopeSample& s);

    // GPU pulse sample (raw readings). Substrate derives pulse factors and axis scales.
    void submit_gpu_pulse_sample(uint64_t freq_hz_u64, uint64_t freq_ref_hz_u64,
                                uint32_t amp_u32, uint32_t amp_ref_u32);


    // GPU pulse sample including an optional voltage channel (raw readings).
    void submit_gpu_pulse_sample_v2(uint64_t freq_hz_u64, uint64_t freq_ref_hz_u64,
                                    uint32_t amp_u32, uint32_t amp_ref_u32,
                                    uint32_t volt_u32, uint32_t volt_ref_u32);


    // Load an AnchorOpPacked_v1 record (exact byte layout, 1500 bytes).
    // Stored in substrate memory and may be executed each tick by the microprocessor.
    void submit_operator_packet_v1(const uint8_t* bytes, size_t bytes_len);

    Basis9 projected_for(const Anchor& a) const;

    void tick();

    // Modality injection API (viewport / dispatcher calls).
    void inject_text_utf8(const char* utf8);
    void inject_image_pixels_u8(const uint8_t* rgba, int width, int height);
    void inject_audio_pcm16(const int16_t* pcm, int samples, int channels);

    // Build a visualization point set from the current anchor basis.
    // This is projection-only: it does not modify simulation state.
    void build_viz_points(std::vector<EwVizPoint>& out) const;

    // Compile a deterministic experiment template from a user text line and
    // submit the resulting opcode program as operator packets.
    // Returns true if the line was recognized as an experiment request.
    bool compile_and_submit_experiment_from_text(const std::string& utf8_line);

    // Programmatic entry point used by curriculum loops.
    // Compiles a request into operator packets and submits them.
    bool compile_and_submit_experiment(const EigenWare::EwExperimentRequest& req);

    // Enable/disable lattice projection tags used by build_viz_points().
    // Projection is read-only and draws from the authoritative GPU lattice.
    void set_lattice_projection_tag(uint32_t slice_z_u32,
                                    uint32_t stride_u32,
                                    uint32_t max_points_u32,
                                    uint32_t intensity_min_u8,
                                    bool enabled);

    // Extended lattice projection tag with lattice selection (0=world,1=probe).
    void set_lattice_projection_tag_ex(uint32_t lattice_sel_u32,
                                       uint32_t slice_z_u32,
                                       uint32_t stride_u32,
                                       uint32_t max_points_u32,
                                       uint32_t intensity_min_u8,
                                       bool enabled);

    bool check_invariants() const;

    // Deterministic hydration projection: projects commit-ready inspector
    // artifacts into a functioning file workspace.
    // Returns true on success.
    bool hydrate_workspace_to(const std::string& root_dir);

    // Read-only AI status for visualization and host integration.
    const EwAiStatus& ai_status() const { return neural_ai.status(); }

    // Internal lattice accessors for GPU-driven learning/sandbox.
    // These return pointers to GPU lattice objects; callers must not mutate
    // state directly except via lattice APIs.
    EwFieldLatticeGpu* world_lattice_gpu_for_learning();
    EwFieldLatticeGpu* probe_lattice_gpu_for_learning(uint32_t sandbox_id_u32 = 0u);

private:
    std::unique_ptr<EwFieldLatticeGpu> lattice_gpu_;
    std::unique_ptr<EwFieldLatticeGpu> lattice_probe_gpu_[EW_LEARNING_SANDBOX_MAX];
    void ensure_lattice_gpu_();
    void apply_object_imprint_writeback_();
    void ensure_lattice_probe_gpu_();

    struct EwLatticeProjectionTag {
        bool enabled = false;
        uint32_t lattice_sel_u32 = 0; // 0=world, 1=probe
        uint32_t slice_z_u32 = 0;
        uint32_t stride_u32 = 2;
        uint32_t max_points_u32 = 50000;
        uint32_t intensity_min_u8 = 8;
    };
    EwLatticeProjectionTag lattice_proj_tag_{};
};
