# Genesis Engine Learning Pipeline (Canonical)

This document is the canonical specification for the Genesis Engine AI learning pipeline and its crawler-driven ingestion system.

Scope:
- Defines the required sequential curriculum ordering (parallel-sequential execution).
- Defines how web ingestion is gated by visualization/internalization.
- Defines the GPU-resident crawler ingestion flow (single-machine) and determinism/safety constraints.

This file is normative: crawler implementations and learning-loop implementations MUST conform.


## 0) Honesty gate: measurable metrics only (6% default tolerance)

Genesis Engine learning is sandbox-verified. The AI MUST NOT advance the curriculum (or accept a source as credible) unless the simulation can reproduce **measurable metrics** that can be validated against ingested references.

Rules:
- Only simulate **objects, experiments, or events** that yield measurable metrics that can be validated against ingested sources.
- For each lesson/concept, define a metric vector `M_target` from sources and a corresponding simulated metric vector `M_sim`.
- The concept is admissible only if `err(M_sim, M_target) <= tol`, with a default **relative tolerance of 6%** (per-metric unless otherwise specified).
- If a source repeatedly fails validation, it MUST be down-weighted or quarantined (do not let it steer the curriculum).

Compute budget rule (per-domain request window):
- The learner MUST continue form-fitting toward 100% agreement for the full request window budget.
- It MUST NOT stop when reaching 6% tolerance early.
- Acceptance is evaluated at window end using the best fit found.

Exception:
- If the initial parameter-driven "reverse-calc" attempt yields a 100% match (zero error)
  on the first try, the task is accepted immediately and the remaining window budget may be
  applied to additional tasks.

GPU-only requirement:
- Candidate generation, simulation evaluation, and best-fit search MUST be executed on the GPU
  (substrate microprocessor). The CPU may orchestrate network I/O and kernel dispatch only.

Notes:
- “Measurable” means the metric has a clear unit/definition and is comparable across sources (e.g., energies, frequencies, cross-sections, orbital radii/ratios, bond lengths, reaction enthalpies, spectra peaks, macroscopic material properties, planetary/orbital parameters, etc.).
- Tolerance is relative by default: `abs(sim-target) / max(abs(target), eps) <= 0.06`.

This gate exists to keep the AI honest about what sources to believe and to prevent drifting into untestable narratives.

## 1) Curriculum ordering (parallel-sequential)

The AI MUST learn in the following order. Work inside a stage MAY be parallel, but stages MUST NOT be skipped or reordered.

Stage 1 — Quantum Mechanics (QM)
- Focus: excitations, interference, quantization, measurement-like gating, conserved updates in the lattice.
- Output requirement: the AI must be able to reproduce target metrics in the Genesis sandbox via simulation checkpoints before progressing.

Stage 2 — Orbitals → Atoms → Chemical Bonds
- Focus: electron orbital-like structures as stable excitation modes; atoms as bound state aggregates; bonds as constrained coupling modes.
- Output requirement: metric-matched sandbox reproductions (energy ratios, orbital occupancy patterns, bond length/angle proxies) before progressing.

Stage 3 — Chemistry
- Focus: reaction-like transitions as stochastic constrained processes; diffusion/transport; equilibrium and non-equilibrium behavior.
- Output requirement: reproduce reaction trend metrics and stability under perturbation.

Stage 4 — Materials and Physical Sciences
- Focus: bulk properties emerging from microstructure; conductivity/thermal behavior/strength; phase transitions.
- Output requirement: reproduce material response curves and demonstrate environment-dependent behavior.

Stage 5 — Cosmology / Atmospheres / Environmental Constraints
- Focus: gravity-like constraints, radiation-like fields, atmospheres, temperature/pressure regimes; planet-scale boundary conditions.
- Output requirement: demonstrate environment-conditioned chemistry and stable boundary-layer dynamics.

Stage 6 — Biology (deferred)
- Biology MUST NOT be introduced until Stage 5 is achieved.
- Rationale: the sandbox baseline is near absolute zero by default; biological dynamics are invalid without learned/implemented environmental constraints.

### Stage advancement rule (canonical)

Curriculum stages advance only after a sufficient number of **accepted** measurable
validation checkpoints have been completed for the current stage. Stages never
move backwards.

Default deterministic requirement: **8 accepted checkpoints per stage** for the
first four stages (QM, Chemistry, Materials, Cosmology). Biology remains deferred
until after cosmology/environment constraints are achieved.


## 2) Visualization as "internalization" gate

The crawler ingestion MUST be gated by the AI’s ability to internalize and visualize concepts.

Definitions:
- Verification: metric checks required to progress within the curriculum.
- Visualization: generating sandbox renderables (frames/slices/field probes) that serve as the AI’s "visual memory".

The learning loop MUST NOT advance to a new concept state until required simulation metrics match targets within tolerance.

Two visualization tiers:
- Tier A (Verification) is ALWAYS ON: produces metrics and sparse checkpoint artifacts.
- Tier B (Presentation) is TOGGLEABLE: live viewport output (headless vs visible). Headless disables Tier B, not Tier A.


## 3) Crawler intake gating policy (do not learn faster than you can visualize)

The system MUST maintain backlogs:
- Q_viz: pending visualization/internalization tasks.
- Q_learn: pending learning/integration tasks (index/coherence/anchor merges).

Admission control:
- External ingestion admission is throttled when Q_viz grows beyond target.
- Throttling applies to request scheduling (token refill) and/or to commit admission (spooling vs committing).

