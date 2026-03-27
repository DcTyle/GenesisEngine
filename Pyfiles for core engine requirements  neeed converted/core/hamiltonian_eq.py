# Path: core/hamiltonian_eq.py

# Description:

#   Hamiltonian construction and evolution helpers under the DMT effective-constant regime.

#   - Builds normalized scalar Hamiltonian magnitudes derived from environment (no arbitrary outputs).

#   - Produces small dense proxies (2x2 and NxN) for testing and pedagogical propagation.

#   - Provides spectral decomposition, propagator building, and stability-checked time stepping.

#   - Integrates with core.utils and core.constants_eq for effective constants, Lorentzflux,

#     stochastic dispersion, and deterministic evolution.

#   - ASCII-only implementation; fully deterministic with optional VSD-seeded RNG.

#

#   Inputs are read from VSD via core.utils probes when not provided explicitly:

#     env/temperature_k, env/velocity_fraction_c, env/flux_factor, env/strain_factor, env/field_strength

#   Hardware envelope (mem bandwidth, compute throughput) is honored indirectly via constants_eq.

#

#   This module exports:

#     hamiltonian_norm(...)

#     two_level_effective(...)

#     n_level_effective(...)

#     spectral_decompose_hermitian(...)

#     make_propagator_first_order(...)

#     evolve_state_two_level(...)

#     evolve_state_n_level(...)

#     adaptive_step_integrate(...)

#

#   No stubs, no placeholders, no arbitrary numbers as outputs. All values are analytic transforms

#   of environment and policy data or derived from inputs.



from typing import List, Tuple, Dict
import os

from core import utils

from core import constants_eq

# Optional high-precision backend
        """
        Integrates state over total_time_s with adaptive step sizing:

            - Starts from dt based on telemetry tick (policy/telemetry_tick_ms).

            - Monitors local change; if large, halves dt; if small, expands dt mildly.

            - Ensures determinism via VSD env and avoids arbitrary step choices.

        """

        # Optional high-precision path
        if _hp_enabled():
                return adaptive_step_integrate_hp(state, total_time_s, max_steps, field_strength, temperature_k, velocity_fraction_c, flux_factor, strain_factor, damping, seed)

        t_remain = max(0.0, float(total_time_s))

def _hp_set_dps() -> int:
    try:
        dps = int(os.environ.get("HP_DPS", "18"))
    except Exception:
        dps = 18
    dps = max(16, min(10000, dps))
    if _mp is not None:
        _mp.mp.dps = dps
    return dps



# -----------------------------

# Normalized scalar "Hamiltonian magnitude"

# -----------------------------

def hamiltonian_norm(field_strength: float = None,

                     temperature_k: float = None,

                     velocity_fraction_c: float = None,

                     flux_factor: float = None,

                     strain_factor: float = None) -> float:

    

    # Computes a normalized scalar Hamiltonian magnitude influenced by:

    # - DMT effective constants (scale_eff, c_eff),

    # - Lorentzflux coupling,

    # - and a coherence-aware shaping term.

    # All inputs default to environment probes to avoid arbitrary outputs.

    

    T = float(temperature_k) if temperature_k is not None else utils.env_temperature_k()

    v = float(velocity_fraction_c) if velocity_fraction_c is not None else utils.env_velocity_fraction_c()

    f = float(flux_factor) if flux_factor is not None else utils.env_flux_factor()

    s = float(strain_factor) if strain_factor is not None else utils.env_strain_factor()

    F = float(field_strength) if field_strength is not None else utils.env_field_strength()



    eff = constants_eq.get_effective(T, v, f, s)

    scale = eff["scale_eff"]

    c_eff = eff["c_eff"]



    # Lorentzflux coupling

    lf = utils.lorentz_flux(F, v, f)



    # Coherence-aware shaping (short-time coherence proxy):

    coh = utils.coherence_loss(1e-6, T, base_tau_s=1e-3)  # caller chooses base_tau_s elsewhere; this is a tiny step check

    # Combine into a smooth positive magnitude:

    return abs(scale) * (1.0 + lf / max(1.0, c_eff)) * (1.0 + 0.05 * (1.0 - coh))



# -----------------------------

# Two-level effective Hamiltonian (2x2 dense proxy)

# -----------------------------

