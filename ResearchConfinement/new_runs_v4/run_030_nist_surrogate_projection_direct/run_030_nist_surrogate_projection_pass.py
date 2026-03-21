
import json, math, pandas as pd, os

base='/mnt/data/run_029_temporal_alignment_probability_collapse'
hist=pd.read_csv(f'{base}/probability_collapse_history.csv')
summary=json.load(open(f'{base}/probability_collapse_summary.json'))

results=summary['results']
r5=next(r for r in results if r['radius_bin']==5)
r6=next(r for r in results if r['radius_bin']==6)

# NIST surrogate wavelengths from the NIST lithium strong-lines page (air wavelengths, Angstrom)
lam_D1_A=6707.926
lam_D2_A=6707.775
c=299792458.0
f_D1=c/(lam_D1_A*1e-10)
f_D2=c/(lam_D2_A*1e-10)
sep=abs(f_D2-f_D1)

step_sep=r6['onset_target']-r5['onset_target']
mhz_per_step=sep/step_sep/1e6

rel_unc=3e-11
abs_unc=((f_D1+f_D2)/2)*rel_unc
comb_acc=((f_D1+f_D2)/2)*3e-13
natural_width=5.87e6

outdir='/mnt/data/run_030_nist_surrogate_projection'
os.makedirs(outdir, exist_ok=True)

df=pd.DataFrame({
    'quantity': [
        'D1 wavelength (A)', 'D2 wavelength (A)', 'D-line separation (GHz)',
        'Toy onset separation (steps)', 'Frequency scale (MHz/step)',
        'Approx absolute uncertainty from 3e-11 (kHz)', 'Comb tie scale (kHz)',
        'Natural linewidth (MHz)', 'r5 lock offset (steps)', 'r6 lock offset (steps)'
    ],
    'value': [
        lam_D1_A, lam_D2_A, sep/1e9, step_sep, mhz_per_step,
        abs_unc/1e3, comb_acc/1e3, natural_width/1e6,
        r5['threshold_cross_offset_from_onset'], r6['threshold_cross_offset_from_onset']
    ]
})
df.to_csv(f'{outdir}/nist_surrogate_summary.csv', index=False)
print(df)
