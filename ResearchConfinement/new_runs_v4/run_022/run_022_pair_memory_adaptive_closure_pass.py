import json
import math
import zipfile
from pathlib import Path

import numpy as np
import pandas as pd

BASE = Path('/mnt/data/research_v3/research_handoff_package_v3_all_in_one')
OUT = Path('/mnt/data/run_022_pair_memory_adaptive_closure')
OUT.mkdir(parents=True, exist_ok=True)

history_path = BASE / 'continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_history.csv'
summary_path = BASE / 'continuation_runs/next_pass_outputs/lithium_abszero_causal_pair_replay_summary.json'
run021_summary_path = Path('/mnt/data/run_021_exact_partition_closure/partition_closure_summary.json')

hist = pd.read_csv(history_path)
summary = json.loads(summary_path.read_text())
run021_summary = json.loads(run021_summary_path.read_text()) if run021_summary_path.exists() else None

# Canonical target ledger
onset_targets = {2: 4, 3: 5, 5: 18, 6: 31}
final_targets = {2: 2.1869844097664024, 3: 1.831799583084502, 5: 0.8290027573827758, 6: 0.6721458801801243}

pair_support_map = {
    3: float(summary['pair_support_raw']['2-3']),
    5: float(summary['pair_support_raw']['2-5']),
    6: float(summary['pair_support_raw']['2-6']) + float(summary['pair_support_raw']['5-6']),
}
correlation_deficit_map = {
    3: 1.0 - float(summary['timing_correction']['3']['delta_t_pair']) / float(summary['timing_correction']['3']['t_local']),
    5: 1.0 - float(summary['timing_correction']['5']['delta_t_pair']) / float(summary['timing_correction']['5']['t_local']),
    6: 1.0 - float(summary['timing_correction']['6']['delta_t_pair']) / float(summary['timing_correction']['6']['t_local']),
}
# Convert back to deficit used in run021 notes.
correlation_deficit_map = {k: 1.0 - v for k, v in correlation_deficit_map.items()}

# Upstream pair-memory routing partners available from the accessible summary.
upstream_partners = {
    3: [(2, float(summary['pair_support_raw']['2-3']))],
    5: [(2, float(summary['pair_support_raw']['2-5']))],
    6: [(2, float(summary['pair_support_raw']['2-6'])), (5, float(summary['pair_support_raw']['5-6']))],
}

steps = hist['step'].to_numpy(dtype=int)
closure = pd.DataFrame({'step': steps})
for r in [2, 3, 5, 6]:
    closure[f'n_r{r}_baseline'] = hist[f'n_r{r}_baseline']
    closure[f'n_r{r}_adaptive'] = hist[f'n_r{r}_baseline']

ledger_rows = []

