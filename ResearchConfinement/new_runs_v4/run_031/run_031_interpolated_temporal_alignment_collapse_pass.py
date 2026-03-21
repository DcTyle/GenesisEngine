import json
import zipfile
from pathlib import Path

import numpy as np
import pandas as pd

PACKAGE_ZIP = Path('/mnt/data/research_handoff_package_v3_all_in_one_fixed.zip')
EXTRACT_ROOT = Path('/mnt/data/research_v3')
BASE = EXTRACT_ROOT / 'research_handoff_package_v3_all_in_one'
OUT = Path('/mnt/data/run_031_interpolated_temporal_alignment_collapse')
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
candidate_rows = []

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
    candidate_lags = []
    candidate_channels = []

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
            candidate_lags.append(lag)
            candidate_channels.append(cname)

    score_stack = np.vstack(candidate_scores)
    logits = score_stack / (score_stack.max(axis=0, keepdims=True) + 1e-12)
    temperature = 0.10
    probs = np.exp(logits / temperature)
    probs = probs / probs.sum(axis=0, keepdims=True)

    entropy = -(probs * np.log(np.clip(probs, 1e-12, None))).sum(axis=0) / np.log(probs.shape[0])
    entropy_rejection = 1.0 - entropy
    top2_idx = np.argsort(probs, axis=0)[-2:]
    idx1 = top2_idx[-1]
    idx2 = top2_idx[-2]
    p1 = probs[idx1, np.arange(probs.shape[1])]
    p2 = probs[idx2, np.arange(probs.shape[1])]

    chosen_align1 = np.vstack(candidate_align)[idx1, np.arange(len(n))]
    chosen_align2 = np.vstack(candidate_align)[idx2, np.arange(len(n))]
    chosen_future1 = np.vstack(candidate_future)[idx1, np.arange(len(n))]
    chosen_future2 = np.vstack(candidate_future)[idx2, np.arange(len(n))]
    chosen_retro1 = np.vstack(candidate_retro)[idx1, np.arange(len(n))]
    chosen_retro2 = np.vstack(candidate_retro)[idx2, np.arange(len(n))]
    chosen_persist1 = np.vstack(candidate_persist)[idx1, np.arange(len(n))]
    chosen_persist2 = np.vstack(candidate_persist)[idx2, np.arange(len(n))]

    lag1 = np.array(candidate_lags)[idx1]
    lag2 = np.array(candidate_lags)[idx2]
    channel1 = np.array(candidate_channels, dtype=object)[idx1]
    channel2 = np.array(candidate_channels, dtype=object)[idx2]
    name1 = np.array(candidate_names, dtype=object)[idx1]
    name2 = np.array(candidate_names, dtype=object)[idx2]

    # Coherent two-branch condition: both branches remain probable and the distribution is not fully singular yet.
    dual_branch_coherence = np.clip((p1 + p2) * entropy_rejection, 0.0, 1.0)
    coherence_climb = np.clip(np.gradient(norm01((p1 + p2) * entropy_rejection)), 0.0, None)
    coherence_climb = norm01(coherence_climb)

    # Internal consistency falls when branch geometry disagrees: lag mismatch, channel mismatch, and field mismatch.
    field_disagreement = np.clip(
        0.30 * np.abs(chosen_align1 - chosen_align2)
        + 0.25 * np.abs(chosen_future1 - chosen_future2)
        + 0.20 * np.abs(chosen_retro1 - chosen_retro2)
        + 0.15 * np.abs(chosen_persist1 - chosen_persist2)
        + 0.10 * np.abs(np.clip(local_curvature, 0.0, None) - np.roll(np.clip(local_curvature, 0.0, None), lag2 - lag1)),
        0.0,
        None,
    )
    lag_mismatch = np.abs(lag1 - lag2) / max(lags)
    channel_mismatch = (channel1 != channel2).astype(float)
    internal_inconsistency = np.clip(0.45 * field_disagreement + 0.35 * lag_mismatch + 0.20 * channel_mismatch, 0.0, 1.0)

    # Collapse trigger: coherence rises while internal consistency is poor; this forces interpolation.
    interpolation_trigger = dual_branch_coherence * coherence_climb * internal_inconsistency
    interpolation_trigger = norm01(interpolation_trigger)

    # Effective weights include coherence but penalize inconsistent branches.
    w1 = p1 * (0.35 + 0.65 * chosen_persist1) * (1.0 - 0.45 * internal_inconsistency)
    w2 = p2 * (0.35 + 0.65 * chosen_persist2) * (1.0 - 0.45 * internal_inconsistency)
    wsum = w1 + w2 + 1e-12
    w1n = w1 / wsum
    w2n = w2 / wsum

    # Interpolated event location in sub-step units: base step shifted by the weighted lag average.
    base_event = np.arange(len(n), dtype=float)
    effective_lag = w1n * lag1 + w2n * lag2
    interp_event = base_event + effective_lag

    # Internal-structure admissibility penalty: do not let pure midpoint soup win if structure resists it.
    interp_alignment = w1n * chosen_align1 + w2n * chosen_align2
    interp_future = w1n * chosen_future1 + w2n * chosen_future2
    interp_retro = w1n * chosen_retro1 + w2n * chosen_retro2
    interp_persist = w1n * chosen_persist1 + w2n * chosen_persist2
    structural_admissibility = np.clip(
        (0.30 + 0.70 * interp_alignment)
        * (0.30 + 0.70 * interp_future)
        * (0.30 + 0.70 * interp_retro)
        * (0.20 + 0.80 * interp_persist)
        * (1.0 - 0.70 * internal_inconsistency),
        0.0,
        1.0,
    )

    # Interpolated collapse probability and detector-facing readout.
    collapse_prob = interpolation_trigger * structural_admissibility
    collapse_prob = norm01(collapse_prob)
    readout_score = collapse_prob * (0.20 + 0.80 * visible_frac)
    readout_score = norm01(readout_score)

    # Fractional onset estimate from threshold crossing with linear interpolation.
    pre = readout_score[:onset]
    threshold = max(float(pre.mean() + 2.0 * pre.std()), float(pre.max() * 1.03)) if len(pre) else 0.0
    crossing_idx = np.where(readout_score >= threshold)[0]
    if len(crossing_idx):
        first_cross = int(crossing_idx[0])
        if first_cross > 0 and readout_score[first_cross] != readout_score[first_cross - 1]:
            frac = (threshold - readout_score[first_cross - 1]) / (readout_score[first_cross] - readout_score[first_cross - 1] + 1e-12)
            frac = float(np.clip(frac, 0.0, 1.0))
            fractional_cross = (first_cross - 1) + frac
        else:
            fractional_cross = float(first_cross)
    else:
        first_cross = None
        fractional_cross = None

    # Interpolated collapse event near onset from the step with strongest trigger in onset neighborhood.
    win_start = max(0, onset - 1)
    win_end = min(len(n), onset + 3)
    local_peak_rel = int(np.argmax(collapse_prob[win_start:win_end]))
    local_peak_step = win_start + local_peak_rel
    local_interp_event = float(interp_event[local_peak_step])

    for i, cname in enumerate(candidate_names):
        candidate_rows.append({
            'radius_bin': r,
            'candidate_name': cname,
            'channel': candidate_channels[i],
            'lag': int(candidate_lags[i]),
            'mean_probability': float(probs[i].mean()),
            'onset_probability': float(probs[i, onset]),
        })

    for step in range(len(n)):
        history_rows.append({
            'radius_bin': r,
            'step': int(step),
            'n_baseline': float(n[step]),
            'visible_fraction': float(visible_frac[step]),
            'top1_name': str(name1[step]),
            'top2_name': str(name2[step]),
            'top1_prob': float(p1[step]),
            'top2_prob': float(p2[step]),
            'dual_branch_coherence': float(dual_branch_coherence[step]),
            'coherence_climb': float(coherence_climb[step]),
            'internal_inconsistency': float(internal_inconsistency[step]),
            'interpolation_trigger': float(interpolation_trigger[step]),
            'effective_lag': float(effective_lag[step]),
            'interpolated_event': float(interp_event[step]),
            'structural_admissibility': float(structural_admissibility[step]),
            'collapse_prob': float(collapse_prob[step]),
            'readout_score': float(readout_score[step]),
        })

    summary_rows.append({
        'radius_bin': r,
        'onset_target': onset,
        'top1_before': str(name1[max(0, onset - 1)]),
        'top2_before': str(name2[max(0, onset - 1)]),
        'top1_at': str(name1[onset]),
        'top2_at': str(name2[onset]),
        'dual_branch_coherence_at_onset': float(dual_branch_coherence[onset]),
        'coherence_climb_at_onset': float(coherence_climb[onset]),
        'internal_inconsistency_at_onset': float(internal_inconsistency[onset]),
        'interpolation_trigger_at_onset': float(interpolation_trigger[onset]),
        'effective_lag_at_onset': float(effective_lag[onset]),
        'interpolated_event_at_onset': float(interp_event[onset]),
        'collapse_prob_prev': float(collapse_prob[max(0, onset - 1)]),
        'collapse_prob_onset': float(collapse_prob[onset]),
        'collapse_prob_next': float(collapse_prob[min(len(collapse_prob) - 1, onset + 1)]),
        'readout_prev': float(readout_score[max(0, onset - 1)]),
        'readout_onset': float(readout_score[onset]),
        'readout_next': float(readout_score[min(len(readout_score) - 1, onset + 1)]),
        'jump_prev_to_onset': float(readout_score[onset] / (readout_score[max(0, onset - 1)] + 1e-12)),
        'jump_onset_to_next': float(readout_score[min(len(readout_score) - 1, onset + 1)] / (readout_score[onset] + 1e-12)),
        'threshold': threshold,
        'first_threshold_cross': first_cross,
        'fractional_threshold_cross': fractional_cross,
        'fractional_offset_from_onset': None if fractional_cross is None else float(fractional_cross - onset),
        'local_peak_step_near_onset': int(local_peak_step),
        'local_peak_interpolated_event': local_interp_event,
        'local_peak_offset_from_onset': float(local_interp_event - onset),
        'peak_step_global': int(np.argmax(readout_score)),
        'peak_delay_global_from_onset': int(np.argmax(readout_score) - onset),
    })

