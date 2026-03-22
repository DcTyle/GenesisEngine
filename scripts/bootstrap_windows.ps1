# Genesis Engine Vulkan + ClangCL bootstrap (Win64)
# Installs or verifies the canonical Windows toolchain used by the Vulkan app.

$ErrorActionPreference = "Stop"

$VulkanSdkVersion = "1.4.341.1"

function Test-CommandAvailable([string]$Name) {
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Get-UserVulkanSdkRoot() {
    $sdkBase = Join-Path $env:LOCALAPPDATA "Programs\VulkanSDK"
    if (-not (Test-Path $sdkBase)) {
        return $null
    }
    $latestDir = Get-ChildItem -Path $sdkBase -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($null -eq $latestDir) {
        return $null
    }
    return $latestDir.FullName
}

function Set-UserEnvironmentPathOnce([string]$BinPath) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $segments = @()
    if (-not [string]::IsNullOrWhiteSpace($userPath)) {
        $segments = $userPath -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    }
    if ($segments -notcontains $BinPath) {
        $newPath = if ($segments.Count -gt 0) {
            (($segments + $BinPath) -join ';')
        } else {
            $BinPath
        }
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    }
}

function Use-VulkanSdk([string]$SdkRoot) {
    $binPath = Join-Path $SdkRoot "Bin"
    $env:VULKAN_SDK = $SdkRoot
    $env:VK_SDK_PATH = $SdkRoot
    [Environment]::SetEnvironmentVariable("VULKAN_SDK", $SdkRoot, "User")
    [Environment]::SetEnvironmentVariable("VK_SDK_PATH", $SdkRoot, "User")
    Set-UserEnvironmentPathOnce $binPath
    if (($env:PATH -split ';') -notcontains $binPath) {
        $env:PATH = $binPath + ";" + $env:PATH
    }
}

function Install-LocalVulkanSdk() {
    $sdkRoot = Join-Path $env:LOCALAPPDATA ("Programs\VulkanSDK\" + $VulkanSdkVersion)
    $glslcPath = Join-Path $sdkRoot "Bin\glslc.exe"
    if (-not (Test-Path $glslcPath)) {
        $installerUrl = "https://sdk.lunarg.com/sdk/download/$VulkanSdkVersion/windows/vulkansdk-windows-X64-$VulkanSdkVersion.exe"
        $installerPath = Join-Path $env:TEMP ("vulkansdk-windows-X64-" + $VulkanSdkVersion + ".exe")
        Write-Host ("Downloading Vulkan SDK " + $VulkanSdkVersion + "...") -ForegroundColor Yellow
        Invoke-WebRequest -Uri $installerUrl -OutFile $installerPath
        Write-Host ("Installing Vulkan SDK into " + $sdkRoot + "...") -ForegroundColor Yellow
        & $installerPath --root $sdkRoot --accept-licenses --default-answer --confirm-command install copy_only=1
        if ($LASTEXITCODE -ne 0) {
            throw ("Local Vulkan SDK install failed with exit code " + $LASTEXITCODE)
        }
    }
    Use-VulkanSdk $sdkRoot
    Write-Host ("Using Vulkan SDK at " + $sdkRoot) -ForegroundColor Green
}

function Install-WingetPackage([string]$Id, [string]$OverrideArgs = "") {
    if (-not (Test-CommandAvailable "winget")) {
        throw "winget is required to install missing prerequisites automatically."
    }
    $wingetInstallParams = @(
        "install",
        "--id", $Id,
        "--accept-source-agreements",
        "--accept-package-agreements"
    )
    if (-not [string]::IsNullOrWhiteSpace($OverrideArgs)) {
        $wingetInstallParams += @("--override", $OverrideArgs)
    }
    Write-Host ("Installing " + $Id + "...") -ForegroundColor Yellow
    & winget @wingetInstallParams
    if ($LASTEXITCODE -ne 0) {
        throw ("winget install failed for " + $Id + " with exit code " + $LASTEXITCODE)
    }
}

function Get-VsWherePath() {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        "D:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return $null
}

function Test-BuildToolsFallbackPath() {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"),
        "D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return $null
}

function Use-NsisIfPresent() {
    $candidates = @(
        "C:\Program Files (x86)\NSIS\makensis.exe",
        "D:\Program Files (x86)\NSIS\makensis.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            $nsisDir = Split-Path -Parent $candidate
            Set-UserEnvironmentPathOnce $nsisDir
            if (($env:PATH -split ';') -notcontains $nsisDir) {
                $env:PATH = $nsisDir + ";" + $env:PATH
            }
            return $candidate
        }
    }
    return $null
}

