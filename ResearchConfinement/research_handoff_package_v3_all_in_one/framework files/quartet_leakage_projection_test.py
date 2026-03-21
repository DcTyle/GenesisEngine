import math, json, os
import numpy as np
import pandas as pd

exp = pd.read_csv('/mnt/data/full_exp/research_handoff_package/artifacts/lithium_abszero_effective_exposure_summary.csv').set_index('radius_bin')
pair = pd.read_csv('/mnt/data/full_exp/research_handoff_package/artifacts/lithium_abszero_timinglaw_pair_channels.csv')

for r in exp.index:
    exp.loc[r, 'T_eff'] = exp.loc[r, 'h_eff_mean'] / exp.loc[2, 'h_eff_mean']
    exp.loc[r, 'X_eff'] = exp.loc[r, 'c_eff_mean'] / exp.loc[2, 'c_eff_mean']
    exp.loc[r, 'T_dopp'] = exp.loc[r, 'h_eff_doppler_mean'] / exp.loc[r, 'h_eff_mean']
    exp.loc[r, 'X_dopp'] = exp.loc[r, 'c_eff_doppler_mean'] / exp.loc[r, 'c_eff_mean']
    exp.loc[r, 'gamma_ts'] = math.sqrt((exp.loc[r, 'T_dopp'] / exp.loc[r, 'T_eff']) * (exp.loc[r, 'X_dopp'] / exp.loc[r, 'X_eff']))
    exp.loc[r, 'eta_def'] = exp.loc[r, 'gamma_ts'] * (1.0 - exp.loc[r, 'Rel_i_calc_mean'])

pair_map = {}
for _, row in pair.iterrows():
    i, j = map(int, row['pair'].split('-'))
    pair_map[(i, j)] = row


def pair_support(i: int, j: int) -> float:
    row = pair_map[(i, j)]
    return float((row['Rel_ij'] ** 2) * row['O_ij'] * row['R_ij'] * row['Cgate_ij'] * row['Tpair_base_ij'] * row['Tflux_ij'])


def normalize(vec):
    arr = np.array(vec, dtype=float)
    s = arr.sum()
    return arr / s if s != 0 else np.ones_like(arr) / len(arr)


def gaussian_occ(r: float, mu: float, sigma: float) -> float:
    return math.exp(-0.5 * ((r - mu) / sigma) ** 2)


S26 = pair_support(2, 6)
S56 = pair_support(5, 6)

bands = {}
for r in [2, 5, 6]:
    row = exp.loc[r]
    bands[r] = dict(
        eta=float(row['eta_def']),
        rel=float(row['Rel_i_calc_mean']),
        gamma=float(row['gamma_ts']),
        xret=float(row['Xret_eff_mean']),
        curv=float(row['curvature_mean']),
        aeth=float(row['aether_mean']),
        coh=float(row['coherence_native_mean']),
    )

inner_axis = normalize([bands[2]['curv'], bands[2]['aeth'], bands[2]['coh']])
out6_axis = normalize([bands[6]['curv'], bands[6]['aeth'], bands[6]['coh']])
out5_axis = normalize([bands[5]['curv'], bands[5]['aeth'], bands[5]['coh']])
axis_tilt = out6_axis / inner_axis
axis_tilt = axis_tilt / axis_tilt.mean()
ret_tilt = np.sqrt(inner_axis / (inner_axis + out6_axis))
ret_tilt = ret_tilt / ret_tilt.mean()

mix26_axis = normalize((np.array([bands[2]['curv'], bands[2]['aeth'], bands[2]['coh']]) + np.array([bands[6]['curv'], bands[6]['aeth'], bands[6]['coh']])) / 2.0)
mix56_axis = normalize((np.array([bands[5]['curv'], bands[5]['aeth'], bands[5]['coh']]) + np.array([bands[6]['curv'], bands[6]['aeth'], bands[6]['coh']])) / 2.0)

transport26 = float(mix26_axis @ axis_tilt)
transport56 = float(mix56_axis @ axis_tilt)
retention26 = float(mix26_axis @ ret_tilt)
retention56 = float(mix56_axis @ ret_tilt)


