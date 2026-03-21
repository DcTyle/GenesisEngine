# Run 028 winner-selection collapse operator

This pass replaces the smooth collapse proxy with an explicit winner-selection operator over 20 lag/channel candidates per outer band.

Main findings:
- bands with threshold crossing at onset or onset+1: 0 / 3
- bands with threshold crossing within 4 steps of onset: 2 / 3
- bands with the same leader before and at onset: 2 / 3

Interpretation:
- the winner-selection operator produces extremely sharp point-vector dominance probabilities at onset
- r3 behaves most like immediate point-vector lock
- r5 and r6 behave more like early post-onset lock, within 4 steps, with leader changes still occurring around onset
- this is closer to your collapse picture than run 027, but it still looks like a short locking interval rather than an infinitely sharp single-step collapse in the accessible replay