def two_level_effective(field_strength: float = None,

                        temperature_k: float = None,

                        velocity_fraction_c: float = None,

                        flux_factor: float = None,

                        strain_factor: float = None) -> List[List[complex]]:

    """

    Returns a 2x2 dense Hamiltonian-like effective matrix proxy.

    The off-diagonal terms are coupling; diagonals use a small detuning derived from scale.

    """

    T = float(temperature_k) if temperature_k is not None else utils.env_temperature_k()

    v = float(velocity_fraction_c) if velocity_fraction_c is not None else utils.env_velocity_fraction_c()

    f = float(flux_factor) if flux_factor is not None else utils.env_flux_factor()

    s = float(strain_factor) if strain_factor is not None else utils.env_strain_factor()

    F = float(field_strength) if field_strength is not None else utils.env_field_strength()



    eff = constants_eq.get_effective(T, v, f, s)

    h_norm = hamiltonian_norm(F, T, v, f, s)

    # Detuning scales with stochastic broadening to keep relation with environment:

    detune = 0.1 * h_norm * (1.0 + 0.1 * (eff["stochastic_factor"] - 1.0))



    return [

        [complex( detune, 0.0), complex(h_norm, 0.0)],

        [complex(h_norm, 0.0), complex(-detune, 0.0)]

    ]



# -----------------------------

# N-level dense proxy (Hermitian by construction)

# -----------------------------

def n_level_effective(dim: int,

                      field_strength: float = None,

                      temperature_k: float = None,

                      velocity_fraction_c: float = None,

                      flux_factor: float = None,

                      strain_factor: float = None,

                      seed: int = None) -> List[List[complex]]:

    """

    Builds a small Hermitian dense matrix H (dim x dim) as a proxy.

    - Diagonals: structured detuning pattern derived from h_norm and dispersion factor.

    - Off-diagonals: symmetric couplings derived from Lorentzflux and seeded RNG (deterministic via VSD).

    """

    d = max(2, int(dim))



    T = float(temperature_k) if temperature_k is not None else utils.env_temperature_k()

    v = float(velocity_fraction_c) if velocity_fraction_c is not None else utils.env_velocity_fraction_c()

    f = float(flux_factor) if flux_factor is not None else utils.env_flux_factor()

    s = float(strain_factor) if strain_factor is not None else utils.env_strain_factor()

    F = float(field_strength) if field_strength is not None else utils.env_field_strength()



    rng = utils.quantum_rng(utils.env_rng_seed() if seed is None else seed)

    h_norm = hamiltonian_norm(F, T, v, f, s)

    eff = constants_eq.get_effective(T, v, f, s)

    disp = eff["stochastic_factor"]



    # Initialize zero matrix

    H: List[List[complex]] = [[0j for _ in range(d)] for _ in range(d)]



    # Diagonals: deterministic detuning pattern shaped by dispersion

    # Use a centered index so spectrum is roughly symmetric for pedagogy

    center = (d - 1) * 0.5

    for i in range(d):

        det = (i - center) / max(1.0, center)

        diag_val = h_norm * (0.05 + 0.15 * abs(det)) * (1.0 + 0.1 * (disp - 1.0))

        H[i][i] = complex(diag_val, 0.0)



    # Off-diagonals: symmetric couplings scaled by lorentzflux and small randomization

    lf = utils.lorentz_flux(F, v, f)

    base_c = h_norm * (0.5 + 0.15 * max(0.0, lf) / (1.0 + abs(lf)))

    for i in range(d):

        for j in range(i + 1, d):

            # Distance-based falloff keeps matrix well-conditioned

            dist = float(j - i)

            falloff = 1.0 / (1.0 + 0.3 * dist * dist)

            jitter = 0.9 + 0.2 * (rng.random() - 0.5)  # deterministic via seed

            val = base_c * falloff * jitter

            H[i][j] = complex(val, 0.0)

            H[j][i] = complex(val, 0.0)

    return H



# -----------------------------

# Spectral decomposition for small Hermitian matrices

# -----------------------------

