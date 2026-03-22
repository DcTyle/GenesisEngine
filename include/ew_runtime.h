#pragma once
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
bool ew_runtime_get_render_object_packets(const SubstrateManager* sm,
                                          const EwRenderObjectPacket** out_packets,
                                          uint32_t* out_count_u32,
                                          uint64_t* out_tick_u64);


bool ew_runtime_get_render_assist_packet(const SubstrateManager* sm, EwRenderAssistPacket* out);
bool ew_runtime_get_render_xr_eye_packet(const SubstrateManager* sm, uint32_t eye_index_u32, EwRenderXrEyePacket* out);
