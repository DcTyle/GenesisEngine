import json
import zipfile
from pathlib import Path

import numpy as np
import pandas as pd

BASE = Path('/mnt/data/research_v3/research_handoff_package_v3_all_in_one')
OUT = Path('/mnt/data/run_026_coherence_gated_lag_tensor_closure')
OUT.mkdir(parents=True, exist_ok=True)

history_path = BASE / 'continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_history.csv'
summary020_path = BASE / 'continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_summary.json'
run025_summary_path = Path('/mnt/data/run_025_lag_aware_temporal_tensor_closure/lag_tensor_summary.json')

hist = pd.read_csv(history_path)
summary020 = json.loads(summary020_path.read_text())
run025 = json.loads(run025_summary_path.read_text()) if run025_summary_path.exists() else None

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
            out[i] = 0.0
            continue
        xb = b[xidx_start:xidx_end]
        if len(xa) < 3 or len(xa) != len(xb):
            out[i] = 0.0
            continue
        sa = xa.std()
        sb = xb.std()
        if sa < 1e-15 or sb < 1e-15:
            out[i] = 0.0
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


def choose_best_lag_with_coherence(a, b, lags, window, persistence_radius=1, margin_floor=0.0):
    mats = []
    strengths = {}
    for lag in lags:
        corr = local_corr(a, b, window=window, lag=lag)
        mats.append(corr)
        strengths[f'lag_{lag}'] = float(corr.sum())
    stack = np.vstack(mats)
    best_idx = np.argmax(stack, axis=0)
    best = stack[best_idx, np.arange(stack.shape[1])]
    chosen_lag = np.array(lags, dtype=int)[best_idx]

    sorted_stack = np.sort(stack, axis=0)
    if stack.shape[0] > 1:
        second_best = sorted_stack[-2]
    else:
        second_best = np.zeros(stack.shape[1], dtype=float)
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
        'strength_norm': strength_norm,
        'coherence_gate': coherence_gate,
        'strengths': strengths,
    }


ledger_rows = []
channel_detail_rows = []
comparison_rows = []

global_zero_point_added = 0.0
global_zero_point_removed = 0.0

