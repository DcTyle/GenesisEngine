param(
    [string]$BuildRoot = "out/build/win64-vs2022-clang-vulkan",
    [string]$RunDir = "ResearchConfinement/Run43",
    [int]$PacketCount = 24,
    [int]$BinCount = 96,
    [int]$Steps = 96,
    [int]$ReconSamples = 256,
    [int]$EquivalentGridLinear = 1024
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Clamp01 {
    param([double]$Value)
    if ($Value -lt 0.0) { return 0.0 }
    if ($Value -gt 1.0) { return 1.0 }
    return $Value
}

function Read-JsonNoComment {
    param([Parameter(Mandatory = $true)][string]$Path)
    $raw = Get-Content -Path $Path -Raw -Encoding UTF8
    $clean = [regex]::Replace($raw, "^\s*//[^\r\n]*\r?\n", "")
    return $clean | ConvertFrom-Json
}

function Get-Mean {
    param([double[]]$Values)
    if ($null -eq $Values -or $Values.Count -eq 0) { return 0.0 }
    return ($Values | Measure-Object -Average).Average
}

function Get-Rmse {
    param([double[]]$Values)
    if ($null -eq $Values -or $Values.Count -eq 0) { return 0.0 }
    $accum = 0.0
    foreach ($v in $Values) {
        $accum += $v * $v
    }
    return [Math]::Sqrt($accum / $Values.Count)
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildAbs = Join-Path $repoRoot $BuildRoot
$runAbs = if ([System.IO.Path]::IsPathRooted($RunDir)) { $RunDir } else { Join-Path $repoRoot $RunDir }
$sweepDir = Join-Path $runAbs "sweep"
$validationDir = Join-Path $runAbs "validation"
New-Item -ItemType Directory -Path $runAbs -Force | Out-Null
New-Item -ItemType Directory -Path $sweepDir -Force | Out-Null
New-Item -ItemType Directory -Path $validationDir -Force | Out-Null

$exeCandidates = @(
    (Join-Path $buildAbs "Release/photon_frequency_domain_sim.exe"),
    (Join-Path $buildAbs "Debug/photon_frequency_domain_sim.exe"),
    (Join-Path $buildAbs "photon_frequency_domain_sim.exe")
)
$exe = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    throw "photon_frequency_domain_sim.exe not found under $buildAbs"
}

$center = [ordered]@{
    F = 0.245
    A = 0.18
    I = 0.33
    V = 0.33
}
$delta = [ordered]@{
    F = 0.01
    A = 0.02
    I = 0.03
    V = 0.03
}
$vars = @("F", "A", "I", "V")

function New-RunResult {
    param(
        [string]$Tag,
        [double]$F,
        [double]$A,
        [double]$I,
        [double]$V,
        [string]$OutDir,
        [object]$Summary,
        [object]$Pulse
    )
    $trap = [double]$Pulse.field_response.low_frequency_trap_ratio
    $coherence = [double]$Pulse.field_response.mean_temporal_coherence
    $inertia = [double]$Pulse.field_response.mean_temporal_inertia
    $curvature = [double]$Pulse.field_response.mean_curvature
    $interference = $trap * $coherence * (1.0 + (1000.0 * $inertia)) / (1.0 + ($curvature / 50.0))
    return [pscustomobject]@{
        tag = $Tag
        F = $F
        A = $A
        I = $I
        V = $V
        silicon_score = [double]$Summary.aggregate_metrics.silicon_reproduction_score
        trap_ratio = $trap
        coherence = $coherence
        inertia = $inertia
        curvature = $curvature
        interference_gain = $interference
        output_dir = $OutDir
    }
}

function Invoke-PhotonRun {
    param(
        [string]$Tag,
        [double]$F,
        [double]$A,
        [double]$I,
        [double]$V,
        [string]$BaseDir
    )
    $outDir = Join-Path $BaseDir $Tag
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    & $exe `
        --packet-count $PacketCount `
        --bin-count $BinCount `
        --steps $Steps `
        --recon-samples $ReconSamples `
        --equivalent-grid-linear $EquivalentGridLinear `
        --pulse-frequency $F `
        --pulse-amplitude $A `
        --pulse-amperage $I `
        --pulse-voltage $V `
        --output-dir $outDir | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Simulation run failed for tag=$Tag"
    }
    $summary = Read-JsonNoComment -Path (Join-Path $outDir "frequency_domain_run_summary.json")
    $pulse = Read-JsonNoComment -Path (Join-Path $outDir "photon_pulse_effects_sample.json")
    return New-RunResult -Tag $Tag -F $F -A $A -I $I -V $V -OutDir $outDir -Summary $summary -Pulse $pulse
}

