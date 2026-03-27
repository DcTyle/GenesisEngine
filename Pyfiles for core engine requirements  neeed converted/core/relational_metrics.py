"""
Relational metrics for DHS.
compute_DH(i, j, state): Doppler–Hilbert factor from relational-only quantities, gated by R_cutoff and c·dt.
This implementation is deterministic and bounded; it does not alter physics or rendering.
"""
from typing import Any, Dict

EPS = 1e-12

def compute_DH(i: int, j: int, state: Dict[str, Any], R_cutoff: float, c_light: float, dt: float) -> float:
    """Compute Doppler–Hilbert factor D_H in [0,1] for pair (i,j).
    - Uses relational similarity s_ij and phase-rate proxy omega.
    - Applies neighborhood gates: d_ij <= R_cutoff and d_ij <= c_light * dt.
    - Returns 0.0 if outside gates.
    """
    particles = state.get("particles", {})
    pi = particles.get(i)
    pj = particles.get(j)
    if pi is None or pj is None:
        return 0.0
    # Pairwise separation (norm), origin-free relative distance
    d_ij = float(pi.get("dist_to", {}).get(j, float("inf")))
    if R_cutoff is not None and d_ij > R_cutoff:
        return 0.0
    if c_light is not None and dt is not None:
        if d_ij > c_light * dt:
            return 0.0
    # Similarity s_ij in [0,1]
    s_ij = float(pi.get("similarity_to", {}).get(j, 0.0))
    if s_ij < 0.0:
        s_ij = 0.0
    if s_ij > 1.0:
        s_ij = 1.0
    # Phase-rate proxies omega_i, omega_j in [0,1]
    h_i = float(pi.get("hamiltonian_norm", 0.0))
    h_j = float(pj.get("hamiltonian_norm", 0.0))
    omega_i = h_i / (h_i + EPS)
    omega_j = h_j / (h_j + EPS)
    r_omega = abs(omega_i - omega_j) / (abs(omega_i) + abs(omega_j) + EPS)
    D_H = r_omega * (1.0 - s_ij)
    if D_H < 0.0:
        D_H = 0.0
    if D_H > 1.0:
        D_H = 1.0
    return D_H
