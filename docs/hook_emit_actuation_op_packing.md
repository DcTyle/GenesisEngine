# HookEmitActuationOp packing (v1)

This spec defines the canonical packing used by the OP console / AI bridge when emitting `HookEmitActuationOp` into a spectral field anchor’s hook inbox.

## Intent

A `HookEmitActuationOp` is a control-plane packet that carries a small math/ops request (ADD/MUL/CLAMP) into the substrate, where it becomes a bounded `EwActuationPacket` executed during spectral fanout.

## Source of truth

All emitters and consumers must use the shared header-only helper:

- `include/ew_actuation_op_pack.hpp`

No other packing logic is allowed.

## Fields

The hook packet uses:

- `hook_op_u8 = HookEmitActuationOp`
- `causal_tag_u8 = op_tag` (one of `EW_ACT_OP_ADD`, `EW_ACT_OP_MUL`, `EW_ACT_OP_CLAMP`)
- `p0_q32_32` and `p1_q32_32` as opaque 64-bit payload words

## Payload layout

### p0 (64-bit)

`p0` packs two signed operands:

- low 32 bits: `a0_i32` (signed Q16.16)
- high 32 bits: `a1_i32` (signed Q16.16)

Bit layout:

- `p0[31:0]   = a0_i32`
- `p0[63:32]  = a1_i32`

### p1 (64-bit)

`p1` packs a third operand plus routing/flags:

- low 32 bits: `a2_i32` (signed Q16.16)
- next 16 bits: `flags_u16` (reserved, must be 0 in v1)
- high 16 bits: `drive_k_u16`

Bit layout:

- `p1[31:0]   = a2_i32`
- `p1[47:32]  = flags_u16`
- `p1[63:48]  = drive_k_u16`

## Q formats

All operands are signed fixed-point `Q16.16` (`int32_t`).

- Integer part is in the high 16 bits.
- Fractional part is in the low 16 bits.

The initial OP console grammar interprets numeric inputs as integer values and encodes them as `Q16.16` by shifting left 16.

## Routing

`drive_k_u16` selects the target spectral bin.

- `0xFFFF` is a broadcast sentinel that applies the actuation to bins `0..7` (low-band) deterministically.
- Otherwise, the consumer maps `drive_k_u16` into `[0..EW_SPECTRAL_N-1]` via bitmask.

## Validation rules (v1)

Emitters:

- Must set `flags_u16 = 0`.
- Must emit only supported `op_tag` values.

Consumers:

- Must reject unknown `op_tag` values.
- Must reject any nonzero `flags_u16`.
- Must remain bounded: if inbox or actuation slots are full, drop deterministically.
