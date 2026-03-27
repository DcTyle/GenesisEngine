param(
    [string]$BuildRoot = "out/build/win64-vs2022-clang-vulkan",
    [switch]$StartApp,
    [switch]$StatusOnly,
    [int]$StartupDelayMs = 1500
)

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRootAbs = Join-Path $repoRoot $BuildRoot
$editorExe = Join-Path $buildRootAbs "GenesisEngineState/Binaries/Win64/Editor/Release/GenesisEngine.exe"
$remoteExe = Join-Path $buildRootAbs "Release/GenesisRemote.exe"

if (-not (Test-Path $editorExe)) {
    throw "Editor executable not found: $editorExe"
}
if (-not (Test-Path $remoteExe)) {
    throw "GenesisRemote executable not found: $remoteExe"
}

if ($StartApp) {
    Start-Process -FilePath $editorExe -WorkingDirectory $repoRoot | Out-Null
    Start-Sleep -Milliseconds $StartupDelayMs
}

$commands = @()
if ($StatusOnly) {
    $commands += "viewport status"
    $commands += "/sim_temporal status"
} else {
    $commands += "viewport live on"
    $commands += "viewport resonance off"
    $commands += "viewport vector on"
    $commands += "viewport vector gain 100"
    $commands += "viewport lattice volume on"
    $commands += "viewport lattice stride 2"
    $commands += "viewport lattice maxpoints 32768"
    $commands += "viewport confinement on"
    $commands += "/sim_temporal nist"
    $commands += "/sim_live_gpu on"
    $commands += "/sim_play on"
}

foreach ($command in $commands) {
    & $remoteExe "command=$command"
}
