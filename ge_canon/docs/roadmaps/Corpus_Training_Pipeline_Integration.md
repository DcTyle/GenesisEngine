# Roadmap: Corpus Training Pipeline Integration for Genesis Engine (Deterministic, Substrate-Native)

This document is a planning artifact and does not imply any network crawling is enabled by default. Offline ingestion is the MVP path.

## 1) Heavily Detailed Summary

The purpose of this integration is to turn the Genesis Engine from “an engine with a smart substrate” into “an engine that can grow its own substrate-native language model from a controlled corpus,” without breaking the determinism contract built into the runtime. Concretely: add a corpus pipeline that (a) ingests content only from an explicit allowlist, (b) canonicalizes and encodes bytes using the existing text→frequency machinery, (c) collapses component frequencies into stable carriers, (d) stores resulting vectors as anchors optimized for phase overlap / coherence retrieval, and (e) iteratively trains an internal LM-like memory and prediction layer using learning gates and CUDA acceleration—while “quantity peeling” steadily reduces decoherence via relative-error thresholds.

### What this unlocks
1. Substrate-native retrieval + learning loop. Pieces exist: pinned buffers + GPU compute orchestration (`crawler_subsystem.cpp`), canonical text embedding/segmentation (`text_encoder.hpp`), deterministic carrier collapse (`frequency_collapse.hpp`), replay-stable delta mapping (`delta_profiles.cpp`), strict acceptance based on relative error (`GE_learning_checkpoint_gate.cpp`), and a decision layer (`GE_neural_phase_ai.cpp`). The missing link is the end-to-end loop turning “raw corpus bytes” into “anchors + training signals + stable model state evolution.”
2. A deterministic “language model” inside the substrate. Start with a stable vocabulary + n-gram/skip-gram/co-occurrence lattice, augmented with phase-coded anchors and a gated update rule. “Weights” are fixed-point phase/strength values stored as anchor parameters and updated only when gates allow.
3. Curriculum-driven capability growth. The requested progression (vocabulary → physics → biology/anatomy → neurology → intent controls for character simulation) becomes a set of domain lanes and training phases, each with its own delta profile, gating thresholds, and acceptance criteria.
4. Crosstalk computing without true superposition. Rely on inter-anchor interference patterns: anchors live in a coherence graph, and retrieval/training uses controlled overlap (phase similarity + strength + lane) to combine signals deterministically.

### How it aligns with the current codebase (file-by-file)
- `crawler_subsystem.cpp` already has the orchestration shape: deterministic batching/scheduling and GPU-side encode/compute. Extend it to enforce allowlist, deterministic chunk tags (seq/offset), stable queue ordering, and offline ingestion adapters (dumps) so MVP is repeatable.
- `text_encoder.hpp` provides canonical normalization/segmentation plus a 9D embedding concept. Training-critical math must be fixed-point in core paths; floating is presentation-only and converted immediately.
- `frequency_collapse.hpp` standardizes carrier identity: component bins collapse to stable carrier params used for keying and overlap scoring.
- `delta_profiles.cpp` is the hook for curriculum lanes: crawl/lang/physics/bio/neurology each get conservative, replay-stable profiles.
- `GE_learning_checkpoint_gate.cpp` supplies the acceptance oracle (relative error). Quantity peeling is a schedule of tightening thresholds per lane.
- `GE_neural_phase_ai.cpp` is the policy controller: select ingest targets, reinforcement actions, curriculum transitions, and ambiguity resolution.

### Technical implications (what “LM inside the substrate” means)
Three layers:
1. Corpus Anchor Store (CAS): durable memory, keyed by carriers + provenance, retrieved via phase overlap.
2. Coherence Graph + Crosstalk Router (CGCR): bounded-degree graph over anchors/tokens/segments.
3. Gated Trainer + Policy Loop (GTPL): proposes updates, commits only when gates pass.

### Risks and mitigations
- Coherence divergence: mitigate with lane separation, bounded fan-out (top-K), overlap budget per tick, gate rejection on ambiguity increase.
- Data overload: mitigate with per-domain token bucket limits, deterministic batching, and materialization budget per tick.
- Non-determinism creep: mitigate with offline-first, stable ordering, fixed-point math, deterministic CUDA reductions, and replay logs.
- Quality drift: mitigate by objective gates + domain weighting + license tagging.

## 2) Implementation List (Key Features / Modules)

- CorpusAllowlistLoader
- DomainPolicy + RateLimiter
- CrawlerSubsystem extensions (allowlist, offline adapters, deterministic chunking, stable ordering, throttling)
- CorpusCanonicalizer (UTF-8 validation + newline/whitespace canonicalization)
- SpiderPulse builder (canonical blocks → SpiderCode4 carriers + identity tuple)
- Carrier collapse + anchor keying
- Corpus Anchor Store (append-only, deterministic compaction)
- Coherence Graph Store (bounded degree, deterministic merge/reduction)
- Overlap scoring + retrieval router (fixed-point + top-K + crosstalk rules)
- Quantity Peeling Trainer (gated updates via relative error)
- Curriculum Manager (lane configs + threshold schedules + success metrics)
- NeuralPhaseAI hooks for ingest/training scheduling + ambiguity resolution
- Testing harness + replay logs

## 3) Roadmap (Phases)

Phase 1 — Ingestion Setup (Week 1)
- Allowlist loader
- Deterministic queue ordering + fixed chunking
- Per-domain token bucket
- Offline ingestion adapters

Phase 2 — Vectorization + Anchor Storage (Week 2)
- Canonicalizer → encoder → collapse → carrier params
- CAS append-only store + payload references
- Basic retrieval (query → top-K anchors)

Phase 3 — Training Loop + Quantity Peeling (Weeks 3–4)
- QuantityPeelingTrainer (propose → gate → commit)
- CUDA batch scoring/update (deterministic reductions)
- Replay logs + checkpoints

Phase 4 — Domain-Specific Learning (Weeks 5–6)
- Physics / bio / neuro lanes, lane-specific tests, cross-lane bridges gated

Phase 5 — Integration + Autonomy (Weeks 7–8)
- EwNeuralPhaseAI policy integration
- Safe self-iteration loop (proposal gating)
- Intent-control hooks (tokens → control primitives)

## 4) Implementation Checklist

See the main prompt’s checklist for the full Yes/No items. This roadmap is treated as a living spec; implementation should preserve determinism and the CPU-only-for-IO constraint.
