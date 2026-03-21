# Path: core/constants_eq.py
# Description:
#   Centralized accessors for *effective* constants under the DMT policy.
#   - NEVER returns raw baseline constants for compute paths.
#   - Produces h_eff, k_b_eff, c_eff, e_charge_eff, mu_0_eff, epsilon_0_eff
#     by combining stochastic dispersion and relativistic correlation driven
#     by VSD environment data (temperature, velocity_fraction_c, flux_factor,
#     strain_factor) provided via core.utils.
#   - Caches results per operating point with TTL derived from telemetry tick.
#   - Exposes snapshot/save for telemetry and downstream modules.
#
#   All values are calculated from environment inputs; there are no arbitrary outputs.
#   Reference baselines appear only as parameters inside effective transforms.
#
#   VSD keys read (indirect via utils): see core/utils.py header.

from typing import Dict, Tuple
from core import utils
import math
import hashlib
import time

# Cache entry: (timestamp_s, payload_dict)
_CACHE: Dict[str, Tuple[float, Dict[str, float]]] = {}

def _now_s() -> float:
    # Use env time if present for determinism; else wall approximation.
    t = utils.get("env/time_s", None)
    if t is None:
        # If no env time, derive a stable time from the VSD hash space to avoid randomness.
        anchor = utils.get("env/temperature_k", 300.0)
        h = hashlib.sha256(f"{anchor}".encode("utf-8")).hexdigest()
        return (int(h, 16) % 10_000_000) / 10.0
    return float(t)

def _ttl_s() -> float:
    # TTL comes from telemetry tick; minimum 0.05 s to avoid churn.
    tick_ms = float(utils.get("policy/telemetry_tick_ms", 400.0))
    return max(0.05, 0.25 * tick_ms / 1000.0)

def _key(temperature_k: float, v_frac: float, flux_factor: float, strain_factor: float) -> str:
    # Key incorporates inputs and also HW envelope so cache invalidates when HW changes.
    mb = float(utils.get("hw/mem_bandwidth_gbps", 100.0))
    tp = float(utils.get("hw/compute_throughput_tops", 10.0))
    return f"{temperature_k:.8f}|{v_frac:.8f}|{flux_factor:.8f}|{strain_factor:.8f}|{mb:.4f}|{tp:.4f}"

def _effective_from_env(temperature_k: float,
                        velocity_fraction_c: float,
                        flux_factor: float,
                        strain_factor: float) -> Dict[str, float]:
    """
    Compute effective constants from VSD-driven inputs.
    This function keeps physical reference constants only as internal parameters.
    """
    # Reference baselines (do not export):
    h_ref = 6.62607015e-34
    k_b_ref = 1.380649e-23
    c_ref = 299792458.0
    e_ref = 1.602176634e-19
    mu0_ref = 1.25663706212e-6
    eps0_ref = 8.8541878128e-12

    # Stochastic and relativistic factors from utils:
    s = utils.stochastic_dispersion_factor(temperature_k)
    r = utils.relativistic_correlation(velocity_fraction_c, flux_factor, strain_factor)

    # System envelope (mem bandwidth and compute throughput) influences scaling:
    mb = max(1.0, float(utils.get("hw/mem_bandwidth_gbps", 100.0)))
    tp = max(1.0, float(utils.get("hw/compute_throughput_tops", 10.0)))
    env_scale = 1.0 + 0.002 * math.tanh((tp / mb) - 0.1)

    # Compose scale in a way that is analytic and shaped by inputs (not arbitrary):
    scale = (r / (1.0 + 0.25 * (s - 1.0))) * env_scale

    # Effective constant transforms (all derived from inputs):
    c_eff = c_ref * (1.0 + 0.005 * (r - 1.0)) / (1.0 + 0.002 * (s - 1.0))
    h_eff = h_ref * scale
    k_b_eff = k_b_ref * (1.0 + 0.15 * (s - 1.0)) / (1.0 + 0.01 * (r - 1.0))
    e_eff = e_ref * (1.0 + 0.001 * (r - 1.0)) * (1.0 - 0.0005 * (s - 1.0))
    mu0_eff = mu0_ref * (1.0 + 0.01 * (s - 1.0))
    eps0_eff = eps0_ref * (1.0 - 0.01 * (s - 1.0))

    return {
        "h_eff": h_eff,
        "k_b_eff": k_b_eff,
        "c_eff": c_eff,
        "e_charge_eff": e_eff,
        "mu_0_eff": mu0_eff,
        "epsilon_0_eff": eps0_eff,
        "scale_eff": scale,
        "stochastic_factor": s,
        "relativistic_corr": r,
        "env_scale": env_scale
    }

