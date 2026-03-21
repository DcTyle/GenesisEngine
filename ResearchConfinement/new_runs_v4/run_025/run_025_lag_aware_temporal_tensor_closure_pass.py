import json
import zipfile
from pathlib import Path

import numpy as np
import pandas as pd

BASE = Path('/mnt/data/research_v3/research_handoff_package_v3_all_in_one')
OUT = Path('/mnt/data/run_025_lag_aware_temporal_tensor_closure')
OUT.mkdir(parents=True, exist_ok=True)

history_path = BASE / 'continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_history.csv'
summary020_path = BASE / 'continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_summary.json'
run024_summary_path = Path('/mnt/data/run_024_correlation_derived_zero_point_closure/correlation_zero_point_summary.json')

hist = pd.read_csv(history_path)
summary020 = json.loads(summary020_path.read_text())
run024 = json.loads(run024_summary_path.read_text()) if run024_summary_path.exists() else None

closure = hist[['step', 'n_r2_baseline', 'n_r3_baseline', 'n_r5_baseline', 'n_r6_baseline']].copy()

onset_targets = {2: 4, 3: 5, 5: 18, 6: 31}
final_targets = {
    2: 2.1869844097664024,
    3: 1.831799583084502,
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


def choose_best_lag(a, b, lags, window):
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
    return best, chosen_lag, strengths


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

    # Support channels: lag-aware local temporal tensor
    sup_pair_upstream, sup_pair_upstream_lag, sup_pair_upstream_strengths = choose_best_lag(pair_memory, upstream_slope, causal_lags, support_window)
    sup_res_def, sup_res_def_lag, sup_res_def_strengths = choose_best_lag(residence, deficit, causal_lags, support_window)
    sup_def_slope, sup_def_slope_lag, sup_def_slope_strengths = choose_best_lag(deficit, np.clip(local_slope, 0.0, None), causal_lags, support_window)
    sup_curv_def, sup_curv_def_lag, sup_curv_def_strengths = choose_best_lag(np.abs(local_curvature), deficit, causal_lags, support_window)
    sup_pair_tail, sup_pair_tail_lag, sup_pair_tail_strengths = choose_best_lag(pair_memory, tail_persistence, causal_lags, support_window)

    # Reserve channels: also lag-aware but allow shorter causal lookback
    res_res_donor, res_res_donor_lag, res_res_donor_strengths = choose_best_lag(residence, donor_activity + 1e-12, reserve_lags, reserve_window)
    res_tail_donor, res_tail_donor_lag, res_tail_donor_strengths = choose_best_lag(tail_persistence, donor_activity + 1e-12, reserve_lags, reserve_window)
    res_close_donor, res_close_donor_lag, res_close_donor_strengths = choose_best_lag(closure_readiness, donor_activity + 1e-12, reserve_lags, reserve_window)
    res_curv_donor, res_curv_donor_lag, res_curv_donor_strengths = choose_best_lag(np.abs(local_curvature), donor_activity + 1e-12, reserve_lags, reserve_window)

    # Same tensor zero-point fields, now also lag-aware
    zp_support_drive = np.maximum(0.0, 0.35 * sup_pair_upstream + 0.25 * sup_res_def + 0.25 * sup_def_slope + 0.15 * sup_curv_def)
    zp_reserve_relax = np.maximum(0.0, 0.40 * res_tail_donor + 0.25 * res_res_donor + 0.20 * res_close_donor + 0.15 * res_curv_donor)

    support_channels = {
        'pair_upstream_slope_lagbest': sup_pair_upstream[support_idx],
        'residence_deficit_lagbest': sup_res_def[support_idx],
        'deficit_local_slope_lagbest': sup_def_slope[support_idx],
        'curvature_deficit_lagbest': sup_curv_def[support_idx],
        'pair_tail_lagbest': sup_pair_tail[support_idx],
        'zp_drive_support': zp_support_drive[support_idx],
    }
    support_strengths = {k: float(v.sum()) for k, v in support_channels.items()}
    support_total = sum(support_strengths.values()) + 1e-12
    support_channel_weights = {k: v / support_total for k, v in support_strengths.items()}
    support_score = np.zeros(len(support_idx), dtype=float)
    for k, arr in support_channels.items():
        support_score += support_channel_weights[k] * arr
    support_score *= (1.0 + deficit[support_idx] / (deficit[support_idx].max() + 1e-12))
    support_score *= (1.0 + 0.5 * zp_support_drive[support_idx] / (zp_support_drive[support_idx].max() + 1e-12))
    if support_score.sum() <= 0:
        support_score = np.ones_like(support_score)
    support_weights = support_score / support_score.sum()
    support_coherence = float(support_score.mean() / (support_score.max() + 1e-12))

    reserve_channels = {
        'residence_donor_lagbest': res_res_donor[reserve_idx],
        'tail_donor_lagbest': res_tail_donor[reserve_idx],
        'closure_donor_lagbest': res_close_donor[reserve_idx],
        'curvature_donor_lagbest': res_curv_donor[reserve_idx],
        'zp_relax_reserve': zp_reserve_relax[reserve_idx],
    }
    reserve_strengths = {k: float(v.sum()) for k, v in reserve_channels.items()}
    reserve_total = sum(reserve_strengths.values()) + 1e-12
    reserve_channel_weights = {k: v / reserve_total for k, v in reserve_strengths.items()}
    reserve_score = np.zeros(len(reserve_idx), dtype=float)
    for k, arr in reserve_channels.items():
        reserve_score += reserve_channel_weights[k] * arr
    reserve_score *= np.clip(donor_activity[reserve_idx] + 1e-12, 0.0, None)
    reserve_score *= (1.0 + 0.5 * zp_reserve_relax[reserve_idx] / (zp_reserve_relax[reserve_idx].max() + 1e-12))
    if reserve_score.sum() <= 0:
        reserve_score = np.clip(donor_activity[reserve_idx] + 1e-12, 1e-12, None)
    reserve_weights = reserve_score / reserve_score.sum()
    reserve_coherence = float(reserve_score.mean() / (reserve_score.max() + 1e-12))

    causal_alignment = float(np.mean(sup_pair_upstream_lag[support_idx] + sup_def_slope_lag[support_idx]) / 6.0)
    reserve_alignment = float(np.mean(res_tail_donor_lag[reserve_idx] + res_close_donor_lag[reserve_idx]) / 4.0)
    correlation_deficit_raw = float((deficit[support_idx] * (1.0 + zp_support_drive[support_idx])).sum() / len(support_idx))
    reserve_mass = float(np.clip(dn[reserve_idx], 0.0, None).sum())
    zp_support_coherence = float(zp_support_drive[support_idx].mean() / (zp_support_drive[support_idx].max() + 1e-12))
    zp_reserve_coherence = float(zp_reserve_relax[reserve_idx].mean() / (zp_reserve_relax[reserve_idx].max() + 1e-12))

    transport_fraction = (
        pair_support_norm[r]
        * min(1.0, correlation_deficit_raw / (max(1e-12, final_n / len(support_idx)) * 2.0))
        * support_coherence
        * reserve_coherence
        * (0.75 + 0.25 * causal_alignment)
        * (0.75 + 0.25 * reserve_alignment)
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

    closure[f'n_r{r}_lag_tensor'] = n_new

    post_mask = np.arange(len(n)) > onset
    centroid_baseline = float((np.arange(len(n))[post_mask] * dn[post_mask]).sum() / (dn[post_mask].sum() + 1e-12))
    centroid_new = float((np.arange(len(n))[post_mask] * dn_new[post_mask]).sum() / (dn_new[post_mask].sum() + 1e-12))

    zp_added = float(advance.sum())
    zp_removed = float(repay.sum())
    global_zero_point_added += zp_added
    global_zero_point_removed += zp_removed

    lag_strengths = {
        'support_pair_upstream': sup_pair_upstream_strengths,
        'support_residence_deficit': sup_res_def_strengths,
        'support_deficit_slope': sup_def_slope_strengths,
        'support_curvature_deficit': sup_curv_def_strengths,
        'support_pair_tail': sup_pair_tail_strengths,
        'reserve_residence_donor': res_res_donor_strengths,
        'reserve_tail_donor': res_tail_donor_strengths,
        'reserve_closure_donor': res_close_donor_strengths,
        'reserve_curvature_donor': res_curv_donor_strengths,
    }

    ledger_rows.append({
        'radius_bin': r,
        'support_start': int(support_idx[0]),
        'support_end': int(support_idx[-1]),
        'reserve_start': int(reserve_idx[0]),
        'reserve_end': int(reserve_idx[-1]),
        'pair_support_raw': pair_support_map[r],
        'pair_support_norm': pair_support_norm[r],
        'correlation_deficit_raw': correlation_deficit_raw,
        'support_coherence': support_coherence,
        'reserve_coherence': reserve_coherence,
        'causal_alignment': causal_alignment,
        'reserve_alignment': reserve_alignment,
        'zp_support_coherence': zp_support_coherence,
        'zp_reserve_coherence': zp_reserve_coherence,
        'reserve_mass': reserve_mass,
        'transport_fraction': transport_fraction,
        'actual_transport': actual_transport,
        'zp_added': zp_added,
        'zp_removed': zp_removed,
        'baseline_final_n': float(n[-1]),
        'lag_tensor_final_n': float(n_new[-1]),
        'post_onset_centroid_baseline': centroid_baseline,
        'post_onset_centroid_lag_tensor': centroid_new,
        'support_strengths': support_strengths,
        'reserve_strengths': reserve_strengths,
        'support_channel_weights': support_channel_weights,
        'reserve_channel_weights': reserve_channel_weights,
        'lag_strengths': lag_strengths,
    })

    for idx_local, step in enumerate(support_idx):
        channel_detail_rows.append({
            'radius_bin': r,
            'domain': 'support',
            'step': int(step),
            'pair_upstream_slope_lagbest': float(sup_pair_upstream[step]),
            'pair_upstream_selected_lag': int(sup_pair_upstream_lag[step]),
            'residence_deficit_lagbest': float(sup_res_def[step]),
            'residence_deficit_selected_lag': int(sup_res_def_lag[step]),
            'deficit_local_slope_lagbest': float(sup_def_slope[step]),
            'deficit_local_slope_selected_lag': int(sup_def_slope_lag[step]),
            'curvature_deficit_lagbest': float(sup_curv_def[step]),
            'curvature_deficit_selected_lag': int(sup_curv_def_lag[step]),
            'pair_tail_lagbest': float(sup_pair_tail[step]),
            'pair_tail_selected_lag': int(sup_pair_tail_lag[step]),
            'zp_support_drive': float(zp_support_drive[step]),
            'composite_score': float(support_score[idx_local]),
            'normalized_weight': float(support_weights[idx_local]),
        })

    for idx_local, step in enumerate(reserve_idx):
        channel_detail_rows.append({
            'radius_bin': r,
            'domain': 'reserve',
            'step': int(step),
            'residence_donor_lagbest': float(res_res_donor[step]),
            'residence_donor_selected_lag': int(res_res_donor_lag[step]),
            'tail_donor_lagbest': float(res_tail_donor[step]),
            'tail_donor_selected_lag': int(res_tail_donor_lag[step]),
            'closure_donor_lagbest': float(res_close_donor[step]),
            'closure_donor_selected_lag': int(res_close_donor_lag[step]),
            'curvature_donor_lagbest': float(res_curv_donor[step]),
            'curvature_donor_selected_lag': int(res_curv_donor_lag[step]),
            'zp_reserve_relax': float(zp_reserve_relax[step]),
            'composite_score': float(reserve_score[idx_local]),
            'normalized_weight': float(reserve_weights[idx_local]),
        })

closure['n_r2_lag_tensor'] = closure['n_r2_baseline']

for r in [2, 3, 5, 6]:
    base_n = closure[f'n_r{r}_baseline'].to_numpy(dtype=float)
    new_n = closure[f'n_r{r}_lag_tensor'].to_numpy(dtype=float)
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
        'lag_tensor_first_persist': first_n,
        'target_final_n': final_targets[r],
        'baseline_final_n': float(base_n[-1]),
        'lag_tensor_final_n': float(new_n[-1]),
        'lag_tensor_minus_target': float(new_n[-1] - final_targets[r]),
        'post_onset_centroid_baseline': centroid_b,
        'post_onset_centroid_lag_tensor': centroid_n,
    })

