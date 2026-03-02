#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ew_types.h"

class SubstrateManager;
struct EwVizPoint;
struct EwExternalApiRequest;
struct EwExternalApiResponse;

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