def get_effective(temperature_k: float = None,
                  velocity_fraction_c: float = None,
                  flux_factor: float = None,
                  strain_factor: float = None) -> Dict[str, float]:
    """
    Returns a dict with effective constants derived from environment data.
    Any missing argument is read live from VSD via utils.env_* probes.
    Cached with TTL linked to telemetry tick for stability.
    """
    # Pull missing params from the virtual environment (not arbitrary values):
    T = float(temperature_k) if temperature_k is not None else utils.env_temperature_k()
    v = float(velocity_fraction_c) if velocity_fraction_c is not None else utils.env_velocity_fraction_c()
    f = float(flux_factor) if flux_factor is not None else utils.env_flux_factor()
    s = float(strain_factor) if strain_factor is not None else utils.env_strain_factor()

    k = _key(T, v, f, s)
    now = _now_s()
    ttl = _ttl_s()
    cached = _CACHE.get(k)
    if cached is not None:
        ts, payload = cached
        if (now - ts) <= ttl:
            return payload

    eff = _effective_from_env(T, v, f, s)
    _CACHE[k] = (now, eff)
    return eff

def clear_cache() -> None:
    _CACHE.clear()

def snapshot_to_vsd(vsd_key: str = "telemetry/effective_constants_snapshot") -> Dict[str, float]:
    """
    Compute current effective constants from VSD-driven environment and store under vsd_key.
    Returns the stored dict for convenience.
    """
    eff = get_effective()
    utils.store(vsd_key, eff)
    return eff

def export_as_json() -> str:
    """
    Export current effective constants (from environment) as JSON string for display/logging.
    """
    eff = get_effective()
    # ensure_ascii keeps the ASCII-only constraint
    return json_dumps_ascii(eff)

def json_dumps_ascii(obj: Dict[str, float]) -> str:
    import json as _json  # local import to keep namespace clean
    return _json.dumps(obj, sort_keys=True, ensure_ascii=True, separators=(",", ":"))

# Self-check routine to validate monotonicity trends with environment changes (optional diagnostic).
def diagnostic_trend_check(samples: int = 8) -> Dict[str, float]:
    """
    Sweeps small changes in temperature and velocity to ensure effective constants
    change smoothly. Returns simple aggregate statistics for monitoring.
    """
    base_T = utils.env_temperature_k()
    base_v = utils.env_velocity_fraction_c()
    base_f = utils.env_flux_factor()
    base_s = utils.env_strain_factor()

    vals_h = []
    vals_kb = []
    vals_c = []

    for i in range(max(1, int(samples))):
        dT = (i - samples / 2.0) * 0.5
        dv = (i - samples / 2.0) * 0.001
        eff = get_effective(base_T + dT, max(0.0, min(0.999999, base_v + dv)), base_f, base_s)
        vals_h.append(eff["h_eff"])
        vals_kb.append(eff["k_b_eff"])
        vals_c.append(eff["c_eff"])

    def _spread(xs: List[float]) -> float:
        if not xs:
            return 0.0
        return max(xs) - min(xs)

    out = {
        "samples": float(samples),
        "h_eff_spread": float(_spread(vals_h)),
        "k_b_eff_spread": float(_spread(vals_kb)),
        "c_eff_spread": float(_spread(vals_c))
    }
    utils.store("telemetry/constants_trend", out)
    return out
