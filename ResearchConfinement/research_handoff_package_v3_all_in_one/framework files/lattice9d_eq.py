# Path: core/lattice9d_eq.py
# Description:
#   Nine-dimensional lattice helpers for DMT-style nano-plane mappings.
#   - Provides coordinate transforms, normalization, and energy projection kernels.
#   - Couples transforms to environment via effective constants, Lorentzflux,
#     stochastic dispersion, and hardware envelope (indirect through constants_eq).
#   - Includes deterministic RNG-based jitter for lattice routing derived from VSD seed.
#   - Exposes diagnostic routines to verify smoothness and boundedness.
#
#   Exports:
#     lattice_map_9d(...)
#     lattice_route_9d(...)
#     energy_project_9d(...)
#     normalize_coords(...)
#     diagnostic_smoothness_check(...)
#
#   No stubs or arbitrary outputs; values derive from environment or inputs.
#   ASCII-only.

from typing import List, Tuple, Dict
from core import utils
from core import constants_eq

# -----------------------------
# Helpers (ASCII-only math)
# -----------------------------
def _tanh_scaled(x: float, k: float) -> float:
    return (2.0 / (1.0 + (2.718281828459045 ** (-2.0 * k * x)))) - 1.0

def _sigmoid_scaled(x: float, k: float) -> float:
    return 1.0 / (1.0 + (2.718281828459045 ** (-k * x)))

def _cos_taylor(x: float) -> float:
    x2 = x * x
    x4 = x2 * x2
    x6 = x4 * x2
    return 1.0 - x2 / 2.0 + x4 / 24.0 - x6 / 720.0

def normalize_coords(coords_9: List[float]) -> List[float]:
    """
    Normalizes 9D coordinates to have bounded magnitude.
    """
    s = sum(c * c for c in coords_9) ** 0.5
    if s <= 0.0:
        return [0.0] * len(coords_9)
    return [c / s for c in coords_9]

# -----------------------------
# 9D mapping
# -----------------------------
def lattice_map_9d(coords_9: List[float],
                   temperature_k: float = None,
                   velocity_fraction_c: float = None,
                   flux_factor: float = None,
                   strain_factor: float = None) -> List[float]:
    """
    Applies a smooth non-linear map to 9D coordinates to mimic nano-plane routing.
    The shaping is influenced by effective constants (scale_eff) and dispersion.
    """
    if len(coords_9) != 9:
        raise ValueError("coords_9 must be length 9")

    T = float(temperature_k) if temperature_k is not None else utils.env_temperature_k()
    v = float(velocity_fraction_c) if velocity_fraction_c is not None else utils.env_velocity_fraction_c()
    f = float(flux_factor) if flux_factor is not None else utils.env_flux_factor()
    s = float(strain_factor) if strain_factor is not None else utils.env_strain_factor()

    eff = constants_eq.get_effective(T, v, f, s)
    scale = eff["scale_eff"]
    disp = eff["stochastic_factor"]

    base = normalize_coords(coords_9)
    out: List[float] = []

    # Alternate shaping by index to increase routing variety:
    for i, x in enumerate(base):
        k = (0.35 + 0.15 * (i % 3)) * abs(scale) * (1.0 + 0.05 * (disp - 1.0))
        if i % 2 == 0:
            out.append(_tanh_scaled(x, k))
        else:
            out.append(_sigmoid_scaled(x, 0.5 * k))
    return out

