# Substrate Actuation Contract

This document defines the engine’s *substrate actuation* invariants. These are the rules that keep the microprocessor substrate deterministic, bounded, and single-source-of-truth.

## Core invariants

1) Overwrite-in-place only.

Truth-state storage uses fixed-capacity slots/rings. No per-tick heap growth is allowed on the truth path.

2) Bounded per-tick work.

Each tick has explicit caps (slots, inbox sizes, fanout budgets). When a cap is hit, the system fails closed (drops, clamps, or defers deterministically) rather than allocating.

3) Deterministic ordering.

All consumption of inboxes/slots is in deterministic order. Any traversal of variable collections must be stable-sorted by a deterministic key (e.g., anchor id) before use.

4) CPU is control-plane only.

The CPU may schedule/bridge inputs (UI/AI/control packets) into the substrate via bounded hook/packet structures. The CPU must not perform unbounded math or become the source of truth for substrate evolution.

5) Latent tick is last resort.

When load exceeds the per-tick actuation capacity, the system uses deterministic overflow behavior. A latent/defer path may exist, but it must be explicitly gated and remain rare. It must not allocate additional memory.

Overflow order is fixed:

1) primary actuation slots
2) sidecar actuation bands
3) container packets (compaction of multiple small ops)
4) latent tick deferral ring (rare)

See: `docs/actuation_container_packet.md`.

6) Derived-only observability.

Debug/inspection outputs (status lines, overlays, latched “last result” fields) are *derived-only* and must never feed back into truth-state evolution.

## Bridge rules

1) Hook/packet payloads are formally specified.

Packing/unpacking of control-plane payloads must be implemented once (shared helpers) and used consistently by emitters and consumers.

See: `docs/hook_emit_actuation_op_packing.md`.

2) Rejection is deterministic.

Invalid op-tags, invalid payload lengths, or forbidden flags must be rejected deterministically (drop with optional derived-only reporting).

3) No parallel truth systems.

Subsystems must not maintain “shadow state” that competes with anchors/slots/rings as authoritative truth.

## Build-time observability toggle

UI/status emission is a deterministic observability channel and may be compiled out in production without changing substrate semantics.

* Build option: `EW_ENABLE_UI_STATUS` (ON/OFF)
