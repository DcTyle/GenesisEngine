#pragma once
#include <cstdint>
#include <vector>

// Compute emergent realism carrier triple
//   x = leak/density proxy (Q16.16 in [0,1])
//   y = doppler_k (Q16.16 in [-1,1])
//   z = packed: low16=harm_mean Q0.15, high16=flux_gradient_proxy Q0.15
// for a batch of anchor indices.
//
// Determinism contract:
// - No hashing/crypto.
// - Per-element computation only; no cross-thread reductions/atomics.
// - Uses only integer arithmetic.
//
// Build contract:
// - When CUDA is enabled, this runs on GPU.
// - When CUDA is disabled, this runs on CPU with identical integer math.

struct EwCarrierTriple {
    uint32_t x_u32;
    uint32_t y_u32;
    uint32_t z_u32;
};

class Anchor;

bool ew_compute_carrier_triples_for_anchor_ids(
    const std::vector<Anchor>& anchors,
    const std::vector<uint32_t>& anchor_ids,
    std::vector<EwCarrierTriple>& out_triples
);
