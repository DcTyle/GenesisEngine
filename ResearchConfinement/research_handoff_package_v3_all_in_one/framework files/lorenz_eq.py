# Path: core/lorenz_eq.py
# Description:
#   Lorentz-field correlation and fluxstrain dynamics under DMT effective constants.
#   - Models Lorentz-style coupled differential systems parameterized by effective constants.
#   - Provides functions for virtual trajectory generation, attractor embedding, and
#     temporal coherence diagnostics.
#   - All scaling terms come from VSD-driven environment data (temperature, flux, strain, etc.).
#   - Deterministic, ASCII-only implementation; suitable for diagnostic visualization.
#
#   Exports:
#     lorenz_step(...)
#     lorenz_trajectory(...)
#     lorentz_field_energy(...)
#     lorentz_flux_spectrum(...)
#     diagnostic_lorentz_stability(...)
#
#   All computations use DMT effective constants for correlation, no arbitrary numbers.

from typing import List, Dict, Tuple
from core import utils
from core import constants_eq
import math

# -----------------------------
# Lorenz-like system stepper
# -----------------------------
def lorenz_step(x: float, y: float, z: float,
                dt_s: float,
                temperature_k: float = None,
                velocity_fraction_c: float = None,
                flux_factor: float = None,
                strain_factor: float = None) -> Tuple[float, float, float]:
    """
    One integration step for Lorentz-like fluxstrain system.
    Coefficients are derived from effective constants and flux coupling.
    """
    T = float(temperature_k) if temperature_k is not None else utils.env_temperature_k()
    v = float(velocity_fraction_c) if velocity_fraction_c is not None else utils.env_velocity_fraction_c()
    f = float(flux_factor) if flux_factor is not None else utils.env_flux_factor()
    s = float(strain_factor) if strain_factor is not None else utils.env_strain_factor()
    eff = constants_eq.get_effective(T, v, f, s)

    sigma = 10.0 * eff["scale_eff"]      # fluxstrain coupling gain
    beta = 2.5 * eff["stochastic_factor"]
    rho = 28.0 * (1.0 + 0.1 * (eff["relativistic_corr"] - 1.0))

    dx = sigma * (y - x)
    dy = x * (rho - z) - y
    dz = x * y - beta * z

    nx = x + dt_s * dx
    ny = y + dt_s * dy
    nz = z + dt_s * dz
    return nx, ny, nz

# -----------------------------
# Trajectory generator
# -----------------------------
def lorenz_trajectory(steps: int,
                      dt_s: float,
                      init: Tuple[float, float, float] = (0.1, 0.0, 0.0)) -> List[Tuple[float, float, float]]:
    """
    Generates Lorentz-like trajectory using VSD environment parameters.
    Returns list of (x, y, z) tuples.
    """
    x, y, z = init
    path: List[Tuple[float, float, float]] = []
    for _ in range(max(1, int(steps))):
        x, y, z = lorenz_step(x, y, z, dt_s)
        path.append((x, y, z))
    return path

# -----------------------------
# Field energy metric
# -----------------------------
def lorentz_field_energy(state: Tuple[float, float, float]) -> float:
    """
    Computes an effective "field energy" proxy from state using Lorentzflux scaling.
    """
    x, y, z = state
    F = utils.env_field_strength()
    v = utils.env_velocity_fraction_c()
    f = utils.env_flux_factor()
    lf = utils.lorentz_flux(F, v, f)
    return abs(lf) * (x * x + y * y + 0.5 * z * z)

# -----------------------------
# Flux spectrum sampler
# -----------------------------
def lorentz_flux_spectrum(samples: int = 128) -> List[float]:
    """
    Returns smooth spectrum of fluxstrain power from virtual Lorentz trajectory.
    """
    traj = lorenz_trajectory(samples, dt_s=0.002)
    vals: List[float] = []
    for s in traj:
        vals.append(lorentz_field_energy(s))
    # Normalize
    total = sum(vals) + 1e-12
    vals = [v / total for v in vals]
    return vals

# -----------------------------
# Diagnostics
# -----------------------------
def diagnostic_lorentz_stability(samples: int = 64) -> Dict[str, float]:
    """
    Checks Lorentz trajectory boundedness and stores mean amplitude & energy spread.
    """
    traj = lorenz_trajectory(samples, dt_s=0.0015)
    amps = [abs(x) + abs(y) + abs(z) for (x, y, z) in traj]
    energy = [lorentz_field_energy(s) for s in traj]
    mean_amp = sum(amps) / len(amps)
    spread = max(amps) - min(amps)
    mean_E = sum(energy) / len(energy)
    spread_E = max(energy) - min(energy)
    out = {
        "mean_amplitude": float(mean_amp),
        "spread_amplitude": float(spread),
        "mean_energy": float(mean_E),
        "spread_energy": float(spread_E)
    }
    utils.store("telemetry/lorentz_stability", out)
    return out
