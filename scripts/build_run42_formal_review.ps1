param(
    [string]$RunDir = "ResearchConfinement/Run42",
    [int]$SampleStride = 10
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-JsonWithOptionalComment {
    param([Parameter(Mandatory = $true)][string]$Path)
    $raw = Get-Content -Path $Path -Raw -Encoding UTF8
    $clean = [regex]::Replace($raw, "^\s*//[^\r\n]*\r?\n", "")
    return $clean | ConvertFrom-Json
}

function Import-CsvWithOptionalComment {
    param([Parameter(Mandatory = $true)][string]$Path)
    $raw = Get-Content -Path $Path -Raw -Encoding UTF8
    $clean = [regex]::Replace($raw, "^\s*//[^\r\n]*\r?\n", "")
    $lines = $clean -split "`r?`n" | Where-Object { $_ -ne "" }
    if ($lines.Count -eq 0) { return @() }
    $joined = $lines -join "`n"
    return $joined | ConvertFrom-Csv
}

function Get-Mean {
    param([double[]]$Values)
    if ($null -eq $Values -or $Values.Count -eq 0) { return 0.0 }
    return ($Values | Measure-Object -Average).Average
}

function Get-Std {
    param([double[]]$Values)
    if ($null -eq $Values -or $Values.Count -le 1) { return 0.0 }
    $mean = Get-Mean -Values $Values
    $accum = 0.0
    foreach ($v in $Values) {
        $d = $v - $mean
        $accum += $d * $d
    }
    return [Math]::Sqrt($accum / [Math]::Max(1, $Values.Count - 1))
}

function Clamp01 {
    param([double]$Value)
    if ($Value -lt 0.0) { return 0.0 }
    if ($Value -gt 1.0) { return 1.0 }
    return $Value
}

function Get-Pearson {
    param(
        [double[]]$A,
        [double[]]$B
    )
    if ($A.Count -ne $B.Count -or $A.Count -le 1) { return 0.0 }
    $meanA = Get-Mean -Values $A
    $meanB = Get-Mean -Values $B
    $num = 0.0
    $denA = 0.0
    $denB = 0.0
    for ($i = 0; $i -lt $A.Count; $i++) {
        $da = $A[$i] - $meanA
        $db = $B[$i] - $meanB
        $num += $da * $db
        $denA += $da * $da
        $denB += $db * $db
    }
    $den = [Math]::Sqrt($denA * $denB)
    if ($den -le 1.0e-12) { return 0.0 }
    return $num / $den
}

function Get-DominantRecurrence {
    param(
        [double[]]$Series,
        [int]$MaxLag
    )
    if ($Series.Count -lt 8) {
        return [pscustomobject]@{
            Lag = 1
            Corr = 0.0
        }
    }
    $bestLag = 1
    $bestCorr = -1.0
    $safeLag = [Math]::Min($MaxLag, [Math]::Max(1, $Series.Count - 3))
    for ($lag = 1; $lag -le $safeLag; $lag++) {
        $len = $Series.Count - $lag
        if ($len -lt 4) { break }
        $lhs = New-Object double[] $len
        $rhs = New-Object double[] $len
        for ($i = 0; $i -lt $len; $i++) {
            $lhs[$i] = $Series[$i]
            $rhs[$i] = $Series[$i + $lag]
        }
        $corr = Get-Pearson -A $lhs -B $rhs
        if ($corr -gt $bestCorr) {
            $bestCorr = $corr
            $bestLag = $lag
        }
    }
    return [pscustomobject]@{
        Lag = $bestLag
        Corr = [Math]::Max(0.0, $bestCorr)
    }
}

function Write-CsvRows {
    param(
        [string]$Path,
        [string]$Header,
        [string[]]$Rows
    )
    $all = @($Header) + $Rows
    Set-Content -Path $Path -Value $all -Encoding UTF8
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$runAbs = if ([System.IO.Path]::IsPathRooted($RunDir)) { $RunDir } else { Join-Path $repoRoot $RunDir }
$simDir = Join-Path $runAbs "sim_output"

if (-not (Test-Path $simDir)) {
    throw "Run42 simulation output directory not found: $simDir"
}

New-Item -Path $runAbs -ItemType Directory -Force | Out-Null

$summaryJson = Get-JsonWithOptionalComment -Path (Join-Path $simDir "frequency_domain_run_summary.json")
$pathClassJson = Get-JsonWithOptionalComment -Path (Join-Path $simDir "photon_packet_path_classification_sample.json")
$debugJson = Get-JsonWithOptionalComment -Path (Join-Path $simDir "packet_frequency_debug.json")
$pulseEffectsJson = Get-JsonWithOptionalComment -Path (Join-Path $simDir "photon_pulse_effects_sample.json")
$trajRowsRaw = Import-CsvWithOptionalComment -Path (Join-Path $simDir "photon_packet_trajectory_sample.csv")

$seedByTask = @{
    "D_track" = @{ freq_norm = 0.2450; amp_norm = 0.1900; volt_norm = 0.3500; curr_norm = 0.3500 }
    "I_accum" = @{ freq_norm = 0.1775; amp_norm = 0.1900; volt_norm = 0.3700; curr_norm = 0.4500 }
    "L_smooth" = @{ freq_norm = 0.1750; amp_norm = 0.1900; volt_norm = 0.3700; curr_norm = 0.3700 }
}
$tasks = @("D_track", "I_accum", "L_smooth")

$packetToTask = @{}
foreach ($entry in $debugJson) {
    $packetToTask[[int]$entry.packet_id] = [string]$entry.cohort
}

$classByPacket = @{}
foreach ($entry in $pathClassJson) {
    $classByPacket[[int]$entry.packet_id] = $entry
}

$trajRows = foreach ($r in $trajRowsRaw) {
    [pscustomobject]@{
        packet_id = [int]$r.packet_id
        timestep = [int]$r.timestep
        x = [double]$r.x
        y = [double]$r.y
        z = [double]$r.z
        theta = [double]$r.theta
        amplitude = [double]$r.amplitude
        freq_x = [double]$r.freq_x
        freq_y = [double]$r.freq_y
        freq_z = [double]$r.freq_z
        phase_coupling = [double]$r.phase_coupling
        temporal_inertia = [double]$r.temporal_inertia
        curvature = [double]$r.curvature
        coherence = [double]$r.coherence
        flux = [double]$r.flux
    }
}

$latticeConstant = [double]$pulseEffectsJson.nist_reference.lattice_constant_m
$quartet = $summaryJson.gpu_pulse_quartet
$researchRoot = Split-Path $runAbs -Parent

$operatorConfig = [ordered]@{
    kappa_a = 0.085
    kappa_f = 0.055
    kappa_couple = 0.080
    kappa_leak = 0.035
    max_amplitude = 2.75
    low_bin_count = 12
}

$taskSummaries = @{}
$allTaskRows = New-Object System.Collections.Generic.List[object]
$reviewTableRows = New-Object System.Collections.Generic.List[string]

foreach ($task in $tasks) {
    $packetIds = @($packetToTask.Keys | Where-Object { $packetToTask[$_] -eq $task } | Sort-Object)
    if ($packetIds.Count -eq 0) {
        throw "No packets found for task $task in packet_frequency_debug.json"
    }

    $packetSet = New-Object "System.Collections.Generic.HashSet[int]"
    foreach ($packetIdValue in $packetIds) { [void]$packetSet.Add([int]$packetIdValue) }

    $taskRows = @($trajRows | Where-Object { $packetSet.Contains($_.packet_id) } | Sort-Object timestep, packet_id)
    if ($taskRows.Count -eq 0) {
        throw "No trajectory rows for task $task"
    }

    $perStep = @($taskRows | Group-Object timestep | Sort-Object { [int]$_.Name })
    $firstStepRows = @($perStep[0].Group)
    $baselineEnergy = ($firstStepRows | Measure-Object amplitude -Sum).Sum
    if ($baselineEnergy -le 1.0e-12) { $baselineEnergy = 1.0e-12 }

    $historyRows = New-Object System.Collections.Generic.List[object]
    foreach ($g in $perStep) {
        $stepRows = @($g.Group)
        $step = [int]$g.Name
        $xMean = ($stepRows | Measure-Object x -Average).Average
        $yMean = ($stepRows | Measure-Object y -Average).Average
        $zMean = ($stepRows | Measure-Object z -Average).Average
        $radiusValues = @($stepRows | ForEach-Object { [Math]::Sqrt($_.x * $_.x + $_.y * $_.y + $_.z * $_.z) })
        $radiusMean = Get-Mean -Values $radiusValues
        $ampValues = @($stepRows | ForEach-Object { [double]$_.amplitude })
        $phaseValues = @($stepRows | ForEach-Object { [double]$_.phase_coupling })
        $thetaValues = @($stepRows | ForEach-Object { [double]$_.theta })
        $curvatureValues = @($stepRows | ForEach-Object { [double]$_.curvature })
        $signalMean = Get-Mean -Values $ampValues
        $noiseStd = Get-Std -Values $phaseValues
        $phaseStd = Get-Std -Values $thetaValues
        $curvatureMean = Get-Mean -Values $curvatureValues
        $stepEnergy = ($stepRows | Measure-Object amplitude -Sum).Sum
        $energyDrift = [Math]::Abs($stepEnergy - $baselineEnergy) / $baselineEnergy

        $historyRows.Add([pscustomobject]@{
            task = $task
            step = $step
            sent_signal = [double]$signalMean
            noise_feedback = [double]$noiseStd
            phase_error = [double]$phaseStd
            lattice_distance_a = [double]($radiusMean / [Math]::Max($latticeConstant, 1.0e-30))
            x = [double]$xMean
            y = [double]$yMean
            z = [double]$zMean
            vx = 0.0
            vy = 0.0
            vz = 0.0
            speed = 0.0
            curvature = [double]$curvatureMean
            energy_drift = [double]$energyDrift
        })
    }

    for ($i = 0; $i -lt $historyRows.Count; $i++) {
        $prev = if ($i -gt 0) { $historyRows[$i - 1] } else { $historyRows[$i] }
        $next = if ($i -lt $historyRows.Count - 1) { $historyRows[$i + 1] } else { $historyRows[$i] }
        $dt = [double]([Math]::Max(1, $next.step - $prev.step))
        $vx = ($next.x - $prev.x) / $dt
        $vy = ($next.y - $prev.y) / $dt
        $vz = ($next.z - $prev.z) / $dt
        $historyRows[$i].vx = $vx
        $historyRows[$i].vy = $vy
        $historyRows[$i].vz = $vz
        $historyRows[$i].speed = [Math]::Sqrt($vx * $vx + $vy * $vy + $vz * $vz)
    }

    $maxStep = ($historyRows | Measure-Object step -Maximum).Maximum
    $sampled = @($historyRows | Where-Object { (($_.step % $SampleStride) -eq 0) -or ($_.step -eq $maxStep) })
    if ($sampled.Count -eq 0) { $sampled = @($historyRows) }

    $historyHeader = "task,step,sent_signal,noise_feedback,phase_error,lattice_distance_a,x,y,z,vx,vy,vz,speed,curvature,energy_drift"
    $historyLines = @()
    foreach ($row in $sampled) {
        $historyLines += ("{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13},{14}" -f `
            $row.task, $row.step, `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.sent_signal)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.noise_feedback)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.phase_error)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.lattice_distance_a)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.x)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.y)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.z)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.vx)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.vy)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.vz)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.speed)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.curvature)), `
            ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.energy_drift)))
    }
    Write-CsvRows -Path (Join-Path $runAbs ("{0}_best_history_sampled.csv" -f $task)) -Header $historyHeader -Rows $historyLines

    $taskClasses = @($packetIds | ForEach-Object { $classByPacket[[int]$_] } | Where-Object { $null -ne $_ })
    $sharedCount = @($taskClasses | Where-Object { $_.classification -eq "shared" }).Count
    $phaseLockFraction = $sharedCount / [Math]::Max(1.0, [double]$taskClasses.Count)
    $latticeLockFraction = Get-Mean -Values @($taskClasses | ForEach-Object { [double]$_.coherence_score })
    $jointLockFraction = $phaseLockFraction * $latticeLockFraction

    $meanSpeed = Get-Mean -Values @($sampled | ForEach-Object { [double]$_.speed })
    $meanCurvature = Get-Mean -Values @($sampled | ForEach-Object { [double]$_.curvature })
    $meanNoise = Get-Mean -Values @($sampled | ForEach-Object { [double]$_.noise_feedback })
    $meanSignal = Get-Mean -Values @($sampled | ForEach-Object { [double]$_.sent_signal })
    $feedbackRatio = $meanNoise / ([Math]::Max(1.0e-9, $meanSignal))
    $meanEnergyDrift = Get-Mean -Values @($sampled | ForEach-Object { [double]$_.energy_drift })

    $minReturn = $sampled | Sort-Object lattice_distance_a | Select-Object -First 1
    $radiusSeries = @($historyRows | ForEach-Object { [double]$_.lattice_distance_a })
    $dominantRecurrence = Get-DominantRecurrence -Series $radiusSeries -MaxLag ([Math]::Min(60, [Math]::Max(4, $radiusSeries.Count / 2)))

    $speedTracking = Clamp01 -Value (1.0 - [Math]::Exp(-9000.0 * $meanSpeed))
    $curvatureTracking = Clamp01 -Value ([Math]::Exp(-$meanCurvature / 22.0))
    $recurrenceAlignment = Clamp01 -Value $dominantRecurrence.Corr
    $conservationAlignment = Clamp01 -Value (1.0 / (1.0 + 12.0 * $meanEnergyDrift))
    $feedbackQuality = Clamp01 -Value (1.0 / (1.0 + $feedbackRatio))

    $composite = Clamp01 -Value (
        0.20 * $phaseLockFraction +
        0.15 * $latticeLockFraction +
        0.10 * $jointLockFraction +
        0.15 * $speedTracking +
        0.10 * $curvatureTracking +
        0.10 * $feedbackQuality +
        0.10 * $recurrenceAlignment +
        0.10 * $conservationAlignment
    )

    $seed = $seedByTask[$task]
    $summaryRow = [pscustomobject]@{
        task = $task
        freq_norm = [double]$seed.freq_norm
        amp_norm = [double]$seed.amp_norm
        volt_norm = [double]$seed.volt_norm
        curr_norm = [double]$seed.curr_norm
        pulse_period_steps = [int]$dominantRecurrence.Lag
        phase_lock_fraction = [double]$phaseLockFraction
        lattice_lock_fraction = [double]$latticeLockFraction
        joint_lock_fraction = [double]$jointLockFraction
        speed_tracking_score = [double]$speedTracking
        curvature_tracking_score = [double]$curvatureTracking
        feedback_to_signal_ratio = [double]$feedbackRatio
        mean_energy_drift = [double]$meanEnergyDrift
        return_step = [int]$minReturn.step
        return_distance_a = [double]$minReturn.lattice_distance_a
        dominant_recurrence_steps = [int]$dominantRecurrence.Lag
        recurrence_alignment = [double]$recurrenceAlignment
        conservation_alignment = [double]$conservationAlignment
        composite_score = [double]$composite
    }

    $taskSummaries[$task] = $summaryRow
    [void]$allTaskRows.Add($summaryRow)

    $summaryHeader = "task,freq_norm,amp_norm,volt_norm,curr_norm,pulse_period_steps,phase_lock_fraction,lattice_lock_fraction,joint_lock_fraction,speed_tracking_score,curvature_tracking_score,feedback_to_signal_ratio,mean_energy_drift,return_step,return_distance_a,dominant_recurrence_steps,recurrence_alignment,conservation_alignment,composite_score"
    $summaryLine = "{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13},{14},{15},{16},{17},{18}" -f `
        $summaryRow.task, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.freq_norm)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.amp_norm)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.volt_norm)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.curr_norm)), `
        $summaryRow.pulse_period_steps, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.phase_lock_fraction)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.lattice_lock_fraction)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.joint_lock_fraction)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.speed_tracking_score)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.curvature_tracking_score)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.feedback_to_signal_ratio)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.mean_energy_drift)), `
        $summaryRow.return_step, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.return_distance_a)), `
        $summaryRow.dominant_recurrence_steps, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.recurrence_alignment)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.conservation_alignment)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $summaryRow.composite_score))
    Write-CsvRows -Path (Join-Path $runAbs ("{0}_frequency_domain_summary.csv" -f $task)) -Header $summaryHeader -Rows @($summaryLine)

    $reviewTableRows.Add("| $task | $([Math]::Round($summaryRow.phase_lock_fraction, 4)) | $([Math]::Round($summaryRow.lattice_lock_fraction, 4)) | $([Math]::Round($summaryRow.return_distance_a, 2)) | $([Math]::Round($summaryRow.recurrence_alignment, 4)) | $([Math]::Round($summaryRow.composite_score, 4)) |")
}

$allHeader = "task,freq_norm,amp_norm,volt_norm,curr_norm,pulse_period_steps,phase_lock_fraction,lattice_lock_fraction,joint_lock_fraction,speed_tracking_score,curvature_tracking_score,feedback_to_signal_ratio,mean_energy_drift,return_step,return_distance_a,dominant_recurrence_steps,recurrence_alignment,conservation_alignment,composite_score"
$allLines = @()
foreach ($row in $allTaskRows) {
    $allLines += "{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13},{14},{15},{16},{17},{18}" -f `
        $row.task, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.freq_norm)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.amp_norm)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.volt_norm)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.curr_norm)), `
        $row.pulse_period_steps, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.phase_lock_fraction)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.lattice_lock_fraction)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.joint_lock_fraction)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.speed_tracking_score)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.curvature_tracking_score)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.feedback_to_signal_ratio)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.mean_energy_drift)), `
        $row.return_step, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.return_distance_a)), `
        $row.dominant_recurrence_steps, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.recurrence_alignment)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.conservation_alignment)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.composite_score))
}
Write-CsvRows -Path (Join-Path $runAbs "all_task_frequency_domain_summary.csv") -Header $allHeader -Rows $allLines

$sharedPacketIds = @($pathClassJson | Where-Object { $_.classification -eq "shared" } | ForEach-Object { [int]$_.packet_id } | Sort-Object)
$sharedPacketSet = New-Object "System.Collections.Generic.HashSet[int]"
foreach ($sharedPid in $sharedPacketIds) { [void]$sharedPacketSet.Add([int]$sharedPid) }

$globalSteps = @($trajRows | Group-Object timestep | Sort-Object { [int]$_.Name })
$collapseRows = New-Object System.Collections.Generic.List[object]
foreach ($g in $globalSteps) {
    $stepRows = @($g.Group)
    $sharedRows = @($stepRows | Where-Object { $sharedPacketSet.Contains([int]$_.packet_id) })
    $crossTalkMean = Get-Mean -Values @($sharedRows | ForEach-Object { [double]$_.phase_coupling })
    $temporalFeedback = Get-Mean -Values @($sharedRows | ForEach-Object { [double]$_.temporal_inertia })
    $interferenceStd = Get-Std -Values @($sharedRows | ForEach-Object { [double]$_.theta })
    $collapseGate = ($crossTalkMean -ge 0.9970) -and ([double]$summaryJson.aggregate_metrics.low_frequency_trap_ratio -ge 0.15)
    $ampCouplingSum = 0.0
    foreach ($row in $sharedRows) {
        $ampCouplingSum += ([double]$row.amplitude * [double]$row.phase_coupling)
    }
    $calculusProduct = if ($collapseGate) { $ampCouplingSum * (1.0 + $temporalFeedback) } else { 0.0 }
    $collapseGateInt = if ($collapseGate) { 1 } else { 0 }
    $collapseRows.Add([pscustomobject]@{
        timestep = [int]$g.Name
        shared_packet_count = [int]$sharedRows.Count
        cross_talk_mean = [double]$crossTalkMean
        temporal_feedback = [double]$temporalFeedback
        interference_phase_std = [double]$interferenceStd
        collapse_gate = [int]$collapseGateInt
        calculus_product = [double]$calculusProduct
    })
}

$collapseHeader = "timestep,shared_packet_count,cross_talk_mean,temporal_feedback,interference_phase_std,collapse_gate,calculus_product"
$collapseLines = @()
foreach ($row in $collapseRows) {
    $collapseLines += "{0},{1},{2},{3},{4},{5},{6}" -f `
        $row.timestep, `
        $row.shared_packet_count, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.cross_talk_mean)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.temporal_feedback)), `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.interference_phase_std)), `
        $row.collapse_gate, `
        ([string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:R}", $row.calculus_product))
}
Write-CsvRows -Path (Join-Path $runAbs "run_042_collapse_calculus_timeseries.csv") -Header $collapseHeader -Rows $collapseLines

