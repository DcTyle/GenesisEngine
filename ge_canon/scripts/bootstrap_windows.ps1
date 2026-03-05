# EigenWare Vulkan + ClangCL bootstrap (Win64)
# This script installs Vulkan SDK using winget and prints recommended cmake configure commands.

$ErrorActionPreference = "Stop"

Write-Host "EigenWare bootstrap (Win64)" -ForegroundColor Cyan

# Vulkan SDK
Write-Host "Checking Vulkan SDK..." -ForegroundColor Cyan
$vkSdk = $env:VULKAN_SDK
if ([string]::IsNullOrEmpty($vkSdk)) {
    Write-Host "VULKAN_SDK not set. Attempting to install LunarG Vulkan SDK via winget..." -ForegroundColor Yellow
    try {
        winget install --id LunarG.VulkanSDK --accept-source-agreements --accept-package-agreements
    } catch {
        Write-Host "winget install failed. Install Vulkan SDK manually from LunarG and re-run." -ForegroundColor Red
        throw
    }
} else {
    Write-Host "Found VULKAN_SDK=$vkSdk" -ForegroundColor Green
}

Write-Host "\nToolchain:" -ForegroundColor Cyan
Write-Host "- Visual Studio 2022" 
Write-Host "- ClangCL toolset (required for __int128)" 

Write-Host "\nConfigure/build:" -ForegroundColor Cyan
Write-Host "cmake -S . -B build -G \"Visual Studio 17 2022\" -A x64 -T ClangCL" 
Write-Host "cmake --build build --config Release --target GenesisEngineVulkan" 

Write-Host "\nOptional validation:" -ForegroundColor Cyan
Write-Host "set EW_VK_VALIDATION=1" 
