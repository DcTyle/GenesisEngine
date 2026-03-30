# Run 042 Lab Notes

- run id: `run_042_frequency_domain_temporal_coupling_extension`
- generated utc: 2026-03-30T02:53:33.2396346Z
- runtime command:
  - `photon_frequency_domain_sim.exe --packet-count 50 --bin-count 128 --steps 160 --recon-samples 512 --equivalent-grid-linear 2048 --pulse-frequency 0.245 --pulse-amplitude 0.18 --pulse-amperage 0.33 --pulse-voltage 0.33 --output-dir ResearchConfinement/Run42/sim_output`
- execution log: [run_042_execution.log](./run_042_execution.log)

## Core observations
- total packets: 50
- shared packets: 23
- individual packets: 27
- low-frequency trap ratio: 0.209172
- mean shared score: 0.997396
- silicon reproduction score: 0.684799

## Encoded control data
- encoded quartet (`f_code`, `a_code`, `i_code`, `v_code`): `(0.245, 0.18, 0.33, 0.33)`
- encoded data snapshot: [run_042_encoded_data.json](./run_042_encoded_data.json)
- equation stack: [run_042_equation_stack.md](./run_042_equation_stack.md)

## Collapse -> calculus product evidence (cross-talk + temporal feedback)
- collapse gate engagement: 0.98125
- calculus product mean (gate active): 0.94443
- calculus product peak: 0.979558
- time-series export: [run_042_collapse_calculus_timeseries.csv](./run_042_collapse_calculus_timeseries.csv)

## Certainty statement (model scope)
- model certainty score: 0.920435
- cross-talk stability: 0.999694
- conservation stability: 0.728359
- split-balance (shared vs individual): 0.92
- run42 composite mean: 0.656472
- run41 composite mean: 0.50542
- delta (run42 - run41): 0.151052
- certainty scope: model-internal operator certainty only; not direct physical proof.

## Data products
- trajectory logs (json/csv): `photon_packet_trajectory_sample.*`
- path classes (json/csv): `photon_packet_path_classification_sample.*`
- tensor + vector + shader + audio sample payloads: `photon_*_sample.json`
- metadata substrate payload: `photon_volume_expansion.gevsd`