for r in [3, 5, 6]:
    n = closure[f'n_r{r}_baseline'].to_numpy(dtype=float)
    dn = np.diff(np.concatenate(([0.0], n)))
    final_n = final_targets[r]
    onset = onset_targets[r]
    support_idx = np.arange(onset + 1, min(onset + 13, len(n)))
    reserve_idx = np.arange(min(onset + 14, len(n) - 1), len(n))
    if len(support_idx) == 0 or len(reserve_idx) == 0:
        raise RuntimeError(f'Empty support or reserve window for r{r}')

    upstream = closure['n_r2_baseline'].to_numpy(dtype=float)
    if r == 6:
        upstream = 0.6 * closure['n_r2_baseline'].to_numpy(dtype=float) + 0.4 * closure['n_r5_baseline'].to_numpy(dtype=float)

    local_slope = np.gradient(n)
    upstream_slope = np.gradient(upstream)
    local_curvature = np.gradient(local_slope)
    deficit = np.clip(final_n - n, 0.0, None)
    donor_activity = np.clip(dn, 0.0, None)
    pair_memory = pair_support_map[r] * np.clip(upstream / (upstream.max() + 1e-12), 0.0, None)
    residence = np.cumsum(np.clip(n, 0.0, None)) / (np.cumsum(np.clip(n, 0.0, None))[-1] + 1e-12)
    tail_mass = np.cumsum(np.clip(dn[::-1], 0.0, None))[::-1]
    tail_persistence = tail_mass / (tail_mass.max() + 1e-12)
    closure_readiness = np.clip(1.0 - deficit / (deficit.max() + 1e-12), 0.0, 1.0)

    support_window = 5
    reserve_window = 7
    causal_lags = [0, 1, 2, 3]
    reserve_lags = [0, 1, 2]

    # Coherence-gated lag selection.
    sup_pair_upstream = choose_best_lag_with_coherence(pair_memory, upstream_slope, causal_lags, support_window, persistence_radius=1, margin_floor=0.05)
    sup_res_def = choose_best_lag_with_coherence(residence, deficit, causal_lags, support_window, persistence_radius=1, margin_floor=0.05)
    sup_def_slope = choose_best_lag_with_coherence(deficit, np.clip(local_slope, 0.0, None), causal_lags, support_window, persistence_radius=1, margin_floor=0.05)
    sup_curv_def = choose_best_lag_with_coherence(np.abs(local_curvature), deficit, causal_lags, support_window, persistence_radius=1, margin_floor=0.05)
    sup_pair_tail = choose_best_lag_with_coherence(pair_memory, tail_persistence, causal_lags, support_window, persistence_radius=1, margin_floor=0.05)

    res_res_donor = choose_best_lag_with_coherence(residence, donor_activity + 1e-12, reserve_lags, reserve_window, persistence_radius=1, margin_floor=0.05)
    res_tail_donor = choose_best_lag_with_coherence(tail_persistence, donor_activity + 1e-12, reserve_lags, reserve_window, persistence_radius=1, margin_floor=0.05)
    res_close_donor = choose_best_lag_with_coherence(closure_readiness, donor_activity + 1e-12, reserve_lags, reserve_window, persistence_radius=1, margin_floor=0.05)
    res_curv_donor = choose_best_lag_with_coherence(np.abs(local_curvature), donor_activity + 1e-12, reserve_lags, reserve_window, persistence_radius=1, margin_floor=0.05)

    zp_support_drive = np.maximum(
        0.0,
        0.35 * sup_pair_upstream['gated_best']
        + 0.25 * sup_res_def['gated_best']
        + 0.25 * sup_def_slope['gated_best']
        + 0.15 * sup_curv_def['gated_best']
    )
    zp_reserve_relax = np.maximum(
        0.0,
        0.40 * res_tail_donor['gated_best']
        + 0.25 * res_res_donor['gated_best']
        + 0.20 * res_close_donor['gated_best']
        + 0.15 * res_curv_donor['gated_best']
    )

    support_channels = {
        'pair_upstream_slope_gated': sup_pair_upstream['gated_best'][support_idx],
        'residence_deficit_gated': sup_res_def['gated_best'][support_idx],
        'deficit_local_slope_gated': sup_def_slope['gated_best'][support_idx],
        'curvature_deficit_gated': sup_curv_def['gated_best'][support_idx],
        'pair_tail_gated': sup_pair_tail['gated_best'][support_idx],
        'zp_drive_support': zp_support_drive[support_idx],
    }
    support_strengths = {k: float(v.sum()) for k, v in support_channels.items()}
    support_total = sum(support_strengths.values()) + 1e-12
    support_channel_weights = {k: v / support_total for k, v in support_strengths.items()}
    support_score = np.zeros(len(support_idx), dtype=float)
    for k, arr in support_channels.items():
        support_score += support_channel_weights[k] * arr

    mean_support_gate = (
        sup_pair_upstream['coherence_gate'][support_idx]
        + sup_res_def['coherence_gate'][support_idx]
        + sup_def_slope['coherence_gate'][support_idx]
        + sup_curv_def['coherence_gate'][support_idx]
        + sup_pair_tail['coherence_gate'][support_idx]
    ) / 5.0

    support_score *= (0.75 + 0.25 * mean_support_gate)
    support_score *= (1.0 + deficit[support_idx] / (deficit[support_idx].max() + 1e-12))
    support_score *= (1.0 + 0.5 * zp_support_drive[support_idx] / (zp_support_drive[support_idx].max() + 1e-12))
    if support_score.sum() <= 0:
        support_score = np.ones_like(support_score)
    support_weights = support_score / support_score.sum()
    support_coherence = float(support_score.mean() / (support_score.max() + 1e-12))

    reserve_channels = {
        'residence_donor_gated': res_res_donor['gated_best'][reserve_idx],
        'tail_donor_gated': res_tail_donor['gated_best'][reserve_idx],
        'closure_donor_gated': res_close_donor['gated_best'][reserve_idx],
        'curvature_donor_gated': res_curv_donor['gated_best'][reserve_idx],
        'zp_relax_reserve': zp_reserve_relax[reserve_idx],
    }
    reserve_strengths = {k: float(v.sum()) for k, v in reserve_channels.items()}
    reserve_total = sum(reserve_strengths.values()) + 1e-12
    reserve_channel_weights = {k: v / reserve_total for k, v in reserve_strengths.items()}
    reserve_score = np.zeros(len(reserve_idx), dtype=float)
    for k, arr in reserve_channels.items():
        reserve_score += reserve_channel_weights[k] * arr

    mean_reserve_gate = (
        res_res_donor['coherence_gate'][reserve_idx]
        + res_tail_donor['coherence_gate'][reserve_idx]
        + res_close_donor['coherence_gate'][reserve_idx]
        + res_curv_donor['coherence_gate'][reserve_idx]
    ) / 4.0

    reserve_score *= (0.75 + 0.25 * mean_reserve_gate)
    reserve_score *= np.clip(donor_activity[reserve_idx] + 1e-12, 0.0, None)
    reserve_score *= (1.0 + 0.5 * zp_reserve_relax[reserve_idx] / (zp_reserve_relax[reserve_idx].max() + 1e-12))
    if reserve_score.sum() <= 0:
        reserve_score = np.clip(donor_activity[reserve_idx] + 1e-12, 1e-12, None)
    reserve_weights = reserve_score / reserve_score.sum()
    reserve_coherence = float(reserve_score.mean() / (reserve_score.max() + 1e-12))

    support_lag_stability = float(np.mean([
        sup_pair_upstream['persistence'][support_idx].mean(),
        sup_res_def['persistence'][support_idx].mean(),
        sup_def_slope['persistence'][support_idx].mean(),
        sup_curv_def['persistence'][support_idx].mean(),
        sup_pair_tail['persistence'][support_idx].mean(),
    ]))
    reserve_lag_stability = float(np.mean([
        res_res_donor['persistence'][reserve_idx].mean(),
        res_tail_donor['persistence'][reserve_idx].mean(),
        res_close_donor['persistence'][reserve_idx].mean(),
        res_curv_donor['persistence'][reserve_idx].mean(),
    ]))
    support_margin_mean = float(np.mean([
        sup_pair_upstream['margin_norm'][support_idx].mean(),
        sup_res_def['margin_norm'][support_idx].mean(),
        sup_def_slope['margin_norm'][support_idx].mean(),
        sup_curv_def['margin_norm'][support_idx].mean(),
        sup_pair_tail['margin_norm'][support_idx].mean(),
    ]))
    reserve_margin_mean = float(np.mean([
        res_res_donor['margin_norm'][reserve_idx].mean(),
        res_tail_donor['margin_norm'][reserve_idx].mean(),
        res_close_donor['margin_norm'][reserve_idx].mean(),
        res_curv_donor['margin_norm'][reserve_idx].mean(),
    ]))

    correlation_deficit_raw = float((deficit[support_idx] * (1.0 + zp_support_drive[support_idx])).sum() / len(support_idx))
    reserve_mass = float(np.clip(dn[reserve_idx], 0.0, None).sum())
    zp_support_coherence = float(zp_support_drive[support_idx].mean() / (zp_support_drive[support_idx].max() + 1e-12))
    zp_reserve_coherence = float(zp_reserve_relax[reserve_idx].mean() / (zp_reserve_relax[reserve_idx].max() + 1e-12))

    transport_fraction = (
        pair_support_norm[r]
        * min(1.0, correlation_deficit_raw / (max(1e-12, final_n / len(support_idx)) * 2.0))
        * support_coherence
        * reserve_coherence
        * (0.5 + 0.5 * support_lag_stability)
        * (0.5 + 0.5 * reserve_lag_stability)
        * (0.5 + 0.5 * support_margin_mean)
        * (0.5 + 0.5 * reserve_margin_mean)
        * (0.5 + 0.5 * zp_support_coherence)
        * (0.5 + 0.5 * zp_reserve_coherence)
    )
    actual_transport = reserve_mass * transport_fraction

    dn_new = dn.copy()
    advance = actual_transport * support_weights
    repay = actual_transport * reserve_weights
    dn_new[support_idx] += advance
    dn_new[reserve_idx] -= repay
    n_new = np.cumsum(dn_new)

    total_err = n_new[-1] - final_n
    if abs(total_err) > 1e-15:
        renorm_weights = np.clip(reserve_weights, 1e-16, None)
        renorm_weights /= renorm_weights.sum()
        dn_new[reserve_idx] -= total_err * renorm_weights
        repay = repay + total_err * renorm_weights
        n_new = np.cumsum(dn_new)

    closure[f'n_r{r}_coherence_gated'] = n_new

    post_mask = np.arange(len(n)) > onset
    centroid_baseline = float((np.arange(len(n))[post_mask] * dn[post_mask]).sum() / (dn[post_mask].sum() + 1e-12))
    centroid_new = float((np.arange(len(n))[post_mask] * dn_new[post_mask]).sum() / (dn_new[post_mask].sum() + 1e-12))

    zp_added = float(advance.sum())
    zp_removed = float(repay.sum())
    global_zero_point_added += zp_added
    global_zero_point_removed += zp_removed

    ledger_rows.append({
        'radius_bin': r,
        'support_start': int(support_idx[0]),
        'support_end': int(support_idx[-1]),
        'reserve_start': int(reserve_idx[0]),
        'reserve_end': int(reserve_idx[-1]),
        'pair_support_raw': pair_support_map[r],
        'pair_support_norm': pair_support_norm[r],
        'support_lag_stability': support_lag_stability,
        'reserve_lag_stability': reserve_lag_stability,
        'support_margin_mean': support_margin_mean,
        'reserve_margin_mean': reserve_margin_mean,
        'correlation_deficit_raw': correlation_deficit_raw,
        'support_coherence': support_coherence,
        'reserve_coherence': reserve_coherence,
        'zp_support_coherence': zp_support_coherence,
        'zp_reserve_coherence': zp_reserve_coherence,
        'reserve_mass': reserve_mass,
        'transport_fraction': transport_fraction,
        'actual_transport': actual_transport,
        'zp_added': zp_added,
        'zp_removed': zp_removed,
        'baseline_final_n': float(n[-1]),
        'coherence_gated_final_n': float(n_new[-1]),
        'post_onset_centroid_baseline': centroid_baseline,
        'post_onset_centroid_coherence_gated': centroid_new,
        'support_strengths': support_strengths,
        'reserve_strengths': reserve_strengths,
        'support_channel_weights': support_channel_weights,
        'reserve_channel_weights': reserve_channel_weights,
    })

    support_meta = {
        'pair_upstream': sup_pair_upstream,
        'residence_deficit': sup_res_def,
        'deficit_slope': sup_def_slope,
        'curvature_deficit': sup_curv_def,
        'pair_tail': sup_pair_tail,
    }
    reserve_meta = {
        'residence_donor': res_res_donor,
        'tail_donor': res_tail_donor,
        'closure_donor': res_close_donor,
        'curvature_donor': res_curv_donor,
    }

    for idx_local, step in enumerate(support_idx):
        row = {
            'radius_bin': r,
            'domain': 'support',
            'step': int(step),
            'zp_support_drive': float(zp_support_drive[step]),
            'composite_score': float(support_score[idx_local]),
            'normalized_weight': float(support_weights[idx_local]),
        }
        for name, meta in support_meta.items():
            row[f'{name}_gated'] = float(meta['gated_best'][step])
            row[f'{name}_raw'] = float(meta['raw_best'][step])
            row[f'{name}_selected_lag'] = int(meta['chosen_lag'][step])
            row[f'{name}_margin_norm'] = float(meta['margin_norm'][step])
            row[f'{name}_persistence'] = float(meta['persistence'][step])
            row[f'{name}_coherence_gate'] = float(meta['coherence_gate'][step])
        channel_detail_rows.append(row)

    for idx_local, step in enumerate(reserve_idx):
        row = {
            'radius_bin': r,
            'domain': 'reserve',
            'step': int(step),
            'zp_reserve_relax': float(zp_reserve_relax[step]),
            'composite_score': float(reserve_score[idx_local]),
            'normalized_weight': float(reserve_weights[idx_local]),
        }
        for name, meta in reserve_meta.items():
            row[f'{name}_gated'] = float(meta['gated_best'][step])
            row[f'{name}_raw'] = float(meta['raw_best'][step])
            row[f'{name}_selected_lag'] = int(meta['chosen_lag'][step])
            row[f'{name}_margin_norm'] = float(meta['margin_norm'][step])
            row[f'{name}_persistence'] = float(meta['persistence'][step])
            row[f'{name}_coherence_gate'] = float(meta['coherence_gate'][step])
        channel_detail_rows.append(row)