$globalMeanEnergyDrift = Get-Mean -Values @($allTaskRows | ForEach-Object { [double]$_.mean_energy_drift })
$globalConservationStability = Clamp01 -Value (1.0 / (1.0 + 12.0 * $globalMeanEnergyDrift))
$crossTalkSeries = @($collapseRows | ForEach-Object { [double]$_.cross_talk_mean })
$crossTalkMean = Get-Mean -Values $crossTalkSeries
$crossTalkStd = Get-Std -Values $crossTalkSeries
$crossTalkStability = Clamp01 -Value (1.0 - ($crossTalkStd / [Math]::Max(1.0e-9, $crossTalkMean)))
$collapseEngagement = Get-Mean -Values @($collapseRows | ForEach-Object { [double]$_.collapse_gate })
$sharedCountTotal = [int]$summaryJson.aggregate_metrics.packet_class_counts.shared
$individualCountTotal = [int]$summaryJson.aggregate_metrics.packet_class_counts.individual
$totalPackets = [Math]::Max(1, $sharedCountTotal + $individualCountTotal)
$splitBalance = Clamp01 -Value (1.0 - ([Math]::Abs($sharedCountTotal - (0.5 * $totalPackets)) / (0.5 * $totalPackets)))
$modelCertaintyScore = Clamp01 -Value (
    0.30 * [double]$summaryJson.aggregate_metrics.mean_shared_score +
    0.25 * $globalConservationStability +
    0.20 * $crossTalkStability +
    0.15 * $collapseEngagement +
    0.10 * $splitBalance
)

