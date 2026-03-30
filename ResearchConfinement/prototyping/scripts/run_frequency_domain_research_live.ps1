param(
    [string]$BuildRoot = "out/build/win64-vs2022-clang-vulkan",
    [int]$PacketCount = 32,
    [int]$BinCount = 128,
    [int]$Steps = 48,
    [int]$ReconSamples = 256,
    [int]$EquivalentGridLinear = 256,
    [int]$Seed = 41,
    [switch]$StartApp,
    [switch]$ApplyViewport,
    [int]$StartupDelayMs = 1500
)

$repoRoot = Split-Path -Parent $PSScriptRoot
$simScript = Join-Path $repoRoot "ResearchConfinement/prototyping/python/photon_frequency_domain_sim.py"
$runDir = Join-Path $repoRoot "ResearchConfinement/frequency_domain_runs/latest"

if (-not (Test-Path $simScript)) {
    throw "Frequency-domain simulator not found: $simScript"
}

$pythonCmd = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCmd) {
    $pythonCmd = Get-Command py -ErrorAction SilentlyContinue
}
if (-not $pythonCmd) {
    throw "Python interpreter not found in PATH."
}

$pythonArgs = @(
    $simScript,
    "--packet-count", "$PacketCount",
    "--bin-count", "$BinCount",
    "--steps", "$Steps",
    "--recon-samples", "$ReconSamples",
    "--equivalent-grid-linear", "$EquivalentGridLinear",
    "--seed", "$Seed",
    "--output-dir", $runDir,
    "--write-root-samples"
)

Write-Host "Running frequency-domain photon confinement sim..."
& $pythonCmd.Source @pythonArgs
if ($LASTEXITCODE -ne 0) {
    throw "Frequency-domain simulator failed with exit code $LASTEXITCODE"
}

$summaryPath = Join-Path $runDir "frequency_domain_run_summary.json"
if (Test-Path $summaryPath) {
    $jsonText = Get-Content $summaryPath -Raw
    if ($jsonText.StartsWith("//")) {
        $jsonText = ($jsonText -split "`r?`n", 2)[1]
    }
    $summary = $jsonText | ConvertFrom-Json
    Write-Host ("Packets: {0}, bins: {1}, shared: {2}, individual: {3}" -f `
        $summary.packet_count, `
        $summary.bin_count, `
        $summary.aggregate_metrics.packet_class_counts.shared, `
        $summary.aggregate_metrics.packet_class_counts.individual)
    Write-Host ("Artifacts: {0}" -f (Join-Path $runDir "debug_view.html"))
}

if (-not ($StartApp -or $ApplyViewport)) {
    return
}

$buildRootAbs = Join-Path $repoRoot $BuildRoot
$editorCandidates = @(
    (Join-Path $buildRootAbs "GenesisEngineState/Binaries/Win64/Editor/Release/GenesisEngine.exe"),
    (Join-Path $buildRootAbs "GenesisEngineState/Binaries/Win64/Editor/Debug/GenesisEngine.exe"),
    (Join-Path $buildRootAbs "GenesisEngineState/Binaries/Win64/Editor/GenesisEngine.exe")
)
$remoteCandidates = @(
    (Join-Path $buildRootAbs "Release/GenesisRemote.exe"),
    (Join-Path $buildRootAbs "Debug/GenesisRemote.exe"),
    (Join-Path $buildRootAbs "GenesisEngineState/Binaries/Win64/Runtime/GenesisRemote.exe")
)

$editorExe = $editorCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$remoteExe = $remoteCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $editorExe) {
    throw "Editor executable not found under: $($editorCandidates -join '; ')"
}
if (-not $remoteExe) {
    throw "GenesisRemote executable not found under: $($remoteCandidates -join '; ')"
}

if ($StartApp) {
    Write-Host "Starting GenesisEngine viewport..."
    Start-Process -FilePath $editorExe -WorkingDirectory $repoRoot | Out-Null
    Start-Sleep -Milliseconds $StartupDelayMs
}

$commands = @()
$commands += "viewport live on"
$commands += "viewport resonance off"
$commands += "viewport vector on"
$commands += "viewport vector gain 100"
$commands += "viewport lattice volume on"
$commands += "viewport lattice stride 2"
$commands += "viewport lattice maxpoints 32768"
$commands += "viewport confinement on"
$commands += "viewport status"

Write-Host "Applying viewport commands..."
foreach ($command in $commands) {
    & $remoteExe "command=$command"
}