def simulate(mode: str = 'control', steps: int = 180, p=None):
    if p is None:
        p = {}
    r = np.array([1.38, 2.15, 8.6], dtype=float)
    v = np.zeros(3, dtype=float)
    base_target = np.array([2.0, 2.18, 6.30], dtype=float)
    rows = []

    for t in range(steps):
        n_r2 = gaussian_occ(r[0], 2.0, 0.35) + gaussian_occ(r[1], 2.0, 0.35)
        n_r5 = gaussian_occ(r[2], 5.0, 0.55)
        n_r6 = gaussian_occ(r[2], 6.0, 0.60)
        inner_occ = min(n_r2 / 1.8, 1.3)
        p26 = S26 * inner_occ
        p56 = S56 * max(n_r5, 0.05)
        pair_outer = (0.6 * S26 + 0.4 * S56) * inner_occ

        f = np.zeros(3, dtype=float)

        # Hybrid split nucleus, but scalar on purpose.
        id_gain = p.get('id_gain', 0.055)
        env_gain = p.get('env_gain', 0.060)
        soft_gain = p.get('soft_gain', 0.028)
        outer_shift_gain = p.get('outer_shift_gain', 0.95)
        outer_drive_gain = p.get('outer_drive_gain', 0.055)
        rep_gain = p.get('rep_gain', 0.14)

        f += id_gain * (base_target - r)
        inner_soft = soft_gain * bands[2]['eta'] * (1.0 - inner_occ * 0.2)
        f[0] += inner_soft * (1.96 - r[0])
        f[1] += inner_soft * (2.23 - r[1])

        outer_shift = outer_shift_gain * bands[6]['eta'] * pair_outer * (1.0 + 0.5 * (bands[6]['gamma'] - 1.0)) * 20.0
        outer_target = 6.05 + outer_shift
        outer_strength = env_gain * (0.55 + 0.8 * bands[6]['rel'] + 0.6 * pair_outer)
        path_mix = 1.0
        support_gain_26 = 1.0
        support_gain_56 = 1.0
        rep_gain_13 = 1.0
        rep_gain_23 = 1.0

        if mode == 'quartet_projected':
            a = p.get('a', 0.2)
            b = p.get('b', 0.7)
            c = p.get('c', 0.2)
            g26 = 1.0 + a * (transport26 - 1.0) + b * (retention26 - 1.0)
            g56 = 1.0 + a * (transport56 - 1.0) + b * (retention56 - 1.0)
            path_mix = float((p26 * g26 + p56 * g56) / max(p26 + p56, 1e-12))
            support_gain_26 = 1.0 + c * (g26 - 1.0)
            support_gain_56 = 1.0 + c * (g56 - 1.0)
            rep_gain_13 = 1.0 + 0.5 * (g26 - 1.0)
            rep_gain_23 = 1.0 + 0.5 * (g56 - 1.0)

        f[2] += outer_strength * path_mix * (outer_target - r[2])
        f[2] += outer_drive_gain * ((0.6 * p26 * support_gain_26) + (0.4 * p56 * support_gain_56)) * (1.0 + bands[6]['eta'])
        f[0] -= 0.30 * outer_drive_gain * p26 * support_gain_26
        f[1] -= 0.22 * outer_drive_gain * p56 * support_gain_56

        for i in range(3):
            for j in range(i + 1, 3):
                sep = r[j] - r[i]
                sgn = 1.0 if sep >= 0 else -1.0
                dist = abs(sep) + 1e-3
                ei = bands[2]['eta'] if i < 2 else bands[6]['eta']
                ej = bands[2]['eta'] if j < 2 else bands[6]['eta']
                xret = bands[2]['xret'] if j < 2 else bands[6]['xret']
                leak_pair = 0.5 * (ei + ej)
                falloff = 1.0 / (1.0 + dist / (1.0 + 1000.0 * max(xret, 1e-6)))
                rep = rep_gain * leak_pair * falloff / (dist ** 1.08)
                if mode == 'quartet_projected':
                    if (i, j) == (0, 2):
                        rep *= rep_gain_13
                    elif (i, j) == (1, 2):
                        rep *= rep_gain_23
                f[i] -= sgn * rep
                f[j] += sgn * rep

        v = 0.89 * v + f
        r = np.sort(r + 0.11 * v)
        rows.append(dict(
            step=t,
            r_e1=r[0], r_e2=r[1], r_e3=r[2],
            n_r2=n_r2, n_r5=n_r5, n_r6=n_r6,
            inner_occ=inner_occ,
            p26=p26, p56=p56,
            outer_target=outer_target,
            outer_strength=outer_strength,
            path_mix=path_mix,
            support_gain_26=support_gain_26,
            support_gain_56=support_gain_56,
            rep_gain_13=rep_gain_13,
            rep_gain_23=rep_gain_23,
        ))

    df = pd.DataFrame(rows)
    arr = df['n_r6'].to_numpy()
    first = None
    for i in range(len(arr) - 4):
        if np.all(arr[i:i+5] > 0.2):
            first = i
            break
    summary = dict(
        model=mode,
        r6_first_persistent_gt_0p2=int(first) if first is not None else None,
        final_n_r2=float(df['n_r2'].iloc[-1]),
        final_n_r6=float(df['n_r6'].iloc[-1]),
        final_r6_over_r2=float(df['n_r6'].iloc[-1] / max(df['n_r2'].iloc[-1], 1e-12)),
        final_radii=[float(df['r_e1'].iloc[-1]), float(df['r_e2'].iloc[-1]), float(df['r_e3'].iloc[-1])],
        final_path_mix=float(df['path_mix'].iloc[-1]),
        final_support_gain_26=float(df['support_gain_26'].iloc[-1]),
        final_support_gain_56=float(df['support_gain_56'].iloc[-1]),
        final_rep_gain_13=float(df['rep_gain_13'].iloc[-1]),
        final_rep_gain_23=float(df['rep_gain_23'].iloc[-1]),
    )
    return df, summary