function Build-Model {
    param(
        [hashtable]$ResultsByTag,
        [hashtable]$Center,
        [hashtable]$Delta,
        [string]$MetricName
    )
    $f0 = [double]$ResultsByTag["C"].$MetricName
    $jacobian = [ordered]@{}
    $hdiag = [ordered]@{}
    $hcross = [ordered]@{}

    foreach ($v in $vars) {
        $fp = [double]$ResultsByTag["$($v)p"].$MetricName
        $fm = [double]$ResultsByTag["$($v)m"].$MetricName
        $d = [double]$Delta[$v]
        $jacobian[$v] = ($fp - $fm) / (2.0 * $d)
        $hdiag[$v] = ($fp - (2.0 * $f0) + $fm) / ($d * $d)
    }

    for ($i = 0; $i -lt $vars.Count; $i++) {
        for ($j = $i + 1; $j -lt $vars.Count; $j++) {
            $v1 = $vars[$i]
            $v2 = $vars[$j]
            $fpp = [double]$ResultsByTag["$($v1)p$($v2)p"].$MetricName
            $fpm = [double]$ResultsByTag["$($v1)p$($v2)m"].$MetricName
            $fmp = [double]$ResultsByTag["$($v1)m$($v2)p"].$MetricName
            $fmm = [double]$ResultsByTag["$($v1)m$($v2)m"].$MetricName
            $d1 = [double]$Delta[$v1]
            $d2 = [double]$Delta[$v2]
            $hcross["$v1$v2"] = ($fpp - $fpm - $fmp + $fmm) / (4.0 * $d1 * $d2)
        }
    }

    return [ordered]@{
        metric = $MetricName
        center_value = $f0
        jacobian = $jacobian
        hessian_diag = $hdiag
        hessian_cross = $hcross
        deviation_formula = "f(x) ~= f0 + sum(J_i*dx_i) + 0.5*sum(H_ii*dx_i^2) + sum(H_ij*dx_i*dx_j)"
    }
}

function Predict-FromModel {
    param(
        [object]$Model,
        [hashtable]$Center,
        [double]$F,
        [double]$A,
        [double]$I,
        [double]$Vpulse
    )
    $dF = $F - [double]$Center["F"]
    $dA = $A - [double]$Center["A"]
    $dI = $I - [double]$Center["I"]
    $dV = $Vpulse - [double]$Center["V"]
    $deltaVec = [ordered]@{ F = $dF; A = $dA; I = $dI; V = $dV }

    $pred = [double]$Model.center_value
    foreach ($v in $vars) {
        $j = [double]($Model.jacobian.$v)
        $h = [double]($Model.hessian_diag.$v)
        $dx = [double]($deltaVec[$v])
        $pred += $j * $dx
        $pred += 0.5 * $h * $dx * $dx
    }

    $pairs = @("FA", "FI", "FV", "AI", "AV", "IV")
    foreach ($pair in $pairs) {
        $v1 = $pair.Substring(0, 1)
        $v2 = $pair.Substring(1, 1)
        $hc = [double]($Model.hessian_cross.$pair)
        $dx1 = [double]($deltaVec[$v1])
        $dx2 = [double]($deltaVec[$v2])
        $pred += $hc * $dx1 * $dx2
    }
    return $pred
}