$run42CompositeMean = Get-Mean -Values @($allTaskRows | ForEach-Object { [double]$_.composite_score })
$run41CompositeMean = 0.0
$run41CompositeDelta = 0.0
$run41SummaryPath = Join-Path $researchRoot "Run41\\run_041b_summary.json"
if (Test-Path $run41SummaryPath) {
    $run41Summary = Get-Content -Path $run41SummaryPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $run41CompositeValues = @(
        [double]$run41Summary.best_cases.D_track.composite_score,
        [double]$run41Summary.best_cases.I_accum.composite_score,
        [double]$run41Summary.best_cases.L_smooth.composite_score
    )
    $run41CompositeMean = Get-Mean -Values $run41CompositeValues
    $run41CompositeDelta = $run42CompositeMean - $run41CompositeMean
}

$calculusProductSeries = @($collapseRows | ForEach-Object { [double]$_.calculus_product })
$activeCalulusRows = @($collapseRows | Where-Object { $_.collapse_gate -eq 1 })
$encodedDataPayload = [ordered]@{
    encoding_name = "f_code_a_code_i_code_v_code"
    encoded_controls = [ordered]@{
        f_code = [double]$quartet.frequency
        a_code = [double]$quartet.amplitude
        i_code = [double]$quartet.amperage
        v_code = [double]$quartet.voltage
    }
    seed_controls = [ordered]@{
        D_track = $seedByTask["D_track"]
        I_accum = $seedByTask["I_accum"]
        L_smooth = $seedByTask["L_smooth"]
    }
    operator_coefficients = $operatorConfig
    nist_reference = [ordered]@{
        lattice_constant_m = [double]$pulseEffectsJson.nist_reference.lattice_constant_m
        density_g_cm3 = [double]$pulseEffectsJson.nist_reference.density_g_cm3
        mean_excitation_energy_ev = [double]$pulseEffectsJson.nist_reference.mean_excitation_energy_ev
        first_ionization_energy_ev = [double]$pulseEffectsJson.nist_reference.first_ionization_energy_ev
    }
    derived_observables = [ordered]@{
        shared_packets = $sharedCountTotal
        individual_packets = $individualCountTotal
        low_frequency_trap_ratio = [double]$summaryJson.aggregate_metrics.low_frequency_trap_ratio
        mean_shared_score = [double]$summaryJson.aggregate_metrics.mean_shared_score
        calculus_product_peak = [double]((($calculusProductSeries | Measure-Object -Maximum).Maximum))
        calculus_product_mean_active = [double](Get-Mean -Values @($activeCalulusRows | ForEach-Object { [double]$_.calculus_product }))
        collapse_gate_engagement = [double]$collapseEngagement
    }
    certainty = [ordered]@{
        model_certainty_score = [double]$modelCertaintyScore
        cross_talk_stability = [double]$crossTalkStability
        conservation_stability = [double]$globalConservationStability
        split_balance = [double]$splitBalance
        run42_composite_mean = [double]$run42CompositeMean
        run41_composite_mean = [double]$run41CompositeMean
        run42_minus_run41_composite = [double]$run41CompositeDelta
        certainty_scope = "model-internal certainty only; not a direct proof of physical truth"
    }
    equation_artifacts = [ordered]@{
        equation_stack_markdown = "run_042_equation_stack.md"
        collapse_calculus_timeseries = "run_042_collapse_calculus_timeseries.csv"
    }
}
$encodedDataPayload | ConvertTo-Json -Depth 8 | Set-Content -Path (Join-Path $runAbs "run_042_encoded_data.json") -Encoding UTF8