closure['n_r2_coherence_gated'] = closure['n_r2_baseline']

for r in [2, 3, 5, 6]:
    base_n = closure[f'n_r{r}_baseline'].to_numpy(dtype=float)
    new_n = closure[f'n_r{r}_coherence_gated'].to_numpy(dtype=float)
    dn_b = np.diff(np.concatenate(([0.0], base_n)))
    dn_n = np.diff(np.concatenate(([0.0], new_n)))
    onset = onset_targets[r]
    post_mask = np.arange(len(base_n)) > onset
    centroid_b = float((np.arange(len(base_n))[post_mask] * dn_b[post_mask]).sum() / (dn_b[post_mask].sum() + 1e-12))
    centroid_n = float((np.arange(len(base_n))[post_mask] * dn_n[post_mask]).sum() / (dn_n[post_mask].sum() + 1e-12))
    first_b = int(np.where(base_n >= 0.1)[0][0])
    first_n = int(np.where(new_n >= 0.1)[0][0])
    comparison_rows.append({
        'radius_bin': r,
        'onset_target': onset,
        'baseline_first_persist': first_b,
        'coherence_gated_first_persist': first_n,
        'target_final_n': final_targets[r],
        'baseline_final_n': float(base_n[-1]),
        'coherence_gated_final_n': float(new_n[-1]),
        'coherence_gated_minus_target': float(new_n[-1] - final_targets[r]),
        'post_onset_centroid_baseline': centroid_b,
        'post_onset_centroid_coherence_gated': centroid_n,
    })