# Sweep runs for central finite-difference model.
$resultsByTag = @{}
$resultsList = New-Object System.Collections.Generic.List[object]

$centerRun = Invoke-PhotonRun -Tag "C" -F $center.F -A $center.A -I $center.I -V $center.V -BaseDir $sweepDir
$resultsByTag["C"] = $centerRun
[void]$resultsList.Add($centerRun)

foreach ($v in $vars) {
    $plus = [ordered]@{ F = $center.F; A = $center.A; I = $center.I; V = $center.V }
    $minus = [ordered]@{ F = $center.F; A = $center.A; I = $center.I; V = $center.V }
    $plus[$v] = Clamp01 ([double]$center[$v] + [double]$delta[$v])
    $minus[$v] = Clamp01 ([double]$center[$v] - [double]$delta[$v])

    $tagP = "$($v)p"
    $tagM = "$($v)m"
    $runP = Invoke-PhotonRun -Tag $tagP -F $plus.F -A $plus.A -I $plus.I -V $plus.V -BaseDir $sweepDir
    $runM = Invoke-PhotonRun -Tag $tagM -F $minus.F -A $minus.A -I $minus.I -V $minus.V -BaseDir $sweepDir
    $resultsByTag[$tagP] = $runP
    $resultsByTag[$tagM] = $runM
    [void]$resultsList.Add($runP)
    [void]$resultsList.Add($runM)
}

for ($i = 0; $i -lt $vars.Count; $i++) {
    for ($j = $i + 1; $j -lt $vars.Count; $j++) {
        $v1 = $vars[$i]
        $v2 = $vars[$j]
        $signs = @(
            @{ s1 = 1; s2 = 1; tag = "$($v1)p$($v2)p" },
            @{ s1 = 1; s2 = -1; tag = "$($v1)p$($v2)m" },
            @{ s1 = -1; s2 = 1; tag = "$($v1)m$($v2)p" },
            @{ s1 = -1; s2 = -1; tag = "$($v1)m$($v2)m" }
        )
        foreach ($s in $signs) {
            $p = [ordered]@{ F = $center.F; A = $center.A; I = $center.I; V = $center.V }
            $p[$v1] = Clamp01 ([double]$center[$v1] + ([double]$s.s1 * [double]$delta[$v1]))
            $p[$v2] = Clamp01 ([double]$center[$v2] + ([double]$s.s2 * [double]$delta[$v2]))
            $run = Invoke-PhotonRun -Tag ([string]$s.tag) -F $p.F -A $p.A -I $p.I -V $p.V -BaseDir $sweepDir
            $resultsByTag[[string]$s.tag] = $run
            [void]$resultsList.Add($run)
        }
    }
}

$models = [ordered]@{
    silicon_score = Build-Model -ResultsByTag $resultsByTag -Center $center -Delta $delta -MetricName "silicon_score"
    trap_ratio = Build-Model -ResultsByTag $resultsByTag -Center $center -Delta $delta -MetricName "trap_ratio"
    coherence = Build-Model -ResultsByTag $resultsByTag -Center $center -Delta $delta -MetricName "coherence"
    inertia = Build-Model -ResultsByTag $resultsByTag -Center $center -Delta $delta -MetricName "inertia"
    curvature = Build-Model -ResultsByTag $resultsByTag -Center $center -Delta $delta -MetricName "curvature"
}

# Validation runs on unseen offsets inside the local neighborhood.
$validationOffsets = @(
    @{ name = "V1"; F = 0.50; A = 0.50; I = 0.50; V = 0.50 },
    @{ name = "V2"; F = -0.50; A = 0.25; I = -0.75; V = 0.50 },
    @{ name = "V3"; F = 0.25; A = -0.50; I = 0.50; V = -0.50 },
    @{ name = "V4"; F = -0.75; A = -0.25; I = 0.50; V = 0.25 },
    @{ name = "V5"; F = 0.00; A = 0.75; I = -0.50; V = -0.25 },
    @{ name = "V6"; F = 0.60; A = -0.60; I = -0.40; V = 0.20 },
    @{ name = "V7"; F = -0.30; A = 0.40; I = 0.70; V = -0.60 },
    @{ name = "V8"; F = 0.10; A = -0.10; I = -0.20; V = 0.80 }
)

