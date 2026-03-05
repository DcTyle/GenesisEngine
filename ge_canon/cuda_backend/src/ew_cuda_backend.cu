#include <cuda_runtime.h>
#include <cstdint>
#include "../include/ew_cuda_backend.h"

// Canonical CUDA backend (Blueprint v7 V31-A).
// IMPORTANT:
// - No ring buffer: ingress uses a mailbox-style superpack overwrite.
// - Payload is treated as carrier-super-compressed phase-differential words.
// - This file is a stable hook point; authoritative evolution remains on CPU path
//   until a later pass migrates kernels end-to-end.

namespace ew_cuda_backend {

static bool g_inited = false;

// Device mailbox (single slot).
struct DeviceMailbox {
  uint64_t carrier_id_u64;
  uint32_t artifact_id_u32;
  uint32_t value_tag_u32;
  int32_t  coord9_i32[9];
  uint32_t payload_words_u32;
  uint32_t payload_u32[64];
};

__device__ DeviceMailbox g_mailbox;

// Simple deterministic no-op tick kernel.
// In later passes, this is where carrier micro-ops execute as GPU phase-dynamics.
__global__ void ew_tick_kernel() {
  // Read mailbox to prevent compiler from optimizing it away.
  DeviceMailbox mb = g_mailbox;
  // Deterministic "touch": fold a couple words into a local accumulator.
  uint32_t acc = mb.payload_words_u32 ^ (uint32_t)mb.carrier_id_u64 ^ mb.artifact_id_u32 ^ mb.value_tag_u32;
  if (mb.payload_words_u32 > 0) {
    acc ^= mb.payload_u32[0];
  }
  // Prevent unused warning.
  if (acc == 0xFFFFFFFFu) {
    // unreachable in sane use
    g_mailbox.payload_u32[0] = acc;
  }
}

int init_or_throw() {
  if (g_inited) return 0;
  cudaError_t st = cudaFree(0); // force context init
  if (st != cudaSuccess) return (int)st;
  // Zero mailbox deterministically.
  DeviceMailbox zero{};
  st = cudaMemcpyToSymbol(g_mailbox, &zero, sizeof(DeviceMailbox), 0, cudaMemcpyHostToDevice);
  if (st != cudaSuccess) return (int)st;
  g_inited = true;
  return 0;
}

int submit_superpack(const PulseSuperpackV1* pkt) {
  if (!g_inited) {
    int rc = init_or_throw();
    if (rc != 0) return rc;
  }
  if (!pkt) return -1;

  DeviceMailbox mb{};
  mb.carrier_id_u64 = pkt->carrier_id_u64;
  mb.artifact_id_u32 = pkt->artifact_id_u32;
  mb.value_tag_u32 = pkt->value_tag_u32;
  for (int i = 0; i < 9; ++i) mb.coord9_i32[i] = pkt->coord9_i32[i];
  mb.payload_words_u32 = (pkt->payload_words_u32 > 64u) ? 64u : pkt->payload_words_u32;
  for (uint32_t i = 0; i < mb.payload_words_u32; ++i) mb.payload_u32[i] = pkt->payload_u32[i];

  cudaError_t st = cudaMemcpyToSymbol(g_mailbox, &mb, sizeof(DeviceMailbox), 0, cudaMemcpyHostToDevice);
  return (st == cudaSuccess) ? 0 : (int)st;
}

int run_tick() {
  if (!g_inited) {
    int rc = init_or_throw();
    if (rc != 0) return rc;
  }
  ew_tick_kernel<<<1, 1>>>();
  cudaError_t st = cudaDeviceSynchronize();
  return (st == cudaSuccess) ? 0 : (int)st;
}

void shutdown() {
  if (!g_inited) return;
  // No heap allocations currently; just reset flag.
  g_inited = false;
}

} // namespace ew_cuda_backend