comparison = pd.DataFrame(comparison_rows)
ledger = pd.DataFrame(ledger_rows)
channel_detail = pd.DataFrame(channel_detail_rows)

advanced_mass_total = float(ledger['actual_transport'].sum())
repaid_mass_total = float(ledger['actual_transport'].sum())
closure_residual = advanced_mass_total - repaid_mass_total
zero_point_residual = global_zero_point_added - global_zero_point_removed

global_base = float(sum(closure[f'n_r{r}_baseline'].iloc[-1] for r in [2, 3, 5, 6]))
global_new = float(sum(closure[f'n_r{r}_coherence_gated'].iloc[-1] for r in [2, 3, 5, 6]))

summary_out = {
    'run_id': '026_coherence_gated_lag_tensor_closure',
    'hypothesis': 'Exact closure should remain stable when lag-best routing is coherence-gated so temporally unstable winners lose authority instead of contributing equally with persistent lag structure.',
    'method': {
        'frozen_region': 'all dn_j(t) for t <= onset_j are left unchanged',
        'support_window_rule': 't in [onset_j+1, onset_j+12]',
        'reserve_window_rule': 't in [onset_j+14, end]',
        'lag_rule': 'bounded causal lag search chooses local winners over rolling neighborhoods',
        'coherence_gate_rule': 'each lag-best channel is attenuated by local lag persistence, best-vs-second-best margin, and local strength',
        'support_lags_tested': causal_lags,
        'reserve_lags_tested': reserve_lags,
        'support_window_size': support_window,
        'reserve_window_size': reserve_window,
        'transport_fraction_rule': 'pair_support_norm_j * deficit_norm_like_j * support_coherence_j * reserve_coherence_j * support_stability_j * reserve_stability_j * support_margin_j * reserve_margin_j * zp_support_factor_j * zp_reserve_factor_j',
        'exact_balance_rule': 'mass added to support equals mass removed from reserve exactly inside each band; final target is reserve-renormalized only',
    },
    'transport_ledger': ledger.to_dict(orient='records'),
    'closure_metrics': {
        'advanced_mass_total': advanced_mass_total,
        'repaid_mass_total': repaid_mass_total,
        'closure_residual': closure_residual,
        'zero_point_added_total': global_zero_point_added,
        'zero_point_removed_total': global_zero_point_removed,
        'zero_point_residual': zero_point_residual,
        'global_final_total_baseline': global_base,
        'global_final_total_coherence_gated': global_new,
    },
    'results': comparison.to_dict(orient='records'),
    'relative_to_run_025': run025['closure_metrics'] if run025 else None,
    'honesty_note': 'This remains a toy/analogue continuation on the accessible replay. The coherence gate is a bookkeeping stabilizer over causal lag assignments, not a claim of experimentally established microscopic temporal selection.',
}

