param(
    [string]$BuildRoot = "out/build/win64-vs2022-clang-vulkan",
    [int]$PacketCount = 32,
    [int]$BinCount = 128,
    [int]$Steps = 48,
    [int]$ReconSamples = 256,
    [int]$EquivalentGridLinear = 256,
    [switch]$WriteRootSamples
)

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildAbs = Join-Path $repoRoot $BuildRoot
$candidates = @(
    (Join-Path $buildAbs "Release/photon_frequency_domain_sim.exe"),
    (Join-Path $buildAbs "Debug/photon_frequency_domain_sim.exe"),
    (Join-Path $buildAbs "photon_frequency_domain_sim.exe")
)
$exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    throw "C++ simulator executable not found under: $($candidates -join '; ')"
}

$args = @(
    "--packet-count", "$PacketCount",
    "--bin-count", "$BinCount",
    "--steps", "$Steps",
    "--recon-samples", "$ReconSamples",
    "--equivalent-grid-linear", "$EquivalentGridLinear"
)
if ($WriteRootSamples) {
    $args += "--write-root-samples"
}

& $exe @args
if ($LASTEXITCODE -ne 0) {
    throw "photon_frequency_domain_sim failed with exit code $LASTEXITCODE"
}