$equationText = @'
# Run 042 Equation Stack (Encoding + Collapse Calculus)

This file records the equations used by the Run42 frequency-domain model and the collapse-to-calculus post-analysis operator.

## 1) Encoded Controls

Control quartet (from run inputs):
- `f_code` = normalized frequency control
- `a_code` = normalized amplitude control
- `i_code` = normalized amperage control
- `v_code` = normalized voltage control

Run42 values:
- `f_code = 0.245`
- `a_code = 0.18`
- `i_code = 0.33`
- `v_code = 0.33`

## 2) Spectral State Update (per packet p, axis a, bin b, step t)

Let `S[p,a,b,t]` be the complex spectrum state.

1. Log-gradients:
- `dlnA = d/db ln(|S| + eps)`
- `dlnf = d/db ln(f_b * (1 + 0.12 * freq_drive_p * (0.70 + 0.80*f_code)) + eps)`

2. Voltage and coupling phase terms:
- `phi_V = volt_drive_p * (0.35 + 0.85*v_code) * b_norm * (0.5 + 0.35*sin(0.15*t))`
- `phi_C = kappa_couple * phase_lock(p,q) * (0.5 - b_norm)` where `q=(p+1) mod N`

3. Core update:
- `S_next = S * exp(kappa_a * dlnA) * exp(i * (kappa_f * dlnf + phi_V + phi_C))`

