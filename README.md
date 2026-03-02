Genesis Engine

Build:
  mkdir build && cd build
  cmake .. -DEW_ENABLE_CUDA=ON
  cmake --build . --config Release

Notes:
- Canonical CPU reference remains the authoritative path.
- CUDA kernel outputs per-anchor leaks; sum on host in ID order for canonical reservoir.
- Vulkan native app provides a Win64 viewport + AI UI panel for deterministic tick stepping and substrate I/O.

Tests:
  - Build: cmake --build . --config Release
  - Run:   ctest --output-on-failure

Contract harness:
  - Build: cmake --build . --config Release
  - Run:   ./ew_contract_harness --out_dir .
  - Produces: contract_report.md, contract_metrics.json, failure_repro.txt

Vulkan viewport app:
  - Build target: GenesisEngineVulkan
  - Window layout: left = viewport, right = objects + AI panel
  - Viewport controls (UE-like):
      RMB fly (WASD + QE + mouse), Shift = faster
      Alt+LMB orbit, Alt+MMB pan, Alt+RMB dolly, mouse wheel dolly
      Object translate: hold W and drag with LMB (selected object)
  - AI panel: enter a line and click Send; lines are submitted as UTF-8 observations.
      Common commands: QUERY:, WEBSEARCH:, OPEN:, ANSWER:

Canonical docs:
The canonical specification bundle for this prototype is included under `docs/`.

  - docs/GENESIS_LEARNING_PIPELINE.md: canonical crawler ingestion + sequential curriculum + visualization gating spec.

Operator packet payload layouts (canonical)

The runtime enforces byte-for-byte payload layouts for every operator packet kind.
Payload bytes are little-endian and the payload length must match exactly.

  - 0x00000001 OPK_TEXT_EIGEN_ENCODE: 56 bytes
  - 0x00000002 OPK_AGGREGATE_NORMALIZED_SUM: 80 bytes
  - 0x00000003 OPK_PROJECT_COH_DOT: 8 bytes
  - 0x00000004 OPK_CONSTRAIN_PI_G: 160 bytes
  - 0x00000005 OPK_CHAIN_APPLY: 256 bytes (chain_len 0..63; unused entries must be 0)
  - 0x00000006 OPK_OBSERVABLE_PROJECT: 72 bytes
  - 0x00000007 OPK_EFFECTIVE_CONSTANT: 72 bytes
  - 0x00000008 OPK_SINK_OMEGA: 76 bytes
  - 0x00000120 OPK_SHR_LOG2_CARD: 4 bytes
  - 0x00000130 OPK_LOAD_ANCHOR_COORD_Q63: 4 bytes
  - 0x00000131 OPK_LOAD_PARAM_Q32_32: 4 bytes
  - 0x00000140 OPK_STORE_ARTIFACT_KV: 256 bytes (path_len 0..200; bytes beyond path_len must be 0)

All other implemented kinds require a 0-byte payload.


Canonical docs (learning/crawling):
- docs/GENESIS_LEARNING_PIPELINE.md
- docs/GENESIS_VALIDATION_GATE.md
