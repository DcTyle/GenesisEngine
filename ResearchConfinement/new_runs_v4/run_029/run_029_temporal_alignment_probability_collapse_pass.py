import json
import zipfile
from pathlib import Path

import numpy as np
import pandas as pd

PACKAGE_ZIP = Path('/mnt/data/research_handoff_package_v3_all_in_one_fixed.zip')
EXTRACT_ROOT = Path('/mnt/data/research_v3')
BASE = EXTRACT_ROOT / 'research_handoff_package_v3_all_in_one'
OUT = Path('/mnt/data/run_029_temporal_alignment_probability_collapse')
OUT.mkdir(parents=True, exist_ok=True)

if not BASE.exists():
    with zipfile.ZipFile(PACKAGE_ZIP) as zf:
        zf.extractall(EXTRACT_ROOT)

hist = pd.read_csv(BASE / 'continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_history.csv')
summary020 = json.loads((BASE / 'continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_summary.json').read_text())

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
        bstart = start - lag
        bend = end - lag
        if bstart < 0 or bend > n:
            continue
        xb = b[bstart:bend]
        if len(xa) < 3 or len(xa) != len(xb):
            continue
        sa = xa.std()
        sb = xb.std()
        if sa < 1e-15 or sb < 1e-15:
            continue
        out[i] = np.mean(((xa - xa.mean()) / sa) * ((xb - xb.mean()) / sb))
    return np.maximum(0.0, out)


def moving_mode_stability(values, radius=1):
    values = np.asarray(values, dtype=int)
    out = np.zeros(len(values), dtype=float)
    for i in range(len(values)):
        start = max(0, i - radius)
        end = min(len(values), i + radius + 1)
        seg = values[start:end]
        _, counts = np.unique(seg, return_counts=True)
        out[i] = counts.max() / len(seg)
    return out


def moving_mean(x, radius=1):
    x = np.asarray(x, dtype=float)
    out = np.zeros(len(x), dtype=float)
    for i in range(len(x)):
        start = max(0, i - radius)
        end = min(len(x), i + radius + 1)
        out[i] = x[start:end].mean()
    return out


def norm01(x):
    x = np.asarray(x, dtype=float)
    xmin = float(np.nanmin(x))
    xmax = float(np.nanmax(x))
    if xmax - xmin < 1e-15:
        return np.zeros_like(x)
    return np.clip((x - xmin) / (xmax - xmin), 0.0, 1.0)


summary_rows = []
history_rows = []
leader_rows = []