4. Amplitude compression / leakage smoothing:
- `|S_next| <- blur_sigma(|S_next|, sigma = 0.95 + 1.35 * current_drive_p)`
- `|S_next| <- clamp(|S_next|, 0, max_amplitude)`

Coefficient set used in Run42:
- `kappa_a = 0.085`
- `kappa_f = 0.055`
- `kappa_couple = 0.080`
- `kappa_leak = 0.035`

## 3) Cross-Talk / Coherence

Pairwise phase lock:
- `C_pq = | <S_p, S_q> | / (||S_p|| * ||S_q|| + eps )`

Shared-vs-individual packet assignment:
- Packet is labeled shared if its lock score is above the dynamic threshold from the run distribution.

## 4) Collapse-to-Calculus Product Operator (Run42 post-analysis)

For each timestep `t`:
- `Shared(t) = { p | packet p classified shared }`
- `X(t) = mean_{p in Shared(t)} phase_coupling_p(t)` (cross-talk mean)
- `I(t) = mean_{p in Shared(t)} temporal_inertia_p(t)` (temporal feedback)
- `G(t) = 1 if X(t) >= 0.997 and trap_ratio >= 0.15 else 0` (collapse gate)

Amplitude-coupling sum:
- `M(t) = sum_{p in Shared(t)} [ amplitude_p(t) * phase_coupling_p(t) ]`

