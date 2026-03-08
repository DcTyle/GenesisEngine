#include "ew_runtime.h"
#include "GE_runtime.hpp"
#include "ew_ingress.h"
#include "GE_control_packets.hpp"
#include "GE_camera_anchor.hpp"

void ew_runtime_submit_pulsepacket(SubstrateManager* sm, const PulsePacketV1* p) {
    if (!sm || !p) return;
    if (ew_pulsepacketv1_validate(p) != EW_INGRESS_OK) return;

    // Map ingress into the canonical Pulse type.
    //
    // PulsePacketV1 expresses frequency + Q32.32 amplitude; the substrate ingests
    // deterministic pulses via f_code/a_code and routes them through qubit lanes.
    Pulse pp{};
    pp.anchor_id = 0u;
    pp.f_code = (p->freq_hz_u32 > 0x7FFFFFFFu) ? 0x7FFFFFFF : (int32_t)p->freq_hz_u32;

    // Deterministic amplitude mapping: |amp_q32_32| in Q32.32 -> unsigned 16-bit.
    // We take the high 16 bits of the fractional-scaled magnitude (Q32.32 >> 16),
    // clamped to 0..65535.
    int64_t amp_abs = (p->amp_q32_32 < 0) ? -p->amp_q32_32 : p->amp_q32_32;
    uint64_t a = (uint64_t)(amp_abs >> 16);
    if (a > 65535ull) a = 65535ull;
    pp.a_code = (uint16_t)a;
    // External pulsepack does not specify v/i codes; default to 0 (no gating power).
    pp.v_code = 0u;
    pp.i_code = 0u;

    pp.profile_id = 0u;
    pp.causal_tag = (uint8_t)(p->lane_mask_u32 & 0xFFu);
    pp.pad0 = 0u;
    pp.pad1 = 0u;
    pp.tick = p->tick_u64;

    sm->enqueue_inbound_pulse(pp);
}

void ew_runtime_submit_external_api_response(SubstrateManager* sm, const EwExternalApiResponse& resp) {
    if (!sm) return;
    sm->submit_external_api_response(resp);
}

bool ew_runtime_pop_external_api_request(SubstrateManager* sm, EwExternalApiRequest* out_req) {
    if (!sm || !out_req) return false;
    return sm->pop_external_api_request(*out_req);
}


bool ew_runtime_language_bootstrap(SubstrateManager* sm, const char* root_dir_utf8) {
    if (!sm || !root_dir_utf8) return false;
    return sm->language_bootstrap_from_dir(std::string(root_dir_utf8));
}

std::vector<EwVizPoint> ew_runtime_project_points(const SubstrateManager* sm, uint32_t max_points_u32) {
    if (!sm) return {};
    std::vector<EwVizPoint> pts;
    pts.reserve((max_points_u32 == 0u) ? 0u : max_points_u32);
    sm->build_viz_points(pts);
    if (max_points_u32 != 0u && pts.size() > (size_t)max_points_u32) pts.resize((size_t)max_points_u32);
    return pts;
}

bool ew_runtime_submit_control_packet(SubstrateManager* sm, const EwControlPacket* p) {
    if (!sm || !p) return false;
    return sm->control_packet_push(*p);
}

bool ew_runtime_get_render_camera_packet(const SubstrateManager* sm, EwRenderCameraPacket* out) {
    if (!sm || !out) return false;
    // Renderer consumes the projected packet computed inside the substrate tick.
    // Fail closed if not yet projected.
    if (sm->render_camera_packet_tick_u64 == 0u) return false;
    *out = sm->render_camera_packet;
    return true;
}



bool ew_runtime_get_render_assist_packet(const SubstrateManager* sm, EwRenderAssistPacket* out) {
    if (!sm || !out) return false;
    if (sm->render_assist_packet_tick_u64 == 0u) return false;
    *out = sm->render_assist_packet;
    return true;
}

bool ew_runtime_get_render_xr_eye_packet(const SubstrateManager* sm, uint32_t eye_index_u32, EwRenderXrEyePacket* out) {
    if (!sm || !out) return false;
    return sm->get_render_xr_eye_packet(eye_index_u32, out);
}
