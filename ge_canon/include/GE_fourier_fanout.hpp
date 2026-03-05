#pragma once

#include <cstdint>

struct EwState;
struct EwInputs;
struct EwCtx;

// Step spectral field anchors (Fourier-transform fan-out microprocessor).
// Minimal bootstrap: 1D spectral bank with deterministic mode injection.
void ew_fourier_fanout_step(EwState& cand, const EwInputs& inputs, const EwCtx& ctx);

// Derived probes for viewport/debug UI.
// Hard rule: these values are read-only and MUST NOT feed back as authoritative inputs.
struct EwSpectralFieldAnchorState;
int16_t ew_spectral_probe_field_q1_15(const EwSpectralFieldAnchorState& s, const int32_t pos_q16_16[3]);
int16_t ew_spectral_probe_grad_q1_15(const EwSpectralFieldAnchorState& s, const int32_t pos_q16_16[3]);
