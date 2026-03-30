# Photon-Native DMT Simulation Spec

This file is the active repo-local handoff for the photon-native work requested in chat.

## Current Ground Truth

- Start from [OPEN_ME_FIRST.txt](./OPEN_ME_FIRST.txt) and the V4 continuation docs under `docs_v4/`.
- The durable research archive on disk is still the toy / analogue continuation package described in `docs_v4/README_CONTINUATION_V4.md`.
- The live engine path for this work is Vulkan / Win64. Do not start new CUDA migration work from this spec.
- Existing photon-native sample deliverables already on disk are:
  - `photon_packet_trajectory_sample.*`
  - `photon_packet_path_classification_sample.*`
  - `photon_lattice_tensor6d_sample.*`
  - `photon_vector_excitation_sample.*`
  - `photon_tensor_gradient_glyph_sample.*`
  - `photon_shader_texture_sample.*`
  - `photon_audio_waveform_sample.*`

## Priority Scope

The oldest active chat prompt takes priority:

1. Track every individual packet and preserve per-packet temporal trajectories.
2. Expose shared vs. individual packet paths.
3. Compute 6DoF tensor gradients from temporal dynamics and ledger conservation.
4. Feed vector/tensor outputs directly into the Vulkan viewport.
5. Feed waveform outputs directly into the engine audio ingress path.
6. Keep the model falsifiable against external references instead of tuning to fit.

## Active Simulation Mode

The photon confinement sim now has a packet-spectrum execution path that replaces the old "explicit lattice vectors first" mental model:

- Each packet owns only spectral state:
  - `f_x`, `f_y`, `f_z` bins
  - harmonic amplitudes
  - harmonic phases
- There is no fully materialized voxel lattice in this mode.
- "256^3 equivalent" refers to spectral reconstruction density and output sampling scale, not a resident 3D grid allocation.
- Spatial vectors are emergent only after inverse FFT of the packet spectrum.

Prototype reference driver (not part of C++ runtime path):

- `ResearchConfinement/prototyping/python/photon_frequency_domain_sim.py`
  - backend: `numpy.fft`
  - output: trajectory, path classification, tensor samples, vector excitations, shader colors, audio waveforms, plots, hover-debug HTML
  - optional root refresh: `--write-root-samples`

Runtime authority remains C++/Vulkan (`GE_research_confinement.*`, `vulkan_app/*`).

Native generator:

- `tools/photon_frequency_domain_sim.cpp`
  - build target: `photon_frequency_domain_sim`
  - emits the same research sample files and VSD metadata format used by the archive loader.
  - headless NIST wafer mode:
    - loads `ResearchConfinement/nist_silicon_reference.json`
    - injects quartet pulse terms into the field equations (simulation only):
      - `--pulse-frequency`
      - `--pulse-amplitude`
      - `--pulse-amperage`
      - `--pulse-voltage`
    - strict NIST input gate:
      - `--nist-ref <path>`
      - `--nist-strict`

## Frequency-Domain Rules

- Packet state:
  - `64-128` bins per axis with complex values `A * exp(i * theta)`
- Temporal coupling:
  - cross-correlate packets through beat spacing `|f_i - f_j|`
  - shared paths emerge from phase lock and beat-weighted coupling
- Amplitude:
  - bin power carries signal strength and leakage
- Frequency:
  - dominant bins and harmonic offsets define the packet carrier
- Amperage:
  - compresses / blurs bin resolution as a time-dilation proxy
- Voltage:
  - tilts the spectrum by a phase ramp over bin index
- Retro patch:
  - inject a future-biased spike into early bins, then forward-propagate it through the coupling operator
- Confinement:
  - low-frequency envelopes trap higher-frequency packets without introducing explicit geometric constraints

## Runtime Contract

The current engine integration is intentionally split into two layers:

- `GE_research_confinement.*`
  - owns parsing durable research artifacts
  - builds animated research visualization points
  - builds deterministic PCM frames from the archived waveform samples
- `vulkan_app/src/GE_app.cpp`
  - consumes those outputs
  - projects them into camera space as Vulkan instances
  - injects archived waveform frames into the substrate audio ingress path each tick

## Visual Mapping Contract

- Trajectory replay packets:
  - source: `photon_packet_trajectory_sample.json` from the frequency-domain simulator
  - output: lit trajectory heads + temporal ghosts
  - color: particle class mapping
- Vector excitations:
  - source: `photon_vector_excitation_sample.json`
  - output: animated stream / arrow-like billboards reconstructed from inverse FFT
  - color: derived from spin vector magnitude by axis
- Tensor glyphs:
  - source: `photon_tensor_gradient_glyph_sample.json`
  - output: curvature glyph billboards
  - color: direct sample color
- Shader texture samples:
  - source: `photon_shader_texture_sample.json`
  - output: sparse emissive packet/bin samples for the viewport
  - color: direct sample RGB from amplitude, phase, and OAM-derived mapping

## Audio Mapping Contract

- Source: `photon_audio_waveform_sample.json`
- Runtime path:
  1. interpolate archived waveform samples for the current tick
  2. convert to interleaved PCM16
  3. inject into `SubstrateManager::inject_audio_pcm16`
- This keeps the photon-native waveform tied to the same spectral archive used by the visuals.

## Vulkan Compute Notes

The placeholder compute shaders under `shaders/vkcompute/` are the staging area for the eventual GPU-native texture/audio generation path:

- `vkcompute_photon_visual.glsl`
  - maps phase, amplitude, and OAM density into RGB
- `vkcompute_photon_audio.glsl`
  - maps temporal phase evolution and circulation into a multichannel waveform buffer

These files are still design-time hooks unless they are explicitly added to the build graph.

## No-Form-Fitting Rule

- Keep only fixed constants and durable reference data as inputs.
- Any new emergent quantity must come from temporal operators, ledger conservation, or deterministic archive replay.
- Do not tune coefficients just to mimic an expected outside result.

## Falsification Note

The durable repo artifacts include placeholder falsification and emergent-feature reports. They remain hypothesis scaffolds until the upgraded runtime emits quantitative packet, tensor, visual, and audio outputs from the live Vulkan path.
