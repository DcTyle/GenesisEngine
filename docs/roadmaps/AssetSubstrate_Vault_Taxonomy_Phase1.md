# Asset Substrate + Vault Taxonomy — Phase 1 Scaffold

This patch does not change substrate truth computation. It only restructures the deterministic project library scaffold so later UI phases can bind to a stable folder grammar.

## Goals

- Keep the existing substrate microprocessor canonical.
- Prepare the project library for Unreal-style content browsing, Substance-style material workflows, and Blender-like authored asset separation.
- Separate authored assets from vault/reference material so modulator data does not live beside runtime object assets.
- Keep the structure deterministic and schema-locked.

## Top-level partitions

- `Worlds/`
- `Planets/`
- `Simulations/`
- `Actors/`
- `Character/`
- `Foliage/`
- `Assets/`
- `Vault/`

## Authored asset partition (`Assets/`)

This partition is for project-authored or imported runtime assets.

- `Assets/Objects/`
- `Assets/Meshes/`
- `Assets/Textures/`
- `Assets/UV/`
- `Assets/Voxels/`
- `Assets/Materials/Mixer/`
- `Assets/Materials/Designer/`
- `Assets/Materials/SurfaceProfiles/`
- `Assets/Materials/Compositions/`
- `Assets/Materials/PeriodicTable/Particles/`
- `Assets/Materials/PeriodicTable/Atoms/`
- `Assets/Materials/PeriodicTable/Compounds/`
- `Assets/Materials/PeriodicTable/DNA/`

Rationale:
- Material compositions are separated from object assets and texture assets.
- Periodic-table exploration is separated into particles / atoms / compounds / DNA so future tools can expose distinct views.
- Compound-level modulation can later bind to the richer composition tree without forcing single-atom assets to pretend they are compounds.

## Vault partition (`Vault/`)

This partition is for structured reference assets, AI mirrors, curated material references, and validation artifacts.

- `Vault/AI/`
- `Vault/AI/research/`
- `Vault/AI/experiments/metrics/`
- `Vault/AI/experiments/metrics_failures/`
- `Vault/AI/corpus/allowlist_pages/`
- `Vault/AI/corpus/resonant_pages/`
- `Vault/AI/corpus/speech_boot/`
- `Vault/AI/uspto/`
- `Vault/Objects/`
- `Vault/Components/`
- `Vault/Machines/`
- `Vault/Inventions/`
- `Vault/Materials/Compositions/`
- `Vault/Materials/PeriodicTable/Particles/`
- `Vault/Materials/PeriodicTable/Atoms/`
- `Vault/Materials/PeriodicTable/Compounds/`
- `Vault/Materials/PeriodicTable/DNA/`

Rationale:
- The vault is the stable reference library.
- AI mirror artifacts now live under `AssetSubstrate/Vault/AI` instead of beside authored assets.
- Material reference data is separated from surface authoring lanes.

## Immediate behavioral changes in this patch

- `GeAssetSubstrate` now scaffolds the schema-locked folder tree for both project root and cache root.
- Asset index rebuild now includes both `.geasset` and `.geassetref` files so AI/vault references can appear in future content-browser passes.
- Runtime asset substrate initialization now reads roots from project settings instead of ignoring those settings.

## Explicit non-goals of this patch

- No viewport-control changes.
- No material mixer UI yet.
- No voxel designer UI yet.
- No content browser UI rewrite yet.
- No atom/compound editor semantics yet.

This is groundwork only: deterministic plumbing before the higher UX layers arrive.