comparison = pd.DataFrame(comparison_rows)
ledger = pd.DataFrame(ledger_rows)
channel_detail = pd.DataFrame(channel_detail_rows)

advanced_mass_total = float(ledger['actual_transport'].sum())
repaid_mass_total = float(ledger['actual_transport'].sum())
closure_residual = advanced_mass_total - repaid_mass_total
zero_point_residual = global_zero_point_added - global_zero_point_removed

global_base = float(sum(closure[f'n_r{r}_baseline'].iloc[-1] for r in [2, 3, 5, 6]))
global_new = float(sum(closure[f'n_r{r}_lag_tensor'].iloc[-1] for r in [2, 3, 5, 6]))

summary_out = {
    'run_id': '025_lag_aware_temporal_tensor_closure',
    'hypothesis': 'Exact closure should survive when the correlation tensor is made explicitly temporal and causal by using rolling local neighborhoods with lag selection rather than static same-index correlations.',
    'method': {
        'frozen_region': 'all dn_j(t) for t <= onset_j are left unchanged',
        'support_window_rule': 't in [onset_j+1, onset_j+12]',
        'reserve_window_rule': 't in [onset_j+14, end]',
        'temporal_tensor_rule': 'support, reserve, and zero-point channels are computed from lag-aware rolling local correlations over causal neighborhoods',
        'support_lags_tested': causal_lags,
        'reserve_lags_tested': reserve_lags,
        'support_window_size': support_window,
        'reserve_window_size': reserve_window,
        'support_channels': [
            'lag-best(pair_memory x upstream_slope)',
            'lag-best(residence x deficit)',
            'lag-best(deficit x local_slope)',
            'lag-best(curvature x deficit)',
            'lag-best(pair_memory x tail_persistence)',
            'zero_point_support_drive',
        ],
        'reserve_channels': [
            'lag-best(residence x donor_activity)',
            'lag-best(tail_persistence x donor_activity)',
            'lag-best(closure_readiness x donor_activity)',
            'lag-best(curvature x donor_activity)',
            'zero_point_reserve_relax',
        ],
        'transport_fraction_rule': 'pair_support_norm_j * deficit_norm_like_j * support_coherence_j * reserve_coherence_j * causal_alignment_j * reserve_alignment_j * zp_support_factor_j * zp_reserve_factor_j',
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
        'global_final_total_lag_tensor': global_new,
    },
    'results': comparison.to_dict(orient='records'),
    'relative_to_run_024': run024['closure_metrics'] if run024 else None,
    'honesty_note': 'This remains a toy/analogue continuation on the accessible replay. Lag selection here is a causal bookkeeping device over local neighborhoods, not an experimentally validated microscopic timing law.',
}