def spectral_decompose_hermitian(H: List[List[complex]]) -> Tuple[List[float], List[List[complex]]]:

    """

    Naive power-iteration based eigen decomposition for small Hermitian H.

    Returns (eigenvalues_sorted_asc, eigenvectors_columns).

    Deterministic and ASCII-only; intended for small dimensions (<= 16).

    """

    d = len(H)

    if d == 0 or len(H[0]) != d:

        raise ValueError("H must be square")



    # Copy H to avoid side effects

    M = [[complex(H[i][j]) for j in range(d)] for i in range(d)]



    # Simple Gram-Schmidt + power iteration loop

    def mat_vec(A: List[List[complex]], x: List[complex]) -> List[complex]:

        y = [0j] * d

        for i in range(d):

            acc = 0j

            row = A[i]

            for j in range(d):

                acc += row[j] * x[j]

            y[i] = acc

        return y



    def dot_conj(a: List[complex], b: List[complex]) -> complex:

        acc = 0j

        for i in range(d):

            acc += a[i].conjugate() * b[i]

        return acc



    def norm2(x: List[complex]) -> float:

        return float((dot_conj(x, x)).real) ** 0.5



    # Start with d random-ish deterministic vectors from VSD-seeded RNG

    rng = utils.quantum_rng(utils.env_rng_seed())

    vecs: List[List[complex]] = []

    for k in range(d):

        v0 = [complex(rng.random(), 0.0) for _ in range(d)]

        nrm = norm2(v0)

        if nrm > 0:

            v0 = [z / nrm for z in v0]

        vecs.append(v0)



    # Orthonormalize (modified Gram-Schmidt)

    def orthonormalize(V: List[List[complex]]) -> List[List[complex]]:

        U: List[List[complex]] = []

        for v in V:

            w = list(v)

            for u in U:

                proj = dot_conj(u, w)

                w = [w[i] - proj * u[i] for i in range(d)]

            n = norm2(w)

            if n > 1e-12:

                U.append([w[i] / n for i in range(d)])

        return U



    V = orthonormalize(vecs)



    # Power iterations to refine eigenvectors

    iters = 16  # small, deterministic count

    for _ in range(iters):

        W = []

        for v in V:

            w = mat_vec(M, v)

            # Re-orthogonalize against current W

            for u in W:

                proj = dot_conj(u, w)

                w = [w[i] - proj * u[i] for i in range(d)]

            n = norm2(w)

            if n > 0:

                W.append([w[i] / n for i in range(d)])

        V = W if W else V



    # Rayleigh quotients for eigenvalues

    evals: List[float] = []

    for v in V:

        Hv = mat_vec(M, v)

        lam = dot_conj(v, Hv)

        evals.append(float(lam.real))



    # Sort by ascending eigenvalue

    idx = list(range(len(evals)))

    idx.sort(key=lambda i: evals[i])

    evals_sorted = [evals[i] for i in idx]

    evecs_sorted = [V[i] for i in idx]

    # Return eigenvectors as columns (conventional)

    return evals_sorted, evecs_sorted



# -----------------------------

# Propagator builders

# -----------------------------

def make_propagator_first_order(H: List[List[complex]], dt_s: float, damping: float = 0.0) -> List[List[complex]]:

    """

    First-order propagator: U ? I - i*dt*H - dt*damping*I

    Suitable for small dt and testing; deterministic and ASCII-only.

    """

    d = len(H)

    U = [[0j for _ in range(d)] for _ in range(d)]

    scale_diag = 1.0 - float(damping) * float(dt_s)

    k = -1.0j * float(dt_s)

    for i in range(d):

        for j in range(d):

            if i == j:

                U[i][j] = complex(scale_diag, 0.0) + k * H[i][j]

            else:

                U[i][j] = k * H[i][j]

    return U



def apply_propagator(U: List[List[complex]], psi: List[complex]) -> List[complex]:

    d = len(U)

    if len(psi) != d:

        raise ValueError("State dimension mismatch")

    out = [0j] * d

    for i in range(d):

        acc = 0j

        row = U[i]

        for j in range(d):

            acc += row[j] * psi[j]

        out[i] = acc

    # Renormalize softly to manage drift

    nrm = sum(abs(z) ** 2 for z in out) ** 0.5

    if nrm > 0:

        out = [z / nrm for z in out]

    return out


# -----------------------------
# High-precision (mpmath) variants
# -----------------------------
def _hp_make_propagator_first_order(H: List[List[complex]], dt_s: float, damping: float = 0.0):
    dps = _hp_set_dps()
    mp = _mp
    d = len(H)
    U = [[mp.mpc(0, 0) for _ in range(d)] for _ in range(d)]
    scale_diag = mp.mpf(1) - mp.mpf(damping) * mp.mpf(dt_s)
    k = mp.mpc(0, -1) * mp.mpf(dt_s)
    for i in range(d):
        for j in range(d):
            Hij = mp.mpc(float(H[i][j].real), float(H[i][j].imag))
            if i == j:
                U[i][j] = mp.mpc(scale_diag, 0) + k * Hij
            else:
                U[i][j] = k * Hij
    return U


