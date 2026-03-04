#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ew_types.h"

class SubstrateManager;
struct EwVizPoint;
struct EwExternalApiRequest;
struct EwExternalApiResponse;
struct EwControlPacket;
struct EwRenderCameraPacket;
struct EwRenderAssistPacket;
struct EwRenderXrEyePacket;
struct EwRenderObjectPacket;

// Canonical runtime dispatcher surface per Spec v7.
// These are thin wrappers over the existing SubstrateManager runtime to satisfy
// the canonical artifact contract and provide a stable include surface.

void ew_runtime_submit_pulsepacket(SubstrateManager* sm, const PulsePacketV1* p);
void ew_runtime_submit_external_api_response(SubstrateManager* sm, const EwExternalApiResponse& resp);
bool ew_runtime_pop_external_api_request(SubstrateManager* sm, EwExternalApiRequest* out_req);

// Explicit language foundations bootstrap (dictionary/thesaurus/encyclopedia/speech).
// Returns true if at least one dataset was loaded and checkpoints enqueued.
bool ew_runtime_language_bootstrap(SubstrateManager* sm, const char* root_dir_utf8);

// Visualization projection (UE/host inspection).
std::vector<EwVizPoint> ew_runtime_project_points(const SubstrateManager* sm, uint32_t max_points_u32);

// Control surfaces (UI/editor/input) -> AI substrate.
bool ew_runtime_submit_control_packet(SubstrateManager* sm, const EwControlPacket* p);

// Renderer read path: derive a render camera packet from the camera anchor.
bool ew_runtime_get_render_camera_packet(const SubstrateManager* sm, EwRenderCameraPacket* out);

// Renderer read path: assist coefficients derived inside the substrate.
bool ew_runtime_get_render_assist_packet(const SubstrateManager* sm, EwRenderAssistPacket* out);

// Camera sensor sample ingress (observation only). The viewport may submit
// normalized median depth from the Vulkan histogram pipeline. The substrate
// converts to meters and performs autofocus inside operator execution.
void ew_runtime_submit_camera_sensor_median_norm(SubstrateManager* sm, int32_t median_depth_norm_q16_16, uint64_t tick_u64);

// XR eye pose ingress (observation only). The viewport submits raw OpenXR eye poses (position + quaternion).
// The substrate converts to fixed-point and projects per-eye view matrices deterministically.
void ew_runtime_submit_xr_eye_pose_f32(SubstrateManager* sm, uint32_t eye_index_u32, const float pos_xyz_f32[3], const float rot_xyzw_f32[4], uint64_t tick_u64);

// XR render read path: per-eye projected view matrix derived inside the substrate.
bool ew_runtime_get_render_xr_eye_packet(const SubstrateManager* sm, uint32_t eye_index_u32, EwRenderXrEyePacket* out);

// Renderer read path: projected object packets (objects + planets).
// Returns false if no packets are available.
bool ew_runtime_get_render_object_packets(const SubstrateManager* sm, const EwRenderObjectPacket** out_packets, size_t* out_count);

// Determinism fingerprint (9D fingerprint harness).
uint64_t ew_runtime_get_state_fingerprint_9d(const SubstrateManager* sm, uint64_t* out_tick_u64);

