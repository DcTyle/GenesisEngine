"""
9D state core for Genesis Engine.
Origin-free, deterministic, and constraint-only access.
"""
from dataclasses import dataclass, field
from typing import Dict, Any, Iterable, Tuple

@dataclass
class State9D:
    fields: Dict[str, Any] = field(default_factory=dict)
    particles: Dict[int, Dict[str, Any]] = field(default_factory=dict)

    def neighborhood(self, center_id: int, R_cutoff: float) -> Iterable[Tuple[int, float]]:
        """Yield (neighbor_id, distance) for neighbors within R_cutoff.
        Distances are origin-free (pairwise), sourced from particles[center_id]['dist_to'][neighbor_id].
        """
        center = self.particles.get(center_id, {})
        dist_map = center.get("dist_to", {})
        for nid, d in dist_map.items():
            try:
                dval = float(d)
            except Exception:
                continue
            if R_cutoff is None or dval <= R_cutoff:
                yield (nid, dval)

    def apply_delta(self, delta: Dict[str, Any]) -> None:
        """Apply a delta to fields; constraint-only updates.
        This function does not inject forces or alter camera/scale.
        """
        for k, v in delta.items():
            # Only allow constraint-labelled modifications
            if k.startswith("constraint:"):
                self.fields[k] = v
