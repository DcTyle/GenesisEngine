# Genesis Engine Vulkan + ClangCL bootstrap (Win64)
# Installs or verifies the canonical Windows toolchain used by the Vulkan app.

$ErrorActionPreference = "Stop"

function Test-CommandAvailable([string]$Name) {
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Install-WingetPackage([string]$Id, [string]$OverrideArgs = "") {
    if (-not (Test-CommandAvailable "winget")) {
        throw "winget is required to install missing prerequisites automatically."
    }
    $args = @(
        "install",
        "--id", $Id,
        "--accept-source-agreements",
        "--accept-package-agreements"
    )
    if (-not [string]::IsNullOrWhiteSpace($OverrideArgs)) {
        $args += @("--override", $OverrideArgs)
    }
    Write-Host ("Installing " + $Id + "...") -ForegroundColor Yellow
    & winget @args
    if ($LASTEXITCODE -ne 0) {
        throw ("winget install failed for " + $Id + " with exit code " + $LASTEXITCODE)
    }
}

Write-Host "Genesis Engine Vulkan bootstrap (Win64)" -ForegroundColor Cyan

if (-not (Test-CommandAvailable "cmake")) {
    Install-WingetPackage "Kitware.CMake"
} else {
    Write-Host "Found cmake." -ForegroundColor Green
}

$vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$hasVsClang = $false
if (Test-Path $vsWhere) {
    $installPath = & $vsWhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                  Microsoft.VisualStudio.Component.VC.Llvm.Clang `
                  Microsoft.VisualStudio.Component.Windows11SDK.22621 `
        -property installationPath 2>$null
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installPath)) {
        $hasVsClang = $true
        Write-Host ("Found Visual Studio toolchain at " + $installPath) -ForegroundColor Green
    }
}
if (-not $hasVsClang) {
    $override = "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.Windows11SDK.22621"
    Install-WingetPackage "Microsoft.VisualStudio.2022.BuildTools" $override
}

if (-not [string]::IsNullOrEmpty($env:VULKAN_SDK) -or (Test-CommandAvailable "glslc") -or (Test-CommandAvailable "glslangValidator")) {
    Write-Host "Found Vulkan shader toolchain." -ForegroundColor Green
} else {
    Install-WingetPackage "KhronosGroup.VulkanSDK"
}

Write-Host ""
Write-Host "Canonical preset:" -ForegroundColor Cyan
Write-Host "  win64-vs2022-clang-vulkan"
Write-Host ""
Write-Host "Configure/build:" -ForegroundColor Cyan
Write-Host "  cmake --preset win64-vs2022-clang-vulkan"
Write-Host "  cmake --build --preset win64-vs2022-clang-vulkan --config Release --target genesis_app genesis_runtime genesis_remote"
Write-Host ""
Write-Host "Install locally:" -ForegroundColor Cyan
Write-Host "  scripts\\install_vulkan_app_win64.bat"
Write-Host ""
Write-Host "Optional validation:" -ForegroundColor Cyan
Write-Host "  set EW_VK_VALIDATION=1"
Write-Host ""
Write-Host "If this script installed Visual Studio Build Tools or the Vulkan SDK, open a fresh terminal before building." -ForegroundColor Yellow
