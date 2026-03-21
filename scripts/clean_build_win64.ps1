Param(
  [ValidateSet("win64-vs2022-clang-vulkan")]
  [string]$Preset = "win64-vs2022-clang-vulkan",
  [ValidateSet("Release","RelWithDebInfo","Debug")]
  [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

# Always do a fresh out-of-tree build for each iteration.
$SourceDir = Split-Path -Parent $PSScriptRoot
$BuildDir  = Join-Path $SourceDir ("out\build\" + $Preset)

Write-Host ("[GE] SourceDir: " + $SourceDir)
Write-Host ("[GE] BuildDir : " + $BuildDir)
Write-Host ("[GE] Preset   : " + $Preset)
Write-Host ("[GE] Config   : " + $Config)

if (Test-Path $BuildDir) {
  Write-Host "[GE] Removing previous build dir..."
  Remove-Item -Recurse -Force $BuildDir
}

Write-Host "[GE] Configuring..."
cmake --preset $Preset

Write-Host "[GE] Building..."
cmake --build --preset $Preset --config $Config

Write-Host "[GE] Done."
