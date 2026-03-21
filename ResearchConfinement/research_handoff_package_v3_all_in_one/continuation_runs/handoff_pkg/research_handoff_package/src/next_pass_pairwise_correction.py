from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Tuple

import pandas as pd


@dataclass(frozen=True)
class TimingLaw:
    intercept: float = 14.5729166667
    omega_coef: float = 1808.22336112
    omega_anchor: float = 0.9957121838
    r_coef: float = -275.99151246
    r_anchor: float = 0.4425915037
    c_coef: float = 1367.81476721
    c_anchor: float = 0.3855790654
    scale_doppler_coef: float = -135161.28797655
    scale_doppler_anchor: float = 1.0156430049

    def predict(self, omega_eff: float, r_eff: float, c_eff: float, scale_eff_doppler: float) -> float:
        return (
            self.intercept
            + self.omega_coef * (omega_eff - self.omega_anchor)
            + self.r_coef * (r_eff - self.r_anchor)
            + self.c_coef * (c_eff - self.c_anchor)
            + self.scale_doppler_coef * (scale_eff_doppler - self.scale_doppler_anchor)
        )


def derived_pair_support(row: pd.Series) -> float:
    """
    Derived pair support carried forward from the logged pair channels.

    This keeps only logged effective channels and the pair closure structure,
    omitting the unresolved shared numerator factor (1 + Tself_i + Tself_j).
    """
    return float(
        (row["Rel_ij"] ** 2)
        * row["O_ij"]
        * row["R_ij"]
        * row["Cgate_ij"]
        * row["Tpair_base_ij"]
        * row["Tflux_ij"]
    )


def inner_pair_time_advance(t_local: float, rel_i_calc: float, inner_pair_support: float) -> float:
    """
    Pairwise effective placement/timing correction.

    Interpretation:
    - t_local is the working appearance time from the local timing law.
    - (1 - Rel_i_calc) is the local correlation deficit relative to the calibrated basin.
    - inner_pair_support is accumulated only from smaller-radius active support channels.

    The correction is therefore derived from:
        local timing x local correlation deficit x logged pair support

    with no arbitrary shell-scaling term.
    """
    rel_deficit = max(0.0, 1.0 - float(rel_i_calc))
    return float(t_local * rel_deficit * inner_pair_support)


def run(base_dir: Path) -> Dict[int, Dict[str, float]]:
    artifacts_dir = base_dir / "artifacts"
    summary = pd.read_csv(artifacts_dir / "lithium_abszero_effective_exposure_summary.csv").set_index("radius_bin")
    pairs = pd.read_csv(artifacts_dir / "lithium_abszero_timinglaw_pair_channels.csv")

    timing = TimingLaw()

    pair_support: Dict[Tuple[int, int], float] = {}
    for _, row in pairs.iterrows():
        i_str, j_str = str(row["pair"]).split("-")
        i = int(i_str)
        j = int(j_str)
        score = derived_pair_support(row)
        pair_support[(i, j)] = score
        pair_support[(j, i)] = score

    records = []
    result: Dict[int, Dict[str, float]] = {}
    for radius_bin in sorted(int(x) for x in summary.index.tolist()):
        row = summary.loc[radius_bin]
        t_local = timing.predict(
            omega_eff=float(row["omega_eff_mean"]),
            r_eff=float(row["R_eff_mean"]),
            c_eff=float(row["C_eff_mean"]),
            scale_eff_doppler=float(row["scale_eff_doppler_mean"]),
        )
        inner_bins = [other for other in sorted(int(x) for x in summary.index.tolist()) if other < radius_bin]
        inner_support = sum(pair_support.get((other, radius_bin), 0.0) for other in inner_bins)
        delta_t = inner_pair_time_advance(
            t_local=t_local,
            rel_i_calc=float(row["Rel_i_calc_mean"]),
            inner_pair_support=inner_support,
        )
        t_corrected = t_local - delta_t
        payload = {
            "t_local_pred": float(t_local),
            "inner_pair_support": float(inner_support),
            "rel_deficit": float(max(0.0, 1.0 - float(row["Rel_i_calc_mean"]))),
            "delta_t_pair_corr": float(delta_t),
            "t_pair_corrected": float(t_corrected),
        }
        result[radius_bin] = payload
        records.append({"radius_bin": radius_bin, **payload})

    pd.DataFrame(records).to_csv(artifacts_dir / "lithium_abszero_pairwise_correction_test.csv", index=False)
    (artifacts_dir / "lithium_abszero_pairwise_correction_test.json").write_text(json.dumps(result, indent=2))
    return result


if __name__ == "__main__":
    here = Path(__file__).resolve().parents[1]
    output = run(here)
    print(json.dumps(output, indent=2))