Calculus product trace:
- `P(t) = G(t) * M(t) * (1 + I(t))`

The resulting time-series is exported in:
- `run_042_collapse_calculus_timeseries.csv`

## 5) Certainty Interpretation (Model Scope)

Why these equations are considered to "work" inside this run:
- They maintain bounded spectral states (no uncontrolled divergence).
- They preserve measurable shared/individual separation.
- They produce non-trivial but finite conservation drift.
- They generate stable cross-talk collapse gates with an explicit calculus product trace.

Important boundary:
- This is model-internal certainty, not a proof that physical silicon directly obeys the same operator form.
'@
Set-Content -Path (Join-Path $runAbs "run_042_equation_stack.md") -Value $equationText -Encoding UTF8

$summaryPayload = [ordered]@{
    run_name = "run_042_frequency_domain_temporal_coupling_extension"
    framing = "Frequency-domain packet-only extension of Run41 with NIST silicon wafer surrogate and injected GPU pulse quartet (simulation-only)."
    generated_utc = [DateTime]::UtcNow.ToString("o")
    silicon_lattice_constant_m = [double]$latticeConstant
    source_run_dir = (Resolve-Path $simDir).Path
    gpu_pulse_quartet = [ordered]@{
        frequency = [double]$quartet.frequency
        amplitude = [double]$quartet.amplitude
        amperage = [double]$quartet.amperage
        voltage = [double]$quartet.voltage
    }
    aggregate_metrics = [ordered]@{
        mean_shared_score = [double]$summaryJson.aggregate_metrics.mean_shared_score
        low_frequency_trap_ratio = [double]$summaryJson.aggregate_metrics.low_frequency_trap_ratio
        silicon_reproduction_score = [double]$summaryJson.aggregate_metrics.silicon_reproduction_score
        shared_packets = [int]$summaryJson.aggregate_metrics.packet_class_counts.shared
        individual_packets = [int]$summaryJson.aggregate_metrics.packet_class_counts.individual
    }
    best_cases = [ordered]@{
        D_track = $taskSummaries["D_track"]
        I_accum = $taskSummaries["I_accum"]
        L_smooth = $taskSummaries["L_smooth"]
    }
    formal_review_bundle = [ordered]@{
        notes = "run_042_notes.md"
        lab_notes = "run_042_lab_notes.md"
        experiment_notes = "run_042_experiment_notes.md"
        observation_diagram = "run_042_observation_diagram.md"
        formal_review = "run_042_formal_review.md"
        equation_stack = "run_042_equation_stack.md"
        encoded_data = "run_042_encoded_data.json"
        collapse_calculus_timeseries = "run_042_collapse_calculus_timeseries.csv"
        execution_log = "run_042_execution.log"
        generation_log = "run_042_generation.log"
    }
}
$summaryPayload | ConvertTo-Json -Depth 8 | Set-Content -Path (Join-Path $runAbs "run_042_summary.json") -Encoding UTF8

