# Pass 10 / Later Series I — Vector & Spectra Identity Conversion (Tranche 2)

Canonical baseline: `GenesisEngine_repo_inherited_patch_v52_pass9_vector_identity_tranche1.zip`

This pass continues the later dedicated `I1–I5` series on the active inherited line. It does not start `J*` annotation ingestion work.

## Scout classification against the active inherited line

| Item | Classification | Repo evidence before patch | Pass 10 action |
|---|---|---|---|
| I1. Replace remaining hash/fingerprint identity fields | partial | Active packet/snapshot lanes still used `payload_hash_u64`, `intent_hash_u64`, `measured_hash_u64`, `pulse_*_hash_u64`, `leakage_hash_u64`, `influx_hash_u64` in coherence packets, temporal summaries, spectral state, voxel state, and producers/consumers | Convert these canonical active-line fields to `EwId9` vector identities and remove the old fields from the touched path |
| I2. Replace summary construction logic in fanout / voxel coupling / state summary paths | partial | Fanout and voxel coupling still synthesized temporal and payload summaries through ad hoc 64-bit mix values | Add canonical bounded `EwId9` summary helpers in the temporal summary layer and rewire fanout/voxel producers to use them |
| I3. Convert coherence bus dedupe/routing to vector IDs only | missing | Coherence bus sorting, dedupe, and replay suppression still ranked packets using mixed 64-bit key material derived from old hash fields | Rewire coherence bus collection, sort, and duplicate reduction to use packet `EwId9` identities directly |
| I4. Remove “fingerprint” whole-state naming and logic | partial | Pass 9 already moved the source-of-truth whole-state naming to `signature`; the remaining gap was low-level packet fields folded into the signature using old hash names | Update whole-state signature folding to consume the new vector ID fields |
| I5. Sync docs/comments/debug/UI strings after conversion | partial | Touched low-level comments still described hash-based packets/summaries | Sync comments in the touched active-line files to vector/spectra identity language; large historical spec-upload docs remain intentionally untouched in this tranche |

## File-by-file implementation matrix

| File | Status before patch | Reason | Pass 10 implementation |
|---|---|---|---|
| `include/GE_coherence_packets.hpp` | partial | Active coherence packets still exposed hash-named payload/intent/measured fields | Replaced packet identity fields with canonical `EwId9` members and updated packet comments |
| `include/GE_temporal_summaries.hpp` | partial | Temporal summaries still exposed hash-named fields and had no canonical vector-ID helper layer | Replaced summary fields with `EwId9` members and added bounded canonical ID builders for intent, measured, pulse-intent, pulse-measured, leakage, and influx summaries |
| `include/GE_spectral_field_anchor.hpp` | partial | Spectral field state still stored `leakage_hash_u64` | Replaced with `leakage_id9` |
| `include/GE_voxel_coupling_anchor.hpp` | partial | Voxel coupling state still stored `influx_hash_u64` | Replaced with `influx_id9` |
| `src/GE_fourier_fanout.cpp` | partial | Spectral producer path still built temporal/pulse/leakage identity through 64-bit mixed hash values | Rewired intent/measured/pulse/leakage identity construction to canonical `EwId9` helper functions and updated comments |
| `src/GE_voxel_coupling.cpp` | partial | Voxel producer path still built temporal/pulse/influx identity through 64-bit hash mixing | Rewired intent/measured/pulse/influx identity construction to canonical `EwId9` helper functions |
| `src/GE_coherence_bus.cpp` | missing for vector-only routing | Coherence bus still used mixed 64-bit keys and hash-named fields for sort/dedupe/replay suppression | Replaced those paths with direct `EwId9` sort and duplicate-reduction logic |
| `src/GE_state_signature.cpp` | partial | Whole-state signature fold still consumed old packet/summary hash fields | Updated signature folding to consume `EwId9` lanes through one canonical fold path |

## Validation plan for this pass

- Syntax-check `src/GE_coherence_bus.cpp`
- Syntax-check `src/GE_voxel_coupling.cpp`
- Syntax-check `src/GE_fourier_fanout.cpp`
- Syntax-check `src/GE_state_signature.cpp`
- Syntax-check `src/GE_runtime.cpp` to catch header ripple errors on the active line

## Explicit non-goals for this tranche

- No `J*` annotation-driven CPU-to-substrate ingestion work
- No repo-wide rewrite of every historical blueprint/spec-upload mention of `fingerprint`, `hash`, or `digest`
- No UI/Win32 panel rewrite
- No second identity system; this tranche removes the old active packet/summary fields from the touched path instead of keeping shadow aliases alive