Canonical throttle factor:
- throttle = clamp01(1 - Q_viz / Q_viz_max)
- domain token refill = base_refill * throttle

This keeps the GPU busy with internal work while preventing unbounded intake.


## 4) Single-machine GPU-resident crawler ingestion (canonical flow)

On a normal consumer machine, web bytes arrive in RAM first. The GPU cannot directly dereference arbitrary RAM pointers.

Canonical flow:
1) Network I/O (OS/NIC) streams HTTP(S) response bytes into pinned/page-locked RAM buffers.
2) RAM→VRAM DMA upload of fixed-size chunks into a VRAM staging buffer (one-way).
3) GPU/substrate kernels perform canonicalization + compression:
   - UTF-8 validation/canonicalization (reject unsafe control bytes deterministically)
   - parsing/token scanning (streamed)
   - SpiderCode4 / UTF-8 phase mapping
4) GPU commits updates directly in VRAM:
   - anchor writes for the page/site
   - ledger deltas
   - coherence graph / indices

No GPU↔CPU↔GPU ping-pong is allowed except optional logging/persistence or viewport readout.

Streaming rule:
- Do not wait for a full page. Process chunks as:
  chunk arrives → upload → GPU compress/encode → emit pulse events → deterministic commit.


## 5) Pulse representation for ingestion

The canonical carrier for ingestion events is SpiderCode4:
- (f_code, a_code, v_code, i_code)
  - f: phase/trajectory selection
  - a: harmonic/expansion pressure
  - v: representational headroom / regime thresholds
  - i: throughput/current budget

Minimal identity/ordering MUST exist.
- Single-machine option: implicit per-socket identity is acceptable if 1 crawler = 1 stream.
- Otherwise include explicit source/stream IDs.
- Always include a sequence index (chunk_seq) and offset.

Determinism requirements:
- Chunk into fixed-size blocks (e.g., 16–256 KB).
- Tag each block with (conn/stream key, chunk_seq, offset).
- Batch per tick.
- Stable-sort before commit.
- Merge duplicates via deterministic reductions (avoid nondeterministic atomics).


## 6) Fan-out and VRAM bandwidth limits

Fan-out is the mechanism to bypass command-stream and random-write bottlenecks.

Two update classes MUST be distinguished:
- Materialized updates: explicit per-object/per-anchor writes (bandwidth-limited).
- Affected updates: implicit updates via region/lattice fields sampled later (much larger effective reach).

Policy:
- Prefer implicit region/lattice updates for far-field or bulk effects.
- Materialize only when required (viewport overlap, high-fidelity states, critical anchors).

Capacity is ultimately bounded by GPU VRAM bandwidth and bytes touched per committed update.


## 7) Safety governor (hardware envelope)

The ingestion and simulation pipeline MUST remain within a deterministic safety envelope.

Proxies:
- power_proxy = v_code * i_code
- tau_comp (temporal compression proxy) = |f_code| * a_code (normalized)
- inv_risk (event-horizon invertibility proxy) = tau_comp / (v_code + eps)

Constraints:
- Enforce hard caps and a ~70% operating target.
- Use dwell/cooldown to avoid staying near peaks.

Load shedding order (deterministic):
1) reduce a_code (fan-out/harmonics)
2) reduce active strands/region size
3) reduce materialization budget
4) reduce i_code (throughput)
5) reduce f_code last


## 8) Domain scheduling (politeness and throughput)

Internet crawling is RTT/rate-limit/politeness bound.

Canonical scheduling:
- Maintain per-domain token bucket and next_allowed_time.
- Default safe policy is 1 request/sec/domain unless overridden.
- Keep many lanes idle/sleeping; activate only when tokens and next_allowed_time permit.
- Recycle lane state machines; do not spawn/kill OS threads per request.


## 9) Corpus allowlist and staged crawling

Crawling MUST use an allowlist/frontier list.

Recommended:
- Keep an allowlist file listing domains/sources.
- Run staged crawling aligned to curriculum stages:
  - Stage 1 (QM): prefer physics/QM sources.
  - Stage 2–3: chemistry sources.
  - Stage 4–5: materials/cosmology sources.

Parallel-sequential rule:
- Multiple sources may be crawled in parallel within a stage.
- The pipeline MUST NOT admit Stage N+1 content into the learning loop until Stage N gates are satisfied.



## Curriculum stage advancement checklist (canonical)

Stage advancement is not based on a raw count alone. Each stage declares a deterministic checklist of required measurable MetricKinds. A stage is complete only when all required kinds have been validated (<=6% tolerance) via the learning checkpoint gate.

- Stage 0 (QM): Double-slit fringes, particle-in-box levels, harmonic oscillator spacing, tunneling transmission.
- Stage 1 (Orbitals/Atoms/Bonds + Chem): orbital energy ratios, radial node counts, bond equilibrium length, bond vibration frequency, reaction rate vs temperature, equilibrium constant, diffusion coefficient.
- Stage 2 (Materials): thermal conductivity, electrical conductivity, stress-strain modulus, phase-change threshold.
- Stage 3 (Cosmology/Atmos): orbit period, radiation spectrum, atmospheric pressure profile.
- Stage 4 (Biology): deferred; unlocked only after Stage 3. Uses osmosis/diffusion metrics as the first gate.

## Visualization tier toggle

Presentation is toggleable at runtime: press **H** to toggle continuous rendering on/off. In headless mode, simulation + metric validation continue; only presentation is suppressed.
