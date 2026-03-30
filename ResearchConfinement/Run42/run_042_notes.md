# Run 042 - Frequency-domain temporal coupling extension

This run extends the Run40/Run41 research sequence with a packet-frequency-only simulation pass.

It preserves the Run41 cohort seeds (`D_track`, `I_accum`, `L_smooth`) and executes a higher-resolution temporal coupling pass against the NIST silicon wafer surrogate.

## Tasks
- D_track
- I_accum
- L_smooth

## Primary artifacts
- sim output: [sim_output](./sim_output)
- aggregate summary: [run_042_summary.json](./run_042_summary.json)
- per-task summaries: [all_task_frequency_domain_summary.csv](./all_task_frequency_domain_summary.csv)
- formal review: [run_042_formal_review.md](./run_042_formal_review.md)
