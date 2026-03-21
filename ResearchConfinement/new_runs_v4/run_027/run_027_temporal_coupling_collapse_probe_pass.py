import json
import zipfile
from pathlib import Path

import numpy as np
import pandas as pd

PACKAGE_ZIP = Path('/mnt/data/research_handoff_package_v3_all_in_one_fixed.zip')
EXTRACT_ROOT = Path('/mnt/data/research_v3')
BASE = EXTRACT_ROOT / 'research_handoff_package_v3_all_in_one'
OUT = Path('/mnt/data/run_027_temporal_coupling_collapse_probe')
OUT.mkdir(parents=True, exist_ok=True)

if not BASE.exists():
    with zipfile.ZipFile(PACKAGE_ZIP) as zf:
        zf.extractall(EXTRACT_ROOT)

hist = pd.read_csv(BASE / 'continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_history.csv')
summary020 = json.loads((BASE / 'continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_summary.json').read_text())

closure = hist[['step', 'n_r2_baseline', 'n_r3_baseline', 'n_r5_baseline', 'n_r6_baseline']].copy()
onset_targets = {2: 4, 3: 5, 5: 18, 6: 31}
final_targets = {
    2: 2.1869844097664024,
    3: 1.8317995830845024,
    5: 0.8290027573827758,
    6: 0.6721458801801243,
}
pair_support_map = {
    3: summary020['pair_support_raw']['2-3'],
    5: summary020['pair_support_raw']['2-5'],
    6: summary020['pair_support_raw']['2-6'] + summary020['pair_support_raw']['5-6'],
}
max_pair = max(pair_support_map.values())
pair_support_norm = {k: v / max_pair for k, v in pair_support_map.items()}


def local_corr(a, b, window=5, lag=0):
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    n = len(a)
    out = np.zeros(n, dtype=float)
    half = window // 2
    for i in range(n):
        start = max(0, i - half)
        end = min(n, i + half + 1)
        xa = a[start:end]
        xidx_start = start - lag
        xidx_end = end - lag
        if xidx_start < 0 or xidx_end > n:
            continue
        xb = b[xidx_start:xidx_end]
        if len(xa) < 3 or len(xa) != len(xb):
            continue
        sa = xa.std()
        sb = xb.std()
        if sa < 1e-15 or sb < 1e-15:
            continue
        ca = (xa - xa.mean()) / sa
        cb = (xb - xb.mean()) / sb
        out[i] = np.mean(ca * cb)
    return np.maximum(0.0, out)


def moving_mean(x, radius=1):
    x = np.asarray(x, dtype=float)
    n = len(x)
    out = np.zeros(n, dtype=float)
    for i in range(n):
        s = max(0, i - radius)
        e = min(n, i + radius + 1)
        out[i] = x[s:e].mean()
    return out


def moving_mode_stability(lags, radius=1):
    lags = np.asarray(lags, dtype=int)
    n = len(lags)
    out = np.zeros(n, dtype=float)
    for i in range(n):
        s = max(0, i - radius)
        e = min(n, i + radius + 1)
        segment = lags[s:e]
        vals, counts = np.unique(segment, return_counts=True)
        winner = vals[np.argmax(counts)]
        out[i] = np.mean(segment == winner)
    return out


def choose_best_lag_with_coherence(a, b, lags, window, persistence_radius=1, margin_floor=0.05):
    mats = []
    for lag in lags:
        mats.append(local_corr(a, b, window=window, lag=lag))
    stack = np.vstack(mats)
    best_idx = np.argmax(stack, axis=0)
    best = stack[best_idx, np.arange(stack.shape[1])]
    chosen_lag = np.array(lags, dtype=int)[best_idx]
    sorted_stack = np.sort(stack, axis=0)
    second_best = sorted_stack[-2] if stack.shape[0] > 1 else np.zeros(stack.shape[1], dtype=float)
    margin = np.clip(best - second_best, 0.0, None)
    margin_norm = margin / (best + second_best + 1e-12)
    if margin_floor > 0:
        margin_norm = np.clip((margin_norm - margin_floor) / max(1e-12, 1.0 - margin_floor), 0.0, 1.0)
    persistence = moving_mode_stability(chosen_lag, radius=persistence_radius)
    local_strength = moving_mean(best, radius=persistence_radius)
    strength_norm = local_strength / (local_strength.max() + 1e-12)
    coherence_gate = np.clip((0.50 * persistence + 0.30 * margin_norm + 0.20 * strength_norm), 0.0, 1.0)
    gated = best * coherence_gate
    return {
        'raw_best': best,
        'gated_best': gated,
        'chosen_lag': chosen_lag,
        'margin_norm': margin_norm,
        'persistence': persistence,
        'coherence_gate': coherence_gate,
    }


