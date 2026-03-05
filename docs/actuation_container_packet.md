# Actuation container packet

This document defines a bounded, deterministic container format for compacting multiple small actuation ops into a single `EwActuationPacket`.

The container exists to handle per-tick overflow without allocating memory and without introducing unbounded work. It is used only after primary and sidecar actuation slots are saturated, and before latent-tick deferral is used.

## Packet header

A container is an `EwActuationPacket` with:

- `op_tag_u8 = EW_ACT_OP_CONTAINER`
- `payload_len_u8 = 4 + 16 * EW_ACT_CONTAINER_SUBSLOTS` (fixed)

The container ignores `drive_k_u16` at the packet header level; each subslot carries its own `drive_k_u16`.

## Payload layout (fixed-size)

The payload is parsed as a fixed header plus a fixed number of fixed-size subslots.

- `payload[0]`: `sub_count_u8` (0..EW_ACT_CONTAINER_SUBSLOTS)
- `payload[1]`: reserved (0)
- `payload[2]`: reserved (0)
- `payload[3]`: reserved (0)
- subslots begin at `payload[4]`

Each subslot is 16 bytes:

- `+0`: `op_tag_u8` (v1 supports only `ADD`, `MUL`, `CLAMP`)
- `+1`: `flags_u8` (v1 must be 0; non-zero is rejected)
- `+2..+3`: `drive_k_u16` (little-endian)
- `+4..+7`: `a0_q16_16` (int32, little-endian)
- `+8..+11`: `a1_q16_16` (int32, little-endian)
- `+12..+15`: `a2_q16_16` (int32, little-endian; `CLAMP` uses this as `hi`, otherwise 0)

Unused subslots must be zero.

## Execution order

Container ops are expanded deterministically:

1) the container packets are processed in the order they were created within the tick
2) within a container, subslots are processed from lowest index to highest index
3) only the first `sub_count_u8` subslots are executed; the remaining subslots are ignored

This preserves arrival order within the compacted overflow region.