outdir = '/mnt/data/quartet_projection_hybrid_test'
os.makedirs(outdir, exist_ok=True)

control_df, control_summary = simulate('control')
quartet_df, quartet_summary = simulate('quartet_projected')

control_df.to_csv(os.path.join(outdir, 'sim1_hybrid_scalar_control_history.csv'), index=False)
quartet_df.to_csv(os.path.join(outdir, 'sim2_quartet_projected_history.csv'), index=False)
pd.DataFrame([control_summary, quartet_summary]).to_csv(os.path.join(outdir, 'quartet_projection_hybrid_comparison.csv'), index=False)

pd.DataFrame([
    {'channel': 'inner_axis_x', 'value': inner_axis[0]},
    {'channel': 'inner_axis_y', 'value': inner_axis[1]},
    {'channel': 'inner_axis_z', 'value': inner_axis[2]},
    {'channel': 'outer6_axis_x', 'value': out6_axis[0]},
    {'channel': 'outer6_axis_y', 'value': out6_axis[1]},
    {'channel': 'outer6_axis_z', 'value': out6_axis[2]},
    {'channel': 'axis_tilt_x', 'value': axis_tilt[0]},
    {'channel': 'axis_tilt_y', 'value': axis_tilt[1]},
    {'channel': 'axis_tilt_z', 'value': axis_tilt[2]},
    {'channel': 'ret_tilt_x', 'value': ret_tilt[0]},
    {'channel': 'ret_tilt_y', 'value': ret_tilt[1]},
    {'channel': 'ret_tilt_z', 'value': ret_tilt[2]},
    {'channel': 'transport26', 'value': transport26},
    {'channel': 'transport56', 'value': transport56},
    {'channel': 'retention26', 'value': retention26},
    {'channel': 'retention56', 'value': retention56},
    {'channel': 'projection_a', 'value': 0.2},
    {'channel': 'projection_b', 'value': 0.7},
    {'channel': 'projection_c', 'value': 0.2},
]).to_csv(os.path.join(outdir, 'quartet_projection_channels.csv'), index=False)

with open(os.path.join(outdir, 'quartet_projection_hybrid_summary.json'), 'w') as f:
    json.dump({
        'control_hybrid_scalar': control_summary,
        'quartet_projection_hybrid': quartet_summary,
        'interpretation': 'Axis resolution was moved out of the softer nucleus pull and into pair-specific quartet leakage/retention projection. The hybrid split nucleus stayed scalar. This improves final outer/inner balance slightly but does not materially improve outer onset, suggesting the remaining miss is still not solved by directional leakage projection alone.'
    }, f, indent=2)
