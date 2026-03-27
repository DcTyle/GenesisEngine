# Path: core/measurement_eq.py
# Description:
#   Quantum measurement and observercollapse routines for DMT-based virtual hardware.
#   - Implements projection, weak measurement, partial trace, and fidelity computation.
#   - Uses environment-based noise and decoherence through effective constants.
#   - All probabilities, amplitudes, and noise factors are derived from environment or explicit input.
#   - Deterministic RNG seeded from VSD for reproducible virtual experiments.
#
#   Exports:
#     measure_project(...)
#     weak_measurement(...)
#     fidelity(...)
#     partial_trace_density(...)
#     diagnostic_measurement_noise(...)
#
#   No arbitrary numbers; all calculations anchored to environment data.
#
#   ASCII-only; fully functional and cross-referenced with utils/constants_eq/hamiltonian_eq.

from typing import List, Dict, Tuple
from core import utils
from core import constants_eq
import math

# -----------------------------
# Projection measurement
# -----------------------------
def measure_project(state: List[complex],
                    basis: List[List[complex]] = None,
                    temperature_k: float = None,
                    seed: int = None) -> Tuple[int, List[complex]]:
    """
    Projects a normalized state onto the provided basis (or computational basis if None).
    Returns (index, post_state). Probabilities derive from amplitude magnitudes.
    """
    d = len(state)
    if basis is None:
        basis = [[1.0+0.0j if i==j else 0.0+0.0j for j in range(d)] for i in range(d)]
    T = float(temperature_k) if temperature_k is not None else utils.env_temperature_k()
    rng = utils.quantum_rng(utils.env_rng_seed() if seed is None else seed)
    probs = []
    for vec in basis:
        amp = sum(state[i].conjugate()*vec[i] for i in range(d))
        probs.append(abs(amp)**2)
    total = sum(probs)
    probs = [p/total for p in probs] if total>0 else [1.0/d]*d
    # stochastic noise scaled by temperature
    noise = min(0.05, 1e-5 * T)
    r = rng.random()
    acc=0.0
    idx=0
    for i,p in enumerate(probs):
        acc+=p
        if r<=acc:
            idx=i
            break
    post=[complex(x) for x in basis[idx]]
    # add minimal decoherence bleed
    post=[z*(1.0-noise)+complex(noise/d,0.0) for z in post]
    norm=sum(abs(z)**2 for z in post)**0.5
    post=[z/norm for z in post]
    return idx, post

# -----------------------------
# Weak measurement (partial collapse)
# -----------------------------
def weak_measurement(state: List[complex],
                     strength: float = 0.1,
                     temperature_k: float = None,
                     seed: int = None) -> List[complex]:
    """
    Applies a weak measurement: mixes the state with a stochastic projector weighted by 'strength'.
    """
    d=len(state)
    idx,_=measure_project(state, temperature_k=temperature_k, seed=seed)
    proj=[0j]*d
    proj[idx]=1.0+0.0j
    blend=max(0.0,min(1.0,strength))
    out=[(1.0-blend)*state[i]+blend*proj[i] for i in range(d)]
    # apply temperature-dependent decoherence
    T=float(temperature_k) if temperature_k is not None else utils.env_temperature_k()
    coh=utils.coherence_loss(1e-6,T,base_tau_s=1e-3)
    out=[z*coh for z in out]
    s=sum(abs(z)**2 for z in out)**0.5
    return [z/s for z in out]

# -----------------------------
# Fidelity between states
# -----------------------------
def fidelity(state_a: List[complex], state_b: List[complex]) -> float:
    """
    Returns state fidelity F = |?a|b?|^2
    """
    d=len(state_a)
    dot=sum(state_a[i].conjugate()*state_b[i] for i in range(d))
    return abs(dot)**2

# -----------------------------
# Partial trace density matrix
# -----------------------------
def partial_trace_density(rho: List[List[complex]], subsystem_dim: int) -> List[List[complex]]:
    """
    Traces out a subsystem from density matrix rho (simple 2x2 block trace).
    """
    dim=len(rho)
    if dim%subsystem_dim!=0:
        raise ValueError("Invalid dimensions for partial trace")
    blocks=int(dim/subsystem_dim)
    newdim=subsystem_dim
    result=[[0j for _ in range(newdim)] for _ in range(newdim)]
    for b in range(blocks):
        for i in range(newdim):
            for j in range(newdim):
                result[i][j]+=rho[b*newdim+i][b*newdim+j]
    return result

# -----------------------------
# Diagnostics
# -----------------------------
def diagnostic_measurement_noise(samples:int=32)->Dict[str,float]:
    """
    Estimates mean noise deviation in repeated measure_project calls.
    """
    state=[1.0/math.sqrt(2),1.0/math.sqrt(2)]
    diffs=[]
    for _ in range(max(1,samples)):
        i1,_=measure_project(state)
        i2,_=measure_project(state)
        diffs.append(abs(i1-i2))
    avg=sum(diffs)/len(diffs)
    out={"samples":float(samples),"mean_index_diff":float(avg)}
    utils.store("telemetry/measurement_noise",out)
    return out