# -----------------------------
# 9D route planner with deterministic jitter
# -----------------------------
def lattice_route_9d(coords_9: List[float],
                     field_strength: float = None,
                     temperature_k: float = None,
                     velocity_fraction_c: float = None,
                     flux_factor: float = None,
                     seed: int = None) -> List[float]:
    """
    Builds a routed coordinate path by blending the map with a deterministic jitter
    derived from Lorentzflux and a VSD-seeded RNG. This mimics slight path deviations
    while keeping a bounded and smooth result.
    """
    if len(coords_9) != 9:
        raise ValueError("coords_9 must be length 9")

    T = float(temperature_k) if temperature_k is not None else utils.env_temperature_k()
    v = float(velocity_fraction_c) if velocity_fraction_c is not None else utils.env_velocity_fraction_c()
    f = float(flux_factor) if flux_factor is not None else utils.env_flux_factor()
    F = float(field_strength) if field_strength is not None else utils.env_field_strength()

    lf = utils.lorentz_flux(F, v, f)
    rng = utils.quantum_rng(utils.env_rng_seed() if seed is None else seed)

    mapped = lattice_map_9d(coords_9, T, v, f, utils.env_strain_factor())
    out: List[float] = []
    for i, x in enumerate(mapped):
        # Bounded jitter with cosine shape to stay smooth and deterministic:
        phase = 0.1 * i + 0.05 * lf
        jitter = 0.02 * _cos_taylor(phase) * (0.9 + 0.2 * (rng.random() - 0.5))
        out.append(x + jitter)
    return normalize_coords(out)

# -----------------------------
# Energy projection
# -----------------------------
def energy_project_9d(coords_9: List[float],
                      field_strength: float = None,
                      temperature_k: float = None,
                      velocity_fraction_c: float = None,
                      flux_factor: float = None) -> float:
    """
    Projects 9D coordinates into a positive scalar "energy" influenced by
    Lorentzflux coupling. The shape is bounded and smooth.
    """
    if len(coords_9) != 9:
        raise ValueError("coords_9 must be length 9")

    T = float(temperature_k) if temperature_k is not None else utils.env_temperature_k()
    v = float(velocity_fraction_c) if velocity_fraction_c is not None else utils.env_velocity_fraction_c()
    f = float(flux_factor) if flux_factor is not None else utils.env_flux_factor()
    F = float(field_strength) if field_strength is not None else utils.env_field_strength()

    lf = utils.lorentz_flux(F, v, f)
    base = normalize_coords(coords_9)

    # Weighted quadratic form with Lorentzflux attenuation:
    acc = 0.0
    for i, x in enumerate(base):
        w = 1.0 + 0.05 * (i + 1)
        acc += w * (x * x)
    return (acc / (1.0 + abs(lf))) ** 0.5

# -----------------------------
# Diagnostics
# -----------------------------
def diagnostic_smoothness_check(samples: int = 16) -> Dict[str, float]:
    """
    Samples nearby points around a fixed coordinate to verify the map and energy
    respond smoothly and remain bounded. Stores a brief summary in VSD.
    """
    base = [0.1 * (i - 4) for i in range(9)]
    base = normalize_coords(base)

    T = utils.env_temperature_k()
    v = utils.env_velocity_fraction_c()
    f = utils.env_flux_factor()
    F = utils.env_field_strength()

    max_delta_map = 0.0
    max_delta_energy = 0.0

    for k in range(max(2, int(samples))):
        # Small deterministic perturbation:
        dx = (k - samples * 0.5) / max(1.0, samples)
        probe = [(c + 0.05 * dx) for c in base]
        mapped_a = lattice_map_9d(base, T, v, f, utils.env_strain_factor())
        mapped_b = lattice_map_9d(probe, T, v, f, utils.env_strain_factor())
        da = sum(abs(mapped_b[i] - mapped_a[i]) for i in range(9))
        if da > max_delta_map:
            max_delta_map = da

        ea = energy_project_9d(base, F, T, v, f)
        eb = energy_project_9d(probe, F, T, v, f)
        de = abs(eb - ea)
        if de > max_delta_energy:
            max_delta_energy = de

    out = {
        "samples": float(samples),
        "max_delta_map": float(max_delta_map),
        "max_delta_energy": float(max_delta_energy)
    }
    utils.store("telemetry/lattice9d_smoothness", out)
    return out