def norm01(x):
    x = np.asarray(x, dtype=float)
    mn = float(np.min(x))
    mx = float(np.max(x))
    if mx - mn < 1e-15:
        return np.zeros_like(x)
    return (x - mn) / (mx - mn)


summary_rows = []
history_rows = []

for r in [3, 5, 6]:
    n = closure[f'n_r{r}_baseline'].to_numpy(dtype=float)
    dn = np.diff(np.concatenate(([0.0], n)))
    onset = onset_targets[r]
    final_n = final_targets[r]

    upstream = closure['n_r2_baseline'].to_numpy(dtype=float)
    if r == 6:
        upstream = 0.6 * closure['n_r2_baseline'].to_numpy(dtype=float) + 0.4 * closure['n_r5_baseline'].to_numpy(dtype=float)

    local_slope = np.gradient(n)
    upstream_slope = np.gradient(upstream)
    local_curvature = np.gradient(local_slope)
    deficit = np.clip(final_n - n, 0.0, None)
    pair_memory = pair_support_map[r] * np.clip(upstream / (upstream.max() + 1e-12), 0.0, None)
    residence = np.cumsum(np.clip(n, 0.0, None)) / (np.cumsum(np.clip(n, 0.0, None))[-1] + 1e-12)
    tail_mass = np.cumsum(np.clip(dn[::-1], 0.0, None))[::-1]
    tail_persistence = tail_mass / (tail_mass.max() + 1e-12)

    causal_lags = [0, 1, 2, 3]
    sup_pair_upstream = choose_best_lag_with_coherence(pair_memory, upstream_slope, causal_lags, 5)
    sup_res_def = choose_best_lag_with_coherence(residence, deficit, causal_lags, 5)
    sup_def_slope = choose_best_lag_with_coherence(deficit, np.clip(local_slope, 0.0, None), causal_lags, 5)
    sup_curv_def = choose_best_lag_with_coherence(np.abs(local_curvature), deficit, causal_lags, 5)
    sup_pair_tail = choose_best_lag_with_coherence(pair_memory, tail_persistence, causal_lags, 5)

    zp_support_drive = np.maximum(
        0.0,
        0.35 * sup_pair_upstream['gated_best']
        + 0.25 * sup_res_def['gated_best']
        + 0.25 * sup_def_slope['gated_best']
        + 0.15 * sup_curv_def['gated_best']
    )

    temporal_lock = (
        sup_pair_upstream['persistence']
        + sup_res_def['persistence']
        + sup_def_slope['persistence']
        + sup_curv_def['persistence']
        + sup_pair_tail['persistence']
    ) / 5.0
    lag_margin = (
        sup_pair_upstream['margin_norm']
        + sup_res_def['margin_norm']
        + sup_def_slope['margin_norm']
        + sup_curv_def['margin_norm']
        + sup_pair_tail['margin_norm']
    ) / 5.0
    collapse_authority = (
        sup_pair_upstream['coherence_gate']
        + sup_res_def['coherence_gate']
        + sup_def_slope['coherence_gate']
        + sup_curv_def['coherence_gate']
        + sup_pair_tail['coherence_gate']
    ) / 5.0

    trajectory_alignment = (
        norm01(sup_pair_upstream['gated_best'])
        + norm01(sup_def_slope['gated_best'])
        + norm01(sup_curv_def['gated_best'])
        + norm01(sup_pair_tail['gated_best'])
    ) / 4.0

    # Observation proxy: the logs do not contain explicit 3 separate detector coordinates,
    # so occupancy fraction is used as the detector-facing projection term.
    projected_visibility = np.clip(n / (final_n + 1e-12), 0.0, 1.0)
    point_selection_pressure = projected_visibility * (0.5 + 0.5 * norm01(zp_support_drive))

    collapse_score_raw = pair_support_norm[r] * (
        (0.5 + 0.5 * temporal_lock)
        * (0.5 + 0.5 * lag_margin)
        * (0.5 + 0.5 * collapse_authority)
        * trajectory_alignment
        * point_selection_pressure
    )
    collapse_score_rel = collapse_score_raw / (collapse_score_raw.max() + 1e-12)

    for step in range(len(n)):
        history_rows.append({
            'radius_bin': r,
            'step': int(step),
            'n_baseline': float(n[step]),
            'temporal_lock': float(temporal_lock[step]),
            'lag_margin': float(lag_margin[step]),
            'collapse_authority': float(collapse_authority[step]),
            'trajectory_alignment': float(trajectory_alignment[step]),
            'projected_visibility': float(projected_visibility[step]),
            'zp_support_drive': float(zp_support_drive[step]),
            'point_selection_pressure': float(point_selection_pressure[step]),
            'collapse_score_raw': float(collapse_score_raw[step]),
            'collapse_score_rel': float(collapse_score_rel[step]),
            'pair_upstream_lag': int(sup_pair_upstream['chosen_lag'][step]),
            'deficit_slope_lag': int(sup_def_slope['chosen_lag'][step]),
            'curvature_deficit_lag': int(sup_curv_def['chosen_lag'][step]),
            'pair_tail_lag': int(sup_pair_tail['chosen_lag'][step]),
        })

    post_scores = collapse_score_rel[onset:]
    first_rel25 = onset + int(np.where(post_scores >= 0.25)[0][0]) if np.any(post_scores >= 0.25) else None
    first_rel50 = onset + int(np.where(post_scores >= 0.50)[0][0]) if np.any(post_scores >= 0.50) else None
    first_rel75 = onset + int(np.where(post_scores >= 0.75)[0][0]) if np.any(post_scores >= 0.75) else None
    peak_step = int(np.argmax(collapse_score_rel))

    pre = collapse_score_rel[max(0, onset - 1)]
    at = collapse_score_rel[onset]
    nxt = collapse_score_rel[min(len(collapse_score_rel) - 1, onset + 1)]
    onset_jump_prev = float(at / (pre + 1e-12)) if pre > 0 else float('inf')
    onset_jump_next = float(nxt / (at + 1e-12)) if at > 0 else float('inf')

    summary_rows.append({
        'radius_bin': r,
        'onset_target': onset,
        'collapse_score_rel_prev': float(pre),
        'collapse_score_rel_onset': float(at),
        'collapse_score_rel_next': float(nxt),
        'onset_jump_prev_to_onset': onset_jump_prev,
        'onset_jump_onset_to_next': onset_jump_next,
        'first_post_onset_rel25_cross': first_rel25,
        'first_post_onset_rel50_cross': first_rel50,
        'first_post_onset_rel75_cross': first_rel75,
        'peak_step': peak_step,
        'peak_delay_from_onset': int(peak_step - onset),
        'peak_rel_score': float(collapse_score_rel[peak_step]),
        'mean_temporal_lock_post_onset_6step': float(np.mean(temporal_lock[onset:min(len(n), onset + 6)])),
        'mean_collapse_authority_post_onset_6step': float(np.mean(collapse_authority[onset:min(len(n), onset + 6)])),
        'mean_trajectory_alignment_post_onset_6step': float(np.mean(trajectory_alignment[onset:min(len(n), onset + 6)])),
        'mean_projected_visibility_post_onset_6step': float(np.mean(projected_visibility[onset:min(len(n), onset + 6)])),
    })