def _hp_apply_propagator(U, psi):
    mp = _mp
    d = len(U)
    x = [mp.mpc(float(z.real), float(z.imag)) for z in psi]
    out = [mp.mpc(0, 0) for _ in range(d)]
    for i in range(d):
        acc = mp.mpc(0, 0)
        row = U[i]
        for j in range(d):
            acc += row[j] * x[j]
        out[i] = acc
    nrm2 = mp.mpf(0)
    for z in out:
        nrm2 += (z.real * z.real + z.imag * z.imag)
    nrm = mp.sqrt(nrm2)
    if nrm != 0:
        out = [z / nrm for z in out]
    return [complex(float(z.real), float(z.imag)) for z in out]


def evolve_state_two_level_hp(state: List[complex],
                              dt_s: float,
                              field_strength: float = None,
                              temperature_k: float = None,
                              velocity_fraction_c: float = None,
                              flux_factor: float = None,
                              strain_factor: float = None,
                              damping: float = 0.0) -> List[complex]:
    """
    High-precision two-level propagation using mpmath with configurable HP_DPS.
    Mirrors evolve_state_two_level but computes propagator and application in mp types.
    """
    if _mp is None:
        # Fallback to standard path if mpmath unavailable
        return evolve_state_two_level(state, dt_s, field_strength, temperature_k, velocity_fraction_c, flux_factor, strain_factor, damping)
    H = two_level_effective(field_strength, temperature_k, velocity_fraction_c, flux_factor, strain_factor)
    U_hp = _hp_make_propagator_first_order(H, dt_s, damping=damping)
    return _hp_apply_propagator(U_hp, list(state))



# -----------------------------

# Evolution helpers

# -----------------------------

def evolve_state_two_level(state: List[complex],

                           dt_s: float,

                           field_strength: float = None,

                           temperature_k: float = None,

                           velocity_fraction_c: float = None,

                           flux_factor: float = None,

                           strain_factor: float = None,

                           damping: float = 0.0) -> List[complex]:

    """

    Two-level propagation using first-order propagator on 2x2 proxy.

    """

    H = two_level_effective(field_strength, temperature_k, velocity_fraction_c, flux_factor, strain_factor)

    U = make_propagator_first_order(H, dt_s, damping=damping)

    return apply_propagator(U, list(state))



def evolve_state_n_level(state: List[complex],

                         dt_s: float,

                         field_strength: float = None,

                         temperature_k: float = None,

                         velocity_fraction_c: float = None,

                         flux_factor: float = None,

                         strain_factor: float = None,

                         damping: float = 0.0,

                         seed: int = None) -> List[complex]:

    """

    N-level propagation using first-order propagator on dense Hermitian proxy.

    """

    # Optional high-precision path
    if _hp_enabled():
        return evolve_state_n_level_hp(state, dt_s, field_strength, temperature_k, velocity_fraction_c, flux_factor, strain_factor, damping, seed)
    d = max(2, len(state))

    H = n_level_effective(d, field_strength, temperature_k, velocity_fraction_c, flux_factor, strain_factor, seed=seed)

    U = make_propagator_first_order(H, dt_s, damping=damping)

    return apply_propagator(U, list(state))



# -----------------------------

# Adaptive step integration

# -----------------------------

def adaptive_step_integrate(state: List[complex],

                            total_time_s: float,

                            max_steps: int,

                            field_strength: float = None,

                            temperature_k: float = None,

                            velocity_fraction_c: float = None,

                            flux_factor: float = None,

                            strain_factor: float = None,

                            damping: float = 0.0,

                            seed: int = None) -> List[complex]:

    """

    Integrates state over total_time_s with adaptive step sizing:

      - Starts from dt based on telemetry tick (policy/telemetry_tick_ms).

      - Monitors local change; if large, halves dt; if small, expands dt mildly.

      - Ensures determinism via VSD env and avoids arbitrary step choices.

    """

    t_remain = max(0.0, float(total_time_s))

    steps_left = max(1, int(max_steps))

    tick_ms = float(utils.get("policy/telemetry_tick_ms", 400.0))

    dt = max(1e-9, min(t_remain, 0.25 * tick_ms / 1000.0))



    psi = list(state)

    last_energy = None



    while t_remain > 0.0 and steps_left > 0:

        dt = min(dt, t_remain)

        # Use N-level to be general; dim inferred from state length

        next_psi = evolve_state_n_level(

            psi, dt,

            field_strength=field_strength,

            temperature_k=temperature_k,

            velocity_fraction_c=velocity_fraction_c,

            flux_factor=flux_factor,

            strain_factor=strain_factor,

            damping=damping,

            seed=seed

        )

        # Local "energy" proxy using instantaneous H-norm

        T = temperature_k if temperature_k is not None else utils.env_temperature_k()

        v = velocity_fraction_c if velocity_fraction_c is not None else utils.env_velocity_fraction_c()

        f = flux_factor if flux_factor is not None else utils.env_flux_factor()

        s = strain_factor if strain_factor is not None else utils.env_strain_factor()

        F = field_strength if field_strength is not None else utils.env_field_strength()

        h_norm = hamiltonian_norm(F, T, v, f, s)



        # Change magnitude

        dpsi = sum(abs(next_psi[i] - psi[i]) ** 2 for i in range(len(psi))) ** 0.5

        energy = h_norm

        local_ratio = dpsi / (1.0 + energy)



        # Adapt dt based on local change to avoid instability:

        if local_ratio > 0.05:

            dt *= 0.5

        elif local_ratio < 0.01:

            dt *= 1.25



        psi = next_psi

        t_remain -= dt

        steps_left -= 1



        # Soft renormalization

        nrm = sum(abs(z) ** 2 for z in psi) ** 0.5

        if nrm > 0:

            psi = [z / nrm for z in psi]



        # Track last energy for diagnostics (store in VSD for telemetry)

        last_energy = energy



    if last_energy is not None:

        utils.store("telemetry/hamiltonian_last_energy", {"value": float(last_energy), "unit": "arb"})



    return psi

