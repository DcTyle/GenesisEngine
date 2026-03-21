# Run 030 — NIST surrogate projection sanity check

## Goal

Take the existing run-029 temporal-alignment probability-collapse output and ask a narrower, honest question:

Can the current toy state, *as represented by its internal step/lock structure*, plausibly be said to lie within the uncertainty scale of the NIST lithium D-line measurements?

## Mapping used

This pass does **not** claim a first-principles physical derivation.

Instead it uses a surrogate dimensional map:

- assign the NIST lithium strong-line pair near 670.8 nm as the D-line scale
- use the toy r5→r6 onset spacing (13 steps) as the surrogate image of the D1→D2 fine-structure separation
- infer a toy-to-frequency scale in MHz per step

That yields a frequency scale of **773.905 MHz/step**.

## Consequence

At that scale:

- a 1-step delay in collapse lock corresponds to **773.905 MHz**
- a 2-step delay corresponds to **1547.810 MHz**

By comparison:

- the NIST news report says the line width is about **5.87 MHz**
- the same report says the final measurement quality was about **3 parts in 10^11**
- at ~446.9 THz optical frequency, that is only about **13.4 kHz**
- the comb was tied to a cesium clock at a few parts in 10^13, i.e. order **0.1 kHz** on this scale

## Verdict

Under this surrogate projection, the current toy step granularity is **nowhere near** the NIST calibration/uncertainty scale.

One internal step corresponds to:

- about **131.8 natural linewidths**
- about **57,720 ×** the ~3e-11 absolute uncertainty scale

So the present model can still be called *internally tight against its handoff ledger*, but it **cannot honestly be described as within NIST measurement uncertainty**.

## What this means

The limiting problem is not just target accuracy.
It is **projection resolution**.

Before any serious calibration-to-NIST claim, the toy outputs need to be converted into:
1. absolute line-center predictions in SI units,
2. a hyperfine/blend model,
3. a linewidth model,
4. uncertainty bars,
5. sub-step interpolation or a continuous timing readout rather than integer-step lock timing.

Right now the model is still too coarse by many orders of magnitude for a metrology claim.