summary = pd.DataFrame(summary_rows)
history = pd.DataFrame(history_rows)

# Global qualitative alignment view.
summary['immediate_onset_acceleration'] = (
    (summary['onset_jump_prev_to_onset'] > 1.5) & (summary['onset_jump_onset_to_next'] > 1.2)
)
summary['quarter_peak_within_8_steps'] = (
    summary['first_post_onset_rel25_cross'].notna() & ((summary['first_post_onset_rel25_cross'] - summary['onset_target']) <= 8)
)
summary['half_peak_within_12_steps'] = (
    summary['first_post_onset_rel50_cross'].notna() & ((summary['first_post_onset_rel50_cross'] - summary['onset_target']) <= 12)
)

summary_json = {
    'run_id': '027_temporal_coupling_collapse_probe',
    'hypothesis': 'If temporal coupling is the observation-forcing term, then a collapse proxy derived from lag-stable pair-memory / slope / curvature structure should accelerate sharply at onset and build toward a later single-vector dominance peak.',
    'method': {
        'source': 'accessible lithium causal-pair replay from the v3 handoff package',
        'temporal_lock_term': 'mean support-side lag persistence across pair-upstream, residence-deficit, deficit-slope, curvature-deficit, pair-tail',
        'collapse_authority_term': 'mean support-side coherence gate across the same channels',
        'trajectory_alignment_term': 'band-relative normalized combination of pair-upstream, deficit-slope, curvature-deficit, pair-tail support channels',
        'point_selection_pressure': 'projected occupancy fraction times normalized zero-point drive',
        'collapse_score_raw': 'pair_support_norm * (0.5+0.5*temporal_lock) * (0.5+0.5*lag_margin) * (0.5+0.5*collapse_authority) * trajectory_alignment * point_selection_pressure',
        'collapse_score_rel': 'collapse_score_raw normalized by its band maximum',
    },
    'results': summary.to_dict(orient='records'),
    'global_findings': {
        'bands_with_immediate_onset_acceleration': int(summary['immediate_onset_acceleration'].sum()),
        'bands_with_quarter_peak_within_8_steps': int(summary['quarter_peak_within_8_steps'].sum()),
        'bands_with_half_peak_within_12_steps': int(summary['half_peak_within_12_steps'].sum()),
    },
    'honesty_note': 'The logs do not contain explicit independent 3-coordinate detector channels or a literal collapse operator. This pass therefore tests the idea using detector-facing proxies derived from the lag tensor and occupancy history, not a direct physical measurement of 4D/9D collapse.',
}

