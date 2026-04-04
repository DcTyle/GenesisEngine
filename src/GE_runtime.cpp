#include "GE_runtime.hpp"
#include "ew_kv_params.hpp"

#include "GE_experiment_templates.hpp"
#include "GE_operator_registry.hpp"
#include "cmb_bath.hpp"
#include <cstddef>
#include <climits>
#include <cmath>
#include <cstring>
#include <algorithm>

#include "text_encoder.hpp"
#include "bytes_encoder.hpp"
#include "canonical_ops.hpp"
#include "fixed_point.hpp"
#include "learning_gate_cuda.hpp"
#include "crawler_encode_cuda.hpp"
#include "symbol_tokenize_cuda.hpp"
#include "code_artifact_ops.hpp"
#include "ew_eq_exec.h"
#include <map>
#include "substrate_alu.hpp"
#include "substrate_harmonics.hpp"
#include "ew_coherence_analyzer.hpp"

#include "GE_shell_mesh.hpp"
#include "GE_object_ancilla.hpp"
#include "GE_planet_ancilla.hpp"
#include "GE_pulse_trajectory_bound.hpp"

#include "GE_fourier_fanout.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

static inline int64_t q32_32_mul(int64_t a_q32_32, int64_t b_q32_32) {
    // (a*b)>>32, no rounding.
    __int128 p = (__int128)a_q32_32 * (__int128)b_q32_32;
    return (int64_t)(p >> 32);
}

static inline int64_t ge_clamp_i64_local(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline uint16_t ge_ratio_q15_u64(uint64_t num, uint64_t den) {
    if (den == 0u) return (num != 0u) ? 32767u : 0u;
    __int128 s = (__int128)num << 15;
    uint64_t q = (uint64_t)(s / (__int128)den);
    if (q > 32767u) q = 32767u;
    return (uint16_t)q;
}

static inline uint16_t ge_headroom_q15_from_used_u32(uint32_t used_u32, uint32_t cap_u32) {
    const uint16_t sat = ge_ratio_q15_u64((uint64_t)used_u32, (uint64_t)((cap_u32 != 0u) ? cap_u32 : 1u));
    return (sat >= 32767u) ? 0u : (uint16_t)(32767u - sat);
}

static inline uint16_t ge_mean_q15_u16(const uint16_t* v, uint32_t n) {
    if (!v || n == 0u) return 0u;
    uint64_t sum = 0u;
    for (uint32_t i = 0u; i < n; ++i) sum += (uint64_t)v[i];
    uint64_t q = sum / (uint64_t)n;
    if (q > 32767u) q = 32767u;
    return (uint16_t)q;
}

static inline uint16_t ge_mean_abs_diff_q15_u16(const uint16_t* a, const uint16_t* b, uint32_t n) {
    if (!a || !b || n == 0u) return 0u;
    uint64_t sum = 0u;
    for (uint32_t i = 0u; i < n; ++i) {
        const int32_t d = (int32_t)a[i] - (int32_t)b[i];
        sum += (uint64_t)((d < 0) ? -d : d);
    }
    uint64_t q = sum / (uint64_t)n;
    if (q > 32767u) q = 32767u;
    return (uint16_t)q;
}

static inline int64_t ge_q15_error_to_q32_32(uint16_t measured_q15, uint16_t intent_q15) {
    return ((int64_t)measured_q15 - (int64_t)intent_q15) << 17;
}

static inline uint16_t ge_abs_q32_32_to_q15_sat(int64_t v_q32_32) {
    uint64_t mag = (uint64_t)((v_q32_32 < 0) ? -v_q32_32 : v_q32_32);
    uint64_t q = mag >> 17;
    if (q > 32767u) q = 32767u;
    return (uint16_t)q;
}

static inline int64_t ge_q15_to_q32_32(uint16_t v_q15) {
    if (v_q15 >= 32767u) return (1LL << 32);
    return (int64_t)(((__int128)v_q15 << 32) / 32767);
}

static inline uint16_t ge_bool_to_q15(bool on) {
    return on ? 32767u : 0u;
}

static inline uint16_t ge_abs_i16_q15_to_q15_sat(int32_t v_q1_15) {
    int32_t mag = (v_q1_15 < 0) ? -v_q1_15 : v_q1_15;
    if (mag > 32767) mag = 32767;
    return (uint16_t)mag;
}

static inline uint16_t ge_q16_16_ratio_to_q15_sat(int32_t v_q16_16, int32_t full_scale_q16_16) {
    if (full_scale_q16_16 <= 0) return 0u;
    uint64_t mag = (uint64_t)((v_q16_16 < 0) ? -(int64_t)v_q16_16 : (int64_t)v_q16_16);
    uint64_t den = (uint64_t)full_scale_q16_16;
    if (mag > den) mag = den;
    return ge_ratio_q15_u64(mag, den);
}

static inline uint16_t ge_q32_32_ratio_to_q15_sat(int64_t v_q32_32, int64_t full_scale_q32_32) {
    if (full_scale_q32_32 <= 0) return 0u;
    uint64_t mag = (uint64_t)((v_q32_32 < 0) ? -(int64_t)v_q32_32 : v_q32_32);
    uint64_t den = (uint64_t)full_scale_q32_32;
    if (mag > den) mag = den;
    return ge_ratio_q15_u64(mag, den);
}

static inline uint32_t ge_abs_i32_u32_local(int32_t v) {
    return (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
}

static inline uint16_t ge_full_u16_to_q15(uint16_t v_u16) {
    return (uint16_t)(((uint32_t)v_u16) >> 1);
}

static inline uint16_t ge_code_u16_to_q15(uint16_t code_u16, uint32_t max_code_u32) {
    return ge_ratio_q15_u64((uint64_t)code_u16,
                            (uint64_t)((max_code_u32 != 0u) ? max_code_u32 : 1u));
}

static inline uint32_t ge_popcount_u64_local(uint64_t v) {
    uint32_t n = 0u;
    while (v != 0u) {
        n += (uint32_t)(v & 1u);
        v >>= 1u;
    }
    return n;
}

static uint16_t ge_meta_tensor_norm_q15(const genesis::GeMetaFrequencyTensor& tensor) {
    const uint16_t mags_q15[4] = {
        ge_full_u16_to_q15(tensor.dft[0].magnitude_q15),
        ge_full_u16_to_q15(tensor.dft[1].magnitude_q15),
        ge_full_u16_to_q15(tensor.dft[2].magnitude_q15),
        ge_full_u16_to_q15(tensor.dft[3].magnitude_q15)
    };
    return ge_mean_q15_u16(mags_q15, 4u);
}

static bool ge_encode_spidercode4_from_record_input(const std::string& type_utf8,
                                                    const std::string& metadata_utf8,
                                                    const std::vector<uint8_t>& value_bytes,
                                                    SpiderCode4& out_sc) {
    const std::string normalized_type_utf8 = normalize_text(type_utf8);
    const std::string normalized_metadata_utf8 = normalize_text(metadata_utf8);
    const std::string merged_meta_utf8 = normalized_type_utf8 + "\n" + normalized_metadata_utf8;

    std::vector<uint8_t> merged_bytes;
    merged_bytes.reserve(merged_meta_utf8.size() + 1u + value_bytes.size());
    merged_bytes.insert(merged_bytes.end(), merged_meta_utf8.begin(), merged_meta_utf8.end());
    merged_bytes.push_back(0u);
    merged_bytes.insert(merged_bytes.end(), value_bytes.begin(), value_bytes.end());
    if (merged_bytes.empty()) return false;
    return ew_encode_spidercode4_from_bytes_chunked_cuda(merged_bytes.data(),
                                                         merged_bytes.size(),
                                                         256u,
                                                         &out_sc);
}

static void ge_update_ai_gpu_spider_drive(SubstrateManager& sm, EwAiDataSubstrateTelemetry& out) {
    int32_t last_f_code_i32 = 0;
    uint16_t last_a_code_u16 = 0;
    uint16_t last_v_code_u16 = 0;
    uint16_t last_i_code_u16 = 0;
    if (sm.ai_action_log_count_u32 > 0u) {
        const uint32_t idx =
            (sm.ai_action_log_head_u32 + SubstrateManager::AI_ACTION_LOG_CAP - 1u) %
            SubstrateManager::AI_ACTION_LOG_CAP;
        const EwAiActionEvent& e = sm.ai_action_log[idx];
        last_f_code_i32 = e.f_code_i32;
        last_a_code_u16 = (uint16_t)((e.a_code_u32 > 65535u) ? 65535u : e.a_code_u32);
        last_v_code_u16 = (uint16_t)((e.v_code_u32 > 65535u) ? 65535u : e.v_code_u32);
        last_i_code_u16 = (uint16_t)((e.i_code_u32 > 65535u) ? 65535u : e.i_code_u32);
    }

    const uint16_t freq_norm_q15 =
        (sm.gpu_pulse_freq_hz_u64 != 0u && sm.gpu_pulse_freq_ref_hz_u64 != 0u)
            ? ge_ratio_q15_u64(sm.gpu_pulse_freq_hz_u64, sm.gpu_pulse_freq_ref_hz_u64)
            : ge_ratio_q15_u64((uint64_t)ge_abs_i32_u32_local(last_f_code_i32),
                               (uint64_t)((F_SCALE != 0) ? F_SCALE : 1));
    const uint16_t amp_norm_q15 =
        (sm.gpu_pulse_amp_ref_u32 != 0u && sm.gpu_pulse_amp_u32 != 0u)
            ? ge_ratio_q15_u64((uint64_t)sm.gpu_pulse_amp_u32, (uint64_t)sm.gpu_pulse_amp_ref_u32)
            : ge_code_u16_to_q15(last_a_code_u16, 65535u);
    const uint16_t volt_norm_q15 =
        (sm.gpu_pulse_volt_ref_u32 != 0u && sm.gpu_pulse_volt_u32 != 0u)
            ? ge_ratio_q15_u64((uint64_t)sm.gpu_pulse_volt_u32, (uint64_t)sm.gpu_pulse_volt_ref_u32)
            : ge_code_u16_to_q15(last_v_code_u16, V_MAX);
    const uint16_t ampere_norm_q15 =
        (sm.process_substrate_telemetry.valid_u32 != 0u && sm.process_substrate_telemetry.last_i_code_u16 != 0u)
            ? ge_code_u16_to_q15(sm.process_substrate_telemetry.last_i_code_u16, I_MAX)
            : ge_code_u16_to_q15(last_i_code_u16, I_MAX);

    const int32_t dims_i32[EW_AI_GPU_GRADIENT_DIM_CAP] = {
        (int32_t)freq_norm_q15,
        (int32_t)amp_norm_q15,
        (int32_t)volt_norm_q15,
        (int32_t)ampere_norm_q15,
        (int32_t)amp_norm_q15 - (int32_t)freq_norm_q15,
        (int32_t)volt_norm_q15 - (int32_t)freq_norm_q15,
        (int32_t)ampere_norm_q15 - (int32_t)freq_norm_q15,
        (int32_t)volt_norm_q15 - (int32_t)amp_norm_q15,
        (int32_t)ampere_norm_q15 - (int32_t)amp_norm_q15,
        (int32_t)ampere_norm_q15 - (int32_t)volt_norm_q15
    };

    out.gpu_gradient_dim_q15[0] = freq_norm_q15;
    out.gpu_gradient_dim_q15[1] = amp_norm_q15;
    out.gpu_gradient_dim_q15[2] = volt_norm_q15;
    out.gpu_gradient_dim_q15[3] = ampere_norm_q15;
    for (uint32_t i = 4u; i < EW_AI_GPU_GRADIENT_DIM_CAP; ++i) {
        out.gpu_gradient_dim_q15[i] = (uint16_t)ge_abs_i32_u32_local(dims_i32[i]);
    }
    out.gpu_gradient_energy_norm_q15 = ge_mean_q15_u16(&out.gpu_gradient_dim_q15[4], 6u);

    std::vector<uint8_t> payload;
    payload.resize(EW_AI_GPU_GRADIENT_DIM_CAP * sizeof(int32_t), 0u);
    for (uint32_t i = 0u; i < EW_AI_GPU_GRADIENT_DIM_CAP; ++i) {
        const uint32_t off = i * 4u;
        const uint32_t u = (uint32_t)dims_i32[i];
        payload[off + 0u] = (uint8_t)(u & 0xFFu);
        payload[off + 1u] = (uint8_t)((u >> 8) & 0xFFu);
        payload[off + 2u] = (uint8_t)((u >> 16) & 0xFFu);
        payload[off + 3u] = (uint8_t)((u >> 24) & 0xFFu);
    }

    std::ostringstream meta;
    meta << "domain=ai_substrate\n"
         << "kind=gpu_spider_10d\n"
         << "layout=F,A,V,I,FA,FV,FI,AV,AI,VI\n"
         << "tick=" << sm.canonical_tick << "\n"
         << "freq_raw=" << sm.gpu_pulse_freq_hz_u64 << "\n"
         << "freq_ref=" << sm.gpu_pulse_freq_ref_hz_u64 << "\n"
         << "amp_raw=" << sm.gpu_pulse_amp_u32 << "\n"
         << "amp_ref=" << sm.gpu_pulse_amp_ref_u32 << "\n"
         << "volt_raw=" << sm.gpu_pulse_volt_u32 << "\n"
         << "volt_ref=" << sm.gpu_pulse_volt_ref_u32 << "\n"
         << "ampere_code=" << sm.process_substrate_telemetry.last_i_code_u16 << "\n";

    if (sm.ai_substrate_drive.put_bytes("ai/gpu_spider_10d",
                                        "gpu_spider_10d",
                                        meta.str(),
                                        payload.data(),
                                        payload.size())) {
        sm.ai_gpu_spider_encode_count_u64 += 1u;
    }

    SpiderCode4 spider_sc{};
    if (ge_encode_spidercode4_from_record_input("gpu_spider_10d", meta.str(), payload, spider_sc)) {
        out.gpu_spider_f_code_i32 = spider_sc.f_code;
        out.gpu_spider_a_code_u16 = spider_sc.a_code;
        out.gpu_spider_v_code_u16 = spider_sc.v_code;
        out.gpu_spider_i_code_u16 = spider_sc.i_code;
    }

    genesis::GeVirtualStateRecord record{};
    if (sm.ai_substrate_drive.get_record("ai/gpu_spider_10d", record)) {
        if (out.gpu_spider_f_code_i32 == 0) out.gpu_spider_f_code_i32 = record.tensor.channels.f_code;
        if (out.gpu_spider_a_code_u16 == 0u) out.gpu_spider_a_code_u16 = record.tensor.channels.a_code;
        if (out.gpu_spider_v_code_u16 == 0u) out.gpu_spider_v_code_u16 = record.tensor.channels.v_code;
        if (out.gpu_spider_i_code_u16 == 0u) out.gpu_spider_i_code_u16 = record.tensor.channels.i_code;
        out.gpu_spider_carrier_phase_u64 = record.tensor.carrier_phase_u64;
    }
    if (out.gpu_spider_f_code_i32 == 0 && out.gpu_gradient_energy_norm_q15 != 0u) {
        int32_t signed_gradient_sum_i32 = 0;
        for (uint32_t i = 4u; i < EW_AI_GPU_GRADIENT_DIM_CAP; ++i) {
            signed_gradient_sum_i32 += dims_i32[i];
        }
        int32_t fallback_f_code_i32 = signed_gradient_sum_i32 / 256;
        if (fallback_f_code_i32 == 0) {
            fallback_f_code_i32 = (dims_i32[6] >= dims_i32[9]) ? 1 : -1;
        }
        out.gpu_spider_f_code_i32 = fallback_f_code_i32;
    }
}

static void ge_update_ai_api_decode_drive(SubstrateManager& sm, EwAiDataSubstrateTelemetry& out) {
    out.api_decode_request_id_u64 = sm.ai_last_api_decode_request_id_u64;
    out.api_decode_http_status_s32 = sm.ai_last_api_decode_http_status_s32;
    out.api_decode_count_u16 = (uint16_t)((sm.ai_api_decode_count_u64 > 65535u) ? 65535u : sm.ai_api_decode_count_u64);
    out.api_decode_body_bytes_u16 =
        (uint16_t)((sm.ai_last_api_decode_body_bytes_u32 > 65535u) ? 65535u : sm.ai_last_api_decode_body_bytes_u32);
    out.api_decode_http_status_q15 =
        (sm.ai_last_api_decode_http_status_s32 >= 200 && sm.ai_last_api_decode_http_status_s32 < 400) ? 32767u : 0u;

    genesis::GeVirtualStateRecord record{};
    if (!sm.ai_substrate_drive.get_record("ai/api_decode/last", record)) return;

    SpiderCode4 spider_sc{};
    if (ge_encode_spidercode4_from_record_input(record.type_utf8, record.metadata_utf8, record.value_bytes, spider_sc)) {
        out.api_decode_f_code_i32 = spider_sc.f_code;
        out.api_decode_a_code_u16 = spider_sc.a_code;
        out.api_decode_v_code_u16 = spider_sc.v_code;
        out.api_decode_i_code_u16 = spider_sc.i_code;
    } else {
        out.api_decode_f_code_i32 = record.tensor.channels.f_code;
        out.api_decode_a_code_u16 = record.tensor.channels.a_code;
        out.api_decode_v_code_u16 = record.tensor.channels.v_code;
        out.api_decode_i_code_u16 = record.tensor.channels.i_code;
    }
    out.api_decode_norm_q15 = ge_meta_tensor_norm_q15(record.tensor);
    if (out.api_decode_f_code_i32 == 0 && out.api_decode_norm_q15 != 0u) {
        const int32_t signed_api_carrier_i32 =
            (int32_t)out.api_decode_a_code_u16 +
            (int32_t)out.api_decode_v_code_u16 -
            (int32_t)out.api_decode_i_code_u16;
        out.api_decode_f_code_i32 = (signed_api_carrier_i32 >= 0) ? 1 : -1;
    }
}

static const EwSubsystemCalculusLane* ge_find_prev_subsystem_lane(
    const EwSubsystemSubstrateTelemetry& telemetry,
    uint32_t subsystem_id_u32) {
    for (uint32_t i = 0u; i < telemetry.subsystem_count_u32 &&
                          i < EW_SUBSYSTEM_SUBSTRATE_LANE_CAP; ++i) {
        if (telemetry.lanes[i].subsystem_id_u32 == subsystem_id_u32) {
            return &telemetry.lanes[i];
        }
    }
    return nullptr;
}

static const char* ge_subsystem_plane_name_ascii(uint32_t subsystem_id_u32) {
    switch (subsystem_id_u32) {
        case EW_SUBSYSTEM_PLANE_SPECTRAL_PROCESS: return "spectral_process";
        case EW_SUBSYSTEM_PLANE_CAMERA: return "camera";
        case EW_SUBSYSTEM_PLANE_RENDER: return "render";
        case EW_SUBSYSTEM_PLANE_ASSET_OBJECT: return "asset_object";
        case EW_SUBSYSTEM_PLANE_NBODY: return "nbody";
        case EW_SUBSYSTEM_PLANE_CURRICULUM: return "curriculum";
        case EW_SUBSYSTEM_PLANE_AUTOMATION: return "automation";
        case EW_SUBSYSTEM_PLANE_LANGUAGE: return "language";
        case EW_SUBSYSTEM_PLANE_MATH: return "math";
        case EW_SUBSYSTEM_PLANE_CORPUS: return "corpus";
        case EW_SUBSYSTEM_PLANE_EXTERNAL_API: return "external_api";
        case EW_SUBSYSTEM_PLANE_AI_CORE: return "ai_core";
        case EW_SUBSYSTEM_PLANE_AI_DATA: return "ai_data";
        default: return "unknown";
    }
}

static void ge_finalize_subsystem_lane(EwSubsystemCalculusLane& lane,
                                       uint16_t intent_norm_q15,
                                       uint16_t measured_norm_q15,
                                       uint16_t spin_norm_q15,
                                       uint16_t coupling_norm_q15,
                                       int64_t prev_error_q32_32,
                                       int64_t prev_integral_q32_32) {
    lane.intent_norm_q15 = intent_norm_q15;
    lane.measured_norm_q15 = measured_norm_q15;
    lane.residual_norm_q15 = (uint16_t)((intent_norm_q15 > measured_norm_q15)
                                            ? (intent_norm_q15 - measured_norm_q15)
                                            : (measured_norm_q15 - intent_norm_q15));
    lane.spin_norm_q15 = spin_norm_q15;
    lane.coupling_norm_q15 = coupling_norm_q15;

    lane.error_q32_32 = ge_q15_error_to_q32_32(measured_norm_q15, intent_norm_q15);
    lane.error_delta_q32_32 = lane.error_q32_32 - prev_error_q32_32;
    lane.error_integral_q32_32 =
        ge_clamp_i64_local(prev_integral_q32_32 + lane.error_q32_32,
                           -(16LL << 32),
                           +(16LL << 32));
    lane.error_delta_norm_q15 = ge_abs_q32_32_to_q15_sat(lane.error_delta_q32_32);
    lane.error_integral_norm_q15 = ge_abs_q32_32_to_q15_sat(lane.error_integral_q32_32);

    uint32_t authority_u32 = (uint32_t)lane.residual_norm_q15 +
                             ((uint32_t)lane.error_delta_norm_q15 / 2u) +
                             ((uint32_t)lane.error_integral_norm_q15 / 4u) +
                             ((uint32_t)lane.spin_norm_q15 / 4u) +
                             ((uint32_t)lane.coupling_norm_q15 / 4u);
    if (authority_u32 > 32767u) authority_u32 = 32767u;
    lane.controller_authority_q15 = (uint16_t)authority_u32;
}


static void ge_rebuild_cached_anchor_ids(SubstrateManager& sm) {
    sm.camera_anchor_id_u32 = 0u;
    sm.coherence_bus_anchor_id_u32 = 0u;
    sm.spectral_field_anchor_id_u32 = 0u;
    sm.voxel_coupling_anchor_id_u32 = 0u;
    sm.collision_env_anchor_id_u32 = 0u;

    const uint32_t n = (uint32_t)sm.anchors.size();
    for (uint32_t id = 1u; id < n; ++id) {
        const Anchor& a = sm.anchors[id];
        switch (a.kind_u32) {
            case EW_ANCHOR_KIND_CAMERA:
                if (sm.camera_anchor_id_u32 == 0u) sm.camera_anchor_id_u32 = id;
                break;
            case EW_ANCHOR_KIND_COHERENCE_BUS:
                if (sm.coherence_bus_anchor_id_u32 == 0u) sm.coherence_bus_anchor_id_u32 = id;
                break;
            case EW_ANCHOR_KIND_SPECTRAL_FIELD:
                if (sm.spectral_field_anchor_id_u32 == 0u) sm.spectral_field_anchor_id_u32 = id;
                break;
            case EW_ANCHOR_KIND_VOXEL_COUPLING:
                if (sm.voxel_coupling_anchor_id_u32 == 0u) sm.voxel_coupling_anchor_id_u32 = id;
                break;
            case EW_ANCHOR_KIND_COLLISION_ENV:
                if (sm.collision_env_anchor_id_u32 == 0u) sm.collision_env_anchor_id_u32 = id;
                break;
            default: break;
        }
    }

    // Ensure next id never regresses below vector size (anchors are id-indexed).
    if (sm.next_anchor_id_u32 < n) sm.next_anchor_id_u32 = n;
}

static void ge_update_process_substrate_telemetry(SubstrateManager& sm) {
    sm.process_substrate_telemetry = EwProcessSubstrateTelemetry{};
    EwProcessSubstrateTelemetry& out = sm.process_substrate_telemetry;
    out.tick_u64 = sm.canonical_tick;

    const Anchor* spectral = nullptr;
    if (sm.spectral_field_anchor_id_u32 != 0u && sm.spectral_field_anchor_id_u32 < sm.anchors.size()) {
        const Anchor& a = sm.anchors[sm.spectral_field_anchor_id_u32];
        if (a.kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) spectral = &a;
    }
    const Anchor* coherence = nullptr;
    if (sm.coherence_bus_anchor_id_u32 != 0u && sm.coherence_bus_anchor_id_u32 < sm.anchors.size()) {
        const Anchor& a = sm.anchors[sm.coherence_bus_anchor_id_u32];
        if (a.kind_u32 == EW_ANCHOR_KIND_COHERENCE_BUS) coherence = &a;
    }

    if (spectral) {
        const EwSpectralFieldAnchorState& ss = spectral->spectral_field_state;
        out.valid_u32 = 1u;
        out.spectral_anchor_id_u32 = spectral->id;
        out.fanout_budget_u32 = ss.fanout_budget_u32;
        out.dt_scale_q32_32 = ss.dt_scale_q32_32;
        out.viscosity_bias_q32_32 = ss.viscosity_bias_q32_32;
        out.error_q32_32 = ss.calculus_summary.error_q32_32;
        out.error_delta_q32_32 = ss.calculus_summary.error_delta_q32_32;
        out.error_integral_q32_32 = ss.calculus_summary.error_integral_q32_32;
        out.calibration_error_q32_32 = ss.calibration_summary.calibration_error_q32_32;
        out.calibration_delta_q32_32 = ss.calibration_summary.calibration_delta_q32_32;
        out.calibration_integral_q32_32 = ss.calibration_summary.calibration_integral_q32_32;
        out.intent_norm_q15 = ss.intent_summary.intent_norm_q15;
        out.energy_mean_q15 = ss.measured_summary.energy_mean_q15;
        out.energy_peak_q15 = ss.measured_summary.energy_peak_q15;
        out.leakage_abs_q15 = ss.measured_summary.leakage_abs_q15;
        out.residual_norm_q15 = ss.temporal_residual.residual_norm_q15;
        out.error_delta_norm_q15 = ss.calculus_summary.error_delta_norm_q15;
        out.error_integral_norm_q15 = ss.calculus_summary.error_integral_norm_q15;
        out.controller_authority_q15 = ss.calculus_summary.controller_authority_q15;
        out.gpu_freq_norm_q15 = ss.calibration_summary.gpu_freq_norm_q15;
        out.gpu_amp_norm_q15 = ss.calibration_summary.gpu_amp_norm_q15;
        out.gpu_volt_norm_q15 = ss.calibration_summary.gpu_volt_norm_q15;
        out.interference_norm_q15 = ss.calibration_summary.interference_norm_q15;
        out.coherence_norm_q15 = ss.calibration_summary.coherence_norm_q15;
        out.observer_norm_q15 = ss.calibration_summary.observer_norm_q15;
        out.source_vibration_q15 = ss.calibration_summary.source_vibration_q15;
        out.calibration_authority_q15 = ss.calibration_summary.calibration_authority_q15;
        out.op_gain_q15 = ss.op_gain_q15;
        out.op_phase_bias_q15 = ss.op_phase_bias_q15;
        out.learning_coupling_q15 = ss.learning_coupling_q15;
        out.last_v_code_u16 = ss.intent_summary.last_v_code_u16;
        out.last_i_code_u16 = ss.intent_summary.last_i_code_u16;
        out.residual_pending_u8 = ss.temporal_residual.residual_pending_u8;
        out.hold_tick_u8 = ss.calculus_summary.controller_hold_u8;
        out.last_commit_u8 = ss.calculus_summary.last_commit_u8;
    }

    if (coherence) {
        const EwCoherenceBusAnchorState& bs = coherence->coherence_bus_state;
        out.coherence_anchor_id_u32 = coherence->id;
        out.phys_coherence_q15 = bs.phys_coherence_q15;
        out.learning_coherence_q15 = bs.learning_coherence_q15;
        out.temporal_coherence_q15 = bs.temporal_coherence_q15;
    }
}

static void ge_update_ai_substrate_telemetry(SubstrateManager& sm) {
    sm.ai_substrate_telemetry = EwAiSubstrateTelemetry{};
    EwAiSubstrateTelemetry& out = sm.ai_substrate_telemetry;

    out.valid_u32 = 1u;
    out.tick_u64 = sm.canonical_tick;
    out.class_id_u32 = sm.neural_ai.status().class_id_u32;
    out.sig9_u64 = sm.neural_ai.status().sig9_u64;
    out.observation_seq_u64 = sm.observation_seq_u64;
    out.dispatched_observation_seq_u64 = sm.ai_last_command_observation_seq_u64;
    out.confidence_q32_32 = sm.neural_ai.status().confidence_q32_32;
    out.attractor_strength_q32_32 = sm.neural_ai.last_attractor_strength_q32_32();
    out.command_count_u32 = sm.ai_commands_count_u32;
    out.action_log_count_u32 = sm.ai_action_log_count_u32;
    out.carrier_pulse_q63 = sm.ai_pulse_q63;
    out.carrier_total_weight_q63 = sm.ai_total_weight_q63;
    out.pending_external_api_u32 = (uint32_t)sm.external_api_pending.size();
    out.inflight_external_api_u32 = (uint32_t)sm.external_api_inflight.size();
    out.ui_chat_count_u32 = (uint32_t)sm.ui_chat_q.size();

    if (sm.ai_anticipation_enabled) out.anticipation_flags_u16 |= 0x1u;
    if (sm.ai_anticipation_auto_execute) out.anticipation_flags_u16 |= 0x2u;
    if (sm.ai_anticipation_emit_ui) out.anticipation_flags_u16 |= 0x4u;

    const EwAiDataSubstrateTelemetry& ai_data = sm.ai_data_substrate_telemetry;
    const CrawlerStats& crawler_stats = sm.crawler.stats();
    const Anchor* spectral = nullptr;
    if (sm.spectral_field_anchor_id_u32 != 0u && sm.spectral_field_anchor_id_u32 < sm.anchors.size()) {
        const Anchor& a = sm.anchors[sm.spectral_field_anchor_id_u32];
        if (a.kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) spectral = &a;
    }
    uint16_t process_coherence_q15 = sm.process_substrate_telemetry.coherence_norm_q15;
    uint16_t process_temporal_q15 = sm.process_substrate_telemetry.temporal_coherence_q15;
    uint16_t process_vibration_q15 = 0u;
    if (spectral) {
        const EwSpectralFieldAnchorState& ss = spectral->spectral_field_state;
        out.fanout_budget_u32 = ss.fanout_budget_u32;
        out.controller_authority_q15 = ss.calculus_summary.controller_authority_q15;
        out.op_gain_q15 = ss.op_gain_q15;
        out.op_phase_bias_q15 = ss.op_phase_bias_q15;
        process_coherence_q15 = ss.calibration_summary.coherence_norm_q15;
        process_vibration_q15 = ss.calibration_summary.source_vibration_q15;
        for (uint32_t i = 0; i < 8u; ++i) {
            out.carrier_band_q15[i] = ss.intent_summary.band_mag_q15[i];
        }
    }
    uint32_t nonzero_carrier_count_u32 = 0u;
    for (uint32_t i = 0u; i < 8u; ++i) {
        if (out.carrier_band_q15[i] != 0u) nonzero_carrier_count_u32 += 1u;
    }
    const uint16_t confidence_q15 = ge_abs_q32_32_to_q15_sat(out.confidence_q32_32);
    const uint16_t attractor_q15 = ge_abs_q32_32_to_q15_sat(out.attractor_strength_q32_32);
    const uint16_t command_q15 = ge_ratio_q15_u64((uint64_t)out.command_count_u32, 128u);
    const uint16_t action_q15 = ge_ratio_q15_u64((uint64_t)out.action_log_count_u32, 128u);
    const uint16_t chat_q15 = ge_ratio_q15_u64((uint64_t)out.ui_chat_count_u32, 64u);
    const uint16_t carrier_diversity_q15 = ge_ratio_q15_u64((uint64_t)nonzero_carrier_count_u32, 8u);
    const uint16_t api_headroom_q15 =
        ge_headroom_q15_from_used_u32(out.pending_external_api_u32 + out.inflight_external_api_u32, 32u);
    const uint16_t gpu_spider_f_q15 =
        ge_ratio_q15_u64((uint64_t)ge_abs_i32_u32_local(ai_data.gpu_spider_f_code_i32),
                         (uint64_t)((F_SCALE != 0) ? F_SCALE : 1));
    const uint16_t gpu_spider_q15_tmp[4] = {
        gpu_spider_f_q15,
        ge_code_u16_to_q15(ai_data.gpu_spider_a_code_u16, 65535u),
        ge_code_u16_to_q15(ai_data.gpu_spider_v_code_u16, V_MAX),
        ge_code_u16_to_q15(ai_data.gpu_spider_i_code_u16, I_MAX)
    };
    const uint16_t gpu_spider_norm_q15 = ge_mean_q15_u16(gpu_spider_q15_tmp, 4u);
    const uint16_t api_decode_f_q15 =
        ge_ratio_q15_u64((uint64_t)ge_abs_i32_u32_local(ai_data.api_decode_f_code_i32),
                         (uint64_t)((F_SCALE != 0) ? F_SCALE : 1));
    const uint16_t api_decode_q15_tmp[4] = {
        api_decode_f_q15,
        ge_code_u16_to_q15(ai_data.api_decode_a_code_u16, 65535u),
        ge_code_u16_to_q15(ai_data.api_decode_v_code_u16, V_MAX),
        ge_code_u16_to_q15(ai_data.api_decode_i_code_u16, I_MAX)
    };
    const uint16_t api_decode_spider_q15 = ge_mean_q15_u16(api_decode_q15_tmp, 4u);
    const uint16_t memory_tmp[5] = {
        attractor_q15,
        ai_data.temporal_memory_norm_q15,
        ai_data.measured_band_q15[6],
        process_vibration_q15,
        ai_data.api_decode_norm_q15
    };
    const uint16_t reasoning_tmp[5] = {
        confidence_q15,
        ai_data.measured_band_q15[4],
        ai_data.measured_band_q15[5],
        ai_data.controller_authority_q15,
        gpu_spider_norm_q15
    };
    const uint16_t planning_tmp[5] = {
        api_headroom_q15,
        ai_data.measured_band_q15[7],
        command_q15,
        ai_data.temporal_prediction_norm_q15,
        ai_data.api_decode_http_status_q15
    };
    const uint16_t creativity_tmp[5] = {
        carrier_diversity_q15,
        ai_data.measured_band_q15[2],
        ai_data.measured_band_q15[3],
        action_q15,
        gpu_spider_norm_q15
    };
    const uint16_t perception_tmp[5] = {
        chat_q15,
        ai_data.crawler_flow_norm_q15,
        ai_data.measured_band_q15[0],
        ai_data.measured_band_q15[1],
        ai_data.api_decode_norm_q15
    };
    const uint16_t temporal_tmp[5] = {
        ai_data.temporal_prediction_norm_q15,
        ai_data.crawler_coherence_norm_q15,
        process_temporal_q15,
        process_vibration_q15,
        ai_data.gpu_gradient_energy_norm_q15
    };
    const uint16_t network_tmp[5] = {
        ai_data.crawler_coherence_norm_q15,
        process_coherence_q15,
        api_headroom_q15,
        ge_headroom_q15_from_used_u32(crawler_stats.pending_obs_u32, 64u),
        api_decode_spider_q15
    };
    out.memory_norm_q15 = ge_mean_q15_u16(memory_tmp, 5u);
    out.reasoning_norm_q15 = ge_mean_q15_u16(reasoning_tmp, 5u);
    out.planning_norm_q15 = ge_mean_q15_u16(planning_tmp, 5u);
    out.creativity_norm_q15 = ge_mean_q15_u16(creativity_tmp, 5u);
    out.perception_norm_q15 = ge_mean_q15_u16(perception_tmp, 5u);
    out.temporal_binding_norm_q15 = ge_mean_q15_u16(temporal_tmp, 5u);
    out.crawler_drift_norm_q15 = ai_data.crawler_drift_norm_q15;
    out.network_coherence_norm_q15 = ge_mean_q15_u16(network_tmp, 5u);

    if (sm.ai_action_log_count_u32 > 0u) {
        const uint32_t idx = (sm.ai_action_log_head_u32 + SubstrateManager::AI_ACTION_LOG_CAP - 1u) %
                             SubstrateManager::AI_ACTION_LOG_CAP;
        const EwAiActionEvent& e = sm.ai_action_log[idx];
        out.last_action_kind_u16 = e.kind_u16;
        out.last_action_profile_u16 = e.profile_id_u16;
        out.last_target_anchor_id_u32 = e.target_anchor_id_u32;
        out.last_f_code_i32 = e.f_code_i32;
        out.last_a_code_u16 = (uint16_t)((e.a_code_u32 > 65535u) ? 65535u : e.a_code_u32);
        out.last_v_code_u16 = (uint16_t)((e.v_code_u32 > 65535u) ? 65535u : e.v_code_u32);
        out.last_i_code_u16 = (uint16_t)((e.i_code_u32 > 65535u) ? 65535u : e.i_code_u32);
    }
}

static void ge_update_ai_data_substrate_telemetry(SubstrateManager& sm) {
    const int64_t prev_error_q32_32 = sm.ai_data_substrate_telemetry.error_q32_32;
    const int64_t prev_integral_q32_32 = sm.ai_data_substrate_telemetry.error_integral_q32_32;
    const uint64_t prev_crawler_enqueued_obs_u64 = sm.ai_data_substrate_telemetry.crawler_enqueued_obs_u64;
    const uint64_t prev_crawler_admitted_pulses_u64 = sm.ai_data_substrate_telemetry.crawler_admitted_pulses_u64;
    const uint64_t prev_crawler_dropped_obs_u64 = sm.ai_data_substrate_telemetry.crawler_dropped_obs_u64;
    const uint64_t prev_crawler_truncated_segments_u64 = sm.ai_data_substrate_telemetry.crawler_truncated_segments_u64;

    sm.ai_data_substrate_telemetry = EwAiDataSubstrateTelemetry{};
    EwAiDataSubstrateTelemetry& out = sm.ai_data_substrate_telemetry;
    out.valid_u32 = 1u;
    out.tick_u64 = sm.canonical_tick;

    std::vector<EwInspectorArtifact> corpus;
    sm.inspector_fields.snapshot_prefix("Draft Container/Corpus/", corpus);
    out.corpus_artifact_count_u32 = (uint32_t)corpus.size();

    uint32_t corpus_text_count_u32 = 0u;
    for (size_t i = 0; i < corpus.size(); ++i) {
        const std::string& p = corpus[i].rel_path;
        if (p.find(".txt") != std::string::npos || p.find(".md") != std::string::npos) {
            corpus_text_count_u32 += 1u;
        }
    }
    out.corpus_text_artifact_count_u32 = corpus_text_count_u32;
    out.manifest_record_count_u32 = (uint32_t)sm.manifest_records.size();
    out.pending_metric_count_u32 = sm.learning_gate.registry().pending_count_u32();
    out.completed_metric_count_u32 = sm.learning_gate.registry().completed_count_u32();

    const auto& completed = sm.learning_gate.registry().completed();
    uint32_t accepted_metric_count_u32 = 0u;
    for (size_t i = 0; i < completed.size(); ++i) {
        if (completed[i].accepted) accepted_metric_count_u32 += 1u;
    }
    out.accepted_metric_count_u32 = accepted_metric_count_u32;

    const genesis::LangLexiconStats& ls = sm.language_foundation.stats();
    out.language_word_count_u32 = ls.word_count_u32;
    out.language_pron_count_u32 = ls.pron_count_u32;
    out.language_relation_count_u32 = ls.relations_count_u32;
    out.language_concept_count_u32 = ls.concept_count_u32;
    out.language_speech_utt_count_u32 = ls.speech_utt_count_u32;
    out.language_speech_word_tokens_u32 = ls.speech_word_tokens_u32;

    const genesis::MathStats& ms = sm.math_foundation.stats();
    out.math_pemdas_cases_total_u32 = ms.pemdas_cases_total_u32;
    out.math_pemdas_cases_passed_u32 = ms.pemdas_cases_passed_u32;
    out.math_graph_packets_emitted_u32 = ms.graph_1d_packets_emitted_u32;
    out.math_khan_pages_seen_u32 = ms.khan_pages_seen_u32;
    out.math_khan_chars_ingested_u32 = ms.khan_chars_ingested_u32;

    out.pending_external_api_u32 = (uint32_t)sm.external_api_pending.size();
    out.inflight_external_api_u32 = (uint32_t)sm.external_api_inflight.size();
    const CrawlerStats& crawler_stats = sm.crawler.stats();
    out.crawler_pending_obs_u32 = crawler_stats.pending_obs_u32;
    out.crawler_last_utf8_bytes_u32 = crawler_stats.last_utf8_bytes_u32;
    out.crawler_enqueued_obs_u64 = crawler_stats.enqueued_obs_u64;
    out.crawler_admitted_pulses_u64 = crawler_stats.admitted_pulses_u64;
    out.crawler_dropped_obs_u64 = crawler_stats.dropped_obs_u64;
    out.crawler_truncated_segments_u64 = crawler_stats.truncated_segments_u64;

    const uint64_t delta_crawler_enqueued_u64 =
        (out.crawler_enqueued_obs_u64 >= prev_crawler_enqueued_obs_u64)
            ? (out.crawler_enqueued_obs_u64 - prev_crawler_enqueued_obs_u64)
            : 0u;
    const uint64_t delta_crawler_admitted_u64 =
        (out.crawler_admitted_pulses_u64 >= prev_crawler_admitted_pulses_u64)
            ? (out.crawler_admitted_pulses_u64 - prev_crawler_admitted_pulses_u64)
            : 0u;
    const uint64_t delta_crawler_dropped_u64 =
        (out.crawler_dropped_obs_u64 >= prev_crawler_dropped_obs_u64)
            ? (out.crawler_dropped_obs_u64 - prev_crawler_dropped_obs_u64)
            : 0u;
    const uint64_t delta_crawler_truncated_u64 =
        (out.crawler_truncated_segments_u64 >= prev_crawler_truncated_segments_u64)
            ? (out.crawler_truncated_segments_u64 - prev_crawler_truncated_segments_u64)
            : 0u;
    const uint16_t delta_crawler_enqueued_q15 = ge_ratio_q15_u64(delta_crawler_enqueued_u64, 64u);
    const uint16_t delta_crawler_admitted_q15 = ge_ratio_q15_u64(delta_crawler_admitted_u64, 64u);
    const uint16_t delta_crawler_dropped_q15 = ge_ratio_q15_u64(delta_crawler_dropped_u64, 32u);
    const uint16_t delta_crawler_truncated_q15 = ge_ratio_q15_u64(delta_crawler_truncated_u64, 32u);
    const uint16_t pending_crawler_q15 = ge_ratio_q15_u64((uint64_t)out.crawler_pending_obs_u32, 64u);
    const uint16_t crawler_bytes_q15 = ge_ratio_q15_u64((uint64_t)out.crawler_last_utf8_bytes_u32, 4096u);
    const uint16_t crawler_flow_tmp[3] = {
        delta_crawler_enqueued_q15,
        delta_crawler_admitted_q15,
        crawler_bytes_q15
    };
    out.crawler_flow_norm_q15 = ge_mean_q15_u16(crawler_flow_tmp, 3u);
    const uint16_t crawler_drift_gap_q15 =
        (delta_crawler_enqueued_q15 > delta_crawler_admitted_q15)
            ? (uint16_t)(delta_crawler_enqueued_q15 - delta_crawler_admitted_q15)
            : (uint16_t)(delta_crawler_admitted_q15 - delta_crawler_enqueued_q15);
    const uint16_t crawler_drift_tmp[4] = {
        crawler_drift_gap_q15,
        delta_crawler_dropped_q15,
        delta_crawler_truncated_q15,
        pending_crawler_q15
    };
    out.crawler_drift_norm_q15 = ge_mean_q15_u16(crawler_drift_tmp, 4u);
    const uint16_t process_interference_q15 =
        (sm.process_substrate_telemetry.valid_u32 != 0u) ? sm.process_substrate_telemetry.interference_norm_q15 : 0u;
    const uint16_t crawler_interference_tmp[4] = {
        process_interference_q15,
        pending_crawler_q15,
        delta_crawler_dropped_q15,
        delta_crawler_truncated_q15
    };
    out.crawler_interference_norm_q15 = ge_mean_q15_u16(crawler_interference_tmp, 4u);
    const uint16_t process_coherence_q15 =
        (sm.process_substrate_telemetry.valid_u32 != 0u) ? sm.process_substrate_telemetry.coherence_norm_q15 : 0u;
    const uint16_t admitted_alignment_q15 =
        (delta_crawler_enqueued_u64 != 0u)
            ? ge_ratio_q15_u64(delta_crawler_admitted_u64, delta_crawler_enqueued_u64)
            : ge_headroom_q15_from_used_u32(out.crawler_pending_obs_u32, 64u);
    const int64_t crawler_flow_q32_32 = ge_q15_to_q32_32(out.crawler_flow_norm_q15);
    const int64_t process_coherence_q32_32 = ge_q15_to_q32_32(process_coherence_q15);
    const int64_t coherence_dev_q32_32 = coherence_map_dev(process_coherence_q32_32, crawler_flow_q32_32);
    const uint16_t coherence_dev_q15 = ge_abs_q32_32_to_q15_sat(coherence_dev_q32_32);
    out.crawler_coherence_norm_q15 = (coherence_dev_q15 >= 32767u) ? 0u : (uint16_t)(32767u - coherence_dev_q15);
    const uint16_t temporal_memory_seed_tmp[3] = {
        out.crawler_flow_norm_q15,
        ge_ratio_q15_u64((uint64_t)out.accepted_metric_count_u32,
                         (uint64_t)((out.completed_metric_count_u32 != 0u) ? out.completed_metric_count_u32 : 1u)),
        admitted_alignment_q15
    };
    const int64_t temporal_memory_seed_q32_32 =
        ge_q15_to_q32_32(ge_mean_q15_u16(temporal_memory_seed_tmp, 3u));
    const int64_t temporal_memory_q32_32 =
        admit_memory(temporal_memory_seed_q32_32, ge_q15_to_q32_32(out.crawler_coherence_norm_q15));
    out.temporal_memory_norm_q15 = ge_abs_q32_32_to_q15_sat(temporal_memory_q32_32);
    int64_t temporal_prediction_q32_32 =
        temporal_memory_q32_32 +
        (ge_q15_to_q32_32(out.crawler_coherence_norm_q15) >> 1) -
        (ge_q15_to_q32_32(out.crawler_drift_norm_q15) >> 1);
    temporal_prediction_q32_32 = budget_state(temporal_prediction_q32_32, (1LL << 32));
    if (temporal_prediction_q32_32 < 0) temporal_prediction_q32_32 = 0;
    out.temporal_prediction_q32_32 = temporal_prediction_q32_32;
    out.temporal_prediction_norm_q15 = ge_abs_q32_32_to_q15_sat(out.temporal_prediction_q32_32);
    ge_update_ai_gpu_spider_drive(sm, out);
    ge_update_ai_api_decode_drive(sm, out);

    out.intent_band_q15[0] = sm.corpus_pipeline_enable_u32 ? 32767u : 0u;
    out.intent_band_q15[1] = sm.corpus_pipeline_enable_u32 ? 32767u : 0u;
    out.intent_band_q15[2] = 32767u;
    out.intent_band_q15[3] = 32767u;
    out.intent_band_q15[4] = 32767u;
    out.intent_band_q15[5] = 32767u;
    out.intent_band_q15[6] = 32767u;
    out.intent_band_q15[7] = 32767u;

    out.measured_band_q15[0] = ge_ratio_q15_u64(out.corpus_artifact_count_u32, 128u);
    out.measured_band_q15[1] = ge_ratio_q15_u64(out.manifest_record_count_u32, 64u);
    {
        uint16_t tmp[3] = {
            ge_ratio_q15_u64(out.language_word_count_u32, 50000u),
            ge_ratio_q15_u64(out.language_pron_count_u32, 50000u),
            ge_ratio_q15_u64(out.language_relation_count_u32, 50000u)
        };
        out.measured_band_q15[2] = ge_mean_q15_u16(tmp, 3u);
    }
    {
        uint16_t tmp[3] = {
            ge_ratio_q15_u64(out.language_concept_count_u32, 10000u),
            ge_ratio_q15_u64(out.language_speech_utt_count_u32, 10000u),
            ge_ratio_q15_u64(out.language_speech_word_tokens_u32, 50000u)
        };
        out.measured_band_q15[3] = ge_mean_q15_u16(tmp, 3u);
    }
    {
        uint16_t pemdas_q15 = ge_ratio_q15_u64(out.math_pemdas_cases_passed_u32,
                                               (uint64_t)((out.math_pemdas_cases_total_u32 != 0u) ? out.math_pemdas_cases_total_u32 : 1u));
        uint16_t graph_q15 = ge_ratio_q15_u64(out.math_graph_packets_emitted_u32, 8u);
        uint16_t tmp[2] = { pemdas_q15, graph_q15 };
        out.measured_band_q15[4] = ge_mean_q15_u16(tmp, 2u);
    }
    {
        uint16_t tmp[2] = {
            ge_ratio_q15_u64(out.math_khan_pages_seen_u32, 32u),
            ge_ratio_q15_u64(out.math_khan_chars_ingested_u32, 131072u)
        };
        out.measured_band_q15[5] = ge_mean_q15_u16(tmp, 2u);
    }
    {
        uint16_t completion_sat_q15 = ge_ratio_q15_u64(out.completed_metric_count_u32, 32u);
        uint16_t accept_ratio_q15 = ge_ratio_q15_u64(out.accepted_metric_count_u32,
                                                     (uint64_t)((out.completed_metric_count_u32 != 0u) ? out.completed_metric_count_u32 : 1u));
        uint16_t tmp[2] = { completion_sat_q15, accept_ratio_q15 };
        out.measured_band_q15[6] = ge_mean_q15_u16(tmp, 2u);
    }
    {
        uint16_t pending_headroom_q15 = ge_headroom_q15_from_used_u32(out.pending_metric_count_u32, 32u);
        uint16_t api_headroom_q15 = ge_headroom_q15_from_used_u32(out.pending_external_api_u32 + out.inflight_external_api_u32, 16u);
        uint16_t data_surface_q15 = ge_ratio_q15_u64(out.corpus_text_artifact_count_u32, 64u);
        uint16_t tmp[6] = {
            pending_headroom_q15,
            api_headroom_q15,
            data_surface_q15,
            out.temporal_prediction_norm_q15,
            out.gpu_gradient_energy_norm_q15,
            out.api_decode_norm_q15
        };
        out.measured_band_q15[7] = ge_mean_q15_u16(tmp, 6u);
    }

    out.intent_norm_q15 = ge_mean_q15_u16(out.intent_band_q15, 8u);
    out.measured_norm_q15 = ge_mean_q15_u16(out.measured_band_q15, 8u);
    out.residual_norm_q15 = ge_mean_abs_diff_q15_u16(out.intent_band_q15, out.measured_band_q15, 8u);

    out.error_q32_32 = ge_q15_error_to_q32_32(out.measured_norm_q15, out.intent_norm_q15);
    out.error_delta_q32_32 = out.error_q32_32 - prev_error_q32_32;
    out.error_integral_q32_32 = ge_clamp_i64_local(prev_integral_q32_32 + out.error_q32_32,
                                                   -(8LL << 32),
                                                   +(8LL << 32));
    out.error_delta_norm_q15 = ge_abs_q32_32_to_q15_sat(out.error_delta_q32_32);
    out.error_integral_norm_q15 = ge_abs_q32_32_to_q15_sat(out.error_integral_q32_32);

    uint32_t authority_u32 = (uint32_t)out.residual_norm_q15 +
                             ((uint32_t)out.error_delta_norm_q15 / 2u) +
                             ((uint32_t)out.error_integral_norm_q15 / 4u);
    if (authority_u32 > 32767u) authority_u32 = 32767u;
    out.controller_authority_q15 = (uint16_t)authority_u32;
}

static void ge_update_subsystem_substrate_telemetry(SubstrateManager& sm) {
    const EwSubsystemSubstrateTelemetry prev = sm.subsystem_substrate_telemetry;
    sm.subsystem_substrate_telemetry = EwSubsystemSubstrateTelemetry{};
    EwSubsystemSubstrateTelemetry& out = sm.subsystem_substrate_telemetry;
    out.tick_u64 = sm.canonical_tick;

    const EwProcessSubstrateTelemetry& process = sm.process_substrate_telemetry;
    const EwAiSubstrateTelemetry& ai = sm.ai_substrate_telemetry;
    const EwAiDataSubstrateTelemetry& ai_data = sm.ai_data_substrate_telemetry;

    const uint16_t sx_q15 =
        ge_q32_32_ratio_to_q15_sat(sm.sx_q32_32, (int64_t)(2LL << 32));
    const uint16_t sy_q15 =
        ge_q32_32_ratio_to_q15_sat(sm.sy_q32_32, (int64_t)(2LL << 32));
    const uint16_t sz_q15 =
        ge_q32_32_ratio_to_q15_sat(sm.sz_q32_32, (int64_t)(2LL << 32));
    const uint16_t axis_scale_mean_q15 = ge_mean_q15_u16(&sx_q15, 1u);
    (void)axis_scale_mean_q15;

    const bool camera_valid =
        sm.camera_anchor_id_u32 != 0u &&
        sm.camera_anchor_id_u32 < sm.anchors.size() &&
        sm.anchors[sm.camera_anchor_id_u32].kind_u32 == EW_ANCHOR_KIND_CAMERA;
    const uint16_t depth_q15 =
        ge_ratio_q15_u64((uint64_t)std::max<int32_t>(0, sm.camera_sensor.median_depth_norm_q16_16),
                         65536u);
    const uint16_t camera_focus_q15 =
        ge_q32_32_ratio_to_q15_sat(sm.render_camera_packet.focus_distance_m_q32_32,
                                   (int64_t)(32LL << 32));
    const uint16_t camera_audio_field_q15 =
        ge_abs_i16_q15_to_q15_sat(sm.render_camera_packet.audio_env_field_q1_15);
    const uint16_t camera_audio_grad_q15 =
        ge_abs_i16_q15_to_q15_sat(sm.render_camera_packet.audio_env_grad_q1_15);
    const uint16_t camera_color_band_q15 =
        ge_ratio_q15_u64((uint64_t)sm.render_camera_packet.color_band_u8, 7u);
    const uint16_t camera_measured_tmp[3] = {
        depth_q15,
        sm.render_camera_packet.audio_env_coherence_q15,
        camera_focus_q15
    };
    const uint16_t camera_spin_tmp[3] = {
        camera_audio_field_q15,
        camera_audio_grad_q15,
        camera_color_band_q15
    };
    const uint16_t camera_coupling_tmp[4] = {
        sx_q15, sy_q15, sz_q15, sm.render_camera_packet.audio_env_coherence_q15
    };

    const uint16_t render_active_q15 =
        ge_bool_to_q15(sm.render_camera_packet_tick_u64 != 0u ||
                       sm.render_object_packets_tick_u64 != 0u);
    const uint16_t render_object_count_q15 =
        ge_ratio_q15_u64((uint64_t)sm.render_object_packets.size(), 64u);
    const uint16_t render_screen_proxy_q15 =
        ge_q16_16_ratio_to_q15_sat(sm.render_assist_packet.screen_proxy_scale_q16_16,
                                   (int32_t)(8 * 65536));
    const uint16_t render_focus_band_q15 =
        ge_q32_32_ratio_to_q15_sat(sm.render_assist_packet.focus_band_m_q32_32,
                                   (int64_t)(8LL << 32));
    const uint16_t render_audio_field_q15 =
        ge_abs_i16_q15_to_q15_sat(sm.render_assist_packet.audio_env_field_q1_15);
    const uint16_t render_audio_grad_q15 =
        ge_abs_i16_q15_to_q15_sat(sm.render_assist_packet.audio_env_grad_q1_15);
    const uint16_t render_color_band_q15 =
        ge_ratio_q15_u64((uint64_t)sm.render_assist_packet.color_band_u8, 7u);
    const uint16_t render_measured_tmp[3] = {
        render_object_count_q15,
        sm.render_assist_packet.audio_env_coherence_q15,
        render_focus_band_q15
    };
    const uint16_t render_spin_tmp[3] = {
        render_audio_field_q15,
        render_audio_grad_q15,
        render_color_band_q15
    };
    const uint16_t render_coupling_tmp[4] = {
        render_object_count_q15,
        render_screen_proxy_q15,
        sx_q15,
        sm.render_assist_packet.audio_env_coherence_q15
    };

    std::vector<uint64_t> object_ids;
    sm.object_store.list_object_ids_sorted(object_ids);
    uint32_t voxel_ready_u32 = 0u;
    uint32_t uv_ready_u32 = 0u;
    uint32_t local_ready_u32 = 0u;
    for (size_t i = 0; i < object_ids.size(); ++i) {
        const EwObjectEntry* e = sm.object_store.find(object_ids[i]);
        if (!e) continue;
        if ((e->voxel_grid_x_u32 != 0u && e->voxel_grid_y_u32 != 0u && e->voxel_grid_z_u32 != 0u) ||
            e->voxel_blob_id_u64 != 0u) {
            voxel_ready_u32 += 1u;
        }
        if ((e->uv_atlas_w_u32 != 0u && e->uv_atlas_h_u32 != 0u) ||
            e->uv_atlas_blob_id_u64 != 0u) {
            uv_ready_u32 += 1u;
        }
        if ((e->local_grid_x_u32 != 0u && e->local_grid_y_u32 != 0u && e->local_grid_z_u32 != 0u) ||
            e->local_blob_id_u64 != 0u) {
            local_ready_u32 += 1u;
        }
    }
    const uint32_t object_count_u32 = (uint32_t)object_ids.size();
    const uint16_t object_count_q15 = ge_ratio_q15_u64((uint64_t)object_count_u32, 64u);
    const uint16_t voxel_ratio_q15 = ge_ratio_q15_u64((uint64_t)voxel_ready_u32,
                                                      (uint64_t)((object_count_u32 != 0u) ? object_count_u32 : 1u));
    const uint16_t uv_ratio_q15 = ge_ratio_q15_u64((uint64_t)uv_ready_u32,
                                                   (uint64_t)((object_count_u32 != 0u) ? object_count_u32 : 1u));
    const uint16_t local_ratio_q15 = ge_ratio_q15_u64((uint64_t)local_ready_u32,
                                                      (uint64_t)((object_count_u32 != 0u) ? object_count_u32 : 1u));
    const uint16_t asset_root_q15 = ge_bool_to_q15(!sm.asset_substrate.project_root().empty());
    const uint16_t asset_intent_tmp[2] = {
        asset_root_q15,
        ge_bool_to_q15(object_count_u32 != 0u)
    };
    const uint16_t asset_measured_tmp[4] = {
        object_count_q15,
        voxel_ratio_q15,
        uv_ratio_q15,
        local_ratio_q15
    };
    const uint16_t asset_spin_tmp[3] = {
        local_ratio_q15,
        uv_ratio_q15,
        render_object_count_q15
    };
    const uint16_t asset_coupling_tmp[3] = {
        voxel_ratio_q15,
        local_ratio_q15,
        render_object_count_q15
    };

    const genesis::EwNBodyState& nb = sm.nbody_state;
    uint32_t anchored_bodies_u32 = 0u;
    uint32_t orbiting_bodies_u32 = 0u;
    uint64_t velocity_q15_sum_u64 = 0u;
    uint32_t velocity_samples_u32 = 0u;
    for (uint32_t i = 0u; i < nb.body_count_u32 && i < genesis::EwNBodyState::MAX_BODIES; ++i) {
        const genesis::EwNBodyBody& body = nb.bodies[i];
        if (body.planet_anchor_id_u32 != 0u) anchored_bodies_u32 += 1u;
        if (body.object_id_u64 != 0u &&
            (body.radius_m_q32_32 != 0 || body.emissive_q32_32 != 0 || body.atmosphere_thickness_m_q32_32 != 0)) {
            orbiting_bodies_u32 += 1u;
        }
        const uint16_t vx_q15 =
            ge_q32_32_ratio_to_q15_sat(body.vel_mps_q32_32[0], (int64_t)(20000LL << 32));
        const uint16_t vy_q15 =
            ge_q32_32_ratio_to_q15_sat(body.vel_mps_q32_32[1], (int64_t)(20000LL << 32));
        const uint16_t vz_q15 =
            ge_q32_32_ratio_to_q15_sat(body.vel_mps_q32_32[2], (int64_t)(20000LL << 32));
        const uint16_t vel_tmp[3] = { vx_q15, vy_q15, vz_q15 };
        velocity_q15_sum_u64 += (uint64_t)ge_mean_q15_u16(vel_tmp, 3u);
        velocity_samples_u32 += 1u;
    }
    const uint16_t nbody_enabled_q15 = ge_bool_to_q15(nb.enabled_u32 != 0u);
    const uint16_t nbody_initialized_q15 = ge_bool_to_q15(nb.initialized_u32 != 0u);
    const uint16_t nbody_count_q15 = ge_ratio_q15_u64((uint64_t)nb.body_count_u32,
                                                      (uint64_t)genesis::EwNBodyState::MAX_BODIES);
    const uint16_t nbody_anchor_ratio_q15 = ge_ratio_q15_u64((uint64_t)anchored_bodies_u32,
                                                             (uint64_t)((nb.body_count_u32 != 0u) ? nb.body_count_u32 : 1u));
    const uint16_t nbody_orbit_ratio_q15 = ge_ratio_q15_u64((uint64_t)orbiting_bodies_u32,
                                                            (uint64_t)((nb.body_count_u32 != 0u) ? nb.body_count_u32 : 1u));
    const uint16_t nbody_velocity_q15 =
        (velocity_samples_u32 != 0u)
            ? ge_ratio_q15_u64(velocity_q15_sum_u64, (uint64_t)velocity_samples_u32)
            : 0u;
    const uint16_t nbody_measured_tmp[4] = {
        nbody_enabled_q15,
        nbody_initialized_q15,
        nbody_count_q15,
        nbody_anchor_ratio_q15
    };
    const uint16_t nbody_spin_tmp[2] = {
        nbody_velocity_q15,
        nbody_orbit_ratio_q15
    };
    const uint16_t nbody_coupling_tmp[3] = {
        nbody_anchor_ratio_q15,
        nbody_orbit_ratio_q15,
        nbody_count_q15
    };

    const uint32_t cur_stage_u32 =
        (sm.learning_curriculum_stage_u32 < SubstrateManager::GENESIS_CURRICULUM_STAGE_COUNT)
            ? sm.learning_curriculum_stage_u32
            : (SubstrateManager::GENESIS_CURRICULUM_STAGE_COUNT - 1u);
    const uint64_t req_mask_u64 = sm.learning_stage_required_mask_u64[cur_stage_u32];
    const uint64_t done_mask_u64 = sm.learning_stage_completed_mask_u64[cur_stage_u32] & req_mask_u64;
    const uint32_t req_bits_u32 = ge_popcount_u64_local(req_mask_u64);
    const uint32_t done_bits_u32 = ge_popcount_u64_local(done_mask_u64);
    const uint16_t curriculum_stage_q15 =
        ge_ratio_q15_u64((uint64_t)cur_stage_u32,
                         (uint64_t)((SubstrateManager::GENESIS_CURRICULUM_STAGE_COUNT > 1u)
                                        ? (SubstrateManager::GENESIS_CURRICULUM_STAGE_COUNT - 1u)
                                        : 1u));
    const uint16_t curriculum_completion_q15 =
        ge_ratio_q15_u64((uint64_t)done_bits_u32,
                         (uint64_t)((req_bits_u32 != 0u) ? req_bits_u32 : 1u));
    const uint16_t curriculum_lane_density_q15 =
        ge_ratio_q15_u64((uint64_t)(ge_popcount_u64_local((uint64_t)sm.curriculum.ingest_lane_mask_u32) +
                                    ge_popcount_u64_local((uint64_t)sm.curriculum.train_lane_mask_u32)),
                         32u);
    const uint16_t curriculum_stage_advances_q15 =
        ge_ratio_q15_u64(sm.curriculum.stage_advances_u64, 32u);
    const uint16_t curriculum_measured_tmp[2] = {
        curriculum_stage_q15,
        curriculum_completion_q15
    };
    const uint16_t curriculum_spin_tmp[2] = {
        curriculum_lane_density_q15,
        curriculum_stage_advances_q15
    };
    const uint16_t curriculum_coupling_tmp[3] = {
        curriculum_completion_q15,
        curriculum_lane_density_q15,
        curriculum_stage_advances_q15
    };

    const uint32_t automation_bus_size_u32 = sm.learning_automation.bus().size_u32();
    const uint16_t automation_enabled_q15 = ge_bool_to_q15(sm.learning_automation.enabled());
    const uint16_t automation_bus_used_q15 =
        ge_ratio_q15_u64((uint64_t)automation_bus_size_u32, 64u);
    const uint16_t automation_bus_headroom_q15 =
        ge_headroom_q15_from_used_u32(automation_bus_size_u32, 64u);
    const uint16_t automation_pending_metrics_q15 =
        ge_ratio_q15_u64((uint64_t)sm.learning_gate.registry().pending_count_u32(), 64u);
    const uint16_t automation_completed_metrics_q15 =
        ge_ratio_q15_u64((uint64_t)sm.learning_gate.registry().completed_count_u32(), 64u);
    const uint16_t automation_measured_tmp[3] = {
        automation_enabled_q15,
        automation_bus_headroom_q15,
        automation_completed_metrics_q15
    };
    const uint16_t automation_spin_tmp[3] = {
        automation_bus_used_q15,
        automation_pending_metrics_q15,
        automation_completed_metrics_q15
    };
    const uint16_t automation_coupling_tmp[3] = {
        automation_bus_used_q15,
        automation_completed_metrics_q15,
        curriculum_completion_q15
    };

    const genesis::LangLexiconStats& lang = sm.language_foundation.stats();
    const uint16_t lang_word_q15 = ge_ratio_q15_u64((uint64_t)lang.word_count_u32, 50000u);
    const uint16_t lang_pron_q15 = ge_ratio_q15_u64((uint64_t)lang.pron_count_u32, 50000u);
    const uint16_t lang_rel_q15 = ge_ratio_q15_u64((uint64_t)lang.relations_count_u32, 50000u);
    const uint16_t lang_concept_q15 = ge_ratio_q15_u64((uint64_t)lang.concept_count_u32, 10000u);
    const uint16_t lang_speech_utt_q15 = ge_ratio_q15_u64((uint64_t)lang.speech_utt_count_u32, 10000u);
    const uint16_t lang_speech_tok_q15 = ge_ratio_q15_u64((uint64_t)lang.speech_word_tokens_u32, 50000u);
    const uint16_t lang_measured_tmp[4] = {
        lang_word_q15,
        lang_pron_q15,
        lang_rel_q15,
        lang_concept_q15
    };
    const uint16_t lang_spin_tmp[2] = {
        lang_speech_utt_q15,
        lang_speech_tok_q15
    };
    const uint16_t lang_coupling_tmp[3] = {
        lang_rel_q15,
        lang_concept_q15,
        lang_speech_tok_q15
    };

    const genesis::MathStats& math = sm.math_foundation.stats();
    const uint16_t math_pemdas_q15 =
        ge_ratio_q15_u64((uint64_t)math.pemdas_cases_passed_u32,
                         (uint64_t)((math.pemdas_cases_total_u32 != 0u) ? math.pemdas_cases_total_u32 : 1u));
    const uint16_t math_graph_samples_q15 =
        ge_ratio_q15_u64((uint64_t)math.graph_1d_samples_total_u32, 512u);
    const uint16_t math_graph_packets_q15 =
        ge_ratio_q15_u64((uint64_t)math.graph_1d_packets_emitted_u32, 128u);
    const uint16_t math_khan_pages_q15 =
        ge_ratio_q15_u64((uint64_t)math.khan_pages_seen_u32, 64u);
    const uint16_t math_khan_chars_q15 =
        ge_ratio_q15_u64((uint64_t)math.khan_chars_ingested_u32, 262144u);
    const uint16_t math_measured_tmp[4] = {
        math_pemdas_q15,
        math_graph_samples_q15,
        math_graph_packets_q15,
        math_khan_pages_q15
    };
    const uint16_t math_spin_tmp[2] = {
        math_graph_packets_q15,
        math_khan_chars_q15
    };
    const uint16_t math_coupling_tmp[3] = {
        math_pemdas_q15,
        math_graph_packets_q15,
        math_khan_chars_q15
    };

    const uint16_t corpus_enabled_q15 = ge_bool_to_q15(sm.corpus_pipeline_enable_u32 != 0u);
    const uint16_t corpus_allowlist_q15 =
        ge_bool_to_q15(sm.corpus_allowlist_loaded || sm.corpus_allowlist_user_loaded);
    const uint16_t corpus_artifacts_q15 =
        ge_ratio_q15_u64((uint64_t)ai_data.corpus_artifact_count_u32, 128u);
    const uint16_t corpus_text_q15 =
        ge_ratio_q15_u64((uint64_t)ai_data.corpus_text_artifact_count_u32, 64u);
    const uint16_t manifest_q15 =
        ge_ratio_q15_u64((uint64_t)ai_data.manifest_record_count_u32, 64u);
    const uint16_t crawl_lane_q15 =
        ge_ratio_q15_u64((uint64_t)sm.crawl_allowlist_lane_max_u32, 8u);
    const uint16_t corpus_measured_tmp[4] = {
        corpus_artifacts_q15,
        corpus_text_q15,
        manifest_q15,
        corpus_allowlist_q15
    };
    const uint16_t corpus_spin_tmp[2] = {
        crawl_lane_q15,
        manifest_q15
    };
    const uint16_t corpus_coupling_tmp[3] = {
        corpus_allowlist_q15,
        crawl_lane_q15,
        corpus_text_q15
    };

    const uint32_t external_pending_u32 = (uint32_t)sm.external_api_pending.size();
    const uint32_t external_inflight_u32 = (uint32_t)sm.external_api_inflight.size();
    const uint32_t external_docs_u32 = (uint32_t)sm.external_api_ingest_docs.size();
    const uint32_t external_chunks_u32 = (uint32_t)sm.external_api_ingest_inbox.size();
    const uint16_t external_pending_q15 = ge_ratio_q15_u64((uint64_t)external_pending_u32, 16u);
    const uint16_t external_inflight_q15 = ge_ratio_q15_u64((uint64_t)external_inflight_u32, 16u);
    const uint16_t external_docs_q15 = ge_ratio_q15_u64((uint64_t)external_docs_u32, 32u);
    const uint16_t external_chunks_q15 = ge_ratio_q15_u64((uint64_t)external_chunks_u32, 64u);
    const uint16_t external_bytes_q15 =
        ge_ratio_q15_u64(sm.external_api_ingest_inflight_bytes_u64, (uint64_t)(8u * 1024u * 1024u));
    const uint16_t external_intent_tmp[3] = {
        external_pending_q15,
        external_inflight_q15,
        external_docs_q15
    };
    const uint16_t external_measured_tmp[4] = {
        external_pending_q15,
        external_inflight_q15,
        external_docs_q15,
        external_chunks_q15
    };
    const uint16_t external_spin_tmp[2] = {
        external_inflight_q15,
        external_bytes_q15
    };
    const uint16_t external_coupling_tmp[3] = {
        external_docs_q15,
        external_chunks_q15,
        external_inflight_q15
    };

    const uint16_t ai_conf_q15 = ge_abs_q32_32_to_q15_sat(ai.confidence_q32_32);
    const uint16_t ai_attractor_q15 = ge_abs_q32_32_to_q15_sat(ai.attractor_strength_q32_32);
    const uint16_t ai_command_q15 = ge_ratio_q15_u64((uint64_t)ai.command_count_u32, 128u);
    const uint16_t ai_action_q15 = ge_ratio_q15_u64((uint64_t)ai.action_log_count_u32, 128u);
    const uint16_t ai_anticipation_q15 =
        ge_ratio_q15_u64((uint64_t)ge_popcount_u64_local((uint64_t)ai.anticipation_flags_u16), 3u);
    const uint16_t ai_carrier_q15 = ge_mean_q15_u16(ai.carrier_band_q15, 8u);
    const uint16_t ai_measured_tmp[3] = {
        ai_conf_q15,
        ai_carrier_q15,
        ai_anticipation_q15
    };
    const uint16_t ai_spin_tmp[3] = {
        ai_command_q15,
        ai_action_q15,
        ai_attractor_q15
    };
    const uint16_t ai_coupling_tmp[3] = {
        ai.controller_authority_q15,
        ai.op_gain_q15,
        ai.op_phase_bias_q15
    };

    const uint16_t ai_data_spin_tmp[3] = {
        ai_data.measured_band_q15[4],
        ai_data.measured_band_q15[5],
        ai_data.measured_band_q15[6]
    };
    const uint16_t ai_data_coupling_tmp[3] = {
        ai_data.controller_authority_q15,
        ai_data.measured_band_q15[6],
        ai_data.measured_band_q15[7]
    };

    uint32_t lane_index_u32 = 0u;
    auto push_lane = [&](uint32_t subsystem_id_u32,
                         uint16_t intent_norm_q15,
                         uint16_t measured_norm_q15,
                         uint16_t spin_norm_q15,
                         uint16_t coupling_norm_q15) {
        if (lane_index_u32 >= EW_SUBSYSTEM_SUBSTRATE_LANE_CAP) return;
        EwSubsystemCalculusLane lane{};
        lane.subsystem_id_u32 = subsystem_id_u32;
        const EwSubsystemCalculusLane* prev_lane =
            ge_find_prev_subsystem_lane(prev, subsystem_id_u32);
        ge_finalize_subsystem_lane(lane,
                                   intent_norm_q15,
                                   measured_norm_q15,
                                   spin_norm_q15,
                                   coupling_norm_q15,
                                   prev_lane ? prev_lane->error_q32_32 : 0,
                                   prev_lane ? prev_lane->error_integral_q32_32 : 0);
        out.lanes[lane_index_u32++] = lane;
    };

    const uint16_t spectral_measured_tmp[4] = {
        process.energy_mean_q15,
        process.energy_peak_q15,
        process.coherence_norm_q15,
        process.temporal_coherence_q15
    };
    const uint16_t spectral_spin_tmp[3] = {
        process.source_vibration_q15,
        process.op_phase_bias_q15,
        process.interference_norm_q15
    };
    const uint16_t spectral_coupling_tmp[4] = {
        process.controller_authority_q15,
        process.calibration_authority_q15,
        process.learning_coupling_q15,
        process.phys_coherence_q15
    };
    push_lane(EW_SUBSYSTEM_PLANE_SPECTRAL_PROCESS,
              process.intent_norm_q15,
              ge_mean_q15_u16(spectral_measured_tmp, 4u),
              ge_mean_q15_u16(spectral_spin_tmp, 3u),
              ge_mean_q15_u16(spectral_coupling_tmp, 4u));
    push_lane(EW_SUBSYSTEM_PLANE_CAMERA,
              ge_bool_to_q15(camera_valid),
              ge_mean_q15_u16(camera_measured_tmp, 3u),
              ge_mean_q15_u16(camera_spin_tmp, 3u),
              ge_mean_q15_u16(camera_coupling_tmp, 4u));
    push_lane(EW_SUBSYSTEM_PLANE_RENDER,
              render_active_q15,
              ge_mean_q15_u16(render_measured_tmp, 3u),
              ge_mean_q15_u16(render_spin_tmp, 3u),
              ge_mean_q15_u16(render_coupling_tmp, 4u));
    push_lane(EW_SUBSYSTEM_PLANE_ASSET_OBJECT,
              ge_mean_q15_u16(asset_intent_tmp, 2u),
              ge_mean_q15_u16(asset_measured_tmp, 4u),
              ge_mean_q15_u16(asset_spin_tmp, 3u),
              ge_mean_q15_u16(asset_coupling_tmp, 3u));
    push_lane(EW_SUBSYSTEM_PLANE_NBODY,
              (nb.enabled_u32 != 0u) ? 32767u : (nbody_count_q15 / 2u),
              ge_mean_q15_u16(nbody_measured_tmp, 4u),
              ge_mean_q15_u16(nbody_spin_tmp, 2u),
              ge_mean_q15_u16(nbody_coupling_tmp, 3u));
    push_lane(EW_SUBSYSTEM_PLANE_CURRICULUM,
              32767u,
              ge_mean_q15_u16(curriculum_measured_tmp, 2u),
              ge_mean_q15_u16(curriculum_spin_tmp, 2u),
              ge_mean_q15_u16(curriculum_coupling_tmp, 3u));
    push_lane(EW_SUBSYSTEM_PLANE_AUTOMATION,
              automation_enabled_q15,
              ge_mean_q15_u16(automation_measured_tmp, 3u),
              ge_mean_q15_u16(automation_spin_tmp, 3u),
              ge_mean_q15_u16(automation_coupling_tmp, 3u));
    push_lane(EW_SUBSYSTEM_PLANE_LANGUAGE,
              32767u,
              ge_mean_q15_u16(lang_measured_tmp, 4u),
              ge_mean_q15_u16(lang_spin_tmp, 2u),
              ge_mean_q15_u16(lang_coupling_tmp, 3u));
    push_lane(EW_SUBSYSTEM_PLANE_MATH,
              32767u,
              ge_mean_q15_u16(math_measured_tmp, 4u),
              ge_mean_q15_u16(math_spin_tmp, 2u),
              ge_mean_q15_u16(math_coupling_tmp, 3u));
    push_lane(EW_SUBSYSTEM_PLANE_CORPUS,
              corpus_enabled_q15,
              ge_mean_q15_u16(corpus_measured_tmp, 4u),
              ge_mean_q15_u16(corpus_spin_tmp, 2u),
              ge_mean_q15_u16(corpus_coupling_tmp, 3u));
    push_lane(EW_SUBSYSTEM_PLANE_EXTERNAL_API,
              ge_mean_q15_u16(external_intent_tmp, 3u),
              ge_mean_q15_u16(external_measured_tmp, 4u),
              ge_mean_q15_u16(external_spin_tmp, 2u),
              ge_mean_q15_u16(external_coupling_tmp, 3u));
    push_lane(EW_SUBSYSTEM_PLANE_AI_CORE,
              ge_bool_to_q15(ai.valid_u32 != 0u),
              ge_mean_q15_u16(ai_measured_tmp, 3u),
              ge_mean_q15_u16(ai_spin_tmp, 3u),
              ge_mean_q15_u16(ai_coupling_tmp, 3u));
    push_lane(EW_SUBSYSTEM_PLANE_AI_DATA,
              ai_data.intent_norm_q15,
              ai_data.measured_norm_q15,
              ge_mean_q15_u16(ai_data_spin_tmp, 3u),
              ge_mean_q15_u16(ai_data_coupling_tmp, 3u));

    out.subsystem_count_u32 = lane_index_u32;
    out.valid_u32 = (lane_index_u32 != 0u) ? 1u : 0u;

    uint64_t intent_sum_u64 = 0u;
    uint64_t measured_sum_u64 = 0u;
    uint64_t residual_sum_u64 = 0u;
    uint64_t spin_sum_u64 = 0u;
    uint64_t coupling_sum_u64 = 0u;
    uint64_t derr_sum_u64 = 0u;
    uint64_t ierr_sum_u64 = 0u;
    uint64_t ctrl_sum_u64 = 0u;
    int64_t err_sum_q32_32 = 0;
    int64_t derr_sum_q32_32 = 0;
    int64_t ierr_sum_q32_32 = 0;
    uint32_t active_count_u32 = 0u;
    uint32_t dominant_score_u32 = 0u;
    for (uint32_t i = 0u; i < lane_index_u32; ++i) {
        const EwSubsystemCalculusLane& lane = out.lanes[i];
        const uint32_t score_u32 = (uint32_t)lane.residual_norm_q15 +
                                   (uint32_t)lane.spin_norm_q15 +
                                   (uint32_t)lane.coupling_norm_q15 +
                                   (uint32_t)lane.controller_authority_q15;
        if (score_u32 != 0u) active_count_u32 += 1u;
        if (score_u32 >= dominant_score_u32) {
            dominant_score_u32 = score_u32;
            out.dominant_subsystem_id_u32 = lane.subsystem_id_u32;
            out.dominant_spin_norm_q15 = lane.spin_norm_q15;
            out.dominant_coupling_norm_q15 = lane.coupling_norm_q15;
            out.dominant_residual_norm_q15 = lane.residual_norm_q15;
        }
        intent_sum_u64 += (uint64_t)lane.intent_norm_q15;
        measured_sum_u64 += (uint64_t)lane.measured_norm_q15;
        residual_sum_u64 += (uint64_t)lane.residual_norm_q15;
        spin_sum_u64 += (uint64_t)lane.spin_norm_q15;
        coupling_sum_u64 += (uint64_t)lane.coupling_norm_q15;
        derr_sum_u64 += (uint64_t)lane.error_delta_norm_q15;
        ierr_sum_u64 += (uint64_t)lane.error_integral_norm_q15;
        ctrl_sum_u64 += (uint64_t)lane.controller_authority_q15;
        err_sum_q32_32 += lane.error_q32_32;
        derr_sum_q32_32 += lane.error_delta_q32_32;
        ierr_sum_q32_32 += lane.error_integral_q32_32;
    }
    out.active_subsystem_count_u32 = active_count_u32;
    const uint32_t divisor_u32 = (active_count_u32 != 0u) ? active_count_u32 :
                                 ((lane_index_u32 != 0u) ? lane_index_u32 : 1u);
    out.aggregate_intent_norm_q15 = (uint16_t)std::min<uint64_t>(32767u, intent_sum_u64 / divisor_u32);
    out.aggregate_measured_norm_q15 = (uint16_t)std::min<uint64_t>(32767u, measured_sum_u64 / divisor_u32);
    out.aggregate_residual_norm_q15 = (uint16_t)std::min<uint64_t>(32767u, residual_sum_u64 / divisor_u32);
    out.aggregate_spin_norm_q15 = (uint16_t)std::min<uint64_t>(32767u, spin_sum_u64 / divisor_u32);
    out.aggregate_coupling_norm_q15 = (uint16_t)std::min<uint64_t>(32767u, coupling_sum_u64 / divisor_u32);
    out.aggregate_error_delta_norm_q15 = (uint16_t)std::min<uint64_t>(32767u, derr_sum_u64 / divisor_u32);
    out.aggregate_error_integral_norm_q15 = (uint16_t)std::min<uint64_t>(32767u, ierr_sum_u64 / divisor_u32);
    out.aggregate_controller_authority_q15 = (uint16_t)std::min<uint64_t>(32767u, ctrl_sum_u64 / divisor_u32);
    out.aggregate_error_q32_32 = err_sum_q32_32 / (int64_t)divisor_u32;
    out.aggregate_error_delta_q32_32 = derr_sum_q32_32 / (int64_t)divisor_u32;
    out.aggregate_error_integral_q32_32 = ierr_sum_q32_32 / (int64_t)divisor_u32;
}

static std::string ge_format_process_substrate_status_line(const EwProcessSubstrateTelemetry& t,
                                                           const char* prefix_ascii) {
    std::ostringstream oss;
    oss << (prefix_ascii ? prefix_ascii : "PROCESS_SUBSTRATE_STATUS")
        << " valid=" << t.valid_u32
        << " tick=" << t.tick_u64
        << " spectral_anchor=" << t.spectral_anchor_id_u32
        << " coherence_anchor=" << t.coherence_anchor_id_u32
        << " fanout=" << t.fanout_budget_u32
        << " intent_q15=" << t.intent_norm_q15
        << " Emean_q15=" << t.energy_mean_q15
        << " Epeak_q15=" << t.energy_peak_q15
        << " LeakAbs_q15=" << t.leakage_abs_q15
        << " residual_q15=" << t.residual_norm_q15
        << " dErr_q15=" << t.error_delta_norm_q15
        << " intErr_q15=" << t.error_integral_norm_q15
        << " ctrl_q15=" << t.controller_authority_q15
        << " dt_q32_32=" << t.dt_scale_q32_32
        << " visc_q32_32=" << t.viscosity_bias_q32_32
        << " err_q32_32=" << t.error_q32_32
        << " derr_q32_32=" << t.error_delta_q32_32
        << " ierr_q32_32=" << t.error_integral_q32_32
        << " gpuF_q15=" << t.gpu_freq_norm_q15
        << " gpuA_q15=" << t.gpu_amp_norm_q15
        << " gpuV_q15=" << t.gpu_volt_norm_q15
        << " intf_q15=" << t.interference_norm_q15
        << " coh_q15=" << t.coherence_norm_q15
        << " obs_q15=" << t.observer_norm_q15
        << " vib_q15=" << t.source_vibration_q15
        << " calctrl_q15=" << t.calibration_authority_q15
        << " calerr_q32_32=" << t.calibration_error_q32_32
        << " calderr_q32_32=" << t.calibration_delta_q32_32
        << " calierr_q32_32=" << t.calibration_integral_q32_32
        << " gain_q15=" << t.op_gain_q15
        << " phase_q15=" << t.op_phase_bias_q15
        << " learn_q15=" << t.learning_coupling_q15
        << " phys_q15=" << t.phys_coherence_q15
        << " lrncoh_q15=" << t.learning_coherence_q15
        << " tempcoh_q15=" << t.temporal_coherence_q15
        << " V=" << t.last_v_code_u16
        << " I=" << t.last_i_code_u16
        << " pending=" << (uint32_t)t.residual_pending_u8
        << " hold=" << (uint32_t)t.hold_tick_u8
        << " commit=" << (uint32_t)t.last_commit_u8;
    return oss.str();
}

static std::string ge_format_subsystem_substrate_status_line(const EwSubsystemSubstrateTelemetry& t,
                                                             const char* prefix_ascii) {
    std::ostringstream oss;
    oss << (prefix_ascii ? prefix_ascii : "SUBSYSTEM_SUBSTRATE_STATUS")
        << " valid=" << t.valid_u32
        << " tick=" << t.tick_u64
        << " count=" << t.subsystem_count_u32
        << " active=" << t.active_subsystem_count_u32
        << " dominant=" << ge_subsystem_plane_name_ascii(t.dominant_subsystem_id_u32)
        << " aggIntent_q15=" << t.aggregate_intent_norm_q15
        << " aggMeasured_q15=" << t.aggregate_measured_norm_q15
        << " aggResidual_q15=" << t.aggregate_residual_norm_q15
        << " aggSpin_q15=" << t.aggregate_spin_norm_q15
        << " aggCoupling_q15=" << t.aggregate_coupling_norm_q15
        << " aggCtrl_q15=" << t.aggregate_controller_authority_q15
        << " aggErr_q32_32=" << t.aggregate_error_q32_32
        << " aggDErr_q32_32=" << t.aggregate_error_delta_q32_32
        << " aggIErr_q32_32=" << t.aggregate_error_integral_q32_32;
    for (uint32_t i = 0u; i < t.subsystem_count_u32 &&
                          i < EW_SUBSYSTEM_SUBSTRATE_LANE_CAP; ++i) {
        const EwSubsystemCalculusLane& lane = t.lanes[i];
        if (lane.subsystem_id_u32 == EW_SUBSYSTEM_PLANE_NONE) continue;
        oss << " " << ge_subsystem_plane_name_ascii(lane.subsystem_id_u32)
            << "(res=" << lane.residual_norm_q15
            << ",spin=" << lane.spin_norm_q15
            << ",cpl=" << lane.coupling_norm_q15
            << ",ctrl=" << lane.controller_authority_q15
            << ")";
    }
    return oss.str();
}

static std::string ge_format_ai_substrate_status_line(const EwAiSubstrateTelemetry& t,
                                                      const char* prefix_ascii) {
    std::ostringstream oss;
    oss << (prefix_ascii ? prefix_ascii : "AI_SUBSTRATE_STATUS")
        << " valid=" << t.valid_u32
        << " tick=" << t.tick_u64
        << " class=" << t.class_id_u32
        << " sig9=" << t.sig9_u64
        << " obs=" << t.observation_seq_u64
        << " cmd_obs=" << t.dispatched_observation_seq_u64
        << " conf_q32_32=" << t.confidence_q32_32
        << " attract_q32_32=" << t.attractor_strength_q32_32
        << " cmds=" << t.command_count_u32
        << " actions=" << t.action_log_count_u32
        << " carrier_q63=" << t.carrier_pulse_q63
        << " totalw_q63=" << t.carrier_total_weight_q63
        << " pending_api=" << t.pending_external_api_u32
        << " inflight_api=" << t.inflight_external_api_u32
        << " fanout=" << t.fanout_budget_u32
        << " ant=" << t.anticipation_flags_u16
        << " ctrl_q15=" << t.controller_authority_q15
        << " gain_q15=" << t.op_gain_q15
        << " phase_q15=" << t.op_phase_bias_q15
        << " mem_q15=" << t.memory_norm_q15
        << " reason_q15=" << t.reasoning_norm_q15
        << " plan_q15=" << t.planning_norm_q15
        << " create_q15=" << t.creativity_norm_q15
        << " perceive_q15=" << t.perception_norm_q15
        << " bind_q15=" << t.temporal_binding_norm_q15
        << " drift_q15=" << t.crawler_drift_norm_q15
        << " net_q15=" << t.network_coherence_norm_q15
        << " b0=" << t.carrier_band_q15[0]
        << " b1=" << t.carrier_band_q15[1]
        << " b2=" << t.carrier_band_q15[2]
        << " b3=" << t.carrier_band_q15[3]
        << " b4=" << t.carrier_band_q15[4]
        << " b5=" << t.carrier_band_q15[5]
        << " b6=" << t.carrier_band_q15[6]
        << " b7=" << t.carrier_band_q15[7]
        << " kind=" << t.last_action_kind_u16
        << " profile=" << t.last_action_profile_u16
        << " anchor=" << t.last_target_anchor_id_u32
        << " f=" << t.last_f_code_i32
        << " a=" << t.last_a_code_u16
        << " v=" << t.last_v_code_u16
        << " i=" << t.last_i_code_u16;
    return oss.str();
}

static std::string ge_format_ai_data_substrate_status_line(const EwAiDataSubstrateTelemetry& t,
                                                           const char* prefix_ascii) {
    std::ostringstream oss;
    oss << (prefix_ascii ? prefix_ascii : "AI_DATA_SUBSTRATE_STATUS")
        << " valid=" << t.valid_u32
        << " tick=" << t.tick_u64
        << " corpus=" << t.corpus_artifact_count_u32
        << " corpus_txt=" << t.corpus_text_artifact_count_u32
        << " manifest=" << t.manifest_record_count_u32
        << " pending_metrics=" << t.pending_metric_count_u32
        << " completed_metrics=" << t.completed_metric_count_u32
        << " accepted_metrics=" << t.accepted_metric_count_u32
        << " lex=" << t.language_word_count_u32
        << " pron=" << t.language_pron_count_u32
        << " rel=" << t.language_relation_count_u32
        << " concept=" << t.language_concept_count_u32
        << " speech=" << t.language_speech_utt_count_u32
        << " pemdas=" << t.math_pemdas_cases_passed_u32 << "/" << t.math_pemdas_cases_total_u32
        << " graph=" << t.math_graph_packets_emitted_u32
        << " khan_pages=" << t.math_khan_pages_seen_u32
        << " khan_chars=" << t.math_khan_chars_ingested_u32
        << " pending_api=" << t.pending_external_api_u32
        << " inflight_api=" << t.inflight_external_api_u32
        << " crawl_pending=" << t.crawler_pending_obs_u32
        << " crawl_enq=" << t.crawler_enqueued_obs_u64
        << " crawl_adm=" << t.crawler_admitted_pulses_u64
        << " crawl_drop=" << t.crawler_dropped_obs_u64
        << " crawl_trunc=" << t.crawler_truncated_segments_u64
        << " intent_q15=" << t.intent_norm_q15
        << " measured_q15=" << t.measured_norm_q15
        << " residual_q15=" << t.residual_norm_q15
        << " dErr_q15=" << t.error_delta_norm_q15
        << " intErr_q15=" << t.error_integral_norm_q15
        << " ctrl_q15=" << t.controller_authority_q15
        << " crawl_flow_q15=" << t.crawler_flow_norm_q15
        << " crawl_drift_q15=" << t.crawler_drift_norm_q15
        << " crawl_intf_q15=" << t.crawler_interference_norm_q15
        << " crawl_coh_q15=" << t.crawler_coherence_norm_q15
        << " mem_q15=" << t.temporal_memory_norm_q15
        << " pred_q15=" << t.temporal_prediction_norm_q15
        << " gpu_grad_q15=" << t.gpu_gradient_energy_norm_q15
        << " gpu_spider_f=" << t.gpu_spider_f_code_i32
        << " gpu_spider_a=" << t.gpu_spider_a_code_u16
        << " gpu_spider_v=" << t.gpu_spider_v_code_u16
        << " gpu_spider_i=" << t.gpu_spider_i_code_u16
        << " api_decode_n=" << t.api_decode_count_u16
        << " api_http=" << t.api_decode_http_status_s32
        << " api_decode_q15=" << t.api_decode_norm_q15
        << " api_decode_f=" << t.api_decode_f_code_i32
        << " err_q32_32=" << t.error_q32_32
        << " derr_q32_32=" << t.error_delta_q32_32
        << " ierr_q32_32=" << t.error_integral_q32_32
        << " pred_q32_32=" << t.temporal_prediction_q32_32
        << " t0=" << t.intent_band_q15[0]
        << " t1=" << t.intent_band_q15[1]
        << " t2=" << t.intent_band_q15[2]
        << " t3=" << t.intent_band_q15[3]
        << " t4=" << t.intent_band_q15[4]
        << " t5=" << t.intent_band_q15[5]
        << " t6=" << t.intent_band_q15[6]
        << " t7=" << t.intent_band_q15[7]
        << " m0=" << t.measured_band_q15[0]
        << " m1=" << t.measured_band_q15[1]
        << " m2=" << t.measured_band_q15[2]
        << " m3=" << t.measured_band_q15[3]
        << " m4=" << t.measured_band_q15[4]
        << " m5=" << t.measured_band_q15[5]
        << " m6=" << t.measured_band_q15[6]
        << " m7=" << t.measured_band_q15[7];
    return oss.str();
}

static std::string ge_format_repo_reader_status_line(const SubstrateManager& sm,
                                                     const char* prefix_ascii) {
    std::ostringstream oss;
    oss << prefix_ascii
        << " enabled=" << (sm.repo_reader.enabled ? 1u : 0u)
        << " scanned=" << (sm.repo_reader.scanned ? 1u : 0u)
        << " files=" << sm.repo_reader.files_rel_ascii.size()
        << " cursor=" << sm.repo_reader.cursor_u32
        << " files_per_tick=" << sm.repo_reader_files_per_tick_u32
        << " bytes_per_file=" << sm.repo_reader_bytes_per_file_u32;
    if (!sm.repo_reader.repo_root_ascii.empty()) {
        oss << " root=" << sm.repo_reader.repo_root_ascii;
    }
    return oss.str();
}

static std::string ge_format_merged_repo_status_line(const SubstrateManager& sm,
                                                     const char* prefix_ascii) {
    const std::filesystem::path repo_root =
        sm.repo_reader.repo_root_ascii.empty()
            ? std::filesystem::current_path()
            : std::filesystem::path(sm.repo_reader.repo_root_ascii);
    std::ostringstream oss;
    oss << prefix_ascii
        << " ge_canon=" << (std::filesystem::exists(repo_root / "ge_canon") ? 1u : 0u)
        << " research_live_day_src=" << (std::filesystem::exists(repo_root / "src/tools/research_live_day_sim_main.cpp") ? 1u : 0u)
        << " package_script=" << (std::filesystem::exists(repo_root / "scripts/package_project.py") ? 1u : 0u);
    if (!sm.repo_reader.repo_root_ascii.empty()) {
        oss << " repo_root=" << sm.repo_reader.repo_root_ascii;
    }
    return oss.str();
}
// Build a view matrix (row-major 4x4) in Q16.16 from a camera pose expressed as
// (pos_q16_16, quat_q16_16). Quaternion is interpreted as camera->world rotation.
// View matrix is world->camera: R^T and -R^T * pos.
static void ew_build_view_mat_q16_16(const int32_t pos_xyz_q16_16[3], const int32_t q_q16_16[4], int32_t out_m16_q16_16[16]) {
    const int64_t x = (int64_t)q_q16_16[0];
    const int64_t y = (int64_t)q_q16_16[1];
    const int64_t z = (int64_t)q_q16_16[2];
    const int64_t w = (int64_t)q_q16_16[3];

    auto mul_q16 = [&](int64_t a, int64_t b) -> int64_t { return (a * b) >> 16; };
    const int64_t xx = mul_q16(x, x);
    const int64_t yy = mul_q16(y, y);
    const int64_t zz = mul_q16(z, z);
    const int64_t xy = mul_q16(x, y);
    const int64_t xz = mul_q16(x, z);
    const int64_t yz = mul_q16(y, z);
    const int64_t xw = mul_q16(x, w);
    const int64_t yw = mul_q16(y, w);
    const int64_t zw = mul_q16(z, w);

    const int64_t one = 65536;
    const int64_t two = 2;

    int32_t R[9];
    R[0] = (int32_t)(one - two * (yy + zz));
    R[1] = (int32_t)(two * (xy - zw));
    R[2] = (int32_t)(two * (xz + yw));

    R[3] = (int32_t)(two * (xy + zw));
    R[4] = (int32_t)(one - two * (xx + zz));
    R[5] = (int32_t)(two * (yz - xw));

    R[6] = (int32_t)(two * (xz - yw));
    R[7] = (int32_t)(two * (yz + xw));
    R[8] = (int32_t)(one - two * (xx + yy));

    const int32_t Rt00 = R[0];
    const int32_t Rt01 = R[3];
    const int32_t Rt02 = R[6];
    const int32_t Rt10 = R[1];
    const int32_t Rt11 = R[4];
    const int32_t Rt12 = R[7];
    const int32_t Rt20 = R[2];
    const int32_t Rt21 = R[5];
    const int32_t Rt22 = R[8];

    const int64_t px = (int64_t)pos_xyz_q16_16[0];
    const int64_t py = (int64_t)pos_xyz_q16_16[1];
    const int64_t pz = (int64_t)pos_xyz_q16_16[2];

    auto dot3_q16 = [&](int32_t a0, int32_t a1, int32_t a2, int64_t bx, int64_t by, int64_t bz) -> int32_t {
        int64_t s = ((int64_t)a0 * bx + (int64_t)a1 * by + (int64_t)a2 * bz) >> 16;
        if (s > 2147483647LL) s = 2147483647LL;
        if (s < -2147483648LL) s = -2147483648LL;
        return (int32_t)s;
    };

    const int32_t tx = (int32_t)(-dot3_q16(Rt00, Rt01, Rt02, px, py, pz));
    const int32_t ty = (int32_t)(-dot3_q16(Rt10, Rt11, Rt12, px, py, pz));
    const int32_t tz = (int32_t)(-dot3_q16(Rt20, Rt21, Rt22, px, py, pz));

    out_m16_q16_16[0] = Rt00; out_m16_q16_16[1] = Rt01; out_m16_q16_16[2] = Rt02; out_m16_q16_16[3] = tx;
    out_m16_q16_16[4] = Rt10; out_m16_q16_16[5] = Rt11; out_m16_q16_16[6] = Rt12; out_m16_q16_16[7] = ty;
    out_m16_q16_16[8] = Rt20; out_m16_q16_16[9] = Rt21; out_m16_q16_16[10]= Rt22; out_m16_q16_16[11]= tz;
    out_m16_q16_16[12]= 0;    out_m16_q16_16[13]= 0;    out_m16_q16_16[14]= 0;    out_m16_q16_16[15]= 65536;
}


static inline int64_t ew_mul_q63_local(int64_t a_q63, int64_t b_q63) {
    __int128 p = (__int128)a_q63 * (__int128)b_q63;
    return (int64_t)(p >> 63);
}

static inline int64_t i64_abs(int64_t v) {
    return (v < 0) ? -v : v;
}

// -----------------------------------------------------------------------------
// Anchored code synthesis (symbol index + patch planning) lives inside the
// substrate microprocessor tick, not as an external "agent".
// -----------------------------------------------------------------------------

static inline bool ew_is_ident_start(char c) {
    return (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static inline bool ew_is_ident_body(char c) {
    return ew_is_ident_start(c) || (c >= '0' && c <= '9');
}

static inline void ew_tokenize_idents_ascii_unique(const std::string& s, std::vector<std::string>& out) {
    out.clear();
    out.reserve(128);
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        unsigned char uc = (unsigned char)s[i];
        if (uc >= 0x80) { ++i; continue; }
        char c = (char)uc;
        if (!ew_is_ident_start(c)) { ++i; continue; }
        size_t j = i + 1;
        while (j < n) {
            unsigned char uj = (unsigned char)s[j];
            if (uj >= 0x80) break;
            if (!ew_is_ident_body((char)uj)) break;
            ++j;
        }
        std::string tok;
        tok.reserve(j - i);
        for (size_t k = i; k < j; ++k) {
            char cc = s[k];
            if (cc >= 'A' && cc <= 'Z') cc = (char)(cc - 'A' + 'a');
            tok.push_back(cc);
        }
        if (tok.size() >= 3) {
            static const char* stop[] = {"the","and","for","with","from","this","that","true","false","uint","int","size"};
            bool is_stop = false;
            for (size_t si = 0; si < sizeof(stop)/sizeof(stop[0]); ++si) {
                if (tok == stop[si]) { is_stop = true; break; }
            }
            if (!is_stop) out.push_back(tok);
        }
        i = j;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

static inline std::string ew_lower_ascii_only(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x80) continue;
        char cc = (char)c;
        if (cc >= 'A' && cc <= 'Z') cc = (char)(cc - 'A' + 'a');
        out.push_back(cc);
    }
    return out;
}

static inline void ew_trim_left_ws(std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    if (i) s = s.substr(i);
}

static inline std::string ew_extract_first_ident_ascii(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char uc = (unsigned char)s[i];
        if (uc >= 0x80) continue;
        if (!ew_is_ident_start((char)uc)) continue;
        size_t j = i + 1;
        while (j < s.size()) {
            unsigned char uj = (unsigned char)s[j];
            if (uj >= 0x80) break;
            if (!ew_is_ident_body((char)uj)) break;
            ++j;
        }
        return s.substr(i, j - i);
    }
    return std::string();
}

static inline bool ew_looks_like_rel_path_hint(const std::string& s) {
    return (s.find(".cpp") != std::string::npos) || (s.find(".hpp") != std::string::npos) ||
           (s.find("CMakeLists.txt") != std::string::npos) || (s.find(".cmake") != std::string::npos);
}

static std::string ew_pick_explicit_path_or_empty(const std::string& request) {
    const std::string lc = ew_lower_ascii_only(request);
    const char* keys[] = {"to file ", "file ", "path ", "in "};
    for (size_t ki = 0; ki < sizeof(keys)/sizeof(keys[0]); ++ki) {
        const std::string k(keys[ki]);
        size_t p = lc.find(k);
        if (p == std::string::npos) continue;
        size_t s = p + k.size();
        size_t e = s;
        while (e < request.size() && request[e] != ' ' && request[e] != '\t' && request[e] != '\r' && request[e] != '\n') ++e;
        if (e <= s) continue;
        std::string cand = request.substr(s, e - s);
        if (ew_looks_like_rel_path_hint(cand)) return cand;
    }
    return std::string();
}

static void ew_synth_index_rebuild(SubstrateManager* sm) {
    if (!sm) return;
    const uint64_t rev = sm->inspector_fields.revision_u64();
    if (rev == sm->synth_index_revision_u64) return;

    sm->synth_index_revision_u64 = rev;
    sm->synth_artifacts.clear();
    sm->synth_sym_refs.clear();

    std::vector<EwInspectorArtifact> all;
    sm->inspector_fields.snapshot_all(all);
    std::sort(all.begin(), all.end(), [](const EwInspectorArtifact& a, const EwInspectorArtifact& b) {
        if (a.rel_path != b.rel_path) return a.rel_path < b.rel_path;
        return a.kind_u32 < b.kind_u32;
    });

    std::vector<uint32_t> art_ids;
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> lens;
    std::string concat;
    concat.reserve(4096);

    for (size_t ai = 0; ai < all.size(); ++ai) {
        const EwInspectorArtifact& a = all[ai];
        const std::string& lp = a.rel_path;
        const bool is_code = (a.kind_u32 == EW_ARTIFACT_CPP) || (a.kind_u32 == EW_ARTIFACT_HPP) || (a.kind_u32 == EW_ARTIFACT_CMAKE);
        const bool is_sym_stream = (lp.find(".code_symbols.txt") != std::string::npos);
        if (!is_code && !is_sym_stream) continue;

        SubstrateManager::EwSynthArtifactInfo info;
        info.rel_path = a.rel_path;
        info.kind_u32 = a.kind_u32;
        const uint32_t art_index = (uint32_t)sm->synth_artifacts.size();
        sm->synth_artifacts.push_back(info);

        const std::string& body = a.payload;
        const size_t cap = (body.size() < (size_t)131072) ? body.size() : (size_t)131072;
        art_ids.push_back(art_index);
        offsets.push_back((uint32_t)concat.size());
        lens.push_back((uint32_t)cap);
        concat.append(body.data(), cap);
    }

    if (sm->synth_artifacts.empty()) return;

#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
    // CUDA backend: emits fixed-width 9D lane codes per symbol (no hashing).
    uint32_t max_symbols_per_art = 16u;
    for (uint32_t i = 0u; i < (uint32_t)lens.size(); ++i) {
        uint32_t cap = (lens[i] / 4u);
        if (cap < 16u) cap = 16u;
        if (cap > max_symbols_per_art) max_symbols_per_art = cap;
    }

    std::vector<EwSymbolToken9> symbols((size_t)sm->synth_artifacts.size() * (size_t)max_symbols_per_art);
    std::vector<uint32_t> counts(sm->synth_artifacts.size(), 0u);

    const bool ok = ew_cuda_tokenize_symbols_batch(
        (const uint8_t*)concat.data(),
        offsets.data(),
        lens.data(),
        art_ids.data(),
        (uint32_t)sm->synth_artifacts.size(),
        symbols.data(),
        counts.data(),
        max_symbols_per_art
    );

    if (ok) {
        for (uint32_t art_i = 0u; art_i < (uint32_t)sm->synth_artifacts.size(); ++art_i) {
            const uint16_t base_w = 8192;
            const uint32_t n_sym = counts[art_i];
            for (uint32_t t = 0u; t < n_sym; ++t) {
                const EwSymbolToken9& tt = symbols[(size_t)art_i * (size_t)max_symbols_per_art + t];
                SubstrateManager::EwSynthSymRef r{};
                for (int j = 0; j < 9; ++j) r.sym_id9.u32[j] = tt.lanes_u32[j];
                r.art_index_u32 = art_i;
                r.weight_q15 = (uint16_t)std::min<uint32_t>(32768u, (uint32_t)base_w + (tt.len_u32 >= 16u ? 16384u : (tt.len_u32 * 1024u)));
                sm->synth_sym_refs.push_back(r);
            }
        }

        std::stable_sort(sm->synth_sym_refs.begin(), sm->synth_sym_refs.end(),
            [](const SubstrateManager::EwSynthSymRef& a, const SubstrateManager::EwSynthSymRef& b) {
                if (a.sym_id9 != b.sym_id9) return a.sym_id9 < b.sym_id9;
                if (a.art_index_u32 != b.art_index_u32) return a.art_index_u32 < b.art_index_u32;
                return a.weight_q15 < b.weight_q15;
            });
        return;
    }
#endif

    // CPU backend: direct ASCII segment projection to EwId9 (no hashing).
    auto is_ident_start = [](char c)->bool { return (c=='_') || (c>='A'&&c<='Z') || (c>='a'&&c<='z'); };
    auto is_ident_body = [&](char c)->bool { return is_ident_start(c) || (c>='0'&&c<='9'); };

    for (uint32_t art_i = 0u; art_i < (uint32_t)sm->synth_artifacts.size(); ++art_i) {
        const uint32_t off = offsets[art_i];
        const uint32_t len = lens[art_i];
        const char* p = concat.data() + off;
        uint32_t i = 0u;
        while (i < len) {
            const char c = p[i];
            if (!is_ident_start(c)) { ++i; continue; }
            const uint32_t start = i++;
            while (i < len && is_ident_body(p[i])) ++i;
            const uint32_t seg_len = i - start;

            SubstrateManager::EwSynthSymRef r{};
            r.sym_id9 = ew_id9_from_ascii((const uint8_t*)(p + start), (size_t)seg_len);
            r.art_index_u32 = art_i;
            r.weight_q15 = (uint16_t)std::min<uint32_t>(32768u, 8192u + (seg_len >= 16u ? 16384u : (seg_len * 1024u)));
            sm->synth_sym_refs.push_back(r);
        }
    }

    std::stable_sort(sm->synth_sym_refs.begin(), sm->synth_sym_refs.end(),
        [](const SubstrateManager::EwSynthSymRef& a, const SubstrateManager::EwSynthSymRef& b) {
            if (a.sym_id9 != b.sym_id9) return a.sym_id9 < b.sym_id9;
            if (a.art_index_u32 != b.art_index_u32) return a.art_index_u32 < b.art_index_u32;
            return a.weight_q15 < b.weight_q15;
        });
}

static void ew_synth_query_best(SubstrateManager* sm, const std::string& request_utf8, uint32_t max_out,
                               std::vector<std::pair<std::string, uint32_t>>& out) {
    out.clear();
    if (!sm) return;
    ew_synth_index_rebuild(sm);
    if (sm->synth_artifacts.empty() || sm->synth_sym_refs.empty()) return;
    if (max_out < 1u) max_out = 1u;
    if (max_out > 64u) max_out = 64u;

    std::vector<std::string> toks;
    ew_tokenize_idents_ascii_unique(request_utf8, toks);
    if (toks.empty()) return;

    std::vector<uint32_t> score(sm->synth_artifacts.size(), 0u);
    for (const auto& t : toks) {
        // No hashing/crypto/token ids: request segments are projected to 9D ids.
        const EwId9 key = ew_id9_from_string_ascii(t);
        auto lb = std::lower_bound(sm->synth_sym_refs.begin(), sm->synth_sym_refs.end(), key,
            [](const SubstrateManager::EwSynthSymRef& r, const EwId9& k){ return r.sym_id9 < k; });
        auto ub = std::upper_bound(sm->synth_sym_refs.begin(), sm->synth_sym_refs.end(), key,
            [](const EwId9& k, const SubstrateManager::EwSynthSymRef& r){ return k < r.sym_id9; });
        for (auto it = lb; it != ub; ++it) {
            const uint32_t ai = it->art_index_u32;
            if (ai < score.size()) score[ai] += (uint32_t)it->weight_q15;
        }
    }

    std::vector<uint32_t> idx;
    idx.reserve(sm->synth_artifacts.size());
    for (uint32_t i = 0; i < (uint32_t)sm->synth_artifacts.size(); ++i) {
        if (score[i] != 0u) idx.push_back(i);
    }
    std::sort(idx.begin(), idx.end(), [&](uint32_t a, uint32_t b) {
        if (score[a] != score[b]) return score[a] > score[b];
        return sm->synth_artifacts[a].rel_path < sm->synth_artifacts[b].rel_path;
    });

    const uint32_t take = ((uint32_t)idx.size() < max_out) ? (uint32_t)idx.size() : max_out;
    out.reserve(take);
    for (uint32_t i = 0; i < take; ++i) {
        out.push_back({sm->synth_artifacts[idx[i]].rel_path, score[idx[i]]});
    }
}

static bool ew_synth_emit_cli_tool(SubstrateManager* sm, const std::string& tool_name) {
    if (!sm) return false;
    if (tool_name.empty()) return false;

    const std::string base_dir = "Draft Container/GenesisEngine/src/tools/";
    const std::string cpp_path = base_dir + tool_name + "_main.cpp";

    std::string cpp;
    cpp.reserve(2048);
    cpp += "#include \"GE_runtime.hpp\"\n\n";
    cpp += "#include <cstdio>\n";
    cpp += "#include <string>\n\n";
    cpp += "static std::string join_args(int argc, char** argv) {\n";
    cpp += "    std::string out;\n";
    cpp += "    for (int i = 1; i < argc; ++i) {\n";
    cpp += "        if (i > 1) out.push_back(' ');\n";
    cpp += "        out += argv[i];\n";
    cpp += "    }\n";
    cpp += "    return out;\n";
    cpp += "}\n\n";
    cpp += "int main(int argc, char** argv) {\n";
    cpp += "    if (argc < 2) {\n";
    cpp += "        std::fprintf(stderr, \"usage: ";
    cpp += tool_name;
    cpp += " <request>\\n\");\n";
    cpp += "        return 2;\n";
    cpp += "    }\n";
    cpp += "    SubstrateManager sm;\n";
    cpp += "    sm.set_projection_seed(1u);\n";
    cpp += "    const std::string req = join_args(argc, argv);\n";
    cpp += "    sm.ui_submit_user_text_line(std::string(\"SYNTHCODE:\") + req);\n";
    cpp += "    for (int i = 0; i < 12; ++i) sm.tick();\n";
    cpp += "    for (;;) {\n";
    cpp += "        std::string out;\n";
    cpp += "        if (!sm.ui_pop_output_text(out)) break;\n";
    cpp += "        std::printf(\"%s\\n\", out.c_str());\n";
    cpp += "    }\n";
    cpp += "    return 0;\n";
    cpp += "}\n";

    EwInspectorArtifact a;
    a.rel_path = cpp_path;
    a.kind_u32 = (uint32_t)EW_ARTIFACT_CPP;
    a.payload = cpp;
    a.producer_operator_id_u32 = 0xE331u;
    a.producer_tick_u64 = sm->canonical_tick;
    const EwCoherenceResult cr = EwCoherenceGate::validate_artifact(a.rel_path, a.kind_u32, a.payload);
    a.coherence_q15 = cr.coherence_q15;
    a.commit_ready = cr.commit_ready;
    a.denial_code_u32 = cr.denial_code_u32;
    sm->inspector_fields.upsert(a);

    const std::string cmake_path = "Draft Container/GenesisEngine/CMakeLists.txt";
    EwPatchSpec ps;
    ps.mode_u16 = EW_PATCH_APPEND_EOF;
    ps.text = "\n# EW_ANCHOR:EW_SYNTH_TOOLS\n";
    ps.text += "add_executable(";
    ps.text += tool_name;
    ps.text += " src/tools/";
    ps.text += tool_name;
    ps.text += "_main.cpp)\n";
    ps.text += "target_link_libraries(";
    ps.text += tool_name;
    ps.text += " PRIVATE EigenWareCore)\n";
    (void)code_apply_patch_coherence_gated(sm, cmake_path, (uint32_t)EW_ARTIFACT_CMAKE, ps, 0xE332u);
    return true;
}

static bool ew_synthcode_execute(SubstrateManager* sm, const std::string& request_utf8) {
    if (!sm) return false;
    std::string req = request_utf8;
    ew_trim_left_ws(req);
    if (req.empty()) return false;

    // Emit deterministic index stats for observability.
    ew_synth_index_rebuild(sm);
    {
        std::string msg = "SYNTHCODE_INDEX artifacts=";
        msg += std::to_string((unsigned long long)sm->synth_artifacts.size());
        msg += " sym_refs=";
        msg += std::to_string((unsigned long long)sm->synth_sym_refs.size());
        sm->emit_ui_line(msg);
    }

    const std::string lc = ew_lower_ascii_only(req);

    if (lc.find("create module") != std::string::npos || lc.find("new module") != std::string::npos) {
        std::string name = ew_extract_first_ident_ascii(req);
        if (name.empty()) name = "eigenware_module";
        code_emit_minimal_cpp_module(sm, name);
        sm->emit_ui_line(std::string("SYNTHCODE_CREATED_MODULE ") + name);
        return true;
    }

    if (lc.find("cli") != std::string::npos || lc.find("tool") != std::string::npos) {
        std::string name = ew_extract_first_ident_ascii(req);
        if (name.empty()) name = "ew_synth_tool";
        std::string nn;
        for (size_t i = 0; i < name.size(); ++i) {
            char c = name[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') nn.push_back(c);
        }
        if (nn.empty()) nn = "ew_synth_tool";
        (void)ew_synth_emit_cli_tool(sm, nn);
        sm->emit_ui_line(std::string("SYNTHCODE_CREATED_TOOL ") + nn);
        return true;
    }

    // No stubs. SYNTHCODE only emits complete, coherence-gated artifacts (modules/tools)
    // or fails closed. If the request is not one of the implemented emitters,
    // we refuse deterministically so the repo never accumulates incomplete logic.
    sm->emit_ui_line("SYNTHCODE_REFUSED_NO_IMPLEMENTATION");
    return false;
}
// Game-engine bootstrap request. This is a substrate-managed process that seeds
// measurable tasks for the "game" curriculum bucket and emits minimal engine
// scaffolding modules as inspector artifacts for later hydration.
static bool ew_gameboot_execute(SubstrateManager* sm, const std::string& request_utf8) {
    if (!sm) return false;
    std::string req = request_utf8;
    ew_trim_left_ws(req);
    if (req.empty()) req = "bootstrap";

    // Enqueue measurable checkpoint tasks for Stage 6 (game engine bootstrap).
    // These tasks are validated by the existing learning gate in the sandbox.
    {
        genesis::MetricTask t{};
        t.target.kind = genesis::MetricKind::Game_RenderPipeline_Determinism;
        t.context_anchor_id_u32 = genesis::EwObjectAncilla::LEDGER_ANCILLA_ID;
        t.tries_remaining_u64 = 1024;
        t.tries_per_step_u32 = 32;
        t.ticks_remaining_u32 = 360;
        sm->learning_gate.registry().enqueue_task(t);
    }
    {
        genesis::MetricTask t{};
        t.target.kind = genesis::MetricKind::Game_SceneGraph_TransformConsistency;
        t.context_anchor_id_u32 = genesis::EwObjectAncilla::LEDGER_ANCILLA_ID;
        t.tries_remaining_u64 = 1024;
        t.tries_per_step_u32 = 32;
        t.ticks_remaining_u32 = 360;
        sm->learning_gate.registry().enqueue_task(t);
    }
    {
        genesis::MetricTask t{};
        t.target.kind = genesis::MetricKind::Game_EditorHook_CommandSurface;
        t.context_anchor_id_u32 = genesis::EwObjectAncilla::LEDGER_ANCILLA_ID;
        t.tries_remaining_u64 = 1024;
        t.tries_per_step_u32 = 32;
        t.ticks_remaining_u32 = 360;
        sm->learning_gate.registry().enqueue_task(t);
    }

    // Emit minimal scaffolding modules (no autonomous side-effects).
    // These are projections only; hydration remains a gated, explicit step.
    code_emit_minimal_cpp_module(sm, "GE_game_stage");
    code_emit_minimal_cpp_module(sm, "GE_game_world");
    code_emit_minimal_cpp_module(sm, "GE_editor_ai_hooks");

    sm->emit_ui_line("GAMEBOOT_ENQUEUED req=" + req);
    return true;
}




static inline int64_t clamp_q32_32(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int64_t q32_32_div_i64(int64_t num, int64_t den) {
    if (den == 0) return 0;
    __int128 p = (__int128)num << 32;
    return (int64_t)(p / (__int128)den);
}

static inline int32_t q16_16_mul_q32_32(int32_t v_q16_16, int64_t s_q32_32) {
    // (v * s) where v is Q16.16 and s is Q32.32 -> Q16.16
    __int128 p = (__int128)v_q16_16 * (__int128)s_q32_32;
    int64_t out = (int64_t)(p >> 32);
    if (out < INT32_MIN) out = INT32_MIN;
    if (out > INT32_MAX) out = INT32_MAX;
    return (int32_t)out;
}

static inline int64_t q32_32_exp_small(int64_t x_q32_32) {
    // Deterministic small-x exp approximation in Q32.32.
    // exp(x) ≈ 1 + x + x^2/2 + x^3/6 for |x| sufficiently small.
    // All arithmetic is integer and uses truncation (no ad-hoc rounding).
    const int64_t one = (1LL << 32);
   


    __int128 x2_p = (__int128)x_q32_32 * (__int128)x_q32_32;
    const int64_t x2_q32_32 = (int64_t)(x2_p >> 32);

    // x3 = x2*x (Q32.32)
    __int128 x3_p = (__int128)x2_q32_32 * (__int128)x_q32_32;
    const int64_t x3_q32_32 = (int64_t)(x3_p >> 32);

    // term2 = x^2/2, term3 = x^3/6
    const int64_t term2 = x2_q32_32 / 2;
    const int64_t term3 = x3_q32_32 / 6;
    return one + x_q32_32 + term2 + term3;
}

static inline int64_t q32_32_from_double(double x) {
    const double s = x * 4294967296.0;
    // No ad-hoc rounding: truncation toward zero is deterministic.
    return (int64_t)s;
}

static inline int32_t q16_16_from_turns(int64_t turns_q) {
    // Map TURN_SCALE units into Q16.16 in a stable, bounded way.
    // Here, 1 TURN_SCALE maps to 1.0 in Q16.16.
    __int128 p = (__int128)turns_q * (__int128)(1 << 16);
    int64_t v = (int64_t)(p / (__int128)TURN_SCALE);
    if (v < INT32_MIN) v = INT32_MIN;
    if (v > INT32_MAX) v = INT32_MAX;
    return (int32_t)v;
}

SubstrateManager::SubstrateManager(size_t count) {
    int32_t w[9] = {32,32,32,64,384,192,64,128,96}; // sum 1024
    for (int i = 0; i < 9; ++i) weights_q10[i] = w[i];

    denom_q[0] = TURN_SCALE;
    denom_q[1] = TURN_SCALE;
    denom_q[2] = TURN_SCALE;
    denom_q[3] = TURN_SCALE;
    denom_q[4] = TURN_SCALE / 2;
    denom_q[5] = TURN_SCALE;
    denom_q[6] = TURN_SCALE;
    denom_q[7] = TURN_SCALE;
    denom_q[8] = TURN_SCALE;

    
for (size_t i = 0; i < count; ++i) anchors.emplace_back((uint32_t)i);

    // Initialize durable topology state.
    // Vector sizes track the maximum allocated id + 1.
    const uint32_t init_n = (uint32_t)((count > 1) ? count : 2);
    redirect_to.assign(init_n, 0u);
    split_child_a.assign(init_n, 0u);
    split_child_b.assign(init_n, 0u);
    next_anchor_id_u32 = init_n;


    // Allocate and initialize ancilla (Equations A.18).
    ancilla.assign(count, ancilla_particle{});
    for (size_t i = 0; i < count; ++i) {
        ancilla[i].current_mA_q32_32 = 0;
        ancilla[i].delta_I_mA_q32_32 = 0;
        ancilla[i].delta_I_prev_mA_q32_32 = 0;
        ancilla[i].phase_offset_u64 = (uint64_t)i;
        ancilla[i].convergence_metric_q32_32 = 0;
        ancilla[i].env_temp_q32_32 = 0;
        ancilla[i].env_oxygen_q32_32 = (1LL << 32);
        ancilla[i].oxidation_q32_32 = 0;
        ancilla[i].reaction_rate_q32_32 = 0;
    }

    if (count >= 2) {
        for (size_t i = 0; i < count; ++i) {
            anchors[i].neighbors.push_back((uint32_t)((i + 1) % count));
            anchors[i].neighbors.push_back((uint32_t)((i + count - 1) % count));
        }
    }
    // Bind immutable operator parameters (time dilation, etc.) from the projection seed.
    set_projection_seed(projection_seed);

    // Initialize the neural phase AI controller deterministically.
    neural_ai.init(projection_seed);

    // Initialize deterministic AI policy table.
    ai_policy.init(projection_seed);

    // Load project settings (fail closed to defaults if missing, but report).
    {
        std::string err;
        EwProjectSettings loaded;
        if (ge_project_settings_load(loaded, "Draft Container/ProjectSettings/project_settings.ewcfg", err)) {
            project_settings = loaded;
            emit_ui_line(std::string("PROJECT_SETTINGS_LOADED ") + "Draft Container/ProjectSettings/project_settings.ewcfg");
        } else {
            // Keep defaults; emit observable.
            emit_ui_line(std::string("PROJECT_SETTINGS_DEFAULTS ") + err);
        }

        // Load input bindings deterministically (fail closed but observable).
        std::string berr;
        if (!load_input_bindings_if_needed(&berr)) {
            emit_ui_line(std::string("INPUT_BINDINGS_DEFAULTS ") + berr);
        } else {
            emit_ui_line(std::string("INPUT_BINDINGS_LOADED ") + project_settings.input.bindings_path_utf8);
        }
    }

    // Ensure camera anchor exists and is initialized from project settings.
    {
        camera_anchor_id_u32 = next_anchor_id_u32;
        const uint32_t id = alloc_anchor_id();
        camera_anchor_id_u32 = id;
        anchors[id].kind_u32 = EW_ANCHOR_KIND_CAMERA;
        anchors[id].camera_state.focal_length_mm_q16_16 = project_settings.camera.default_focal_length_mm_q16_16;
        anchors[id].camera_state.aperture_f_q16_16 = project_settings.camera.default_aperture_f_q16_16;
        anchors[id].camera_state.exposure_ev_q16_16 = project_settings.camera.default_exposure_ev_q16_16;
        anchors[id].camera_state.focus_mode_u8 = (uint8_t)EwFocusMode::ManualDistance;
    }

    // ------------------------------------------------------------------
    // Global coherence bus anchor (mass-leakage -> coherence-frequency bus)
    // ------------------------------------------------------------------
    {
        const uint64_t kBusObjectId = 0x31535542484F434FULL; // 'COHOBUS1' little-endian-ish tag
        const uint32_t id = alloc_anchor_id();
        coherence_bus_anchor_id_u32 = id;
        anchors[id].kind_u32 = EW_ANCHOR_KIND_COHERENCE_BUS;
        anchors[id].object_id_u64 = kBusObjectId;
        // Deterministic router seed.
        anchors[id].coherence_bus_state.router_seed_u64 = (uint64_t)projection_seed ^ 0xC0B5BEEFULL;
        // Default per-band caps (may be tuned later via operator packets).
        for (uint32_t b = 0u; b < EW_COHERENCE_BANDS; ++b) {
            anchors[id].coherence_bus_state.max_packets_per_band_per_tick_u16[b] = 4u;
            anchors[id].coherence_bus_state.authority_cap_q15[b] = 16384u;
        }
    }

    // ------------------------------------------------------------------
    // Bootstrap spectral field anchor (substrate Fourier microprocessor)
    // ------------------------------------------------------------------
    {
        const uint64_t kSpecObjectId = 0x3154444C46504553ULL; // 'SPEFLDT1' tag
        const uint32_t id = alloc_anchor_id();
        spectral_field_anchor_id_u32 = id;
        anchors[id].kind_u32 = EW_ANCHOR_KIND_SPECTRAL_FIELD;
        anchors[id].object_id_u64 = kSpecObjectId;
        // Deterministic initial noise floor and dt scale.
        anchors[id].spectral_field_state.n_u32 = EW_SPECTRAL_N;
        anchors[id].spectral_field_state.log2n_u32 = 6u;
        anchors[id].spectral_field_state.twiddle_profile_u32 = 0u;
        anchors[id].spectral_field_state.region_center_q16_16[0] = 0;
        anchors[id].spectral_field_state.region_center_q16_16[1] = 0;
        anchors[id].spectral_field_state.region_center_q16_16[2] = 0;
        anchors[id].spectral_field_state.region_radius_m_q16_16 = (int32_t)(64 * 65536);
        anchors[id].spectral_field_state.calibration_mode_u8 = 1u;
        anchors[id].spectral_field_state.calibration_profile_u8 = 0u;
        anchors[id].spectral_field_state.calibration_ticks_remaining_u32 = 240u;
        anchors[id].spectral_field_state.noise_floor_q15 = 64;
        anchors[id].spectral_field_state.min_delta_q15 = 8;
        anchors[id].spectral_field_state.dt_scale_q32_32 = (1LL << 32);
        anchors[id].spectral_field_state.fanout_budget_u32 = 32;
    }

    // ------------------------------------------------------------------
    // ------------------------------------------------------------------
    // Bootstrap voxel coupling anchor (dense persistence + boundary coupling)
    // ------------------------------------------------------------------
    {
        const uint64_t kVoxObjectId = 0x31504C50434F5856ULL; // 'VXOCPPL1' tag
        const uint32_t id = alloc_anchor_id();
        voxel_coupling_anchor_id_u32 = id;
        anchors[id].kind_u32 = EW_ANCHOR_KIND_VOXEL_COUPLING;
        anchors[id].object_id_u64 = kVoxObjectId;
        anchors[id].voxel_coupling_state.origin_q16_16[0] = -4 * 65536;
        anchors[id].voxel_coupling_state.origin_q16_16[1] = -4 * 65536;
        anchors[id].voxel_coupling_state.origin_q16_16[2] = -4 * 65536;
        anchors[id].voxel_coupling_state.voxel_size_m_q16_16 = 1 * 65536;
        anchors[id].voxel_coupling_state.max_particles_u32 = EW_VOXEL_COUPLING_PARTICLES_MAX;
        anchors[id].voxel_coupling_state.spawn_seed_u64 = (uint64_t)projection_seed ^ 0xA11C0FFEEULL;
    }

    // ------------------------------------------------------------------
    // Collision environment anchor (solver-facing constraints inbox)
    // ------------------------------------------------------------------
    {
        const uint64_t kColEnvObjectId = 0x31564E455C4F4343ULL; // 'CCO\ENV1' tag-ish
        const uint32_t id = alloc_anchor_id();
        collision_env_anchor_id_u32 = id;
        anchors[id].kind_u32 = EW_ANCHOR_KIND_COLLISION_ENV;
        anchors[id].object_id_u64 = kColEnvObjectId;
        anchors[id].collision_env_state.clear_for_tick(0);
    }

    // AnchorPack boot: embed selected process artifacts as carrier-encoded
    // anchors for substrate-native microprocessing.
    // ------------------------------------------------------------------
    {
        const EwId9 domain_id9 = EigenWare::AnchorPack_id9_from_relpath("DOMAIN:ANCHOR_PACK");
        (void)EigenWare::AnchorPack_install(anchor_pack_records, 0u, domain_id9);
        if (!anchor_pack_records.empty()) {
            // Observable: emit one line reporting count.
            emit_ui_line(std::string("ANCHOR_PACK_INSTALLED count=") + std::to_string(anchor_pack_records.size()));
        }
    }

    // Static coherence checks (deterministic). Report into UI output.
    {
        std::string rep;
        (void)ew_coherence_analyzer::ew_analyze_operator_name_surface(rep);
        if (!rep.empty()) {
            // Emit at most one line to avoid UI spam.
            size_t nl = rep.find('\n');
            const std::string first = (nl == std::string::npos) ? rep : rep.substr(0, nl);
            if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back(first);
        }
    }

    // Clear action log deterministically.
    ai_action_log_head_u32 = 0;
    ai_action_log_count_u32 = 0;
    for (uint32_t i = 0; i < AI_ACTION_LOG_CAP; ++i) {
        ai_action_log[i] = EwAiActionEvent{};
    }

    // Clear kernel-ancilla event ring deterministically.
    kernel_event_head_u32 = 0;
    kernel_event_count_u32 = 0;
    for (uint32_t i = 0; i < KERNEL_EVENT_CAP; ++i) kernel_events[i] = EwKernelAncillaEvent{};

    // Blueprint 14.3: initialize lane substrate deterministically.
    lanes.assign(lane_policy.min_lanes, EwQubitLane{});


    // ------------------------------------------------------------------
    // Curriculum stage required checkpoint masks (deterministic checklist).
    // These masks gate crawler lane admission and stage advancement.
    // ------------------------------------------------------------------
    auto kind_bit = [] (genesis::MetricKind k) -> uint64_t {
        const uint32_t id = (uint32_t)k;
        const uint32_t b = id & 63u;
        return 1ULL << b;
    };
    for (uint32_t i = 0; i < GENESIS_CURRICULUM_STAGE_COUNT; ++i) {
        learning_stage_required_mask_u64[i] = 0ULL;
        learning_stage_completed_mask_u64[i] = 0ULL;    }
    // Stage 0: Language + Math foundations (parallel)
    learning_stage_required_mask_u64[0] =
        kind_bit(genesis::MetricKind::Lang_Dictionary_LexiconStats) |
        kind_bit(genesis::MetricKind::Lang_Thesaurus_RelationStats) |
        kind_bit(genesis::MetricKind::Lang_Encyclopedia_ConceptStats) |
        kind_bit(genesis::MetricKind::Lang_SpeechCorpus_AlignmentStats) |
        kind_bit(genesis::MetricKind::Math_Pemdas_PrecedenceStats) |
        kind_bit(genesis::MetricKind::Math_Graph_1D_ProbeStats) |
        kind_bit(genesis::MetricKind::Math_KhanAcademy_CoverageStats);

    // Stage 1: Physics & quantum physics
    learning_stage_required_mask_u64[1] =
        kind_bit(genesis::MetricKind::Qm_DoubleSlit_Fringes) |
        kind_bit(genesis::MetricKind::Qm_ParticleInBox_Levels) |
        kind_bit(genesis::MetricKind::Qm_HarmonicOsc_Spacing) |
        kind_bit(genesis::MetricKind::Qm_Tunneling_Transmission);

    // Stage 2: Orbitals/Atoms/Bonds + Chemistry foundations
    learning_stage_required_mask_u64[2] =
        kind_bit(genesis::MetricKind::Atom_Orbital_EnergyRatios) |
        kind_bit(genesis::MetricKind::Atom_Orbital_RadialNodes) |
        kind_bit(genesis::MetricKind::Bond_Length_Equilibrium) |
        kind_bit(genesis::MetricKind::Bond_Vibration_Frequency) |
        kind_bit(genesis::MetricKind::Chem_ReactionRate_Temp) |
        kind_bit(genesis::MetricKind::Chem_Equilibrium_Constant) |
        kind_bit(genesis::MetricKind::Chem_Diffusion_Coefficient);

    // Stage 3: Materials / physical science
    learning_stage_required_mask_u64[3] =
        kind_bit(genesis::MetricKind::Mat_Thermal_Conductivity) |
        kind_bit(genesis::MetricKind::Mat_Electrical_Conductivity) |
        kind_bit(genesis::MetricKind::Mat_StressStrain_Modulus) |
        kind_bit(genesis::MetricKind::Mat_PhaseChange_Threshold);

    // Stage 4: Cosmology / atmospheres
    learning_stage_required_mask_u64[4] =
        kind_bit(genesis::MetricKind::Cosmo_Orbit_Period) |
        kind_bit(genesis::MetricKind::Cosmo_Radiation_Spectrum) |
        kind_bit(genesis::MetricKind::Cosmo_Atmos_PressureProfile);

    // Stage 5: Biology (deferred)
    learning_stage_required_mask_u64[5] =
        kind_bit(genesis::MetricKind::Bio_CellDiffusion_Osmosis);
// -----------------------------------------------------------------------------

}

uint32_t SubstrateManager::alloc_anchor_id() {
    const uint32_t id = next_anchor_id_u32++;
    if (id >= anchors.size()) {
        anchors.emplace_back(id);
        ancilla.emplace_back(ancilla_particle{});
        lanes.emplace_back(EwQubitLane{});
        redirect_to.push_back(0u);
        split_child_a.push_back(0u);
        split_child_b.push_back(0u);
    }
    // default immutable object id binding
    anchors[id].id = id;
    anchors[id].object_id_u64 = (uint64_t)id;
    return id;
}

uint32_t SubstrateManager::derived_crawler_chunk_bytes_u32() const {
    // Derive a stable DMA-friendly chunk size from the configured segment budget.
    // We target a power-of-two chunk so the GPU kernels can index deterministically.
    // Clamp to a conservative range to avoid pathological allocations.
    const uint32_t seg = (crawler_max_bytes_per_segment_u32 == 0u) ? 2048u : crawler_max_bytes_per_segment_u32;
    uint64_t want = (uint64_t)seg * 32ull; // 32 segments per chunk as a default packing ratio.
    if (want < 4096ull) want = 4096ull;
    if (want > 262144ull) want = 262144ull;
    // Round to nearest lower power of two.
    uint64_t p2 = 1ull;
    while ((p2 << 1ull) <= want) p2 <<= 1ull;
    return (uint32_t)p2;
}

uint32_t SubstrateManager::derived_crawler_max_chunks_per_obs_u32() const {
    // Bound chunk fan-out so one observation cannot monopolize the per-tick pulse budget.
    const uint32_t max_pulses = (crawler_max_pulses_per_tick_u32 == 0u) ? 1u : crawler_max_pulses_per_tick_u32;
    // Allow at most 1/4 of the per-tick pulse budget to be chunk-stream pulses for one observation.
    const uint32_t cap = (max_pulses < 4u) ? 1u : (max_pulses / 4u);
    // Ensure at least 1 chunk if chunk streaming is enabled.
    return (cap == 0u) ? 1u : cap;
}

uint32_t SubstrateManager::derived_crawler_chunk_stream_backlog_threshold_u32() const {
    // Gate chunk streaming when backlog is approaching the one-window limit.
    // Use 1/8th of the derived backlog limit as the threshold to keep headroom.
    const uint32_t lim = derived_learning_backlog_limit_u32();
    const uint32_t thr = (lim < 8u) ? lim : (lim / 8u);
    return (thr == 0u) ? 1u : thr;
}

uint32_t SubstrateManager::derived_pulse_fanout_max_u32(const EwCtx& ctx) const {
    // Derive a max fan-out from available gradient headroom and the governor target.
    // This avoids hardcoded fan-out ceilings.
    const uint64_t unit = (uint64_t)((ctx.phase_orbital_displacement_unit_mA_q32_32 == 0) ? 1 : (uint64_t)ctx.phase_orbital_displacement_unit_mA_q32_32);
    const uint64_t head = (uint64_t)((ctx.gradient_headroom_mA_q32_32 == 0) ? 1 : (uint64_t)ctx.gradient_headroom_mA_q32_32);
    uint64_t raw = head / unit;
    if (raw < 1ull) raw = 1ull;
    if (raw > 256ull) raw = 256ull;
    // Governor target fraction reduces effective fan-out.
    const uint64_t gt = (uint64_t)ctx.governor.target_frac_q15;
    raw = (raw * gt) / 32768ull;
    if (raw < 1ull) raw = 1ull;
    return (uint32_t)raw;
}

uint32_t SubstrateManager::derived_learning_probe_micro_ticks_u32(const EwCtx& ctx) const {
    // Derive micro-ticks from temporal envelope and governor, bounded conservatively.
    uint64_t env = (ctx.temporal_envelope_ticks_u64 == 0) ? 1ull : (uint64_t)ctx.temporal_envelope_ticks_u64;
    uint64_t raw = env / 32ull;
    if (raw < 1ull) raw = 1ull;
    if (raw > 1024ull) raw = 1024ull;
    const uint64_t gt = (uint64_t)ctx.governor.target_frac_q15;
    raw = (raw * gt) / 32768ull;
    if (raw < 1ull) raw = 1ull;
    return (uint32_t)raw;
}

uint32_t SubstrateManager::derived_learning_tries_per_step_u32(const EwCtx& ctx) const {
    // One step represents many parallel tries via fan-out. Tie this to derived fan-out.
    const uint32_t fo = derived_pulse_fanout_max_u32(ctx);
    // Base tries per step is 2^16 scaled by fan-out, capped.
    uint64_t t = 65536ull * (uint64_t)fo;
    if (t > 0xFFFFFFFFull) t = 0xFFFFFFFFull;
    return (uint32_t)t;
}

uint32_t SubstrateManager::derived_learning_max_steps_per_tick_u32(const EwCtx& ctx) const {
    // Bound steps per tick so we respect the governor and avoid host stalls.
    // Scale with current budget proxy (v*i) via the ctx current max.
    const uint64_t cur = (uint64_t)((ctx.pulse_current_max_mA_q32_32 == 0) ? 1 : (uint64_t)ctx.pulse_current_max_mA_q32_32);
    // Map current budget to a 1..8192 range.
    uint64_t s = cur / (1ull << 24);
    if (s < 1ull) s = 1ull;
    if (s > 8192ull) s = 8192ull;
    const uint64_t gt = (uint64_t)ctx.governor.target_frac_q15;
    s = (s * gt) / 32768ull;
    if (s < 1ull) s = 1ull;
    return (uint32_t)s;
}

void SubstrateManager::ensure_lattice_gpu_() {
    if (!gpu_lattice_authoritative) return;
    if (lattice_gpu_) return;
    // Deterministic default lattice dimensions for the authoritative substrate.
    // Photon-confinement simulation now defaults to a full 256^3 3D lattice.
    const uint32_t gx = 256, gy = 256, gz = 256;
    lattice_gpu_.reset(new EwFieldLatticeGpu(gx, gy, gz));
    lattice_gpu_->init(projection_seed);
    // Default density is empty space (no black hole exclusion).
    std::vector<uint8_t> dens((size_t)gx * (size_t)gy * (size_t)gz, 0u);
    lattice_gpu_->upload_density_mask_u8(dens.data(), dens.size());
}

void SubstrateManager::apply_object_imprint_writeback_() {
    // Bounded object→world writeback: accumulate the object-local phase/occupancy
    // imprint for objects advanced this tick, then apply it once per cell.
    // This method is called from tick() only when the authoritative lattice exists.
    if (!lattice_gpu_) return;
    if (object_updates_last_tick_u64.empty()) return;

    lattice_gpu_->clear_object_imprint();

    auto q32_32_to_i32_round = [](uint64_t q)->int32_t {
        const int64_t s = (int64_t)q;
        const int64_t r = (s >= 0) ? (s + (1ll << 31)) : (s - (1ll << 31));
        return (int32_t)(r >> 32);
    };

    const uint32_t gxw = lattice_gpu_->grid_x();
    const uint32_t gyw = lattice_gpu_->grid_y();
    const uint32_t gzw = lattice_gpu_->grid_z();

    for (size_t i = 0; i < object_updates_last_tick_u64.size(); ++i) {
        const uint64_t oid = object_updates_last_tick_u64[i];
        const EwObjectEntry* e = object_store.find(oid);
        if (!e) continue;

        EwVoxelVolumeView vv;
        if (!object_store.view_voxel_volume(oid, vv)) continue;
        if (vv.format_u32 != 1u || !vv.bytes || vv.byte_count == 0) continue;

        uint32_t lgx=0,lgy=0,lgz=0,lfmt=0;
        const int16_t* phi = nullptr;
        size_t phi_bytes = 0;
        if (!object_store.view_local_phi(oid, lgx, lgy, lgz, lfmt, phi, phi_bytes) ||
            lfmt != 1u || !phi ||
            lgx != vv.grid_x_u32 || lgy != vv.grid_y_u32 || lgz != vv.grid_z_u32 ||
            phi_bytes != vv.byte_count * 2u) {
            continue;
        }

        int32_t cx = q32_32_to_i32_round(e->geomcoord9_u64x9.u64x9[0]) + (int32_t)(gxw / 2u);
        int32_t cy = q32_32_to_i32_round(e->geomcoord9_u64x9.u64x9[1]) + (int32_t)(gyw / 2u);
        int32_t cz = q32_32_to_i32_round(e->geomcoord9_u64x9.u64x9[2]) + (int32_t)(gzw / 2u);
        if (cx < 0) cx = 0; if (cx >= (int32_t)gxw) cx = (int32_t)gxw - 1;
        if (cy < 0) cy = 0; if (cy >= (int32_t)gyw) cy = (int32_t)gyw - 1;
        if (cz < 0) cz = 0; if (cz >= (int32_t)gzw) cz = (int32_t)gzw - 1;

        // Deterministic bounded imprint accumulation. Scale factors are kept
        // conservative in this branch (no additional tuning constants).
        (void)lattice_gpu_->accumulate_object_imprint5_q15(vv.bytes, phi,
                                                           vv.grid_x_u32, vv.grid_y_u32, vv.grid_z_u32,
                                                           (uint32_t)cx, (uint32_t)cy, (uint32_t)cz,
                                                           0.0f, 0.0f,
                                                           0.0f, 0.0f, 0.0f);
    }

    lattice_gpu_->apply_object_imprint_to_fields();
}

void SubstrateManager::ensure_lattice_probe_gpu_() {
    if (!gpu_lattice_authoritative) return;
    // Default sandbox 0.
    const uint32_t sandbox_id = 0u;
    if (sandbox_id >= EW_LEARNING_SANDBOX_MAX) return;
    if (lattice_probe_gpu_[sandbox_id]) return;
    // Deterministic learning sandbox lattice dimensions.
    // This lattice is used for parameter molding/evolution during metric fitting
    // and never perturbs the world lattice.
    const uint32_t gx = 32, gy = 32, gz = 32;
    lattice_probe_gpu_[sandbox_id].reset(new EwFieldLatticeGpu(gx, gy, gz));
    lattice_probe_gpu_[sandbox_id]->init(projection_seed ^ 0xC0FFEEULL);
    std::vector<uint8_t> dens((size_t)gx * (size_t)gy * (size_t)gz, 0u);
    lattice_probe_gpu_[sandbox_id]->upload_density_mask_u8(dens.data(), dens.size());

    // Bind probe lattice view for learning metric extraction.
    (void)genesis::ew_learning_bind_probe_lattice_cuda(
        lattice_probe_gpu_[sandbox_id]->device_E_curr_f32(),
        lattice_probe_gpu_[sandbox_id]->device_flux_f32(),
        lattice_probe_gpu_[sandbox_id]->device_coherence_f32(),
        lattice_probe_gpu_[sandbox_id]->device_curvature_f32(),
        lattice_probe_gpu_[sandbox_id]->device_doppler_f32(),
        (int)gx, (int)gy, (int)gz
    );
}

void SubstrateManager::lattice_get_radiance_slice_bgra8(uint32_t slice_z, std::vector<uint8_t>& out_bgra8, EwFieldFrameHeader& out_hdr) {
    out_bgra8.clear();
    std::memset(&out_hdr, 0, sizeof(out_hdr));
    if (!gpu_lattice_authoritative) return;
    ensure_lattice_gpu_();
    if (!lattice_gpu_) return;
    lattice_gpu_->get_radiance_slice_bgra8(slice_z, out_bgra8, out_hdr);
}

EwFieldLatticeGpu* SubstrateManager::world_lattice_gpu_for_learning() {
    if (!gpu_lattice_authoritative) return nullptr;
    ensure_lattice_gpu_();
    return lattice_gpu_.get();
}

EwFieldLatticeGpu* SubstrateManager::probe_lattice_gpu_for_learning(uint32_t sandbox_id_u32) {
    if (!gpu_lattice_authoritative) return nullptr;
    ensure_lattice_gpu_();
    if (sandbox_id_u32 >= EW_LEARNING_SANDBOX_MAX) sandbox_id_u32 = 0u;
    // Ensure the requested sandbox exists.
    if (!lattice_probe_gpu_[sandbox_id_u32]) {
        // For now, initialize only sandbox 0 deterministically; other ids alias to 0.
        sandbox_id_u32 = 0u;
        ensure_lattice_probe_gpu_();
    }
    return lattice_probe_gpu_[sandbox_id_u32].get();
}

void SubstrateManager::ai_log_event(const EwAiActionEvent& e) {
    const uint32_t idx = (ai_action_log_head_u32) % AI_ACTION_LOG_CAP;
    ai_action_log[idx] = e;
    ai_action_log_head_u32 = (ai_action_log_head_u32 + 1u) % AI_ACTION_LOG_CAP;
    if (ai_action_log_count_u32 < AI_ACTION_LOG_CAP) {
        ai_action_log_count_u32 += 1u;
    }
}

uint32_t SubstrateManager::ai_get_action_log(EwAiActionEvent* out_events, uint32_t max_events) const {
    if (!out_events || max_events == 0) return 0;
    const uint32_t n = (ai_action_log_count_u32 < max_events) ? ai_action_log_count_u32 : max_events;
    // Oldest entry is head - count.
    uint32_t start = 0;
    if (ai_action_log_count_u32 == AI_ACTION_LOG_CAP) {
        start = ai_action_log_head_u32;
    } else {
        start = 0;
    }
    // If not full, entries are in [0..count-1].
    if (ai_action_log_count_u32 < AI_ACTION_LOG_CAP) {
        for (uint32_t i = 0; i < n; ++i) out_events[i] = ai_action_log[i];
        return n;
    }
    // Full ring: oldest at start.
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t idx = (start + i) % AI_ACTION_LOG_CAP;
        out_events[i] = ai_action_log[idx];
    }
    return n;
}



void SubstrateManager::set_projection_seed(uint64_t seed) {
    // Allowed only before the first tick. After tick 0, anchors are frozen.
    if (canonical_tick != 0) return;

    projection_seed = seed ? seed : 1;

    // Keep neural AI viewport-derived identically to the rest of the immutable operator map.
    neural_ai.init(projection_seed);

    // Keep policy viewport-derived identically as well.
    ai_policy.init(projection_seed);

    // Time-dilation operator parameters are part of the immutable operator map.
    // For this prototype, keep time dilation neutral so the substrate encoder
    // and spider-compression path can be validated first.
    const int64_t one_q32_32 = (1LL << 32);
    td_params.td_min_q32_32 = one_q32_32;
    td_params.td_max_q32_32 = one_q32_32;
    td_params.k_coh_q32_32  = 0;
    td_params.k_curv_q32_32 = 0;
    td_params.k_dop_q32_32  = 0;
    td_params.chi_ref_turns_q = TURN_SCALE;
    td_params.norm_turns_q = TURN_SCALE;
}

void SubstrateManager::set_projection_viewport_basis(uint64_t basis_u64) {
    set_projection_seed(basis_u64);
}

void SubstrateManager::configure_cosmic_expansion(int64_t h0_q32_32, int64_t dt_seconds_q32_32) {
    // Store baseline references only. Effective constants are derived per-tick
    // inside the substrate microprocessor from relative factors.
    hubble_h0_q32_32 = (h0_q32_32 != 0) ? h0_q32_32 : hubble_h0_ref_default_q32_32();
    tick_dt_seconds_q32_32 = dt_seconds_q32_32;
    boundary_scale_step_q32_32 = (1LL << 32);
    boundary_scale_q32_32 = (1LL << 32);
}

void SubstrateManager::submit_pulse(const Pulse& p) {
    inbound.push_back(p);
}

void SubstrateManager::submit_envelope_sample(const EwEnvelopeSample& s) {
    envelope_sample = s;
}


void SubstrateManager::submit_gpu_pulse_sample(uint64_t freq_hz_u64, uint64_t freq_ref_hz_u64,
                                uint32_t amp_u32, uint32_t amp_ref_u32) {
    // Backward-compatible entry point: voltage channel defaults to zero.
    submit_gpu_pulse_sample_v2(freq_hz_u64, freq_ref_hz_u64, amp_u32, amp_ref_u32, 0u, 1u);
}

void SubstrateManager::submit_gpu_pulse_sample_v2(uint64_t freq_hz_u64, uint64_t freq_ref_hz_u64,
                                uint32_t amp_u32, uint32_t amp_ref_u32,
                                uint32_t volt_u32, uint32_t volt_ref_u32) {
    // Store latest raw readings. All derived scaling and factors are computed inside the
    // substrate microprocessor during tick() (Eq 3.0.2/3.0.3 and envelope coupling).
    gpu_pulse_freq_hz_u64 = freq_hz_u64;
    gpu_pulse_freq_ref_hz_u64 = (freq_ref_hz_u64 != 0u) ? freq_ref_hz_u64 : 1u;

    gpu_pulse_amp_u32 = amp_u32;
    gpu_pulse_amp_ref_u32 = (amp_ref_u32 != 0u) ? amp_ref_u32 : 1u;

    gpu_pulse_volt_u32 = volt_u32;
    gpu_pulse_volt_ref_u32 = (volt_ref_u32 != 0u) ? volt_ref_u32 : 1u;

    // Record the sample into the kernel-ancilla ring as a boundary event.
    // Events carry only raw pulse scalars; acceptance/commit happens during tick().
    const uint32_t idx = kernel_event_head_u32 % KERNEL_EVENT_CAP;
    kernel_events[idx].tick_u64 = canonical_tick;
    kernel_events[idx].anchor_id_u32 = 0u;
    kernel_events[idx].lane_u32 = 0u;
    kernel_events[idx].freq_hz_u64 = gpu_pulse_freq_hz_u64;
    kernel_events[idx].freq_ref_hz_u64 = gpu_pulse_freq_ref_hz_u64;
    kernel_events[idx].amp_u32 = gpu_pulse_amp_u32;
    kernel_events[idx].amp_ref_u32 = gpu_pulse_amp_ref_u32;
    kernel_events[idx].volt_u32 = gpu_pulse_volt_u32;
    kernel_events[idx].volt_ref_u32 = gpu_pulse_volt_ref_u32;
    kernel_event_head_u32 = (kernel_event_head_u32 + 1u) % KERNEL_EVENT_CAP;
    if (kernel_event_count_u32 < KERNEL_EVENT_CAP) kernel_event_count_u32 += 1u;
 }



void SubstrateManager::submit_ai_commands_fixed(const EwAiCommand* cmds, uint32_t count_u32) {
    // Fixed-array schema (BF.3A). Commands are copied into substrate state so
    // decompression and effects occur inside the microprocessor.
    ai_commands_count_u32 = 0;
    ai_pulse_q63 = 0;
    ai_total_weight_q63 = 0;
    if (cmds == nullptr) return;

    const uint32_t n = (count_u32 > EW_AI_COMMAND_MAX) ? EW_AI_COMMAND_MAX : count_u32;
    for (uint32_t i = 0; i < n; ++i) {
        EwAiCommand c = cmds[i];
        if (c.opcode_u16 == EW_AI_OP_NOOP) continue;
        if (c.weight_q63 <= 0) continue;
        if (ai_commands_count_u32 < EW_AI_COMMAND_MAX) {
            ai_commands_fixed[ai_commands_count_u32++] = c;
            ai_total_weight_q63 += c.weight_q63;
            if (ai_total_weight_q63 < 0) ai_total_weight_q63 = INT64_MAX;
        }
    }

    // Pulse compression carrier (BF.5): aggregate amplitude is sum(weight).
    ai_pulse_q63 = ai_total_weight_q63;
    if (ai_pulse_q63 < 0) ai_pulse_q63 = 0;
    if (ai_pulse_q63 > INT64_MAX) ai_pulse_q63 = INT64_MAX;
}

bool SubstrateManager::control_packet_push(const EwControlPacket& p) {
    if (control_inbox_count_u32 >= CONTROL_INBOX_CAP) return false;
    const uint32_t idx = (control_inbox_head_u32 + control_inbox_count_u32) % CONTROL_INBOX_CAP;
    control_inbox[idx] = p;
    control_inbox_count_u32++;
    return true;
}

bool SubstrateManager::control_packet_pop(EwControlPacket& out) {
    if (control_inbox_count_u32 == 0) return false;
    out = control_inbox[control_inbox_head_u32];
    control_inbox_head_u32 = (control_inbox_head_u32 + 1) % CONTROL_INBOX_CAP;
    control_inbox_count_u32--;
    return true;
}


bool SubstrateManager::load_input_bindings_if_needed(std::string* out_err) {
    if (input_bindings_loaded) return true;

    // Deterministic load of bindings file. Format (one per line):
    //  action <raw_id_u32> <mapped_u32> <scale_q16_16>
    //  axis   <raw_id_u32> <mapped_u32> <scale_q16_16>
    // Lines may have comments starting with '#'. Whitespace-delimited.
    const std::string path = project_settings.input.bindings_path_utf8;
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    if (!f.good()) {
        if (out_err) *out_err = std::string("input_bindings_open_failed ") + path;
        return false;
    }

    input_action_bindings.clear();
    input_axis_bindings.clear();

    std::string line;
    uint32_t line_no = 0;
    while (std::getline(f, line)) {
        line_no++;
        // Strip comment.
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        // Trim spaces.
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n' || line.back()==' ' || line.back()=='\t')) line.pop_back();
        size_t st = 0;
        while (st < line.size() && (line[st]==' ' || line[st]=='\t')) st++;
        if (st >= line.size()) continue;
        line = line.substr(st);

        std::istringstream iss(line);
        std::string kind;
        uint32_t raw = 0, mapped = 0;
        int32_t scale = (int32_t)(1 * 65536);
        if (!(iss >> kind >> raw >> mapped >> scale)) {
            // Fail closed deterministically: ignore malformed lines but report.
            continue;
        }
        EwInputBinding b;
        b.raw_id_u32 = raw;
        b.mapped_u32 = mapped;
        b.scale_q16_16 = scale;
        if (kind == "action") input_action_bindings.push_back(b);
        else if (kind == "axis") input_axis_bindings.push_back(b);
    }

    // Stable ordering: sort by raw_id then mapped.
    auto cmp = [](const EwInputBinding& a, const EwInputBinding& b) {
        if (a.raw_id_u32 != b.raw_id_u32) return a.raw_id_u32 < b.raw_id_u32;
        return a.mapped_u32 < b.mapped_u32;
    };
    std::sort(input_action_bindings.begin(), input_action_bindings.end(), cmp);
    std::sort(input_axis_bindings.begin(), input_axis_bindings.end(), cmp);

    input_bindings_loaded = true;
    if (out_err) *out_err = std::string();
    return true;
}

bool SubstrateManager::save_input_bindings_if_dirty(std::string* out_err) {
    if (!input_bindings_dirty) return true;
    const std::string path = project_settings.input.bindings_path_utf8;
    std::ofstream f(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.good()) {
        if (out_err) *out_err = std::string("input_bindings_write_failed ") + path;
        return false;
    }

    // Deterministic header.
    const char* hdr = "# Genesis Engine input bindings (deterministic)\n"
                      "# Format: action|axis <raw_id_u32> <mapped_u32> <scale_q16_16>\n";
    f.write(hdr, (std::streamsize)std::strlen(hdr));

    auto write_vec = [&](const char* kind, const std::vector<EwInputBinding>& v) {
        for (const auto& b : v) {
            char line[128];
            // snprintf is deterministic for integers.
            const int n = std::snprintf(line, sizeof(line), "%s %u %u %d\n",
                                        kind,
                                        (unsigned)b.raw_id_u32,
                                        (unsigned)b.mapped_u32,
                                        (int)b.scale_q16_16);
            if (n > 0) f.write(line, (std::streamsize)n);
        }
    };

    // Ensure stable ordering prior to write.
    auto cmp = [](const EwInputBinding& a, const EwInputBinding& b) {
        if (a.raw_id_u32 != b.raw_id_u32) return a.raw_id_u32 < b.raw_id_u32;
        return a.mapped_u32 < b.mapped_u32;
    };
    std::sort(input_action_bindings.begin(), input_action_bindings.end(), cmp);
    std::sort(input_axis_bindings.begin(), input_axis_bindings.end(), cmp);

    write_vec("action", input_action_bindings);
    write_vec("axis", input_axis_bindings);

    input_bindings_dirty = false;
    if (out_err) *out_err = std::string();
    return true;
}



void SubstrateManager::submit_operator_packet_v1(const uint8_t* bytes, size_t bytes_len) {
    if (bytes == nullptr) return;
    if (bytes_len != EW_ANCHOR_OP_PACKED_V1_BYTES) return;
    EwAnchorOpPackedV1Bytes pkt{};
    for (size_t i = 0; i < EW_ANCHOR_OP_PACKED_V1_BYTES; ++i) pkt.bytes[i] = bytes[i];
    operator_packets_v1.push_back(pkt);
}


void SubstrateManager::enqueue_inbound_pulse(const Pulse& p) {
    // Canonical inbound admission for simulated subsystems.
    inbound.push_back(p);
}

const GE_CorpusAllowlist* SubstrateManager::corpus_allowlist_ptr() const {
    return corpus_allowlist_loaded ? &corpus_allowlist : nullptr;
}

void SubstrateManager::crawler_enqueue_observation_utf8(
    uint64_t artifact_id_u64,
    uint32_t stream_id_u32,
    uint32_t extractor_id_u32,
    uint32_t trust_class_u32,
    uint32_t causal_tag_u32,
    const std::string& domain_ascii,
    const std::string& url_ascii,
    const std::string& utf8
) {
    crawler.enqueue_observation_utf8(
        artifact_id_u64,
        0u,
        0u,
        0u,
        stream_id_u32,
        extractor_id_u32,
        trust_class_u32,
        causal_tag_u32,
        domain_ascii,
        url_ascii,
        utf8
    );

    // External API request emission is a substrate-level actuation. The adapter
    // executes requests and returns responses via submit_external_api_response().
    // Causal tag identifies request lines.
    const uint32_t EW_CAUSAL_TAG_EXTERNAL_API_REQ = 0x41504931U; // 'API1'
    if (causal_tag_u32 == EW_CAUSAL_TAG_EXTERNAL_API_REQ) {
        // Deterministic whitespace split.
        size_t a = 0;
        while (a < utf8.size() && (utf8[a] == ' ' || utf8[a] == '\t' || utf8[a] == '\n' || utf8[a] == '\r')) a++;
        size_t b = a;
        while (b < utf8.size() && utf8[b] != ' ' && utf8[b] != '\t' && utf8[b] != '\n' && utf8[b] != '\r') b++;
        size_t c = b;
        while (c < utf8.size() && (utf8[c] == ' ' || utf8[c] == '\t')) c++;
        size_t d = c;
        while (d < utf8.size() && utf8[d] != ' ' && utf8[d] != '\t' && utf8[d] != '\n' && utf8[d] != '\r') d++;
        std::string method = (b > a) ? utf8.substr(a, b - a) : std::string();
        std::string url = (d > c) ? utf8.substr(c, d - c) : std::string();
        std::string headers;
        size_t e = d;
        while (e < utf8.size() && (utf8[e] == ' ' || utf8[e] == '\t')) e++;
        if (e < utf8.size()) headers = utf8.substr(e);

        if (!method.empty() && !url.empty()) {
            EwExternalApiRequest req;
            req.tick_u64 = canonical_tick;
            req.request_id_u64 = (canonical_tick << 32) ^ (external_api_request_seq_u64++);
            req.method_utf8 = method;
            req.url_utf8 = url;
            req.headers_kv_csv = headers;
            // External API response buffer cap is bounded separately from crawler segmentation.
            // This enables larger responses (search pages, PDFs) while keeping strict ceilings.
            uint32_t cap = external_api_default_response_cap_u32;
            if (cap < 4096u) cap = 4096u;
            if (cap > ingest_max_doc_bytes_u32) cap = ingest_max_doc_bytes_u32;
            req.response_cap_u32 = cap;
            external_api_pending.push_back(req);
        }
    }
}

bool SubstrateManager::pop_external_api_request(EwExternalApiRequest& out_req) {
    if (external_api_pending.empty()) return false;
    out_req = external_api_pending.front();
    external_api_pending.pop_front();

    // Record inflight metadata so the substrate can deterministically process responses
    // (download detection, sitemap parsing, license gating) without adapter-side parsing.
    EwExternalApiInflight in{};
    in.request_id_u64 = out_req.request_id_u64;
    in.tick_u64 = out_req.tick_u64;
    in.url_utf8 = out_req.url_utf8;
    in.context_anchor_id_u32 = out_req.context_anchor_id_u32;
    in.crawler_anchor_id_u32 = out_req.crawler_anchor_id_u32;
    in.domain_anchor_id_u32 = out_req.domain_anchor_id_u32;

    // session/stage/profile are encoded in request_id low bits for crawl-scheduled requests.
    // For non-crawl requests (no marker), keep zero.
    const uint32_t rid_lo = (uint32_t)(out_req.request_id_u64 & 0xFFFFFFFFu);
    if ((rid_lo & 0x80000000u) != 0u) {
        in.session_idx_u32 = (uint32_t)((rid_lo >> 20) & 0x3u);
        in.stage_u32 = (uint32_t)((rid_lo >> 16) & 0xFu);
        in.profile_u32 = (uint32_t)((rid_lo >> 12) & 0xFu);
    } else {
        in.session_idx_u32 = 0u;
        in.stage_u32 = 0u;
        in.profile_u32 = 0u;
    }

    // Parse host/path from URL (https://host/path).
    auto parse_host_path = [&](const std::string& url, std::string& host, std::string& path) {
        host.clear(); path.clear();
        const std::string pre = "https://";
        size_t i = 0;
        if (url.size() >= pre.size() && url.substr(0, pre.size()) == pre) i = pre.size();
        size_t slash = url.find('/', i);
        if (slash == std::string::npos) {
            host = url.substr(i);
            path = "/";
        } else {
            host = url.substr(i, slash - i);
            path = url.substr(slash);
            if (path.empty()) path = "/";
        }
    };
    parse_host_path(in.url_utf8, in.host_utf8, in.path_utf8);

    external_api_inflight.push_back(in);
    return true;
}


static inline char ew_utf8_lower_ascii_only(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static std::string ew_utf8_lower_ascii_only_str(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) out.push_back(ew_utf8_lower_ascii_only(s[i]));
    return out;
}

static bool ew_has_download_ext_utf8(const std::string& path_utf8) {
    const std::string p = ew_utf8_lower_ascii_only_str(path_utf8);
    const char* exts[] = {".pdf", ".zip", ".json", ".xml"};
    for (size_t i = 0; i < sizeof(exts)/sizeof(exts[0]); ++i) {
        const std::string e(exts[i]);
        if (p.size() >= e.size() && p.substr(p.size() - e.size()) == e) return true;
    }
    return false;
}

// Back-compat name used by several call sites.
// All paths in this runtime are already treated as UTF-8.
static bool ew_has_download_ext(const std::string& path_utf8) {
    return ew_has_download_ext_utf8(path_utf8);
}


enum EwDocKindU32 {
    EW_DOC_KIND_UNKNOWN = 0u,
    EW_DOC_KIND_PDF = 1u,
    EW_DOC_KIND_ZIP = 2u,
    EW_DOC_KIND_JSON = 3u,
    EW_DOC_KIND_XML = 4u,
    EW_DOC_KIND_HTML = 5u,
    EW_DOC_KIND_TEXT = 6u
};

static uint32_t ew_doc_kind_from_bytes(const uint8_t* b, size_t n) {
    if (b == nullptr || n == 0u) return EW_DOC_KIND_UNKNOWN;
    // PDF magic: %PDF-
    if (n >= 5u && b[0] == 0x25 && b[1] == 0x50 && b[2] == 0x44 && b[3] == 0x46 && b[4] == 0x2D) return EW_DOC_KIND_PDF;
    // ZIP local file header: PK\x03\x04
    if (n >= 4u && b[0] == 0x50 && b[1] == 0x4B && b[2] == 0x03 && b[3] == 0x04) return EW_DOC_KIND_ZIP;

    // Skip BOM/whitespace for text-like sniff
    size_t i = 0u;
    if (n >= 3u && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) i = 3u;
    while (i < n) {
        const uint8_t ch = b[i];
        if (ch == 0x20 || ch == 0x09 || ch == 0x0A || ch == 0x0D) { ++i; continue; }
        break;
    }
    if (i >= n) return EW_DOC_KIND_TEXT;

    // JSON starts with { or [
    if (b[i] == '{' || b[i] == '[') return EW_DOC_KIND_JSON;

    // XML/HTML starts with <
    if (b[i] == '<') {
        // HTML heuristic: <html or <!doctype
        const size_t rem = n - i;
        auto lower = [&](size_t off) -> char {
            if (i + off >= n) return '\0';
            const uint8_t c = b[i + off];
            if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
            return (char)c;
        };
        if (rem >= 5u && lower(1) == 'h' && lower(2) == 't' && lower(3) == 'm' && lower(4) == 'l') return EW_DOC_KIND_HTML;
        if (rem >= 9u && lower(1) == '!' && lower(2) == 'd') return EW_DOC_KIND_HTML;
        return EW_DOC_KIND_XML;
    }

    return EW_DOC_KIND_TEXT;
}

static void ew_extract_utf8_view(const uint8_t* b, size_t n, std::string& out, size_t out_cap) {
    // Produce a bounded UTF-8 view of arbitrary bytes for deterministic parsing.
    // Strategy: keep well-formed UTF-8 sequences; map invalid bytes to spaces.
    // Also deny C0 controls (except '\n', '\r', '\t') and DEL.
    out.clear();
    out.reserve((out_cap < n) ? out_cap : n);

    auto allowed_ctrl = [](unsigned char x) {
        return x == (unsigned char)'\n' || x == (unsigned char)'\r' || x == (unsigned char)'\t';
    };

    size_t i = 0;
    while (i < n && out.size() < out_cap) {
        unsigned char b0 = (unsigned char)b[i];

        if (b0 < 0x80) {
            if ((b0 < 0x20 && !allowed_ctrl(b0)) || b0 == 0x7F) {
                // Map disallowed controls to a single space.
                out.push_back(' ');
            } else {
                out.push_back((char)b0);
            }
            i += 1;
            continue;
        }

        // 2-byte
        if ((b0 & 0xE0) == 0xC0) {
            if (i + 1 >= n) { out.push_back(' '); break; }
            unsigned char b1 = (unsigned char)b[i + 1];
            if ((b1 & 0xC0) != 0x80 || b0 < 0xC2) { out.push_back(' '); i += 1; continue; }
            if (out.size() + 2 > out_cap) break;
            out.push_back((char)b0); out.push_back((char)b1);
            i += 2;
            continue;
        }

        // 3-byte
        if ((b0 & 0xF0) == 0xE0) {
            if (i + 2 >= n) { out.push_back(' '); break; }
            unsigned char b1 = (unsigned char)b[i + 1];
            unsigned char b2 = (unsigned char)b[i + 2];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { out.push_back(' '); i += 1; continue; }
            if (b0 == 0xE0 && b1 < 0xA0) { out.push_back(' '); i += 1; continue; }
            if (b0 == 0xED && b1 >= 0xA0) { out.push_back(' '); i += 1; continue; }
            if (out.size() + 3 > out_cap) break;
            out.push_back((char)b0); out.push_back((char)b1); out.push_back((char)b2);
            i += 3;
            continue;
        }

        // 4-byte
        if ((b0 & 0xF8) == 0xF0) {
            if (i + 3 >= n) { out.push_back(' '); break; }
            unsigned char b1 = (unsigned char)b[i + 1];
            unsigned char b2 = (unsigned char)b[i + 2];
            unsigned char b3 = (unsigned char)b[i + 3];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) { out.push_back(' '); i += 1; continue; }
            if (b0 == 0xF0 && b1 < 0x90) { out.push_back(' '); i += 1; continue; }
            if (b0 == 0xF4 && b1 > 0x8F) { out.push_back(' '); i += 1; continue; }
            if (b0 > 0xF4) { out.push_back(' '); i += 1; continue; }
            if (out.size() + 4 > out_cap) break;
            out.push_back((char)b0); out.push_back((char)b1); out.push_back((char)b2); out.push_back((char)b3);
            i += 4;
            continue;
        }

        // Invalid leading byte.
        out.push_back(' ');
        i += 1;
    }
}


static void ew_zip_manifest_from_bytes(const uint8_t* b, size_t n, std::string& out, size_t out_cap) {
    out.clear();
    if (b == nullptr || n < 4u || out_cap == 0u) return;
    out.reserve((out_cap < 4096u) ? out_cap : 4096u);

    // Scan for central directory headers PK\x01\x02
    const uint32_t sig = 0x02014B50u;
    auto rd_u16 = [&](size_t off) -> uint16_t {
        if (off + 2u > n) return 0;
        return (uint16_t)(b[off] | ((uint16_t)b[off + 1u] << 8));
    };
    auto rd_u32 = [&](size_t off) -> uint32_t {
        if (off + 4u > n) return 0;
        return (uint32_t)(b[off] | ((uint32_t)b[off + 1u] << 8) | ((uint32_t)b[off + 2u] << 16) | ((uint32_t)b[off + 3u] << 24));
    };

    size_t i = 0u;
    uint32_t seen = 0u;
    while (i + 46u <= n && out.size() < out_cap) {
        const uint32_t v = rd_u32(i);
        if (v != sig) { ++i; continue; }

        const uint16_t name_len = rd_u16(i + 28u);
        const uint16_t extra_len = rd_u16(i + 30u);
        const uint16_t comment_len = rd_u16(i + 32u);
        const size_t name_off = i + 46u;
        if (name_off + (size_t)name_len > n) { ++i; continue; }

        // Append filename (ASCII safe filter)
        if (seen < 256u) {
            if (out.size() + (size_t)name_len + 2u <= out_cap) {
                for (size_t k = 0; k < (size_t)name_len; ++k) {
                    const uint8_t ch = b[name_off + k];
                    if (ch >= 32u && ch <= 126u) out.push_back((char)ch);
                    else out.push_back('_');
                }
                out.push_back('\n');
                ++seen;
            }
        }

        i = name_off + (size_t)name_len + (size_t)extra_len + (size_t)comment_len;
    }
    if (out.empty()) out = "ZIP_MANIFEST_EMPTY\n";
}

static void ew_json_key_summary_from_bytes(const uint8_t* b, size_t n, std::string& out, size_t out_cap) {
    out.clear();
    if (b == nullptr || n == 0u || out_cap == 0u) return;
    out.reserve((out_cap < 4096u) ? out_cap : 4096u);

    // Simple deterministic key scanner: capture "key" : at depth <= 2.
    size_t i = 0u;
    // skip BOM/whitespace
    if (n >= 3u && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) i = 3u;
    while (i < n && (b[i] == 0x20 || b[i] == 0x09 || b[i] == 0x0A || b[i] == 0x0D)) ++i;

    uint32_t depth = 0u;
    bool in_str = false;
    bool esc = false;
    std::string cur;
    cur.reserve(128);
    uint32_t keys = 0u;

    auto push_key = [&](const std::string& k) {
        if (k.empty()) return;
        if (out.size() + k.size() + 2u > out_cap) return;
        out += k;
        out.push_back('\n');
        ++keys;
    };

    while (i < n && out.size() < out_cap && keys < 256u) {
        const uint8_t ch = b[i++];

        if (in_str) {
            if (esc) { esc = false; continue; }
            if (ch == '\\') { esc = true; continue; }
            if (ch == '"') { in_str = false; continue; }
            if (cur.size() < 128u && ch >= 32u && ch <= 126u) cur.push_back((char)ch);
            continue;
        }

        if (ch == '"') {
            in_str = true;
            cur.clear();
            continue;
        }

        if (ch == '{' || ch == '[') { ++depth; continue; }
        if (ch == '}' || ch == ']') { if (depth > 0u) --depth; continue; }

        if (ch == ':' && depth <= 2u) {
            // The last completed string (cur) is the key if we just exited a string.
            // We approximated by collecting cur only while in_str, so here we need a heuristic:
            // treat cur as key if it was captured recently via in_str. To remain deterministic,
            // we instead look backward for the last quote-delimited fragment within a bounded window.
            // For simplicity and determinism, we accept the most recent cur captured.
            // If cur is empty, do nothing.
            push_key(cur);
            continue;
        }
    }

    if (out.empty()) out = "JSON_KEYS_EMPTY\n";
}

static void ew_xml_tag_summary_from_bytes(const uint8_t* b, size_t n, std::string& out, size_t out_cap) {
    out.clear();
    if (b == nullptr || n == 0u || out_cap == 0u) return;
    out.reserve((out_cap < 4096u) ? out_cap : 4096u);

    uint32_t tags = 0u;
    size_t i = 0u;
    while (i < n && out.size() < out_cap && tags < 256u) {
        if (b[i] != '<') { ++i; continue; }
        ++i;
        if (i >= n) break;

        const uint8_t c = b[i];
        if (c == '/' || c == '?' || c == '!') continue;

        // tag name
        std::string name;
        name.reserve(64);
        while (i < n) {
            const uint8_t ch = b[i];
            if (ch == 0x20 || ch == 0x09 || ch == 0x0A || ch == 0x0D || ch == '>' || ch == '/') break;
            if (name.size() < 64u && ch >= 33u && ch <= 126u) name.push_back((char)ch);
            ++i;
        }
        if (!name.empty()) {
            if (out.size() + name.size() + 2u <= out_cap) {
                out += name;
                out.push_back('\n');
                ++tags;
            }
        }
    }
    if (out.empty()) out = "XML_TAGS_EMPTY\n";
}

static bool ew_starts_with_utf8_ascii_prefix(const std::string& s, const char* pre) {
    const size_t n = std::strlen(pre);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) if (s[i] != pre[i]) return false;
    return true;
}

static void ew_parse_url_host_path_utf8(const std::string& url, std::string& host, std::string& path) {
    host.clear();
    path.clear();
    size_t i = 0;
    if (ew_starts_with_utf8_ascii_prefix(url, "https://")) i = 8;
    else if (ew_starts_with_utf8_ascii_prefix(url, "http://")) i = 7;
    size_t slash = url.find('/', i);
    if (slash == std::string::npos) {
        host = url.substr(i);
        path = "/";
    } else {
        host = url.substr(i, slash - i);
        path = url.substr(slash);
        if (path.empty()) path = "/";
    }
}

// Extract bounded allowlisted URLs from HTML-ish text. Deterministic scan for href=.
static void ew_extract_download_links_from_html(
    const std::string& text_utf8,
    size_t max_links,
    std::vector<std::string>& out_urls)
{
    out_urls.clear();
    const std::string t = text_utf8;
    size_t i = 0;
    while (i < t.size() && out_urls.size() < max_links) {
        size_t h = t.find("href", i);
        if (h == std::string::npos) break;
        size_t eq = t.find('=', h);
        if (eq == std::string::npos) { i = h + 4; continue; }
        size_t q = eq + 1;
        while (q < t.size() && (t[q] == ' ' || t[q] == '\t')) q++;
        if (q >= t.size()) break;
        char quote = 0;
        if (t[q] == '"' || t[q] == '\'') { quote = t[q]; q++; }
        size_t end = q;
        while (end < t.size()) {
            char c = t[end];
            if (quote != 0) {
                if (c == quote) break;
            } else {
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '>') break;
            }
            end++;
        }
        if (end > q) {
            std::string u = t.substr(q, end - q);
            // Accept only likely download links (extension check on path).
            std::string host, path;
            if (ew_starts_with_utf8_ascii_prefix(u, "http://") || ew_starts_with_utf8_ascii_prefix(u, "https://")) {
                ew_parse_url_host_path_utf8(u, host, path);
                if (ew_has_download_ext(path)) out_urls.push_back(u);
            } else {
                // Relative URL: keep as path only; caller will resolve host.
                if (!u.empty() && u[0] == '/' && ew_has_download_ext(u)) out_urls.push_back(u);
            }
        }
        i = end + 1;
    }

    // Deterministic sort by lexical order.
    std::sort(out_urls.begin(), out_urls.end());
}

// Extract sitemap URLs from robots.txt (lines "Sitemap: <url>")
static void ew_extract_sitemaps_from_robots(
    const std::string& text_utf8,
    size_t max_urls,
    std::vector<std::string>& out_urls)
{
    out_urls.clear();
    size_t i = 0;
    while (i < text_utf8.size() && out_urls.size() < max_urls) {
        size_t j = text_utf8.find('\n', i);
        if (j == std::string::npos) j = text_utf8.size();
        std::string line = text_utf8.substr(i, j - i);
        // Trim leading spaces
        size_t k = 0;
        while (k < line.size() && (line[k] == ' ' || line[k] == '\t' || line[k] == '\r')) k++;
        if (line.size() >= k + 8) {
            std::string head = ew_utf8_lower_ascii_only_str(line.substr(k, 8));
            if (head == "sitemap:") {
                size_t p = k + 8;
                while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
                if (p < line.size()) {
                    std::string u = line.substr(p);
                    // trim trailing \r
                    while (!u.empty() && (u.back() == '\r' || u.back() == ' ' || u.back() == '\t')) u.pop_back();
                    if (!u.empty()) out_urls.push_back(u);
                }
            }
        }
        i = (j < text_utf8.size()) ? (j + 1) : text_utf8.size();
    }
    std::sort(out_urls.begin(), out_urls.end());
}

// Extract <loc>...</loc> URLs from sitemap xml.
static void ew_extract_loc_urls_from_xml(
    const std::string& text_utf8,
    size_t max_urls,
    std::vector<std::string>& out_urls)
{
    out_urls.clear();
    size_t i = 0;
    while (i < text_utf8.size() && out_urls.size() < max_urls) {
        size_t a = text_utf8.find("<loc>", i);
        if (a == std::string::npos) break;
        size_t b = text_utf8.find("</loc>", a + 5);
        if (b == std::string::npos) break;
        size_t s = a + 5;
        if (b > s) {
            std::string u = text_utf8.substr(s, b - s);
            // trim whitespace
            while (!u.empty() && (u[0] == ' ' || u[0] == '\n' || u[0] == '\r' || u[0] == '\t')) u.erase(u.begin());
            while (!u.empty() && (u.back() == ' ' || u.back() == '\n' || u.back() == '\r' || u.back() == '\t')) u.pop_back();
            if (!u.empty()) out_urls.push_back(u);
        }
        i = b + 6;
    }
    std::sort(out_urls.begin(), out_urls.end());
}


// ------------------------------------------------------------------
// Web result extraction (HTML). Conservative deterministic parsing.
// Used for search results pages so the user can OPEN:<n> and so the
// corpus gains grounded result metadata over time.
// ------------------------------------------------------------------
struct EwWebResultAscii {
    std::string url_utf8;
    std::string title_utf8;
    std::string snippet_utf8;
};

static int ew_hex_val_ascii(char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'A' && c <= 'F') return (int)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (int)(c - 'a' + 10);
    return -1;
}

static std::string ew_percent_decode_utf8(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '%' && (i + 2) < in.size()) {
            int a = ew_hex_val_ascii(in[i + 1]);
            int b = ew_hex_val_ascii(in[i + 2]);
            if (a >= 0 && b >= 0) {
                out.push_back((char)((a << 4) | b));
                i += 2;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

static void ew_trim_utf8_inplace(std::string& s) {
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\n' || s[0] == '\r')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
}

static void ew_strip_tags_utf8_inplace(std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_tag = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (!in_tag && c == '<') { in_tag = true; continue; }
        if (in_tag && c == '>') { in_tag = false; continue; }
        if (!in_tag) out.push_back(c);
    }
    s.swap(out);
}

static void ew_html_entity_decode_utf8_inplace(std::string& s) {
    // Minimal entity set needed for search result titles/snippets.
    auto repl_all = [&](const char* a, const char* b) {
        const std::string A(a), B(b);
        size_t pos = 0;
        while (true) {
            pos = s.find(A, pos);
            if (pos == std::string::npos) break;
            s.replace(pos, A.size(), B);
            pos += B.size();
        }
    };
    repl_all("&amp;", "&");
    repl_all("&quot;", "\"");
    repl_all("&#34;", "\"");
    repl_all("&#39;", "'");
    repl_all("&apos;", "'");
    repl_all("&lt;", "<");
    repl_all("&gt;", ">");
}

static bool ew_extract_query_param_utf8(const std::string& url, const char* key, std::string& out_val) {
    out_val.clear();
    const std::string k = std::string(key) + "=";
    size_t p = url.find(k);
    if (p == std::string::npos) return false;
    p += k.size();
    size_t e = url.find('&', p);
    if (e == std::string::npos) e = url.size();
    if (e <= p) return false;
    out_val = url.substr(p, e - p);
    return true;
}

static void ew_extract_ddg_search_results_from_html_bytes(
    const uint8_t* bytes,
    size_t n,
    size_t max_results,
    std::vector<EwWebResultAscii>& out_results)
{
    out_results.clear();
    if (bytes == nullptr || n == 0u) return;

    const size_t cap = (n < (size_t)262144) ? n : (size_t)262144;
    std::string t;
    t.reserve(cap);
    for (size_t i = 0; i < cap; ++i) {
        unsigned char ch = (unsigned char)bytes[i];
        if (ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 32 && ch <= 126)) t.push_back((char)ch);
        else t.push_back(' ');
    }

    size_t i = 0;
    while (i < t.size() && out_results.size() < max_results) {
        // Anchor class marker.
        size_t a = t.find("result__a", i);
        if (a == std::string::npos) break;

        // Find containing <a ...> tag.
        size_t tag_start = t.rfind("<a", a);
        if (tag_start == std::string::npos) { i = a + 8; continue; }
        size_t tag_end = t.find('>', a);
        if (tag_end == std::string::npos) break;

        // href within tag.
        size_t href = t.find("href", tag_start);
        if (href == std::string::npos || href > tag_end) { i = tag_end + 1; continue; }
        size_t eq = t.find('=', href);
        if (eq == std::string::npos || eq > tag_end) { i = tag_end + 1; continue; }
        size_t q = eq + 1;
        while (q < tag_end && (t[q] == ' ' || t[q] == '\t')) q++;
        if (q >= tag_end) { i = tag_end + 1; continue; }
        char quote = 0;
        if (t[q] == '"' || t[q] == '\'') { quote = t[q]; q++; }
        size_t end = q;
        while (end < tag_end) {
            const char c = t[end];
            if (quote != 0) { if (c == quote) break; }
            else { if (c == ' ' || c == '\t') break; }
            end++;
        }
        if (end <= q) { i = tag_end + 1; continue; }

        std::string href_val = t.substr(q, end - q);

        // Resolve DuckDuckGo redirect to a real URL when possible.
        std::string real_url = href_val;
        std::string uddg;
        if (ew_extract_query_param_utf8(href_val, "uddg", uddg)) {
            real_url = ew_percent_decode_utf8(uddg);
        }

        // Basic accept: http(s) only.
        if (!(ew_starts_with_utf8_ascii_prefix(real_url, "http://") || ew_starts_with_utf8_ascii_prefix(real_url, "https://"))) {
            i = tag_end + 1;
            continue;
        }

        // Title: inner text until </a>.
        size_t close_a = t.find("</a>", tag_end + 1);
        if (close_a == std::string::npos) break;
        std::string title = t.substr(tag_end + 1, close_a - (tag_end + 1));
        ew_strip_tags_utf8_inplace(title);
        ew_html_entity_decode_utf8_inplace(title);
        ew_trim_utf8_inplace(title);
        if (title.size() > 160) title.resize(160);

        // Snippet: search forward for result__snippet within a bounded window.
        std::string snippet;
        const size_t win_end = ((close_a + 8192) < t.size()) ? (close_a + 8192) : t.size();
        size_t sn = t.find("result__snippet", close_a);
        if (sn != std::string::npos && sn < win_end) {
            size_t sn_tag_end = t.find('>', sn);
            if (sn_tag_end != std::string::npos && sn_tag_end < win_end) {
                size_t sn_close = t.find("</", sn_tag_end + 1);
                if (sn_close != std::string::npos && sn_close < win_end) {
                    snippet = t.substr(sn_tag_end + 1, sn_close - (sn_tag_end + 1));
                    ew_strip_tags_utf8_inplace(snippet);
                    ew_html_entity_decode_utf8_inplace(snippet);
                    ew_trim_utf8_inplace(snippet);
                    if (snippet.size() > 240) snippet.resize(240);
                }
            }
        }

        EwWebResultAscii r{};
        r.url_utf8 = real_url;
        r.title_utf8 = title;
        r.snippet_utf8 = snippet;
        out_results.push_back(r);

        i = close_a + 4;
    }
}
// Deterministic license marker check (bounded scan). Returns true if explicit open marker found.
static bool ew_license_marker_ok_bounded(const uint8_t* bytes, size_t n) {
    if (bytes == nullptr || n == 0) return false;
    const size_t cap = (n < (size_t)8192) ? n : (size_t)8192;
    std::string s;
    s.reserve(cap);
    for (size_t i = 0; i < cap; ++i) {
        unsigned char ch = (unsigned char)bytes[i];
        if (ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 32 && ch <= 126)) s.push_back((char)ch);
        else s.push_back(' ');
    }
    const std::string lo = ew_utf8_lower_ascii_only_str(s);
    const char* marks[] = {"creativecommons.org", "cc-by", "cc by", "public domain", "open license", "cc0"};
    for (size_t i = 0; i < sizeof(marks)/sizeof(marks[0]); ++i) {
        if (lo.find(marks[i]) != std::string::npos) return true;
    }
    return false;
}

void SubstrateManager::submit_external_api_response(const EwExternalApiResponse& resp) {
    // Feed a deterministic response summary back into the substrate. We do not
    // parse response bodies here; parsing/meaning is phase dynamics.
    const uint32_t EW_CAUSAL_TAG_EXTERNAL_API_RESP = 0x41504932U; // 'API2'
    std::string line = "API_RESP ";
    line += std::to_string((unsigned long long)resp.request_id_u64);
    line += " ";
    line += std::to_string((long long)resp.http_status_s32);
    line += " ";
    line += std::to_string((unsigned long long)resp.body_bytes.size());
    last_observation_text = line;
    const std::string domain_ascii = std::string("domain_") + std::to_string((unsigned long long)resp.domain_anchor_id_u32);
    const std::string url_ascii = std::string("api://") + std::to_string((unsigned long long)resp.request_id_u64);
    crawler.enqueue_observation_utf8(
        resp.request_id_u64,
        (resp.domain_anchor_id_u32 != 0u) ? resp.domain_anchor_id_u32 : active_crawl_crawler_anchor_id_u32,
        resp.crawler_anchor_id_u32,
        resp.context_anchor_id_u32,
        0u,
        0u,
        1u,
        EW_CAUSAL_TAG_EXTERNAL_API_RESP,
        domain_ascii,
        url_ascii,
        line
    );

    // Also enqueue a bounded UTF-8 snippet for ingestion (deterministic truncation + sanitation).
    if (!resp.body_bytes.empty()) {
        const size_t cap = (resp.body_bytes.size() < (size_t)1024) ? resp.body_bytes.size() : (size_t)1024;
        std::string snippet;
        snippet.reserve(cap);
        for (size_t i = 0; i < cap; ++i) {
            unsigned char ch = (unsigned char)resp.body_bytes[i];
            if (ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 32 && ch <= 126)) snippet.push_back((char)ch);
            else snippet.push_back(' ');
        }
        crawler.enqueue_observation_utf8(
            resp.request_id_u64 ^ 0xB0D1U,
            (resp.domain_anchor_id_u32 != 0u) ? resp.domain_anchor_id_u32 : active_crawl_crawler_anchor_id_u32,
            resp.crawler_anchor_id_u32,
            resp.context_anchor_id_u32,
            2u,
            2u,
            1u,
            EW_CAUSAL_TAG_EXTERNAL_API_RESP,
            domain_ascii,
            url_ascii,
            snippet
        );

        const size_t drive_cap =
            (resp.body_bytes.size() < (size_t)4096) ? resp.body_bytes.size() : (size_t)4096;
        if (drive_cap != 0u) {
            std::ostringstream meta;
            meta << "domain=ai_substrate\n"
                 << "kind=api_decode\n"
                 << "request_id=" << resp.request_id_u64 << "\n"
                 << "http_status=" << resp.http_status_s32 << "\n"
                 << "context_anchor=" << resp.context_anchor_id_u32 << "\n"
                 << "crawler_anchor=" << resp.crawler_anchor_id_u32 << "\n"
                 << "domain_anchor=" << resp.domain_anchor_id_u32 << "\n";
            if (ai_substrate_drive.put_bytes("ai/api_decode/last",
                                             "api_decode_bytes",
                                             meta.str(),
                                             resp.body_bytes.data(),
                                             drive_cap)) {
                ai_api_decode_count_u64 += 1u;
                ai_last_api_decode_request_id_u64 = resp.request_id_u64;
                ai_last_api_decode_http_status_s32 = resp.http_status_s32;
                ai_last_api_decode_body_bytes_u32 = (uint32_t)drive_cap;
            }
        }
    }
}

#if 0

    // ------------------------------------------------------------------
    // Learning honesty gate task registration (measurable target)
    // + GPU-resident crawler compression (page-level SpiderCode4)
    //
    // Canonical rule: CPU does not compute metric selection or encoding.
    // We generate a GPU page summary (SpiderCode4 + keyword mask + counts)
    // and then enqueue specific MetricKind tasks based on detected keywords.
    // ------------------------------------------------------------------
    {
        EwCrawlerPageSummary sum{};
        bool ok = false;
#if defined(EW_ENABLE_CUDA) && (EW_ENABLE_CUDA==1)
        if (!resp.body_bytes.empty()) {
            ok = ew_encode_page_summary_cuda(resp.body_bytes.data(), resp.body_bytes.size(), (size_t)65536, &sum);
        }
#endif
        if (ok) {
            // Update domain topic mask for scheduling priority.
            for (uint32_t si = 0; si < EW_CRAWL_SESSION_MAX; ++si) {
                if (!crawl_sessions[si].active) continue;
                EwCrawlSession& ss = crawl_sessions[si];
                for (size_t di = 0; di < ss.domain_map.size(); ++di) {
                    if (ss.domain_map[di].domain_anchor_id_u32 == resp.domain_anchor_id_u32) {
                        ss.domain_map[di].observed_topic_mask_u64 |= sum.metric_mask_u64;
                        break;
                    }
                }
            }

            auto kind_bit = [] (genesis::MetricKind k) -> uint64_t {
                const uint32_t id = (uint32_t)k;
                return 1ULL << (id & 63u);
            };
            const uint32_t cur_stage = learning_curriculum_stage_u32;
            const uint64_t need_mask = learning_stage_required_mask_u64[cur_stage] & ~learning_stage_completed_mask_u64[cur_stage];
            const uint64_t hit_mask = sum.metric_mask_u64 & need_mask;

            // Deterministic measurable target vector from GPU counts.
            genesis::MetricVector mv{};
            mv.dim_u32 = 3u;
            mv.v_q32_32[0] = (int64_t)((uint64_t)sum.len_u32 << 32);
            mv.v_q32_32[1] = (int64_t)((uint64_t)sum.ascii_sum_u32 << 32);
            mv.v_q32_32[2] = (int64_t)((uint64_t)sum.newline_count_u32 << 32);

            auto enqueue_kind = [&] (genesis::MetricKind k) {
                genesis::MetricTask mt{};
                mt.task_id_u64 = 0;
                mt.source_id_u64 = resp.request_id_u64;
                mt.source_anchor_id_u32 = resp.domain_anchor_id_u32;
                mt.context_anchor_id_u32 = resp.context_anchor_id_u32;
                mt.target.kind = k;
                mt.target.target = mv;
                mt.target.tol_num_u32 = 6u;
                mt.target.tol_den_u32 = 100u;
                learning_gate.registry().enqueue_task(mt);
            };

            if (hit_mask & kind_bit(genesis::MetricKind::Qm_DoubleSlit_Fringes)) enqueue_kind(genesis::MetricKind::Qm_DoubleSlit_Fringes);
            if (hit_mask & kind_bit(genesis::MetricKind::Qm_ParticleInBox_Levels)) enqueue_kind(genesis::MetricKind::Qm_ParticleInBox_Levels);
            if (hit_mask & kind_bit(genesis::MetricKind::Qm_HarmonicOsc_Spacing)) enqueue_kind(genesis::MetricKind::Qm_HarmonicOsc_Spacing);
            if (hit_mask & kind_bit(genesis::MetricKind::Qm_Tunneling_Transmission)) enqueue_kind(genesis::MetricKind::Qm_Tunneling_Transmission);

            if (hit_mask & kind_bit(genesis::MetricKind::Atom_Orbital_EnergyRatios)) enqueue_kind(genesis::MetricKind::Atom_Orbital_EnergyRatios);
            if (hit_mask & kind_bit(genesis::MetricKind::Atom_Orbital_RadialNodes)) enqueue_kind(genesis::MetricKind::Atom_Orbital_RadialNodes);
            if (hit_mask & kind_bit(genesis::MetricKind::Bond_Length_Equilibrium)) enqueue_kind(genesis::MetricKind::Bond_Length_Equilibrium);
            if (hit_mask & kind_bit(genesis::MetricKind::Bond_Vibration_Frequency)) enqueue_kind(genesis::MetricKind::Bond_Vibration_Frequency);

            if (hit_mask & kind_bit(genesis::MetricKind::Chem_ReactionRate_Temp)) enqueue_kind(genesis::MetricKind::Chem_ReactionRate_Temp);
            if (hit_mask & kind_bit(genesis::MetricKind::Chem_Equilibrium_Constant)) enqueue_kind(genesis::MetricKind::Chem_Equilibrium_Constant);
            if (hit_mask & kind_bit(genesis::MetricKind::Chem_Diffusion_Coefficient)) enqueue_kind(genesis::MetricKind::Chem_Diffusion_Coefficient);

            if (hit_mask & kind_bit(genesis::MetricKind::Mat_Thermal_Conductivity)) enqueue_kind(genesis::MetricKind::Mat_Thermal_Conductivity);
            if (hit_mask & kind_bit(genesis::MetricKind::Mat_Electrical_Conductivity)) enqueue_kind(genesis::MetricKind::Mat_Electrical_Conductivity);
            if (hit_mask & kind_bit(genesis::MetricKind::Mat_StressStrain_Modulus)) enqueue_kind(genesis::MetricKind::Mat_StressStrain_Modulus);
            if (hit_mask & kind_bit(genesis::MetricKind::Mat_PhaseChange_Threshold)) enqueue_kind(genesis::MetricKind::Mat_PhaseChange_Threshold);

            if (hit_mask & kind_bit(genesis::MetricKind::Cosmo_Orbit_Period)) enqueue_kind(genesis::MetricKind::Cosmo_Orbit_Period);
            if (hit_mask & kind_bit(genesis::MetricKind::Cosmo_Radiation_Spectrum)) enqueue_kind(genesis::MetricKind::Cosmo_Radiation_Spectrum);
            if (hit_mask & kind_bit(genesis::MetricKind::Cosmo_Atmos_PressureProfile)) enqueue_kind(genesis::MetricKind::Cosmo_Atmos_PressureProfile);

            if (hit_mask & kind_bit(genesis::MetricKind::Bio_CellDiffusion_Osmosis)) enqueue_kind(genesis::MetricKind::Bio_CellDiffusion_Osmosis);

            // Admit a single page-level pulse into the inbound queue.
            {
                const SpiderCode4 sc = sum.page_sc;
                Pulse p{};
                p.anchor_id = (resp.domain_anchor_id_u32 != 0u) ? resp.domain_anchor_id_u32 : active_crawl_crawler_anchor_id_u32;
                p.f_code = sc.f_code;
                p.a_code = sc.a_code;
                p.v_code = sc.v_code;
                p.i_code = sc.i_code;
                p.profile_id = 0u;
                p.causal_tag = EW_CAUSAL_TAG_EXTERNAL_API_RESP;
                p.pad0 = 0u;
                p.pad1 = 0u;
                p.tick = canonical_tick;
                inbound.push_back(p);
            }
        }
    }
// ------------------------------------------------------------------
// Deterministic crawl stage expansion:
// - robots.txt -> sitemap urls + root
// - root/html -> prefer downloadable links (.pdf/.zip/.json/.xml)
// - sitemap xml -> prefer downloadable loc urls
// Strict allowlist: only schedule hosts present in the session domain map.
// ------------------------------------------------------------------
// Find inflight metadata.
EwExternalApiInflight infl{};
bool have_in = false;
for (auto it = external_api_inflight.begin(); it != external_api_inflight.end(); ++it) {
    if (it->request_id_u64 == resp.request_id_u64) {
        infl = *it;
        external_api_inflight.erase(it);
        have_in = true;
        break;
    }
}
if (have_in) {
    const uint32_t si = (infl.session_idx_u32 < EW_CRAWL_SESSION_MAX) ? infl.session_idx_u32 : 0u;
    EwCrawlSession& ss = crawl_sessions[si];
    if (ss.active) {
        // Bounded ascii text view of body for deterministic parsing.
        const size_t cap = (resp.body_bytes.size() < (size_t)8192) ? resp.body_bytes.size() : (size_t)8192;
        std::string text;
        text.reserve(cap);
        for (size_t i = 0; i < cap; ++i) {
            unsigned char ch = (unsigned char)resp.body_bytes[i];
            if (ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 32 && ch <= 126)) text.push_back((char)ch);
            else text.push_back(' ');
        }

        auto session_has_host = [&](const std::string& host)->uint32_t {
            for (size_t di = 0; di < ss.domain_map.size(); ++di) {
                if (ss.domain_map[di].domain_utf8 == host) return ss.domain_map[di].domain_anchor_id_u32;
            }
            return 0u;
        };

        auto seen_insert = [&](const std::string& key)->bool {
            // Bounded dedupe list.
            for (size_t k = 0; k < ss.seen_url_keys.size(); ++k) if (ss.seen_url_keys[k] == key) return false;
            if (ss.seen_url_keys.size() < 4096) ss.seen_url_keys.push_back(key);
            return true;
        };

        auto push_target_front = [&](uint32_t stage_u32, const std::string& host, const std::string& path) {
            if (host.empty()) return;
            if (path.empty() || path[0] != '/') return;
            if (session_has_host(host) == 0u) return; // strict allowlist
            const std::string key = std::string("https://") + host + path;
            if (!seen_insert(key)) return;

            EwCorpusCrawlTarget t{};
            t.lane_u32 = 0;
            t.stage_u32 = stage_u32;
            t.profile_u32 = ss.profile_u32;
            t.host_utf8 = host;
            t.path_utf8 = path;
            ss.q.push_front(t);
        };

        auto push_target_back = [&](uint32_t stage_u32, const std::string& host, const std::string& path) {
            if (host.empty()) return;
            if (path.empty() || path[0] != '/') return;
            if (session_has_host(host) == 0u) return; // strict allowlist
            const std::string key = std::string("https://") + host + path;
            if (!seen_insert(key)) return;

            EwCorpusCrawlTarget t{};
            t.lane_u32 = 0;
            t.stage_u32 = stage_u32;
            t.profile_u32 = ss.profile_u32;
            t.host_utf8 = host;
            t.path_utf8 = path;
            ss.q.push_back(t);
        };

        // Only expand stages on successful responses.
        if (resp.http_status_s32 >= 200 && resp.http_status_s32 < 400) {
            if (infl.stage_u32 == 0u) {
                // robots.txt -> enqueue root and any sitemap urls
                push_target_back(1u, infl.host_utf8, "/");

                std::vector<std::string> smaps;
                ew_extract_sitemaps_from_robots(text, 16, smaps);
                for (size_t k = 0; k < smaps.size(); ++k) {
                    std::string h, p;
                    ew_parse_url_host_path_utf8(smaps[k], h, p);
                    if (!h.empty() && !p.empty() && p[0] == '/') {
                        // Prefer sitemap parsing early.
                        push_target_front(2u, h, p);
                    }
                }
            } else if (infl.stage_u32 == 1u) {
                // root/html -> detect downloadable links and schedule them.
                const std::string lo = ew_utf8_lower_ascii_only_str(text);
                const bool looks_html = (lo.find("<html") != std::string::npos) || (lo.find("href") != std::string::npos);
                if (looks_html) {
                    std::vector<std::string> links;
                    ew_extract_download_links_from_html(lo, 32, links);
                    // push in reverse so the lowest lexical ends up first (stable preference).
                    for (size_t r = links.size(); r > 0; --r) {
                        const std::string u = links[r - 1];
                        if (ew_starts_with_utf8_ascii_prefix(u, "http://") || ew_starts_with_utf8_ascii_prefix(u, "https://")) {
                            std::string h, p;
                            ew_parse_url_host_path_utf8(u, h, p);
                            if (ew_has_download_ext(p)) push_target_front(3u, h, p);
                        } else {
                            if (!u.empty() && u[0] == '/' && ew_has_download_ext(u)) push_target_front(3u, infl.host_utf8, u);
                        }
                    }
                }
            } else if (infl.stage_u32 == 2u) {
                // sitemap xml -> schedule downloadable loc urls
                const std::string lo = ew_utf8_lower_ascii_only_str(text);
                std::vector<std::string> locs;
                ew_extract_loc_urls_from_xml(lo, 64, locs);
                for (size_t r = locs.size(); r > 0; --r) {
                    std::string h, p;
                    ew_parse_url_host_path_utf8(locs[r - 1], h, p);
                    if (ew_has_download_ext(p)) push_target_front(3u, h, p);
                    else {
                        // If loc is relative path inside same host and looks downloadable.
                        if (!locs[r - 1].empty() && locs[r - 1][0] == '/' && ew_has_download_ext(locs[r - 1])) {
                            push_target_front(3u, infl.host_utf8, locs[r - 1]);
                        }
                    }
                }
            } else {
                // stage 3: downloadable doc; no further expansion.
            }
        }
    }
}


}

#endif // disabled block for compilation hygiene

bool SubstrateManager::submit_external_api_response_chunk(
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
) {
    if (bytes_len_u32 == 0 && !is_final) return true;
    if (bytes == nullptr && bytes_len_u32 != 0) return false;

    // Backpressure: bound inflight bytes and per-doc size.
    if (external_api_ingest_inflight_bytes_u64 + (uint64_t)bytes_len_u32 > (uint64_t)ingest_max_inflight_bytes_u32) {
        return false;
    }
    if (bytes_len_u32 > ingest_max_doc_bytes_u32) return false;

    EwExternalApiIngestChunk c{};
    c.request_id_u64 = request_id_u64;
    c.tick_u64 = tick_u64;
    c.http_status_s32 = http_status_s32;
    c.context_anchor_id_u32 = context_anchor_id_u32;
    c.crawler_anchor_id_u32 = crawler_anchor_id_u32;
    c.domain_anchor_id_u32 = domain_anchor_id_u32;
    c.offset_u32 = offset_u32;
    c.is_final = is_final;
    if (bytes_len_u32 != 0) {
        c.bytes.assign(bytes, bytes + (size_t)bytes_len_u32);
        external_api_ingest_inflight_bytes_u64 += (uint64_t)bytes_len_u32;
    }
    external_api_ingest_inbox.push_back(c);
    return true;
}


void SubstrateManager::observe_text_line(const std::string& utf8_line) {
    // Store observation inside substrate memory for phase operators.
    observation_seq_u64 += 1u;
    last_observation_text = utf8_line;

    // Route through crawler with deterministic labels.
    // artifact_id_u64: structural coord coord-tag derived from the same
    // UTF-8->frequency path used by the TEXT encoder.
    // Packed form: (byte_len_u32, f_code_u32).
    const int32_t f_code = ew_text_utf8_to_frequency_code(utf8_line, (uint8_t)EW_PROFILE_LANGUAGE_INJECTION);
    const uint32_t byte_len_u32 = (uint32_t)utf8_line.size();
    const uint32_t f_code_u32 = (uint32_t)f_code;
    const uint64_t artifact_id_u64 = (static_cast<uint64_t>(byte_len_u32) << 32) | static_cast<uint64_t>(f_code_u32);


    // Phase-amplitude current: every observation emits an activation footprint.
    phase_current.on_activation(genesis::EwPhaseCurrent::footprint_from_text(canonical_tick, artifact_id_u64));

    const uint32_t stream_id_u32 = 1u;
    const uint32_t extractor_id_u32 = 1u; // UTF8 text
    const uint32_t trust_class_u32 = 1u;  // local user observation
    const uint32_t causal_tag_u32 = 1u;   // activate

    crawler_enqueue_observation_utf8(
        artifact_id_u64,
        stream_id_u32,
        extractor_id_u32,
        trust_class_u32,
        causal_tag_u32,
        std::string("user"),
        std::string("ui:text"),
        utf8_line
    );
}


void SubstrateManager::ui_submit_user_text_line(const std::string& utf8_line) {
    // Control commands (deterministic ASCII parsing)
    //  - /crawl_live on consent=1
    //  - /crawl_live off
    //  - /crawl_seed url=http://example.com/
    //  - /anticipation on|off [auto=0|1] [emit=0|1]
    if (utf8_line.rfind("/crawl_live", 0) == 0 || utf8_line.rfind("crawl_live:", 0) == 0) {
        std::vector<std::string> toks;
        ew::ew_split_shell_ascii(utf8_line, toks);
        bool on = false;
        bool off = false;
        bool consent = false;
        for (size_t i = 1; i < toks.size(); ++i) {
            const std::string& t = toks[i];
            if (t == "on") on = true;
            else if (t == "off") off = true;
            else {
                std::string_view k_sv, v_sv;
                if (ew::ew_split_kv_token_ascii(std::string_view(t), k_sv, v_sv)) {
                    if (k_sv == "consent") {
                        bool b = false;
                        if (ew::ew_parse_bool_ascii(v_sv, b)) consent = b;
                    }
                }
            }
        }
        if (off) {
            crawler_enable_live_u32 = 0u;
            crawler_live_consent_required_u32 = 1u;
            live_crawler.enabled = false;
            live_crawler.consent_granted = false;
            emit_ui_line("GE_LIVE_CRAWL:disabled");
            return;
        }
        if (on) {
            crawler_enable_live_u32 = 1u;
            if (consent) {
                crawler_live_consent_required_u32 = 0u;
                live_crawler.enabled = true;
                live_crawler.consent_granted = true;
                emit_ui_line("GE_LIVE_CRAWL:enabled consent=1");
            } else {
                crawler_live_consent_required_u32 = 1u;
                emit_ui_line("GE_LIVE_CRAWL:enable_requested consent=0");
            }
            return;
        }
    }
    if (utf8_line.rfind("/crawl_seed", 0) == 0 || utf8_line.rfind("crawl_seed:", 0) == 0) {
        std::vector<std::string> toks;
        ew::ew_split_shell_ascii(utf8_line, toks);
        std::string url;
        for (size_t i = 1; i < toks.size(); ++i) {
            std::string_view k_sv, v_sv;
            if (ew::ew_split_kv_token_ascii(std::string_view(toks[i]), k_sv, v_sv)) {
                if (k_sv == "url") url = std::string(v_sv);
            }
        }
        if (!url.empty()) {
            live_crawler.enqueue_url(url);
            emit_ui_line("GE_LIVE_CRAWL:seed url=" + url);
        }
        return;
    }

    // User-updatable allowlist.
    //  - /allowlist_reload (loads Draft Container/Corpus/allowlist_user.md if present)
    //  - /allowlist_update <markdown...>  (inline markdown; bounded)
    //  - allowlist_update: <markdown...>
    if (utf8_line.rfind("/allowlist_reload", 0) == 0 || utf8_line.rfind("allowlist_reload:", 0) == 0) {
        const bool ok = corpus_allowlist_load_user_file_if_present();
        emit_ui_line(ok ? "ALLOWLIST_RELOADED" : "ALLOWLIST_RELOAD_NOFILE");
        return;
    }
    if (utf8_line.rfind("/allowlist_update", 0) == 0 || utf8_line.rfind("allowlist_update:", 0) == 0) {
        // Inline update: everything after first space (or after colon) is treated as markdown.
        std::string md;
        const size_t psp = utf8_line.find(' ');
        const size_t pco = utf8_line.find(':');
        size_t p = std::string::npos;
        if (psp != std::string::npos) p = psp + 1;
        else if (pco != std::string::npos) p = pco + 1;
        if (p != std::string::npos && p < utf8_line.size()) {
            md = utf8_line.substr(p);
            while (!md.empty() && (md[0] == ' ' || md[0] == '\t')) md.erase(md.begin());
        }
        if (md.empty()) {
            emit_ui_line("ALLOWLIST_UPDATE_EMPTY");
        } else {
            (void)corpus_allowlist_update_from_user_text(md);
        }
        return;
    }

    if (utf8_line.rfind("/repo_reader", 0) == 0 || utf8_line.rfind("repo_reader:", 0) == 0) {
        std::vector<std::string> toks;
        ew::ew_split_shell_ascii(utf8_line, toks);
        bool enable = false;
        bool disable = false;
        bool rescan = false;
        for (size_t i = 1; i < toks.size(); ++i) {
            const std::string& t = toks[i];
            if (t == "on") enable = true;
            else if (t == "off") disable = true;
            else if (t == "scan" || t == "rescan") rescan = true;
            else {
                std::string_view k_sv, v_sv;
                if (ew::ew_split_kv_token_ascii(std::string_view(t), k_sv, v_sv)) {
                    if (k_sv == "files") {
                        uint32_t v = (uint32_t)std::strtoul(std::string(v_sv).c_str(), nullptr, 10);
                        repo_reader_files_per_tick_u32 = std::max<uint32_t>(1u, std::min<uint32_t>(v, 16u));
                    } else if (k_sv == "bytes") {
                        uint32_t v = (uint32_t)std::strtoul(std::string(v_sv).c_str(), nullptr, 10);
                        repo_reader_bytes_per_file_u32 = std::max<uint32_t>(512u, std::min<uint32_t>(v, 65536u));
                    } else if (k_sv == "rescan") {
                        bool b = false;
                        if (ew::ew_parse_bool_ascii(v_sv, b)) rescan = b;
                    }
                }
            }
        }
        if (disable) repo_reader.enabled = false;
        if (enable) repo_reader.enabled = true;
        if (rescan) repo_reader.scan_repo_root();
        emit_ui_line(ge_format_repo_reader_status_line(*this, "REPO_READER_STATUS"));
        return;
    }
    if (utf8_line.rfind("/repo_reader_status", 0) == 0 ||
        utf8_line.rfind("repo_reader_status:", 0) == 0) {
        if (!repo_reader.scanned) repo_reader.scan_repo_root();
        emit_ui_line(ge_format_repo_reader_status_line(*this, "REPO_READER_STATUS"));
        return;
    }
    if (utf8_line.rfind("/merged_repo_status", 0) == 0 ||
        utf8_line.rfind("merged_repo_status:", 0) == 0) {
        if (!repo_reader.scanned) repo_reader.scan_repo_root();
        emit_ui_line(ge_format_merged_repo_status_line(*this, "MERGED_REPO_STATUS"));
        emit_ui_line(ge_format_repo_reader_status_line(*this, "REPO_READER_STATUS"));
        return;
    }

    if (utf8_line.rfind("/process_substrate_status", 0) == 0 ||
        utf8_line.rfind("process_substrate_status:", 0) == 0) {
        emit_ui_line(ge_format_process_substrate_status_line(process_substrate_telemetry,
                                                             "PROCESS_SUBSTRATE_STATUS"));
        return;
    }
    if (utf8_line.rfind("/subsystem_substrate_status", 0) == 0 ||
        utf8_line.rfind("subsystem_substrate_status:", 0) == 0) {
        emit_ui_line(ge_format_subsystem_substrate_status_line(subsystem_substrate_telemetry,
                                                               "SUBSYSTEM_SUBSTRATE_STATUS"));
        return;
    }
    if (utf8_line.rfind("/ai_status", 0) == 0 ||
        utf8_line.rfind("ai_status:", 0) == 0 ||
        utf8_line.rfind("/ai_substrate_status", 0) == 0 ||
        utf8_line.rfind("ai_substrate_status:", 0) == 0) {
        emit_ui_line(ge_format_ai_substrate_status_line(ai_substrate_telemetry,
                                                        "AI_SUBSTRATE_STATUS"));
        return;
    }
    if (utf8_line.rfind("/ai_data_status", 0) == 0 ||
        utf8_line.rfind("ai_data_status:", 0) == 0 ||
        utf8_line.rfind("/ai_data_substrate_status", 0) == 0 ||
        utf8_line.rfind("ai_data_substrate_status:", 0) == 0) {
        emit_ui_line(ge_format_ai_data_substrate_status_line(ai_data_substrate_telemetry,
                                                             "AI_DATA_SUBSTRATE_STATUS"));
        return;
    }
    if (utf8_line.rfind("/research_status", 0) == 0 ||
        utf8_line.rfind("research_status:", 0) == 0) {
        emit_ui_line(ge_format_process_substrate_status_line(process_substrate_telemetry,
                                                             "PROCESS_SUBSTRATE_STATUS"));
        emit_ui_line(ge_format_subsystem_substrate_status_line(subsystem_substrate_telemetry,
                                                               "SUBSYSTEM_SUBSTRATE_STATUS"));
        emit_ui_line(ge_format_ai_substrate_status_line(ai_substrate_telemetry,
                                                        "AI_SUBSTRATE_STATUS"));
        emit_ui_line(ge_format_ai_data_substrate_status_line(ai_data_substrate_telemetry,
                                                             "AI_DATA_SUBSTRATE_STATUS"));
        emit_ui_line(ge_format_merged_repo_status_line(*this, "MERGED_REPO_STATUS"));
        emit_ui_line(ge_format_repo_reader_status_line(*this, "REPO_READER_STATUS"));
        return;
    }

    // Simulation snapshot + playback/injection.
    //  - /sim_save name=foo
    //  - /sim_play_loop name=foo
    //  - /sim_play_live name=foo
    if (utf8_line.rfind("/sim_save", 0) == 0 || utf8_line.rfind("sim_save:", 0) == 0) {
        std::vector<std::string> toks;
        ew::ew_split_shell_ascii(utf8_line, toks);
        std::string name;
        for (size_t i = 1; i < toks.size(); ++i) {
            std::string_view k_sv, v_sv;
            if (ew::ew_split_kv_token_ascii(std::string_view(toks[i]), k_sv, v_sv)) {
                if (k_sv == "name") name = std::string(v_sv);
            }
        }
        (void)sim_save_to_file(name);
        return;
    }
    if (utf8_line.rfind("/sim_play_loop", 0) == 0 || utf8_line.rfind("sim_play_loop:", 0) == 0) {
        std::vector<std::string> toks;
        ew::ew_split_shell_ascii(utf8_line, toks);
        std::string name;
        for (size_t i = 1; i < toks.size(); ++i) {
            std::string_view k_sv, v_sv;
            if (ew::ew_split_kv_token_ascii(std::string_view(toks[i]), k_sv, v_sv)) {
                if (k_sv == "name") name = std::string(v_sv);
            }
        }
        (void)sim_load_from_file(name, true);
        return;
    }
    if (utf8_line.rfind("/sim_play_live", 0) == 0 || utf8_line.rfind("sim_play_live:", 0) == 0) {
        std::vector<std::string> toks;
        ew::ew_split_shell_ascii(utf8_line, toks);
        std::string name;
        for (size_t i = 1; i < toks.size(); ++i) {
            std::string_view k_sv, v_sv;
            if (ew::ew_split_kv_token_ascii(std::string_view(toks[i]), k_sv, v_sv)) {
                if (k_sv == "name") name = std::string(v_sv);
            }
        }
        (void)sim_load_from_file(name, false);
        return;
    }

    // AI anticipation configuration (deterministic, ASCII tokens).
    if (utf8_line.rfind("/anticipation", 0) == 0 || utf8_line.rfind("anticipation:", 0) == 0) {
        std::vector<std::string> toks;
        ew::ew_split_shell_ascii(utf8_line, toks);
        bool on = false;
        bool off = false;
        bool auto_set = false;
        bool emit_set = false;
        bool auto_v = ai_anticipation_auto_execute;
        bool emit_v = ai_anticipation_emit_ui;
        for (size_t i = 1; i < toks.size(); ++i) {
            const std::string& t = toks[i];
            if (t == "on") on = true;
            else if (t == "off") off = true;
            else {
                std::string_view k_sv, v_sv;
                if (ew::ew_split_kv_token_ascii(std::string_view(t), k_sv, v_sv)) {
                    if (k_sv == "auto") {
                        bool b = false;
                        if (ew::ew_parse_bool_ascii(v_sv, b)) { auto_set = true; auto_v = b; }
                    } else if (k_sv == "emit") {
                        bool b = false;
                        if (ew::ew_parse_bool_ascii(v_sv, b)) { emit_set = true; emit_v = b; }
                    }
                }
            }
        }
        if (on) ai_anticipation_enabled = true;
        if (off) ai_anticipation_enabled = false;
        if (auto_set) ai_anticipation_auto_execute = auto_v;
        if (emit_set) ai_anticipation_emit_ui = emit_v;
        {
            std::string s = "AI_ANTICIPATION:";
            s += (ai_anticipation_enabled ? "on" : "off");
            s += " auto=";
            s += (ai_anticipation_auto_execute ? "1" : "0");
            s += " emit=";
            s += (ai_anticipation_emit_ui ? "1" : "0");
            emit_ui_line(s);
        }
        return;
    }

    // Deterministic anticipation route (predictive intent).
    // For non-command lines, optionally rewrite to a routed command.
    std::string routed_line = utf8_line;
    if (ai_anticipation_enabled) {
        std::string out_line;
        std::string ui_tag;
        const bool changed = ai_anticipator.route(this, utf8_line, out_line, ui_tag);
        if (changed) {
            if (ai_anticipation_emit_ui && !ui_tag.empty()) emit_ui_line(ui_tag);
            if (ai_anticipation_auto_execute) routed_line = out_line;
        }
    }

    if (!routed_line.empty() && routed_line[0] != '/') {
        if (ui_chat_q.size() >= UI_CHAT_CAP) ui_chat_q.pop_front();
        ui_chat_q.push_back(routed_line);
    }

    // UI text submission is a viewport observation. All downstream processing occurs in substrate.
    observe_text_line(routed_line);

    // Optional experiment compilation: user input can request a deterministic
    // opcode program which is submitted as operator packets.
    (void)compile_and_submit_experiment_from_text(routed_line);
}

bool SubstrateManager::compile_and_submit_experiment_from_text(const std::string& utf8_line) {
    EigenWare::EwExperimentRequest req;
    if (!EigenWare::ew_parse_experiment_request(utf8_line, req)) return false;

    return compile_and_submit_experiment(req);
}

bool SubstrateManager::compile_and_submit_experiment(const EigenWare::EwExperimentRequest& req) {
    std::vector<std::vector<uint8_t>> pkts;
    if (!ew_compile_experiment_to_operator_packets(req, pkts)) return false;
    for (size_t i = 0; i < pkts.size(); ++i) {
        const std::vector<uint8_t>& b = pkts[i];
        submit_operator_packet_v1(b.data(), b.size());
    }
    {
        std::string s = "EXPERIMENT_COMPILED:name=" + req.name +
                        " packets=" + std::to_string((uint32_t)pkts.size()) +
                        " micro=" + std::to_string(req.micro_ticks_u32);
        if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back(s);
    }
    return true;
}

void SubstrateManager::set_lattice_projection_tag(uint32_t slice_z_u32,
                                                  uint32_t stride_u32,
                                                  uint32_t max_points_u32,
                                                  uint32_t intensity_min_u8,
                                                  bool enabled) {
    set_lattice_projection_tag_ex(0u, slice_z_u32, stride_u32, max_points_u32, intensity_min_u8, enabled);
}

void SubstrateManager::set_lattice_projection_tag_ex(uint32_t lattice_sel_u32,
                                                     uint32_t slice_z_u32,
                                                     uint32_t stride_u32,
                                                     uint32_t max_points_u32,
                                                     uint32_t intensity_min_u8,
                                                     bool enabled) {
    // Single implementation point for all projection-tag callers.
    // Keep behavior identical regardless of which API surface is used.
    lattice_proj_tag_.enabled = enabled;
    lattice_proj_tag_.lattice_sel_u32 = lattice_sel_u32;
    lattice_proj_tag_.slice_z_u32 = slice_z_u32;
    lattice_proj_tag_.stride_u32 = (stride_u32 == 0) ? 1u : stride_u32;
    lattice_proj_tag_.max_points_u32 = (max_points_u32 == 0) ? 1u : max_points_u32;
    lattice_proj_tag_.intensity_min_u8 = intensity_min_u8;
}


uint32_t SubstrateManager::ui_create_chat_anchor_from_text(const std::string& utf8_line) {
#if defined(EW_ENABLE_UI_PARTITION) && (EW_ENABLE_UI_PARTITION==1)
    // Bounded ring buffer (drop oldest deterministically).
    if (ui_chat_q.size() >= UI_CHAT_CAP) ui_chat_q.pop_front();
    ui_chat_q.push_back(utf8_line);

    // Create a UI chat anchor as a dedicated partitioned anchor.
    Anchor a{};
    a.id = next_anchor_id_u32++;
    a.kind_u32 = EW_ANCHOR_KIND_DEFAULT;
    a.flags_u32 = (EW_ANCHOR_FLAG_UI_PARTITION | EW_ANCHOR_FLAG_CHAT_MESSAGE);
    a.context_id_u32 = 0u;
    a.crawler_id_u32 = 0u;
    a.object_id_u64 = (uint64_t)a.id;
    a.object_phase_seed_u64 = (uint64_t)a.id;

    // Start "low coherence": chi_q is the observable we already use for confidence gating.
    // Use a small, deterministic baseline rather than zero to avoid divide-by-zero behavior.
    a.chi_q = (TURN_SCALE / 256);
    a.theta_q = 0;
    a.m_q = 0;
    a.tau_q = 0;
    a.tau_turns_q = 0;
    a.curvature_q = 0;
    a.doppler_q = 0;
    a.last_theta_q = 0;
    a.last_f_code = 0;
    a.last_a_code = 0;
    a.last_v_code = 0;
    a.last_i_code = 0;
    for (uint32_t k = 0; k < Anchor::HARMONICS_N; ++k) a.harmonics_q15[k] = 0;
    a.harmonics_mean_q15 = 0;
    a.harmonics_epoch_u32 = 0;
    a.last_lnA_q32_32 = 0;
    a.last_lnF_q32_32 = 0;

    const uint32_t idx = (uint32_t)anchors.size();
    anchors.push_back(a);
    ancilla.push_back(ancilla_particle{});
    // Ensure lane count matches anchor count (Blueprint requirement).
    if (lanes.size() < anchors.size()) {
        EwQubitLane lane{};
        lanes.push_back(lane);
    }

    ui_anchor_indices.push_back(idx);
    ui_last_chat_anchor_idx_u32 = idx;

    // Feed the text into the crawler encoder path as a local observation so it becomes harmonics.
    // This keeps all "meaning extraction" GPU/substrate-side.
    crawler.enqueue_observation_utf8(
        /*artifact_id*/ (uint64_t)idx,
        /*target_anchor_id*/ idx,
        /*crawler_anchor_id*/ 0u,
        /*context_anchor_id*/ 0u,
        /*stream_id*/ 0u,
        /*extractor_id*/ 0u,
        /*trust_class*/ 1u,
        /*causal_tag*/ 0u,
        /*domain*/ "local",
        /*url*/ std::string("chat:") + std::to_string((uint64_t)idx),
        /*utf8*/ utf8_line
    );

    // Spike global coherence deterministically from user input (attention).
    // Trust for UI input is high but not absolute; keep bounded.
    global_coherence.spike_from_user_input(canonical_tick_u64(), (uint32_t)utf8_line.size(), (uint16_t)24576);

    return idx;
#else
    (void)utf8_line;
    return 0u;
#endif
}


bool SubstrateManager::corpus_allowlist_update_from_user_text(const std::string& allowlist_md_utf8) {
    // Deterministic validation: bounded size + strict parser.
    std::string s = allowlist_md_utf8;
    if (s.size() > (size_t)128 * 1024) s.resize((size_t)128 * 1024);

    GE_CorpusAllowlist tmp;
    if (!GE_load_corpus_allowlist_from_md_text(s, tmp)) {
        if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("ALLOWLIST_UPDATE_REJECTED");
        return false;
    }

    corpus_allowlist = tmp;
    corpus_allowlist_loaded = true;
    corpus_allowlist_user_md_utf8 = s;
    corpus_allowlist_user_loaded = true;
    domain_policies.build_from_allowlist(corpus_allowlist);

    // Persist to disk (deterministic relpath under Draft Container).
    {
        const std::string rel = "Draft Container/Corpus/allowlist_user.md";
        std::ofstream f(rel.c_str(), std::ios::binary);
        if (f.good()) {
            f.write(s.data(), (std::streamsize)s.size());
            f.flush();
        }
    }

    // Also persist as an inspector artifact for UI inspection/copy.
    EwInspectorArtifact out{};
    out.coord_coord9_u64 = ((uint64_t)canonical_tick << 1) ^ 0x414C4C57U; // 'ALLW'
    out.kind_u32 = EW_ARTIFACT_TEXT;
    out.rel_path = "Draft Container/Corpus/allowlist_user.md";
    out.producer_tick_u64 = canonical_tick;
    out.payload = s;
    inspector_fields.upsert(out);

    if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("ALLOWLIST_UPDATE_OK");
    return true;
}


bool SubstrateManager::corpus_allowlist_load_user_file_if_present() {
    const std::string rel = "Draft Container/Corpus/allowlist_user.md";
    std::ifstream f(rel.c_str(), std::ios::binary);
    if (!f.good()) return false;
    std::string s;
    f.seekg(0, std::ios::end);
    std::streampos n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (n > (std::streampos)(128 * 1024)) n = (std::streampos)(128 * 1024);
    s.resize((size_t)n);
    if (n > 0) f.read(&s[0], n);
    if (s.empty()) return false;
    return corpus_allowlist_update_from_user_text(s);
}


static inline bool ew_write_i64(std::ostream& out, int64_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    return out.good();
}
static inline bool ew_read_i64(std::istream& in, int64_t& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return in.good();
}

bool SubstrateManager::sim_save_to_file(const std::string& name_ascii) {
    // Snapshot current state (same struct used by candidate/commit path).
    EwState st;
    st.canonical_tick = canonical_tick;
    st.reservoir = reservoir;
    st.boundary_scale_q32_32 = boundary_scale_q32_32;
    st.anchors = anchors;
    st.ancilla = ancilla;
    st.lanes = lanes;
    st.object_store = object_store;
    st.nbody_state = nbody_state;
    st.materials_calib_done = materials_calib_done;
    sim_snapshot = st;
    sim_snapshot_valid = true;

    // Persist.
    std::string nm = name_ascii;
    if (nm.empty()) nm = "default";
    for (size_t i = 0; i < nm.size(); ++i) {
        const unsigned char b = (unsigned char)nm[i];
        const bool ok = (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_' || b == '-';
        if (!ok) nm[i] = '_';
    }
    const std::string rel = std::string("Draft Container/Sim/sim_") + nm + ".bin";

    // Ensure directory exists (best-effort; on failure we still keep in-memory snapshot).
    (void)std::system("mkdir -p \"Draft Container/Sim\" 2>nul");

    std::ofstream out(rel.c_str(), std::ios::binary);
    if (!out.good()) {
        emit_ui_line("SIM_SAVE_FAIL");
        return false;
    }
    const uint32_t magic = 0x47455349U; // 'GESI'
    const uint32_t ver = 13u;
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&ver), sizeof(ver));
    if (!out.good()) return false;
    // Core scalars
    out.write(reinterpret_cast<const char*>(&st.canonical_tick), sizeof(st.canonical_tick));
    if (!ew_write_i64(out, st.reservoir)) return false;
    if (!ew_write_i64(out, st.boundary_scale_q32_32)) return false;
    const uint32_t mc = st.materials_calib_done ? 1u : 0u;
    out.write(reinterpret_cast<const char*>(&mc), sizeof(mc));


// N-body state (fixed-size, strict)
out.write(reinterpret_cast<const char*>(&st.nbody_state.enabled_u32), sizeof(st.nbody_state.enabled_u32));
out.write(reinterpret_cast<const char*>(&st.nbody_state.initialized_u32), sizeof(st.nbody_state.initialized_u32));
out.write(reinterpret_cast<const char*>(&st.nbody_state.body_count_u32), sizeof(st.nbody_state.body_count_u32));
out.write(reinterpret_cast<const char*>(&st.nbody_state.pad_u32), sizeof(st.nbody_state.pad_u32));
if (!ew_write_i64(out, st.nbody_state.G_q32_32)) return false;
if (!ew_write_i64(out, st.nbody_state.dt_seconds_q32_32)) return false;
out.write(reinterpret_cast<const char*>(&st.nbody_state.bodies[0]), (std::streamsize)(sizeof(st.nbody_state.bodies)));
if (!out.good()) return false;

    // Anchors
    uint32_t na = (uint32_t)st.anchors.size();
    out.write(reinterpret_cast<const char*>(&na), sizeof(na));
    for (uint32_t i = 0; i < na; ++i) {
        const Anchor& a = st.anchors[i];
        // Serialize a stable subset + neighbors.
        out.write(reinterpret_cast<const char*>(&a.id), sizeof(a.id));
        out.write(reinterpret_cast<const char*>(&a.kind_u32), sizeof(a.kind_u32));
        out.write(reinterpret_cast<const char*>(&a.flags_u32), sizeof(a.flags_u32));
        out.write(reinterpret_cast<const char*>(&a.context_id_u32), sizeof(a.context_id_u32));
        out.write(reinterpret_cast<const char*>(&a.crawler_id_u32), sizeof(a.crawler_id_u32));
        out.write(reinterpret_cast<const char*>(&a.object_id_u64), sizeof(a.object_id_u64));
        out.write(reinterpret_cast<const char*>(&a.object_phase_seed_u64), sizeof(a.object_phase_seed_u64));
        out.write(reinterpret_cast<const char*>(&a.theta_q), sizeof(a.theta_q));
        out.write(reinterpret_cast<const char*>(&a.chi_q), sizeof(a.chi_q));
        out.write(reinterpret_cast<const char*>(&a.m_q), sizeof(a.m_q));
        out.write(reinterpret_cast<const char*>(&a.tau_q), sizeof(a.tau_q));
        out.write(reinterpret_cast<const char*>(&a.curvature_q), sizeof(a.curvature_q));
        out.write(reinterpret_cast<const char*>(&a.doppler_q), sizeof(a.doppler_q));
        out.write(reinterpret_cast<const char*>(&a.world_flux_grad_mean_q15), sizeof(a.world_flux_grad_mean_q15));
        out.write(reinterpret_cast<const char*>(&a.harmonics_mean_q15), sizeof(a.harmonics_mean_q15));
        out.write(reinterpret_cast<const char*>(&a.harmonics_epoch_u32), sizeof(a.harmonics_epoch_u32));
        out.write(reinterpret_cast<const char*>(&a.last_lnA_q32_32), sizeof(a.last_lnA_q32_32));
        out.write(reinterpret_cast<const char*>(&a.last_lnF_q32_32), sizeof(a.last_lnF_q32_32));
        // Harmonics
        for (uint32_t k = 0; k < Anchor::HARMONICS_N; ++k) {
            out.write(reinterpret_cast<const char*>(&a.harmonics_q15[k]), sizeof(a.harmonics_q15[k]));
        }
        // Neighbors
        uint32_t nn = (uint32_t)a.neighbors.size();
        if (nn > 1024u) nn = 1024u;
        out.write(reinterpret_cast<const char*>(&nn), sizeof(nn));
        for (uint32_t k = 0; k < nn; ++k) {
            const uint32_t nid = a.neighbors[k];
            out.write(reinterpret_cast<const char*>(&nid), sizeof(nid));
        }
        if (!out.good()) return false;

        // Kind-specific payload (strict; required for deterministic rehydration).
        // Format: payload_kind_u32, payload_size_u32, payload_bytes.
        const uint32_t pk = a.kind_u32;
        out.write(reinterpret_cast<const char*>(&pk), sizeof(pk));
        uint32_t psz = 0u;
        if (a.kind_u32 == EW_ANCHOR_KIND_CAMERA) psz = (uint32_t)sizeof(a.camera_state);
        else if (a.kind_u32 == EW_ANCHOR_KIND_OBJECT) psz = (uint32_t)sizeof(a.object_state);
        else if (a.kind_u32 == EW_ANCHOR_KIND_PLANET) psz = (uint32_t)sizeof(a.planet_state);
        else if (a.kind_u32 == EW_ANCHOR_KIND_EDITOR) psz = (uint32_t)sizeof(a.editor_state);
        else if (a.kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) psz = (uint32_t)sizeof(a.spectral_field_state);
        else if (a.kind_u32 == EW_ANCHOR_KIND_COHERENCE_BUS) psz = (uint32_t)sizeof(a.coherence_bus_state);
        else if (a.kind_u32 == EW_ANCHOR_KIND_VOXEL_COUPLING) psz = (uint32_t)sizeof(a.voxel_coupling_state);
        else if (a.kind_u32 == EW_ANCHOR_KIND_COLLISION_ENV) psz = (uint32_t)sizeof(a.collision_env_state);
        out.write(reinterpret_cast<const char*>(&psz), sizeof(psz));
        if (psz) {
            if (a.kind_u32 == EW_ANCHOR_KIND_CAMERA) out.write(reinterpret_cast<const char*>(&a.camera_state), (std::streamsize)psz);
            else if (a.kind_u32 == EW_ANCHOR_KIND_OBJECT) out.write(reinterpret_cast<const char*>(&a.object_state), (std::streamsize)psz);
            else if (a.kind_u32 == EW_ANCHOR_KIND_PLANET) out.write(reinterpret_cast<const char*>(&a.planet_state), (std::streamsize)psz);
            else if (a.kind_u32 == EW_ANCHOR_KIND_EDITOR) out.write(reinterpret_cast<const char*>(&a.editor_state), (std::streamsize)psz);
            else if (a.kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) out.write(reinterpret_cast<const char*>(&a.spectral_field_state), (std::streamsize)psz);
            else if (a.kind_u32 == EW_ANCHOR_KIND_COHERENCE_BUS) out.write(reinterpret_cast<const char*>(&a.coherence_bus_state), (std::streamsize)psz);
            else if (a.kind_u32 == EW_ANCHOR_KIND_VOXEL_COUPLING) out.write(reinterpret_cast<const char*>(&a.voxel_coupling_state), (std::streamsize)psz);
            else if (a.kind_u32 == EW_ANCHOR_KIND_COLLISION_ENV) out.write(reinterpret_cast<const char*>(&a.collision_env_state), (std::streamsize)psz);
        }
        if (!out.good()) return false;
    }

    // Ancilla particles
    uint32_t np = (uint32_t)st.ancilla.size();
    out.write(reinterpret_cast<const char*>(&np), sizeof(np));
    if (np) {
        out.write(reinterpret_cast<const char*>(st.ancilla.data()), (std::streamsize)(np * sizeof(ancilla_particle)));
        if (!out.good()) return false;
    }

    // Lanes
    uint32_t nl = (uint32_t)st.lanes.size();
    out.write(reinterpret_cast<const char*>(&nl), sizeof(nl));
    if (nl) {
        out.write(reinterpret_cast<const char*>(st.lanes.data()), (std::streamsize)(nl * sizeof(EwQubitLane)));
        if (!out.good()) return false;
    }

    // Object store
    if (!st.object_store.serialize_binary(out)) return false;

    emit_ui_line(std::string("SIM_SAVED ") + rel);
    return true;
}

bool SubstrateManager::sim_load_from_file(const std::string& name_ascii, bool play_loop) {
    std::string nm = name_ascii;
    if (nm.empty()) nm = "default";
    for (size_t i = 0; i < nm.size(); ++i) {
        const unsigned char b = (unsigned char)nm[i];
        const bool ok = (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_' || b == '-';
        if (!ok) nm[i] = '_';
    }
    const std::string rel = std::string("Draft Container/Sim/sim_") + nm + ".bin";
    std::ifstream in(rel.c_str(), std::ios::binary);
    if (!in.good()) {
        emit_ui_line("SIM_LOAD_NOFILE");
        return false;
    }
    uint32_t magic = 0, ver = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&ver), sizeof(ver));
    if (!in.good() || magic != 0x47455349U || ver != 13u) {
        emit_ui_line("SIM_LOAD_BAD_HEADER");
        return false;
    }
    EwState st;
    in.read(reinterpret_cast<char*>(&st.canonical_tick), sizeof(st.canonical_tick));
    if (!ew_read_i64(in, st.reservoir)) return false;
    if (!ew_read_i64(in, st.boundary_scale_q32_32)) return false;
    uint32_t mc = 0;
    in.read(reinterpret_cast<char*>(&mc), sizeof(mc));
    st.materials_calib_done = (mc != 0u);


// N-body state (fixed-size, strict)
in.read(reinterpret_cast<char*>(&st.nbody_state.enabled_u32), sizeof(st.nbody_state.enabled_u32));
in.read(reinterpret_cast<char*>(&st.nbody_state.initialized_u32), sizeof(st.nbody_state.initialized_u32));
in.read(reinterpret_cast<char*>(&st.nbody_state.body_count_u32), sizeof(st.nbody_state.body_count_u32));
in.read(reinterpret_cast<char*>(&st.nbody_state.pad_u32), sizeof(st.nbody_state.pad_u32));
if (!ew_read_i64(in, st.nbody_state.G_q32_32)) return false;
if (!ew_read_i64(in, st.nbody_state.dt_seconds_q32_32)) return false;
in.read(reinterpret_cast<char*>(&st.nbody_state.bodies[0]), (std::streamsize)(sizeof(st.nbody_state.bodies)));
if (!in.good()) return false;

    uint32_t na = 0;
    in.read(reinterpret_cast<char*>(&na), sizeof(na));
    if (na > 200000u) return false;
    st.anchors.resize(na);
    for (uint32_t i = 0; i < na; ++i) {
        Anchor a{};
        in.read(reinterpret_cast<char*>(&a.id), sizeof(a.id));
        in.read(reinterpret_cast<char*>(&a.kind_u32), sizeof(a.kind_u32));
        in.read(reinterpret_cast<char*>(&a.flags_u32), sizeof(a.flags_u32));
        in.read(reinterpret_cast<char*>(&a.context_id_u32), sizeof(a.context_id_u32));
        in.read(reinterpret_cast<char*>(&a.crawler_id_u32), sizeof(a.crawler_id_u32));
        in.read(reinterpret_cast<char*>(&a.object_id_u64), sizeof(a.object_id_u64));
        in.read(reinterpret_cast<char*>(&a.object_phase_seed_u64), sizeof(a.object_phase_seed_u64));
        in.read(reinterpret_cast<char*>(&a.theta_q), sizeof(a.theta_q));
        in.read(reinterpret_cast<char*>(&a.chi_q), sizeof(a.chi_q));
        in.read(reinterpret_cast<char*>(&a.m_q), sizeof(a.m_q));
        in.read(reinterpret_cast<char*>(&a.tau_q), sizeof(a.tau_q));
        in.read(reinterpret_cast<char*>(&a.curvature_q), sizeof(a.curvature_q));
        in.read(reinterpret_cast<char*>(&a.doppler_q), sizeof(a.doppler_q));
        in.read(reinterpret_cast<char*>(&a.world_flux_grad_mean_q15), sizeof(a.world_flux_grad_mean_q15));
        in.read(reinterpret_cast<char*>(&a.harmonics_mean_q15), sizeof(a.harmonics_mean_q15));
        in.read(reinterpret_cast<char*>(&a.harmonics_epoch_u32), sizeof(a.harmonics_epoch_u32));
        in.read(reinterpret_cast<char*>(&a.last_lnA_q32_32), sizeof(a.last_lnA_q32_32));
        in.read(reinterpret_cast<char*>(&a.last_lnF_q32_32), sizeof(a.last_lnF_q32_32));
        for (uint32_t k = 0; k < Anchor::HARMONICS_N; ++k) {
            in.read(reinterpret_cast<char*>(&a.harmonics_q15[k]), sizeof(a.harmonics_q15[k]));
        }
        uint32_t nn = 0;
        in.read(reinterpret_cast<char*>(&nn), sizeof(nn));
        if (nn > 1024u) return false;
        a.neighbors.resize(nn);
        for (uint32_t k = 0; k < nn; ++k) {
            uint32_t nid = 0;
            in.read(reinterpret_cast<char*>(&nid), sizeof(nid));
            a.neighbors[k] = nid;
        }
        if (!in.good()) return false;

        // Kind-specific payload (strict).
        uint32_t pk = 0u, psz = 0u;
        in.read(reinterpret_cast<char*>(&pk), sizeof(pk));
        in.read(reinterpret_cast<char*>(&psz), sizeof(psz));
        if (!in.good()) return false;
        if (pk != a.kind_u32) return false;
        if (psz > (1024u * 1024u)) return false;
        if (psz) {
            if (a.kind_u32 == EW_ANCHOR_KIND_CAMERA) {
                if (psz != (uint32_t)sizeof(a.camera_state)) return false;
                in.read(reinterpret_cast<char*>(&a.camera_state), (std::streamsize)psz);
            } else if (a.kind_u32 == EW_ANCHOR_KIND_OBJECT) {
                if (psz != (uint32_t)sizeof(a.object_state)) return false;
                in.read(reinterpret_cast<char*>(&a.object_state), (std::streamsize)psz);
            } else if (a.kind_u32 == EW_ANCHOR_KIND_PLANET) {
                if (psz != (uint32_t)sizeof(a.planet_state)) return false;
                in.read(reinterpret_cast<char*>(&a.planet_state), (std::streamsize)psz);
            } else if (a.kind_u32 == EW_ANCHOR_KIND_EDITOR) {
                if (psz != (uint32_t)sizeof(a.editor_state)) return false;
                in.read(reinterpret_cast<char*>(&a.editor_state), (std::streamsize)psz);
            } else if (a.kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) {
                if (psz != (uint32_t)sizeof(a.spectral_field_state)) return false;
                in.read(reinterpret_cast<char*>(&a.spectral_field_state), (std::streamsize)psz);
            } else if (a.kind_u32 == EW_ANCHOR_KIND_COHERENCE_BUS) {
                if (psz != (uint32_t)sizeof(a.coherence_bus_state)) return false;
                in.read(reinterpret_cast<char*>(&a.coherence_bus_state), (std::streamsize)psz);
            } else if (a.kind_u32 == EW_ANCHOR_KIND_VOXEL_COUPLING) {
                if (psz != (uint32_t)sizeof(a.voxel_coupling_state)) return false;
                in.read(reinterpret_cast<char*>(&a.voxel_coupling_state), (std::streamsize)psz);
            } else if (a.kind_u32 == EW_ANCHOR_KIND_COLLISION_ENV) {
                if (psz != (uint32_t)sizeof(a.collision_env_state)) return false;
                in.read(reinterpret_cast<char*>(&a.collision_env_state), (std::streamsize)psz);
            } else {
                // Unknown kind with nonzero payload is rejected (fail closed).
                return false;
            }
            if (!in.good()) return false;
        }

        a.sync_basis9_from_core();
        st.anchors[i] = a;
    }

    uint32_t np = 0;
    in.read(reinterpret_cast<char*>(&np), sizeof(np));
    if (np > 200000u) return false;
    st.ancilla.resize(np);
    if (np) {
        in.read(reinterpret_cast<char*>(st.ancilla.data()), (std::streamsize)(np * sizeof(ancilla_particle)));
        if (!in.good()) return false;
    }

    uint32_t nl = 0;
    in.read(reinterpret_cast<char*>(&nl), sizeof(nl));
    if (nl > 200000u) return false;
    st.lanes.resize(nl);
    if (nl) {
        in.read(reinterpret_cast<char*>(st.lanes.data()), (std::streamsize)(nl * sizeof(EwQubitLane)));
        if (!in.good()) return false;
    }

    if (!st.object_store.deserialize_binary(in)) {
        emit_ui_line("SIM_LOAD_OBJECT_STORE_FAIL");
        return false;
    }

    // Apply to live runtime.
    reservoir = st.reservoir;
    boundary_scale_q32_32 = st.boundary_scale_q32_32;
    anchors = st.anchors;
    ancilla = st.ancilla;
    lanes = st.lanes;
    object_store = st.object_store;
    materials_calib_done = st.materials_calib_done;
    nbody_state = st.nbody_state;
    // Ensure topology vectors are sized.
    const uint32_t need = (uint32_t)anchors.size();
    if (redirect_to.size() < need) redirect_to.resize(need, 0u);
    if (split_child_a.size() < need) split_child_a.resize(need, 0u);
    if (split_child_b.size() < need) split_child_b.resize(need, 0u);
    next_anchor_id_u32 = (uint32_t)anchors.size();
    ge_rebuild_cached_anchor_ids(*this);

    // Snapshot for loop playback.
    sim_snapshot = st;
    sim_snapshot_valid = true;
    sim_play_loop_enabled = play_loop;
    sim_play_loop_start_tick_u64 = canonical_tick_u64();
    ai_substrate_drive.clear();
    ai_gpu_spider_encode_count_u64 = 0u;
    ai_api_decode_count_u64 = 0u;
    ai_last_api_decode_request_id_u64 = 0u;
    ai_last_api_decode_http_status_s32 = -1;
    ai_last_api_decode_body_bytes_u32 = 0u;

    emit_ui_line(play_loop ? "SIM_PLAY_LOOP" : "SIM_PLAY_LIVE");
    return true;
}


uint32_t SubstrateManager::ui_create_anchor_from_chat_seed(uint32_t chat_anchor_idx_u32, uint16_t resonance_q15) {
#if defined(EW_ENABLE_UI_PARTITION) && (EW_ENABLE_UI_PARTITION==1)
    if (chat_anchor_idx_u32 >= anchors.size()) return 0u;

    Anchor a{};
    a.id = next_anchor_id_u32++;
    a.kind_u32 = EW_ANCHOR_KIND_CONTEXT_ROOT;
    a.flags_u32 = EW_ANCHOR_FLAG_UI_PARTITION;
    a.context_id_u32 = anchors[chat_anchor_idx_u32].id;
    a.crawler_id_u32 = 0u;
    a.object_id_u64 = (uint64_t)a.id;
    a.object_phase_seed_u64 = (uint64_t)a.id;

    // Start at low coherence, then bump based on resonance.
    // resonance_q15 is [0..32768].
    const int64_t base = (TURN_SCALE / 256);
    const int64_t bump = (int64_t)(((__int128)TURN_SCALE * (int64_t)resonance_q15) / (int64_t)32768 / 64); // max ~TURN/64
    a.chi_q = base + bump;
    a.theta_q = 0;
    a.m_q = 0;
    a.tau_q = 0;
    a.tau_turns_q = 0;
    a.curvature_q = 0;
    a.doppler_q = 0;
    a.last_theta_q = 0;
    a.last_f_code = 0;
    a.last_a_code = 0;
    a.last_v_code = 0;
    a.last_i_code = 0;
    for (uint32_t k = 0; k < Anchor::HARMONICS_N; ++k) a.harmonics_q15[k] = 0;
    a.harmonics_mean_q15 = 0;
    a.harmonics_epoch_u32 = 0;
    a.last_lnA_q32_32 = 0;
    a.last_lnF_q32_32 = 0;

    const uint32_t idx = (uint32_t)anchors.size();
    anchors.push_back(a);
    ancilla.push_back(ancilla_particle{});
    if (lanes.size() < anchors.size()) {
        EwQubitLane lane{};
        lanes.push_back(lane);
    }

    ui_anchor_indices.push_back(idx);
    // Target subsequent live crawl observations to this new training anchor.
    ui_livecrawl_target_anchor_idx_u32 = idx;

    // Also seed a plan packet so automation can chain immediately.
    genesis::AutoArtifact plan{};
    plan.kind = genesis::AutoArtifactKind::PlanPacket;
    plan.created_tick_u64 = canonical_tick_u64();
    plan.lane_u32 = 0u;
    plan.payload_utf8 = "{\"plan\":\"ui_seed\",\"source\":\"chat\",\"tol_percent\":6}";
    learning_automation.bus().push(plan);

    return idx;
#else
    (void)chat_anchor_idx_u32; (void)resonance_q15;
    return 0u;
#endif
}


void SubstrateManager::ui_maybe_enqueue_crawl_seeds_from_text(const std::string& utf8_line) {
    // Only act when live crawl is enabled and consent is granted.
    if (crawler_enable_live_u32 == 0u || crawler_live_consent_required_u32 != 0u) {
        emit_ui_line("AUTO_CRAWL_BLOCKED: live crawl disabled or consent required");
        return;
    }
    // Extract any explicit http://... tokens first.
    std::vector<std::string> toks;
    ew::ew_split_shell_ascii(utf8_line, toks);
    bool saw = false;
    for (const auto& t : toks) {
        if (t.rfind("http://", 0) == 0) {
            live_crawler.enqueue_url(t);
            saw = true;
        }
    }
    if (saw) return;

    // No explicit URL provided; deterministically choose one allowed domain root
    // from domain policies as a "topic-general" seed.
    // This keeps automation moving without guessing arbitrary external sites.
    uint64_t acc = 1469598103934665603ULL;
    for (size_t i = 0; i < utf8_line.size(); ++i) {
        acc ^= (uint8_t)utf8_line[i];
        acc *= 1099511628211ULL;
    }
    const size_t n = domain_policies.rows.size();
    if (n == 0) {
        emit_ui_line("AUTO_CRAWL_BLOCKED: no domain policies loaded");
        return;
    }
    const size_t start = (size_t)(acc % (uint64_t)n);
    for (size_t k = 0; k < n; ++k) {
        const size_t idx = (start + k) % n;
        const auto& r = domain_policies.rows[idx];
        if (r.offline_only) continue;
        if (!r.allow_http) continue;
        live_crawler.enqueue_url(std::string("http://") + r.domain_ascii + "/");
        emit_ui_line(std::string("AUTO_CRAWL_SEED: domain=") + r.domain_ascii);
        return;
    }
    emit_ui_line("AUTO_CRAWL_BLOCKED: no http-allowed domains");
}


void SubstrateManager::emit_ui_line(const std::string& utf8_line) {
    if (ui_out_q.size() < UI_OUT_CAP) {
        ui_out_q.push_back(utf8_line);
    }
}

bool SubstrateManager::ui_pop_output_text(std::string& out_utf8) {
    if (ui_out_q.empty()) return false;
    out_utf8 = ui_out_q.front();
    ui_out_q.pop_front();
    return true;
}

bool SubstrateManager::export_object_bundle(uint64_t object_id_u64,
                                            const std::string& out_dir_utf8,
                                            std::string* out_report_utf8) const {
    auto rep = [&](const std::string& s){ if (out_report_utf8) { out_report_utf8->append(s); out_report_utf8->push_back('\n'); } };

    const EwObjectEntry* e = object_store.find(object_id_u64);
    if (!e) {
        rep("EXPORT:missing_object");
        return false;
    }

    EwVoxelVolumeView vv;
    if (!object_store.view_voxel_volume(object_id_u64, vv) || !vv.bytes || vv.byte_count == 0) {
        rep("EXPORT:missing_voxel_volume");
        return false;
    }

    // Ensure output directory exists.
    std::error_code ec;
    std::filesystem::create_directories(out_dir_utf8, ec);
    if (ec) {
        rep(std::string("EXPORT:mkdir_failed:") + ec.message());
        return false;
    }

    // Build standard shell mesh from voxels (auto-topologize).
    genesis::EwMeshV1 shell;
    if (!genesis::ge_build_shell_mesh_from_voxels(/*subdiv*/5u,
                                                  vv.grid_x_u32, vv.grid_y_u32, vv.grid_z_u32,
                                                  vv.bytes,
                                                  /*thr*/16u,
                                                  shell)) {
        rep("EXPORT:shell_build_failed");
        return false;
    }

    const std::string mesh_path = (std::filesystem::path(out_dir_utf8) / "mesh_shell.ewmesh").u8string();
    if (!genesis::ewmesh_write_v1(mesh_path, shell)) {
        rep("EXPORT:write_mesh_failed");
        return false;
    }

    // UV atlas
    uint32_t aw=0, ah=0, af=0;
    const uint8_t* atlas_bytes = nullptr;
    size_t atlas_n = 0;
    if (!object_store.view_uv_atlas(object_id_u64, aw, ah, af, atlas_bytes, atlas_n) || !atlas_bytes || atlas_n == 0) {
        rep("EXPORT:missing_uv_atlas");
        return false;
    }
    const std::string atlas_path = (std::filesystem::path(out_dir_utf8) / "uv_atlas.rgba8").u8string();
    {
        std::ofstream f(atlas_path, std::ios::binary);
        if (!f) { rep("EXPORT:write_uv_atlas_failed"); return false; }
        f.write((const char*)atlas_bytes, (std::streamsize)atlas_n);
    }

    // Also export a viewable diffuse texture derived from the UV atlas for universal formats.
    // Deterministic: uncompressed 32bpp TGA (BGRA) named uv_atlas.tga.
    const std::string atlas_tga_rel = "uv_atlas.tga";
    const std::string atlas_tga_path = (std::filesystem::path(out_dir_utf8) / atlas_tga_rel).u8string();
    {
        // TGA header.
        uint8_t hdr[18];
        std::memset(hdr, 0, sizeof(hdr));
        hdr[2] = 2; // uncompressed true-color
        hdr[12] = (uint8_t)(aw & 0xFF);
        hdr[13] = (uint8_t)((aw >> 8) & 0xFF);
        hdr[14] = (uint8_t)(ah & 0xFF);
        hdr[15] = (uint8_t)((ah >> 8) & 0xFF);
        hdr[16] = 32;
        hdr[17] = 8;

        std::ofstream f(atlas_tga_path, std::ios::binary | std::ios::trunc);
        if (!f) { rep("EXPORT:write_uv_tga_failed"); return false; }
        f.write((const char*)hdr, 18);
        // Convert RGBA->BGRA.
        for (size_t i = 0; i + 3 < atlas_n; i += 4) {
            uint8_t bgra[4];
            bgra[0] = atlas_bytes[i + 2];
            bgra[1] = atlas_bytes[i + 1];
            bgra[2] = atlas_bytes[i + 0];
            bgra[3] = atlas_bytes[i + 3];
            f.write((const char*)bgra, 4);
        }
        if (!f.good()) { rep("EXPORT:write_uv_tga_failed"); return false; }
    }

    // Universal export: write FBX using Assimp, using the shell mesh and atlas texture.
    // This makes exported assets portable while keeping the substrate-native UV atlas variables.
    const std::string fbx_path = (std::filesystem::path(out_dir_utf8) / "mesh_shell.fbx").u8string();
    {
        std::string repx;
        if (!genesis::assimp_export_ewmesh_v1_single_material(shell, fbx_path, "fbx", atlas_tga_rel, &repx)) {
            rep("EXPORT:fbx_export_failed");
            if (out_report_utf8) out_report_utf8->append(repx);
            return false;
        }
        if (out_report_utf8) out_report_utf8->append(repx);
    }
    const std::string atlas_meta_path = (std::filesystem::path(out_dir_utf8) / "uv_atlas.json").u8string();
    {
        std::ofstream f(atlas_meta_path, std::ios::binary);
        if (!f) { rep("EXPORT:write_uv_meta_failed"); return false; }
        f << "{\n";
        f << "  \"width\": " << aw << ",\n";
        f << "  \"height\": " << ah << ",\n";
        f << "  \"format\": \"RGBA8_UNORM\",\n";
        f << "  \"channels\": {\n";
        f << "    \"R\": \"density_occupancy_u8\",\n";
        f << "    \"G\": \"coherence_proxy_u8\",\n";
        f << "    \"B\": \"curvature_proxy_u8\",\n";
        f << "    \"A\": \"object_id_lo_u8\"\n";
        f << "  }\n";
        f << "}\n";
    }

    // Material animation: single keyframe (deterministic baseline).
    // When deformation clips are later recorded, this file extends to a sequence.
    const std::string mat_anim_path = (std::filesystem::path(out_dir_utf8) / "material_anim.json").u8string();
    {
        // Derive a stable emissive scalar from mean density.
        uint64_t sum = 0;
        for (size_t i = 0; i < atlas_n; i += 4) sum += atlas_bytes[i];
        const double mean = (atlas_n > 0) ? ((double)sum / (double)(atlas_n/4u)) : 0.0;
        const double emiss = mean / 255.0;
        std::ofstream f(mat_anim_path, std::ios::binary);
        if (!f) { rep("EXPORT:write_mat_anim_failed"); return false; }
        f << "{\n";
        f << "  \"object_id_u64\": " << (unsigned long long)object_id_u64 << ",\n";
        f << "  \"track\": [\n";
        f << "    { \"t\": 0.0, \"emissive\": " << emiss << " }\n";
        f << "  ]\n";
        f << "}\n";
    }

    const std::string manifest_path = (std::filesystem::path(out_dir_utf8) / "export_manifest.json").u8string();
    {
        std::ofstream f(manifest_path, std::ios::binary);
        if (!f) { rep("EXPORT:write_manifest_failed"); return false; }
        f << "{\n";
        f << "  \"object_id_u64\": " << (unsigned long long)object_id_u64 << ",\n";
        f << "  \"label_utf8\": \"";
        for (char c : e->label_utf8) {
            if (c == '"') f << "\\\"";
            else if (c == '\\') f << "\\\\";
            else if ((unsigned char)c < 0x20) f << "?";
            else f << c;
        }
        f << "\",\n";
        f << "  \"files\": {\n";
        f << "    \"mesh\": \"mesh_shell.ewmesh\",\n";
        f << "    \"mesh_fbx\": \"mesh_shell.fbx\",\n";
        f << "    \"uv_atlas\": \"uv_atlas.rgba8\",\n";
        f << "    \"uv_atlas_tga\": \"uv_atlas.tga\",\n";
        f << "    \"uv_atlas_meta\": \"uv_atlas.json\",\n";
        f << "    \"material_anim\": \"material_anim.json\"\n";
        f << "  }\n";
        f << "}\n";
    }

    rep("EXPORT:OK");
    return true;
}

static inline bool ew_is_domain_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || (c == '.') || (c == '-') ;
}

static bool ew_is_host_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '-' ;
}

static void ew_parse_allowlist_targets_deterministic(
    const std::string& md,
    std::vector<SubstrateManager::EwCorpusCrawlTarget>& out_targets)
{
    out_targets.clear();

    // Lane parsing: "## Lane N" -> lane_u32 = N
    uint32_t lane = 0;

    size_t i = 0;
    while (i < md.size()) {
        size_t j = md.find('\n', i);
        if (j == std::string::npos) j = md.size();
        std::string line = md.substr(i, j - i);

        // Update lane on headings.
        if (line.size() >= 7 && line[0] == '#' && line[1] == '#' ) {
            // Find "Lane"
            size_t k = line.find("Lane ");
            if (k != std::string::npos) {
                k += 5;
                while (k < line.size() && (line[k] == ' ' || line[k] == '	')) k++;
                uint32_t v = 0;
                bool any = false;
                while (k < line.size() && line[k] >= '0' && line[k] <= '9') {
                    any = true;
                    v = v * 10u + (uint32_t)(line[k] - '0');
                    k++;
                }
                if (any) lane = v;
            }
        }

        // Extract backtick literals; accept:
        //  - host (example.com)
        //  - https://host/path
        //  - host/path
        for (size_t k = 0; k < line.size(); ++k) {
            if (line[k] != '`') continue;
            size_t b = line.find('`', k + 1);
            if (b == std::string::npos) break;
            std::string tok = line.substr(k + 1, b - (k + 1));
            // strip spaces
            while (!tok.empty() && (tok[0] == ' ' || tok[0] == '	')) tok.erase(tok.begin());
            while (!tok.empty() && (tok.back() == ' ' || tok.back() == '	')) tok.pop_back();

            if (tok.empty()) { k = b; continue; }

            std::string host;
            std::string path;

            if (tok.rfind("https://", 0) == 0) {
                std::string rest = tok.substr(8);
                size_t slash = rest.find('/');
                if (slash == std::string::npos) { host = rest; path = ""; }
                else { host = rest.substr(0, slash); path = rest.substr(slash); }
            } else if (tok.find("http://") == 0) {
                // ignore insecure URL literals deterministically
                host.clear();
            } else {
                // host or host/path
                size_t slash = tok.find('/');
                if (slash == std::string::npos) { host = tok; path = ""; }
                else { host = tok.substr(0, slash); path = tok.substr(slash); }
            }

            if (!host.empty()) {
                // Validate host shape.
                bool ok = (host.find('.') != std::string::npos) && host.size() <= 128;
                for (size_t t = 0; t < host.size() && ok; ++t) ok = ew_is_host_char(host[t]);
                if (ok) {
                    SubstrateManager::EwCorpusCrawlTarget ct{};
                    ct.lane_u32 = lane;
                    ct.stage_u32 = 0;
                    ct.profile_u32 = 0;
                    ct.host_utf8 = host;
                    ct.path_utf8 = path;
                    out_targets.push_back(ct);
                }
            }
            k = b;
        }

        i = (j < md.size()) ? (j + 1) : md.size();
    }

    // Deduplicate deterministically by (lane, host, path).
    std::vector<SubstrateManager::EwCorpusCrawlTarget> uniq;
    uniq.reserve(out_targets.size());
    for (size_t k = 0; k < out_targets.size(); ++k) {
        const auto& x = out_targets[k];
        bool seen = false;
        for (size_t t = 0; t < uniq.size(); ++t) {
            if (uniq[t].lane_u32 == x.lane_u32 &&
                uniq[t].host_utf8 == x.host_utf8 &&
                uniq[t].path_utf8 == x.path_utf8) { seen = true; break; }
        }
        if (!seen) uniq.push_back(x);
    }
    out_targets.swap(uniq);
}

// Deterministically extract allowlist context lines for a host.
// Used to seed per-domain observed_topic_mask_u64 before the first fetch.
static std::string ew_allowlist_extract_host_context_utf8(
    const std::string& md,
    const std::string& host_utf8,
    const uint32_t max_lines = 6u)
{
    if (host_utf8.empty()) return std::string();
    std::string out;
    out.reserve(512);

    std::string last_lane;
    std::string last_section;
    uint32_t added = 0u;

    size_t i = 0;
    while (i < md.size()) {
        size_t j = md.find('\n', i);
        if (j == std::string::npos) j = md.size();
        const std::string line = md.substr(i, j - i);

        // Track headings for context.
        if (line.size() >= 2 && line[0] == '#' && line[1] == '#') {
            if (line.find("Lane") != std::string::npos) last_lane = line;
            else last_section = line;
        } else if (!line.empty() && line[0] != '-' && line[0] != ' ' && line[0] != '\t' && line[0] != '`') {
            if (line.size() <= 96) last_section = line;
        }

        if (line.find(host_utf8) != std::string::npos) {
            if (!last_lane.empty()) {
                out.append(last_lane);
                out.push_back('\n');
                last_lane.clear();
            }
            if (!last_section.empty()) {
                out.append(last_section);
                out.push_back('\n');
                last_section.clear();
            }
            out.append(line);
            out.push_back('\n');
            added += 1u;
            if (added >= max_lines) break;
        }

        i = (j < md.size()) ? (j + 1) : md.size();
    }
    return out;
}

void SubstrateManager::corpus_crawl_start_from_allowlist_text(const std::string& allowlist_md_utf8) {
    // Parse and store allowlist for admission filtering.
    corpus_allowlist_loaded = GE_load_corpus_allowlist_from_md_text(allowlist_md_utf8, corpus_allowlist);
    domain_policies.build_from_allowlist(corpus_allowlist);
    rate_limiter.configure_from_policies(domain_policies);
    // Seed live crawler roots deterministically (disabled unless enabled+consent).
    live_crawler.seed_default_roots(domain_policies);


    // Session 0: general allowlist crawl.
    for (uint32_t si = 0; si < EW_CRAWL_SESSION_MAX; ++si) {
        crawl_sessions[si].active = false;
        crawl_sessions[si].q.clear();
        crawl_sessions[si].domain_map.clear();
        crawl_sessions[si].context_anchor_id_u32 = 0;
        crawl_sessions[si].syllabus_anchor_id_u32 = 0;
        crawl_sessions[si].crawler_anchor_id_u32 = 0;
        crawl_sessions[si].profile_u32 = 0;
    }

    auto ensure_anchor_capacity = [&](uint32_t need_u32) {
        while (anchors.size() < need_u32) anchors.emplace_back((uint32_t)anchors.size());
        while (ancilla.size() < need_u32) ancilla.push_back(ancilla_particle{});
        if (redirect_to.size() < need_u32) redirect_to.resize(need_u32, 0u);
        if (split_child_a.size() < need_u32) split_child_a.resize(need_u32, 0u);
        if (split_child_b.size() < need_u32) split_child_b.resize(need_u32, 0u);
    };
    auto alloc_anchor_id = [&]()->uint32_t {
        const uint32_t id = next_anchor_id_u32;
        next_anchor_id_u32 += 1u;
        ensure_anchor_capacity(next_anchor_id_u32 + 1u);
        return id;
    };

    std::vector<SubstrateManager::EwCorpusCrawlTarget> parsed;
    ew_parse_allowlist_targets_deterministic(allowlist_md_utf8, parsed);

    // Build session 0 domain list: use host-only literals; ignore literals with embedded paths.
    std::vector<std::string> hosts;
    hosts.reserve(parsed.size());
    for (size_t i = 0; i < parsed.size(); ++i) {
        if (!parsed[i].host_utf8.empty()) {
            bool seen = false;
            for (size_t j = 0; j < hosts.size(); ++j) { if (hosts[j] == parsed[i].host_utf8) { seen = true; break; } }
            if (!seen) hosts.push_back(parsed[i].host_utf8);
        }
    }

    // Stage-0 policy: always include Khan Academy math in the crawl plan so
    // math learning can run in parallel with language.
    // This does not bypass allowlist enforcement for other domains; it is an
    // explicit canonical addendum for the Stage-0 curriculum.
    {
        const std::string kh = "www.khanacademy.org";
        bool seen = false;
        for (size_t j = 0; j < hosts.size(); ++j) { if (hosts[j] == kh) { seen = true; break; } }
        if (!seen) hosts.push_back(kh);
    }

    // Create a context + syllabus + crawler anchor set inside substrate storage.
    const uint32_t ctx_id = alloc_anchor_id();
    const uint32_t syl_id = alloc_anchor_id();
    const uint32_t crawl_id = alloc_anchor_id();

    anchors[ctx_id].kind_u32 = EW_ANCHOR_KIND_CONTEXT_ROOT;
    anchors[ctx_id].context_id_u32 = ctx_id;

    anchors[syl_id].kind_u32 = EW_ANCHOR_KIND_SYLLABUS_ROOT;
    anchors[syl_id].context_id_u32 = ctx_id;

    anchors[crawl_id].kind_u32 = EW_ANCHOR_KIND_CRAWLER_ROOT;
    anchors[crawl_id].context_id_u32 = ctx_id;
    anchors[crawl_id].crawler_id_u32 = crawl_id;

    // Link topology deterministically (context <-> syllabus <-> crawler).
    anchors[ctx_id].neighbors.push_back(syl_id);
    anchors[syl_id].neighbors.push_back(ctx_id);
    anchors[syl_id].neighbors.push_back(crawl_id);
    anchors[crawl_id].neighbors.push_back(syl_id);

    EwCrawlSession& ss = crawl_sessions[0];
    ss.active = true;
    ss.profile_u32 = 0;
    ss.context_anchor_id_u32 = ctx_id;
    ss.syllabus_anchor_id_u32 = syl_id;
    ss.crawler_anchor_id_u32 = crawl_id;

    // Domain anchors and per-domain queue.
    for (size_t k = 0; k < hosts.size(); ++k) {
        const std::string& host = hosts[k];
        const uint32_t dom_id = alloc_anchor_id();

        anchors[dom_id].kind_u32 = EW_ANCHOR_KIND_DOMAIN_ROOT;
        anchors[dom_id].context_id_u32 = ctx_id;
        anchors[dom_id].crawler_id_u32 = crawl_id;

        // Link: crawler <-> domain
        anchors[crawl_id].neighbors.push_back(dom_id);
        anchors[dom_id].neighbors.push_back(crawl_id);

        EwDomainAnchorMapEntry me{};
        me.domain_utf8 = host;
        me.domain_anchor_id_u32 = dom_id;
        me.next_allowed_tick_u64 = 0;
        me.tokens_q16_16 = (1u << 16);
        // Seed observed topic mask from allowlist text itself, so scheduling can
        // prioritize domains likely to satisfy missing stage checkpoints before
        // any network fetch completes.
        {
            const std::string ctx = ew_allowlist_extract_host_context_utf8(allowlist_md_utf8, host, 6u);
            if (!ctx.empty()) {
                EwCrawlerPageSummary ps{};
                // Small chunk size is fine here (lines are short); GPU computes mask.
                if (ew_encode_page_summary_cuda((const uint8_t*)ctx.data(), ctx.size(), 4096u, &ps)) {
                    me.observed_topic_mask_u64 |= ps.metric_mask_u64;
                }
            }
        }
        ss.domain_map.push_back(me);

        SubstrateManager::EwCorpusCrawlTarget t{};
        t.lane_u32 = 0;
        t.stage_u32 = 0;
        t.profile_u32 = 0;
        t.host_utf8 = host;
        t.path_utf8.clear();
        ss.q.push_back(t);

        // Khan Academy math: seed an explicit entry path so the crawler
        // reaches math pages without needing a sitemap first.
        if (host == "www.khanacademy.org") {
            SubstrateManager::EwCorpusCrawlTarget t2{};
            t2.lane_u32 = 2u; // Lane 2 = Math
            t2.stage_u32 = 1u;
            t2.profile_u32 = 0u;
            t2.host_utf8 = host;
            t2.path_utf8 = "/math";
            ss.q.push_back(t2);
        }
    }

    std::string msg = "CRAWL_SCHEDULED ";
    msg += std::to_string((unsigned long long)ss.q.size());
    msg += " CRAWLER_ANCHOR ";
    msg += std::to_string((unsigned long long)ss.crawler_anchor_id_u32);
    if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back(msg);
}


// Embedded Neuralis corpus allowlist (markdown). Used for one-button crawl start.
static const char* EW_NEURALIS_CORPUS_ALLOWLIST_MD_UTF8 = R"EW_ALLOWLISTX(
# Neuralis Corpus Domains — Expanded Allowlist

This is a curated, high-signal domain list for Neuralis encoder ingestion, organized by category and context lane.

Safety + compliance defaults:
- Prefer open-licensed / public-domain / open-access sources.
- Respect robots.txt, Terms of Service, rate limits, and attribution requirements.
- Do not test any “AI ↔ Internet” integration without explicit human consent (per governance rules). This list is only “where to ingest from,” not permission to connect live.

---

## Lane 0 — Training-grade meta corpora (widely used building blocks)

- Common Crawl — `commoncrawl.org` (web crawl corpus; huge, heterogeneous; you still need allowlists/filters)  
  Source: https://commoncrawl.org/
- Wikipedia / Wikidata / Wiktionary dumps — `dumps.wikimedia.org` (bulk exports for offline ingestion)  
  Source: https://dumps.wikimedia.org/
- OpenAlex (CC0) — `openalex.org` (open scholarly index; CC0 per docs)  
  Sources: https://openalex.org/ | https://docs.openalex.org/
- Crossref metadata — `crossref.org` (DOI metadata; Crossref says metadata is open for reuse)  
  Source: https://www.crossref.org/services/metadata-retrieval/
- Zenodo — `zenodo.org` (open repository for research outputs)  
  Source: https://zenodo.org/
- Stack Exchange data dumps (Archive.org mirror) — `archive.org`  
  Source: https://archive.org/details/stackexchange
- Hugging Face datasets (e.g., FineWeb) — `huggingface.co` (check dataset cards/licenses)  
  Source: https://huggingface.co/datasets/HuggingFaceFW/fineweb

---

## Lane 1 — Dictionaries, encyclopedias, thesauruses

Encyclopedias & general knowledge:
- `wikipedia.org`
- `wikidata.org`
- `commons.wikimedia.org`
- `wikibooks.org`
- `wikiversity.org`
- `wikisource.org`

Dictionaries / lexicons:
- `wiktionary.org`
- `wordnet.princeton.edu`

Offline exports:
- `dumps.wikimedia.org`

---

## Lane 2 — Math + core STEM education (includes Khan Academy)

Platforms / courseware:
- Khan Academy — `khanacademy.org`  
  Source (brand/material usage guidance): https://support.khanacademy.org/hc/en-us/articles/202263034-What-is-Khan-Academy-s-Trademark-and-Brand-Usage-Policy
- MIT OpenCourseWare — `ocw.mit.edu`
- OpenStax — `openstax.org`

Reference math:
- NIST DLMF — `dlmf.nist.gov`
- OEIS — `oeis.org`

YouTube (college/institution profile channels):
- MIT OpenCourseWare — https://www.youtube.com/@mitocw
- Stanford Online — https://www.youtube.com/stanfordonline

---

## Lane 3 — Physics & quantum physics

High-energy / particle physics:
- CERN Open Data — `opendata.cern.ch`  
  Source: https://opendata.cern.ch/
- INSPIRE-HEP — `inspirehep.net`  
  Source: https://inspirehep.net/
- ATLAS Open Data — `opendata.atlas.cern`  
  Source: https://opendata.atlas.cern/
- arXiv — `arxiv.org`  
  Source: https://arxiv.org/
- PDG — `pdg.lbl.gov`

Government / national labs:
- `nasa.gov` / `science.nasa.gov`
- `fermilab.gov`
- `slac.stanford.edu`

YouTube (institutional / official):
- Perimeter Institute — https://www.youtube.com/PIOutreach
- CERN — https://www.youtube.com/channel/UCrHXK2A9JtiexqwHuWGeSMg

---

## Lane 4 — Biology, chemistry, and life sciences

NIH / NLM / NCBI:
- NCBI — `ncbi.nlm.nih.gov`
- PubMed — `pubmed.ncbi.nlm.nih.gov`
- PubMed Central (OA subset) — `pmc.ncbi.nlm.nih.gov`
- MedlinePlus — `medlineplus.gov`  
  Source: https://medlineplus.gov/

Chemistry / molecules / structures:
- PubChem — `pubchem.ncbi.nlm.nih.gov`
- RCSB PDB — `rcsb.org`
- UniProt — `uniprot.org`
- EMBL-EBI — `ebi.ac.uk`

Open access publishers (check licenses article-by-article):
- `plos.org`
- `elifesciences.org`
- `biorxiv.org`

YouTube (public health + research agencies):
- NIH — https://www.youtube.com/user/NIHOD
- WHO — https://www.youtube.com/channel/UC07-dOwgza1IguKA86jqxNA
- CDC — https://www.youtube.com/user/CDCStreamingHealth

---

## Lane 5 — Nuclear engineering, materials science, aerospace

Nuclear engineering / regulatory + safety:
- U.S. NRC — `nrc.gov`
- DOE / OSTI — `energy.gov` | `osti.gov`
- IAEA — `iaea.org`

Materials science / standards:
- NIST — `nist.gov`
- Materials Project — `materialsproject.org`

Aerospace / space engineering:
- NASA — `nasa.gov`, `science.nasa.gov`, `ntrs.nasa.gov`, `jpl.nasa.gov`
- ESA — `esa.int`

YouTube:
- NASA JPL Edu — https://www.youtube.com/@nasajpledu3401

---

## Lane 6 — Robotics, computer science, software development, computer engineering

Robotics ecosystems:
- Open Robotics — `openrobotics.org`
- ROS docs — `docs.ros.org`
- Gazebo — `gazebosim.org`
- OpenCV — `opencv.org`

Computer science education:
- CS50 — https://www.youtube.com/c/cs50
- CMU Robotics Academy — https://www.youtube.com/user/RoboticsAcademy
- CMU RI videos hub — https://www.ri.cmu.edu/videos/

Software references / specs:
- MDN — `developer.mozilla.org`
- RFC Editor — `rfc-editor.org`
- Python docs — `docs.python.org`
- cppreference — `cppreference.com`
- Rust docs — `doc.rust-lang.org` | `docs.rs`
- Git — `git-scm.com`

Game engine references (license-sensitive):
- Unreal Engine docs (Epic Developer Community) — `dev.epicgames.com`
- Unreal Engine legacy docs — `docs.unrealengine.com`
- Unreal Engine legacy forums — `forums.unrealengine.com`
- Unreal Engine primary domain (entry points to docs/support) — `unrealengine.com`

Code hosting (license-sensitive):
- `github.com`
- `gitlab.com`

---

## Lane 7 — Healthcare (public/official)

- MedlinePlus — `medlineplus.gov`
- NIH — `nih.gov`
- CDC — `cdc.gov`
- WHO — `who.int`
- ClinicalTrials.gov — `clinicaltrials.gov`
- FDA — `fda.gov`

---

## Lane 8 — Creative corpora: images, video, audio, literature

Images / media:
- Wikimedia Commons — `commons.wikimedia.org`
- Smithsonian Open Access (CC0) — `si.edu` / `3d.si.edu`  
  Sources: https://www.si.edu/openaccess | https://www.si.edu/openaccess/faq
- NASA images/media — `images.nasa.gov`
- Europeana datasets — `europeana.eu` / `pro.europeana.eu`  
  Source: https://pro.europeana.eu/page/datasets
- Openverse — `openverse.org`
- Internet Archive — `archive.org`

Audio:
- Freesound — `freesound.org`
- LibriVox — `librivox.org`
- Musopen — `musopen.org`
- Open Music Archive — `openmusicarchive.org`
- Mozilla Common Voice — `commonvoice.mozilla.org` (speech corpus; check per-language licenses)
- OpenSLR (speech corpora index; e.g., LibriSpeech, TED-LIUM) — `openslr.org`

Literature:
- Project Gutenberg — `gutenberg.org`  
  Source: https://www.gutenberg.org/
- DOAB — `doabooks.org`
- DOAJ — `doaj.org`

---

## Lane 9 — 3D objects / UE5-ready assets

CC0 / very permissive:
- Poly Haven (CC0) — `polyhaven.com`
- Kenney (often CC0) — `kenney.nl`
- Smithsonian Open Access (CC0) — `3d.si.edu`
- NASA 3D Resources — `science.nasa.gov/3d-resources/`  
  Source: https://science.nasa.gov/3d-resources/

Mixed-license (must filter by license):
- OpenGameArt — `opengameart.org`
- Sketchfab (filter by CC) — `sketchfab.com`
- Thingiverse — `thingiverse.com`

UE marketplace (not necessarily free/open):
- Fab (Epic ecosystem) — `fab.com`

---

## Suggested context tags (for encoder routing)

- DICT, ENCYC, THESAURUS
- COURSE, LECTURE, TEXTBOOK
- PREPRINT, OA_PAPER, SCHOLARLY_META
- GOV_SCI, SPACE_AGENCY, REGULATORY
- BIO_DB, CHEM_DB, MED_REF
- CODE_DOCS, SPEC, API_DOCS, CODE_REPO
- MEDIA_IMAGE, MEDIA_VIDEO, MEDIA_AUDIO, LITERATURE_PD
- ASSET_3D, ASSET_TEXTURE, ASSET_HDRI

---

## Minimal “starter pack” allowlist (start small, stay high-credibility)

- `wikipedia.org`, `wiktionary.org`, `wikidata.org`, `dumps.wikimedia.org`
- `khanacademy.org`, `ocw.mit.edu`, `openstax.org`
- `arxiv.org`, `openalex.org`, `crossref.org`
- `nist.gov`, `nasa.gov`, `science.nasa.gov`, `opendata.cern.ch`
- `ncbi.nlm.nih.gov`, `pubchem.ncbi.nlm.nih.gov`, `pmc.ncbi.nlm.nih.gov`, `medlineplus.gov`
- `gutenberg.org`, `si.edu`, `3d.si.edu`, `science.nasa.gov/3d-resources/`
)EW_ALLOWLISTX";
void SubstrateManager::corpus_crawl_start_neuralis_corpus_default() {
    // Prefer user-updated allowlist if present.
    // This keeps the allowlist user-updatable without weakening enforcement.
    if (!corpus_allowlist_user_loaded) {
        (void)corpus_allowlist_load_user_file_if_present();
    }

    // Start strictly allowlisted crawls in parallel sessions.
    // Session layout:
    //  0: general allowlist hosts
    //  1: publisher targets (license-gated ingestion profile)
    //  2: US patent targets
    //  3: US trademark + copyright targets (separate endpoints, same scheduling caps)
    if (corpus_allowlist_user_loaded && !corpus_allowlist_user_md_utf8.empty()) {
        corpus_crawl_start_from_allowlist_text(corpus_allowlist_user_md_utf8);
    } else {
        corpus_crawl_start_from_allowlist_text(std::string(EW_NEURALIS_CORPUS_ALLOWLIST_MD_UTF8));
    }

    auto start_session = [&](uint32_t slot, uint32_t profile_u32, const std::vector<std::pair<std::string,std::string>>& host_paths, const char* label) {
        if (slot >= EW_CRAWL_SESSION_MAX) return;

        auto ensure_anchor_capacity = [&](uint32_t need_u32) {
            while (anchors.size() < need_u32) anchors.emplace_back((uint32_t)anchors.size());
            while (ancilla.size() < need_u32) ancilla.push_back(ancilla_particle{});
            if (redirect_to.size() < need_u32) redirect_to.resize(need_u32, 0u);
            if (split_child_a.size() < need_u32) split_child_a.resize(need_u32, 0u);
            if (split_child_b.size() < need_u32) split_child_b.resize(need_u32, 0u);
        };
        auto alloc_anchor_id = [&]()->uint32_t {
            const uint32_t id = next_anchor_id_u32;
            next_anchor_id_u32 += 1u;
            ensure_anchor_capacity(next_anchor_id_u32 + 1u);
            return id;
        };

        EwCrawlSession& ss = crawl_sessions[slot];
        ss.active = true;
        ss.profile_u32 = profile_u32;
        ss.q.clear();
        ss.domain_map.clear();

        const uint32_t ctx_id = alloc_anchor_id();
        const uint32_t syl_id = alloc_anchor_id();
        const uint32_t crawl_id = alloc_anchor_id();

        anchors[ctx_id].kind_u32 = EW_ANCHOR_KIND_CONTEXT_ROOT;
        anchors[ctx_id].context_id_u32 = ctx_id;

        anchors[syl_id].kind_u32 = EW_ANCHOR_KIND_SYLLABUS_ROOT;
        anchors[syl_id].context_id_u32 = ctx_id;

        anchors[crawl_id].kind_u32 = EW_ANCHOR_KIND_CRAWLER_ROOT;
        anchors[crawl_id].context_id_u32 = ctx_id;
        anchors[crawl_id].crawler_id_u32 = crawl_id;

        anchors[ctx_id].neighbors.push_back(syl_id);
        anchors[syl_id].neighbors.push_back(ctx_id);
        anchors[syl_id].neighbors.push_back(crawl_id);
        anchors[crawl_id].neighbors.push_back(syl_id);

        ss.context_anchor_id_u32 = ctx_id;
        ss.syllabus_anchor_id_u32 = syl_id;
        ss.crawler_anchor_id_u32 = crawl_id;

        // Domain anchors per unique host.
        std::vector<std::string> uniq_hosts;
        for (size_t i = 0; i < host_paths.size(); ++i) {
            const std::string& h = host_paths[i].first;
            bool seen = false;
            for (size_t j = 0; j < uniq_hosts.size(); ++j) if (uniq_hosts[j] == h) { seen = true; break; }
            if (!seen) uniq_hosts.push_back(h);
        }
        for (size_t k = 0; k < uniq_hosts.size(); ++k) {
            const std::string& host = uniq_hosts[k];
            const uint32_t dom_id = alloc_anchor_id();
            anchors[dom_id].kind_u32 = EW_ANCHOR_KIND_DOMAIN_ROOT;
            anchors[dom_id].context_id_u32 = ctx_id;
            anchors[dom_id].crawler_id_u32 = crawl_id;

            anchors[crawl_id].neighbors.push_back(dom_id);
            anchors[dom_id].neighbors.push_back(crawl_id);

            EwDomainAnchorMapEntry me{};
            me.domain_utf8 = host;
            me.domain_anchor_id_u32 = dom_id;
            ss.domain_map.push_back(me);
        }

        // Enqueue per-host robots stage and per-endpoint root stages (host+path pairs).
        for (size_t i = 0; i < uniq_hosts.size(); ++i) {
            SubstrateManager::EwCorpusCrawlTarget t{};
            t.lane_u32 = 0;
            t.stage_u32 = 0;
            t.profile_u32 = profile_u32;
            t.host_utf8 = uniq_hosts[i];
            t.path_utf8.clear();
            ss.q.push_back(t);
        }
        for (size_t i = 0; i < host_paths.size(); ++i) {
            SubstrateManager::EwCorpusCrawlTarget t{};
            t.lane_u32 = 0;
            t.stage_u32 = 1;
            t.profile_u32 = profile_u32;
            t.host_utf8 = host_paths[i].first;
            t.path_utf8 = host_paths[i].second;
            ss.q.push_back(t);
        }

        std::string m = "CRAWL_SESSION ";
        m += label;
        m += " SCHEDULED ";
        m += std::to_string((unsigned long long)ss.q.size());
        m += " CRAWLER_ANCHOR ";
        m += std::to_string((unsigned long long)ss.crawler_anchor_id_u32);
        if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back(m);
    };

    // Session 1: publishers (metadata-first; training ingestion is license gated inside substrate).
    const std::vector<std::pair<std::string,std::string>> publishers = {
        {"mheducation.com","/"},
        {"highered.mheducation.com","/"},
        {"pearson.com","/"},
        {"www.pearson.com","/"},
        {"www.mheducation.com","/"},
    };
    start_session(1, 1, publishers, "PUBLISHERS");

    // Session 2: US patent targets (public data endpoints).
    const std::vector<std::pair<std::string,std::string>> uspto_pat = {
        {"www.uspto.gov","/patents"},
        {"bulkdata.uspto.gov","/"},
        {"developer.uspto.gov","/"},
    };
    start_session(2, 2, uspto_pat, "USPTO_PATENTS");

    // Session 3: trademarks + copyright targets (USPTO for trademarks, US Copyright Office for copyrights).
    const std::vector<std::pair<std::string,std::string>> mark_copy = {
        {"www.uspto.gov","/trademarks"},
        {"tmsearch.uspto.gov","/"},
        {"tsdr.uspto.gov","/"},
        {"copyright.gov","/"},
        {"www.copyright.gov","/"},
    };
    start_session(3, 3, mark_copy, "MARK_COPY");
}

void SubstrateManager::corpus_crawl_stop() {
    for (uint32_t si = 0; si < EW_CRAWL_SESSION_MAX; ++si) {
        crawl_sessions[si].active = false;
        crawl_sessions[si].q.clear();
    }
    if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("CRAWL_STOPPED");
}

    
bool SubstrateManager::language_bootstrap_from_dir(const std::string& root_dir_utf8) {
    std::string rep;
    const bool any = language_foundation.bootstrap_from_dir(root_dir_utf8, &rep);

    if (!rep.empty()) {
        // Emit bounded report lines.
        size_t i = 0;
        uint32_t emitted = 0;
        while (i < rep.size() && emitted < 16u) {
	        size_t j = rep.find('\n', i);
            if (j == std::string::npos) j = rep.size();
            const std::string line = rep.substr(i, j - i);
            if (!line.empty() && ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back(line);
            emitted++;
            i = (j < rep.size()) ? (j + 1) : rep.size();
        }
    }

    if (!any) {
        if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("LANG_BOOTSTRAP:NO_DATASETS_LOADED");
        return false;
    }

    // Enqueue deterministic language checkpoint tasks. These gate the curriculum
    // and must be accepted (tol=0) before stage advancement.
    const uint64_t src_id = 0x4C414E475F425354ULL; // 'LANG_BST'
    const uint32_t src_anchor = 0u;
    const uint32_t ctx_anchor = 0u;

    auto enqueue_kind = [&](genesis::MetricKind k) {
        const genesis::MetricVector mv = language_foundation.metrics_for_kind(k);
        if (mv.dim_u32 == 0) return;
        // Require non-zero evidence.
        bool any_nonzero = false;
        for (uint32_t i = 0; i < mv.dim_u32; ++i) if (mv.v_q32_32[i] != 0) { any_nonzero = true; break; }
        if (!any_nonzero) return;
        genesis::MetricTask task = language_foundation.make_task_for_kind(k, src_id, src_anchor, ctx_anchor);
        learning_gate.registry().enqueue_task(task);
    };

    enqueue_kind(genesis::MetricKind::Lang_Dictionary_LexiconStats);
    enqueue_kind(genesis::MetricKind::Lang_Thesaurus_RelationStats);
    enqueue_kind(genesis::MetricKind::Lang_Encyclopedia_ConceptStats);
    enqueue_kind(genesis::MetricKind::Lang_SpeechCorpus_AlignmentStats);

    if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("LANG_BOOTSTRAP:CHECKPOINTS_ENQUEUED");

    // Math foundations are learned in parallel with language during Stage 0.
    // They are derived from deterministic internal test banks plus crawl coverage
    // for Khan Academy math.
    {
        std::string mrep;
        const bool mok = math_foundation.bootstrap_defaults(&mrep);
        if (!mrep.empty()) {
            size_t i = 0;
            uint32_t emitted = 0;
            while (i < mrep.size() && emitted < 16u) {
                size_t j = mrep.find('\n', i);
                if (j == std::string::npos) j = mrep.size();
                const std::string line = mrep.substr(i, j - i);
                if (!line.empty() && ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back(line);
                emitted++;
                i = (j < mrep.size()) ? (j + 1) : mrep.size();
            }
        }
        if (mok) {
            auto enqueue_mk = [&](genesis::MetricKind k) {
                const genesis::MetricVector mv = math_foundation.metrics_for_kind(k);
                if (mv.dim_u32 == 0) return;
                bool any_nonzero = false;
                for (uint32_t i = 0; i < mv.dim_u32; ++i) if (mv.v_q32_32[i] != 0) { any_nonzero = true; break; }
                if (!any_nonzero) return;
                genesis::MetricTask task = math_foundation.make_task_for_kind(k, 0x4D4154485F425354ULL /*'MATH_BST'*/, 0u, 0u);
                learning_gate.registry().enqueue_task(task);
            };
            enqueue_mk(genesis::MetricKind::Math_Pemdas_PrecedenceStats);
            enqueue_mk(genesis::MetricKind::Math_Graph_1D_ProbeStats);
            // Khan coverage will become nonzero as crawl runs; still enqueue so it gates stage0 completion.
            enqueue_mk(genesis::MetricKind::Math_KhanAcademy_CoverageStats);
            if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("MATH_BOOTSTRAP:CHECKPOINTS_ENQUEUED");
        }
    }
    return true;
}


Basis9 SubstrateManager::projected_for(const Anchor& a) const {
    Basis9 p = a.basis9;

    // Projection is a deterministic view of the anchor state plus global
    // measurement-frame mismatch. No pseudo-random offsets are applied.
    p.d[3] = a.tau_turns_q;
    p.d[4] = wrap_turns(a.theta_q + frame_gamma_turns_q);
    p.d[5] = a.chi_q;
    p.d[8] = a.m_q;
    return p;
}


// -----------------------------------------------------------------------------
// ΩA Operator Packet execution (Equations appendix ΩA)
// -----------------------------------------------------------------------------
// Implementation notes:
// - All computation is performed in the simulated substrate microprocessor.
// - Lane values are stored as Basis9 where each component is interpreted as Q32.32.
// - Scalar-as-E9 convention: scalar lanes use scalar_e9.d[0] and remaining zeros.

static inline uint32_t ew_read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void ge_wr_u32_le(uint8_t* b, uint32_t off, uint32_t v) {
    b[off + 0u] = (uint8_t)(v & 0xFFu);
    b[off + 1u] = (uint8_t)((v >> 8u) & 0xFFu);
    b[off + 2u] = (uint8_t)((v >> 16u) & 0xFFu);
    b[off + 3u] = (uint8_t)((v >> 24u) & 0xFFu);
}

static inline void ge_wr_i32_le(uint8_t* b, uint32_t off, int32_t v) {
    ge_wr_u32_le(b, off, (uint32_t)v);
}

static inline void ge_wr_u64_le(uint8_t* b, uint32_t off, uint64_t v) {
    for (uint32_t k = 0u; k < 8u; ++k) {
        b[off + k] = (uint8_t)((v >> (8u * k)) & 0xFFu);
    }
}

static inline int64_t ew_read_i64_le(const uint8_t* p) {
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u |= ((uint64_t)p[i] << (8 * i));
    return (int64_t)u;
}

static inline double ew_read_f64_le(const uint8_t* p) {
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u |= ((uint64_t)p[i] << (8 * i));
    double d;
    static_assert(sizeof(double) == sizeof(uint64_t), "double size mismatch");
    std::memcpy(&d, &u, sizeof(double));
    return d;
}

static inline int64_t ew_i64_round_from_f64(double x) {
    // Lane IDs are specified as exact integer-valued IEEE-754 doubles (<=2^53).
    // We round to nearest integer deterministically.
    if (x >= 0.0) return (int64_t)(x + 0.5);
    return (int64_t)(x - 0.5);
}

static inline EwLaneId9 ew_lane_id9_from_e9_f64(const double e9[9]) {
    EwLaneId9 id{};
    for (int i = 0; i < 9; ++i) id.v[i] = ew_i64_round_from_f64(e9[i]);
    return id;
}

static inline int ew_lane_id9_cmp(const EwLaneId9& a, const EwLaneId9& b) {
    for (int i = 0; i < 9; ++i) {
        if (a.v[i] < b.v[i]) return -1;
        if (a.v[i] > b.v[i]) return  1;
    }
    return 0;
}

static inline EwOpLaneEntry* ew_find_lane(std::vector<EwOpLaneEntry>& lanes, const EwLaneId9& id) {
    for (size_t i = 0; i < lanes.size(); ++i) {
        if (ew_lane_id9_equal(lanes[i].lane_id, id)) return &lanes[i];
    }
    return nullptr;
}

static inline EwOpLaneEntry& ew_upsert_lane_scalar(std::vector<EwOpLaneEntry>& lanes, const EwLaneId9& id) {
    EwOpLaneEntry* e = ew_find_lane(lanes, id);
    if (e) { e->is_buffer = false; return *e; }
    EwOpLaneEntry ne{};
    ne.lane_id = id;
    ne.is_buffer = false;
    for (int k = 0; k < 9; ++k) ne.scalar_e9.d[k] = 0;
    lanes.push_back(ne);
    return lanes.back();
}

static inline EwOpLaneEntry& ew_upsert_lane_buffer(std::vector<EwOpLaneEntry>& lanes, const EwLaneId9& id) {
    EwOpLaneEntry* e = ew_find_lane(lanes, id);
    if (e) { e->is_buffer = true; return *e; }
    EwOpLaneEntry ne{};
    ne.lane_id = id;
    ne.is_buffer = true;
    for (int k = 0; k < 9; ++k) ne.scalar_e9.d[k] = 0;
    lanes.push_back(ne);
    return lanes.back();
}

static inline int64_t ew_mul_q32_32_local(int64_t a, int64_t b) {
    __int128 p = (__int128)a * (__int128)b;
    return (int64_t)(p >> 32);
}

static inline int64_t ew_div_q32_32_local(int64_t a, int64_t b) {
    if (b == 0) return 0;
    __int128 n = (__int128)a << 32;
    return (int64_t)(n / (__int128)b);
}

static inline int64_t ew_wrap_turns_q32_32(int64_t turns_q32_32) {
    // Wrap to [-0.5, +0.5) turns in Q32.32.
    const int64_t one = (1LL << 32);
    const int64_t half = (1LL << 31);
    while (turns_q32_32 >= half) turns_q32_32 -= one;
    while (turns_q32_32 < -half) turns_q32_32 += one;
    return turns_q32_32;
}

// Deterministic sin/cos for angle given in turns (Q32.32).
// Implementation: convert turns->radians using fixed 2π in Q32.32, then use a
// fixed polynomial on [-π,π] (no libm, deterministic).
static inline int64_t ew_two_pi_q32_32() {
    // 2π ≈ 6.283185307179586
    return (int64_t)(6.283185307179586 * (double)(1ULL << 32));
}

static inline int64_t ew_pi_q32_32() {
    return (int64_t)(3.141592653589793 * (double)(1ULL << 32));
}

static inline int64_t ew_sin_poly_q32_32(int64_t x_q32_32) {
    // x in radians Q32.32, clamped to [-π,π].
    const int64_t pi = ew_pi_q32_32();
    if (x_q32_32 > pi) x_q32_32 = pi;
    if (x_q32_32 < -pi) x_q32_32 = -pi;

    // sin(x) ≈ x - x^3/6 + x^5/120 - x^7/5040
    const __int128 x2 = (__int128)x_q32_32 * (__int128)x_q32_32; // Q64.64
    const int64_t x2_q32_32 = (int64_t)(x2 >> 32);
    const __int128 x3 = (__int128)x2_q32_32 * (__int128)x_q32_32; // Q64.64
    const int64_t x3_q32_32 = (int64_t)(x3 >> 32);
    const __int128 x5 = (__int128)x3_q32_32 * (__int128)x2_q32_32; // Q64.64
    const int64_t x5_q32_32 = (int64_t)(x5 >> 32);
    const __int128 x7 = (__int128)x5_q32_32 * (__int128)x2_q32_32; // Q64.64
    const int64_t x7_q32_32 = (int64_t)(x7 >> 32);

    int64_t y = x_q32_32;
    y -= (x3_q32_32 / 6);
    y += (x5_q32_32 / 120);
    y -= (x7_q32_32 / 5040);
    return y;
}

static inline int64_t ew_cos_poly_q32_32(int64_t x_q32_32) {
    const int64_t pi = ew_pi_q32_32();
    if (x_q32_32 > pi) x_q32_32 = pi;
    if (x_q32_32 < -pi) x_q32_32 = -pi;

    // cos(x) ≈ 1 - x^2/2 + x^4/24 - x^6/720
    const __int128 x2 = (__int128)x_q32_32 * (__int128)x_q32_32;
    const int64_t x2_q32_32 = (int64_t)(x2 >> 32);
    const __int128 x4 = (__int128)x2_q32_32 * (__int128)x2_q32_32;
    const int64_t x4_q32_32 = (int64_t)(x4 >> 32);
    const __int128 x6 = (__int128)x4_q32_32 * (__int128)x2_q32_32;
    const int64_t x6_q32_32 = (int64_t)(x6 >> 32);

    const int64_t one = (1LL << 32);
    int64_t y = one;
    y -= (x2_q32_32 / 2);
    y += (x4_q32_32 / 24);
    y -= (x6_q32_32 / 720);
    return y;
}

static inline void ew_sincos_turns_q32_32(int64_t turns_q32_32, int64_t* out_sin_q32_32, int64_t* out_cos_q32_32) {
    // angle_rad = 2π * turns
    const int64_t two_pi = ew_two_pi_q32_32();
    int64_t angle = ew_mul_q32_32_local(two_pi, turns_q32_32);
    // Wrap angle to [-π,π] by wrapping turns first.
    turns_q32_32 = ew_wrap_turns_q32_32(turns_q32_32);
    angle = ew_mul_q32_32_local(two_pi, turns_q32_32);
    *out_sin_q32_32 = ew_sin_poly_q32_32(angle);
    *out_cos_q32_32 = ew_cos_poly_q32_32(angle);
}

static inline int64_t ew_clamp01_q32_32(int64_t x) {
    if (x < 0) return 0;
    if (x > (1LL<<32)) return (1LL<<32);
    return x;
}

// Deterministic UTF-8 decode to Unicode codepoints (uint32).
static inline void ew_utf8_to_codepoints(const std::string& s, std::vector<uint32_t>& out) {
    out.clear();
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x80) {
            out.push_back((uint32_t)c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            uint8_t c1 = (uint8_t)s[i+1];
            uint32_t cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
            out.push_back(cp);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            uint8_t c1 = (uint8_t)s[i+1];
            uint8_t c2 = (uint8_t)s[i+2];
            uint32_t cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(c1 & 0x3F) << 6) | (uint32_t)(c2 & 0x3F);
            out.push_back(cp);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            uint8_t c1 = (uint8_t)s[i+1];
            uint8_t c2 = (uint8_t)s[i+2];
            uint8_t c3 = (uint8_t)s[i+3];
            uint32_t cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(c1 & 0x3F) << 12) | ((uint32_t)(c2 & 0x3F) << 6) | (uint32_t)(c3 & 0x3F);
            out.push_back(cp);
            i += 4;
        } else {
            // Invalid byte: deterministic replacement.
            out.push_back((uint32_t)0xFFFDu);
            i += 1;
        }
    }
}

static inline void ew_lane_id9_set(EwLaneId9& id, int64_t a0,int64_t a1,int64_t a2,int64_t a3,int64_t a4,int64_t a5,int64_t a6,int64_t a7,int64_t a8) {
    id.v[0]=a0; id.v[1]=a1; id.v[2]=a2; id.v[3]=a3; id.v[4]=a4; id.v[5]=a5; id.v[6]=a6; id.v[7]=a7; id.v[8]=a8;
}

static inline void ew_opk_text_eigen_encode(std::vector<EwOpLaneEntry>& lanes,
                                           const EwLaneId9& in_cp_stream_base,
                                           const EwLaneId9& in_n_scalar,
                                           const EwLaneId9& out_char_phase_base,
                                           const uint8_t* payload) {
    const uint32_t P = ew_read_u32_le(payload + 0);
    // trig_impl_mode_u32 is accepted but both modes are deterministic in this build.
    const uint32_t trig_impl_mode = ew_read_u32_le(payload + 4);
    (void)trig_impl_mode;
    const double denom_cp_f = ew_read_f64_le(payload + 8);
    const double c0_f = ew_read_f64_le(payload + 40);
    const double c_span_f = ew_read_f64_le(payload + 48);
    (void)ew_read_f64_le(payload + 16); // t0 (unused by Q32 lane path)
    (void)ew_read_f64_le(payload + 24); // t_span (unused by Q32 lane path)

    // Load cp stream buffer and N scalar.
    EwOpLaneEntry* cp_lane = ew_find_lane(lanes, in_cp_stream_base);
    EwOpLaneEntry* n_lane = ew_find_lane(lanes, in_n_scalar);
    if (!cp_lane || !cp_lane->is_buffer || !n_lane || n_lane->is_buffer) return;

    const uint32_t N = (uint32_t)(n_lane->scalar_e9.d[0] >> 32);
    if (N == 0 || P == 0) return;

    EwOpLaneEntry& out_buf = ew_upsert_lane_buffer(lanes, out_char_phase_base);
    out_buf.buffer_e9.clear();
    out_buf.buffer_e9.reserve((size_t)N * (size_t)P);

    const int64_t denom_cp_q32_32 = (int64_t)(denom_cp_f * (double)(1ULL<<32));
    const int64_t c0_q32_32 = (int64_t)(c0_f * (double)(1ULL<<32));
    const int64_t c_span_q32_32 = (int64_t)(c_span_f * (double)(1ULL<<32));

    for (uint32_t i = 0; i < N; ++i) {
        const uint32_t cp = (uint32_t)(cp_lane->buffer_e9[i].d[0] >> 32);
        const int64_t cp_q32_32 = (int64_t)cp << 32;
        const int64_t n_q32_32 = (denom_cp_q32_32 != 0) ? ew_div_q32_32_local(cp_q32_32, denom_cp_q32_32) : 0;

        for (uint32_t p = 0; p < P; ++p) {
            const int64_t e_q32_32 = (P > 1) ? ew_div_q32_32_local((int64_t)p << 32, (int64_t)(P - 1) << 32) : 0;

            // c = clamp01(c0 + c_span * n * (0.5 + 0.5*e))
            const int64_t half = (1LL<<31);
            const int64_t w = half + ew_mul_q32_32_local(half, e_q32_32);
            const int64_t c = ew_clamp01_q32_32(c0_q32_32 + ew_mul_q32_32_local(c_span_q32_32, ew_mul_q32_32_local(n_q32_32, w)));

            int64_t s2, c2, s4, c4, s8, c8, s16, c16;
            // sin/cos(2π n)
            ew_sincos_turns_q32_32(n_q32_32, &s2, &c2);
            // 2n,4n,8n,16n turns (wrapped)
            int64_t n2 = ew_wrap_turns_q32_32(n_q32_32 * 2);
            int64_t n4 = ew_wrap_turns_q32_32(n_q32_32 * 4);
            int64_t n8 = ew_wrap_turns_q32_32(n_q32_32 * 8);
            int64_t n16= ew_wrap_turns_q32_32(n_q32_32 * 16);
            ew_sincos_turns_q32_32(n2, &s4, &c4);
            ew_sincos_turns_q32_32(n4, &s8, &c8);
            ew_sincos_turns_q32_32(n8, &s16, &c16);

            Basis9 e9{};
            e9.d[0] = s2;
            e9.d[1] = c2;
            e9.d[2] = s4;
            e9.d[3] = c4;
            e9.d[4] = s8;
            e9.d[5] = c;   // coherence index 5
            e9.d[6] = s16;
            e9.d[7] = c16;
            e9.d[8] = e_q32_32;
            out_buf.buffer_e9.push_back(e9);
        }
    }
}

// -----------------------------------------------------------------------------
// Stable core operator helpers (deterministic, Q32.32 lane math, no hashing/crypto)
// -----------------------------------------------------------------------------

static inline void ew_opk_aggregate_normalized_sum(std::vector<EwOpLaneEntry>& lanes,
                                                   const EwLaneId9& in_a,
                                                   const EwLaneId9& in_b,
                                                   const EwLaneId9& out_lane,
                                                   const uint8_t* payload) {
    (void)payload;
    EwOpLaneEntry* a = ew_find_lane(lanes, in_a);
    EwOpLaneEntry* b = ew_find_lane(lanes, in_b);
    if (!a || !b) return;
    if (a->is_buffer || b->is_buffer) return;

    Basis9 s{};
    int64_t max_abs = 0;
    for (int k = 0; k < 9; ++k) {
        s.d[k] = a->scalar_e9.d[k] + b->scalar_e9.d[k];
        const int64_t av = (s.d[k] < 0) ? -s.d[k] : s.d[k];
        if (av > max_abs) max_abs = av;
    }
    // Normalize to [-1,1] in Q32.32 if magnitude exceeds 1.
    const int64_t one = (1LL << 32);
    if (max_abs > one) {
        const int64_t inv = ew_div_q32_32_local(one, max_abs);
        for (int k = 0; k < 9; ++k) s.d[k] = ew_mul_q32_32_local(s.d[k], inv);
    }

    EwOpLaneEntry& out = ew_upsert_lane_scalar(lanes, out_lane);
    out.is_buffer = false;
    out.scalar_e9 = s;
}

static inline void ew_opk_project_coh_dot(std::vector<EwOpLaneEntry>& lanes,
                                          const ancilla_particle* tr,
                                          const EwCtx& ctx,
                                          const EwLaneId9& in_a,
                                          const EwLaneId9& in_b,
                                          const EwLaneId9& out_lane,
                                          const uint8_t* payload) {
    (void)tr;
    (void)ctx;
    (void)payload;
    EwOpLaneEntry* a = ew_find_lane(lanes, in_a);
    EwOpLaneEntry* b = ew_find_lane(lanes, in_b);
    if (!a || !b) return;
    if (a->is_buffer || b->is_buffer) return;

    // Q32.32 dot product, accumulated in Q32.32.
    __int128 acc = 0;
    for (int k = 0; k < 9; ++k) {
        acc += (__int128)a->scalar_e9.d[k] * (__int128)b->scalar_e9.d[k];
    }
    // Scale back to Q32.32 (because each term is Q32.32*Q32.32).
    const int64_t dot_q32_32 = (int64_t)(acc >> 32);

    EwOpLaneEntry& out = ew_upsert_lane_scalar(lanes, out_lane);
    out.is_buffer = false;
    for (int k = 0; k < 9; ++k) out.scalar_e9.d[k] = 0;
    // Store as coherence magnitude in slot 5 by convention.
    out.scalar_e9.d[5] = dot_q32_32;
    out.scalar_e9.d[0] = dot_q32_32;
}

static inline void ew_opk_constrain_pi_g(std::vector<EwOpLaneEntry>& lanes,
                                         const ancilla_particle* tr,
                                         const EwCtx& ctx,
                                         const EwLaneId9& in_cur,
                                         const EwLaneId9& in_cand,
                                         const EwLaneId9& in_fail,
                                         const EwLaneId9& out_s,
                                         const EwLaneId9& out_fail,
                                         const uint8_t* payload) {
    (void)tr;
    (void)ctx;
    // Payload may include thresholds; for now, enforce a strict deterministic improvement rule.
    (void)payload;

    EwOpLaneEntry* cur = ew_find_lane(lanes, in_cur);
    EwOpLaneEntry* cand = ew_find_lane(lanes, in_cand);
    EwOpLaneEntry* fail = ew_find_lane(lanes, in_fail);
    if (!cur || !cand || !fail) return;
    if (cur->is_buffer || cand->is_buffer || fail->is_buffer) return;

    const int64_t cur_c = cur->scalar_e9.d[5];
    const int64_t cand_c = cand->scalar_e9.d[5];
    const bool accept = (cand_c >= cur_c);

    EwOpLaneEntry& out0 = ew_upsert_lane_scalar(lanes, out_s);
    EwOpLaneEntry& out1 = ew_upsert_lane_scalar(lanes, out_fail);
    out0.is_buffer = false;
    out1.is_buffer = false;

    out0.scalar_e9 = accept ? cand->scalar_e9 : cur->scalar_e9;
    out1.scalar_e9 = accept ? fail->scalar_e9 : cand->scalar_e9;
}

static inline void ew_opk_chain_apply(std::vector<EwOpLaneEntry>& lanes,
                                      const ancilla_particle* tr,
                                      const EwCtx& ctx,
                                      const uint8_t* p_in_lanes,
                                      uint32_t n_in,
                                      const uint8_t* p_out_lanes,
                                      uint32_t n_out,
                                      const uint8_t* payload) {
    (void)tr;
    (void)ctx;
    (void)payload;
    // Default deterministic chain: copy lane i -> lane i for min(n_in,n_out).
    const uint32_t n = (n_in < n_out) ? n_in : n_out;
    double tmp[9];
    auto lane_at = [&](const uint8_t* base, uint32_t idx)->EwLaneId9 {
        for (int k = 0; k < 9; ++k) tmp[k] = ew_read_f64_le(base + (size_t)idx * 72 + (size_t)k * 8);
        return ew_lane_id9_from_e9_f64(tmp);
    };
    for (uint32_t i = 0; i < n; ++i) {
        const EwLaneId9 in_id = lane_at(p_in_lanes, i);
        const EwLaneId9 out_id = lane_at(p_out_lanes, i);
        EwOpLaneEntry* src = ew_find_lane(lanes, in_id);
        if (!src) continue;
        if (src->is_buffer) {
            EwOpLaneEntry& dst = ew_upsert_lane_buffer(lanes, out_id);
            dst.is_buffer = true;
            dst.buffer_e9 = src->buffer_e9;
        } else {
            EwOpLaneEntry& dst = ew_upsert_lane_scalar(lanes, out_id);
            dst.is_buffer = false;
            dst.scalar_e9 = src->scalar_e9;
        }
    }
}


static inline void ew_execute_operator_packets_v1(SubstrateManager* sm) {
    if (!sm) return;
    if (sm->operator_packets_v1.empty()) return;

    // Reset lanes for this tick (deterministic).
    sm->op_lanes.clear();

    // Build a deterministic codepoint stream from the latest observation.
    EwLaneId9 lane_cp_stream{};
    EwLaneId9 lane_n_scalar{};
    ew_lane_id9_set(lane_cp_stream, 2000,0,0,0,0,0,0,0,0);
    ew_lane_id9_set(lane_n_scalar, 2001,0,0,0,0,0,0,0,0);

    std::vector<uint32_t> cps;
    ew_utf8_to_codepoints(sm->last_observation_text, cps);

    EwOpLaneEntry& cp_buf = ew_upsert_lane_buffer(sm->op_lanes, lane_cp_stream);
    cp_buf.buffer_e9.clear();
    cp_buf.buffer_e9.reserve(cps.size());
    for (size_t i = 0; i < cps.size(); ++i) {
        Basis9 v{};
        for (int k = 0; k < 9; ++k) v.d[k] = 0;
        v.d[0] = (int64_t)cps[i] << 32;
        cp_buf.buffer_e9.push_back(v);
    }

    EwOpLaneEntry& n_sc = ew_upsert_lane_scalar(sm->op_lanes, lane_n_scalar);
    for (int k = 0; k < 9; ++k) n_sc.scalar_e9.d[k] = 0;
    n_sc.scalar_e9.d[0] = (int64_t)cps.size() << 32;

    struct PView {
        uint32_t exec_order = 0;
        const EwAnchorOpPackedV1Bytes* pkt = nullptr;
    };
    std::vector<PView> views;
    views.reserve(sm->operator_packets_v1.size());

    for (size_t i = 0; i < sm->operator_packets_v1.size(); ++i) {
        const EwAnchorOpPackedV1Bytes& pb = sm->operator_packets_v1[i];
        const uint8_t* b = pb.bytes;
        const uint32_t exec_order = ew_read_u32_le(b + 76);
        PView v{};
        v.exec_order = exec_order;
        v.pkt = &pb;
        views.push_back(v);
    }

    std::sort(views.begin(), views.end(), [](const PView& a, const PView& b) {
        return a.exec_order < b.exec_order;
    });

    // Minimal payload sizing table for the stable core operator kinds.
    auto expected_payload_bytes = [](uint32_t k)->uint32_t {
        switch (k) {
            case 0x00000001u: return 56u;  // OPK_TEXT_EIGEN_ENCODE
            case 0x00000002u: return 80u;  // OPK_AGGREGATE_NORMALIZED_SUM
            case 0x00000003u: return 8u;   // OPK_PROJECT_COH_DOT
            case 0x00000004u: return 160u; // OPK_CONSTRAIN_PI_G
            case 0x00000005u: return 256u; // OPK_CHAIN_APPLY
            case 0x00000009u: return 72u;  // OPK_COMPUTE_BUS_DISPATCH
            default: return 0xFFFFFFFFu;
        }
    };

    auto i64_abs_local = [](int64_t v)->int64_t { return v < 0 ? -v : v; };

    auto smooth_focus_q32_32 = [&](
        int64_t current_q32_32,
        int64_t target_q32_32,
        int32_t tau_q16_16,
        int32_t max_vel_q16_16,
        int32_t deadband_m_q16_16,
        int32_t dt_ms_s32)->int64_t
    {
        const int64_t deadband_q32_32 = ((int64_t)deadband_m_q16_16) << 16;
        const int64_t diff_q32_32 = target_q32_32 - current_q32_32;
        if (i64_abs_local(diff_q32_32) <= deadband_q32_32) return current_q32_32;

        const int64_t dt_q16_16 = ((int64_t)dt_ms_s32 * 65536) / 1000;
        int64_t alpha_q16_16 = 0;
        if (tau_q16_16 > 0) alpha_q16_16 = (dt_q16_16 << 16) / (int64_t)tau_q16_16;
        if (alpha_q16_16 < 0) alpha_q16_16 = 0;
        if (alpha_q16_16 > 65536) alpha_q16_16 = 65536;

        int64_t delta_q32_32 = (diff_q32_32 * alpha_q16_16) >> 16;
        const int64_t max_delta_q32_32 = (((int64_t)max_vel_q16_16) * dt_q16_16);
        if (delta_q32_32 > max_delta_q32_32) delta_q32_32 = max_delta_q32_32;
        if (delta_q32_32 < -max_delta_q32_32) delta_q32_32 = -max_delta_q32_32;
        return current_q32_32 + delta_q32_32;
    };

    auto opk_compute_bus_dispatch = [&](const uint8_t* payload72) {
        // Payload layout (72 bytes):
        //  0..3   u32 subop
        //  4..19  SpiderCode4 transport fields (for audit/debug)
        //  20..47 EwCarrierWaveQ32_32 (carrier-coded request)
        //  48..71 reserved
        //
        // IMPORTANT: No raw struct/scalar compute inputs are accepted in the
        // payload. The payload is a carrier-coded request marker; the
        // operator reads authoritative scalar state from anchors / ctx.
        const uint32_t subop = ew_read_u32_le(payload72 + 0);

        // Sub-ops:
        //  1: camera focus update (autofocus)
        //  2: project settings apply (validate + mutate settings anchor/state)
        //  3: input mapping effects (raw input -> anchor mutation)
        //  4: input binding set (validate + mutate bindings table)

        // Resolve canonical camera anchor once for ops that need it.
        Anchor* cam_ptr = nullptr;
        if (sm->camera_anchor_id_u32 != 0u && sm->camera_anchor_id_u32 < sm->anchors.size()) {
            Anchor& a = sm->anchors[sm->camera_anchor_id_u32];
            if (a.kind_u32 == EW_ANCHOR_KIND_CAMERA) cam_ptr = &a;
        }

        auto clamp_i32 = [&](int32_t v, int32_t lo, int32_t hi)->int32_t {
            if (v < lo) return lo;
            if (v > hi) return hi;
            return v;
        };

        // Q16.16 quaternion helpers (deterministic integer math).
        auto qmul_q16 = [&](int32_t a, int32_t b)->int32_t {
            return (int32_t)(((int64_t)a * (int64_t)b) >> 16);
        };
        auto quat_mul = [&](const int32_t qA[4], const int32_t qB[4], int32_t out[4]) {
            // (x,y,z,w)
            const int32_t ax=qA[0], ay=qA[1], az=qA[2], aw=qA[3];
            const int32_t bx=qB[0], by=qB[1], bz=qB[2], bw=qB[3];
            out[0] = qmul_q16(aw,bx) + qmul_q16(ax,bw) + qmul_q16(ay,bz) - qmul_q16(az,by);
            out[1] = qmul_q16(aw,by) - qmul_q16(ax,bz) + qmul_q16(ay,bw) + qmul_q16(az,bx);
            out[2] = qmul_q16(aw,bz) + qmul_q16(ax,by) - qmul_q16(ay,bx) + qmul_q16(az,bw);
            out[3] = qmul_q16(aw,bw) - qmul_q16(ax,bx) - qmul_q16(ay,by) - qmul_q16(az,bz);
        };
        auto quat_norm_approx = [&](int32_t q[4]) {
            // One-step normalization using reciprocal sqrt approximation.
            // Deterministic: use Q16.16 length^2, Newton step.
            int64_t s = 0;
            for (int i=0;i<4;++i) s += (int64_t)q[i]*(int64_t)q[i];
            // s is Q32.32. Target is 1.0 in Q32.32 => 1<<32.
            if (s <= 0) { q[0]=q[1]=q[2]=0; q[3]=65536; return; }
            // invsqrt approx in Q16.16: start with 1.0.
            int32_t x = 65536;
            // Convert s to Q16.16 by >>16.
            int32_t s_q16 = (int32_t)(s >> 16);
            // Newton-Raphson: x = x*(3 - s*x*x)/2
            // All in Q16.16.
            int32_t xx = qmul_q16(x, x);
            int32_t sxx = qmul_q16(s_q16, xx);
            int32_t three = (int32_t)(3 * 65536);
            int32_t term = three - sxx;
            x = qmul_q16(x, term);
            x = (int32_t)((int64_t)x >> 1);
            for (int i=0;i<4;++i) q[i] = qmul_q16(q[i], x);
        };
        auto sincos_half_angle_q16 = [&](int32_t theta_rad_q16, int32_t* out_sin_half, int32_t* out_cos_half) {
            // Compute sin(theta/2), cos(theta/2) with small-angle Taylor in Q16.16.
            const int32_t half = (int32_t)((int64_t)theta_rad_q16 >> 1);
            // x in Q16.16. Use sin x ≈ x - x^3/6, cos x ≈ 1 - x^2/2.
            const int32_t x = half;
            const int32_t x2 = qmul_q16(x, x);
            const int32_t x3 = qmul_q16(x2, x);
            const int32_t inv6 = (int32_t)(65536 / 6);
            const int32_t inv2 = (int32_t)(65536 / 2);
            const int32_t sinx = x - qmul_q16(x3, inv6);
            const int32_t cosx = 65536 - qmul_q16(x2, inv2);
            *out_sin_half = sinx;
            *out_cos_half = cosx;
        };

        if (subop == 1u) {
            // Camera autofocus update.
            if (!cam_ptr) return;
            Anchor& a = *cam_ptr;

            const int32_t dt_ms = sm->project_settings.simulation.fixed_dt_ms_s32;
            const uint32_t mode_u32 = (uint32_t)a.camera_state.focus_mode_u8;
            const int64_t cur = a.camera_state.focus_distance_m_q32_32;
            const int64_t manual = a.camera_state.manual_focus_distance_m_q32_32;
            // Median depth is provided as normalized depth in Q16.16; convert to
            // linear meters inside the substrate using camera near/far settings.
            const int32_t dnorm_q16 = sm->camera_sensor.median_depth_norm_q16_16;
            const int32_t near_m_q16 = sm->project_settings.camera.near_m_q16_16;
            const int32_t far_m_q16 = sm->project_settings.camera.far_m_q16_16;
            int64_t median = manual;
            {
                const int32_t far_minus_near = far_m_q16 - near_m_q16;
                const int32_t scaled = qmul_q16(far_minus_near, dnorm_q16);
                const int32_t denom_q16 = far_m_q16 - scaled;
                // numerator Q32.32
                const int64_t num_q32 = (int64_t)near_m_q16 * (int64_t)far_m_q16;
                if (denom_q16 > (int32_t)(0.0001f * 65536.0f)) {
                    // (Q32.32 / Q16.16) => Q16.16
                    const int64_t lin_q16 = (num_q32 / (int64_t)denom_q16);
                    median = (lin_q16 << 16); // Q32.32
                }
            }
            const int32_t tau_q16_16 = a.camera_state.tau_seconds_q16_16;
            const int32_t max_vel_q16_16 = a.camera_state.max_refocus_vel_mps_q16_16;
            const int32_t deadband_m_q16_16 = a.camera_state.deadband_m_q16_16;

            int64_t target = manual;
            if (mode_u32 == (uint32_t)EwFocusMode::MedianDepth) target = median;
            else if (mode_u32 == (uint32_t)EwFocusMode::ManualDistance) target = manual;

            const int64_t next = smooth_focus_q32_32(cur, target, tau_q16_16, max_vel_q16_16, deadband_m_q16_16, dt_ms);
            a.camera_state.focus_distance_m_q32_32 = next;
            return;
        }

        if (subop == 2u) {
            // Project settings apply: validate + mutate inside substrate.
            if (sm->pending_settings_set.valid_u32 == 0u) return;
            const auto req = sm->pending_settings_set;
            sm->pending_settings_set.valid_u32 = 0u;

            // Stable ids (tab/field) are defined by GE_project_settings.*
            if (req.tab_u32 == 0u) { // Rendering
                if (req.field_u32 == 1u) {
                    int32_t v = (int32_t)(req.value_q32_32 >> 16);
                    // gain in [0..8]
                    v = clamp_i32(v, 0, (int32_t)(8 * 65536));
                    sm->project_settings.rendering.dnoise_gain_q16_16 = v;
                } else if (req.field_u32 == 2u) {
                    int32_t v = (int32_t)(req.value_q32_32 >> 16);
                    // bias in [-8..8]
                    v = clamp_i32(v, (int32_t)(-8 * 65536), (int32_t)(8 * 65536));
                    sm->project_settings.rendering.dnoise_bias_q16_16 = v;
                }
            } else if (req.tab_u32 == 2u) { // Camera
                if (req.field_u32 == 1u) {
                    int32_t v = (int32_t)(req.value_q32_32 >> 16);
                    // focal length mm in [5..300]
                    v = clamp_i32(v, (int32_t)(5 * 65536), (int32_t)(300 * 65536));
                    sm->project_settings.camera.default_focal_length_mm_q16_16 = v;
                } else if (req.field_u32 == 10u) {
                    int32_t v = (int32_t)(req.value_q32_32 >> 16);
                    v = clamp_i32(v, 0, (int32_t)(20 * 65536));
                    sm->project_settings.camera.move_speed_mps_q16_16 = v;
                } else if (req.field_u32 == 11u) {
                    int32_t v = (int32_t)(req.value_q32_32 >> 16);
                    v = clamp_i32(v, 0, (int32_t)(5 * 65536));
                    sm->project_settings.camera.move_step_m_q16_16 = v;
                } else if (req.field_u32 == 12u) {
                    int32_t v = (int32_t)(req.value_q32_32 >> 16);
                    v = clamp_i32(v, 0, (int32_t)(1 * 65536));
                    sm->project_settings.camera.look_sens_rad_per_unit_q16_16 = v;
                }
            }

            sm->project_settings_revision_u64++;
            return;
        }

        if (subop == 3u) {
            // Input mapping effects: raw OS/event codes -> anchor mutation.
            if (!cam_ptr) return;
            if (sm->pending_input_event.valid_u32 == 0u) return;
            const auto ev = sm->pending_input_event;
            sm->pending_input_event.valid_u32 = 0u;

            // Ensure bindings are present (fail closed if missing).
            if (!sm->input_bindings_loaded) return;

            auto lookup = [&](const std::vector<SubstrateManager::EwInputBinding>& v, uint32_t raw)->SubstrateManager::EwInputBinding {
                for (const auto& b : v) if (b.raw_id_u32 == raw) return b;
                return SubstrateManager::EwInputBinding{};
            };

            const bool is_action = (ev.kind_u16 == 1u);
            const auto b = is_action ? lookup(sm->input_action_bindings, ev.id_u32) : lookup(sm->input_axis_bindings, ev.id_u32);
            const uint32_t mapped = b.mapped_u32;
            if (mapped == 0u) return;

            Anchor& cam = *cam_ptr;
            const int32_t dt_ms = sm->project_settings.simulation.fixed_dt_ms_s32;

            auto step_delta_q16 = [&](int32_t base_step_q16, int32_t sign)->int32_t {
                (void)dt_ms;
                return (sign >= 0) ? base_step_q16 : -base_step_q16;
            };

            if (mapped == (uint32_t)SubstrateManager::EwMappedInputAction::MoveForward) {
                if (is_action && ev.pressed_u8) cam.camera_state.pos_xyz_q16_16[2] += step_delta_q16(sm->project_settings.camera.move_step_m_q16_16, +1);
            } else if (mapped == (uint32_t)SubstrateManager::EwMappedInputAction::MoveBackward) {
                if (is_action && ev.pressed_u8) cam.camera_state.pos_xyz_q16_16[2] += step_delta_q16(sm->project_settings.camera.move_step_m_q16_16, -1);
            } else if (mapped == (uint32_t)SubstrateManager::EwMappedInputAction::MoveRight) {
                if (is_action && ev.pressed_u8) cam.camera_state.pos_xyz_q16_16[0] += step_delta_q16(sm->project_settings.camera.move_step_m_q16_16, +1);
            } else if (mapped == (uint32_t)SubstrateManager::EwMappedInputAction::MoveLeft) {
                if (is_action && ev.pressed_u8) cam.camera_state.pos_xyz_q16_16[0] += step_delta_q16(sm->project_settings.camera.move_step_m_q16_16, -1);
            } else if (mapped == (uint32_t)SubstrateManager::EwMappedInputAction::MoveUp) {
                if (is_action && ev.pressed_u8) cam.camera_state.pos_xyz_q16_16[1] += step_delta_q16(sm->project_settings.camera.move_step_m_q16_16, +1);
            } else if (mapped == (uint32_t)SubstrateManager::EwMappedInputAction::MoveDown) {
                if (is_action && ev.pressed_u8) cam.camera_state.pos_xyz_q16_16[1] += step_delta_q16(sm->project_settings.camera.move_step_m_q16_16, -1);
            } else if (mapped == (uint32_t)SubstrateManager::EwMappedInputAction::ZoomIn) {
                if (is_action) {
                    if (ev.pressed_u8) cam.camera_state.focus_distance_m_q32_32 -= ((int64_t)sm->project_settings.camera.zoom_step_m_q16_16 << 16);
                } else {
                    // Axis value: sign determines direction.
                    if (ev.value_q16_16 > 0) cam.camera_state.focus_distance_m_q32_32 -= ((int64_t)sm->project_settings.camera.zoom_step_m_q16_16 << 16);
                    if (ev.value_q16_16 < 0) cam.camera_state.focus_distance_m_q32_32 += ((int64_t)sm->project_settings.camera.zoom_step_m_q16_16 << 16);
                }
            } else if (mapped == (uint32_t)SubstrateManager::EwMappedInputAction::ZoomOut) {
                if (is_action) {
                    if (ev.pressed_u8) cam.camera_state.focus_distance_m_q32_32 += ((int64_t)sm->project_settings.camera.zoom_step_m_q16_16 << 16);
                } else {
                    if (ev.value_q16_16 > 0) cam.camera_state.focus_distance_m_q32_32 += ((int64_t)sm->project_settings.camera.zoom_step_m_q16_16 << 16);
                    if (ev.value_q16_16 < 0) cam.camera_state.focus_distance_m_q32_32 -= ((int64_t)sm->project_settings.camera.zoom_step_m_q16_16 << 16);
                }
            } else if (mapped == (uint32_t)SubstrateManager::EwMappedInputAction::LookYaw) {
                // Axis event: value_q16_16 is delta units.
                if (!is_action) {
                    int32_t dtheta = (int32_t)(((int64_t)ev.value_q16_16 * (int64_t)sm->project_settings.camera.look_sens_rad_per_unit_q16_16) >> 16);
                    dtheta = qmul_q16(dtheta, b.scale_q16_16);
                    int32_t sh, ch;
                    sincos_half_angle_q16(dtheta, &sh, &ch);
                    int32_t dq[4] = {0, sh, 0, ch};
                    int32_t out[4];
                    quat_mul(dq, cam.camera_state.rot_quat_q16_16, out);
                    for (int i=0;i<4;++i) cam.camera_state.rot_quat_q16_16[i] = out[i];
                    quat_norm_approx(cam.camera_state.rot_quat_q16_16);
                }
            } else if (mapped == (uint32_t)SubstrateManager::EwMappedInputAction::LookPitch) {
                if (!is_action) {
                    int32_t dtheta = (int32_t)(((int64_t)ev.value_q16_16 * (int64_t)sm->project_settings.camera.look_sens_rad_per_unit_q16_16) >> 16);
                    dtheta = qmul_q16(dtheta, b.scale_q16_16);
                    int32_t sh, ch;
                    sincos_half_angle_q16(dtheta, &sh, &ch);
                    int32_t dq[4] = {sh, 0, 0, ch};
                    int32_t out[4];
                    quat_mul(dq, cam.camera_state.rot_quat_q16_16, out);
                    for (int i=0;i<4;++i) cam.camera_state.rot_quat_q16_16[i] = out[i];
                    quat_norm_approx(cam.camera_state.rot_quat_q16_16);
                }
            }
            return;
        }

        if (subop == 4u) {
            // Input binding set: validate and apply deterministically.
            if (sm->pending_binding_set.valid_u32 == 0u) return;
            const auto req = sm->pending_binding_set;
            sm->pending_binding_set.valid_u32 = 0u;

            // Basic validation: raw id must be non-zero.
            if (req.raw_id_u32 == 0u) return;

            // Clamp scale to a sane deterministic range [-16..16] in Q16.16.
            const int32_t scale = clamp_i32(req.scale_q16_16,
                                           (int32_t)(-16 * 65536),
                                           (int32_t)(16 * 65536));

            // Clamp mapped id to known enum range.
            const uint32_t mapped = req.mapped_u32;
            if (mapped > 4096u) return; // fail closed on nonsense ids

            auto upsert = [&](std::vector<SubstrateManager::EwInputBinding>& v) {
                bool found = false;
                for (auto& b : v) {
                    if (b.raw_id_u32 == req.raw_id_u32) {
                        b.mapped_u32 = mapped;
                        b.scale_q16_16 = scale;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    SubstrateManager::EwInputBinding b;
                    b.raw_id_u32 = req.raw_id_u32;
                    b.mapped_u32 = mapped;
                    b.scale_q16_16 = scale;
                    v.push_back(b);
                }
                auto cmp = [](const SubstrateManager::EwInputBinding& a, const SubstrateManager::EwInputBinding& b) {
                    if (a.raw_id_u32 != b.raw_id_u32) return a.raw_id_u32 < b.raw_id_u32;
                    return a.mapped_u32 < b.mapped_u32;
                };
                std::sort(v.begin(), v.end(), cmp);
            };

            if (req.is_axis_u8) upsert(sm->input_axis_bindings);
            else upsert(sm->input_action_bindings);

            sm->input_bindings_loaded = true;
            sm->input_bindings_dirty = true;
            return;
        }
    };

    ancilla_particle* tr = (!sm->ancilla.empty()) ? &sm->ancilla[0] : nullptr;

    for (size_t vi = 0; vi < views.size(); ++vi) {
        const uint8_t* b = views[vi].pkt->bytes;
        const uint32_t op_kind = ew_read_u32_le(b + 72);
        const uint32_t n_in = ew_read_u32_le(b + 80);
        const uint32_t n_out = ew_read_u32_le(b + 84);
        const uint32_t payload_bytes = ew_read_u32_le(b + 88);
        const uint8_t* p_in_lanes = b + 92;
        const uint8_t* p_out_lanes = p_in_lanes + (size_t)n_in * 72;
        const uint8_t* payload = p_out_lanes + (size_t)n_out * 72;

        const uint32_t expected = expected_payload_bytes(op_kind);
        if (expected == 0xFFFFFFFFu) continue;
        if (payload_bytes != expected) continue;

        double lane_tmp[9];
        auto lane_at = [&](const uint8_t* base, uint32_t idx)->EwLaneId9 {
            for (int k = 0; k < 9; ++k) lane_tmp[k] = ew_read_f64_le(base + (size_t)idx * 72 + (size_t)k * 8);
            return ew_lane_id9_from_e9_f64(lane_tmp);
        };

        if (op_kind == 0x00000001u) {
            if (n_in < 2 || n_out < 1) continue;
            EwLaneId9 in0 = lane_at(p_in_lanes, 0);
            EwLaneId9 in1 = lane_at(p_in_lanes, 1);
            EwLaneId9 out0 = lane_at(p_out_lanes, 0);
            ew_opk_text_eigen_encode(sm->op_lanes, in0, in1, out0, payload);
        } else if (op_kind == 0x00000002u) {
            if (n_in < 2 || n_out < 1) continue;
            EwLaneId9 in0 = lane_at(p_in_lanes, 0);
            EwLaneId9 in1 = lane_at(p_in_lanes, 1);
            EwLaneId9 out0 = lane_at(p_out_lanes, 0);
            ew_opk_aggregate_normalized_sum(sm->op_lanes, in0, in1, out0, payload);
        } else if (op_kind == 0x00000003u) {
            if (n_in < 2 || n_out < 1) continue;
            EwLaneId9 in0 = lane_at(p_in_lanes, 0);
            EwLaneId9 in1 = lane_at(p_in_lanes, 1);
            EwLaneId9 out0 = lane_at(p_out_lanes, 0);
            ew_opk_project_coh_dot(sm->op_lanes, tr, sm->ctx_snapshot, in0, in1, out0, payload);
        } else if (op_kind == 0x00000004u) {
            if (n_in < 3 || n_out < 2) continue;
            EwLaneId9 in_cur = lane_at(p_in_lanes, 0);
            EwLaneId9 in_cand = lane_at(p_in_lanes, 1);
            EwLaneId9 in_fail = lane_at(p_in_lanes, 2);
            EwLaneId9 out_s = lane_at(p_out_lanes, 0);
            EwLaneId9 out_fail = lane_at(p_out_lanes, 1);
            ew_opk_constrain_pi_g(sm->op_lanes, tr, sm->ctx_snapshot, in_cur, in_cand, in_fail, out_s, out_fail, payload);
        } else if (op_kind == 0x00000005u) {
            // Chain apply executes stable core templates only.
            ew_opk_chain_apply(sm->op_lanes, tr, sm->ctx_snapshot, p_in_lanes, n_in, p_out_lanes, n_out, payload);
        }
        else if (op_kind == 0x00000009u) {
            // Compute-bus dispatch: payload-only operator. Writes to anchors/state.
            // This is the hard gate that forbids direct CPU computation paths for
            // camera focus, settings application, and input mapping effects.
            opk_compute_bus_dispatch(payload);
        }
    }
}


void SubstrateManager::tick() {
    const uint64_t prev_tick_u64 = canonical_tick;

    // ------------------------------------------------------------------
    // Compute-bus submission helper (carrier-coded payload marker)
    // ------------------------------------------------------------------
    auto submit_compute_bus = [&](uint32_t subop_u32, uint64_t tick_u64) {
        // Build a tiny canonical request buffer and encode it through the
        // same SpiderCode4->carrier collapse path used by corpus ingestion.
        uint8_t req[32];
        for (int i=0;i<32;++i) req[i] = 0;
        // "CBUS" marker.
        req[0]='C'; req[1]='B'; req[2]='U'; req[3]='S';
        ge_wr_u32_le(req, 4, subop_u32);
        ge_wr_u64_le(req, 8, tick_u64);
        ge_wr_u64_le(req, 16, (uint64_t)camera_anchor_id_u32);

        SpiderCode4 sc{};
        if (!ew_encode_spidercode4_from_bytes_chunked_cuda(req, sizeof(req), 16, &sc)) {
            return; // fail closed deterministically
        }

        // Map to 4 frequency components and collapse.
        std::vector<EwFreqComponentQ32_32> comps;
        comps.reserve(4u);
        auto push_comp = [&](int32_t f_code, int32_t a_code, int32_t phi_code) {
            EwFreqComponentQ32_32 c{};
            c.f_turns_q32_32 = (int64_t)f_code << 16;
            c.a_q32_32 = (int64_t)a_code << 16;
            c.phi_turns_q32_32 = (int64_t)phi_code << 16;
            comps.push_back(c);
        };
        push_comp(sc.f_code, (int32_t)sc.a_code, (int32_t)sc.v_code);
        push_comp((int32_t)sc.v_code, (int32_t)sc.i_code, sc.f_code);
        push_comp((int32_t)sc.i_code, sc.f_code, (int32_t)sc.a_code);
        push_comp((int32_t)sc.a_code, (int32_t)sc.v_code, (int32_t)sc.i_code);

        EwCarrierWaveQ32_32 carrier{};
        if (!ew_collapse_frequency_components_q32_32(comps, carrier)) {
            return;
        }

        uint8_t payload72[72];
        for (int i=0;i<72;++i) payload72[i]=0;
        ge_wr_u32_le(payload72, 0, subop_u32);
        ge_wr_i32_le(payload72, 4, sc.f_code);
        ge_wr_u32_le(payload72, 8, (uint32_t)sc.a_code);
        ge_wr_u32_le(payload72, 12, (uint32_t)sc.v_code);
        ge_wr_u32_le(payload72, 16, (uint32_t)sc.i_code);
        ge_wr_u64_le(payload72, 20, (uint64_t)carrier.f_carrier_turns_q32_32);
        ge_wr_u64_le(payload72, 28, (uint64_t)carrier.A_carrier_q32_32);
        ge_wr_u64_le(payload72, 36, (uint64_t)carrier.phi_carrier_turns_q32_32);
        ge_wr_u32_le(payload72, 44, carrier.component_count_u32);

        std::vector<uint64_t> args;
        args.reserve(1 + (72u/8u));
        args.push_back((uint64_t)canonical_tick);
        for (uint32_t off = 0; off < 72u; off += 8u) {
            uint64_t u = 0;
            for (int k = 0; k < 8; ++k) u |= ((uint64_t)payload72[off + (uint32_t)k]) << (8*k);
            args.push_back(u);
        }
        (void)ew_eq_exec_packet(this, 0x00000009u, args);
    };

    // Loop playback: restore snapshot before any evolution so the world repeats deterministically.
    if (sim_play_loop_enabled && sim_snapshot_valid) {
        reservoir = sim_snapshot.reservoir;
        boundary_scale_q32_32 = sim_snapshot.boundary_scale_q32_32;
        anchors = sim_snapshot.anchors;
        ancilla = sim_snapshot.ancilla;
        nbody_state = sim_snapshot.nbody_state;
        lanes = sim_snapshot.lanes;
        object_store = sim_snapshot.object_store;
        materials_calib_done = sim_snapshot.materials_calib_done;
        // Keep topology vectors sized.
        const uint32_t need = (uint32_t)anchors.size();
        if (redirect_to.size() < need) redirect_to.resize(need, 0u);
        if (split_child_a.size() < need) split_child_a.resize(need, 0u);
        if (split_child_b.size() < need) split_child_b.resize(need, 0u);
        next_anchor_id_u32 = (uint32_t)anchors.size();
        ge_rebuild_cached_anchor_ids(*this);
        ai_substrate_drive.clear();
        ai_gpu_spider_encode_count_u64 = 0u;
        ai_api_decode_count_u64 = 0u;
        ai_last_api_decode_request_id_u64 = 0u;
        ai_last_api_decode_http_status_s32 = -1;
        ai_last_api_decode_body_bytes_u32 = 0u;
    }

    // ------------------------------------------------------------------
    // Consume control packets (input/editor/UI) deterministically.
    // These packets are the only permitted control surface.
    // ------------------------------------------------------------------
    {
        EwControlPacket p;
        while (control_packet_pop(p)) {
            if (p.kind == EwControlPacketKind::CameraSet || p.kind == EwControlPacketKind::CameraSetFocusMode) {
                if (camera_anchor_id_u32 != 0u && camera_anchor_id_u32 < anchors.size()) {
                    Anchor& cam = anchors[camera_anchor_id_u32];
                    if (cam.kind_u32 == EW_ANCHOR_KIND_CAMERA) {
                        cam.camera_state.focus_mode_u8 = p.payload.camera_set.focus_mode_u8;
                        cam.camera_state.manual_focus_distance_m_q32_32 = p.payload.camera_set.manual_focus_distance_m_q32_32;
                        cam.camera_state.focal_length_mm_q16_16 = p.payload.camera_set.focal_length_mm_q16_16;
                        cam.camera_state.aperture_f_q16_16 = p.payload.camera_set.aperture_f_q16_16;
                        cam.camera_state.exposure_ev_q16_16 = p.payload.camera_set.exposure_ev_q16_16;
                        cam.camera_state.pos_xyz_q16_16[0] = p.payload.camera_set.pos_xyz_q16_16[0];
                        cam.camera_state.pos_xyz_q16_16[1] = p.payload.camera_set.pos_xyz_q16_16[1];
                        cam.camera_state.pos_xyz_q16_16[2] = p.payload.camera_set.pos_xyz_q16_16[2];
                        for (int i = 0; i < 4; ++i) cam.camera_state.rot_quat_q16_16[i] = p.payload.camera_set.rot_quat_q16_16[i];
                    }
                }
            } else if (p.kind == EwControlPacketKind::ProjectSettingsSet) {
                // HARD RULE: no validation/clamping/mutation here.
                // Stage the request and submit a compute-bus op; the substrate
                // applies settings deterministically inside op dispatch.
                pending_settings_set.valid_u32 = 1u;
                pending_settings_set.tick_u64 = canonical_tick;
                pending_settings_set.source_u16 = p.source_u16;
                pending_settings_set.tab_u32 = p.payload.settings_set.tab_u32;
                pending_settings_set.field_u32 = p.payload.settings_set.field_u32;
                pending_settings_set.value_q32_32 = p.payload.settings_set.value_q32_32;
                submit_compute_bus(2u, canonical_tick);
            } else if (p.kind == EwControlPacketKind::InputAction) {
                // Raw input event only. Substrate maps it into anchor effects.
                pending_input_event.valid_u32 = 1u;
                pending_input_event.tick_u64 = canonical_tick;
                pending_input_event.source_u16 = p.source_u16;
                pending_input_event.kind_u16 = 1u;
                pending_input_event.id_u32 = p.payload.input_action.action_id_u32;
                pending_input_event.value_q16_16 = p.payload.input_action.pressed_u8 ? (int32_t)(1 * 65536) : 0;
                pending_input_event.pressed_u8 = p.payload.input_action.pressed_u8;
                submit_compute_bus(3u, canonical_tick);
            } else if (p.kind == EwControlPacketKind::InputAxis) {
                pending_input_event.valid_u32 = 1u;
                pending_input_event.tick_u64 = canonical_tick;
                pending_input_event.source_u16 = p.source_u16;
                pending_input_event.kind_u16 = 2u;
                pending_input_event.id_u32 = p.payload.input_axis.axis_id_u32;
                pending_input_event.value_q16_16 = p.payload.input_axis.value_q16_16;
                pending_input_event.pressed_u8 = 0;
                submit_compute_bus(3u, canonical_tick);
            } else if (p.kind == EwControlPacketKind::InputBindingSet) {
                // Stage binding set and apply via compute-bus. UI does not
                // validate; substrate validates/clamps deterministically.
                pending_binding_set.valid_u32 = 1u;
                pending_binding_set.tick_u64 = canonical_tick;
                pending_binding_set.is_axis_u8 = p.payload.binding_set.is_axis_u8;
                pending_binding_set.raw_id_u32 = p.payload.binding_set.raw_id_u32;
                pending_binding_set.mapped_u32 = p.payload.binding_set.mapped_u32;
                pending_binding_set.scale_q16_16 = p.payload.binding_set.scale_q16_16;
                submit_compute_bus(4u, canonical_tick);
            } else if (p.kind == EwControlPacketKind::InputBindingsReload) {
                // Reload from file is a projection/I/O step, not computation.
                input_bindings_loaded = false;
            } else if (p.kind == EwControlPacketKind::AllowlistReload) {
                const bool ok = corpus_allowlist_load_user_file_if_present();
                emit_ui_line(ok ? "ALLOWLIST_RELOADED" : "ALLOWLIST_RELOAD_NOFILE");
            } else if (p.kind == EwControlPacketKind::ObjectRegister) {
                // Authoritative object transforms live in object anchors.
                const uint64_t obj_id = p.payload.object_register.object_id_u64;
                uint32_t found = 0u;
                for (uint32_t ai = 1u; ai < (uint32_t)anchors.size(); ++ai) {
                    if (anchors[ai].kind_u32 == EW_ANCHOR_KIND_OBJECT && anchors[ai].object_id_u64 == obj_id) {
                        found = ai;
                        break;
                    }
                }
                if (found == 0u) {
                    Anchor a(next_anchor_id_u32);
                    a.kind_u32 = EW_ANCHOR_KIND_OBJECT;
                    a.object_id_u64 = obj_id;
                    anchors.push_back(a);
                    found = next_anchor_id_u32;
                    next_anchor_id_u32 = (uint32_t)anchors.size();
                    if (redirect_to.size() < anchors.size()) redirect_to.resize(anchors.size(), 0u);
                    if (split_child_a.size() < anchors.size()) split_child_a.resize(anchors.size(), 0u);
                    if (split_child_b.size() < anchors.size()) split_child_b.resize(anchors.size(), 0u);
                }
                if (found < anchors.size()) {
                    Anchor& a = anchors[found];
                    for (int i = 0; i < 3; ++i) a.object_state.pos_q16_16[i] = p.payload.object_register.pos_q16_16[i];
                    for (int i = 0; i < 4; ++i) a.object_state.rot_quat_q16_16[i] = p.payload.object_register.rot_quat_q16_16[i];
                }
            } else if (p.kind == EwControlPacketKind::ObjectSetTransform) {
                const uint64_t obj_id = p.payload.object_xform.object_id_u64;
                for (uint32_t ai = 1u; ai < (uint32_t)anchors.size(); ++ai) {
                    if (anchors[ai].kind_u32 == EW_ANCHOR_KIND_OBJECT && anchors[ai].object_id_u64 == obj_id) {
                        Anchor& a = anchors[ai];
                        for (int i = 0; i < 3; ++i) a.object_state.pos_q16_16[i] = p.payload.object_xform.pos_q16_16[i];
                        for (int i = 0; i < 4; ++i) a.object_state.rot_quat_q16_16[i] = p.payload.object_xform.rot_quat_q16_16[i];
                        break;
                    }
                }
            } else if (p.kind == EwControlPacketKind::EditorSetSelection ||
                       p.kind == EwControlPacketKind::EditorToggleSelection ||
                       p.kind == EwControlPacketKind::EditorSetGizmo ||
                       p.kind == EwControlPacketKind::EditorSetSnap ||
                       p.kind == EwControlPacketKind::EditorSetAxisConstraint ||
                       p.kind == EwControlPacketKind::EditorCommitTransformTxn ||
                       p.kind == EwControlPacketKind::EditorUndo ||
                       p.kind == EwControlPacketKind::EditorRedo) {
                // Editor interaction contract lives in substrate as an editor anchor.
                // This is authoritative for selection/gizmo/snap settings.
                const uint64_t kEditorObjectId = 0x315F524F54494445ULL; // 'EDITOR_1' little-endian

                uint32_t editor_anchor_id = 0u;
                bool editor_anchor_found = false;
                for (uint32_t i = 0u; i < (uint32_t)anchors.size(); ++i) {
                    if (anchors[i].kind_u32 == EW_ANCHOR_KIND_EDITOR && anchors[i].object_id_u64 == kEditorObjectId) {
                        editor_anchor_id = i;
                        editor_anchor_found = true;
                        break;
                    }
                }
                if (!editor_anchor_found) {
                    Anchor a(next_anchor_id_u32);
                    a.kind_u32 = EW_ANCHOR_KIND_EDITOR;
                    a.object_id_u64 = kEditorObjectId;
                    // Payload already zeroed in constructor.
                    anchors.push_back(a);
                    next_anchor_id_u32 = (uint32_t)anchors.size();
                    const uint32_t need = (uint32_t)anchors.size();
                    if (redirect_to.size() < need) redirect_to.resize(need, 0u);
                    if (split_child_a.size() < need) split_child_a.resize(need, 0u);
                    if (split_child_b.size() < need) split_child_b.resize(need, 0u);
                    editor_anchor_id = (uint32_t)anchors.size() - 1u;
                }

                if (editor_anchor_id < anchors.size()) {
                    Anchor& ed = anchors[editor_anchor_id];
                    if (ed.kind_u32 == EW_ANCHOR_KIND_EDITOR) {
                        
if (p.kind == EwControlPacketKind::EditorSetSelection) {
    const uint64_t oid = p.payload.editor_set_selection.selected_object_id_u64;
    ed.editor_state.selected_object_id_u64 = oid;
    // Replace selection list with [oid] (or clear if oid==0).
    ed.editor_state.selection_count_u32 = 0u;
    for (uint32_t k = 0u; k < EW_EDITOR_MAX_SELECTION; ++k) ed.editor_state.selection_object_id_u64[k] = 0u;
    if (oid != 0u) {
        ed.editor_state.selection_object_id_u64[0] = oid;
        ed.editor_state.selection_count_u32 = 1u;
    }

} else if (p.kind == EwControlPacketKind::PlanetRegister) {
                const uint64_t obj_id = p.payload.planet_register.object_id_u64;
                const uint64_t parent_obj_id = p.payload.planet_register.parent_object_id_u64;
                uint32_t found = 0u;
                for (uint32_t ai = 1u; ai < (uint32_t)anchors.size(); ++ai) {
                    if (anchors[ai].kind_u32 == EW_ANCHOR_KIND_PLANET && anchors[ai].object_id_u64 == obj_id) {
                        found = ai;
                        break;
                    }
                }
                if (found == 0u) {
                    Anchor a(next_anchor_id_u32);
                    a.kind_u32 = EW_ANCHOR_KIND_PLANET;
                    a.object_id_u64 = obj_id;
                    anchors.push_back(a);
                    found = next_anchor_id_u32;
                    next_anchor_id_u32 = (uint32_t)anchors.size();
                    if (redirect_to.size() < anchors.size()) redirect_to.resize(anchors.size(), 0u);
                    if (split_child_a.size() < anchors.size()) split_child_a.resize(anchors.size(), 0u);
                    if (split_child_b.size() < anchors.size()) split_child_b.resize(anchors.size(), 0u);
                }

                // Resolve parent anchor id from parent object id.
                uint32_t parent_anchor_id = 0u;
                if (parent_obj_id != 0u) {
                    for (uint32_t ai = 1u; ai < (uint32_t)anchors.size(); ++ai) {
                        if (anchors[ai].kind_u32 == EW_ANCHOR_KIND_PLANET && anchors[ai].object_id_u64 == parent_obj_id) {
                            parent_anchor_id = ai;
                            break;
                        }
                    }
                }

                if (found < anchors.size()) {
                    Anchor& a = anchors[found];
                    EwPlanetAnchorState& ps = a.planet_state;
                    for (int i = 0; i < 3; ++i) ps.pos_q16_16[i] = p.payload.planet_register.pos_q16_16[i];
                    for (int i = 0; i < 3; ++i) ps.vel_q16_16[i] = p.payload.planet_register.vel_q16_16[i];
                    ps.radius_m_q16_16 = p.payload.planet_register.radius_m_q16_16;
                    ps.mass_kg_q16_16 = p.payload.planet_register.mass_kg_q16_16;
                    ps.parent_anchor_id_u32 = parent_anchor_id;
                    ps.orbit_radius_m_q32_32 = p.payload.planet_register.orbit_radius_m_q32_32;
                    ps.orbit_omega_turns_per_sec_q32_32 = p.payload.planet_register.orbit_omega_turns_per_sec_q32_32;
                    ps.orbit_phase_turns_q32_32 = p.payload.planet_register.orbit_phase_turns_q32_32;
                    ps.albedo_rgba8 = p.payload.planet_register.albedo_rgba8;
                    ps.atmosphere_rgba8 = p.payload.planet_register.atmosphere_rgba8;
                    ps.atmosphere_thickness_m_q16_16 = p.payload.planet_register.atmosphere_thickness_m_q16_16;
                    ps.emissive_q16_16 = p.payload.planet_register.emissive_q16_16;
                }
            }
        }
    }
    }
    }

    // Planet ancilla update (cosmological bodies). Must run inside substrate.
    // Canonical N-body update (celestial dynamics) projected into planet anchors.
    genesis::ew_nbody_tick(this);

    genesis::ew_tick_planet_ancilla(this);
    // Optional live crawler: emits observations into crawler queue (http-only).
    if (crawler_enable_live_u32 != 0u && crawler_live_consent_required_u32 == 0u) {
        live_crawler.enabled = true;
        live_crawler.consent_granted = true;
    }
    live_crawler.tick(this, domain_policies, &rate_limiter);

    // Spec Section 5: crawler/encoder run inside the substrate and may
    // admit pulses into the inbound queue under bounded budget.
    crawler.tick(this);
    repo_reader.tick(this, repo_reader_files_per_tick_u32, repo_reader_bytes_per_file_u32);

    // Stage-0 math foundations run in parallel with language. This loop
    // generates measurable lattice-based experiments (graphs / precedence
    // visualization) and updates math checkpoint metrics deterministically.
    math_foundation.tick(this);

    // ------------------------------------------------------------------
    // Learning checkpoint gate (honesty gate)
    //
    // Uses the full per-request compute budget over the one-second request
    // window, without early-stopping at tolerance. Acceptance occurs only
    // at the end of the window if the best fit is within 6%.
    // ------------------------------------------------------------------
    learning_gate.tick(this);

    // ------------------------------------------------------------------
    // Global coherence update (deterministic)
    //
    // Global coherence is the only gate for AI output. It aggregates:
    //  - language/intention learning progress (stage0 checkpoints)
    //  - physics substrate coherence (lattice probe device coherence)
    //  - crawl evidence stability (manifest records present)
    //  - experiment/metric acceptance stability (learning gate)
    // ------------------------------------------------------------------
    {
        // Language coherence: fraction of required stage0 bits completed.
        uint16_t lang_q15 = 0;
        const uint64_t need0 = learning_stage_required_mask_u64[0];
        const uint64_t have0 = learning_stage_completed_mask_u64[0];
        if (need0 != 0ULL) {
            const uint32_t need_cnt = (uint32_t)__builtin_popcountll(need0);
            const uint32_t have_cnt = (uint32_t)__builtin_popcountll(have0 & need0);
            if (need_cnt > 0) {
                lang_q15 = (uint16_t)((int32_t)32768 * (int32_t)have_cnt / (int32_t)need_cnt);
            }
        }

        // Physics coherence: a deterministic availability proxy.
        // A full device-side coherence reduction is bound by the learning
        // probe metric path when CUDA is enabled.
        uint16_t phys_q15 = lattice_probe_gpu_[0] ? (uint16_t)16384 : 0;

        // Fold coherence bus phys signal (derived from mass-leakage packets) into the physics coherence channel.
        if (coherence_bus_anchor_id_u32 != 0u && coherence_bus_anchor_id_u32 < anchors.size()) {
            const Anchor& b = anchors[coherence_bus_anchor_id_u32];
            if (b.kind_u32 == EW_ANCHOR_KIND_COHERENCE_BUS) {
                if (b.coherence_bus_state.phys_coherence_q15 > phys_q15) phys_q15 = b.coherence_bus_state.phys_coherence_q15;
            }
        }

        // Crawl coherence: whether we have any admitted records in manifest.
        uint16_t crawl_q15 = 0;
        if (manifest_records.size() >= 4) crawl_q15 = 16384;
        if (manifest_records.size() >= 16) crawl_q15 = 24576;
        if (manifest_records.size() >= 64) crawl_q15 = 32768;

        // Experiment coherence: acceptance ratio over recent tasks (bounded window).
        uint16_t exp_q15 = 0;
        {
            const auto& done = learning_gate.registry().completed();
            const size_t win = 32;
            size_t a = (done.size() > win) ? (done.size() - win) : 0;
            uint32_t tot = 0, acc = 0;
            for (size_t i = a; i < done.size(); ++i) {
                tot++;
                if (done[i].accepted) acc++;
            }
            if (tot > 0) exp_q15 = (uint16_t)((int32_t)32768 * (int32_t)acc / (int32_t)tot);
        }

        global_coherence.update(canonical_tick_u64(), lang_q15, phys_q15, crawl_q15, exp_q15);
        // Leak slowly each tick to prevent runaway. 1/4096 per tick.
        global_coherence.leak(canonical_tick_u64(), (uint16_t)8);

        // Read-only diagnostics: emit a compact spectral/coherence status line occasionally.
        if ((canonical_tick_u64() % 360u) == 0u) {
            uint16_t se_m = 0, se_p = 0, sl = 0, bp = 0;
            uint8_t cb = 0;
            if (spectral_field_anchor_id_u32 != 0u && spectral_field_anchor_id_u32 < anchors.size()) {
                const Anchor& sa = anchors[spectral_field_anchor_id_u32];
                if (sa.kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) {
                    se_m = sa.spectral_field_state.energy_mean_q15;
                    se_p = sa.spectral_field_state.energy_peak_q15;
                    sl = sa.spectral_field_state.leakage_abs_q15;
                    cb = sa.spectral_field_state.leakage_band_u8;
                }
            }
            if (coherence_bus_anchor_id_u32 != 0u && coherence_bus_anchor_id_u32 < anchors.size()) {
                const Anchor& ba = anchors[coherence_bus_anchor_id_u32];
                if (ba.kind_u32 == EW_ANCHOR_KIND_COHERENCE_BUS) {
                    bp = ba.coherence_bus_state.phys_coherence_q15;
                }
            }
            emit_ui_line(std::string("SPECTRAL_STATUS ") +
                         "Emean_q15=" + std::to_string((uint32_t)se_m) +
                         " Epeak_q15=" + std::to_string((uint32_t)se_p) +
                         " LeakAbs_q15=" + std::to_string((uint32_t)sl) +
                         " Band=" + std::to_string((uint32_t)cb) +
                         " BusPhys_q15=" + std::to_string((uint32_t)bp));
        }
    }

    // Canonical object ancilla update (bounded fan-out) BEFORE physics evolution.
    // This is the only authority path for object registry/coupling updates.
    genesis::EwObjectAncilla::tick_object_updates(this, 2u);

    // ------------------------------------------------------------------
    // Curriculum stage advancement (deterministic)
    //
    // Process newly completed metric tasks and advance the curriculum stage
    // only when enough measurable checkpoints have been accepted.
    //
    // Stage map (canonical):
    //   stage0: Language foundations (dictionary/thesaurus/encyclopedia/speech)
    //   stage1: Physics & quantum physics
    //   stage2: Orbitals/atoms/bonds + chemistry
    //   stage3: Materials / physical sciences
    //   stage4: Cosmology / atmospheres
    //   stage5: Biology (deferred; unlocked after stage4)
    // ------------------------------------------------------------------
    {
        const auto& done = learning_gate.registry().completed();
        while (learning_completed_cursor_u32 < (uint32_t)done.size()) {
            const genesis::MetricTask& t = done[(size_t)learning_completed_cursor_u32++];
            if (!t.accepted) continue;


            // Phase-amplitude current: accepted measurable results reinforce region current.
            phase_current.on_activation(genesis::EwPhaseCurrent::footprint_from_metric(canonical_tick, (uint32_t)t.target.kind));

            // Map accepted metric kind -> curriculum stage bucket deterministically.
            // Stage buckets align with GENESIS_CURRICULUM_STAGE_COUNT.
            uint32_t stage_bucket = 0xFFFFFFFFu;
            const uint32_t kid = (uint32_t)t.target.kind;
            if (kid >= 100u && kid < 110u) {
                stage_bucket = 0u; // Language
            } else if (kid >= 10u && kid < 20u) {
                stage_bucket = 1u; // QM/Physics
            } else if (kid >= 20u && kid < 40u) {
                stage_bucket = 2u; // Orbitals/Atoms/Bonds + Chemistry
            } else if (kid >= 40u && kid < 50u) {
                stage_bucket = 3u; // Materials
            } else if (kid >= 50u && kid < 60u) {
                stage_bucket = 4u; // Cosmology/Atmos
            } else if (kid >= 60u && kid < 70u) {
                stage_bucket = 5u; // Biology
            }

            if (stage_bucket < GENESIS_CURRICULUM_STAGE_COUNT) {                // Mark the required checkpoint bit for this stage.
                {
                    const uint32_t b = ((uint32_t)t.target.kind) & 63u;
                    learning_stage_completed_mask_u64[stage_bucket] |= (1ULL << b);
                }
            }

            // Materials calibration gate: once we have accepted at least one
            // materials measurable task (kind 40..49) at stage>=3, enable
            // object imports. No alternate pathways.
            if (!materials_calib_done) {
                if (kid >= 40u && kid < 50u) {
                    if (learning_curriculum_stage_u32 >= 3u) {
                        materials_calib_done = true;
                        materials_calib_done_tick_u64 = canonical_tick;
                        if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("MATERIALS_CALIB_DONE");
                    }
                }
            }
        }

        // Advance only forward, never backward.
        if (learning_curriculum_stage_u32 < 5u) {
            const uint32_t cur = learning_curriculum_stage_u32;
            const uint64_t need_mask = learning_stage_required_mask_u64[cur];
            const uint64_t have_mask = learning_stage_completed_mask_u64[cur];
            if (need_mask != 0ULL && (have_mask & need_mask) == need_mask) {
                learning_curriculum_stage_u32 = cur + 1u;
                if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("CURRICULUM_STAGE_ADVANCE");
            }
        }
    }

    // ------------------------------------------------------------------
    // Learning automation (artifact-driven, event-based)
    // ------------------------------------------------------------------
    learning_automation.init_once(this);
    learning_automation.tick(this);


// ------------------------------------------------------------------
// Phase-amplitude current actuation (bounded)
//
// Current accumulates from activation resonance and discharges into
// measurable substrate experiments (no hidden side effects).
// ------------------------------------------------------------------
{
    uint32_t top_keys[8] = {};
    uint16_t top_amp[8] = {};
    const uint32_t n = phase_current.top_regions(8u, top_keys, top_amp);

    // Hard caps: at most 1 experiment launch per tick.
    const uint16_t AMP_LAUNCH_THRESH_Q15 = (uint16_t)(32767 / 8); // 12.5%
    bool launched = false;

    for (uint32_t i = 0; i < n && !launched; ++i) {
        const uint32_t rk = top_keys[i];
        const uint16_t a = top_amp[i];
        if (a < AMP_LAUNCH_THRESH_Q15) continue;

        // Respect curriculum gates: require at least Stage 0 (language) for experiments.
        if (learning_curriculum_stage_u32 < 0u) break;

        // Map region key to a stable probe origin (small deterministic mapping).
        EigenWare::EwExperimentRequest req;
        req.name = "probe_isolate";
        req.micro_ticks_u32 = 64u;
        req.tag_render = false;

        req.origin_x_u32 = (rk      ) & 63u;
        req.origin_y_u32 = (rk >> 6 ) & 63u;
        req.origin_z_u32 = (rk >> 12) & 63u;

        // Pattern kind/radius derived from current bins via region state.
        const auto& rr = phase_current.region(rk);
        req.pattern_kind_u32 = (uint32_t)(1u + (rr.ring_bins_u16[0] % 3u)); // 1..3
        req.pattern_radius_u32 = (uint32_t)(2u + (rr.ring_bins_u16[1] % 6u)); // 2..7

        // Deterministic amplitude from current.
        req.amp_q32_32 = (int64_t)((uint64_t)a << 17); // q15 -> q32.32-ish scale

        std::vector<std::vector<uint8_t>> packets;
        if (EigenWare::ew_compile_experiment_to_operator_packets(req, packets)) {
            for (size_t pi = 0; pi < packets.size(); ++pi) {
                submit_operator_packet_v1(packets[pi].data(), packets[pi].size());
            }
            // Discharge proportional to work launched.
            phase_current.discharge(rk, (uint16_t)(a >> 1));
            launched = true;

            // Observability
            emit_ui_line(std::string("PHASE_CURRENT_LAUNCH probe_isolate region=") + std::to_string((unsigned long long)rk) +
                         " amp_q15=" + std::to_string((unsigned long long)a));
        } else {
            // If compile fails, cool the region to avoid oscillation.
            phase_current.discharge(rk, (uint16_t)(a >> 2));
        }
    }
}

// ------------------------------------------------------------------
    // Global carrier hum: re-admit last tick's emitted carrier pulses as
    // inbound pulses for this tick.
    //
    // This is the deterministic global update mechanism: pulses are the
    // propagation substrate, and fan-out expands their effect across the
    // anchor network per tick.
    // ------------------------------------------------------------------
    if (!carrier_ring.empty()) {
        for (size_t i = 0; i < carrier_ring.size(); ++i) {
            Pulse p = carrier_ring[i];
            // Retarget to the current tick for deterministic ordering.
            p.tick = canonical_tick;
            // Keep causal_tag/profile as emitted; these are already bounded.
            inbound.push_back(p);
        }
    }


// ------------------------------------------------------------------
// Streaming ingest: move downloaded bytes directly into substrate storage
// without filesystem writes. Adapter may submit response bodies as chunks.
// ------------------------------------------------------------------
{
    // Headroom-scaled byte budget per tick.
    uint32_t budget = ingest_max_bytes_per_tick_u32;
    const int64_t h_q32_32 = ctx_snapshot.envelope_headroom_q32_32;
    if (h_q32_32 > 0 && h_q32_32 < (1LL << 32)) {
        budget = (uint32_t)(((__int128)budget * (__int128)h_q32_32) >> 32);
    }
    if (budget < 4096u) budget = 4096u;

    auto find_doc = [&](uint64_t req_id) -> EwExternalApiIngestDoc* {
        for (auto& d : external_api_ingest_docs) {
            if (d.request_id_u64 == req_id) return &d;
        }
        return nullptr;
    };

    auto hex_append = [&](std::string& out, const uint8_t* b, size_t n) {
        static const char* H = "0123456789ABCDEF";
        out.reserve(out.size() + n * 2);
        for (size_t i = 0; i < n; ++i) {
            const uint8_t x = b[i];
            out.push_back(H[(x >> 4) & 0xF]);
            out.push_back(H[x & 0xF]);
        }
    };

    while (!external_api_ingest_inbox.empty() && budget > 0u) {
        EwExternalApiIngestChunk ingest_chunk = external_api_ingest_inbox.front();
        external_api_ingest_inbox.pop_front();

        // Inbox no longer counts toward inflight cap.
        if (!ingest_chunk.bytes.empty()) {
            if (external_api_ingest_inflight_bytes_u64 >= (uint64_t)ingest_chunk.bytes.size()) {
                external_api_ingest_inflight_bytes_u64 -= (uint64_t)ingest_chunk.bytes.size();
            } else {
                external_api_ingest_inflight_bytes_u64 = 0;
            }
        }

        // Enforce per-tick byte budget deterministically.
        const uint32_t take = (uint32_t)((ingest_chunk.bytes.size() < (size_t)budget) ? ingest_chunk.bytes.size() : (size_t)budget);

        EwExternalApiIngestDoc* doc = find_doc(ingest_chunk.request_id_u64);
        if (!doc) {
            EwExternalApiIngestDoc d{};
            d.request_id_u64 = ingest_chunk.request_id_u64;
            d.tick_first_u64 = canonical_tick;
            d.http_status_s32 = ingest_chunk.http_status_s32;
            d.context_anchor_id_u32 = ingest_chunk.context_anchor_id_u32;
            d.crawler_anchor_id_u32 = ingest_chunk.crawler_anchor_id_u32;
            d.domain_anchor_id_u32 = ingest_chunk.domain_anchor_id_u32;
            d.final_seen = false;
            d.expected_next_offset_u32 = 0;
            external_api_ingest_docs.push_back(d);
            doc = &external_api_ingest_docs.back();
        }

        // Strict chunk ordering (fail closed): enforce expected offset.
        if (ingest_chunk.offset_u32 != doc->expected_next_offset_u32) {
            // Drop mismatched chunk and mark final to prevent partial noise injection.
            doc->final_seen = true;
        } else {
            if (take != 0u) {
                const size_t remain = (doc->bytes.size() < (size_t)ingest_max_doc_bytes_u32)
                    ? ((size_t)ingest_max_doc_bytes_u32 - doc->bytes.size())
                    : 0u;
                const size_t write_n = (take < (uint32_t)remain) ? (size_t)take : remain;
                if (write_n != 0u) {
                    doc->bytes.insert(doc->bytes.end(), ingest_chunk.bytes.begin(), ingest_chunk.bytes.begin() + (ptrdiff_t)write_n);
                    doc->expected_next_offset_u32 += (uint32_t)write_n;
                    budget -= (uint32_t)write_n;
                }
            }
        }

        if (ingest_chunk.is_final) doc->final_seen = true;

        // Finalize doc immediately when final_seen.
        if (doc->final_seen) {
            const uint32_t dom_id = (doc->domain_anchor_id_u32 != 0u) ? doc->domain_anchor_id_u32 : doc->crawler_anchor_id_u32;

            // Create a deterministic inspector artifact carrying a bounded hex payload.
            // This is substrate storage; hydrator may later project if commit_ready.
            EwInspectorArtifact a{};
            a.coord_coord9_u64 = (doc->request_id_u64 ^ (uint64_t)dom_id);
            a.kind_u32 = EW_ARTIFACT_TEXT;
            a.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".binhex";
            a.producer_operator_id_u32 = 0u;
            a.producer_tick_u64 = canonical_tick;
            a.coherence_q15 = 0;
            a.commit_ready = false;

            // Header line: request id + size + status
            a.payload = "DOC_BYTES ";
            a.payload += std::to_string((unsigned long long)doc->request_id_u64);
            a.payload += " ";
            a.payload += std::to_string((long long)doc->http_status_s32);
            a.payload += " ";
            a.payload += std::to_string((unsigned long long)doc->bytes.size());
            a.payload += "\n";

            // Append bounded bytes as hex. Keep bounded for storage safety.
            const size_t hex_cap = (doc->bytes.size() < (size_t)65536) ? doc->bytes.size() : (size_t)65536;
            hex_append(a.payload, doc->bytes.data(), hex_cap);
            a.payload += "\n";
            inspector_fields.upsert(a);

// Determine document kind (byte sniff) and generate deterministic derived artifacts.
const uint32_t doc_kind_u32 = ew_doc_kind_from_bytes(doc->bytes.data(), doc->bytes.size());

EwInspectorArtifact meta{};
meta.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x4D455441U; // 'META' fold
meta.kind_u32 = EW_ARTIFACT_TEXT;
meta.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".meta.txt";
meta.producer_operator_id_u32 = 0u;
meta.producer_tick_u64 = canonical_tick;
meta.coherence_q15 = 0;
meta.commit_ready = false;

meta.payload = "DOC_META ";
meta.payload += std::to_string((unsigned long long)doc->request_id_u64);
meta.payload += " KIND ";
meta.payload += std::to_string((unsigned long long)doc_kind_u32);
meta.payload += " STATUS ";
meta.payload += std::to_string((long long)doc->http_status_s32);
meta.payload += " BYTES ";
meta.payload += std::to_string((unsigned long long)doc->bytes.size());
meta.payload += "\n";
inspector_fields.upsert(meta);

// Build deterministic manifest record for provenance + license tagging.
EwInspectorArtifact mf{};
mf.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x4D414E46U;
mf.kind_u32 = EW_ARTIFACT_TEXT;
mf.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".manifest.txt";
mf.producer_operator_id_u32 = 0u;
mf.producer_tick_u64 = canonical_tick;
mf.coherence_q15 = 0;
mf.commit_ready = false;

std::string host_utf8;
std::string path_utf8;
uint32_t profile_u32 = 0u;
uint32_t stage_u32 = 0u;
for (size_t ii = 0; ii < external_api_inflight.size(); ++ii) {
    if (external_api_inflight[ii].request_id_u64 == doc->request_id_u64) {
        host_utf8 = external_api_inflight[ii].host_utf8;
        path_utf8 = external_api_inflight[ii].path_utf8;
        profile_u32 = external_api_inflight[ii].profile_u32;
        stage_u32 = external_api_inflight[ii].stage_u32;
        break;
    }
}

auto license_hint_from_text = [&](const std::string& t)->uint32_t {
    std::string lc = t;
    if (lc.size() > 4096) lc.resize(4096);
	    for (size_t i = 0; i < lc.size(); ++i) {
	        unsigned char b = (unsigned char)lc[i];
	        if (b >= (unsigned char)'A' && b <= (unsigned char)'Z') {
	            b = (unsigned char)(b - (unsigned char)'A' + (unsigned char)'a');
	        }
	        if ((b < 0x20 && b != (unsigned char)'\n' && b != (unsigned char)'\r' && b != (unsigned char)'\t') || b == 0x7F) {
	            b = (unsigned char)' ';
	        }
	        lc[i] = (char)b;
	    }
    if (lc.find("creative commons") != std::string::npos) return 2u;
    if (lc.find("cc-by") != std::string::npos) return 2u;
    if (lc.find("cc0") != std::string::npos) return 2u;
    if (lc.find("public domain") != std::string::npos) return 3u;
    return 0u;
};

uint32_t license_hint_u32 = 0u;
std::string lic_src;
ew_extract_utf8_view(doc->bytes.data(), (doc->bytes.size() < (size_t)262144) ? doc->bytes.size() : (size_t)262144, lic_src, 4096u);
if (!lic_src.empty()) license_hint_u32 = license_hint_from_text(lic_src);

bool trainable = true;
if (profile_u32 == 1u) { trainable = (license_hint_u32 != 0u); }

std::string org_utf8 = host_utf8;
if (host_utf8.find("uspto") != std::string::npos) org_utf8 = "USPTO";
else if (host_utf8.find("copyright") != std::string::npos) org_utf8 = "USCO";
else if (host_utf8.find("mheducation") != std::string::npos) org_utf8 = "McGrawHill";
else if (host_utf8.find("pearson") != std::string::npos) org_utf8 = "Pearson";

mf.payload = "MANIFEST ";
mf.payload += std::to_string((unsigned long long)doc->request_id_u64);
mf.payload += "\nDOMAIN ";
mf.payload += host_utf8;
mf.payload += "\nPATH ";
mf.payload += path_utf8;
mf.payload += "\nORG ";
mf.payload += org_utf8;
mf.payload += "\nKIND ";
mf.payload += std::to_string((unsigned long long)doc_kind_u32);
mf.payload += "\nSTAGE ";
mf.payload += std::to_string((unsigned long long)stage_u32);
mf.payload += "\nPROFILE ";
mf.payload += std::to_string((unsigned long long)profile_u32);
mf.payload += "\nLICENSE_HINT ";
mf.payload += std::to_string((unsigned long long)license_hint_u32);
mf.payload += "\nTRAINABLE ";
mf.payload += (trainable ? "1" : "0");
mf.payload += "\n";
inspector_fields.upsert(mf);

EwManifestRecord mr{};
mr.request_id_u64 = doc->request_id_u64;
mr.artifact_id_u64 = doc->request_id_u64 ^ (uint64_t)dom_id;
mr.tick_first_u64 = doc->tick_first_u64;
mr.tick_final_u64 = canonical_tick;
mr.context_anchor_id_u32 = doc->context_anchor_id_u32;
mr.crawler_anchor_id_u32 = doc->crawler_anchor_id_u32;
mr.domain_anchor_id_u32 = dom_id;
mr.domain_utf8 = host_utf8;
mr.path_utf8 = path_utf8;
mr.org_utf8 = org_utf8;
mr.doc_kind_u32 = doc_kind_u32;
mr.retrieval_method_u32 = stage_u32;
mr.trust_class_u32 = (org_utf8 == "USPTO" || org_utf8 == "USCO") ? 3u : 1u;
mr.license_hint_u32 = license_hint_u32;
mr.trainable_admitted = trainable;
if (!lic_src.empty()) {
    std::string s = lic_src;
    if (s.size() > 8192) s.resize(8192);
	    for (size_t i = 0; i < s.size(); ++i) {
	        unsigned char b = (unsigned char)s[i];
	        if (b >= (unsigned char)'A' && b <= (unsigned char)'Z') {
	            b = (unsigned char)(b - (unsigned char)'A' + (unsigned char)'a');
	        }
	        if ((b < 0x20 && b != (unsigned char)'\n' && b != (unsigned char)'\r' && b != (unsigned char)'\t') || b == 0x7F) {
	            b = (unsigned char)' ';
	        }
	        s[i] = (char)b;
	    }
    mr.sample_lc_utf8 = s;
}
manifest_records.push_back(mr);


std::string derived_text;
std::string derived_aux;

if (doc_kind_u32 == EW_DOC_KIND_PDF) {
    // PDF: extract ASCII runs as a deterministic text surrogate.
    ew_extract_utf8_view(doc->bytes.data(), (doc->bytes.size() < (size_t)262144) ? doc->bytes.size() : (size_t)262144, derived_text, 16384u);

    EwInspectorArtifact t{};
    t.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x50444654U; // 'PDFT'
    t.kind_u32 = EW_ARTIFACT_TEXT;
    t.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".pdf_text.txt";
    t.producer_operator_id_u32 = 0u;
    t.producer_tick_u64 = canonical_tick;
    t.coherence_q15 = 0;
    t.commit_ready = false;
    t.payload = derived_text.empty() ? "PDF_TEXT_EMPTY\n" : derived_text;
    inspector_fields.upsert(t);
} else if (doc_kind_u32 == EW_DOC_KIND_ZIP) {
    // ZIP: extract a manifest (filenames) deterministically.
    ew_zip_manifest_from_bytes(doc->bytes.data(), (doc->bytes.size() < (size_t)262144) ? doc->bytes.size() : (size_t)262144, derived_aux, 16384u);

    EwInspectorArtifact t{};
    t.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x5A49504DU; // 'ZIPM'
    t.kind_u32 = EW_ARTIFACT_TEXT;
    t.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".zip_manifest.txt";
    t.producer_operator_id_u32 = 0u;
    t.producer_tick_u64 = canonical_tick;
    t.coherence_q15 = 0;
    t.commit_ready = false;
    t.payload = derived_aux;
    inspector_fields.upsert(t);
    derived_text = derived_aux;
} else if (doc_kind_u32 == EW_DOC_KIND_JSON) {
    // JSON: extract key summary + ASCII runs.
    ew_json_key_summary_from_bytes(doc->bytes.data(), (doc->bytes.size() < (size_t)262144) ? doc->bytes.size() : (size_t)262144, derived_aux, 16384u);
    ew_extract_utf8_view(doc->bytes.data(), (doc->bytes.size() < (size_t)262144) ? doc->bytes.size() : (size_t)262144, derived_text, 16384u);

    EwInspectorArtifact k{};
    k.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x4A534B59U; // 'JSKY'
    k.kind_u32 = EW_ARTIFACT_TEXT;
    k.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".json_keys.txt";
    k.producer_operator_id_u32 = 0u;
    k.producer_tick_u64 = canonical_tick;
    k.coherence_q15 = 0;
    k.commit_ready = false;
    k.payload = derived_aux;
    inspector_fields.upsert(k);

    EwInspectorArtifact t{};
    t.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x4A534E54U; // 'JSNT'
    t.kind_u32 = EW_ARTIFACT_TEXT;
    t.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".json_text.txt";
    t.producer_operator_id_u32 = 0u;
    t.producer_tick_u64 = canonical_tick;
    t.coherence_q15 = 0;
    t.commit_ready = false;
    t.payload = derived_text.empty() ? "JSON_TEXT_EMPTY\n" : derived_text;
    inspector_fields.upsert(t);
} else if (doc_kind_u32 == EW_DOC_KIND_XML || doc_kind_u32 == EW_DOC_KIND_HTML) {
    // XML/HTML: tag summary + ASCII runs.
    ew_xml_tag_summary_from_bytes(doc->bytes.data(), (doc->bytes.size() < (size_t)262144) ? doc->bytes.size() : (size_t)262144, derived_aux, 16384u);
    ew_extract_utf8_view(doc->bytes.data(), (doc->bytes.size() < (size_t)262144) ? doc->bytes.size() : (size_t)262144, derived_text, 16384u);

    EwInspectorArtifact k{};
    k.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x584D4C54U; // 'XMLT'
    k.kind_u32 = EW_ARTIFACT_TEXT;
    k.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".xml_tags.txt";
    k.producer_operator_id_u32 = 0u;
    k.producer_tick_u64 = canonical_tick;
    k.coherence_q15 = 0;
    k.commit_ready = false;
    k.payload = derived_aux;
    inspector_fields.upsert(k);

    EwInspectorArtifact t{};
    t.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x584D4C58U; // 'XMLX'
    t.kind_u32 = EW_ARTIFACT_TEXT;
    t.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".xml_text.txt";
    t.producer_operator_id_u32 = 0u;
    t.producer_tick_u64 = canonical_tick;
    t.coherence_q15 = 0;
    t.commit_ready = false;
    t.payload = derived_text.empty() ? "XML_TEXT_EMPTY\n" : derived_text;
    inspector_fields.upsert(t);


// If this is a DuckDuckGo HTML search results page, extract result URLs/titles
// into a deterministic artifact and cache them for OPEN:<n>.
{
    const std::string host_lo = ew_utf8_lower_ascii_only_str(host_utf8);
    if (doc_kind_u32 == EW_DOC_KIND_HTML && host_lo.find("duckduckgo.com") != std::string::npos) {
        const bool is_search = (path_utf8.find("/html/") != std::string::npos) || (path_utf8.find("/html?") != std::string::npos);
        if (is_search) {
            std::vector<EwWebResultAscii> results;
            const size_t maxr = (websearch_max_results_u32 == 0u) ? (size_t)0 : (size_t)websearch_max_results_u32;
            ew_extract_ddg_search_results_from_html_bytes(doc->bytes.data(), doc->bytes.size(), maxr, results);
            if (!results.empty()) {
                last_websearch_urls_utf8.clear();
                last_websearch_titles_utf8.clear();
                for (size_t ri = 0; ri < results.size(); ++ri) {
                    last_websearch_urls_utf8.push_back(results[ri].url_utf8);
                    last_websearch_titles_utf8.push_back(results[ri].title_utf8);
                }
                last_websearch_results_request_id_u64 = doc->request_id_u64;

                EwInspectorArtifact sr{};
                sr.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x53455243U; // 'SERC'
                sr.kind_u32 = EW_ARTIFACT_TEXT;
                sr.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".search_results.txt";
                sr.producer_tick_u64 = canonical_tick;
                sr.coherence_q15 = 0;
                sr.commit_ready = false;
                sr.payload = "WEBSEARCH_RESULTS ";
                sr.payload += std::to_string((unsigned long long)doc->request_id_u64);
                sr.payload += " COUNT ";
                sr.payload += std::to_string((unsigned long long)results.size());
                sr.payload += "\n";
                for (size_t ri = 0; ri < results.size(); ++ri) {
                    sr.payload += "R";
                    sr.payload += std::to_string((unsigned long long)(ri + 1));
                    sr.payload += " ";
                    sr.payload += results[ri].url_utf8;
                    sr.payload += "\nTITLE ";
                    sr.payload += results[ri].title_utf8;
                    sr.payload += "\n";
                    if (!results[ri].snippet_utf8.empty()) {
                        sr.payload += "SNIP ";
                        sr.payload += results[ri].snippet_utf8;
                        sr.payload += "\n";
                    }
                }
                inspector_fields.upsert(sr);

                if (ui_out_q.size() < UI_OUT_CAP) {
                    std::string msg = "WEBSEARCH_RESULTS_READY ";
                    msg += std::to_string((unsigned long long)doc->request_id_u64);
                    msg += " ";
                    msg += std::to_string((unsigned long long)results.size());
                    ui_out_q.push_back(msg);
                }
                const uint32_t ui_n = websearch_ui_emit_n_u32;
                for (size_t ri = 0; ri < results.size() && ri < (size_t)ui_n; ++ri) {
                    if (ui_out_q.size() >= UI_OUT_CAP) break;
                    std::string line = "R";
                    line += std::to_string((unsigned long long)(ri + 1));
                    line += " ";
                    std::string title = results[ri].title_utf8;
                    if (title.size() > 64) title.resize(64);
                    line += title;
                    line += " | ";
                    std::string u = results[ri].url_utf8;
                    if (u.size() > 96) u.resize(96);
                    line += u;
                    ui_out_q.push_back(line);
                }

                uint32_t n_auto = websearch_auto_fetch_n_u32;
                if (n_auto > (uint32_t)results.size()) n_auto = (uint32_t)results.size();
                if (n_auto > 0u) {
                    for (uint32_t ri = 0; ri < n_auto; ++ri) {
                        EwExternalApiRequest req{};
                        req.tick_u64 = canonical_tick;
                        req.request_id_u64 = (canonical_tick << 32) ^ (external_api_request_seq_u64++);
                        req.method_utf8 = "GET";
                        req.url_utf8 = results[ri].url_utf8;
                        req.headers_kv_csv = "accept:text/html;user-agent:EigenWareBrowser";
                        req.response_cap_u32 = external_api_default_response_cap_u32;
                        external_api_pending.push_back(req);

                        EwInspectorArtifact rq{};
                        rq.coord_coord9_u64 = req.request_id_u64 ^ 0x57454246U; // 'WEBF'
                        rq.kind_u32 = EW_ARTIFACT_TEXT;
                        rq.rel_path = std::string("Draft Container/Corpus/web_request_") + std::to_string((unsigned long long)req.request_id_u64) + ".txt";
                        rq.producer_tick_u64 = canonical_tick;
                        rq.payload = std::string("GET ") + req.url_utf8 + "\n";
                        inspector_fields.upsert(rq);

                        if (ui_out_q.size() < UI_OUT_CAP) {
                            std::string tag = "WEBFETCH_SCHEDULED ";
                            tag += std::to_string((unsigned long long)req.request_id_u64);
                            ui_out_q.push_back(tag);
                        }
                    }
                }
            }
        }
    }
}

} else {
    // TEXT/UNKNOWN: keep ASCII runs as a deterministic surrogate.
    ew_extract_utf8_view(doc->bytes.data(), (doc->bytes.size() < (size_t)262144) ? doc->bytes.size() : (size_t)262144, derived_text, 16384u);
}



            
// If this appears to be source code by path extension, emit dependency and symbol streams (Blueprint CODE_BAND).
auto is_code_path = [&](const std::string& p)->bool {
    const std::string lc = ew_utf8_lower_ascii_only_str(p);
    const char* exts[] = {".cpp", ".hpp", ".h", ".c", ".cu", ".cmake", "cmakelists.txt"};
    for (size_t i = 0; i < sizeof(exts)/sizeof(exts[0]); ++i) {
        std::string e(exts[i]);
        if (lc.size() >= e.size() && lc.substr(lc.size()-e.size()) == e) return true;
    }
    return false;
};

if (!path_utf8.empty() && is_code_path(path_utf8) && !derived_text.empty()) {
    std::string deps;
    std::string syms;
    deps.reserve(2048);
    syms.reserve(2048);
    size_t start = 0;
    const size_t cap = (derived_text.size() < (size_t)65536) ? derived_text.size() : (size_t)65536;
    while (start < cap) {
        size_t end = derived_text.find("\n", start);
        if (end == std::string::npos || end > cap) end = cap;
        std::string line = derived_text.substr(start, end - start);
        size_t k = 0;
        while (k < line.size() && (line[k] == ' ' || line[k] == '\t' || line[k] == '\r')) k++;
        if (k) line = line.substr(k);
        if (line.rfind("#include", 0) == 0) { deps += line; deps += "\n"; }
        else if (line.rfind("class ", 0) == 0 || line.rfind("struct ", 0) == 0) { syms += line; syms += "\n"; }
        else {
            if (line.find('(') != std::string::npos && line.find(')') != std::string::npos && line.find(';') != std::string::npos) { syms += line; syms += "\n"; }
        }
        start = end + 1;
    }
    if (!deps.empty()) {
        EwInspectorArtifact d{};
        d.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x44455053U;
        d.kind_u32 = EW_ARTIFACT_TEXT;
        d.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".code_deps.txt";
        d.producer_tick_u64 = canonical_tick;
        d.payload = deps;
        inspector_fields.upsert(d);
    }
    if (!syms.empty()) {
        EwInspectorArtifact s{};
        s.coord_coord9_u64 = a.coord_coord9_u64 ^ 0x53594D53U;
        s.kind_u32 = EW_ARTIFACT_TEXT;
        s.rel_path = "Draft Container/Corpus/doc_" + std::to_string((unsigned long long)doc->request_id_u64) + ".code_symbols.txt";
        s.producer_tick_u64 = canonical_tick;
        s.payload = syms;
        inspector_fields.upsert(s);
    }
}

// Also emit a short sanitized UTF-8 snippet as a crawler observation for rapid ingestion.
            
// Also emit a short sanitized UTF-8 snippet as a crawler observation for rapid ingestion.
// Prefer derived text when available (PDF/JSON/XML summaries), otherwise use raw bytes.
const uint8_t* sn_b = nullptr;
size_t sn_n = 0u;
if (!lic_src.empty()) {
    sn_b = (const uint8_t*)derived_text.data();
    sn_n = derived_text.size();
} else if (!doc->bytes.empty()) {
    sn_b = doc->bytes.data();
    sn_n = doc->bytes.size();
}

if (sn_b != nullptr && sn_n != 0u) {
    const size_t cap = (sn_n < (size_t)2048) ? sn_n : (size_t)2048;
    std::string snippet;
    snippet.reserve(cap);
    for (size_t i = 0; i < cap; ++i) {
        unsigned char ch = (unsigned char)sn_b[i];
        if (ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 32 && ch <= 126)) snippet.push_back((char)ch);
        else snippet.push_back(' ');
    }
    crawler.enqueue_observation_utf8(
        doc->request_id_u64 ^ 0xD0C0U,
        dom_id,
        doc->crawler_anchor_id_u32,
        doc->context_anchor_id_u32,
        3u,
        3u,
        1u,
        0x41504932U,
        std::string("domain_") + std::to_string((unsigned long long)dom_id),
        std::string("api://") + std::to_string((unsigned long long)doc->request_id_u64),
        snippet
    );
}



// Deterministic stage expansion for streaming docs (same as submit_external_api_response).
EwExternalApiInflight infl{};
bool have_in = false;
for (auto it = external_api_inflight.begin(); it != external_api_inflight.end(); ++it) {
    if (it->request_id_u64 == doc->request_id_u64) {
        infl = *it;
        external_api_inflight.erase(it);
        have_in = true;
        break;
    }
}
if (have_in) {
    const uint32_t si = (infl.session_idx_u32 < EW_CRAWL_SESSION_MAX) ? infl.session_idx_u32 : 0u;
    EwCrawlSession& ss = crawl_sessions[si];
    if (ss.active && doc->http_status_s32 >= 200 && doc->http_status_s32 < 400) {
        const size_t cap_txt = (doc->bytes.size() < (size_t)8192) ? doc->bytes.size() : (size_t)8192;
        std::string text;
        text.reserve(cap_txt);
        for (size_t i = 0; i < cap_txt; ++i) {
            unsigned char ch = (unsigned char)doc->bytes[i];
            if (ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 32 && ch <= 126)) text.push_back((char)ch);
            else text.push_back(' ');
        }

        auto session_has_host = [&](const std::string& host)->uint32_t {
            for (size_t di = 0; di < ss.domain_map.size(); ++di) {
                if (ss.domain_map[di].domain_utf8 == host) return ss.domain_map[di].domain_anchor_id_u32;
            }
            return 0u;
        };
        auto seen_insert = [&](const std::string& key)->bool {
            for (size_t k = 0; k < ss.seen_url_keys.size(); ++k) if (ss.seen_url_keys[k] == key) return false;
            if (ss.seen_url_keys.size() < 4096) ss.seen_url_keys.push_back(key);
            return true;
        };
        auto push_front = [&](uint32_t stage_u32, const std::string& host, const std::string& path) {
            if (host.empty()) return;
            if (path.empty() || path[0] != '/') return;
            if (session_has_host(host) == 0u) return;
            const std::string key = std::string("https://") + host + path;
            if (!seen_insert(key)) return;
            EwCorpusCrawlTarget t{};
            t.lane_u32 = 0;
            t.stage_u32 = stage_u32;
            t.profile_u32 = ss.profile_u32;
            t.host_utf8 = host;
            t.path_utf8 = path;
            ss.q.push_front(t);
        };
        auto push_back = [&](uint32_t stage_u32, const std::string& host, const std::string& path) {
            if (host.empty()) return;
            if (path.empty() || path[0] != '/') return;
            if (session_has_host(host) == 0u) return;
            const std::string key = std::string("https://") + host + path;
            if (!seen_insert(key)) return;
            EwCorpusCrawlTarget t{};
            t.lane_u32 = 0;
            t.stage_u32 = stage_u32;
            t.profile_u32 = ss.profile_u32;
            t.host_utf8 = host;
            t.path_utf8 = path;
            ss.q.push_back(t);
        };

        const std::string lo = ew_utf8_lower_ascii_only_str(text);

        // Stage-0 parallel math learning: observe Khan Academy math crawl text
        // deterministically (coverage checkpoint).
        math_foundation.observe_crawl_text_khan_math(infl.host_utf8, infl.path_utf8, text);

        if (infl.stage_u32 == 0u) {
            push_back(1u, infl.host_utf8, "/");
            std::vector<std::string> smaps;
            ew_extract_sitemaps_from_robots(lo, 16, smaps);
            for (size_t k = 0; k < smaps.size(); ++k) {
                std::string h, p;
                ew_parse_url_host_path_utf8(smaps[k], h, p);
                if (!h.empty() && !p.empty() && p[0] == '/') push_front(2u, h, p);
            }
        } else if (infl.stage_u32 == 1u) {
            const bool looks_html = (lo.find("<html") != std::string::npos) || (lo.find("href") != std::string::npos);
            if (looks_html) {
                std::vector<std::string> links;
                ew_extract_download_links_from_html(lo, 32, links);
                for (size_t r = links.size(); r > 0; --r) {
                    const std::string u = links[r - 1];
                    if (ew_starts_with_utf8_ascii_prefix(u, "http://") || ew_starts_with_utf8_ascii_prefix(u, "https://")) {
                        std::string h, p;
                        ew_parse_url_host_path_utf8(u, h, p);
                        if (ew_has_download_ext(p)) push_front(3u, h, p);
                    } else {
                        if (!u.empty() && u[0] == '/' && ew_has_download_ext(u)) push_front(3u, infl.host_utf8, u);
                    }
                }
            }
        } else if (infl.stage_u32 == 2u) {
            std::vector<std::string> locs;
            ew_extract_loc_urls_from_xml(lo, 64, locs);
            for (size_t r = locs.size(); r > 0; --r) {
                std::string h, p;
                ew_parse_url_host_path_utf8(locs[r - 1], h, p);
                if (ew_has_download_ext(p)) push_front(3u, h, p);
            }
        }

        // License gating: mark trainable for publisher profile only if explicit marker present.
        const bool license_ok = (ss.profile_u32 != 1u) ? true : ew_license_marker_ok_bounded(doc->bytes.data(), doc->bytes.size());
        if (!license_ok) {
            // Emit a deterministic note to UI for visibility (no blocking of storage).
            if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("LICENSE_GATE_BLOCK_TRAINABLE");
        }
    }
}

            // Remove doc from inflight list deterministically.
            for (auto it = external_api_ingest_docs.begin(); it != external_api_ingest_docs.end(); ++it) {
                if (it->request_id_u64 == doc->request_id_u64) {
                    external_api_ingest_docs.erase(it);
                    break;
                }
            }
        }
    }
}

    // Spec/Blueprint: neural phase dynamics controller.
    // Pre-tick control emits only standard pulses and bounded frame shifts.

    // Camera anchor tick (read-only sampling; no world impulses).
    if (camera_anchor_id_u32 != 0u && camera_anchor_id_u32 < anchors.size()) {
        ge_camera_anchor_tick(*this, anchors[camera_anchor_id_u32], canonical_tick);
    }

neural_ai.pre_tick(this);

// ------------------------------------------------------------------
// ------------------------------------------------------------------
// Corpus crawl scheduling (button press consent; adapter executes network)
// ------------------------------------------------------------------
{
    bool any_active = false;
    for (uint32_t si = 0; si < EW_CRAWL_SESSION_MAX; ++si) { if (crawl_sessions[si].active) { any_active = true; break; } }

    if (any_active) {
        // Curriculum lane gate: enforce parallel-sequential stage ordering.
        // Stage->lane map is monotonic and conservative:
        //  stage0(Language): lanes 0-1
        //  stage1(QM/Physics): lanes 0-3
        //  stage2(Chem): lanes 0-4
        //  stage3(Materials): lanes 0-5
        //  stage4(Cosmology/Atmos): lanes 0-5
        //  stage5(Biology): all lanes
        switch (learning_curriculum_stage_u32) {
            case 0u: crawl_allowlist_lane_max_u32 = 1u; break;
            case 1u: crawl_allowlist_lane_max_u32 = 3u; break;
            case 2u: crawl_allowlist_lane_max_u32 = 4u; break;
            case 3u: crawl_allowlist_lane_max_u32 = 5u; break;
            case 4u: crawl_allowlist_lane_max_u32 = 5u; break;
            default: crawl_allowlist_lane_max_u32 = 0xFFFFFFFFu; break;
        }

        // Visualization/integration backlog gate: do not admit more external pages
        // when the learning gate has a high pending backlog.
        const uint32_t pending_metrics = learning_gate.registry().pending_count_u32();
        if (pending_metrics > 32u) {
            // Fail closed: stall new external requests until backlog drains.
            // GPU remains busy on learning/probe evolution.
            // (Politeness scheduling continues via next_allowed_tick).
        }
        // Headroom-derived throttling (integer-only). If headroom is low, emit fewer requests.
        uint32_t headroom_lane = 0;
        const int64_t h_q32_32 = ctx_snapshot.envelope_headroom_q32_32;
        if (h_q32_32 >= (int64_t)((3LL << 32) / 4)) headroom_lane = 2;
        else if (h_q32_32 >= (int64_t)((1LL << 32) / 2)) headroom_lane = 1;
        else headroom_lane = 0;

        uint32_t cap_pending = corpus_crawl_max_pending_u32;
        if (external_api_pending.size() >= (size_t)cap_pending) headroom_lane = 0;

        uint32_t emit_cap = corpus_crawl_max_emit_per_tick_u32;
        if (headroom_lane < emit_cap) emit_cap = headroom_lane;
        if (pending_metrics > 32u) emit_cap = 0u;

        uint32_t emitted = 0;
        while (emitted < emit_cap) {
            // Round-robin over active sessions for parallelism without overload.
            uint32_t chosen = EW_CRAWL_SESSION_MAX;
            for (uint32_t step = 0; step < EW_CRAWL_SESSION_MAX; ++step) {
                uint32_t si = (crawl_rr_u32 + step) % EW_CRAWL_SESSION_MAX;
                if (crawl_sessions[si].active && !crawl_sessions[si].q.empty()) { chosen = si; crawl_rr_u32 = (si + 1) % EW_CRAWL_SESSION_MAX; break; }
                if (crawl_sessions[si].active && crawl_sessions[si].q.empty()) {
                    crawl_sessions[si].active = false;
                }
            }
            if (chosen == EW_CRAWL_SESSION_MAX) break;

            EwCrawlSession& ss = crawl_sessions[chosen];
            SubstrateManager::EwCorpusCrawlTarget t = ss.q.front();
            ss.q.pop_front();

            // Curriculum gating: if target lane is not yet permitted, re-queue
            // deterministically and skip emission.
            if (t.lane_u32 > crawl_allowlist_lane_max_u32) {
                ss.q.push_back(t);
                // Do not spin endlessly in a single tick.
                break;
            }

            // Strict allowlist enforcement: host must exist in this session's domain map.
            uint32_t dom_anchor_id = 0;
            size_t dom_idx = (size_t)-1;
            for (size_t di = 0; di < ss.domain_map.size(); ++di) {
                if (ss.domain_map[di].domain_utf8 == t.host_utf8) { dom_anchor_id = ss.domain_map[di].domain_anchor_id_u32; dom_idx = di; break; }
            }
            if (dom_anchor_id == 0) {
                // Drop unknown host; fail closed.
                continue;
            }

            // Curriculum requirement prioritization:
            // Prefer domains that appear to contain metrics still missing for the current stage.
            // If a domain has a nonzero observed_topic_mask and it does not intersect the
            // stage missing mask, defer it deterministically.
            {
                const uint32_t cur_stage = learning_curriculum_stage_u32;
                const uint64_t missing_mask = learning_stage_required_mask_u64[cur_stage] & ~learning_stage_completed_mask_u64[cur_stage];
                const uint64_t obs_mask = (dom_idx != (size_t)-1) ? ss.domain_map[dom_idx].observed_topic_mask_u64 : 0ULL;
                if (obs_mask != 0ULL && (obs_mask & missing_mask) == 0ULL) {
                    // Domain appears unhelpful for remaining required checkpoints; re-queue.
                    ss.q.push_back(t);
                    continue;
                }
            }

            // Politeness gate: 1 req/sec/domain by default in canonical tick space.
            if (dom_idx != (size_t)-1) {
                const uint64_t na = ss.domain_map[dom_idx].next_allowed_tick_u64;
                if (canonical_tick < na) {
                    // Re-queue and try another domain/target; avoid violating interval.
                    ss.q.push_back(t);
                    continue;
                }
            }

            std::string path;
            if (t.stage_u32 == 0) path = "/robots.txt";
            else if (!t.path_utf8.empty()) path = t.path_utf8;
            else path = "/";

            if (path.empty() || path[0] != '/') path = "/";

            const std::string url = std::string("https://") + t.host_utf8 + path;

            EwExternalApiRequest req;
            req.tick_u64 = canonical_tick;
            req.request_id_u64 = (canonical_tick << 32) | (uint64_t)(0x80000000u | ((chosen & 0x3u) << 20) | ((t.stage_u32 & 0xFu) << 16) | ((ss.profile_u32 & 0xFu) << 12) | (uint32_t)(external_api_request_seq_u64++ & 0xFFFu));
            req.method_utf8 = "GET";
            req.url_utf8 = url;
            req.headers_kv_csv = "accept:text/plain;user-agent:EigenWareCrawler";
            req.response_cap_u32 = (t.stage_u32 == 3u) ? ingest_max_doc_bytes_u32 : crawler_max_bytes_per_segment_u32;
            req.context_anchor_id_u32 = ss.context_anchor_id_u32;
            req.crawler_anchor_id_u32 = ss.crawler_anchor_id_u32;
            req.domain_anchor_id_u32 = dom_anchor_id;
            external_api_pending.push_back(req);

            // Update next allowed tick for this domain.
            if (dom_idx != (size_t)-1) {
                ss.domain_map[dom_idx].next_allowed_tick_u64 = canonical_tick + crawl_politeness_req_interval_ticks_u64;
            }

            emitted++;
        }

        // Completion message when all sessions inactive.
        bool still = false;
        for (uint32_t si = 0; si < EW_CRAWL_SESSION_MAX; ++si) { if (crawl_sessions[si].active) { still = true; break; } }
        if (!still) {
            if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("CRAWL_COMPLETE");
        }
    }
}



    // ------------------------------------------------------------------
    // AI Interface Layer (BE/BF): deterministic decompression + effects
    // ------------------------------------------------------------------
    // Commands are supplied via submit_ai_commands_fixed(). Decompression is
    // integer-only and stable across replays.
    if (ai_commands_count_u32 > 0 && ai_total_weight_q63 > 0) {
        struct CmdIdx { EwAiCommand c; uint32_t idx; };
        CmdIdx tmp[EW_AI_COMMAND_MAX];
        uint32_t n = ai_commands_count_u32;
        if (n > EW_AI_COMMAND_MAX) n = EW_AI_COMMAND_MAX;
        for (uint32_t i = 0; i < n; ++i) { tmp[i].c = ai_commands_fixed[i]; tmp[i].idx = i; }

        auto cmd_less = [](const CmdIdx& a, const CmdIdx& b)->bool {
            if (a.c.priority_u16 != b.c.priority_u16) return a.c.priority_u16 > b.c.priority_u16; // desc
            if (a.c.opcode_u16 != b.c.opcode_u16) return a.c.opcode_u16 < b.c.opcode_u16;       // asc
            return a.idx < b.idx;                                                                // asc
        };
        std::sort(tmp, tmp + n, cmd_less);

        const int64_t total_w = ai_total_weight_q63;
        const int64_t A_pulse = ai_pulse_q63;

        // Derived gating threshold: at least 1/(2*N) of full-scale.
        const int64_t gate_q63 = (int64_t)(INT64_MAX / (int64_t)(2 * (int64_t)EW_AI_COMMAND_MAX));

        for (uint32_t i = 0; i < n; ++i) {
            const EwAiCommand cmd = tmp[i].c;
            if (cmd.weight_q63 <= 0) continue;

            // frac_i_q63 = (weight<<63)/total_weight
            __int128 num = (__int128)cmd.weight_q63 << 63;
            int64_t frac_q63 = 0;
            if (total_w > 0) frac_q63 = (int64_t)(num / (__int128)total_w);

            // A_i_q63 = (A_pulse * frac) >> 63
            __int128 prod = (__int128)A_pulse * (__int128)frac_q63;
            int64_t A_i_q63 = (int64_t)(prod >> 63);
            if (A_i_q63 < 0) A_i_q63 = 0;

            switch (cmd.opcode_u16) {
                case EW_AI_OP_TASK_SELECT: {
                    // Bounded tasks driven by viewport directives.
                    // All computation and artifact writes occur inside the substrate.
                    if (A_i_q63 < gate_q63) break;

                    auto starts_with = [&](const char* s)->bool {
                        const size_t n0 = std::strlen(s);
                        if (last_observation_text.size() < n0) return false;
                        for (size_t k = 0; k < n0; ++k) if (last_observation_text[k] != s[k]) return false;
                        return true;
                    };
                    auto trim_left = [&](std::string& x) {
                        while (!x.empty() && (x[0] == ' ' || x[0] == '\t')) x.erase(x.begin());
                    };
                    auto kind_from_path = [&](const std::string& p)->uint32_t {
                        if (p.size() >= 4 && p.substr(p.size()-4) == ".cpp") return (uint32_t)EW_ARTIFACT_CPP;
                        if (p.size() >= 4 && p.substr(p.size()-4) == ".hpp") return (uint32_t)EW_ARTIFACT_HPP;
                        if (p.size() >= 14 && p.substr(p.size()-14) == "CMakeLists.txt") return (uint32_t)EW_ARTIFACT_CMAKE;
                        if (p.size() >= 3 && p.substr(p.size()-3) == ".md") return (uint32_t)EW_ARTIFACT_MD;
                        return (uint32_t)EW_ARTIFACT_TEXT;
                    };

                    if (starts_with("QUERY:")) {
                        std::string q = last_observation_text.substr(std::strlen("QUERY:"));
                        trim_left(q);
                        corpus_query_emit_results(q, 0u);
                        corpus_answer_emit(q, 0u);
                        break;
                    }

                    if (starts_with("ANSWER:")) {
                        std::string q = last_observation_text.substr(std::strlen("ANSWER:"));
                        trim_left(q);
                        corpus_answer_emit(q, 0u);
                        break;
                    }

					// Web search + fetch (adapter-executed, substrate-scheduled).
					// Results are ingested into Draft Container/Corpus via the external API ingest path,
					// so subsequent QUERY: calls increasingly hit local grounded data.
					auto url_encode_utf8 = [&](const std::string& in)->std::string {
						static const char* H = "0123456789ABCDEF";
						std::string out;
						out.reserve(in.size() * 3);
						for (size_t i = 0; i < in.size(); ++i) {
							unsigned char c = (unsigned char)in[i];
							const bool unreserved =
								(c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
								(c == '-' || c == '_' || c == '.' || c == '~');
							if (unreserved) { out.push_back((char)c); continue; }
							if (c == ' ') { out.push_back('+'); continue; }
							out.push_back('%');
							out.push_back(H[(c >> 4) & 0xF]);
							out.push_back(H[c & 0xF]);
						}
						return out;
					};

					auto schedule_external_get = [&](const std::string& url, const std::string& headers_kv_csv, const char* ui_tag) {
						// Causal tag must match the external API request gate in crawler_enqueue_observation_utf8.
						const uint32_t EW_CAUSAL_TAG_EXTERNAL_API_REQ = 0x41504931u;
						// Artifact id: deterministic fold from tick + url length + ascii sum.
						uint64_t sum = 0u;
						for (size_t i = 0; i < url.size(); ++i) sum += (uint64_t)(unsigned char)url[i];
						const uint64_t artifact_id_u64 = (canonical_tick << 32) ^ (sum & 0xFFFFFFFFu) ^ (uint64_t)url.size();

						std::string line = "GET ";
						line += url;
						if (!headers_kv_csv.empty()) {
							line.push_back(' ');
							line += headers_kv_csv;
						}

						crawler_enqueue_observation_utf8(
							artifact_id_u64,
							3u, // stream_id: web
							9u, // extractor_id: external_api
							1u, // trust_class: policy-gated by adapter
							EW_CAUSAL_TAG_EXTERNAL_API_REQ,
						/*domain*/([&](){
							std::string host;
							size_t p = 0;
							const size_t n = url.size();
							while (p + 2 < n && !(url[p] == ':' && url[p+1] == '/' && url[p+2] == '/')) p++;
							if (p + 2 < n) p += 3;
							const size_t h0 = p;
							while (p < n && url[p] != '/' && url[p] != '?' && url[p] != '#') p++;
							if (p > h0) host = url.substr(h0, p - h0);
							return host.empty() ? std::string("external") : host;
						}()),
						/*url*/url,
						line
						);

						// Record the request line into the corpus for traceability.
						EwInspectorArtifact a{};
						a.coord_coord9_u64 = artifact_id_u64 ^ 0x57454251u; // 'WEBQ'
						a.kind_u32 = EW_ARTIFACT_TEXT;
						a.rel_path = std::string("Draft Container/Corpus/web_request_") + std::to_string((unsigned long long)artifact_id_u64) + ".txt";
						a.producer_tick_u64 = canonical_tick;
						a.payload = line + "\n";
						inspector_fields.upsert(a);

						std::string msg = ui_tag;
						msg += " ";
						msg += std::to_string((unsigned long long)artifact_id_u64);
						if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back(msg);
					};

					
if (starts_with("OPEN:") || starts_with("OPEN_RESULT:")) {
	// Grammar: OPEN:<n> or OPEN_RESULT:<n> (1-based index into last_websearch_urls_utf8)
	const size_t off = starts_with("OPEN_RESULT:") ? std::strlen("OPEN_RESULT:") : std::strlen("OPEN:");
	std::string rest = last_observation_text.substr(off);
	trim_left(rest);
	if (rest.empty()) break;
	uint32_t idx_n = 0u;
	for (size_t k = 0; k < rest.size(); ++k) {
		char digit_ch = rest[k];
		if (digit_ch < '0' || digit_ch > '9') break;
		idx_n = idx_n * 10u + (uint32_t)(digit_ch - '0');
		if (idx_n > 1000u) break;
	}
	if (idx_n == 0u || idx_n > (uint32_t)last_websearch_urls_utf8.size()) {
		if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("OPEN_NO_SUCH_RESULT");
		break;
	}
	const std::string url = last_websearch_urls_utf8[(size_t)(idx_n - 1u)];
	if (!url.empty()) schedule_external_get(url, std::string(), "WEBOPEN_SCHEDULED");
	break;
}

if (starts_with("WEBFETCH:")) {
						// Grammar: WEBFETCH:<url> [<headers_kv_csv>]
						std::string rest = last_observation_text.substr(std::strlen("WEBFETCH:"));
						trim_left(rest);
						if (rest.empty()) break;
						std::string url;
						std::string hdr;
						// Split first field as URL.
						size_t sp = rest.find(' ');
						if (sp == std::string::npos) { url = rest; }
						else { url = rest.substr(0, sp); hdr = rest.substr(sp + 1); trim_left(hdr); }
						if (!url.empty()) schedule_external_get(url, hdr, "WEBFETCH_SCHEDULED");
						break;
					}

					if (starts_with("WEBSEARCH_CFG:")) {
						// Grammar: WEBSEARCH_CFG:<ui_emit_n>:<max_results>:<auto_fetch_n>
						// Values are clamped to 0..30. max_results is also clamped to be >= ui_emit_n.
						std::string rest = last_observation_text.substr(std::strlen("WEBSEARCH_CFG:"));
						trim_left(rest);
						if (rest.empty()) break;
						auto parse_u32 = [&](std::string& s, uint32_t& out)->bool {
							out = 0u;
							trim_left(s);
							if (s.empty()) return false;
							size_t k = 0;
							while (k < s.size() && s[k] >= '0' && s[k] <= '9') {
								out = out * 10u + (uint32_t)(s[k] - '0');
								if (out > 10000u) break;
								k++;
							}
							if (k == 0) return false;
							s = s.substr(k);
							if (!s.empty() && s[0] == ':') s = s.substr(1);
							return true;
						};
						uint32_t ui_n = 0u, max_n = 0u, auto_n = 0u;
						if (!parse_u32(rest, ui_n)) break;
						if (!parse_u32(rest, max_n)) break;
						if (!parse_u32(rest, auto_n)) break;
						if (ui_n > 30u) ui_n = 30u;
						if (max_n > 30u) max_n = 30u;
						if (auto_n > 30u) auto_n = 30u;
						if (max_n < ui_n) max_n = ui_n;
						websearch_ui_emit_n_u32 = ui_n;
						websearch_max_results_u32 = max_n;
						websearch_auto_fetch_n_u32 = auto_n;
						if (ui_out_q.size() < UI_OUT_CAP) {
							std::string msg = "WEBSEARCH_CFG_SET ";
							msg += std::to_string((unsigned long long)ui_n);
							msg += " ";
							msg += std::to_string((unsigned long long)max_n);
							msg += " ";
							msg += std::to_string((unsigned long long)auto_n);
							ui_out_q.push_back(msg);
						}
						break;
					}

					if (starts_with("WEBSEARCH:") || starts_with("SEARCH:")) {
						const size_t off = starts_with("WEBSEARCH:") ? std::strlen("WEBSEARCH:") : std::strlen("SEARCH:");
						std::string q = last_observation_text.substr(off);
						trim_left(q);
						if (q.empty()) break;

						// If we already have strong local matches, prefer QUERY-only.
						const uint32_t best = corpus_query_best_score(q);
						if (best >= 3u) {
							std::string msg = "WEBSEARCH_LOCAL_HIT ";
							msg += std::to_string((unsigned long long)best);
							if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back(msg);
							corpus_query_emit_results(q, 0u);
							break;
						}

						// Default provider: DuckDuckGo HTML endpoint (no embedded keys).
						const std::string enc = url_encode_utf8(q);
						const std::string url = std::string("https://html.duckduckgo.com/html/?q=") + enc;
						schedule_external_get(url, std::string(), "WEBSEARCH_SCHEDULED");
						break;
					}

                    if (starts_with("CODEGEN:")) {
                        std::string module = last_observation_text.substr(std::strlen("CODEGEN:"));
                        trim_left(module);
                        if (!module.empty()) code_emit_minimal_cpp_module(this, module);
                        break;
                    }

                    if (starts_with("SYNTHCODE:")) {
                        std::string req = last_observation_text.substr(std::strlen("SYNTHCODE:"));
                        trim_left(req);
                        if (!req.empty()) {
                            (void)ew_synthcode_execute(this, req);
                        }
                        break;

if (starts_with("GAMEBOOT:")) {
    std::string req = last_observation_text.substr(std::strlen("GAMEBOOT:"));
    trim_left(req);
    (void)ew_gameboot_execute(this, req);
    break;
}

                    }

                    if (starts_with("CODEEDIT:")) {
                        // Grammar: CODEEDIT:<rel_path>:<append_text>
                        std::string rest = last_observation_text.substr(std::strlen("CODEEDIT:"));
                        const size_t p1 = rest.find(':');
                        if (p1 == std::string::npos) break;
                        std::string rel_path = rest.substr(0, p1);
                        std::string text = rest.substr(p1 + 1);
                        trim_left(rel_path);
                        if (rel_path.empty()) break;

                        EwPatchSpec ps;
                        ps.mode_u16 = EW_PATCH_APPEND_EOF;
                        ps.text = text;
                        (void)code_apply_patch_coherence_gated(this, rel_path, code_artifact_kind_from_rel_path(rel_path), ps, 0xE201u);
                        break;
                    }

                    if (starts_with("PATCH:")) {
                        // Grammar:
                        // PATCH:<rel_path>:APPEND:<text>
                        // PATCH:<rel_path>:INSERT_AFTER:<anchor>:<text>
                        // PATCH:<rel_path>:REPLACE_BETWEEN:<anchorA>:<anchorB>:<text>
                        // PATCH:<rel_path>:DELETE_BETWEEN:<anchorA>:<anchorB>
                        std::string rest = last_observation_text.substr(std::strlen("PATCH:"));
                        const size_t p1 = rest.find(':');
                        if (p1 == std::string::npos) break;
                        std::string rel_path = rest.substr(0, p1);
                        trim_left(rel_path);
                        if (rel_path.empty()) break;
                        rest = rest.substr(p1 + 1);

                        EwPatchSpec ps;
                        auto take_field = [&](std::string& s, std::string& out)->bool {
                            const size_t p = s.find(':');
                            if (p == std::string::npos) return false;
                            out = s.substr(0, p);
                            s = s.substr(p + 1);
                            return true;
                        };
                        std::string op;
                        if (!take_field(rest, op)) break;

                        if (op == "APPEND") {
                            ps.mode_u16 = EW_PATCH_APPEND_EOF;
                            ps.text = rest;
                        } else if (op == "INSERT_AFTER") {
                            ps.mode_u16 = EW_PATCH_INSERT_AFTER_ANCHOR;
                            if (!take_field(rest, ps.anchor_a)) break;
                            ps.text = rest;
                        } else if (op == "REPLACE_BETWEEN") {
                            ps.mode_u16 = EW_PATCH_REPLACE_BETWEEN_ANCHORS;
                            if (!take_field(rest, ps.anchor_a)) break;
                            if (!take_field(rest, ps.anchor_b)) break;
                            ps.text = rest;
                        } else if (op == "DELETE_BETWEEN") {
                            ps.mode_u16 = EW_PATCH_DELETE_BETWEEN_ANCHORS;
                            if (!take_field(rest, ps.anchor_a)) break;
                            ps.anchor_b = rest;
                        } else {
                            break;
                        }

                        (void)code_apply_patch_coherence_gated(this, rel_path, code_artifact_kind_from_rel_path(rel_path), ps, 0xE202u);
                        break;
                    }

                    if (starts_with("HYDRATE:")) {
                        std::string root = last_observation_text.substr(std::strlen("HYDRATE:"));
                        trim_left(root);
                        if (!root.empty()) code_emit_hydration_hint(this, root);
                        break;
                    }

                    // Auto web assist (conservative): if the user typed a short question
                    // that looks like a web-timed request and the local corpus has weak
                    // matches, schedule a web search request.
                    {
                        const std::string s = last_observation_text;
                        if (!s.empty() && s.size() <= 256) {
                            bool looks_question = (s.back() == '?');
                            std::string lc = ew_utf8_lower_ascii_only_str(s);
                            bool looks_time_sensitive = (lc.find("latest") != std::string::npos) ||
                                                       (lc.find("today") != std::string::npos) ||
                                                       (lc.find("current") != std::string::npos) ||
                                                       (lc.find("news") != std::string::npos);
                            bool looks_like_code = (lc.find("#include") != std::string::npos) || (lc.find("cmakelists") != std::string::npos);

                            if ((looks_question || looks_time_sensitive) && !looks_like_code) {
                                const uint32_t best = corpus_query_best_score(s);
                                if (best >= 3u) {
                                    corpus_query_emit_results(s, 0u);
                                    corpus_answer_emit(s, 0u);
                                } else {
                                    const std::string enc = url_encode_utf8(s);
                                    const std::string url = std::string("https://html.duckduckgo.com/html/?q=") + enc;
                                    schedule_external_get(url, std::string(), "WEBSEARCH_SCHEDULED");
                                }
                            }
                        }
                    }
                    break;
                }
                case EW_AI_OP_STORE: {
                    // Inspector fields are the substrate-resident storage surface.
                    // STORE indicates the caller intends artifacts to be committed
                    // on the next hydration projection.
                    // (No filesystem writes occur here.)
                    (void)A_i_q63;
                    break;
                }
                case EW_AI_OP_IO_READ:
                case EW_AI_OP_IO_WRITE:
                case EW_AI_OP_ROUTE:
                case EW_AI_OP_FETCH:
                case EW_AI_OP_RENDER_UPDATE:
                case EW_AI_OP_PRIORITY_HINT:
                default:
                    break;
            }
        }
    }


// ------------------------------------------------------------------
// Deterministic inbound ordering + topology events (MERGE/SPLIT/BIND_UPDATE)
// ------------------------------------------------------------------

    // ------------------------------------------------------------------
    // Bounded admission gate: enforce a fixed per-anchor pulse budget.
    // This prevents unbounded per-tick work when upstream emits too many pulses.
    // Determinism: stable-sort + fixed per-anchor slot count.
    // ------------------------------------------------------------------
    {
        std::vector<Pulse> bounded;
        ge_bound_pulses_per_anchor(
            inbound,
            SubstrateManager::TRAJ_SLOTS_PER_ANCHOR_U32,
            bounded,
            &pulses_seen_last_tick_u32,
            &pulses_dropped_last_tick_u32);
        inbound.swap(bounded);
    }

    // ------------------------------------------------------------------
    // Pulse fan-out propagation (single-hop per tick)
    // ------------------------------------------------------------------
    // Expand inbound pulses across local neighbor topology deterministically.
    // Fan-out factor is driven by amplitude/current codes but capped by
    // pulse_fanout_max_u32 to prevent runaway.
    if (!inbound.empty() && !anchors.empty()) {
        std::vector<Pulse> fan;
        fan.reserve(inbound.size() * 2u);

        auto abs_i32 = [](int32_t x)->uint32_t { return (uint32_t)((x < 0) ? -x : x); };

        for (size_t i = 0; i < inbound.size(); ++i) {
            const Pulse& p0 = inbound[i];
            fan.push_back(p0);

            // Skip topology control pulses; these are handled separately.
            if (p0.causal_tag == 0x2 || p0.causal_tag == 0x3 || p0.causal_tag == 0x4 ||
                p0.causal_tag == 0x5 || p0.causal_tag == 0x6) {
                continue;
            }

            if (p0.anchor_id >= anchors.size()) continue;
            const Anchor& src = anchors[p0.anchor_id];
            if (src.neighbors.empty()) continue;

            // Derive a small deterministic fan-out count from (a_code, i_code).
            // - a_code increases expansion pressure
            // - i_code increases transfer capacity
            // Keep the mapping simple and stable (no floats).
            uint32_t fo = 1u;
            fo += (uint32_t)(p0.a_code >> 3);      // /8
            fo += (uint32_t)(p0.i_code >> 6);      // /64
            if (fo < 1u) fo = 1u;
            if (fo > pulse_fanout_max_u32) fo = pulse_fanout_max_u32;

            // Deterministic neighbor selection order: neighbor list is stable.
            const uint32_t n = (uint32_t)src.neighbors.size();
            if (n == 0u) continue;
            // Choose a starting offset based on pulse signature to distribute load.
            const uint32_t start = (abs_i32(p0.f_code) + (uint32_t)p0.a_code + (uint32_t)p0.v_code + (uint32_t)p0.i_code) % n;

            // Single-hop fan-out: emit up to (fo-1) neighbor pulses.
            const uint32_t emit_n = (fo > 1u) ? (fo - 1u) : 0u;
            for (uint32_t k = 0u; k < emit_n; ++k) {
                const uint32_t nbr = src.neighbors[(start + k) % n];
                if (nbr == p0.anchor_id) continue;
                if (nbr >= anchors.size()) continue;
                Pulse p = p0;
                p.anchor_id = nbr;
                // Keep causal_tag/profile unchanged; ordering handles determinism.
                fan.push_back(p);
            }
        }

        inbound.swap(fan);
    }

auto pulse_less = [](const Pulse& a, const Pulse& b)->bool {
    if (a.tick != b.tick) return a.tick < b.tick;
    if (a.causal_tag != b.causal_tag) return a.causal_tag < b.causal_tag;
    if (a.anchor_id != b.anchor_id) return a.anchor_id < b.anchor_id;
    if (a.profile_id != b.profile_id) return a.profile_id < b.profile_id;
    if (a.f_code != b.f_code) return a.f_code < b.f_code;
    if (a.a_code != b.a_code) return a.a_code < b.a_code;
    if (a.v_code != b.v_code) return a.v_code < b.v_code;
    if (a.i_code != b.i_code) return a.i_code < b.i_code;
    return false;
};
std::sort(inbound.begin(), inbound.end(), pulse_less);

auto ensure_topology_size = [&](uint32_t n) {
    if (redirect_to.size() < n) redirect_to.resize(n, 0u);
    if (split_child_a.size() < n) split_child_a.resize(n, 0u);
    if (split_child_b.size() < n) split_child_b.resize(n, 0u);
};

auto alloc_anchor_id = [&]()->uint32_t {
    const uint32_t id = next_anchor_id_u32;
    next_anchor_id_u32 += 1u;
    ensure_topology_size(next_anchor_id_u32 + 1u);
    return id;
};

auto add_bidirectional_edge = [&](uint32_t a, uint32_t b) {
    if (a >= anchors.size() || b >= anchors.size() || a == b) return;
    auto& na = anchors[a].neighbors;
    auto& nb = anchors[b].neighbors;
    if (std::find(na.begin(), na.end(), b) == na.end()) na.push_back(b);
    if (std::find(nb.begin(), nb.end(), a) == nb.end()) nb.push_back(a);
};

auto remove_bidirectional_edge = [&](uint32_t a, uint32_t b) {
    if (a >= anchors.size() || b >= anchors.size() || a == b) return;
    auto& na = anchors[a].neighbors;
    auto& nb = anchors[b].neighbors;
    na.erase(std::remove(na.begin(), na.end(), b), na.end());
    nb.erase(std::remove(nb.begin(), nb.end(), a), nb.end());
};

auto split_anchor = [&](uint32_t parent_id, const Pulse* mode_a, const Pulse* mode_b) {
    if (parent_id >= anchors.size()) return;

    // Allocate two new child anchors deterministically.
    const uint32_t child_a = alloc_anchor_id();
    const uint32_t child_b = alloc_anchor_id();

    // Ensure vectors are large enough to index new ids as anchor indices.
    const uint32_t need = (child_b + 1u);
    while (anchors.size() < need) anchors.emplace_back((uint32_t)anchors.size());
    while (ancilla.size() < need) ancilla.push_back(ancilla_particle{});

    // Create child anchors by copying parent, then applying deterministic centroid offsets.
    Anchor a = anchors[parent_id];
    Anchor b = anchors[parent_id];
    a.id = child_a;
    b.id = child_b;

    // Deterministic centroid offsets from MODE pulses (if present).
    // We interpret f_code as a signed phase delta in TURN_SCALE/1024 units,
    // and a_code as a chi delta in TURN_SCALE/4096 units. This stays in the substrate,
    // uses only pulse payload, and is deterministic.
    auto apply_mode = [&](Anchor& dst, const Pulse* mp, int sign) {
        if (!mp) return;
        const int64_t dtheta = (int64_t)mp->f_code * (TURN_SCALE / 1024);
        const int64_t dchi = (int64_t)mp->a_code * (TURN_SCALE / 4096);
        dst.theta_q = wrap_turns(dst.theta_q + sign * dtheta);
        int64_t chi = dst.chi_q + sign * dchi;
        if (chi < 0) chi = 0;
        if (chi > TURN_SCALE) chi = TURN_SCALE;
        dst.chi_q = chi;
        dst.sync_basis9_from_core();
    };

    apply_mode(a, mode_a, +1);
    apply_mode(b, mode_b, -1);

    // Mass/continuum split (deterministic): distribute parent mass evenly.
    a.m_q = anchors[parent_id].m_q / 2;
    b.m_q = anchors[parent_id].m_q - a.m_q;
    a.sync_basis9_from_core();
    b.sync_basis9_from_core();

    // Neighbor inheritance: children inherit parent neighbors and connect to each other.
    a.neighbors = anchors[parent_id].neighbors;
    b.neighbors = anchors[parent_id].neighbors;

    // Write anchors back and connect topology.
    anchors[child_a] = a;
    anchors[child_b] = b;
    add_bidirectional_edge(child_a, child_b);

    // Redirect table records split children for deterministic later merge.
    ensure_topology_size(std::max(child_b, parent_id) + 1u);
    split_child_a[parent_id] = child_a;
    split_child_b[parent_id] = child_b;

    // Parent redirect points to the canonical child A for projection unless overridden.
    redirect_to[parent_id] = child_a;
};

auto merge_anchor = [&](uint32_t parent_id) {
    if (parent_id >= anchors.size()) return;
    ensure_topology_size(parent_id + 1u);
    const uint32_t a_id = split_child_a[parent_id];
    const uint32_t b_id = split_child_b[parent_id];
    if (a_id == 0u || b_id == 0u) return;
    if (a_id >= anchors.size() || b_id >= anchors.size()) return;

    // Deterministic merge: parent becomes the mean of children and children are redirected.
    Anchor merged = anchors[parent_id];
    merged.theta_q = wrap_turns((anchors[a_id].theta_q + anchors[b_id].theta_q) / 2);
    merged.chi_q = (anchors[a_id].chi_q + anchors[b_id].chi_q) / 2;
    merged.m_q = anchors[a_id].m_q + anchors[b_id].m_q;
    merged.neighbors = anchors[a_id].neighbors;
    merged.sync_basis9_from_core();
    anchors[parent_id] = merged;

    // Redirect children to parent and disconnect child-child edge.
    redirect_to[a_id] = parent_id;
    redirect_to[b_id] = parent_id;
    remove_bidirectional_edge(a_id, b_id);

    // Clear split map.
    split_child_a[parent_id] = 0u;
    split_child_b[parent_id] = 0u;
};

// Apply topology pulses and keep only non-topology pulses for physics evolution.
std::vector<Pulse> filtered;
filtered.reserve(inbound.size());

for (size_t i = 0; i < inbound.size(); ++i) {
    const Pulse& p = inbound[i];
    if (p.causal_tag == 0x5) { // SPLIT
        // Find MODE_A and MODE_B pulses targeting potential children in this tick.
        // In this single-tier prototype, MODE pulses are optional; if missing, split is symmetric.
        const Pulse* mode_a = nullptr;
        const Pulse* mode_b = nullptr;
        for (size_t j = 0; j < inbound.size(); ++j) {
            if (inbound[j].tick != p.tick) continue;
            if (inbound[j].causal_tag == 0x2) mode_a = &inbound[j];
            if (inbound[j].causal_tag == 0x3) mode_b = &inbound[j];
        }
        split_anchor(p.anchor_id, mode_a, mode_b);
        continue;
    }
    if (p.causal_tag == 0x4) { // MERGE
        merge_anchor(p.anchor_id);
        continue;
    }
    if (p.causal_tag == 0x6) { // BIND_UPDATE
        // Deterministic neighbor binding update: choose neighbor id from |f_code| mod N.
        const uint32_t n = anchors.empty() ? 0u : (uint32_t)anchors.size();
        if (n > 0u) {
            const uint32_t nbr = (uint32_t)((p.f_code < 0 ? -p.f_code : p.f_code) % (int32_t)n);
            add_bidirectional_edge(p.anchor_id, nbr);
        }
        continue;
    }
    filtered.push_back(p);
}

inbound.swap(filtered);
 

// --- Blueprint-mandated candidate/accept/commit pipeline ---

    EwState current_state;
    current_state.canonical_tick = canonical_tick;
    current_state.reservoir = reservoir;
    current_state.boundary_scale_q32_32 = boundary_scale_q32_32;
    current_state.anchors = anchors;
    current_state.ancilla = ancilla;
    current_state.lanes = lanes;
    current_state.object_store = object_store;
    current_state.materials_calib_done = materials_calib_done;

    EwInputs inputs;
    inputs.inbound = inbound;
    inputs.pending_text_x_q = pending_text_x_q;
    inputs.pending_image_y_q = pending_image_y_q;
    inputs.pending_audio_z_q = pending_audio_z_q;

    inputs.envelope = envelope_sample;

    inputs.gpu_pulse_freq_hz_u64 = gpu_pulse_freq_hz_u64;
    inputs.gpu_pulse_freq_ref_hz_u64 = gpu_pulse_freq_ref_hz_u64;
    inputs.gpu_pulse_amp_u32 = gpu_pulse_amp_u32;
    inputs.gpu_pulse_amp_ref_u32 = gpu_pulse_amp_ref_u32;
    inputs.gpu_pulse_volt_u32 = gpu_pulse_volt_u32;
    inputs.gpu_pulse_volt_ref_u32 = gpu_pulse_volt_ref_u32;


    EwCtx ctx;
    ctx.frame_gamma_turns_q = frame_gamma_turns_q;
    ctx.td_params = td_params;
    for (int i = 0; i < 9; ++i) {
        ctx.weights_q10[i] = weights_q10[i];
        ctx.denom_q[i] = denom_q[i];
    }
    // Axis scaling is computed inside the substrate from GPU pulse readings (Eq 3.0.2/3.0.3).
    // Default to caller-provided values only when no GPU reference is available.
    ctx.sx_q32_32 = sx_q32_32;
    ctx.sy_q32_32 = sy_q32_32;
    ctx.sz_q32_32 = sz_q32_32;

    if (gpu_pulse_freq_ref_hz_u64 != 0u && gpu_pulse_freq_hz_u64 != 0u) {
        const __int128 num = (static_cast<__int128>(gpu_pulse_freq_hz_u64) << 32);
        const int64_t pulse_freq_q32_32 = (int64_t)(num / ( __int128)gpu_pulse_freq_ref_hz_u64);
        ctx.sx_q32_32 = pulse_freq_q32_32;
    }
    if (gpu_pulse_amp_ref_u32 != 0u) {
        const __int128 num = (static_cast<__int128>(gpu_pulse_amp_u32) << 32);
        const int64_t pulse_amp_q32_32 = (int64_t)(num / ( __int128)gpu_pulse_amp_ref_u32);
        ctx.sy_q32_32 = pulse_amp_q32_32;
    }

    // AI pulse carrier biases amplitude (BE.3/BE.4). This is a bounded modifier
    // and cannot override control field motion.
    if (ai_pulse_q63 > 0) {
        // Convert q63 -> q32.32 (drop 31 LSBs).
        int64_t ai_bias_q32_32 = (int64_t)((uint64_t)ai_pulse_q63 >> 31);
        if (ai_bias_q32_32 < 0) ai_bias_q32_32 = 0;
        if (ai_bias_q32_32 > (1LL << 32)) ai_bias_q32_32 = (1LL << 32);
        // scale in [0.5, 1.0] as (0.5 + 0.5*bias)
        const int64_t half = (1LL << 31);
        int64_t scale_q32_32 = half + (ai_bias_q32_32 >> 1);
        ctx.sy_q32_32 = mul_q32_32(ctx.sy_q32_32, scale_q32_32);
    }

    // Joint gating for z-axis.
    ctx.sz_q32_32 = mul_q32_32(ctx.sx_q32_32, ctx.sy_q32_32);

    // ctx.hubble_h0_q32_32 is derived from effective constants below.
    ctx.tick_dt_seconds_q32_32 = tick_dt_seconds_q32_32;
    // ctx.boundary_scale_step_q32_32 is derived from effective constants below.
    ctx.boundary_scale_q32_32 = boundary_scale_q32_32;


    // ------------------------------------------------------------------
// ------------------------------------------------------------------
// Execution envelope -> deterministic headroom (Spec 4.1.9 / Eq A.11.6.2.23)
// ------------------------------------------------------------------
// All measurements are treated as read-path counters only. No analog sampling.
// If budgets are zero, treat that channel as unsaturated.
auto clamp01_q32_32 = [](int64_t x)->int64_t {
    if (x < 0) return 0;
    if (x > (1LL<<32)) return (1LL<<32);
    return x;
};

int64_t sat_compute_q32_32 = 0;
if (inputs.envelope.t_budget_ns_u64 > 0) {
    sat_compute_q32_32 = (int64_t)((( (__int128)inputs.envelope.t_exec_ns_u64) << 32) / (uint64_t)inputs.envelope.t_budget_ns_u64);
    sat_compute_q32_32 = clamp01_q32_32(sat_compute_q32_32);
}

int64_t sat_mem_q32_32 = 0;
if (inputs.envelope.bytes_budget_u64 > 0) {
    sat_mem_q32_32 = (int64_t)((( (__int128)inputs.envelope.bytes_moved_u64) << 32) / (uint64_t)inputs.envelope.bytes_budget_u64);
    sat_mem_q32_32 = clamp01_q32_32(sat_mem_q32_32);
}

int64_t sat_queue_q32_32 = 0;
if (inputs.envelope.queue_budget_u32 > 0) {
    sat_queue_q32_32 = (int64_t)((( (uint64_t)inputs.envelope.queue_backlog_u32) << 32) / (uint64_t)inputs.envelope.queue_budget_u32);
    sat_queue_q32_32 = clamp01_q32_32(sat_queue_q32_32);
}

// headroom = 1 - max(sat_compute, sat_mem, sat_queue)
int64_t sat_max_q32_32 = sat_compute_q32_32;
if (sat_mem_q32_32 > sat_max_q32_32) sat_max_q32_32 = sat_mem_q32_32;
if (sat_queue_q32_32 > sat_max_q32_32) sat_max_q32_32 = sat_queue_q32_32;
ctx.envelope_headroom_q32_32 = (1LL<<32) - sat_max_q32_32;
ctx.envelope_headroom_q32_32 = clamp01_q32_32(ctx.envelope_headroom_q32_32);


    // Effective constants derivation (Constance rule):
    // Start from baseline physical references and apply deterministic
    // relative-constants correlation and timespace Doppler/strain/flux
    // factors INSIDE the substrate microprocessor.
    // ------------------------------------------------------------------
    int64_t doppler_mean_turns_q = 0;
    if (!anchors.empty()) {
        for (size_t i = 0; i < anchors.size(); ++i) doppler_mean_turns_q += anchors[i].doppler_q;
        doppler_mean_turns_q /= (int64_t)anchors.size();
    }
    if (doppler_mean_turns_q < 0) doppler_mean_turns_q = -doppler_mean_turns_q;

    // v_fraction_c approximated as |doppler_turns| / TURN_SCALE, clamped to [0,1].
    int64_t v_fraction_c_q32_32 = (TURN_SCALE > 0) ? ((doppler_mean_turns_q << 32) / (int64_t)TURN_SCALE) : 0;
    if (v_fraction_c_q32_32 < 0) v_fraction_c_q32_32 = 0;
    if (v_fraction_c_q32_32 > (1LL << 32)) v_fraction_c_q32_32 = (1LL << 32);

    // flux_factor from carrier metric deviation on flux axis (axis 4).
    int64_t flux_factor_q32_32 = carrier_g_q32_32[4] - (1LL << 32);
    if (flux_factor_q32_32 < 0) flux_factor_q32_32 = -flux_factor_q32_32;

    // strain_factor from average axis scale deviation.
    int64_t sx_dev = sx_q32_32 - (1LL << 32); if (sx_dev < 0) sx_dev = -sx_dev;
    int64_t sy_dev = sy_q32_32 - (1LL << 32); if (sy_dev < 0) sy_dev = -sy_dev;
    int64_t sz_dev = sz_q32_32 - (1LL << 32); if (sz_dev < 0) sz_dev = -sz_dev;
    int64_t strain_factor_q32_32 = (sx_dev + sy_dev + sz_dev) / 3;

    const EwRefConstantsQ32_32 refs = ref_constants_default();
    const int64_t doppler_factor_q32_32 = timespace_doppler_factor(v_fraction_c_q32_32);
    // Environment temperature proxy (dimensionless, Q32.32), derived from
    // measurable substrate state (reservoir per anchor). 0 => absolute zero.
    int64_t temperature_q32_32 = 0;
    {
        const int64_t denom = ((int64_t)anchors.size() > 0) ? ((int64_t)anchors.size() << 32) : (1LL << 32);
        temperature_q32_32 = q32_32_div_i64(i64_abs(reservoir), denom);
        temperature_q32_32 = clamp_q32_32(temperature_q32_32, 0, (4LL << 32));
    }

    ctx.eff = effective_constants(refs, v_fraction_c_q32_32, doppler_factor_q32_32,
                                 flux_factor_q32_32, strain_factor_q32_32, temperature_q32_32);

    // Replace baseline H0 with per-tick effective H0 for evolution scaling.
    ctx.hubble_h0_q32_32 = ctx.eff.hubble_h0_eff_q32_32;

    // Boundary scale step is exp(H0_eff * dt).
    const int64_t x_q32_32 = q32_32_mul(ctx.hubble_h0_q32_32, ctx.tick_dt_seconds_q32_32);
    ctx.boundary_scale_step_q32_32 = q32_32_exp_small(x_q32_32);

    // Omega.3 carrier metric snapshot.
    for (int i = 0; i < 9; ++i) {
        ctx.carrier_g_q32_32[i] = carrier_g_q32_32[i];
    }

    // Pulse-delta sampling / anchor extraction configuration.
    ctx.tau_delta_q15 = tau_delta_q15;
    ctx.theta_ref_turns_q = theta_ref_turns_q;
    ctx.A_ref_q32_32 = A_ref_q32_32;
    ctx.alpha_A_turns_q32_32 = alpha_A_turns_q32_32;
    ctx.kappa_lnA_turns_q32_32 = kappa_lnA_turns_q32_32;
    ctx.kappa_lnF_turns_q32_32 = kappa_lnF_turns_q32_32;
    ctx.coherence_cmin_turns_q = coherence_cmin_turns_q;
    ctx.omega0_turns_per_sec_q32_32 = omega0_turns_per_sec_q32_32;
    ctx.kappa_rho_q32_32 = kappa_rho_q32_32;

    ctx.pulse_current_max_mA_q32_32 = pulse_current_max_mA_q32_32;
    ctx.phase_max_displacement_q32_32 = phase_max_displacement_q32_32;
    ctx.phase_orbital_displacement_unit_mA_q32_32 = phase_orbital_displacement_unit_mA_q32_32;
    ctx.gradient_headroom_mA_q32_32 = gradient_headroom_mA_q32_32;
    ctx.temporal_envelope_ticks_u64 = temporal_envelope_ticks_u64;

    // Carrier safety governor snapshot.
    ctx.governor = governor;

    // Execute any ΩA operator packets inside the substrate microprocessor.
    // This uses viewport-derived inputs and runs before candidate evolution.
    opk_runtime_evolution_requested = false;
    this->ctx_snapshot = ctx;
    ctx_snapshot = ctx;
    ew_execute_operator_packets_v1(this);

    // Deterministic projection/I/O: persist input bindings if they were
    // mutated by substrate ops this tick.
    {
        std::string berr;
        (void)save_input_bindings_if_dirty(&berr);
    }

    EwState candidate_next_state;
    if (gpu_lattice_authoritative) {
        // Authoritative GPU lattice evolution path:
        // 1) Merge inbound pulses into deterministic voxel injections.
        // 2) Upload to lattice and step one tick.
        // 3) Advance canonical state tick and boundary scale; keep other
        //    canonical state holders stable in this prototype.
        ensure_lattice_gpu_();

        // Set lattice dt from canonical tick dt.
        const float dt_s = (float)((double)tick_dt_seconds_q32_32 / 4294967296.0);
        if (lattice_gpu_) lattice_gpu_->set_dt_seconds(dt_s);
// Rebuild world density mask from synthesized object voxel volumes.
// This makes object↔global coupling spatial (same lattice), not just anchor-id addressing.
if (lattice_gpu_) {
    const uint32_t gx = lattice_gpu_->grid_x();
    const uint32_t gy = lattice_gpu_->grid_y();
    const uint32_t gz = lattice_gpu_->grid_z();
    const size_t n = (size_t)gx * (size_t)gy * (size_t)gz;
    std::vector<uint8_t> dens;
    dens.assign(n, 0u);

    auto q32_32_to_i32_round = [](uint64_t q)->int32_t {
        const int64_t s = (int64_t)q;
        // Round toward nearest: add 0.5 in Q32.32 then shift.
        const int64_t r = (s >= 0) ? (s + (1ll << 31)) : (s - (1ll << 31));
        return (int32_t)(r >> 32);
    };

    std::vector<uint64_t> ids;
    object_store.list_object_ids_sorted(ids);
    for (size_t oi = 0; oi < ids.size(); ++oi) {
        const uint64_t oid = ids[oi];
        const EwObjectEntry* e = object_store.find(oid);
        if (!e) continue;

        EwVoxelVolumeView vv;
        if (!object_store.view_voxel_volume(oid, vv)) continue;
        if (vv.format_u32 != 1u || !vv.bytes || vv.byte_count == 0) continue;

        // Deterministic placement: center at geomcoord9[0..2] (Q32.32), mapped 1 cell per 1 unit.
        const int32_t cx = q32_32_to_i32_round(e->geomcoord9_u64x9.u64x9[0]) + (int32_t)(gx / 2u);
        const int32_t cy = q32_32_to_i32_round(e->geomcoord9_u64x9.u64x9[1]) + (int32_t)(gy / 2u);
        const int32_t cz = q32_32_to_i32_round(e->geomcoord9_u64x9.u64x9[2]) + (int32_t)(gz / 2u);

        const int32_t ox0 = cx - (int32_t)(vv.grid_x_u32 / 2u);
        const int32_t oy0 = cy - (int32_t)(vv.grid_y_u32 / 2u);
        const int32_t oz0 = cz - (int32_t)(vv.grid_z_u32 / 2u);

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
                    uint8_t& d = dens[dst_row + (size_t)xt];
                    d = (v > d) ? v : d; // deterministic combine
                }
            }
        }
    }
    lattice_gpu_->upload_density_mask_u8(dens.data(), dens.size());
}

        // Object→world writeback (bounded, deterministic): apply object imprint
        // after density mask rebuild and before pulse injection.
        apply_object_imprint_writeback_();


        struct TmpCmd {
            uint32_t x, y, z;
            float t, im, au;
        };
        std::vector<TmpCmd> tmp;
        tmp.reserve(inbound.size());

        const float gov_target = (float)ctx.governor.target_frac_q15 / 32768.0f;
        auto amp_from_pulse = [&](const Pulse& p, const Anchor* ap)->float {
            const float a = (float)p.a_code / 65535.0f;
            const float v = (float)p.v_code / (float)V_MAX;
            const float i = (float)p.i_code / (float)I_MAX;
            const float sgn = (p.f_code < 0) ? -1.0f : 1.0f;
            // Harmonic influence: scale by (1 + mean_harmonics_q15) in Q15.
            // This uses GPU-derived harmonic artifacts and contains no CPU-side reductions.
            const float h = ap ? (1.0f + ((float)ap->harmonics_mean_q15 / 32768.0f)) : 1.0f;
            return sgn * (a * v * i) * gov_target * h;
        };

        if (lattice_gpu_) {
            const uint32_t gx = lattice_gpu_->grid_x();
            const uint32_t gy = lattice_gpu_->grid_y();
            const uint32_t gz = lattice_gpu_->grid_z();
            const uint64_t gxy = (uint64_t)gx * (uint64_t)gy;
            const uint32_t fanout_max_cfg = this->pulse_fanout_max_u32;
            const uint32_t fanout_max = (fanout_max_cfg == 0u) ? this->derived_pulse_fanout_max_u32(ctx) : fanout_max_cfg;
            for (size_t pi = 0; pi < inbound.size(); ++pi) {
                const Pulse& p = inbound[pi];
                const uint64_t aid = (uint64_t)p.anchor_id;

// Spatial addressing: for per-object anchors, inject at the object's
// lattice-mapped center derived from its geomcoord9 position. This
// makes object↔global coupling act on the same lattice.
uint32_t x = (uint32_t)(aid % (uint64_t)gx);
uint32_t y = (uint32_t)((aid / (uint64_t)gx) % (uint64_t)gy);
uint32_t z = (uint32_t)((aid / gxy) % (uint64_t)gz);

const Anchor* ap = (p.anchor_id < anchors.size()) ? &anchors[p.anchor_id] : nullptr;
if (ap && ap->object_id_u64 != 0u) {
    const EwObjectEntry* oe = object_store.find(ap->object_id_u64);
    if (oe) {
        auto q32_32_to_i32_round = [](uint64_t q)->int32_t {
            const int64_t s = (int64_t)q;
            const int64_t r = (s >= 0) ? (s + (1ll << 31)) : (s - (1ll << 31));
            return (int32_t)(r >> 32);
        };
        const int32_t cx = q32_32_to_i32_round(oe->geomcoord9_u64x9.u64x9[0]) + (int32_t)(gx / 2u);
        const int32_t cy = q32_32_to_i32_round(oe->geomcoord9_u64x9.u64x9[1]) + (int32_t)(gy / 2u);
        const int32_t cz = q32_32_to_i32_round(oe->geomcoord9_u64x9.u64x9[2]) + (int32_t)(gz / 2u);
        if (cx >= 0 && cy >= 0 && cz >= 0) {
            x = (uint32_t)clamp_u32((uint32_t)cx, 0u, gx - 1u);
            y = (uint32_t)clamp_u32((uint32_t)cy, 0u, gy - 1u);
            z = (uint32_t)clamp_u32((uint32_t)cz, 0u, gz - 1u);
        }
    }
}

                const float amp = amp_from_pulse(p, ap);
                float t = 0.0f, im = 0.0f, au = 0.0f;
                // Profile mapping: 0->text, 1->image, 2->audio; otherwise split.
                if (p.profile_id == 0u) t = amp;
                else if (p.profile_id == 1u) im = amp;
                else if (p.profile_id == 2u) au = amp;
                else {
                    t = amp * (1.0f / 3.0f);
                    im = amp * (1.0f / 3.0f);
                    au = amp * (1.0f / 3.0f);
                }
                // Fan-out: expand one pulse into a small deterministic neighborhood
                // influenced by the anchor's harmonic bins. This is the substrate
                // microprocessing pattern (Fourier-like carrier -> multi-op update)
                // while remaining VRAM/bandwidth bounded.
                uint32_t n = 1u;
                if (fanout_max > 1u) {
                    const uint64_t aa = (uint64_t)p.a_code;
                    const uint64_t ii = (uint64_t)p.i_code;
                    const uint64_t denom = (uint64_t)65535ull * (uint64_t)I_MAX;
                    uint64_t scaled = (denom > 0ull) ? ((aa * ii * (uint64_t)(fanout_max - 1u)) / denom) : 0ull;
                    if (scaled > (uint64_t)(fanout_max - 1u)) scaled = (uint64_t)(fanout_max - 1u);
                    n = 1u + (uint32_t)scaled;
                }
                auto h32 = [&](uint64_t s)->uint64_t{
                    s ^= s >> 33; s *= 0xff51afd7ed558ccdULL;
                    s ^= s >> 33; s *= 0xc4ceb9fe1a85ec53ULL;
                    s ^= s >> 33; return s;
                };
                for (uint32_t k = 0; k < n; ++k) {
                    uint64_t h = h32(aid ^ (uint64_t)(uint32_t)p.f_code ^ ((uint64_t)k << 32));
                    int32_t dx = 0, dy = 0, dz = 0;
                    uint32_t rad = 0;
                    if (ap) {
                        const uint32_t bin = (uint32_t)(h % (uint64_t)Anchor::HARMONICS_N);
                        const uint16_t q = ap->harmonics_q15[bin];
                        // Radius 0..3 determined by harmonic magnitude.
                        rad = (uint32_t)((uint64_t)q * 4ull / 32768ull);
                        if (rad > 3u) rad = 3u;
                    }
                    if (rad != 0u) {
                        const uint32_t span = 2u * rad + 1u;
                        dx = (int32_t)((h >> 8) % span) - (int32_t)rad;
                        dy = (int32_t)((h >> 16) % span) - (int32_t)rad;
                        dz = (int32_t)((h >> 24) % span) - (int32_t)rad;
                    }
                    const uint32_t xx = (uint32_t)((int64_t)x + (int64_t)dx + (int64_t)gx) % gx;
                    const uint32_t yy = (uint32_t)((int64_t)y + (int64_t)dy + (int64_t)gy) % gy;
                    const uint32_t zz = (uint32_t)((int64_t)z + (int64_t)dz + (int64_t)gz) % gz;
                    // Split amplitude across fan-out members deterministically.
                    const float invn = 1.0f / (float)n;
                    tmp.push_back({xx, yy, zz, t * invn, im * invn, au * invn});
                }
            }
        }

        // Merge duplicates deterministically by (z,y,x).
        std::sort(tmp.begin(), tmp.end(), [](const TmpCmd& a, const TmpCmd& b){
            if (a.z != b.z) return a.z < b.z;
            if (a.y != b.y) return a.y < b.y;
            return a.x < b.x;
        });

        std::vector<EwPulseInjectCmd> cmds;
        cmds.reserve(tmp.size());
        for (size_t i = 0; i < tmp.size(); ) {
            TmpCmd acc = tmp[i];
            size_t j = i + 1;
            while (j < tmp.size() && tmp[j].x == acc.x && tmp[j].y == acc.y && tmp[j].z == acc.z) {
                acc.t += tmp[j].t;
                acc.im += tmp[j].im;
                acc.au += tmp[j].au;
                ++j;
            }
            EwPulseInjectCmd c{};
            c.x = acc.x; c.y = acc.y; c.z = acc.z;
            c.amp_text = acc.t;
            c.amp_image = acc.im;
            c.amp_audio = acc.au;
            cmds.push_back(c);
            i = j;
        }

        if (lattice_gpu_) {
            lattice_gpu_->upload_pulse_inject_cmds(cmds.data(), cmds.size());
            lattice_gpu_->step_one_tick();
        }

        candidate_next_state = current_state;
        candidate_next_state.canonical_tick = current_state.canonical_tick + 1u;
        candidate_next_state.boundary_scale_q32_32 = mul_q32_32(current_state.boundary_scale_q32_32, ctx.boundary_scale_step_q32_32);
    } else {
        candidate_next_state = evolve_state(current_state, inputs, ctx);
    }

    const EwLedgerDelta ledger_delta = compute_ledger_delta(current_state, candidate_next_state, ctx);

    EwState committed;
    if (accept_state(current_state, candidate_next_state, ledger_delta, ctx)) {
        committed = candidate_next_state;
    } else {
        committed = make_sink_state(current_state, ctx);
        (void)ew_cmb_bath_route_reject(committed, current_state, candidate_next_state, ledger_delta);
    }

    commit_state(current_state, committed);

    // Commit back into the live runtime state.
    canonical_tick = current_state.canonical_tick;
    reservoir = current_state.reservoir;
    cmb_bath = current_state.cmb_bath;
    boundary_scale_q32_32 = current_state.boundary_scale_q32_32;
    boundary_scale_step_q32_32 = ctx.boundary_scale_step_q32_32;
    anchors = current_state.anchors;
    ancilla = current_state.ancilla;
    lanes = current_state.lanes;
    object_store = current_state.object_store;
    materials_calib_done = current_state.materials_calib_done;

    // If no admissible evolution occurred (e.g., absolute-zero budget path), we still
    // advance the scheduling tick deterministically. Physics state remains unchanged.
    if (canonical_tick == prev_tick_u64) {
        canonical_tick = prev_tick_u64 + 1u;
    }

    ge_update_process_substrate_telemetry(*this);
    ge_update_ai_data_substrate_telemetry(*this);
    ge_update_ai_substrate_telemetry(*this);
    ge_update_subsystem_substrate_telemetry(*this);

    if ((canonical_tick_u64() % 360u) == 0u && process_substrate_telemetry.valid_u32 != 0u) {
        emit_ui_line(ge_format_process_substrate_status_line(process_substrate_telemetry,
                                                             "PROCESS_SUBSTRATE_STATUS"));
    }
    if ((canonical_tick_u64() % 360u) == 0u && subsystem_substrate_telemetry.valid_u32 != 0u) {
        emit_ui_line(ge_format_subsystem_substrate_status_line(subsystem_substrate_telemetry,
                                                               "SUBSYSTEM_SUBSTRATE_STATUS"));
    }

    // Deterministic measurement-frame projection is stored onto anchors.
    // This makes frame_gamma_turns_q observable as a basis shift, while
    // remaining a pure frame mismatch (no energy coupling).
    for (size_t i = 0; i < anchors.size(); ++i) {
        anchors[i].basis9 = projected_for(anchors[i]);
    }

    // ------------------------------------------------------------------
    // Derived editor spectral visualization samples (read-only projection)
    // This is a pure projection for the viewport/editor overlay; it must
    // never become authoritative simulation input.
    // ------------------------------------------------------------------
    {
        const uint64_t kEditorObjectId = 0x315F524F54494445ULL; // 'EDITOR_1' little-endian
        Anchor* edp = nullptr;
        for (size_t i = 0; i < anchors.size(); ++i) {
            if (anchors[i].kind_u32 == EW_ANCHOR_KIND_EDITOR && anchors[i].object_id_u64 == kEditorObjectId) {
                edp = &anchors[i];
                break;
            }
        }

        // Note: spectral field anchor selection is performed after selection
        // resolution (nearest-by-region-center, deterministic tie-break on id).
        const Anchor* sp = nullptr;

        if (edp) {
            EwEditorAnchorState& ed = edp->editor_state;
            ed.spectral_viz_count_u32 = 0u;
            for (uint32_t k = 0u; k < EW_EDITOR_SPECTRAL_VIZ_SAMPLES; ++k) {
                // Ensure deterministic zeroing each tick.
                for (int d = 0; d < 3; ++d) ed.spectral_viz[k].pos_q16_16[d] = 0;
                ed.spectral_viz[k].field_q1_15 = 0;
                ed.spectral_viz[k].grad_q1_15 = 0;
                ed.spectral_viz[k].band_u8 = 0;
                ed.spectral_viz[k].pad_u8[0] = 0;
                ed.spectral_viz[k].pad_u8[1] = 0;
                ed.spectral_viz[k].pad_u8[2] = 0;
            }

            if (ed.selected_object_id_u64 != 0u) {
                const Anchor* obj = nullptr;
                for (size_t i = 1; i < anchors.size(); ++i) {
                    if (anchors[i].kind_u32 == EW_ANCHOR_KIND_OBJECT && anchors[i].object_id_u64 == ed.selected_object_id_u64) {
                        obj = &anchors[i];
                        break;
                    }
                }

                if (obj) {
                    // Deterministic selection: nearest spectral field anchor by region center.
                    {
                        const int32_t ox = obj->object_state.pos_q16_16[0];
                        const int32_t oy = obj->object_state.pos_q16_16[1];
                        const int32_t oz = obj->object_state.pos_q16_16[2];
                        __int128 best_d2 = 0;
                        uint32_t best_id = 0u;
                        bool best_set = false;
                        for (uint32_t i = 0u; i < (uint32_t)anchors.size(); ++i) {
                            const Anchor& a = anchors[i];
                            if (a.kind_u32 != EW_ANCHOR_KIND_SPECTRAL_FIELD) continue;
                            const int32_t* rc = a.spectral_field_state.region_center_q16_16;
                            const __int128 dx = (__int128)ox - (__int128)rc[0];
                            const __int128 dy = (__int128)oy - (__int128)rc[1];
                            const __int128 dz = (__int128)oz - (__int128)rc[2];
                            const __int128 d2 = dx*dx + dy*dy + dz*dz;
                            if (!best_set || d2 < best_d2 || (d2 == best_d2 && i < best_id)) {
                                best_set = true;
                                best_d2 = d2;
                                best_id = i;
                                sp = &a;
                            }
                        }
                    }

                    const int32_t cx = obj->object_state.pos_q16_16[0];
                    const int32_t cy = obj->object_state.pos_q16_16[1];
                    const int32_t cz = obj->object_state.pos_q16_16[2];

                    // 4x4 grid in the XZ plane around the selected object.
                    const int32_t offsets_m_q16_16[4] = {
                        (int32_t)(-2 * 65536),
                        (int32_t)(-1 * 65536),
                        (int32_t)( 1 * 65536),
                        (int32_t)( 2 * 65536),
                    };

                    if (sp) {
                        const EwSpectralFieldAnchorState& ss = sp->spectral_field_state;

                        // Budget clamp: derived probes must respect the spectral fanout budget
                        // and HOLD behavior (no probe spam during HOLD ticks).
                        uint32_t max_samples = EW_EDITOR_SPECTRAL_VIZ_SAMPLES;
                        if (ss.hold_tick_u8 != 0u) {
                            max_samples = 0u;
                        } else {
                            const uint32_t b = (ss.fanout_budget_u32 > 0u) ? ss.fanout_budget_u32 : 1u;
                            const uint32_t cap = (b >= 8u) ? (b / 2u) : 4u;
                            if (cap < max_samples) max_samples = cap;
                        }

                        uint32_t outi = 0u;
                        for (uint32_t ix = 0u; ix < 4u && outi < max_samples; ++ix) {
                            for (uint32_t iz = 0u; iz < 4u && outi < max_samples; ++iz) {
                            int32_t p[3] = { cx + offsets_m_q16_16[ix], cy, cz + offsets_m_q16_16[iz] };
                            ed.spectral_viz[outi].pos_q16_16[0] = p[0];
                            ed.spectral_viz[outi].pos_q16_16[1] = p[1];
                            ed.spectral_viz[outi].pos_q16_16[2] = p[2];
                            ed.spectral_viz[outi].field_q1_15 = ew_spectral_probe_field_q1_15(ss, p);
                            ed.spectral_viz[outi].grad_q1_15  = ew_spectral_probe_grad_q1_15(ss, p);
                            ed.spectral_viz[outi].band_u8 = ss.leakage_band_u8;
                            ++outi;
                            }
                        }
                        ed.spectral_viz_count_u32 = outi;
                    }

                    // Derived boundary visualization samples (voxel boundary strength/permeability)
                    ed.boundary_viz_count_u32 = 0u;
                    for (uint32_t k = 0; k < EW_EDITOR_BOUNDARY_VIZ_SAMPLES; ++k) {
                        for (int d = 0; d < 3; ++d) ed.boundary_viz[k].pos_q16_16[d] = 0;
                        ed.boundary_viz[k].boundary_strength_q15 = 0;
                        ed.boundary_viz[k].permeability_q15 = 0;
                        ed.boundary_viz[k].boundary_normal_u8 = 0;
                        ed.boundary_viz[k].no_slip_u8 = 0;
                        ed.boundary_viz[k].color_band_u8 = 0;
                        ed.boundary_viz[k].pad_u8 = 0;
                    }

                    // Deterministic multi-block selection: choose nearest voxel coupling anchor
                    // by block center relative to the selected object.
                    const Anchor* vox = nullptr;
                    {
                        const int32_t ox = obj->object_state.pos_q16_16[0];
                        const int32_t oy = obj->object_state.pos_q16_16[1];
                        const int32_t oz = obj->object_state.pos_q16_16[2];
                        __int128 best_d2 = 0;
                        uint32_t best_id = 0u;
                        bool best_set = false;
                        for (uint32_t i = 0u; i < (uint32_t)anchors.size(); ++i) {
                            const Anchor& a = anchors[i];
                            if (a.kind_u32 != EW_ANCHOR_KIND_VOXEL_COUPLING) continue;
                            const EwVoxelCouplingAnchorState& vs = a.voxel_coupling_state;
                            const int32_t half_extent = (int32_t)(((__int128)EW_VOXEL_COUPLING_DIM * (__int128)vs.voxel_size_m_q16_16) / 2);
                            const int32_t cxv = vs.origin_q16_16[0] + half_extent;
                            const int32_t cyv = vs.origin_q16_16[1] + half_extent;
                            const int32_t czv = vs.origin_q16_16[2] + half_extent;
                            const __int128 dx = (__int128)ox - (__int128)cxv;
                            const __int128 dy = (__int128)oy - (__int128)cyv;
                            const __int128 dz = (__int128)oz - (__int128)czv;
                            const __int128 d2 = dx*dx + dy*dy + dz*dz;
                            if (!best_set || d2 < best_d2 || (d2 == best_d2 && i < best_id)) {
                                best_set = true;
                                best_d2 = d2;
                                best_id = i;
                                vox = &a;
                            }
                        }
                    }

                    if (vox) {
                        // Respect HOLD: if nearest spectral anchor is holding, suppress
                        // derived boundary probes to avoid misleading overlays.
                        const bool hold = (sp && sp->spectral_field_state.hold_tick_u8 != 0u);
                        if (hold) {
                            ed.boundary_viz_count_u32 = 0u;
                        } else {
                        // Sample a 4x4 grid around selection in XZ plane.
                        const int32_t step = (int32_t)(1 * 65536); // 1m
                        // Budget clamp: boundary probes share the same fanout budget envelope.
                        uint32_t max_b = EW_EDITOR_BOUNDARY_VIZ_SAMPLES;
                        if (sp) {
                            const uint32_t b = (sp->spectral_field_state.fanout_budget_u32 > 0u) ? sp->spectral_field_state.fanout_budget_u32 : 1u;
                            const uint32_t cap = (b >= 8u) ? (b / 2u) : 4u;
                            if (cap < max_b) max_b = cap;
                        }

                        uint32_t outb = 0u;
                        for (int iz = -2; iz < 2 && outb < EW_EDITOR_BOUNDARY_VIZ_SAMPLES; ++iz) {
                            for (int ix = -2; ix < 2 && outb < EW_EDITOR_BOUNDARY_VIZ_SAMPLES; ++ix) {
                                if (outb >= max_b) break;
                                int32_t p[3] = { cx + ix*step, cy, cz + iz*step };

                                // Map point to voxel index.
                                const EwVoxelCouplingAnchorState& vs = vox->voxel_coupling_state;
                                const int32_t vxsz = vs.voxel_size_m_q16_16;
                                int32_t relx = p[0] - vs.origin_q16_16[0];
                                int32_t rely = p[1] - vs.origin_q16_16[1];
                                int32_t relz = p[2] - vs.origin_q16_16[2];
                                int xi = (vxsz > 0) ? (int)(relx / vxsz) : 0;
                                int yi = (vxsz > 0) ? (int)(rely / vxsz) : 0;
                                int zi = (vxsz > 0) ? (int)(relz / vxsz) : 0;
                                if (xi < 0) xi = 0; if (yi < 0) yi = 0; if (zi < 0) zi = 0;
                                if (xi >= (int)EW_VOXEL_COUPLING_DIM) xi = (int)EW_VOXEL_COUPLING_DIM-1;
                                if (yi >= (int)EW_VOXEL_COUPLING_DIM) yi = (int)EW_VOXEL_COUPLING_DIM-1;
                                if (zi >= (int)EW_VOXEL_COUPLING_DIM) zi = (int)EW_VOXEL_COUPLING_DIM-1;
                                const uint32_t idx = (uint32_t)(xi + EW_VOXEL_COUPLING_DIM * (yi + EW_VOXEL_COUPLING_DIM * zi));

                                ed.boundary_viz[outb].pos_q16_16[0] = p[0];
                                ed.boundary_viz[outb].pos_q16_16[1] = p[1];
                                ed.boundary_viz[outb].pos_q16_16[2] = p[2];
                                ed.boundary_viz[outb].boundary_strength_q15 = vs.boundary_strength_vox_q15[idx];
                                ed.boundary_viz[outb].permeability_q15 = vs.permeability_vox_q15[idx];
                                ed.boundary_viz[outb].boundary_normal_u8 = vs.boundary_normal_u8[idx];
                                ed.boundary_viz[outb].no_slip_u8 = vs.no_slip_u8[idx];
                                ed.boundary_viz[outb].color_band_u8 = vs.influx_band_u8;
                                outb++;
                            }
                        }
                        ed.boundary_viz_count_u32 = outb;
                        }
                    }

                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Render packet projection (anchors -> renderer)
    // Renderer must not derive camera basis from local controller state.
    // The substrate projects a render-camera packet each tick.
    // ------------------------------------------------------------------
    if (camera_anchor_id_u32 != 0u && camera_anchor_id_u32 < anchors.size()) {
        const Anchor& cam = anchors[camera_anchor_id_u32];
        if (cam.kind_u32 == EW_ANCHOR_KIND_CAMERA) {
            render_camera_packet.focal_length_mm_q16_16 = cam.camera_state.focal_length_mm_q16_16;
            render_camera_packet.aperture_f_q16_16 = cam.camera_state.aperture_f_q16_16;
            render_camera_packet.exposure_ev_q16_16 = cam.camera_state.exposure_ev_q16_16;
            render_camera_packet.focus_distance_m_q32_32 = cam.camera_state.focus_distance_m_q32_32;
            render_camera_packet.focus_mode_u8 = cam.camera_state.focus_mode_u8;
            render_camera_packet.audio_env_field_q1_15 = cam.camera_state.audio_env_field_q1_15;
            render_camera_packet.audio_env_grad_q1_15 = cam.camera_state.audio_env_grad_q1_15;
            render_camera_packet.audio_env_coherence_q15 = cam.camera_state.audio_env_coherence_q15;
            render_camera_packet.color_band_u8 = cam.camera_state.color_band_u8;
            render_camera_packet.color_r_u8 = cam.camera_state.color_r_u8;
            render_camera_packet.color_g_u8 = cam.camera_state.color_g_u8;
            render_camera_packet.color_b_u8 = cam.camera_state.color_b_u8;
            render_camera_packet.audio_eq_preset_u8 = cam.camera_state.audio_eq_preset_u8;
            render_camera_packet.audio_reverb_preset_u8 = cam.camera_state.audio_reverb_preset_u8;
            render_camera_packet.audio_occlusion_preset_u8 = cam.camera_state.audio_occlusion_preset_u8;
            render_camera_packet.pos_xyz_q16_16[0] = cam.camera_state.pos_xyz_q16_16[0];
            render_camera_packet.pos_xyz_q16_16[1] = cam.camera_state.pos_xyz_q16_16[1];
            render_camera_packet.pos_xyz_q16_16[2] = cam.camera_state.pos_xyz_q16_16[2];
            for (int qi = 0; qi < 4; ++qi) render_camera_packet.rot_quat_q16_16[qi] = cam.camera_state.rot_quat_q16_16[qi];

            ew_build_view_mat_q16_16(cam.camera_state.pos_xyz_q16_16, cam.camera_state.rot_quat_q16_16, render_camera_packet.view_mat_q16_16);
            render_camera_packet_tick_u64 = canonical_tick;

            // Render assist packet: pre-derived focus/LOD coefficients.
            render_assist_packet.focus_distance_m_q32_32 = cam.camera_state.focus_distance_m_q32_32;
            render_assist_packet.audio_env_field_q1_15 = cam.camera_state.audio_env_field_q1_15;
            render_assist_packet.audio_env_grad_q1_15 = cam.camera_state.audio_env_grad_q1_15;
            render_assist_packet.audio_env_coherence_q15 = cam.camera_state.audio_env_coherence_q15;
            render_assist_packet.color_band_u8 = cam.camera_state.color_band_u8;
            render_assist_packet.color_r_u8 = cam.camera_state.color_r_u8;
            render_assist_packet.color_g_u8 = cam.camera_state.color_g_u8;
            render_assist_packet.color_b_u8 = cam.camera_state.color_b_u8;
            render_assist_packet.audio_eq_preset_u8 = cam.camera_state.audio_eq_preset_u8;
            render_assist_packet.audio_reverb_preset_u8 = cam.camera_state.audio_reverb_preset_u8;
            render_assist_packet.audio_occlusion_preset_u8 = cam.camera_state.audio_occlusion_preset_u8;
            // Focus band derived from settings (Q16.16 -> Q32.32)
            render_assist_packet.focus_band_m_q32_32 = ((int64_t)project_settings.camera.focus_band_m_q16_16) << 16;

            // Compute squared bounds in meters^2 (Q32.32).
            auto sq_q32_32 = [&](int64_t v_q32)->uint64_t {
                // (Q32.32 * Q32.32) >> 32 => Q32.32
                __int128 prod = ( (__int128)v_q32 * (__int128)v_q32 );
                uint64_t out = (uint64_t)(prod >> 32);
                return out;
            };
            const int64_t d = render_assist_packet.focus_distance_m_q32_32;
            const int64_t w = render_assist_packet.focus_band_m_q32_32;
            int64_t near_d = d - w;
            int64_t far_d  = d + w;
            if (near_d < 0) near_d = 0;
            if (far_d < near_d + (1ll<<16)) far_d = near_d + (1ll<<16);
            render_assist_packet.focus_near_m2_q32_32 = sq_q32_32(near_d);
            render_assist_packet.focus_far_m2_q32_32  = sq_q32_32(far_d);

            const uint64_t range_m2 = (render_assist_packet.focus_far_m2_q32_32 > render_assist_packet.focus_near_m2_q32_32)
                ? (render_assist_packet.focus_far_m2_q32_32 - render_assist_packet.focus_near_m2_q32_32)
                : 1ull;
            // inv_range (Q16.16) ~= (1<<48)/range_q32_32
            render_assist_packet.inv_focus_range_m2_q16_16 = (int32_t)(((__int128)1 << 48) / ( (__int128)range_m2 ));

            // Near boost window: [0.25ft, 1ft] default, but driven by settings.
            const int64_t nmin = ((int64_t)project_settings.camera.near_boost_min_m_q16_16) << 16;
            const int64_t nmax = ((int64_t)project_settings.camera.near_boost_max_m_q16_16) << 16;
            render_assist_packet.near_min_m2_q32_32 = sq_q32_32(nmin);
            render_assist_packet.near_max_m2_q32_32 = sq_q32_32(nmax);
            const uint64_t nrange = (render_assist_packet.near_max_m2_q32_32 > render_assist_packet.near_min_m2_q32_32)
                ? (render_assist_packet.near_max_m2_q32_32 - render_assist_packet.near_min_m2_q32_32)
                : 1ull;
            render_assist_packet.inv_near_range_m2_q16_16 = (int32_t)(((__int128)1 << 48) / ( (__int128)nrange ));

            render_assist_packet.screen_proxy_scale_q16_16 = project_settings.rendering.screen_proxy_scale_q16_16;
            render_assist_packet.lod_boost_max_q16_16 = project_settings.rendering.lod_boost_max_q16_16;
            render_assist_packet_tick_u64 = canonical_tick;

            // ------------------------------------------------------------------
            // XR per-eye view packet projection (observation pose -> fixed view)
            // The viewport submits raw eye poses; the substrate projects view matrices.
            // ------------------------------------------------------------------
            auto f32_to_q16 = [&](float v)->int32_t {
                // Deterministic rounding to nearest.
                double dv = (double)v;
                double q = dv * 65536.0;
                long long r = (long long)llround(q);
                if (r > 2147483647LL) r = 2147483647LL;
                if (r < -2147483648LL) r = -2147483648LL;
                return (int32_t)r;
            };

            auto build_view_from_pose = [&](const float pos_f32[3], const float quat_f32[4], int32_t out_view_q16[16]) {
                // Convert pose to fixed-point.
                const int32_t px = f32_to_q16(pos_f32[0]);
                const int32_t py = f32_to_q16(pos_f32[1]);
                const int32_t pz = f32_to_q16(pos_f32[2]);
                const int32_t qx = f32_to_q16(quat_f32[0]);
                const int32_t qy = f32_to_q16(quat_f32[1]);
                const int32_t qz = f32_to_q16(quat_f32[2]);
                const int32_t qw = f32_to_q16(quat_f32[3]);

                const int64_t one = 65536LL;
                const int64_t two = 131072LL;

                auto mul_q16 = [&](int32_t a, int32_t b)->int64_t { return ((int64_t)a * (int64_t)b) >> 16; };

                const int64_t xx = mul_q16(qx,qx);
                const int64_t yy = mul_q16(qy,qy);
                const int64_t zz = mul_q16(qz,qz);
                const int64_t xy = mul_q16(qx,qy);
                const int64_t xz = mul_q16(qx,qz);
                const int64_t yz = mul_q16(qy,qz);
                const int64_t wx = mul_q16(qw,qx);
                const int64_t wy = mul_q16(qw,qy);
                const int64_t wz = mul_q16(qw,qz);

                // Rotation matrix (world from local), Q16.16
                const int64_t r00 = one - ((two * (yy + zz)) >> 16);
                const int64_t r01 = ((two * (xy - wz)) >> 16);
                const int64_t r02 = ((two * (xz + wy)) >> 16);

                const int64_t r10 = ((two * (xy + wz)) >> 16);
                const int64_t r11 = one - ((two * (xx + zz)) >> 16);
                const int64_t r12 = ((two * (yz - wx)) >> 16);

                const int64_t r20 = ((two * (xz - wy)) >> 16);
                const int64_t r21 = ((two * (yz + wx)) >> 16);
                const int64_t r22 = one - ((two * (xx + yy)) >> 16);

                // View matrix = inverse pose = R^T and -R^T p
                const int64_t vt0 = -(((r00 * px + r10 * py + r20 * pz) >> 16));
                const int64_t vt1 = -(((r01 * px + r11 * py + r21 * pz) >> 16));
                const int64_t vt2 = -(((r02 * px + r12 * py + r22 * pz) >> 16));

                // Row-major: [ R^T | t ]
                out_view_q16[0] = (int32_t)r00; out_view_q16[1] = (int32_t)r10; out_view_q16[2] = (int32_t)r20; out_view_q16[3] = (int32_t)vt0;
                out_view_q16[4] = (int32_t)r01; out_view_q16[5] = (int32_t)r11; out_view_q16[6] = (int32_t)r21; out_view_q16[7] = (int32_t)vt1;
                out_view_q16[8] = (int32_t)r02; out_view_q16[9] = (int32_t)r12; out_view_q16[10]= (int32_t)r22; out_view_q16[11]= (int32_t)vt2;
                out_view_q16[12]= 0; out_view_q16[13]= 0; out_view_q16[14]= 0; out_view_q16[15]= (int32_t)one;
            };

            for (uint32_t ei = 0; ei < 2u; ++ei) {
                EwRenderXrEyePacket& outp = render_xr_eye_packet[ei];
                if (xr_eye_pose_f32[ei].valid_u32 != 0u) {
                    build_view_from_pose(xr_eye_pose_f32[ei].pos_xyz_f32, xr_eye_pose_f32[ei].rot_xyzw_f32, outp.view_mat_q16_16);
                    outp.tick_u64 = canonical_tick;
                    render_xr_eye_packet_tick_u64[ei] = canonical_tick;
                } else {
                    // Fail closed deterministically: identity view
                    for (int k=0;k<16;++k) outp.view_mat_q16_16[k] = 0;
                    outp.view_mat_q16_16[0] = 65536; outp.view_mat_q16_16[5] = 65536; outp.view_mat_q16_16[10] = 65536; outp.view_mat_q16_16[15] = 65536;
                    outp.tick_u64 = canonical_tick;
                    render_xr_eye_packet_tick_u64[ei] = canonical_tick;
                }
            }

        }
    }

    // ------------------------------------------------------------------
    // Object export texture refresh (boundary-coupling observable)
    //
    // The engine treats the UV atlas as a substrate-resident carrier of
    // object variables (density/coherence/curvature/id). To ensure OBJ/MTL
    // parameters (textures) are exportable and reflect the object update
    // law and coupling observables, we refresh the atlas channels from the
    // current anchor frame each tick under a bounded budget.
    //
    // Deterministic policy:
    //  - update at most 2 objects per tick
    //  - object list is ascending id
    //  - per-pixel update is a pure function of (existing atlas, anchor state)
    // ------------------------------------------------------------------
    {
        std::vector<uint64_t> obj_ids;
        object_store.list_object_ids_sorted(obj_ids);
        const uint32_t max_updates = 2u;
        uint32_t updated = 0;
        const uint32_t n = (uint32_t)obj_ids.size();
        const uint32_t start = (n == 0u) ? 0u : (uint32_t)(canonical_tick % (uint64_t)n);
        for (uint32_t k = 0; k < n && updated < max_updates; ++k) {
            const uint32_t idx = (start + k) % n;
            const uint64_t oid = obj_ids[idx];
            uint32_t aw=0, ah=0, af=0;
            const uint8_t* bytes = nullptr;
            size_t bn = 0;
            if (!object_store.view_uv_atlas(oid, aw, ah, af, bytes, bn) || !bytes || bn == 0) continue;

            // Anchor observable: use the object id as anchor id when in range.
            const uint32_t aid = (oid < (uint64_t)anchors.size()) ? (uint32_t)oid : 0u;
            const Anchor* a = (aid < anchors.size()) ? &anchors[aid] : nullptr;
            const uint64_t curv = a ? (uint64_t)((a->curvature_q < 0) ? -a->curvature_q : a->curvature_q) : 0u;
            const uint64_t dop  = a ? (uint64_t)((a->doppler_q < 0) ? -a->doppler_q : a->doppler_q) : 0u;
            // Scale down TURN_SCALE-ish values to 0..255.
            const uint64_t mix = (curv >> 20) + (dop >> 20);
            const uint8_t s = (uint8_t)((mix > 255u) ? 255u : mix);
            const uint8_t idlo = (uint8_t)(oid & 0xFFu);

            std::vector<uint8_t> out;
            out.assign(bytes, bytes + bn);
            for (size_t i = 0; i + 3 < out.size(); i += 4) {
                // R = density remains
                out[i + 1] = (uint8_t)(out[i + 1] ^ s);      // G: coherence proxy modulated by coupling
                out[i + 2] = (uint8_t)(out[i + 2] ^ (s ^ idlo)); // B: curvature proxy modulated
                // A = id tag remains
            }
            (void)object_store.upsert_uv_atlas_rgba8(oid, aw, ah, out.data(), out.size());
            updated++;
        }
    }

    // Post-tick cognition: update attractor memory and classification from
    // committed state. This does not modify the physics state directly.
    neural_ai.post_tick(this);

    // ------------------------------------------------------------------
    // Inspector Fields (Blueprint engineering): substrate-resident
    // workspace artifacts.
    //
    // Mechanism: the AI writes *artifacts* into inspector fields as vector
    // field state. A separate hydrator projection makes them visible as
    // functioning files when and only when coherence-gated.
    // ------------------------------------------------------------------
    {
        EwInspectorArtifact a;
        a.coord_coord9_u64 = neural_ai.status().sig9_u64;
        a.rel_path = "Inspector/ai_status.md";
        a.kind_u32 = EW_ARTIFACT_MD;
        a.producer_operator_id_u32 = 7001u; // canonical: AI_STATUS_ARTIFACT
        a.producer_tick_u64 = canonical_tick;

        // Deterministic payload (ASCII) describing AI state and latest action.
        a.payload.clear();
        a.payload += "# EigenWare AI Status\n\n";
        a.payload += "tick_u64: " + std::to_string(neural_ai.status().tick_u64) + "\n";
        a.payload += "class_id_u32: " + std::to_string(neural_ai.status().class_id_u32) + "\n";
        a.payload += "confidence_q32_32: " + std::to_string(neural_ai.status().confidence_q32_32) + "\n";
        a.payload += "sig9_u64: " + std::to_string(neural_ai.status().sig9_u64) + "\n";
        a.payload += "observation_seq_u64: " + std::to_string(observation_seq_u64) + "\n";
        a.payload += "command_observation_seq_u64: " + std::to_string(ai_last_command_observation_seq_u64) + "\n";

        a.payload += "\n# Process Substrate\n\n";
        a.payload += "valid_u32: " + std::to_string(process_substrate_telemetry.valid_u32) + "\n";
        a.payload += "spectral_anchor_id_u32: " + std::to_string(process_substrate_telemetry.spectral_anchor_id_u32) + "\n";
        a.payload += "coherence_anchor_id_u32: " + std::to_string(process_substrate_telemetry.coherence_anchor_id_u32) + "\n";
        a.payload += "fanout_budget_u32: " + std::to_string(process_substrate_telemetry.fanout_budget_u32) + "\n";
        a.payload += "intent_norm_q15: " + std::to_string((uint32_t)process_substrate_telemetry.intent_norm_q15) + "\n";
        a.payload += "energy_mean_q15: " + std::to_string((uint32_t)process_substrate_telemetry.energy_mean_q15) + "\n";
        a.payload += "energy_peak_q15: " + std::to_string((uint32_t)process_substrate_telemetry.energy_peak_q15) + "\n";
        a.payload += "leakage_abs_q15: " + std::to_string((uint32_t)process_substrate_telemetry.leakage_abs_q15) + "\n";
        a.payload += "residual_norm_q15: " + std::to_string((uint32_t)process_substrate_telemetry.residual_norm_q15) + "\n";
        a.payload += "controller_authority_q15: " + std::to_string((uint32_t)process_substrate_telemetry.controller_authority_q15) + "\n";
        a.payload += "dt_scale_q32_32: " + std::to_string(process_substrate_telemetry.dt_scale_q32_32) + "\n";
        a.payload += "viscosity_bias_q32_32: " + std::to_string(process_substrate_telemetry.viscosity_bias_q32_32) + "\n";
        a.payload += "gpu_freq_norm_q15: " + std::to_string((uint32_t)process_substrate_telemetry.gpu_freq_norm_q15) + "\n";
        a.payload += "gpu_amp_norm_q15: " + std::to_string((uint32_t)process_substrate_telemetry.gpu_amp_norm_q15) + "\n";
        a.payload += "gpu_volt_norm_q15: " + std::to_string((uint32_t)process_substrate_telemetry.gpu_volt_norm_q15) + "\n";
        a.payload += "interference_norm_q15: " + std::to_string((uint32_t)process_substrate_telemetry.interference_norm_q15) + "\n";
        a.payload += "coherence_norm_q15: " + std::to_string((uint32_t)process_substrate_telemetry.coherence_norm_q15) + "\n";
        a.payload += "observer_norm_q15: " + std::to_string((uint32_t)process_substrate_telemetry.observer_norm_q15) + "\n";
        a.payload += "source_vibration_q15: " + std::to_string((uint32_t)process_substrate_telemetry.source_vibration_q15) + "\n";
        a.payload += "calibration_authority_q15: " + std::to_string((uint32_t)process_substrate_telemetry.calibration_authority_q15) + "\n";
        a.payload += "calibration_error_q32_32: " + std::to_string(process_substrate_telemetry.calibration_error_q32_32) + "\n";
        a.payload += "calibration_delta_q32_32: " + std::to_string(process_substrate_telemetry.calibration_delta_q32_32) + "\n";
        a.payload += "calibration_integral_q32_32: " + std::to_string(process_substrate_telemetry.calibration_integral_q32_32) + "\n";
        if (process_substrate_telemetry.valid_u32 != 0u &&
            process_substrate_telemetry.spectral_anchor_id_u32 != 0u &&
            process_substrate_telemetry.spectral_anchor_id_u32 < anchors.size() &&
            anchors[process_substrate_telemetry.spectral_anchor_id_u32].kind_u32 == EW_ANCHOR_KIND_SPECTRAL_FIELD) {
            const EwSpectralFieldAnchorState& ss =
                anchors[process_substrate_telemetry.spectral_anchor_id_u32].spectral_field_state;
            for (uint32_t i = 0; i < 8u; ++i) {
                a.payload += std::string("interference_band_q15_") + std::to_string((unsigned long long)i) + ": " +
                             std::to_string((uint32_t)ss.calibration_summary.interference_band_q15[i]) + "\n";
                a.payload += std::string("coherence_band_q15_") + std::to_string((unsigned long long)i) + ": " +
                             std::to_string((uint32_t)ss.calibration_summary.coherence_band_q15[i]) + "\n";
            }
        }

        a.payload += "\n# Subsystem Substrate\n\n";
        a.payload += "valid_u32: " + std::to_string(subsystem_substrate_telemetry.valid_u32) + "\n";
        a.payload += "subsystem_count_u32: " + std::to_string(subsystem_substrate_telemetry.subsystem_count_u32) + "\n";
        a.payload += "active_subsystem_count_u32: " + std::to_string(subsystem_substrate_telemetry.active_subsystem_count_u32) + "\n";
        a.payload += "dominant_subsystem_id_u32: " + std::to_string(subsystem_substrate_telemetry.dominant_subsystem_id_u32) + "\n";
        a.payload += "aggregate_intent_norm_q15: " + std::to_string((uint32_t)subsystem_substrate_telemetry.aggregate_intent_norm_q15) + "\n";
        a.payload += "aggregate_measured_norm_q15: " + std::to_string((uint32_t)subsystem_substrate_telemetry.aggregate_measured_norm_q15) + "\n";
        a.payload += "aggregate_residual_norm_q15: " + std::to_string((uint32_t)subsystem_substrate_telemetry.aggregate_residual_norm_q15) + "\n";
        a.payload += "aggregate_spin_norm_q15: " + std::to_string((uint32_t)subsystem_substrate_telemetry.aggregate_spin_norm_q15) + "\n";
        a.payload += "aggregate_coupling_norm_q15: " + std::to_string((uint32_t)subsystem_substrate_telemetry.aggregate_coupling_norm_q15) + "\n";
        a.payload += "aggregate_controller_authority_q15: " + std::to_string((uint32_t)subsystem_substrate_telemetry.aggregate_controller_authority_q15) + "\n";
        a.payload += "aggregate_error_q32_32: " + std::to_string(subsystem_substrate_telemetry.aggregate_error_q32_32) + "\n";
        a.payload += "aggregate_error_delta_q32_32: " + std::to_string(subsystem_substrate_telemetry.aggregate_error_delta_q32_32) + "\n";
        a.payload += "aggregate_error_integral_q32_32: " + std::to_string(subsystem_substrate_telemetry.aggregate_error_integral_q32_32) + "\n";
        for (uint32_t i = 0u; i < subsystem_substrate_telemetry.subsystem_count_u32 &&
                              i < EW_SUBSYSTEM_SUBSTRATE_LANE_CAP; ++i) {
            const EwSubsystemCalculusLane& lane = subsystem_substrate_telemetry.lanes[i];
            const std::string prefix = std::string("lane_") + std::to_string((unsigned long long)i) + "_";
            a.payload += prefix + "subsystem_id_u32: " + std::to_string(lane.subsystem_id_u32) + "\n";
            a.payload += prefix + "name: " + ge_subsystem_plane_name_ascii(lane.subsystem_id_u32) + "\n";
            a.payload += prefix + "intent_norm_q15: " + std::to_string((uint32_t)lane.intent_norm_q15) + "\n";
            a.payload += prefix + "measured_norm_q15: " + std::to_string((uint32_t)lane.measured_norm_q15) + "\n";
            a.payload += prefix + "residual_norm_q15: " + std::to_string((uint32_t)lane.residual_norm_q15) + "\n";
            a.payload += prefix + "spin_norm_q15: " + std::to_string((uint32_t)lane.spin_norm_q15) + "\n";
            a.payload += prefix + "coupling_norm_q15: " + std::to_string((uint32_t)lane.coupling_norm_q15) + "\n";
            a.payload += prefix + "controller_authority_q15: " + std::to_string((uint32_t)lane.controller_authority_q15) + "\n";
            a.payload += prefix + "error_q32_32: " + std::to_string(lane.error_q32_32) + "\n";
            a.payload += prefix + "error_delta_q32_32: " + std::to_string(lane.error_delta_q32_32) + "\n";
            a.payload += prefix + "error_integral_q32_32: " + std::to_string(lane.error_integral_q32_32) + "\n";
        }

        a.payload += "\n# AI Backend\n\n";
        a.payload += "command_count_u32: " + std::to_string(ai_substrate_telemetry.command_count_u32) + "\n";
        a.payload += "action_log_count_u32: " + std::to_string(ai_substrate_telemetry.action_log_count_u32) + "\n";
        a.payload += "carrier_pulse_q63: " + std::to_string(ai_substrate_telemetry.carrier_pulse_q63) + "\n";
        a.payload += "carrier_total_weight_q63: " + std::to_string(ai_substrate_telemetry.carrier_total_weight_q63) + "\n";
        a.payload += "pending_external_api_u32: " + std::to_string(ai_substrate_telemetry.pending_external_api_u32) + "\n";
        a.payload += "inflight_external_api_u32: " + std::to_string(ai_substrate_telemetry.inflight_external_api_u32) + "\n";
        a.payload += "fanout_budget_u32: " + std::to_string(ai_substrate_telemetry.fanout_budget_u32) + "\n";
        a.payload += "anticipation_flags_u16: " + std::to_string((uint32_t)ai_substrate_telemetry.anticipation_flags_u16) + "\n";
        a.payload += "controller_authority_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.controller_authority_q15) + "\n";
        a.payload += "op_gain_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.op_gain_q15) + "\n";
        a.payload += "op_phase_bias_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.op_phase_bias_q15) + "\n";
        a.payload += "memory_norm_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.memory_norm_q15) + "\n";
        a.payload += "reasoning_norm_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.reasoning_norm_q15) + "\n";
        a.payload += "planning_norm_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.planning_norm_q15) + "\n";
        a.payload += "creativity_norm_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.creativity_norm_q15) + "\n";
        a.payload += "perception_norm_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.perception_norm_q15) + "\n";
        a.payload += "temporal_binding_norm_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.temporal_binding_norm_q15) + "\n";
        a.payload += "crawler_drift_norm_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.crawler_drift_norm_q15) + "\n";
        a.payload += "network_coherence_norm_q15: " + std::to_string((uint32_t)ai_substrate_telemetry.network_coherence_norm_q15) + "\n";
        for (uint32_t i = 0; i < 8u; ++i) {
            a.payload += std::string("carrier_band_q15_") + std::to_string((unsigned long long)i) + ": " +
                         std::to_string((uint32_t)ai_substrate_telemetry.carrier_band_q15[i]) + "\n";
        }

        a.payload += "\n# AI Data Substrate\n\n";
        a.payload += "corpus_artifact_count_u32: " + std::to_string(ai_data_substrate_telemetry.corpus_artifact_count_u32) + "\n";
        a.payload += "corpus_text_artifact_count_u32: " + std::to_string(ai_data_substrate_telemetry.corpus_text_artifact_count_u32) + "\n";
        a.payload += "manifest_record_count_u32: " + std::to_string(ai_data_substrate_telemetry.manifest_record_count_u32) + "\n";
        a.payload += "pending_metric_count_u32: " + std::to_string(ai_data_substrate_telemetry.pending_metric_count_u32) + "\n";
        a.payload += "completed_metric_count_u32: " + std::to_string(ai_data_substrate_telemetry.completed_metric_count_u32) + "\n";
        a.payload += "accepted_metric_count_u32: " + std::to_string(ai_data_substrate_telemetry.accepted_metric_count_u32) + "\n";
        a.payload += "language_word_count_u32: " + std::to_string(ai_data_substrate_telemetry.language_word_count_u32) + "\n";
        a.payload += "language_pron_count_u32: " + std::to_string(ai_data_substrate_telemetry.language_pron_count_u32) + "\n";
        a.payload += "language_relation_count_u32: " + std::to_string(ai_data_substrate_telemetry.language_relation_count_u32) + "\n";
        a.payload += "language_concept_count_u32: " + std::to_string(ai_data_substrate_telemetry.language_concept_count_u32) + "\n";
        a.payload += "language_speech_utt_count_u32: " + std::to_string(ai_data_substrate_telemetry.language_speech_utt_count_u32) + "\n";
        a.payload += "math_pemdas_cases_passed_u32: " + std::to_string(ai_data_substrate_telemetry.math_pemdas_cases_passed_u32) + "\n";
        a.payload += "math_pemdas_cases_total_u32: " + std::to_string(ai_data_substrate_telemetry.math_pemdas_cases_total_u32) + "\n";
        a.payload += "math_graph_packets_emitted_u32: " + std::to_string(ai_data_substrate_telemetry.math_graph_packets_emitted_u32) + "\n";
        a.payload += "math_khan_pages_seen_u32: " + std::to_string(ai_data_substrate_telemetry.math_khan_pages_seen_u32) + "\n";
        a.payload += "math_khan_chars_ingested_u32: " + std::to_string(ai_data_substrate_telemetry.math_khan_chars_ingested_u32) + "\n";
        a.payload += "crawler_pending_obs_u32: " + std::to_string(ai_data_substrate_telemetry.crawler_pending_obs_u32) + "\n";
        a.payload += "crawler_last_utf8_bytes_u32: " + std::to_string(ai_data_substrate_telemetry.crawler_last_utf8_bytes_u32) + "\n";
        a.payload += "crawler_enqueued_obs_u64: " + std::to_string(ai_data_substrate_telemetry.crawler_enqueued_obs_u64) + "\n";
        a.payload += "crawler_admitted_pulses_u64: " + std::to_string(ai_data_substrate_telemetry.crawler_admitted_pulses_u64) + "\n";
        a.payload += "crawler_dropped_obs_u64: " + std::to_string(ai_data_substrate_telemetry.crawler_dropped_obs_u64) + "\n";
        a.payload += "crawler_truncated_segments_u64: " + std::to_string(ai_data_substrate_telemetry.crawler_truncated_segments_u64) + "\n";
        a.payload += "intent_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.intent_norm_q15) + "\n";
        a.payload += "measured_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.measured_norm_q15) + "\n";
        a.payload += "residual_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.residual_norm_q15) + "\n";
        a.payload += "error_delta_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.error_delta_norm_q15) + "\n";
        a.payload += "error_integral_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.error_integral_norm_q15) + "\n";
        a.payload += "controller_authority_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.controller_authority_q15) + "\n";
        a.payload += "crawler_flow_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.crawler_flow_norm_q15) + "\n";
        a.payload += "crawler_drift_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.crawler_drift_norm_q15) + "\n";
        a.payload += "crawler_interference_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.crawler_interference_norm_q15) + "\n";
        a.payload += "crawler_coherence_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.crawler_coherence_norm_q15) + "\n";
        a.payload += "temporal_memory_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.temporal_memory_norm_q15) + "\n";
        a.payload += "temporal_prediction_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.temporal_prediction_norm_q15) + "\n";
        a.payload += "gpu_gradient_energy_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.gpu_gradient_energy_norm_q15) + "\n";
        a.payload += "gpu_spider_f_code_i32: " + std::to_string((int64_t)ai_data_substrate_telemetry.gpu_spider_f_code_i32) + "\n";
        a.payload += "gpu_spider_a_code_u16: " + std::to_string((uint32_t)ai_data_substrate_telemetry.gpu_spider_a_code_u16) + "\n";
        a.payload += "gpu_spider_v_code_u16: " + std::to_string((uint32_t)ai_data_substrate_telemetry.gpu_spider_v_code_u16) + "\n";
        a.payload += "gpu_spider_i_code_u16: " + std::to_string((uint32_t)ai_data_substrate_telemetry.gpu_spider_i_code_u16) + "\n";
        a.payload += "api_decode_count_u16: " + std::to_string((uint32_t)ai_data_substrate_telemetry.api_decode_count_u16) + "\n";
        a.payload += "api_decode_http_status_s32: " + std::to_string((int64_t)ai_data_substrate_telemetry.api_decode_http_status_s32) + "\n";
        a.payload += "api_decode_norm_q15: " + std::to_string((uint32_t)ai_data_substrate_telemetry.api_decode_norm_q15) + "\n";
        a.payload += "api_decode_f_code_i32: " + std::to_string((int64_t)ai_data_substrate_telemetry.api_decode_f_code_i32) + "\n";
        a.payload += "error_q32_32: " + std::to_string(ai_data_substrate_telemetry.error_q32_32) + "\n";
        a.payload += "error_delta_q32_32: " + std::to_string(ai_data_substrate_telemetry.error_delta_q32_32) + "\n";
        a.payload += "error_integral_q32_32: " + std::to_string(ai_data_substrate_telemetry.error_integral_q32_32) + "\n";
        a.payload += "temporal_prediction_q32_32: " + std::to_string(ai_data_substrate_telemetry.temporal_prediction_q32_32) + "\n";
        for (uint32_t i = 0; i < 8u; ++i) {
            a.payload += std::string("intent_band_q15_") + std::to_string((unsigned long long)i) + ": " +
                         std::to_string((uint32_t)ai_data_substrate_telemetry.intent_band_q15[i]) + "\n";
            a.payload += std::string("measured_band_q15_") + std::to_string((unsigned long long)i) + ": " +
                         std::to_string((uint32_t)ai_data_substrate_telemetry.measured_band_q15[i]) + "\n";
        }

        if (ai_action_log_count_u32 > 0) {
            const uint32_t idx = (ai_action_log_head_u32 + AI_ACTION_LOG_CAP - 1u) % AI_ACTION_LOG_CAP;
            const EwAiActionEvent& e = ai_action_log[idx];
            a.payload += "\n# Latest AI Action\n\n";
            a.payload += "action_tick_u64: " + std::to_string(e.tick_u64) + "\n";
            a.payload += "sig9_u64: " + std::to_string(e.sig9_u64) + "\n";
            a.payload += "class_id_u32: " + std::to_string(e.class_id_u32) + "\n";
            a.payload += "kind_u16: " + std::to_string((uint32_t)e.kind_u16) + "\n";
            a.payload += "profile_id_u16: " + std::to_string((uint32_t)e.profile_id_u16) + "\n";
            a.payload += "target_anchor_id_u32: " + std::to_string(e.target_anchor_id_u32) + "\n";
            a.payload += "f_code_i32: " + std::to_string((int64_t)e.f_code_i32) + "\n";
            a.payload += "a_code_u32: " + std::to_string(e.a_code_u32) + "\n";
        }

        const EwCoherenceResult cr = EwCoherenceGate::validate_artifact(a.rel_path, a.kind_u32, a.payload);
        a.coherence_q15 = cr.coherence_q15;
        a.commit_ready = cr.commit_ready;
        a.denial_code_u32 = cr.denial_code_u32;
        (void)inspector_fields.upsert(a);
    }

    ai_commands_count_u32 = 0u;
    ai_pulse_q63 = 0;
    ai_total_weight_q63 = 0;

    // Inputs are consumed only on successful tick boundary.
    // ------------------------------------------------------------------
    // Render object packets (anchors -> renderer)
    // Renderer must not own authoritative object transforms.
    // ------------------------------------------------------------------
    {
        render_object_packets.clear();
        render_object_packets.reserve(64);
        for (uint32_t ai = 1u; ai < (uint32_t)anchors.size(); ++ai) {
            const Anchor& a = anchors[ai];
            if (a.kind_u32 == EW_ANCHOR_KIND_OBJECT) {
                EwRenderObjectPacket p{};
                p.object_id_u64 = a.object_id_u64;
                p.anchor_id_u32 = a.id;
                p.pos_q16_16[0] = a.object_state.pos_q16_16[0];
                p.pos_q16_16[1] = a.object_state.pos_q16_16[1];
                p.pos_q16_16[2] = a.object_state.pos_q16_16[2];
                p.radius_q16_16 = 0;
                p.albedo_rgba8 = 0xFFFFFFFFu;
                p.atmosphere_rgba8 = 0u;
                p.atmosphere_thickness_q16_16 = 0;
                p.emissive_q16_16 = 0;
                render_object_packets.push_back(p);
            } else if (a.kind_u32 == EW_ANCHOR_KIND_PLANET) {
                EwRenderObjectPacket p{};
                p.object_id_u64 = a.object_id_u64;
                p.anchor_id_u32 = a.id;
                p.pos_q16_16[0] = a.planet_state.pos_q16_16[0];
                p.pos_q16_16[1] = a.planet_state.pos_q16_16[1];
                p.pos_q16_16[2] = a.planet_state.pos_q16_16[2];
                p.radius_q16_16 = a.planet_state.radius_m_q16_16;
                p.albedo_rgba8 = a.planet_state.albedo_rgba8;
                p.atmosphere_rgba8 = a.planet_state.atmosphere_rgba8;
                p.atmosphere_thickness_q16_16 = a.planet_state.atmosphere_thickness_m_q16_16;
                p.emissive_q16_16 = a.planet_state.emissive_q16_16;
                render_object_packets.push_back(p);
            }
        }
        render_object_packets_tick_u64 = canonical_tick;
    }

    // ------------------------------------------------------------------
    // Determinism guardrail: 9D state fingerprint.
    // If a reference trace exists, fail closed on first mismatch.
    // ------------------------------------------------------------------
    if (!determinism_ref_loaded) {
        (void)ge_load_fingerprint_reference("Draft Container/Determinism/fingerprint_ref.txt", determinism_ref_fingerprints);
        determinism_ref_loaded = true;
    }
    state_fingerprint_u64 = ge_compute_state_fingerprint_9d(this);
    state_fingerprint_tick_u64 = canonical_tick;
    if (!determinism_ref_fingerprints.empty()) {
        const uint64_t idx = canonical_tick;
        if (idx < determinism_ref_fingerprints.size()) {
            const uint64_t expect = determinism_ref_fingerprints[(size_t)idx];
            if (expect != 0u && expect != state_fingerprint_u64) {
                emit_ui_line("DETERMINISM_FINGERPRINT_MISMATCH");
                // Fail closed deterministically.
                std::abort();
            }
        }
    }

    inbound.clear();
    pending_text_x_q = 0;
    pending_image_y_q = 0;
    pending_audio_z_q = 0;

    // Rebuild outbound pulses deterministically from committed state.
    outbound.clear();
    for (size_t i = 0; i < anchors.size(); ++i) {
        const Anchor& a = anchors[i];
        Basis9 proj = projected_for(a);
        const SpiderCode4 sc = a.spider_encode_9d_4(proj, weights_q10, denom_q);
        const int32_t f_code = sc.f_code;
        const uint16_t a_code = sc.a_code;
        const uint16_t v_code = sc.v_code;
        const uint16_t i_code = sc.i_code;
        // Outbound pulses use the default profile and zero causal_tag.
        outbound.push_back({a.id, f_code, a_code, v_code, i_code, (uint8_t)EW_PROFILE_CORE_EVOLUTION, 0u, 0u, 0u, canonical_tick});
    }

    // Refresh carrier ring for the next tick. Keep the ring bounded and
    // deterministic (one pulse per anchor).
    carrier_ring = outbound;
}
}

void SubstrateManager::inject_text_utf8(const char* utf8) {
    if (!utf8) return;
    const std::string s(utf8);

    // Spec 5.11.3/5.11.4 + Spec 3.5:
    // TEXT encodes as a frequency coefficient (f_code) derived from UTF-8
    // bytes via the spider graph compressor under the language injection
    // profile. The resulting frequency is then applied as the TEXT->x driver.
    const int32_t f_code = ew_text_utf8_to_frequency_code(s, (uint8_t)EW_PROFILE_LANGUAGE_INJECTION);

    // Convert f_code to a turns-domain delta and apply per-axis scaling (sx).
    const int64_t delta_turns = static_cast<int64_t>(f_code) * (TURN_SCALE / F_SCALE);
    __int128 p = (__int128)delta_turns * (__int128)sx_q32_32;
    const int64_t scaled_turns = (int64_t)(p >> 32);
    pending_text_x_q = wrap_turns(pending_text_x_q + scaled_turns);
}

void SubstrateManager::inject_image_pixels_u8(const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return;

    // IMAGE encoding is frequency-derived and collapsed into a single carrier.
    // The raw RGBA byte stream is treated as an artifact and mapped through
    // the spider encoder.
    const size_t len = (size_t)width * (size_t)height * 4u;
    const int32_t f_code = ew_bytes_to_frequency_code(rgba, len, (uint8_t)EW_PROFILE_IMAGE);

    const int64_t delta_turns = static_cast<int64_t>(f_code) * (TURN_SCALE / F_SCALE);
    __int128 p = (__int128)delta_turns * (__int128)sy_q32_32;
    const int64_t scaled_turns = (int64_t)(p >> 32);
    pending_image_y_q = wrap_turns(pending_image_y_q + scaled_turns);
}

void SubstrateManager::inject_audio_pcm16(const int16_t* pcm, int samples, int channels) {
    if (!pcm || samples <= 0 || channels <= 0) return;

    // AUDIO encoding is frequency-derived and collapsed into a single carrier.
    // The raw PCM stream is treated as an artifact and mapped through
    // the spider encoder.
    const size_t len = (size_t)samples * (size_t)channels * sizeof(int16_t);
    const int32_t f_code = ew_bytes_to_frequency_code((const uint8_t*)pcm, len, (uint8_t)EW_PROFILE_AUDIO);

    const int64_t delta_turns = static_cast<int64_t>(f_code) * (TURN_SCALE / F_SCALE);
    __int128 p = (__int128)delta_turns * (__int128)sz_q32_32;
    const int64_t scaled_turns = (int64_t)(p >> 32);
    pending_audio_z_q = wrap_turns(pending_audio_z_q + scaled_turns);
}

void SubstrateManager::build_viz_points(std::vector<EwVizPoint>& out) const {
    out.clear();

    // If a lattice projection tag is enabled and the GPU lattice exists, project
    // a deterministic point cloud from a radiance slice. This is read-only.
    EwFieldLatticeGpu* viz_lat = nullptr;
    if (lattice_proj_tag_.enabled) {
        const uint32_t sel = lattice_proj_tag_.lattice_sel_u32;
        if (sel == 1u && lattice_probe_gpu_[0]) viz_lat = lattice_probe_gpu_[0].get();
        else if (lattice_gpu_) viz_lat = lattice_gpu_.get();
    }

    if (lattice_proj_tag_.enabled && viz_lat) {
        const uint32_t gx = viz_lat->grid_x();
        const uint32_t gy = lattice_gpu_->grid_y();
        const uint32_t gz = lattice_gpu_->grid_z();
        if (gx != 0 && gy != 0 && gz != 0) {
            const uint32_t slice = (lattice_proj_tag_.slice_z_u32 < gz) ? lattice_proj_tag_.slice_z_u32 : (gz - 1u);
            std::vector<uint8_t> bgra;
            EwFieldFrameHeader hdr{};
            viz_lat->get_radiance_slice_bgra8(slice, bgra, hdr);

            const uint32_t stride = (lattice_proj_tag_.stride_u32 == 0) ? 1u : lattice_proj_tag_.stride_u32;
            const uint32_t max_pts = (lattice_proj_tag_.max_points_u32 == 0) ? 1u : lattice_proj_tag_.max_points_u32;
            const uint8_t min_i = (uint8_t)lattice_proj_tag_.intensity_min_u8;
            out.reserve((size_t)std::min<uint32_t>(max_pts, gx * gy));

            auto q16_16_from_unit = [](double u)->int32_t {
                // u in [-1,1]
                const double v = u * 65536.0;
                if (v >= (double)INT32_MAX) return INT32_MAX;
                if (v <= (double)INT32_MIN) return INT32_MIN;
                return (int32_t)v;
            };

            uint32_t pushed = 0;
            const size_t pixels = (size_t)gx * (size_t)gy;
            if (bgra.size() >= pixels * 4u) {
                for (uint32_t y = 0; y < gy; y += stride) {
                    for (uint32_t x = 0; x < gx; x += stride) {
                        const size_t idx = ((size_t)y * (size_t)gx + (size_t)x) * 4u;
                        const uint8_t b = bgra[idx + 0];
                        const uint8_t g = bgra[idx + 1];
                        const uint8_t r = bgra[idx + 2];
                        const uint8_t a = bgra[idx + 3];
                        const uint8_t m = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
                        if (m < min_i) continue;
                        EwVizPoint p{};
                        const double fx = (gx > 1u) ? ((double)x / (double)(gx - 1u)) : 0.0;
                        const double fy = (gy > 1u) ? ((double)y / (double)(gy - 1u)) : 0.0;
                        const double fz = (gz > 1u) ? ((double)slice / (double)(gz - 1u)) : 0.0;
                        p.x_q16_16 = q16_16_from_unit(fx * 2.0 - 1.0);
                        p.y_q16_16 = q16_16_from_unit(fy * 2.0 - 1.0);
                        p.z_q16_16 = q16_16_from_unit(fz * 2.0 - 1.0);
                        p.rgba8 = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
                        p.anchor_id = 0u;
                        out.push_back(p);
                        if (++pushed >= max_pts) return;
                    }
                }
                return;
            }
        }
        // Fallthrough to anchor projection if slice extraction is unavailable.
    }

    // Default: anchor basis points.
    out.reserve(anchors.size());
    for (size_t i = 0; i < anchors.size(); ++i) {
        const Anchor& a = anchors[i];
        EwVizPoint p{};
        p.anchor_id = a.id;
        // Spatial manifold metric includes global expansion a(t) driven by H0.
        const int32_t x0 = q16_16_from_turns(a.basis9.d[0]);
        const int32_t y0 = q16_16_from_turns(a.basis9.d[1]);
        const int32_t z0 = q16_16_from_turns(a.basis9.d[2]);
        p.x_q16_16 = q16_16_mul_q32_32(x0, boundary_scale_q32_32);
        p.y_q16_16 = q16_16_mul_q32_32(y0, boundary_scale_q32_32);
        p.z_q16_16 = q16_16_mul_q32_32(z0, boundary_scale_q32_32);

        // Color: map chi_q and theta_q deterministically.
        const uint8_t r = (uint8_t)((a.chi_q / 1024) & 0xFF);
        const uint8_t g = (uint8_t)((a.theta_q / 1024) & 0xFF);
        const uint8_t b = (uint8_t)((a.m_q / 1024) & 0xFF);
        p.rgba8 = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | (0xFFu << 24);
        out.push_back(p);
    }
}

bool SubstrateManager::check_invariants() const {
    for (size_t i = 0; i < anchors.size(); ++i) {
        const Anchor& a = anchors[i];
        if (a.theta_q < 0 || a.theta_q >= TURN_SCALE) return false;
        if (a.chi_q < 0) return false;
        if (a.m_q < 0) return false;
    }
    return true;
}

bool SubstrateManager::hydrate_workspace_to(const std::string& root_dir) {
    std::vector<EwInspectorArtifact> committed;
    inspector_fields.snapshot_committed(committed);

    // Nothing to do.
    if (committed.empty()) {
        last_hydration_receipt.rows.clear();
        last_hydration_receipt.hydration_tick_u64 = canonical_tick;
        last_hydration_error_code_u32 = 0;
        return true;
    }

    EwHydrationReceipt receipt;
    std::string err;
    const bool ok = EwHydrator::hydrate_workspace(root_dir, canonical_tick, committed, receipt, err);
    last_hydration_receipt = receipt;
    if (!ok) {
        // Deterministic coarse error coding (no strings in state).
        last_hydration_error_code_u32 = 9001u;
        return false;
    }

    // Clear commit-ready flags on successful hydration.
    inspector_fields.clear_commit_ready();
    last_hydration_error_code_u32 = 0;
    return true;
}

uint32_t SubstrateManager::corpus_query_best_score(const std::string& query_utf8) {
    std::string qlc;
    qlc.reserve(query_utf8.size());
    for (size_t i = 0; i < query_utf8.size(); ++i) {
        char c = query_utf8[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((unsigned char)c >= 32 && (unsigned char)c <= 126) qlc.push_back(c);
    }
    while (!qlc.empty() && (qlc[0] == ' ' || qlc[0] == '\t')) qlc.erase(qlc.begin());
    if (qlc.empty()) return 0u;

    std::vector<EwInspectorArtifact> corpus;
    inspector_fields.snapshot_prefix("Draft Container/Corpus/", corpus);

    auto score_text = [&](std::string s)->uint32_t {
        if (s.size() > 8192) s.resize(8192);
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if ((unsigned char)c < 32 || (unsigned char)c > 126) c = ' ';
            s[i] = c;
        }
        uint32_t cnt = 0;
        size_t pos = 0;
        while (true) {
            pos = s.find(qlc, pos);
            if (pos == std::string::npos) break;
            cnt += 1u;
            pos += qlc.size();
            if (cnt >= 255u) break;
        }
        return cnt;
    };

    uint32_t best = 0u;
    for (size_t i = 0; i < corpus.size(); ++i) {
        const std::string& p = corpus[i].rel_path;
        bool ok = false;
        if (p.find(".pdf_text.txt") != std::string::npos) ok = true;
        if (p.find(".json_text.txt") != std::string::npos) ok = true;
        if (p.find(".xml_text.txt") != std::string::npos) ok = true;
        if (p.find(".manifest.txt") != std::string::npos) ok = true;
        if (p.find(".search_results.txt") != std::string::npos) ok = true;
        if (!ok) continue;
        const uint32_t sc = score_text(corpus[i].payload);
        if (sc > best) best = sc;
        if (best >= 255u) break;
    }
    return best;
}

void SubstrateManager::corpus_query_emit_results(const std::string& query_utf8, uint32_t context_anchor_id_u32) {
    std::string qlc;
    qlc.reserve(query_utf8.size());
    for (size_t i = 0; i < query_utf8.size(); ++i) {
        char c = query_utf8[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((unsigned char)c >= 32 && (unsigned char)c <= 126) qlc.push_back(c);
    }
    while (!qlc.empty() && (qlc[0] == ' ' || qlc[0] == '\t')) qlc.erase(qlc.begin());
    if (qlc.empty()) return;

    std::vector<EwInspectorArtifact> corpus;
    inspector_fields.snapshot_prefix("Draft Container/Corpus/", corpus);

    struct Hit { std::string path; uint32_t score; };
    std::vector<Hit> hits;
    hits.reserve(64);

    auto score_text = [&](std::string s)->uint32_t {
        if (s.size() > 8192) s.resize(8192);
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if ((unsigned char)c < 32 || (unsigned char)c > 126) c = ' ';
            s[i] = c;
        }
        uint32_t cnt = 0;
        size_t pos = 0;
        while (true) {
            pos = s.find(qlc, pos);
            if (pos == std::string::npos) break;
            cnt += 1u;
            pos += qlc.size();
            if (cnt >= 255u) break;
        }
        return cnt;
    };

    for (size_t i = 0; i < corpus.size(); ++i) {
        const std::string& p = corpus[i].rel_path;
        bool ok = false;
        if (p.find(".pdf_text.txt") != std::string::npos) ok = true;
        if (p.find(".json_text.txt") != std::string::npos) ok = true;
        if (p.find(".xml_text.txt") != std::string::npos) ok = true;
        if (p.find(".manifest.txt") != std::string::npos) ok = true;
        if (p.find(".search_results.txt") != std::string::npos) ok = true;
        if (!ok) continue;
        const uint32_t sc = score_text(corpus[i].payload);
        if (sc == 0u) continue;
        hits.push_back(Hit{p, sc});
    }

    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b){
        if (a.score != b.score) return a.score > b.score;
        return a.path < b.path;
    });
    if (hits.size() > 10) hits.resize(10);

    EwInspectorArtifact out{};
    out.coord_coord9_u64 = ((uint64_t)canonical_tick << 1) ^ 0x51555259U;
    out.kind_u32 = EW_ARTIFACT_TEXT;
    out.rel_path = "Draft Container/Corpus/query_results.txt";
    out.producer_tick_u64 = canonical_tick;
    out.payload = "QUERY_RESULTS ";
    out.payload += std::to_string((unsigned long long)canonical_tick);
    out.payload += " CONTEXT ";
    out.payload += std::to_string((unsigned long long)context_anchor_id_u32);
    out.payload += "\n";

auto cite_for_rel_path = [&](const std::string& rel_path, std::string& out_cite, std::string& out_link)->bool {
    out_cite.clear();
    out_link.clear();
    const std::string pre = "Draft Container/Corpus/doc_";
    size_t p0 = rel_path.find(pre);
    if (p0 == std::string::npos) return false;
    p0 += pre.size();
    size_t p1 = rel_path.find('.', p0);
    if (p1 == std::string::npos) return false;
    std::string id_s = rel_path.substr(p0, p1 - p0);
    if (id_s.empty()) return false;
    uint64_t id = 0u;
    for (size_t k = 0; k < id_s.size(); ++k) {
        const char c = id_s[k];
        if (c < '0' || c > '9') break;
        id = id * 10u + (uint64_t)(c - '0');
        if (id > (uint64_t)0xFFFFFFFFFFFFFFF0ULL) break;
    }
    if (id == 0u) return false;
    for (size_t j = manifest_records.size(); j > 0; --j) {
        const EwManifestRecord& mr = manifest_records[j - 1];
        if (mr.request_id_u64 != id) continue;
        out_cite = mr.domain_utf8;
        out_cite.push_back(' ');
        out_cite += mr.path_utf8;
        if (out_cite.size() > 196) out_cite.resize(196);

        // Site link: prefer https in the UI surface (policy-gated fetch enforces scheme separately).
        // Keep bounded and ASCII-clean.
        out_link = "https://";
        out_link += mr.domain_utf8;
        out_link += mr.path_utf8;
        if (out_link.size() > 240) out_link.resize(240);
        return true;
    }
    return false;
};

for (size_t i = 0; i < hits.size(); ++i) {
    out.payload += std::to_string((unsigned long long)hits[i].score);
    out.payload += " ";
    out.payload += hits[i].path;
    std::string cite;
    std::string link;
    if (cite_for_rel_path(hits[i].path, cite, link)) {
        out.payload += " | CITE ";
        out.payload += cite;
        out.payload += " | LINK ";
        out.payload += link;
    }
    out.payload += "\n";
}
    inspector_fields.upsert(out);
    if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("QUERY_RESULTS_READY");

    // Emit top citations to UI for quick click/copy in editor.
    const size_t ui_cap = 3;
    for (size_t i = 0; i < hits.size() && i < ui_cap; ++i) {
        std::string cite;
        std::string link;
        if (!cite_for_rel_path(hits[i].path, cite, link)) continue;
        if (ui_out_q.size() >= UI_OUT_CAP) break;
        std::string line = "C";
        line += std::to_string((unsigned long long)(i + 1));
        line += " ";
        if (link.size() > 160) link.resize(160);
        line += link;
        ui_out_q.push_back(line);
    }
}


void SubstrateManager::corpus_answer_emit(const std::string& query_utf8, uint32_t context_anchor_id_u32) {
    (void)context_anchor_id_u32;

    // Global coherence gate: AI output must be restricted by the *global* coherence,
    // not merely local resonance. If coherence is low, fail closed and
    // deterministically request evidence (allowlist crawl / experiments) instead.
    if (global_coherence.global_q15 < global_coherence_gate_min_q15) {
        if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("AI_GATE_LOW_GLOBAL_COHERENCE");

        // Deterministic behavior:
        // - emit a PlanPacket to nudge automation
        // - emit a CrawlRequest to gather allowlisted evidence (if allowed)
        genesis::AutoArtifact plan{};
        plan.kind = genesis::AutoArtifactKind::PlanPacket;
        plan.created_tick_u64 = canonical_tick_u64();
        plan.lane_u32 = 0u;
        plan.payload_utf8 = "{\"plan\":\"evidence_needed\",\"reason\":\"low_global_coherence\",\"tol_percent\":6}";
        learning_automation.bus().push(plan);

        // If allowlist not loaded yet, start default allowlist crawl deterministically.
        if (!corpus_allowlist_loaded) {
            corpus_crawl_start_neuralis_corpus_default();
        }

        // Issue a bounded CrawlRequest referencing the query. Scheduler will pick canonical ordering.
        genesis::AutoArtifact cr{};
        cr.kind = genesis::AutoArtifactKind::CrawlRequest;
        cr.created_tick_u64 = canonical_tick_u64();
        cr.lane_u32 = 0u;
        // Payload is small JSON-ish and ASCII-safe.
        std::string q = query_utf8;
        if (q.size() > 196) q.resize(196);
        for (size_t i = 0; i < q.size(); ++i) {
            unsigned char b = (unsigned char)q[i];
            if (b < 0x20 || b == 0x7F) q[i] = ' ';
        }
        cr.payload_utf8 = std::string("{\"crawl\":\"query_seed\",\"q\":\"") + q + "\",\"lane_max\":" + std::to_string((unsigned long long)crawl_allowlist_lane_max_u32) + "}";
        learning_automation.bus().push(cr);

        // Deterministic UI guidance (no speculative answer).
        if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("AI_NEEDS_EVIDENCE:run_allowlist_crawl_or_provide_more_context");
        return;
    }

    // Deterministic, extractive "answer": pick best-matching corpus artifacts and
    // emit bounded ASCII snippets around query term matches.
    std::string qlc;
    qlc.reserve(query_utf8.size());
    for (size_t i = 0; i < query_utf8.size(); ++i) {
        char c = query_utf8[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((unsigned char)c >= 32 && (unsigned char)c <= 126) qlc.push_back(c);
    }
    while (!qlc.empty() && (qlc[0] == ' ' || qlc[0] == '\t')) qlc.erase(qlc.begin());
    if (qlc.empty()) return;

    // Tokenize query into up to 6 terms (len>=3), ASCII-lower.
    std::vector<std::string> terms;
    terms.reserve(6);
    {
        std::string cur;
        for (size_t i = 0; i <= qlc.size(); ++i) {
            const char c = (i < qlc.size()) ? qlc[i] : ' ';
            const bool is_alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            if (is_alnum) {
                if (cur.size() < 32) cur.push_back(c);
            } else {
                if (cur.size() >= 3) {
                    bool dup = false;
                    for (size_t k = 0; k < terms.size(); ++k) { if (terms[k] == cur) { dup = true; break; } }
                    if (!dup) {
                        terms.push_back(cur);
                        if (terms.size() >= 6) break;
                    }
                }
                cur.clear();
            }
        }
    }
    if (terms.empty()) return;

    std::vector<EwInspectorArtifact> corpus;
    inspector_fields.snapshot_prefix("Draft Container/Corpus/", corpus);

    auto is_queryable = [&](const std::string& p)->bool {
        bool ok = false;
        if (p.find(".pdf_text.txt") != std::string::npos) ok = true;
        if (p.find(".json_text.txt") != std::string::npos) ok = true;
        if (p.find(".xml_text.txt") != std::string::npos) ok = true;
        if (p.find(".manifest.txt") != std::string::npos) ok = true;
        if (p.find(".search_results.txt") != std::string::npos) ok = true;
        if (!ok) return false;
        // Skip our own derived outputs to avoid feedback loops.
        if (p.find("query_results.txt") != std::string::npos) return false;
        if (p.find("answer_") != std::string::npos) return false;
        return true;
    };
    auto normalize_utf8 = [&](std::string s)->std::string {
        if (s.size() > 8192) s.resize(8192);
	        for (size_t i = 0; i < s.size(); ++i) {
	            unsigned char b = (unsigned char)s[i];
	            // ASCII case-fold only (deterministic, UTF-8 safe).
	            if (b >= (unsigned char)'A' && b <= (unsigned char)'Z') {
	                b = (unsigned char)(b - (unsigned char)'A' + (unsigned char)'a');
	            }
	            // Screen control bytes; keep UTF-8 bytes as-is.
	            if ((b < 0x20 && b != (unsigned char)'\n' && b != (unsigned char)'\r' && b != (unsigned char)'\t') || b == 0x7F) {
	                b = (unsigned char)' ';
	            }
	            s[i] = (char)b;
	        }
        return s;
    };


    struct Hit { size_t idx; uint32_t score; };
    std::vector<Hit> hits;
    hits.reserve(64);

    for (size_t i = 0; i < corpus.size(); ++i) {
        const std::string& p = corpus[i].rel_path;
        if (!is_queryable(p)) continue;

        std::string s0 = normalize_utf8(corpus[i].payload);
        uint32_t sc = 0u;
        for (size_t t = 0; t < terms.size(); ++t) {
            size_t pos = 0;
            uint32_t cnt = 0u;
            while (true) {
                pos = s0.find(terms[t], pos);
                if (pos == std::string::npos) break;
                cnt += 1u;
                pos += terms[t].size();
                if (cnt >= 64u) break;
            }
            sc += cnt;
            if (sc >= 255u) { sc = 255u; break; }
        }
        if (sc == 0u) continue;
        hits.push_back(Hit{i, sc});
    }

    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b){
        if (a.score != b.score) return a.score > b.score;
        return a.idx < b.idx;
    });
    if (hits.size() > 3) hits.resize(3);

    EwInspectorArtifact out{};
    out.coord_coord9_u64 = ((uint64_t)canonical_tick << 1) ^ 0x414E5357U; // 'ANSW'
    out.kind_u32 = EW_ARTIFACT_TEXT;
    out.rel_path = std::string("Draft Container/Corpus/answer_") + std::to_string((unsigned long long)canonical_tick) + ".txt";
    out.producer_tick_u64 = canonical_tick;

    out.payload = "ANSWER ";
    out.payload += std::to_string((unsigned long long)canonical_tick);
    out.payload += "\nQ ";
    out.payload += qlc;
    out.payload += "\n";

    if (hits.empty()) {
        out.payload += "NO_MATCH\n";
        inspector_fields.upsert(out);
        if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("AI_NO_MATCH");
        return;
    }

    // Reuse the same citation helper used by query results (local lambda copy).
    auto cite_for_rel_path = [&](const std::string& rel_path, std::string& out_cite, std::string& out_link)->bool {
        out_cite.clear();
        out_link.clear();
        const std::string pre = "Draft Container/Corpus/doc_";
        size_t p0 = rel_path.find(pre);
        if (p0 == std::string::npos) return false;
        p0 += pre.size();
        size_t p1 = rel_path.find('.', p0);
        if (p1 == std::string::npos) return false;
        std::string id_s = rel_path.substr(p0, p1 - p0);
        if (id_s.empty()) return false;
        uint64_t id = 0u;
        for (size_t k = 0; k < id_s.size(); ++k) {
            const char c = id_s[k];
            if (c < '0' || c > '9') break;
            id = id * 10u + (uint64_t)(c - '0');
            if (id > (uint64_t)0xFFFFFFFFFFFFFFF0ULL) break;
        }
        if (id == 0u) return false;
        for (size_t j = manifest_records.size(); j > 0; --j) {
            const EwManifestRecord& mr = manifest_records[j - 1];
            if (mr.request_id_u64 != id) continue;
            out_cite = mr.domain_utf8;
            out_cite.push_back(' ');
            out_cite += mr.path_utf8;
            if (out_cite.size() > 196) out_cite.resize(196);

            out_link = "https://";
            out_link += mr.domain_utf8;
            out_link += mr.path_utf8;
            if (out_link.size() > 240) out_link.resize(240);
            return true;
        }
        return false;
    };

    auto extract_snippet = [&](const std::string& payload, const std::string& term)->std::string {
        std::string s0 = normalize_utf8(payload);
        size_t pos = s0.find(term);
        if (pos == std::string::npos) return std::string();
        const size_t win = 220;
        size_t a = (pos > win) ? (pos - win) : 0;
        size_t b = pos + term.size() + win;
        if (b > s0.size()) b = s0.size();
        std::string sn = s0.substr(a, b - a);
        // collapse runs of spaces
        std::string out2;
        out2.reserve(sn.size());
        bool last_space = false;
        for (size_t i = 0; i < sn.size(); ++i) {
            char c = sn[i];
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
            if (c == ' ') {
                if (!last_space) out2.push_back(' ');
                last_space = true;
            } else {
                out2.push_back(c);
                last_space = false;
            }
        }
        while (!out2.empty() && out2[0] == ' ') out2.erase(out2.begin());
        while (!out2.empty() && out2.back() == ' ') out2.pop_back();
        if (out2.size() > 480) out2.resize(480);
        return out2;
    };

    out.payload += "HITS ";
    out.payload += std::to_string((unsigned long long)hits.size());
    out.payload += "\n";

    if (ui_out_q.size() < UI_OUT_CAP) ui_out_q.push_back("AI_ANSWER_READY");

    for (size_t hi = 0; hi < hits.size(); ++hi) {
        const EwInspectorArtifact& art = corpus[hits[hi].idx];
        out.payload += "SRC ";
        out.payload += art.rel_path;
        out.payload += " SCORE ";
        out.payload += std::to_string((unsigned long long)hits[hi].score);

        std::string cite;
        std::string link;
        if (cite_for_rel_path(art.rel_path, cite, link)) {
            out.payload += " | CITE ";
            out.payload += cite;
            out.payload += " | LINK ";
            out.payload += link;
        }
        out.payload += "\n";

        // Emit up to 2 snippets per hit.
        uint32_t emitted = 0u;
        for (size_t ti = 0; ti < terms.size() && emitted < 2u; ++ti) {
            std::string sn = extract_snippet(art.payload, terms[ti]);
            if (sn.empty()) continue;
            out.payload += "SNIP ";
            out.payload += sn;
            out.payload += "\n";

            if (ui_out_q.size() < UI_OUT_CAP) {
                std::string line = "AI ";
                line += sn;
                if (line.size() > 220) line.resize(220);
                ui_out_q.push_back(line);
            }
            emitted++;
        }
    }

    inspector_fields.upsert(out);
}




// ------------------------------------------------------------------
// XR eye pose ingress and projected view packets
// ------------------------------------------------------------------

void SubstrateManager::submit_xr_eye_pose_f32(uint32_t eye_index_u32, const float pos_xyz_f32[3], const float rot_xyzw_f32[4], uint64_t tick_u64) {
    if (eye_index_u32 >= 2u) return;
    EwXrEyePoseF32& ep = xr_eye_pose_f32[eye_index_u32];
    if (pos_xyz_f32) {
        ep.pos_xyz_f32[0] = pos_xyz_f32[0];
        ep.pos_xyz_f32[1] = pos_xyz_f32[1];
        ep.pos_xyz_f32[2] = pos_xyz_f32[2];
    }
    if (rot_xyzw_f32) {
        ep.rot_xyzw_f32[0] = rot_xyzw_f32[0];
        ep.rot_xyzw_f32[1] = rot_xyzw_f32[1];
        ep.rot_xyzw_f32[2] = rot_xyzw_f32[2];
        ep.rot_xyzw_f32[3] = rot_xyzw_f32[3];
    }
    ep.tick_u64 = tick_u64;
    ep.valid_u32 = 1u;
}

bool SubstrateManager::get_render_xr_eye_packet(uint32_t eye_index_u32, EwRenderXrEyePacket* out) const {
    if (!out || eye_index_u32 >= 2u) return false;
    if (render_xr_eye_packet_tick_u64[eye_index_u32] == 0u) return false;
    *out = render_xr_eye_packet[eye_index_u32];
    return true;
}

bool SubstrateManager::get_process_substrate_telemetry(EwProcessSubstrateTelemetry* out) const {
    if (!out) return false;
    if (process_substrate_telemetry.valid_u32 == 0u) return false;
    *out = process_substrate_telemetry;
    return true;
}

bool SubstrateManager::get_subsystem_substrate_telemetry(EwSubsystemSubstrateTelemetry* out) const {
    if (!out) return false;
    if (subsystem_substrate_telemetry.valid_u32 == 0u) return false;
    *out = subsystem_substrate_telemetry;
    return true;
}

bool SubstrateManager::get_ai_substrate_telemetry(EwAiSubstrateTelemetry* out) const {
    if (!out) return false;
    if (ai_substrate_telemetry.valid_u32 == 0u) return false;
    *out = ai_substrate_telemetry;
    return true;
}

bool SubstrateManager::get_ai_data_substrate_telemetry(EwAiDataSubstrateTelemetry* out) const {
    if (!out) return false;
    if (ai_data_substrate_telemetry.valid_u32 == 0u) return false;
    *out = ai_data_substrate_telemetry;
    return true;
}