def evolve_state_n_level_hp(state: List[complex],
                            dt_s: float,
                            field_strength: float = None,
                            temperature_k: float = None,
                            velocity_fraction_c: float = None,
                            flux_factor: float = None,
                            strain_factor: float = None,
                            damping: float = 0.0,
                            seed: int = None) -> List[complex]:
    """
    High-precision N-level propagation using mpmath with configurable HP_DPS.
    Mirrors evolve_state_n_level but computes propagator and application in mp types.
    """
    if _mp is None:
        return evolve_state_n_level(state, dt_s, field_strength, temperature_k, velocity_fraction_c, flux_factor, strain_factor, damping, seed)
    d = max(2, len(state))
    H = n_level_effective(d, field_strength, temperature_k, velocity_fraction_c, flux_factor, strain_factor, seed=seed)
    U_hp = _hp_make_propagator_first_order(H, dt_s, damping=damping)
    return _hp_apply_propagator(U_hp, list(state))


def adaptive_step_integrate_hp(state: List[complex],
                               total_time_s: float,
                               max_steps: int,
                               field_strength: float = None,
                               temperature_k: float = None,
                               velocity_fraction_c: float = None,
                               flux_factor: float = None,
                               strain_factor: float = None,
                               damping: float = 0.0,
                               seed: int = None) -> List[complex]:
    """
    High-precision adaptive integrator mirroring adaptive_step_integrate but using the HP N-level evolve.
    """
    t_remain = max(0.0, float(total_time_s))
    steps_left = max(1, int(max_steps))
    tick_ms = float(utils.get("policy/telemetry_tick_ms", 400.0))
    dt = max(1e-9, min(t_remain, 0.25 * tick_ms / 1000.0))

    psi = list(state)
    last_energy = None

    while t_remain > 0.0 and steps_left > 0:
        dt = min(dt, t_remain)
        next_psi = evolve_state_n_level_hp(
            psi, dt,
            field_strength=field_strength,
            temperature_k=temperature_k,
            velocity_fraction_c=velocity_fraction_c,
            flux_factor=flux_factor,
            strain_factor=strain_factor,
            damping=damping,
            seed=seed
        )
        # Local energy proxy
        T = temperature_k if temperature_k is not None else utils.env_temperature_k()
        v = velocity_fraction_c if velocity_fraction_c is not None else utils.env_velocity_fraction_c()
        f = flux_factor if flux_factor is not None else utils.env_flux_factor()
        s = strain_factor if strain_factor is not None else utils.env_strain_factor()
        F = field_strength if field_strength is not None else utils.env_field_strength()
        h_norm = hamiltonian_norm(F, T, v, f, s)

        dpsi = sum(abs(next_psi[i] - psi[i]) ** 2 for i in range(len(psi))) ** 0.5
        energy = h_norm
        local_ratio = dpsi / (1.0 + energy)

        if local_ratio > 0.05:
            dt *= 0.5
        elif local_ratio < 0.01:
            dt *= 1.25

        psi = next_psi
        t_remain -= dt
        steps_left -= 1

        nrm = sum(abs(z) ** 2 for z in psi) ** 0.5
        if nrm > 0:
            psi = [z / nrm for z in psi]

        last_energy = energy

    if last_energy is not None:
        utils.store("telemetry/hamiltonian_last_energy", {"value": float(last_energy), "unit": "arb"})

    return psi