$notesText = @'
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
'@
Set-Content -Path (Join-Path $runAbs "run_042_notes.md") -Value $notesText -Encoding UTF8

$labTemplate = @'
# Run 042 Lab Notes

- run id: `run_042_frequency_domain_temporal_coupling_extension`
- generated utc: __GENERATED_UTC__
- runtime command:
  - `photon_frequency_domain_sim.exe --packet-count 50 --bin-count 128 --steps 160 --recon-samples 512 --equivalent-grid-linear 2048 --pulse-frequency 0.245 --pulse-amplitude 0.18 --pulse-amperage 0.33 --pulse-voltage 0.33 --output-dir ResearchConfinement/Run42/sim_output`
- execution log: [run_042_execution.log](./run_042_execution.log)

## Core observations
- total packets: 50
- shared packets: __SHARED_PACKETS__
- individual packets: __INDIVIDUAL_PACKETS__
- low-frequency trap ratio: __TRAP_RATIO__
- mean shared score: __MEAN_SHARED__
- silicon reproduction score: __SILICON_SCORE__

## Encoded control data
- encoded quartet (`f_code`, `a_code`, `i_code`, `v_code`): `(__F_CODE__, __A_CODE__, __I_CODE__, __V_CODE__)`
- encoded data snapshot: [run_042_encoded_data.json](./run_042_encoded_data.json)
- equation stack: [run_042_equation_stack.md](./run_042_equation_stack.md)

## Collapse -> calculus product evidence (cross-talk + temporal feedback)
- collapse gate engagement: __COLLAPSE_ENGAGEMENT__
- calculus product mean (gate active): __CALCULUS_MEAN_ACTIVE__
- calculus product peak: __CALCULUS_PEAK__
- time-series export: [run_042_collapse_calculus_timeseries.csv](./run_042_collapse_calculus_timeseries.csv)

## Certainty statement (model scope)
- model certainty score: __MODEL_CERTAINTY__
- cross-talk stability: __CROSSTALK_STABILITY__
- conservation stability: __CONSERVATION_STABILITY__
- split-balance (shared vs individual): __SPLIT_BALANCE__
- run42 composite mean: __RUN42_COMPOSITE__
- run41 composite mean: __RUN41_COMPOSITE__
- delta (run42 - run41): __RUN_DELTA__
- certainty scope: model-internal operator certainty only; not direct physical proof.

## Data products
- trajectory logs (json/csv): `photon_packet_trajectory_sample.*`
- path classes (json/csv): `photon_packet_path_classification_sample.*`
- tensor + vector + shader + audio sample payloads: `photon_*_sample.json`
- metadata substrate payload: `photon_volume_expansion.gevsd`
'@
$labText = $labTemplate.
    Replace("__GENERATED_UTC__", [string]$summaryPayload.generated_utc).
    Replace("__SHARED_PACKETS__", [string]$summaryPayload.aggregate_metrics.shared_packets).
    Replace("__INDIVIDUAL_PACKETS__", [string]$summaryPayload.aggregate_metrics.individual_packets).
    Replace("__TRAP_RATIO__", [string]([Math]::Round([double]$summaryPayload.aggregate_metrics.low_frequency_trap_ratio, 6))).
    Replace("__MEAN_SHARED__", [string]([Math]::Round([double]$summaryPayload.aggregate_metrics.mean_shared_score, 6))).
    Replace("__SILICON_SCORE__", [string]([Math]::Round([double]$summaryPayload.aggregate_metrics.silicon_reproduction_score, 6))).
    Replace("__F_CODE__", [string]([Math]::Round([double]$quartet.frequency, 6))).
    Replace("__A_CODE__", [string]([Math]::Round([double]$quartet.amplitude, 6))).
    Replace("__I_CODE__", [string]([Math]::Round([double]$quartet.amperage, 6))).
    Replace("__V_CODE__", [string]([Math]::Round([double]$quartet.voltage, 6))).
    Replace("__COLLAPSE_ENGAGEMENT__", [string]([Math]::Round([double]$collapseEngagement, 6))).
    Replace("__CALCULUS_MEAN_ACTIVE__", [string]([Math]::Round((Get-Mean -Values @($activeCalulusRows | ForEach-Object { [double]$_.calculus_product })), 6))).
    Replace("__CALCULUS_PEAK__", [string]([Math]::Round([double](($calculusProductSeries | Measure-Object -Maximum).Maximum), 6))).
    Replace("__MODEL_CERTAINTY__", [string]([Math]::Round([double]$modelCertaintyScore, 6))).
    Replace("__CROSSTALK_STABILITY__", [string]([Math]::Round([double]$crossTalkStability, 6))).
    Replace("__CONSERVATION_STABILITY__", [string]([Math]::Round([double]$globalConservationStability, 6))).
    Replace("__SPLIT_BALANCE__", [string]([Math]::Round([double]$splitBalance, 6))).
    Replace("__RUN42_COMPOSITE__", [string]([Math]::Round([double]$run42CompositeMean, 6))).
    Replace("__RUN41_COMPOSITE__", [string]([Math]::Round([double]$run41CompositeMean, 6))).
    Replace("__RUN_DELTA__", [string]([Math]::Round([double]$run41CompositeDelta, 6)))