$validationRows = New-Object System.Collections.Generic.List[object]
foreach ($vo in $validationOffsets) {
    $F = Clamp01 ([double]$center.F + ([double]$vo.F * [double]$delta.F))
    $A = Clamp01 ([double]$center.A + ([double]$vo.A * [double]$delta.A))
    $I = Clamp01 ([double]$center.I + ([double]$vo.I * [double]$delta.I))
    $V = Clamp01 ([double]$center.V + ([double]$vo.V * [double]$delta.V))
    $actual = Invoke-PhotonRun -Tag ([string]$vo.name) -F $F -A $A -I $I -V $V -BaseDir $validationDir

    $predScore = Predict-FromModel -Model $models.silicon_score -Center $center -F $F -A $A -I $I -Vpulse $V
    $predTrap = Predict-FromModel -Model $models.trap_ratio -Center $center -F $F -A $A -I $I -Vpulse $V
    $predCoh = Predict-FromModel -Model $models.coherence -Center $center -F $F -A $A -I $I -Vpulse $V
    $predInertia = Predict-FromModel -Model $models.inertia -Center $center -F $F -A $A -I $I -Vpulse $V
    $predCurvature = Predict-FromModel -Model $models.curvature -Center $center -F $F -A $A -I $I -Vpulse $V

    $validationRows.Add([pscustomobject]@{
        tag = $actual.tag
        F = $F
        A = $A
        I = $I
        V = $V
        actual_silicon_score = $actual.silicon_score
        predicted_silicon_score = Clamp01 $predScore
        abs_error_silicon_score = [Math]::Abs($actual.silicon_score - (Clamp01 $predScore))
        actual_trap_ratio = $actual.trap_ratio
        predicted_trap_ratio = $predTrap
        actual_coherence = $actual.coherence
        predicted_coherence = $predCoh
        actual_inertia = $actual.inertia
        predicted_inertia = $predInertia
        actual_curvature = $actual.curvature
        predicted_curvature = $predCurvature
    })
}

$sweepCsv = @("tag,F,A,I,V,silicon_score,trap_ratio,coherence,inertia,curvature,interference_gain,output_dir")
foreach ($row in $resultsList | Sort-Object tag) {
    $sweepCsv += "{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11}" -f `
        $row.tag, $row.F, $row.A, $row.I, $row.V, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.silicon_score)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.trap_ratio)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.coherence)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.inertia)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.curvature)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.interference_gain)), `
        $row.output_dir
}
Set-Content -Path (Join-Path $runAbs "run_043_sweep_results.csv") -Value $sweepCsv -Encoding UTF8

