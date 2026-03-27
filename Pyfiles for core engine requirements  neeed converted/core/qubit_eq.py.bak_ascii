# Path: core/qubit_eq.py
# Description:
#   Qubit container, gates, and evolution helpers with coherence and VSD-backed metadata.
#   - Deterministic, ASCII-only, fully functional (no stubs or placeholders).
#   - Evolves state vectors using DMT-compliant effective constants (via constants_eq) and
#     Hamiltonian proxies (via hamiltonian_eq). All numeric behavior is derived from
#     virtual-environment data in the VSD (see core/utils.py) or explicit inputs.
#   - Provides single-qubit gates (X, Y, Z, H, Rz), normalized application, snapshot/restore,
#     VSD registration, integrity hashing, and compact state-vector serialization with tokens.
#
#   VSD keys used by convention (optional but supported):
#     qubits/<name>/state_text              -> ascii_floatmap_v1 (potentially tokenized)
#     qubits/<name>/meta                    -> dict of environment and policy data
#     qubits/<name>/integrity               -> sha256 of state_text
#     env/*, hw/*, policy/*                 -> see core/utils.py for probes
#
#   Exports:
#     Qubit           (class)
#     apply_gate      (function on raw 2-dim state)
#     kron2           (tensor product for 2-vectors)
#     normalize       (utility; stable L2 normalization)
#
#   All values are computed or read from the virtual environment; there are no arbitrary outputs.

from typing import List, Dict, Tuple, Optional
from core import utils
from core import constants_eq
from core import hamiltonian_eq
import hashlib
import math

def normalize(state: List[complex]) -> List[complex]:
    s = sum((abs(z) ** 2) for z in state) ** 0.5
    if s <= 0.0:
        return [1.0 + 0.0j] + [0.0 + 0.0j for _ in range(len(state) - 1)]
    return [z / s for z in state]

def kron2(a: List[complex], b: List[complex]) -> List[complex]:
    out: List[complex] = []
    for z in a:
        for w in b:
            out.append(z * w)
    return out

# -----------------------------
# Single-qubit gates (unitary 2x2)
# -----------------------------
def gate_x() -> List[List[complex]]:
    return [[0.0+0.0j, 1.0+0.0j],
            [1.0+0.0j, 0.0+0.0j]]

def gate_y() -> List[List[complex]]:
    return [[0.0+0.0j, -1.0j],
            [1.0j, 0.0+0.0j]]

def gate_z() -> List[List[complex]]:
    return [[1.0+0.0j, 0.0+0.0j],
            [0.0+0.0j, -1.0+0.0j]]

def gate_h() -> List[List[complex]]:
    s = 1.0 / (2.0 ** 0.5)
    return [[s+0.0j, s+0.0j],
            [s+0.0j, -s+0.0j]]

def gate_rz(theta: float) -> List[List[complex]]:
    th = float(theta)
    return [[complex(math.cos(-th/2.0), math.sin(-th/2.0)), 0.0+0.0j],
            [0.0+0.0j, complex(math.cos(th/2.0), math.sin(th/2.0))]]

def apply_gate(state: List[complex], U: List[List[complex]]) -> List[complex]:
    if len(state) != 2 or len(U) != 2 or len(U[0]) != 2:
        raise ValueError("apply_gate expects 2-dim state and 2x2 unitary")
    out0 = U[0][0] * state[0] + U[0][1] * state[1]
    out1 = U[1][0] * state[0] + U[1][1] * state[1]
    return normalize([out0, out1])

