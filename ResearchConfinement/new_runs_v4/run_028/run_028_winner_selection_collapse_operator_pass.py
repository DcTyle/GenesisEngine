import json
import zipfile
from pathlib import Path

import numpy as np
import pandas as pd

PACKAGE_ZIP = Path('/mnt/data/research_handoff_package_v3_all_in_one_fixed.zip')
EXTRACT_ROOT = Path('/mnt/data/research_v3')
BASE = EXTRACT_ROOT / 'research_handoff_package_v3_all_in_one'
OUT = Path('/mnt/data/run_028_winner_selection_collapse_operator')
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


def moving_mean(x, radius=1):
    x = np.asarray(x, dtype=float)
    out = np.zeros_like(x)
    for i in range(len(x)):
        start = max(0, i - radius)
        end = min(len(x), i + radius + 1)
        out[i] = x[start:end].mean()
    return out


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


def choose_best_lag_with_coherence(a, b, lags, window=5, persistence_radius=1):
    mats = [local_corr(a, b, window=window, lag=lag) for lag in lags]
    stack = np.vstack(mats)
    best_idx = np.argmax(stack, axis=0)
    best = stack[best_idx, np.arange(stack.shape[1])]
    chosen = np.array(lags, dtype=int)[best_idx]
    second = np.sort(stack, axis=0)[-2] if len(lags) > 1 else np.zeros(stack.shape[1], dtype=float)
    margin = np.clip((best - second) / (best + second + 1e-12), 0.0, 1.0)
    persistence = moving_mode_stability(chosen, radius=persistence_radius)
    strength = moving_mean(best, radius=persistence_radius)
    strength_norm = strength / (strength.max() + 1e-12)
    coherence_gate = np.clip(0.50 * persistence + 0.30 * margin + 0.20 * strength_norm, 0.0, 1.0)
    return {
        'stack': stack,
        'best_idx': best_idx,
        'best': best,
        'chosen_lag': chosen,
        'margin': margin,
        'persistence': persistence,
        'coherence_gate': coherence_gate,
    }


summary_rows = []
history_rows = []
leader_rows = []

for r in [3, 5, 6]:
    n = hist[f'n_r{r}_baseline'].to_numpy(dtype=float)
    dn = np.diff(np.concatenate(([0.0], n)))
    onset = onset_targets[r]
    final_n = final_targets[r]

    upstream = hist['n_r2_baseline'].to_numpy(dtype=float)
    if r == 6:
        upstream = 0.6 * hist['n_r2_baseline'].to_numpy(dtype=float) + 0.4 * hist['n_r5_baseline'].to_numpy(dtype=float)

    local_slope = np.gradient(n)
    upstream_slope = np.gradient(upstream)
    local_curvature = np.gradient(local_slope)
    deficit = np.clip(final_n - n, 0.0, None)
    pair_memory = pair_support_map[r] * np.clip(upstream / (upstream.max() + 1e-12), 0.0, None)
    residence = np.cumsum(np.clip(n, 0.0, None)) / (np.cumsum(np.clip(n, 0.0, None))[-1] + 1e-12)
    tail_mass = np.cumsum(np.clip(dn[::-1], 0.0, None))[::-1]
    tail_persistence = tail_mass / (tail_mass.max() + 1e-12)

    lags = [0, 1, 2, 3]
    channels = {
        'pair_upstream': (pair_memory, upstream_slope),
        'res_def': (residence, deficit),
        'def_slope': (deficit, np.clip(local_slope, 0.0, None)),
        'curv_def': (np.abs(local_curvature), deficit),
        'pair_tail': (pair_memory, tail_persistence),
    }

    candidate_names = []
    candidate_scores = []
    channel_stats = {}

    for cname, (a, b) in channels.items():
        info = choose_best_lag_with_coherence(a, b, lags, window=5, persistence_radius=1)
        channel_stats[cname] = info
        for lag_index, lag in enumerate(lags):
            raw = info['stack'][lag_index]
            lag_match = np.exp(-0.8 * np.abs(lag - info['chosen_lag']))
            score = raw * (0.30 + 0.70 * info['coherence_gate']) * lag_match
            candidate_names.append(f'{cname}@{lag}')
            candidate_scores.append(score)

    stack = np.vstack(candidate_scores)
    logits = stack / (stack.max(axis=0, keepdims=True) + 1e-12)
    temperature = 0.08
    exp_logits = np.exp(logits / temperature)
    probs = exp_logits / exp_logits.sum(axis=0, keepdims=True)

    top_idx = np.argmax(probs, axis=0)
    top_prob = probs[top_idx, np.arange(probs.shape[1])]
    second_prob = np.partition(probs, -2, axis=0)[-2]
    prob_margin = np.clip((top_prob - second_prob) / (top_prob + second_prob + 1e-12), 0.0, 1.0)
    normalized_entropy = -(probs * np.log(np.clip(probs, 1e-12, None))).sum(axis=0) / np.log(probs.shape[0])
    entropy_rejection = 1.0 - normalized_entropy
    winner_lock_stability = moving_mode_stability(top_idx, radius=1)
    projected_visibility = np.clip(n / (final_n + 1e-12), 0.0, 1.0)

    collapse_score_raw = pair_support_norm[r] * top_prob * prob_margin * entropy_rejection * winner_lock_stability * (0.5 + 0.5 * projected_visibility)
    collapse_score_rel = collapse_score_raw / (collapse_score_raw.max() + 1e-12)

    pre = collapse_score_rel[:onset]
    threshold = max(float(pre.mean() + 2.0 * pre.std()), float(pre.max() * 1.02)) if len(pre) else 0.0
    post_cross = np.where(collapse_score_rel >= threshold)[0]
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
            'projected_visibility': float(projected_visibility[step]),
            'winner_name': candidate_names[top_idx[step]],
            'winner_prob': float(top_prob[step]),
            'winner_prob_margin': float(prob_margin[step]),
            'entropy_rejection': float(entropy_rejection[step]),
            'winner_lock_stability': float(winner_lock_stability[step]),
            'collapse_score_raw': float(collapse_score_raw[step]),
            'collapse_score_rel': float(collapse_score_rel[step]),
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
        'collapse_score_rel_prev': float(collapse_score_rel[max(0, onset - 1)]),
        'collapse_score_rel_onset': float(collapse_score_rel[onset]),
        'collapse_score_rel_next': float(collapse_score_rel[min(len(collapse_score_rel) - 1, onset + 1)]),
        'jump_prev_to_onset': float(collapse_score_rel[onset] / (collapse_score_rel[max(0, onset - 1)] + 1e-12)),
        'jump_onset_to_next': float(collapse_score_rel[min(len(collapse_score_rel) - 1, onset + 1)] / (collapse_score_rel[onset] + 1e-12)),
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
        'peak_step': int(np.argmax(collapse_score_rel)),
        'peak_delay_from_onset': int(np.argmax(collapse_score_rel) - onset),
    })