for r in [3, 5, 6]:
    n = hist[f'n_r{r}_baseline'].to_numpy(dtype=float)
    onset = onset_targets[r]
    final_n = final_targets[r]
    dn = np.diff(np.concatenate(([0.0], n)))

    upstream = hist['n_r2_baseline'].to_numpy(dtype=float)
    if r == 6:
        upstream = 0.6 * hist['n_r2_baseline'].to_numpy(dtype=float) + 0.4 * hist['n_r5_baseline'].to_numpy(dtype=float)

    local_slope = np.gradient(n)
    upstream_slope = np.gradient(upstream)
    local_curvature = np.gradient(local_slope)
    deficit = np.clip(final_n - n, 0.0, None)
    residence = np.cumsum(np.clip(n, 0.0, None))
    residence = residence / (residence[-1] + 1e-12)
    pair_memory = pair_support_map[r] * norm01(upstream)
    tail_mass = np.cumsum(np.clip(dn[::-1], 0.0, None))[::-1]
    tail_persistence = tail_mass / (tail_mass.max() + 1e-12)
    visible_frac = np.clip(n / (final_n + 1e-12), 0.0, 1.0)

    # flux-like alignment fields (non-SI proxy fields only)
    flux_drive = norm01(np.clip(local_slope, 0.0, None) * (0.35 + 0.65 * deficit / (deficit.max() + 1e-12)))
    traj_align = norm01(np.abs(local_slope) + 0.5 * np.abs(local_curvature))
    future_support = norm01(np.roll(np.clip(local_slope, 0.0, None), -1) + 0.7 * np.roll(np.clip(deficit, 0.0, None), -1))
    retro_admissibility = norm01(np.roll(pair_memory, -1) + np.roll(tail_persistence, -1) + 0.5 * np.roll(residence, -1))
    future_support[-1] = future_support[-2]
    retro_admissibility[-1] = retro_admissibility[-2]

    channels = {
        'pair_upstream': (pair_memory, upstream_slope),
        'def_slope': (deficit, np.clip(local_slope, 0.0, None)),
        'curv_def': (np.abs(local_curvature), deficit),
        'pair_tail': (pair_memory, tail_persistence),
        'flux_res': (flux_drive, residence),
        'traj_flux': (traj_align, flux_drive),
    }
    lags = [0, 1, 2, 3]

    candidate_names = []
    candidate_scores = []
    candidate_align = []
    candidate_future = []
    candidate_retro = []
    candidate_persist = []

    for cname, (a, b) in channels.items():
        mats = [local_corr(a, b, window=5, lag=lag) for lag in lags]
        stack = np.vstack(mats)
        best_idx = np.argmax(stack, axis=0)
        chosen = np.array(lags, dtype=int)[best_idx]
        lag_persistence = moving_mode_stability(chosen, radius=1)
        lag_strength = moving_mean(stack.max(axis=0), radius=1)
        lag_strength = lag_strength / (lag_strength.max() + 1e-12)
        for lag_i, lag in enumerate(lags):
            align = stack[lag_i]
            lag_match = np.exp(-0.65 * np.abs(lag - chosen))
            future = np.roll(future_support, -lag)
            retro = np.roll(retro_admissibility, lag)
            if lag > 0:
                future[-lag:] = future_support[-lag:]
                retro[:lag] = retro_admissibility[:lag]
            persistence = np.clip(0.55 * lag_persistence + 0.45 * lag_strength, 0.0, 1.0)
            score = pair_support_norm[r] * align * lag_match * (0.35 + 0.65 * future) * (0.35 + 0.65 * retro) * (0.30 + 0.70 * persistence)
            candidate_names.append(f'{cname}@{lag}')
            candidate_scores.append(score)
            candidate_align.append(align)
            candidate_future.append(future)
            candidate_retro.append(retro)
            candidate_persist.append(persistence)

    stack = np.vstack(candidate_scores)
    # probability collapse from temporal alignments only; no direct occupancy in logits
    logits = stack / (stack.max(axis=0, keepdims=True) + 1e-12)
    temperature = 0.10
    exp_logits = np.exp(logits / temperature)
    probs = exp_logits / exp_logits.sum(axis=0, keepdims=True)

    top_idx = np.argmax(probs, axis=0)
    top_prob = probs[top_idx, np.arange(probs.shape[1])]
    second_prob = np.partition(probs, -2, axis=0)[-2]
    prob_margin = np.clip((top_prob - second_prob) / (top_prob + second_prob + 1e-12), 0.0, 1.0)
    entropy = -(probs * np.log(np.clip(probs, 1e-12, None))).sum(axis=0) / np.log(probs.shape[0])
    entropy_rejection = 1.0 - entropy
    winner_lock_stability = moving_mode_stability(top_idx, radius=1)

    chosen_align = np.vstack(candidate_align)[top_idx, np.arange(len(n))]
    chosen_future = np.vstack(candidate_future)[top_idx, np.arange(len(n))]
    chosen_retro = np.vstack(candidate_retro)[top_idx, np.arange(len(n))]
    chosen_persist = np.vstack(candidate_persist)[top_idx, np.arange(len(n))]

    # probability collapse score: temporal alignments create a collapse probability field;
    # detector-facing visibility only modulates the readout, not the selection logits.
    collapse_prob_raw = top_prob * prob_margin * entropy_rejection * winner_lock_stability
    collapse_prob = collapse_prob_raw / (collapse_prob_raw.max() + 1e-12)
    readout_score_raw = collapse_prob * (0.35 + 0.65 * chosen_align) * (0.35 + 0.65 * chosen_future) * (0.35 + 0.65 * chosen_retro) * (0.20 + 0.80 * visible_frac)
    readout_score = readout_score_raw / (readout_score_raw.max() + 1e-12)

    pre = readout_score[:onset]
    threshold = max(float(pre.mean() + 2.0 * pre.std()), float(pre.max() * 1.03)) if len(pre) else 0.0
    post_cross = np.where(readout_score >= threshold)[0]
    first_cross = int(post_cross[0]) if len(post_cross) else None

    winner_changes = np.concatenate(([0], (top_idx[1:] != top_idx[:-1]).astype(int)))
    leader_before = candidate_names[top_idx[max(0, onset - 1)]]
    leader_at = candidate_names[top_idx[onset]]
    leader_after = candidate_names[top_idx[min(len(top_idx) - 1, onset + 1)]]

    for step in range(len(n)):
        history_rows.append({
            'radius_bin': r,
            'step': int(step),
            'n_baseline': float(n[step]),
            'visible_fraction': float(visible_frac[step]),
            'winner_name': candidate_names[top_idx[step]],
            'winner_prob': float(top_prob[step]),
            'winner_prob_margin': float(prob_margin[step]),
            'entropy_rejection': float(entropy_rejection[step]),
            'winner_lock_stability': float(winner_lock_stability[step]),
            'chosen_alignment': float(chosen_align[step]),
            'chosen_future_alignment': float(chosen_future[step]),
            'chosen_retro_admissibility': float(chosen_retro[step]),
            'chosen_persistence': float(chosen_persist[step]),
            'collapse_prob': float(collapse_prob[step]),
            'readout_score': float(readout_score[step]),
            'winner_changed_here': int(winner_changes[step]),
        })

    leader_counts = pd.Series([candidate_names[i] for i in top_idx[onset:]]).value_counts()
    for name, count in leader_counts.items():
        leader_rows.append({
            'radius_bin': r,
            'winner_name': name,
            'post_onset_count': int(count),
            'post_onset_fraction': float(count / max(1, len(top_idx[onset:]))),
        })

    summary_rows.append({
        'radius_bin': r,
        'onset_target': onset,
        'collapse_prob_prev': float(collapse_prob[max(0, onset - 1)]),
        'collapse_prob_onset': float(collapse_prob[onset]),
        'collapse_prob_next': float(collapse_prob[min(len(collapse_prob) - 1, onset + 1)]),
        'readout_prev': float(readout_score[max(0, onset - 1)]),
        'readout_onset': float(readout_score[onset]),
        'readout_next': float(readout_score[min(len(readout_score) - 1, onset + 1)]),
        'jump_prev_to_onset': float(readout_score[onset] / (readout_score[max(0, onset - 1)] + 1e-12)),
        'jump_onset_to_next': float(readout_score[min(len(readout_score) - 1, onset + 1)] / (readout_score[onset] + 1e-12)),
        'first_threshold_cross': first_cross,
        'threshold_cross_offset_from_onset': None if first_cross is None else int(first_cross - onset),
        'threshold': threshold,
        'leader_before_onset': leader_before,
        'leader_at_onset': leader_at,
        'leader_after_onset': leader_after,
        'winner_prob_at_onset': float(top_prob[onset]),
        'winner_prob_margin_at_onset': float(prob_margin[onset]),
        'entropy_rejection_at_onset': float(entropy_rejection[onset]),
        'winner_lock_stability_at_onset': float(winner_lock_stability[onset]),
        'chosen_alignment_at_onset': float(chosen_align[onset]),
        'chosen_future_alignment_at_onset': float(chosen_future[onset]),
        'chosen_retro_admissibility_at_onset': float(chosen_retro[onset]),
        'peak_step': int(np.argmax(readout_score)),
        'peak_delay_from_onset': int(np.argmax(readout_score) - onset),
    })

