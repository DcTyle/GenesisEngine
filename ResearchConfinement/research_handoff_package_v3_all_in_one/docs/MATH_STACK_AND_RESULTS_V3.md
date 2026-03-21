# Math Stack and Results V3

Core recurring quantities used across the visible files and continuation work:
- Effective constants: `h_eff`, `k_b_eff`, `c_eff`, `e_charge_eff`, `scale_eff`, `stochastic_factor`, `relativistic_corr`
- 9D lattice helpers: mapped 9D coordinates and energy projection
- Lorenz-style carrier channels: `sigma`, `beta`, `rho`
- Temporal/spatial effective channels and Doppler counterparts
- Pair channels: `Rel_ij`, `G_ij`, `Tpair_base_ij`, `Cgate_ij`, `Tflux_ij`
- Derived recurrence / residence quantities: traced residence, dwell, lock score, phase-slip, temporal memory, pair temporal memory
- Zero-point temporal bookkeeping: zero-sum drift/debt partitioning and localized redistribution

Key working formulas used in the visible continuation:
- Pair support: `S_ij = Rel_ij^2 * O_ij * R_ij * Cgate_ij * Tpair_base_ij * Tflux_ij`
- Pair timing correction: `delta_t_pair(j) = t_local(j) * (1 - Rel_i_calc(j)) * sum_{i<j} S_ij`
- Quartet-driven deficit proxies: `gamma_ts`, `xi_corr`, `eta_def`
- Recurrence/residence framing: emergent recurrence from relative time dilation, persistence from traced residence on a 9D trajectory
- Frequency tracking pass: root Compton-like frequency used as a traced post-lock amplitude maturation guide

Research direction from latest chat ledger:
- Structural improvements beat brute coefficient fitting.
- Applying temporal dynamics to local exposures helped.
- Applying temporal dynamics to pair channels helped more.
- Strict global zero-point conservation worked numerically but starved outer bins.
- Localized redistribution restored fit, but exact closure still needs refinement.