Write-Host "Genesis Engine Vulkan bootstrap (Win64)" -ForegroundColor Cyan

if (-not (Test-CommandAvailable "cmake")) {
    Install-WingetPackage "Kitware.CMake"
} else {
    Write-Host "Found cmake." -ForegroundColor Green
}

$vsWhere = Get-VsWherePath
$hasVsClang = $false
if (-not [string]::IsNullOrWhiteSpace($vsWhere) -and (Test-Path $vsWhere)) {
    $installPath = & $vsWhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                  Microsoft.VisualStudio.Component.VC.Llvm.Clang `
        -property installationPath 2>$null
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installPath)) {
        $hasVsClang = $true
        Write-Host ("Found Visual Studio toolchain at " + $installPath) -ForegroundColor Green
    }
}
if (-not $hasVsClang) {
    $vsDevCmd = Test-BuildToolsFallbackPath
    if (-not [string]::IsNullOrWhiteSpace($vsDevCmd)) {
        $hasVsClang = $true
        Write-Host ("Found Visual Studio Build Tools fallback at " + $vsDevCmd) -ForegroundColor Green
    }
}
if (-not $hasVsClang) {
    $override = "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
    Install-WingetPackage "Microsoft.VisualStudio.2022.BuildTools" $override
}

if (-not [string]::IsNullOrEmpty($env:VULKAN_SDK) -or (Test-CommandAvailable "glslc") -or (Test-CommandAvailable "glslangValidator")) {
    Write-Host "Found Vulkan shader toolchain." -ForegroundColor Green
} elseif ($userVulkanSdk = Get-UserVulkanSdkRoot) {
    Use-VulkanSdk $userVulkanSdk
    Write-Host ("Found user-local Vulkan SDK at " + $userVulkanSdk) -ForegroundColor Green
} else {
    Install-LocalVulkanSdk
}

if (-not (Test-CommandAvailable "makensis")) {
    Install-WingetPackage "NSIS.NSIS"
    $nsisPath = Use-NsisIfPresent
    if (-not [string]::IsNullOrWhiteSpace($nsisPath)) {
        Write-Host ("Found NSIS installer toolchain at " + $nsisPath) -ForegroundColor Green
    }
} else {
    Write-Host "Found NSIS installer toolchain." -ForegroundColor Green
}

Write-Host ""
Write-Host "Canonical preset:" -ForegroundColor Cyan
Write-Host "  win64-vs2022-clang-vulkan"
Write-Host ""
Write-Host "Configure/build:" -ForegroundColor Cyan
Write-Host "  cmake --preset win64-vs2022-clang-vulkan"
Write-Host "  cmake --build --preset win64-vs2022-clang-vulkan"
Write-Host ""
Write-Host "Editor-only build:" -ForegroundColor Cyan
Write-Host "  cmake --build --preset win64-vs2022-clang-vulkan-editor"
Write-Host ""
Write-Host "Runtime-only build:" -ForegroundColor Cyan
Write-Host "  cmake --build --preset win64-vs2022-clang-vulkan-runtime"
Write-Host ""
Write-Host "Tests:" -ForegroundColor Cyan
Write-Host "  ctest --preset win64-vs2022-clang-vulkan-tests"
Write-Host ""
Write-Host "Install locally:" -ForegroundColor Cyan
Write-Host "  scripts\\install_vulkan_app_win64.bat"
Write-Host ""
Write-Host "Package ZIP / installer:" -ForegroundColor Cyan
Write-Host "  cpack --preset win64-vs2022-clang-vulkan"
Write-Host "  scripts\\package_vulkan_app_win64.bat"
Write-Host ""
Write-Host "Optional validation:" -ForegroundColor Cyan
Write-Host "  set EW_VK_VALIDATION=1"
Write-Host ""
Write-Host "If this script installed Visual Studio Build Tools or the Vulkan SDK, open a fresh terminal before building." -ForegroundColor Yellow