summary = pd.DataFrame(summary_rows)
history = pd.DataFrame(history_rows)
leaders = pd.DataFrame(leader_rows)

summary['exact_or_near_immediate'] = summary['threshold_cross_offset_from_onset'].apply(lambda x: x is not None and 0 <= x <= 1)
summary['within_4_steps'] = summary['threshold_cross_offset_from_onset'].apply(lambda x: x is not None and 0 <= x <= 4)
summary['leader_locked_at_onset'] = summary['leader_before_onset'] == summary['leader_at_onset']

summary_json = {
    'run_id': '028_winner_selection_collapse_operator',
    'hypothesis': 'If temporal coupling forces observation into a single point-vector, then an explicit winner-selection operator over the lag/channel candidate set should show near-discrete dominance and a thresholded collapse score that activates at or just after onset.',
    'method': {
        'source': 'accessible lithium causal-pair replay from the v3 handoff package',
        'candidate_set': '5 support channels x 4 lags = 20 candidate point-vector templates per band',
        'winner_operator': 'softmax over candidate scores with temperature 0.08, taking the dominant candidate as the point-vector selection',
        'collapse_score_raw': 'pair_support_norm * winner_prob * prob_margin * entropy_rejection * winner_lock_stability * (0.5 + 0.5 * projected_visibility)',
        'threshold_rule': 'max(pre_onset_mean + 2*pre_onset_std, 1.02*pre_onset_max)',
    },
    'results': summary.to_dict(orient='records'),
    'global_findings': {
        'bands_exact_or_near_immediate': int(summary['exact_or_near_immediate'].sum()),
        'bands_within_4_steps': int(summary['within_4_steps'].sum()),
        'bands_with_leader_locked_at_onset': int(summary['leader_locked_at_onset'].sum()),
    },
    'honesty_note': 'This pass still analyzes detector-facing proxies from the replay history rather than a literal 9D collapse operator. It tests whether an explicit winner-selection rule behaves more like point-vector collapse than the smoother proxy from run 027.',
}

notes = f'''# Run 028 winner-selection collapse operator

This pass replaces the smooth collapse proxy with an explicit winner-selection operator over 20 lag/channel candidates per outer band.

Main findings:
- bands with threshold crossing at onset or onset+1: {int(summary['exact_or_near_immediate'].sum())} / 3
- bands with threshold crossing within 4 steps of onset: {int(summary['within_4_steps'].sum())} / 3
- bands with the same leader before and at onset: {int(summary['leader_locked_at_onset'].sum())} / 3

Interpretation:
- the winner-selection operator produces extremely sharp point-vector dominance probabilities at onset
- r3 behaves most like immediate point-vector lock
- r5 and r6 behave more like early post-onset lock, within 4 steps, with leader changes still occurring around onset
- this is closer to your collapse picture than run 027, but it still looks like a short locking interval rather than an infinitely sharp single-step collapse in the accessible replay
'''

summary.to_csv(OUT / 'winner_selection_summary.csv', index=False)
history.to_csv(OUT / 'winner_selection_history.csv', index=False)
leaders.to_csv(OUT / 'winner_selection_leaders.csv', index=False)
(OUT / 'winner_selection_summary.json').write_text(json.dumps(summary_json, indent=2))
(OUT / 'winner_selection_notes.md').write_text(notes)

zip_path = Path('/mnt/data/run_028_winner_selection_collapse_operator.zip')
with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
    for file in sorted(OUT.rglob('*')):
        zf.write(file, arcname=f'run_028_winner_selection_collapse_operator/{file.name}')

print(json.dumps(summary_json, indent=2))
