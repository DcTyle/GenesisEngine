# TODO: Vulkan Photon-Native Runtime

- The application runtime path for photon confinement is Vulkan / Win64 only.
- Do not start any new broad CUDA-to-Vulkan migration work from this note.
- Do not convert unrelated CUDA kernels as part of the photon-native task queue.
- Keep all new photon-native tracking, tensor, visual, and audio work on the existing Vulkan app path plus the durable research archive loader.
- Treat `shaders/vkcompute/` as the staging area for future GPU-native lattice texture/audio generation.
- Treat `GE_research_confinement.*` as the durable on-disk archive contract for the photon-native outputs:
  - per-packet trajectories
  - shared/individual path classification
  - 6DoF tensor cells
  - vector excitations
  - tensor glyphs
  - shader texture samples
  - audio waveform samples
- If a future kernel conversion becomes necessary, track it separately and only after the current Vulkan runtime path is verified end to end.