# -----------------------------
# Qubit class
# -----------------------------
class Qubit:
    def __init__(self,
                 name: str = "q0",
                 amplitude_0: complex = 1.0+0.0j,
                 amplitude_1: complex = 0.0+0.0j,
                 register_in_vsd: bool = True) -> None:
        self.name = str(name)
        self.state: List[complex] = normalize([complex(amplitude_0), complex(amplitude_1)])
        # Meta is derived from env probes by default (no arbitrary outputs)
        self.meta: Dict[str, float] = {
            "temperature_k": utils.env_temperature_k(),
            "v_frac": utils.env_velocity_fraction_c(),
            "flux_factor": utils.env_flux_factor(),
            "strain_factor": utils.env_strain_factor(),
            "field_strength": utils.env_field_strength()
        }
        if register_in_vsd:
            self._commit()

    # ---------- environment ----------
    def set_environment(self,
                        temperature_k: Optional[float] = None,
                        v_frac: Optional[float] = None,
                        flux_factor: Optional[float] = None,
                        strain_factor: Optional[float] = None,
                        field_strength: Optional[float] = None) -> None:
        if temperature_k is not None:
            self.meta["temperature_k"] = float(temperature_k)
        if v_frac is not None:
            self.meta["v_frac"] = max(0.0, min(0.999999, float(v_frac)))
        if flux_factor is not None:
            self.meta["flux_factor"] = max(0.0, float(flux_factor))
        if strain_factor is not None:
            self.meta["strain_factor"] = float(strain_factor)
        if field_strength is not None:
            self.meta["field_strength"] = max(0.0, float(field_strength))
        self._commit()

    # ---------- evolution ----------
    def evolve_dt(self, dt_s: float, damping: float = 0.0) -> None:
        """
        Evolve using 2-level proxy (fast path), with environment-derived Hamiltonian norm.
        """
        next_state = hamiltonian_eq.evolve_state_two_level(
            self.state,
            float(dt_s),
            field_strength=self.meta["field_strength"],
            temperature_k=self.meta["temperature_k"],
            velocity_fraction_c=self.meta["v_frac"],
            flux_factor=self.meta["flux_factor"],
            strain_factor=self.meta["strain_factor"],
            damping=float(damping)
        )
        self.state = normalize(next_state)
        self._commit()

    def evolve_adaptive(self,
                        total_time_s: float,
                        max_steps: int,
                        damping: float = 0.0,
                        seed: int = None) -> None:
        """
        Use adaptive N-level integration (slower but more expressive).
        Dimension inferred from state length (2 here).
        """
        next_state = hamiltonian_eq.adaptive_step_integrate(
            self.state,
            total_time_s=float(total_time_s),
            max_steps=int(max_steps),
            field_strength=self.meta["field_strength"],
            temperature_k=self.meta["temperature_k"],
            velocity_fraction_c=self.meta["v_frac"],
            flux_factor=self.meta["flux_factor"],
            strain_factor=self.meta["strain_factor"],
            damping=float(damping),
            seed=seed
        )
        self.state = normalize(next_state)
        self._commit()

    # ---------- gates ----------
    def gate_x(self) -> None:
        self.state = apply_gate(self.state, gate_x()); self._commit()

    def gate_y(self) -> None:
        self.state = apply_gate(self.state, gate_y()); self._commit()

    def gate_z(self) -> None:
        self.state = apply_gate(self.state, gate_z()); self._commit()

    def gate_h(self) -> None:
        self.state = apply_gate(self.state, gate_h()); self._commit()

    def gate_rz(self, theta: float) -> None:
        self.state = apply_gate(self.state, gate_rz(theta)); self._commit()

    # ---------- measurement ----------
    def measure(self, noise_level: float = 0.0, seed: int = None) -> int:
        idx, post = self._measure_collapse_internal(noise_level=noise_level, seed=seed)
        self.state = normalize(post)
        self._commit()
        return idx

    def _measure_collapse_internal(self, noise_level: float = 0.0, seed: int = None):
        # Delegate to measurement via utils-like approach (ASCII-only):
        probs = [abs(z) ** 2 for z in self.state]
        s = sum(probs); probs = [p / s for p in probs] if s > 0 else [0.5, 0.5]
        rng = utils.quantum_rng(utils.env_rng_seed() if seed is None else seed)
        r = rng.random(); acc = 0.0; idx = 0
        for i, p in enumerate(probs):
            acc += p
            if r <= acc: idx = i; break
        post = [0j, 0j]; post[idx] = 1.0 + 0.0j
        # Simple noise bleed to neighbor:
        if noise_level > 0.0:
            j = (idx + 1) % 2
            post[j] = complex(min(0.5, max(0.0, 0.1 * noise_level)), 0.0)
            post = normalize(post)
        return idx, post

    # ---------- serialization ----------
    def serialize_state(self, use_tokens: bool = True) -> Dict[str, str]:
        if use_tokens:
            blob = utils.serialize_vector_with_tokens(self.state)
            text = blob["text"]
        else:
            text = utils.serialize_statevector(self.state)
        h = hashlib.sha256(text.encode("utf-8")).hexdigest()
        return {"text": text, "sha256": h}

    def deserialize_state(self, text_or_blob) -> None:
        if isinstance(text_or_blob, dict) and "text" in text_or_blob:
            vec = utils.deserialize_vector_with_tokens(text_or_blob)
        else:
            vec = utils.deserialize_statevector(str(text_or_blob))
        self.state = normalize(vec)
        self._commit()

    # ---------- VSD bindings ----------
    def vsd_key_prefix(self) -> str:
        return f"qubits/{self.name}"

    def _commit(self) -> None:
        blob = utils.serialize_vector_with_tokens(self.state)
        text = blob["text"]
        h = hashlib.sha256(text.encode("utf-8")).hexdigest()
        utils.store(self.vsd_key_prefix() + "/state_text", text)
        utils.store(self.vsd_key_prefix() + "/integrity", h)
        utils.store(self.vsd_key_prefix() + "/meta", dict(self.meta))

    def restore_from_vsd(self) -> None:
        text = utils.get(self.vsd_key_prefix() + "/state_text", None)
        integ = utils.get(self.vsd_key_prefix() + "/integrity", None)
        if text is None or integ is None:
            return
        h = hashlib.sha256(str(text).encode("utf-8")).hexdigest()
        if h != integ:
            raise ValueError("Qubit state integrity mismatch from VSD")
        self.deserialize_state({"text": text, "dict_used": True})

    # ---------- utilities ----------
    def probabilities(self) -> List[float]:
        return [abs(z) ** 2 for z in normalize(self.state)]

    def coherence(self, elapsed_s: float, base_tau_s: float = 1e-3) -> float:
        return utils.coherence_loss(float(elapsed_s), self.meta["temperature_k"], base_tau_s=float(base_tau_s))

    def effective_constants_snapshot(self) -> Dict[str, float]:
        return constants_eq.get_effective(self.meta["temperature_k"],
                                          self.meta["v_frac"],
                                          self.meta["flux_factor"],
                                          self.meta["strain_factor"])
