"""Run 020: causal pair-support replay from the logged timing-law channels.

This pass continues from the handoff package state.
It does not fit a new global timing law.
It uses:
1) the existing local timing law as the appearance seed,
2) the logged pair channels to derive a pairwise timing correction, and
3) a causal replay that amplifies later-band growth using already-formed inner-band occupancy.

Honesty note:
This is still a toy / analogue replay built from the logged effective channels.
It is a continuity test of the package hypothesis, not validated real-world particle physics.
"""
from __future__ import annotations
import json
import os
from typing import Dict, Tuple

import numpy as np
import pandas as pd


def shift_series_earlier(dn_series: np.ndarray, delta_steps: float) -> np.ndarray:
    x = np.arange(len(dn_series), dtype=float)
    src = x + float(delta_steps)
    return np.interp(src, x, dn_series, left=dn_series[0], right=0.0)


def first_persist(steps: np.ndarray, arr: np.ndarray, threshold: float = 0.1) -> int | None:
    mask = arr >= threshold
    if not mask.any():
        return None
    return int(steps[np.argmax(mask)])


def main() -> None:
    # Update this root_dir if you copy the script elsewhere.
    root_dir = "/mnt/data/full_pkg/research_handoff_package"
    artifacts_dir = os.path.join(root_dir, "artifacts")
    out_dir = "/mnt/data/next_pass_outputs"
    os.makedirs(out_dir, exist_ok=True)

    pair = pd.read_csv(os.path.join(artifacts_dir, "lithium_abszero_timinglaw_pair_channels.csv"))
    hist = pd.read_csv(os.path.join(artifacts_dir, "lithium_abszero_timinglaw_replay_history.csv"))
    exposure = pd.read_csv(os.path.join(artifacts_dir, "lithium_abszero_effective_exposure_summary.csv")).set_index("radius_bin")

    pair["pair_support_raw"] = (
        (pair["Rel_ij"] ** 2)
        * pair["O_ij"]
        * pair["R_ij"]
        * pair["Cgate_ij"]
        * pair["Tpair_base_ij"]
        * pair["Tflux_ij"]
    )

    pair_support: Dict[Tuple[int, int], float] = {}
    for _, row in pair.iterrows():
        left_bin, right_bin = [int(x) for x in row["pair"].split("-")]
        pair_support[(left_bin, right_bin)] = float(row["pair_support_raw"])

    bands = [2, 3, 5, 6]
    t_local = {
        2: float(pair.loc[pair.pair == "2-3", "step_i_pred"].iloc[0]),
        3: float(pair.loc[pair.pair == "2-3", "step_j_pred"].iloc[0]),
        5: float(pair.loc[pair.pair == "2-5", "step_j_pred"].iloc[0]),
        6: float(pair.loc[pair.pair == "2-6", "step_j_pred"].iloc[0]),
    }
    rel_local = {b: float(exposure.loc[b, "Rel_i_calc_mean"]) for b in bands}
    corr_deficit = {b: 1.0 - rel_local[b] for b in bands}
    incoming_support = {b: sum(v for (i, j), v in pair_support.items() if j == b) for b in bands}
    delta_t = {
        b: (t_local[b] * corr_deficit[b] * incoming_support[b]) if b in [3, 5, 6] else 0.0
        for b in bands
    }
    t_corrected = {b: t_local[b] - delta_t[b] for b in bands}

    n0 = {b: hist[f"n_r{b}"].to_numpy() for b in bands}
    dn = {b: np.diff(np.concatenate([[0.0], n0[b]])) for b in bands}
    nfinal_base = {b: float(n0[b][-1]) for b in bands}
    steps = hist["step"].to_numpy()

    dn_shift = {b: shift_series_earlier(dn[b], delta_t[b]) for b in bands}

    n = {b: np.zeros_like(n0[b]) for b in bands}
    for b in bands:
        n[b][0] = dn_shift[b][0]

    for t in range(1, len(steps)):
        n[2][t] = n[2][t - 1] + dn_shift[2][t]

        support_3 = pair_support[(2, 3)] * (n[2][t - 1] / max(nfinal_base[2], 1e-12))
        n[3][t] = n[3][t - 1] + dn_shift[3][t] * (1.0 + support_3)

        support_5 = pair_support[(2, 5)] * (n[2][t - 1] / max(nfinal_base[2], 1e-12))
        n[5][t] = n[5][t - 1] + dn_shift[5][t] * (1.0 + support_5)

        support_6 = (
            pair_support[(2, 6)] * (n[2][t - 1] / max(nfinal_base[2], 1e-12))
            + pair_support[(5, 6)] * (n[5][t - 1] / max(nfinal_base[5], 1e-12))
        )
        n[6][t] = n[6][t - 1] + dn_shift[6][t] * (1.0 + support_6)

    history_out = pd.DataFrame(
        {
            "step": steps,
            "n_r2_baseline": n0[2],
            "n_r3_baseline": n0[3],
            "n_r5_baseline": n0[5],
            "n_r6_baseline": n0[6],
            "n_r2_corrected": n[2],
            "n_r3_corrected": n[3],
            "n_r5_corrected": n[5],
            "n_r6_corrected": n[6],
        }
    )
    history_path = os.path.join(out_dir, "lithium_abszero_causal_pair_replay_history.csv")
    history_out.to_csv(history_path, index=False)

    timing_rows = []
    for b in bands:
        timing_rows.append(
            {
                "radius_bin": b,
                "t_local": t_local[b],
                "Rel_i_calc_mean": rel_local[b],
                "correlation_deficit": corr_deficit[b],
                "incoming_pair_support": incoming_support[b],
                "delta_t_pair": delta_t[b],
                "t_corrected": t_corrected[b],
            }
        )
    timing_df = pd.DataFrame(timing_rows)
    timing_path = os.path.join(out_dir, "lithium_abszero_pairwise_timing_correction.csv")
    timing_df.to_csv(timing_path, index=False)

    summary = {
        "run_id": "020_causal_pair_replay",
        "hypothesis": (
            "The current local timing law is already mostly correct, and the remaining outer-band miss "
            "can be reduced by a causal pair-support accumulation replay built from logged pair channels "
            "without arbitrary shell scaling."
        ),
        "active_equations": {
            "local_timing_law": (
                "t_appear = 14.5729166667 + 1808.22336112*(omega_eff - 0.9957121838) "
                "- 275.99151246*(R_eff - 0.4425915037) + 1367.81476721*(C_eff - 0.3855790654) "
                "- 135161.28797655*(scale_eff_doppler - 1.0156430049)"
            ),
            "pair_support_raw": "S_ij = Rel_ij^2 * O_ij * R_ij * Cgate_ij * Tpair_base_ij * Tflux_ij",
            "pair_timing_correction": "delta_t_pair(j) = t_local(j) * (1 - Rel_i_calc(j)) * sum_{i<j} S_ij",
            "corrected_local_time": "t_corrected(j) = t_local(j) - delta_t_pair(j)",
            "causal_replay_update": (
                "n_j(t+1) = n_j(t) + dn_local_shifted_j(t+1) "
                "* (1 + sum_{i<j} S_ij * n_i(t) / n_i_final_baseline)"
            ),
        },
        "pair_support_raw": {f"{i}-{j}": v for (i, j), v in pair_support.items()},
        "timing_correction": {
            str(b): {"t_local": t_local[b], "delta_t_pair": delta_t[b], "t_corrected": t_corrected[b]}
            for b in bands
        },
        "baseline_metrics": {
            f"r{b}": {"first_persist_step_at_0p1": first_persist(steps, n0[b]), "final_n": float(n0[b][-1])}
            for b in bands
        },
        "corrected_metrics": {
            f"r{b}": {"first_persist_step_at_0p1": first_persist(steps, n[b]), "final_n": float(n[b][-1])}
            for b in bands
        },
        "key_deltas": {
            "r5_first_persist_shift_steps": first_persist(steps, n0[5]) - first_persist(steps, n[5]),
            "r6_first_persist_shift_steps": first_persist(steps, n0[6]) - first_persist(steps, n[6]),
            "r5_final_gain_fraction": float(n[5][-1] / n0[5][-1] - 1.0),
            "r6_final_gain_fraction": float(n[6][-1] / n0[6][-1] - 1.0),
            "r6_to_r5_ratio_baseline": float(n0[6][-1] / n0[5][-1]),
            "r6_to_r5_ratio_corrected": float(n[6][-1] / n[5][-1]),
        },
        "honesty_note": (
            "This is still a toy/analogue replay derived from logged effective channels and baseline replay "
            "occupancies. It is not validated particle physics and it does not prove a canonical closure."
        ),
    }
    summary_path = os.path.join(out_dir, "lithium_abszero_causal_pair_replay_summary.json")
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    print("Wrote:")
    print(history_path)
    print(timing_path)
    print(summary_path)


if __name__ == "__main__":
    main()