summary = pd.DataFrame(summary_rows)
history = pd.DataFrame(history_rows)
candidates = pd.DataFrame(candidate_rows)
summary['within_2_steps_fractional'] = summary['fractional_offset_from_onset'].apply(lambda x: x is not None and 0.0 <= x <= 2.0)
summary['positive_interpolation_trigger_at_onset'] = summary['interpolation_trigger_at_onset'] > 0.0

summary_json = {
    'run_id': '031_interpolated_temporal_alignment_collapse',
    'hypothesis': 'When two temporal alignments remain coherent but internal consistency breaks during a coherence climb, collapse should resolve to an interpolated sub-step structure instead of a hard discrete winner.',
    'method': {
        'source': 'accessible lithium causal-pair replay from the v3 handoff package',
        'candidate_set': '6 temporal-alignment channels x 4 lags = 24 candidate branches per band',
        'top2_rule': 'use the two highest-probability temporal branches at each step',
        'interpolation_trigger': 'dual_branch_coherence * coherence_climb * internal_inconsistency',
        'interpolated_event': 'step + weighted_average(top1_lag, top2_lag)',
        'structural_admissibility': 'interpolated alignment/future/retro/persistence penalized by internal inconsistency',
        'readout_score': 'collapse_prob * visible_fraction',
        'threshold_rule': 'max(pre_onset_mean + 2*pre_onset_std, 1.03*pre_onset_max)',
    },
    'results': summary.to_dict(orient='records'),
    'global_findings': {
        'bands_with_fractional_cross_within_2_steps': int(summary['within_2_steps_fractional'].sum()),
        'bands_with_positive_interpolation_trigger_at_onset': int(summary['positive_interpolation_trigger_at_onset'].sum()),
    },
    'honesty_note': 'This is still a proxy analysis on the accessible replay. The interpolated event coordinates are sub-step synthetic observables derived from the top-two temporal branch structure, not measured detector coordinates or validated spectroscopy line centers.'
}

summary.to_csv(OUT / 'interpolated_collapse_summary.csv', index=False)
history.to_csv(OUT / 'interpolated_collapse_history.csv', index=False)
candidates.to_csv(OUT / 'interpolated_collapse_candidates.csv', index=False)
(OUT / 'interpolated_collapse_summary.json').write_text(json.dumps(summary_json, indent=2))
(OUT / 'interpolated_collapse_notes.md').write_text(
    '# Run 031 interpolated temporal-alignment collapse\n\n'
    'This pass replaces hard winner collapse with a top-two branch interpolation rule when temporal coherence climbs while internal consistency breaks.\n'
)

zip_path = Path('/mnt/data/run_031_interpolated_temporal_alignment_collapse.zip')
with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
    for file in sorted(OUT.rglob('*')):
        zf.write(file, arcname=f'run_031_interpolated_temporal_alignment_collapse/{file.name}')

print(json.dumps(summary_json, indent=2))