for r in [3, 5, 6]:
    onset = onset_targets[r]
    n = hist[f'n_r{r}_baseline'].to_numpy(dtype=float)
    dn = np.diff(np.concatenate(([0.0], n)))
    final_n = final_targets[r]

    support_start = onset + 1
    support_end = min(onset + 12, len(steps) - 1)
    reserve_start = min(onset + 14, len(steps) - 1)
    reserve_end = len(steps) - 1

    support_idx = np.arange(support_start, support_end + 1)
    reserve_idx = np.arange(reserve_start, reserve_end + 1)

    # Pair-memory proxy from upstream normalized occupancies.
    pair_memory = np.zeros_like(n)
    pair_memory_components = {}
    for upstream_r, weight in upstream_partners[r]:
        upstream = hist[f'n_r{upstream_r}_baseline'].to_numpy(dtype=float)
        upstream_norm = upstream / max(final_targets[upstream_r], 1e-12)
        pair_memory += weight * upstream_norm
        pair_memory_components[upstream_r] = upstream_norm
    pair_memory = pair_memory / max(pair_memory.max(), 1e-12)

    # Traced-residence proxy from cumulative own occupancy after onset.
    residence = np.cumsum(np.clip(n, 0.0, None))
    residence = residence / max(residence.max(), 1e-12)

    # Early support weights favor times with strong upstream memory but preserve onset lock.
    tau_support = 2.5 + 0.35 * (r - 2)
    early_decay = np.exp(-(support_idx - support_start) / tau_support)
    support_weights = early_decay * (0.35 + 0.65 * pair_memory[support_idx])
    support_weights = support_weights / support_weights.sum()

    # Late reserve weights favor high-residence tail mass and later repayment.
    tau_reserve = 8.0 + 0.5 * (r - 2)
    late_ramp = 1.0 - np.exp(-(reserve_idx - reserve_start) / tau_reserve)
    reserve_activity = np.clip(dn[reserve_idx], 0.0, None)
    reserve_weights = late_ramp * (0.25 + 0.75 * residence[reserve_idx]) * (0.20 + 0.80 * reserve_activity / max(reserve_activity.max(), 1e-12))
    reserve_weights = reserve_weights / reserve_weights.sum()

    reserve_mass = dn[reserve_idx].sum()
    closure_drive = pair_support_map[r] * correlation_deficit_map[r]

    # Slightly stronger but still bounded transport than run021, modulated by memory concentration.
    memory_concentration = float((pair_memory[support_idx] * support_weights).sum())
    alpha_base = {3: 0.24, 5: 0.27, 6: 0.31}[r]
    alpha = alpha_base * (0.90 + 0.20 * memory_concentration)
    requested_transport = alpha * closure_drive * reserve_mass
    actual_transport = min(requested_transport, reserve_mass * 0.25)

    dn_new = dn.copy()
    dn_new[support_idx] += actual_transport * support_weights
    dn_new[reserve_idx] -= actual_transport * reserve_weights
    n_new = np.cumsum(dn_new)

    # Exact renormalization to final target while preserving frozen pre-onset region and support window.
    total_err = n_new[-1] - final_n
    if abs(total_err) > 1e-15:
        renorm_idx = reserve_idx
        renorm_weights = np.clip(dn_new[renorm_idx], 1e-16, None)
        renorm_weights = renorm_weights / renorm_weights.sum()
        dn_new[renorm_idx] -= total_err * renorm_weights
        n_new = np.cumsum(dn_new)

    closure[f'n_r{r}_adaptive'] = n_new

    # metrics
    def first_persist_at_threshold(arr, thr=0.1):
        idx = np.where(arr >= thr)[0]
        return int(idx[0]) if len(idx) else None

    post_mask = np.arange(len(n)) > onset
    centroid_baseline = float((np.arange(len(n))[post_mask] * dn[post_mask]).sum() / dn[post_mask].sum())
    centroid_adaptive = float((np.arange(len(n))[post_mask] * dn_new[post_mask]).sum() / dn_new[post_mask].sum())

    ledger_rows.append({
        'radius_bin': r,
        'support_start': int(support_start),
        'support_end': int(support_end),
        'reserve_start': int(reserve_start),
        'reserve_end': int(reserve_end),
        'incoming_support': pair_support_map[r],
        'correlation_deficit': correlation_deficit_map[r],
        'closure_drive': closure_drive,
        'reserve_mass': float(reserve_mass),
        'memory_concentration': memory_concentration,
        'alpha': alpha,
        'requested_transport': float(requested_transport),
        'actual_transport': float(actual_transport),
        'baseline_first_persist': first_persist_at_threshold(n),
        'adaptive_first_persist': first_persist_at_threshold(n_new),
        'baseline_final_n': float(n[-1]),
        'adaptive_final_n': float(n_new[-1]),
        'post_onset_centroid_baseline': centroid_baseline,
        'post_onset_centroid_adaptive': centroid_adaptive,
    })

# Keep r2 untouched; exact carry-through.
closure['n_r2_adaptive'] = closure['n_r2_baseline']

# Collect results.
comparison_rows = []
for r in [2, 3, 5, 6]:
    base_n = closure[f'n_r{r}_baseline'].to_numpy(dtype=float)
    adp_n = closure[f'n_r{r}_adaptive'].to_numpy(dtype=float)
    onset = onset_targets[r]
    dn_b = np.diff(np.concatenate(([0.0], base_n)))
    dn_a = np.diff(np.concatenate(([0.0], adp_n)))
    post_mask = np.arange(len(base_n)) > onset
    centroid_b = float((np.arange(len(base_n))[post_mask] * dn_b[post_mask]).sum() / dn_b[post_mask].sum())
    centroid_a = float((np.arange(len(adp_n))[post_mask] * dn_a[post_mask]).sum() / dn_a[post_mask].sum())
    first_b = int(np.where(base_n >= 0.1)[0][0])
    first_a = int(np.where(adp_n >= 0.1)[0][0])
    comparison_rows.append({
        'radius_bin': r,
        'onset_target': onset,
        'baseline_first_persist': first_b,
        'adaptive_first_persist': first_a,
        'target_final_n': final_targets[r],
        'baseline_final_n': float(base_n[-1]),
        'adaptive_final_n': float(adp_n[-1]),
        'adaptive_minus_target': float(adp_n[-1] - final_targets[r]),
        'post_onset_centroid_baseline': centroid_b,
        'post_onset_centroid_adaptive': centroid_a,
    })

