#pragma once
#include <cstdint>
#include "ew_cuda_api.h"

// Canonical CUDA backend interface surface (Blueprint/Spec v7).
// This backend is a GPU-side "subtrate microprocessor" hook: inputs are carried
// as carrier/harmonic-constrained super-compressed phase packets (not a ring buffer).

namespace ew_cuda_backend {

// Fixed-size super-compressed carrier packet.
// Semantics: payload contains phase-differential packed words (carrier-superpack),
// suitable for deterministic GPU tick ingestion.
struct PulseSuperpackV1 {
  uint64_t carrier_id_u64;      // carrier collapse / tick carrier
  uint32_t artifact_id_u32;     // op/artifact identity (e.g., OPK_* or internal)
  uint32_t value_tag_u32;       // value channel tag (theta/curv/doppler/etc.)
  int32_t  coord9_i32[9];       // 9D lane coordinate (addressing, not hashes)
  uint32_t payload_words_u32;   // number of valid 32-bit words in payload_u32
  uint32_t payload_u32[64];     // carrier-super-compressed words (max 256 bytes)
};

// Returns 0 on success.
int init_or_throw();

// Submit a super-compressed carrier packet.
// This is a mailbox-style overwrite (last-writer-wins) by design:
// it is deterministic, minimal-latency, and avoids ring-buffer semantics.
int submit_superpack(const PulseSuperpackV1* pkt);

// Run a single GPU tick using the last submitted packet.
// Returns 0 on success.
int run_tick();

// Shutdown resources (idempotent).
void shutdown();

} // namespace ew_cuda_backend
