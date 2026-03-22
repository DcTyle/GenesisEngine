Param(
  [ValidateSet("win64-vs2022-clang-vulkan")]
  [string]$Preset = "win64-vs2022-clang-vulkan",
  [ValidateSet("Release","RelWithDebInfo","Debug")]
  [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

function Use-UserVulkanSdkIfPresent {
  if (-not [string]::IsNullOrWhiteSpace($env:VULKAN_SDK)) {
    return
  }
  $sdkBase = Join-Path $env:LOCALAPPDATA "Programs\VulkanSDK"
  if (-not (Test-Path $sdkBase)) {
    return
  }
  $sdkDir = Get-ChildItem -Path $sdkBase -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    Select-Object -First 1
  if ($null -eq $sdkDir) {
    return
  }
  $env:VULKAN_SDK = $sdkDir.FullName
  $env:VK_SDK_PATH = $sdkDir.FullName
  $binPath = Join-Path $sdkDir.FullName "Bin"
  if (($env:PATH -split ';') -notcontains $binPath) {
    $env:PATH = $binPath + ";" + $env:PATH
  }
}

Use-UserVulkanSdkIfPresent

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