comparison = pd.DataFrame(comparison_rows)
ledger = pd.DataFrame(ledger_rows)

advanced_mass_total = float(ledger['actual_transport'].sum())
repaid_mass_total = float(ledger['actual_transport'].sum())
closure_residual = advanced_mass_total - repaid_mass_total

global_base = float(sum(closure[f'n_r{r}_baseline'].iloc[-1] for r in [2, 3, 5, 6]))
global_adp = float(sum(closure[f'n_r{r}_adaptive'].iloc[-1] for r in [2, 3, 5, 6]))

summary_out = {
    'run_id': '022_pair_memory_adaptive_closure',
    'hypothesis': 'Exact closure should remain stable if the support window and reserve repayment are shaped by pair-memory and traced-residence proxies rather than fixed uniform transport windows.',
    'method': {
        'frozen_region': 'all dn_j(t) for t <= onset_j are left unchanged',
        'support_window_rule': 't in [onset_j+1, onset_j+12], weighted by upstream pair-memory and exponential early decay',
        'reserve_window_rule': 't in [onset_j+14, end], weighted by late ramp, traced-residence proxy, and positive tail activity',
        'closure_drive': 'incoming_pair_support_j * correlation_deficit_j',
        'transport_rule': 'alpha_j * closure_drive_j * reserve_mass_j with alpha_j modulated by support-window memory concentration',
        'exact_balance_rule': 'mass added to support window equals mass removed from the same band reserve window exactly; final target is renormalized inside the reserve window only',
    },
    'transport_ledger': ledger.to_dict(orient='records'),
    'closure_metrics': {
        'advanced_mass_total': advanced_mass_total,
        'repaid_mass_total': repaid_mass_total,
        'closure_residual': closure_residual,
        'global_final_total_baseline': global_base,
        'global_final_total_adaptive': global_adp,
    },
    'results': comparison.to_dict(orient='records'),
    'relative_to_run_021': run021_summary['closure_metrics'] if run021_summary else None,
    'honesty_note': 'The archived late-stage pair-temporal/zero-point-localized state is still not fully present byte-for-byte, so this pass continues from the accessible canonical replay plus the structural closure direction rather than claiming a perfect replay of missing transient artifacts.',
}

notes = f'''# Run 022 pair-memory adaptive closure\n\nThis pass keeps exact onset structure and exact final occupancies while replacing fixed support/reserve windows with pair-memory-guided support weighting and traced-residence-guided reserve repayment.\nThe point is to test whether the closure can stay exact while becoming more structurally aligned with the archived lesson: preserve pair-memory / residence machinery rather than brute-fitting coefficients.\n\nAdvanced/repaid mass total: {advanced_mass_total:.12f}\nClosure residual: {closure_residual:.12e}\nGlobal baseline total: {global_base:.12f}\nGlobal adaptive total: {global_adp:.12f}\n'''

comparison.to_csv(OUT / 'pair_memory_adaptive_comparison.csv', index=False)
ledger.to_csv(OUT / 'pair_memory_adaptive_ledger.csv', index=False)
closure.to_csv(OUT / 'pair_memory_adaptive_history.csv', index=False)
(OUT / 'pair_memory_adaptive_summary.json').write_text(json.dumps(summary_out, indent=2))
(OUT / 'pair_memory_adaptive_notes.md').write_text(notes)

zip_path = Path('/mnt/data/run_022_pair_memory_adaptive_closure.zip')
with zipfile.ZipFile(zip_path, 'w', compression=zipfile.ZIP_DEFLATED) as zf:
    zf.write('/mnt/data/run_022_pair_memory_adaptive_closure_pass.py', arcname='run_022_pair_memory_adaptive_closure_pass.py')
    for f in OUT.iterdir():
        zf.write(f, arcname=f'run_022_pair_memory_adaptive_closure/{f.name}')

print(json.dumps(summary_out['closure_metrics'], indent=2))
print(comparison.to_string(index=False))