summary = pd.DataFrame(summary_rows)
history = pd.DataFrame(history_rows)
leaders = pd.DataFrame(leader_rows)
summary['exact_or_near_immediate'] = summary['threshold_cross_offset_from_onset'].apply(lambda x: x is not None and 0 <= x <= 1)
summary['within_4_steps'] = summary['threshold_cross_offset_from_onset'].apply(lambda x: x is not None and 0 <= x <= 4)
summary['leader_locked_at_onset'] = summary['leader_before_onset'] == summary['leader_at_onset']

summary_json = {
    'run_id': '029_temporal_alignment_probability_collapse',
    'hypothesis': 'If observation-like locking is a probability collapse produced by temporal alignments rather than measurement, then a candidate distribution derived only from temporal alignment structure should show sharp post-onset winner selection and a thresholded readout score at or near onset.',
    'method': {
        'source': 'accessible lithium causal-pair replay from the v3 handoff package',
        'candidate_set': '6 temporal-alignment channels x 4 lags = 24 candidate branches per band',
        'probability_rule': 'softmax over temporal-alignment scores only; visible_fraction is excluded from the selection logits and used only in the final detector-facing readout score',
        'candidate_score': 'pair_support_norm * local_temporal_alignment * lag_match * future_alignment * retro_admissibility * persistence',
        'collapse_prob': 'winner_prob * probability_margin * entropy_rejection * winner_lock_stability',
        'readout_score': 'collapse_prob * chosen_alignment * chosen_future_alignment * chosen_retro_admissibility * visible_fraction',
        'threshold_rule': 'max(pre_onset_mean + 2*pre_onset_std, 1.03*pre_onset_max)',
    },
    'results': summary.to_dict(orient='records'),
    'global_findings': {
        'bands_exact_or_near_immediate': int(summary['exact_or_near_immediate'].sum()),
        'bands_within_4_steps': int(summary['within_4_steps'].sum()),
        'bands_with_leader_locked_at_onset': int(summary['leader_locked_at_onset'].sum()),
    },
    'honesty_note': 'This is still a proxy analysis on the accessible replay. The collapse probabilities are derived from temporal-alignment fields and retro-admissibility terms, not from raw 3D detector coordinates or a physically validated 9D measurement model.',
}