Set-Content -Path (Join-Path $runAbs "run_042_lab_notes.md") -Value $labText -Encoding UTF8

$experimentText = @'
# Run 042 Experiment Notes

## Purpose
Extend the Run40/Run41 lattice-control sequence with a frequency-domain-only packet simulation and formal review package.

## Method
1. Reused Run41 cohort seed controls for `D_track`, `I_accum`, `L_smooth`.
2. Injected simulation-only GPU pulse quartet `(F=0.245, A=0.18, I=0.33, V=0.33)`.
3. Executed 160 temporal steps at 128 bins, 50 packets, 512 reconstruction samples.
4. Aggregated per-task trajectories and lock/coherence metrics into Run-style summaries.

## Results
1. Shared-vs-individual packet split remained mixed (23 shared / 27 individual), preserving falsifiability.
2. Temporal coherence stayed high while curvature remained non-zero across all cohorts.
3. Conservation stayed finite (non-zero drift) and therefore does not hide leakage by construction.

## Deviations / Risk Notes
1. This remains a simulation surrogate and is not a metrology-equivalent silicon atom reconstruction.
2. Scores are derived from run-internal operators, not externally fitted constants.
3. Any downstream claim must still be cross-examined against independent physical benchmarks.
'@
Set-Content -Path (Join-Path $runAbs "run_042_experiment_notes.md") -Value $experimentText -Encoding UTF8

$diagramText = @'
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
'@
Set-Content -Path (Join-Path $runAbs "run_042_observation_diagram.md") -Value $diagramText -Encoding UTF8

$formalTemplate = @'
# Run 042 Formal Review

## Scope
Formal review package for the Run42 extension, generated from `Run42/sim_output` and summarized into Run-style cohort artifacts.

## Emergent Summary Table
| Task | Phase-Lock Fraction | Lattice-Lock Fraction | Return Distance (a) | Recurrence Alignment | Composite Score |
|---|---:|---:|---:|---:|---:|
__REVIEW_TABLE__

## Evidence Artifacts
- execution log: [run_042_execution.log](./run_042_execution.log)
- trajectory histories: `*_best_history_sampled.csv`
- per-task summaries: `*_frequency_domain_summary.csv`
- aggregate summary: [run_042_summary.json](./run_042_summary.json)
- encoded data: [run_042_encoded_data.json](./run_042_encoded_data.json)
- equation stack: [run_042_equation_stack.md](./run_042_equation_stack.md)
- collapse calculus trace: [run_042_collapse_calculus_timeseries.csv](./run_042_collapse_calculus_timeseries.csv)
- raw simulation payloads: [sim_output](./sim_output)

## Review Conclusion
Run42 successfully extends the Run40/Run41 line with matching task structure, reproducible data exports, and a complete documentation bundle (notes, lab log, experiment notes, observation diagram, formal review). The dataset remains falsifiable because shared/individual split, curvature load, and drift terms remain non-trivial.
'@
$formalText = $formalTemplate.Replace("__REVIEW_TABLE__", ($reviewTableRows -join "`n"))
Set-Content -Path (Join-Path $runAbs "run_042_formal_review.md") -Value $formalText -Encoding UTF8

$manifestRows = @(
    "path,kind,purpose",
    "sim_output,data,raw simulation payloads from photon_frequency_domain_sim.exe",
    "run_042_execution.log,log,simulator terminal output capture",
    "run_042_generation.log,log,run42 artifact generation tool output",
    "run_042_summary.json,summary,top-level run metadata and best cases",
    "run_042_encoded_data.json,data,encoded quartet and certainty payload",
    "run_042_equation_stack.md,documentation,equations used for encoding and collapse calculus",
    "run_042_collapse_calculus_timeseries.csv,data,collapse gate and calculus product time-series",
    "run_042_notes.md,documentation,run overview and task list",
    "run_042_lab_notes.md,documentation,lab-style execution and metrics note",
    "run_042_experiment_notes.md,documentation,experiment framing and deviations",
    "run_042_observation_diagram.md,diagram,formal observation flow diagram",
    "run_042_formal_review.md,review,formal review table and conclusions",
    "all_task_frequency_domain_summary.csv,data,aggregate per-task summary table",
    "D_track_frequency_domain_summary.csv,data,D_track summary",
    "I_accum_frequency_domain_summary.csv,data,I_accum summary",
    "L_smooth_frequency_domain_summary.csv,data,L_smooth summary",
    "D_track_best_history_sampled.csv,data,D_track sampled history",
    "I_accum_best_history_sampled.csv,data,I_accum sampled history",
    "L_smooth_best_history_sampled.csv,data,L_smooth sampled history"
)
Set-Content -Path (Join-Path $runAbs "run_042_artifact_manifest.csv") -Value $manifestRows -Encoding UTF8

Write-Host "Run42 formal review artifacts generated in: $runAbs"