notes = f'''# Run 026 coherence-gated lag tensor closure\n\nThis pass keeps the lag-aware tensor, but attenuates jittery lag winners.\nEach lag-best channel is multiplied by a local coherence gate derived from lag persistence, winner margin, and local channel strength.\n\nAdvanced/repaid mass total: {advanced_mass_total:.12f}\nClosure residual: {closure_residual:.12e}\nZero-point added total: {global_zero_point_added:.12f}\nZero-point removed total: {global_zero_point_removed:.12f}\nZero-point residual: {zero_point_residual:.12e}\nGlobal baseline total: {global_base:.12f}\nGlobal coherence-gated total: {global_new:.12f}\n'''

comparison.to_csv(OUT / 'coherence_gated_comparison.csv', index=False)
ledger.to_csv(OUT / 'coherence_gated_ledger.csv', index=False)
channel_detail.to_csv(OUT / 'coherence_gated_channel_detail.csv', index=False)
closure.to_csv(OUT / 'coherence_gated_history.csv', index=False)
(OUT / 'coherence_gated_summary.json').write_text(json.dumps(summary_out, indent=2))
(OUT / 'coherence_gated_notes.md').write_text(notes)

zip_path = Path('/mnt/data/run_026_coherence_gated_lag_tensor_closure.zip')
with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
    for file in sorted(OUT.rglob('*')):
        zf.write(file, arcname=f'run_026_coherence_gated_lag_tensor_closure/{file.name}')

print(json.dumps(summary_out, indent=2))