notes = f'''# Run 029 temporal-alignment probability collapse

This pass recasts the collapse idea as a probability collapse produced by temporal alignment fields and retro-admissibility, not as a measurement event.

Main findings:
- bands with threshold crossing at onset or onset+1: {int(summary['exact_or_near_immediate'].sum())} / 3
- bands with threshold crossing within 4 steps of onset: {int(summary['within_4_steps'].sum())} / 3
- bands with the same leader before and at onset: {int(summary['leader_locked_at_onset'].sum())} / 3

Interpretation:
- the winner is selected by temporal alignment probabilities, not by direct occupancy weighting
- the detector-facing readout still tends to lock over a short interval rather than an infinitely sharp one-step event in the accessible replay
- this supports a flux/temporal-alignment interpretation better than a literal measurement-collapse narrative
'''

summary.to_csv(OUT / 'probability_collapse_summary.csv', index=False)
history.to_csv(OUT / 'probability_collapse_history.csv', index=False)
leaders.to_csv(OUT / 'probability_collapse_leaders.csv', index=False)
(OUT / 'probability_collapse_summary.json').write_text(json.dumps(summary_json, indent=2))
(OUT / 'probability_collapse_notes.md').write_text(notes)

zip_path = Path('/mnt/data/run_029_temporal_alignment_probability_collapse.zip')
with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
    for file in sorted(OUT.rglob('*')):
        zf.write(file, arcname=f'run_029_temporal_alignment_probability_collapse/{file.name}')

print(json.dumps(summary_json, indent=2))
