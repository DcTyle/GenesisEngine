# Run 042 Observation Diagram

```mermaid
flowchart TD
    A[Run41 Cohort Seeds<br/>D_track I_accum L_smooth] --> B[Packet Frequency Spectra<br/>50 packets x 128 bins]
    B --> C[Temporal Coupling Update<br/>phase + amplitude + leakage]
    C --> D[Path Classification<br/>shared vs individual]
    C --> E[6DoF Tensor / Curvature / Inertia]
    C --> F[IFFT Reconstruction<br/>trajectory paths]
    D --> G[Per-Task Summaries]
    E --> G
    F --> G
    G --> H[Run42 Formal Review]
```

## Legend
- shared: phase-locked packets above dynamic lock threshold
- individual: packets outside the shared lock threshold
- recurrence alignment: dominant lag correlation over cohort lattice-distance series