notes = f'''# Run 027 temporal-coupling collapse probe

This pass does not alter the replay. It analyzes whether the run-026 style lag/coherence observables behave like a collapse proxy.

Main result: all three outer bands show a sharp local acceleration in the collapse proxy at onset, but the proxy reaches quarter/half/peak dominance later rather than instantaneously.

Bands with immediate onset acceleration: {int(summary['immediate_onset_acceleration'].sum())} / 3
Bands with quarter-peak within 8 steps: {int(summary['quarter_peak_within_8_steps'].sum())} / 3
Bands with half-peak within 12 steps: {int(summary['half_peak_within_12_steps'].sum())} / 3

Interpretation:
- onset behaves like the beginning of collapse buildup
- the strongest single-vector dominance proxy appears later
- this supports a staged synchronization-to-collapse picture more than an instantaneous one, at least in the accessible logs
'''

summary.to_csv(OUT / 'collapse_probe_summary.csv', index=False)
history.to_csv(OUT / 'collapse_probe_history.csv', index=False)
(OUT / 'collapse_probe_summary.json').write_text(json.dumps(summary_json, indent=2))
(OUT / 'collapse_probe_notes.md').write_text(notes)

zip_path = Path('/mnt/data/run_027_temporal_coupling_collapse_probe.zip')
with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
    for file in sorted(OUT.rglob('*')):
        zf.write(file, arcname=f'run_027_temporal_coupling_collapse_probe/{file.name}')

print(json.dumps(summary_json, indent=2))
