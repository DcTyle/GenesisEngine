# Run 043 - Silicon Lattice Pulse Interference Equation

This run derives a local quadratic equation for silicon-lattice simulation response from GPU pulse controls using temporal confinement dynamics.

Center quartet:
- F = 0.245
- A = 0.18
- I = 0.33
- V = 0.33

Step widths:
- dF = 0.01
- dA = 0.02
- dI = 0.03
- dV = 0.03

Equation form:
S_hat = S0 + sum(J_i * dx_i) + 0.5 * sum(H_ii * dx_i^2) + sum(H_ij * dx_i * dx_j)

Where:
- S_hat is predicted silicon-lattice score
- dx are offsets from center quartet
- J and H are finite-difference derivatives from sweep runs

Silicon score coefficients:
- S0 = 0.713922
- J_F = -0.023249999999996884
- J_A = 0.026349999999999985
- J_I = -0.083466666666666883
- J_V = -0.034700000000000474
- H_FF = 0.01000000000139778
- H_AA = 0.0050000000004213341
- H_II = 0.0088888888890211373
- H_VV = 0.0022222222224094817
- H_FA = -0.0012499999998971667
- H_FI = 0.039166666666682712
- H_FV = -0.00083333333335729642
- H_AI = 0.0033333333333366673
- H_AV = 0
- H_IV = 0

Validation:
- points tested: 8
- MAE (silicon score): 2.4164687499980686E-05
- RMSE (silicon score): 2.7668259299314168E-05

Temporal confinement interpretation:
- Interference is measured through coupled trap/coherence/inertia/curvature outputs from each run.
- The equation is derived from those dynamic responses, not static curve fitting to external target constants.