$valCsv = @("tag,F,A,I,V,actual_silicon_score,predicted_silicon_score,abs_error_silicon_score,actual_trap_ratio,predicted_trap_ratio,actual_coherence,predicted_coherence,actual_inertia,predicted_inertia,actual_curvature,predicted_curvature")
foreach ($row in $validationRows) {
    $valCsv += "{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13},{14},{15}" -f `
        $row.tag, $row.F, $row.A, $row.I, $row.V, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.actual_silicon_score)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.predicted_silicon_score)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.abs_error_silicon_score)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.actual_trap_ratio)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.predicted_trap_ratio)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.actual_coherence)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.predicted_coherence)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.actual_inertia)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.predicted_inertia)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.actual_curvature)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.predicted_curvature))
}
Set-Content -Path (Join-Path $runAbs "run_043_validation_results.csv") -Value $valCsv -Encoding UTF8

$scoreErrors = @($validationRows | ForEach-Object { [double]$_.abs_error_silicon_score })
$maeScore = Get-Mean -Values $scoreErrors
$rmseScore = Get-Rmse -Values $scoreErrors

$equationMd = @"
# Run 043 - Silicon Lattice Pulse Interference Equation

This run derives a local quadratic equation for silicon-lattice simulation response from GPU pulse controls using temporal confinement dynamics.

Center quartet:
- F = $($center.F)
- A = $($center.A)
- I = $($center.I)
- V = $($center.V)

Step widths:
- dF = $($delta.F)
- dA = $($delta.A)
- dI = $($delta.I)
- dV = $($delta.V)

Equation form:
`S_hat = S0 + sum(J_i * dx_i) + 0.5 * sum(H_ii * dx_i^2) + sum(H_ij * dx_i * dx_j)`

Where:
- `S_hat` is predicted silicon-lattice score
- `dx` are offsets from center quartet
- `J` and `H` are finite-difference derivatives from sweep runs

Silicon score coefficients:
- S0 = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.center_value))
- J_F = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.jacobian.F))
- J_A = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.jacobian.A))
- J_I = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.jacobian.I))
- J_V = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.jacobian.V))
- H_FF = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.hessian_diag.F))
- H_AA = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.hessian_diag.A))
- H_II = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.hessian_diag.I))
- H_VV = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.hessian_diag.V))
- H_FA = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.hessian_cross.FA))
- H_FI = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.hessian_cross.FI))
- H_FV = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.hessian_cross.FV))
- H_AI = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.hessian_cross.AI))
- H_AV = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.hessian_cross.AV))
- H_IV = $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", [double]$models.silicon_score.hessian_cross.IV))

Validation:
- points tested: $($validationRows.Count)
- MAE (silicon score): $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $maeScore))
- RMSE (silicon score): $([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $rmseScore))

Temporal confinement interpretation:
- Interference is measured through coupled trap/coherence/inertia/curvature outputs from each run.
- The equation is derived from those dynamic responses, not static curve fitting to external target constants.
"@
Set-Content -Path (Join-Path $runAbs "run_043_equation.md") -Value $equationMd -Encoding UTF8

$summary = [ordered]@{
    run_name = "run_043_silicon_pulse_interference_equation"
    generated_utc = [DateTime]::UtcNow.ToString("o")
    sim_config = [ordered]@{
        packet_count = $PacketCount
        bin_count = $BinCount
        steps = $Steps
        recon_samples = $ReconSamples
        equivalent_grid_linear = $EquivalentGridLinear
    }
    center_quartet = $center
    delta_quartet = $delta
    sweep_runs = $resultsList.Count
    validation_runs = $validationRows.Count
    models = $models
    validation_error = [ordered]@{
        silicon_score_mae = $maeScore
        silicon_score_rmse = $rmseScore
    }
    artifacts = [ordered]@{
        sweep_results_csv = "run_043_sweep_results.csv"
        validation_results_csv = "run_043_validation_results.csv"
        equation_markdown = "run_043_equation.md"
        sweep_dir = "sweep"
        validation_dir = "validation"
    }
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -Path (Join-Path $runAbs "run_043_derivation_summary.json") -Encoding UTF8

$notes = @"
# Run 043 Notes

Purpose:
- derive and test a silicon-lattice equation from GPU pulse interference patterns under temporal confinement dynamics.

What was run:
- 33-point finite-difference sweep around quartet center (C, +/- single-axis, +/- pair-axis combinations).
- 8 unseen validation points inside the local neighborhood.

Primary outputs:
- [run_043_derivation_summary.json](./run_043_derivation_summary.json)
- [run_043_equation.md](./run_043_equation.md)
- [run_043_sweep_results.csv](./run_043_sweep_results.csv)
- [run_043_validation_results.csv](./run_043_validation_results.csv)
"@
Set-Content -Path (Join-Path $runAbs "run_043_notes.md") -Value $notes -Encoding UTF8

Write-Host "Run43 derivation complete: $runAbs"
Write-Host ("silicon_score_mae={0} rmse={1}" -f `
    ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $maeScore)), `
    ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $rmseScore)))
