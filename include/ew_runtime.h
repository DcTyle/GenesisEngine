#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ew_types.h"

class SubstrateMicroprocessor;
struct EwVizPoint;
struct EwExternalApiRequest;
struct EwExternalApiResponse;
struct EwControlPacket;
struct EwRenderCameraPacket;

// Canonical runtime dispatcher surface per Spec v7.
// These are thin wrappers over the existing SubstrateMicroprocessor runtime to satisfy
// the canonical artifact contract and provide a stable include surface.

void ew_runtime_submit_pulsepacket(SubstrateMicroprocessor* sm, const PulsePacketV1* p);
void ew_runtime_submit_external_api_response(SubstrateMicroprocessor* sm, const EwExternalApiResponse& resp);
bool ew_runtime_pop_external_api_request(SubstrateMicroprocessor* sm, EwExternalApiRequest* out_req);

// Explicit language foundations bootstrap (dictionary/thesaurus/encyclopedia/speech).
// Returns true if at least one dataset was loaded and checkpoints enqueued.
bool ew_runtime_language_bootstrap(SubstrateMicroprocessor* sm, const char* root_dir_utf8);

// Visualization projection (UE/host inspection).
std::vector<EwVizPoint> ew_runtime_project_points(const SubstrateMicroprocessor* sm, uint32_t max_points_u32);

// Control surfaces (UI/editor/input) -> AI substrate.
bool ew_runtime_submit_control_packet(SubstrateMicroprocessor* sm, const EwControlPacket* p);

// Renderer read path: derive a render camera packet from the camera anchor.
bool ew_runtime_get_render_camera_packet(const SubstrateMicroprocessor* sm, EwRenderCameraPacket* out);