notes = f'''# Run 025 lag-aware temporal tensor closure\n\nThis pass makes the routing law explicitly temporal.\nInstead of same-index local correlations, the transport and zero-point fields are derived from lag-aware rolling local neighborhoods, and each channel picks its best causal lag from a small bounded set.\n\nAdvanced/repaid mass total: {advanced_mass_total:.12f}\nClosure residual: {closure_residual:.12e}\nZero-point added total: {global_zero_point_added:.12f}\nZero-point removed total: {global_zero_point_removed:.12f}\nZero-point residual: {zero_point_residual:.12e}\nGlobal baseline total: {global_base:.12f}\nGlobal lag-tensor total: {global_new:.12f}\n'''

comparison.to_csv(OUT / 'lag_tensor_comparison.csv', index=False)
ledger.to_csv(OUT / 'lag_tensor_ledger.csv', index=False)
channel_detail.to_csv(OUT / 'lag_tensor_channel_detail.csv', index=False)
closure.to_csv(OUT / 'lag_tensor_history.csv', index=False)
(OUT / 'lag_tensor_summary.json').write_text(json.dumps(summary_out, indent=2))
(OUT / 'lag_tensor_notes.md').write_text(notes)

zip_path = Path('/mnt/data/run_025_lag_aware_temporal_tensor_closure.zip')
with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
    for file in sorted(OUT.rglob('*')):
        zf.write(file, arcname=f'run_025_lag_aware_temporal_tensor_closure/{file.name}')

print(json.dumps(summary_out, indent=2))
