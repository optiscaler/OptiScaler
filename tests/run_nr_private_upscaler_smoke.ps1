param(
    [ValidateSet('DLSS','FSR22','FFX','XeSS')][string]$Backend = 'FSR22',
    [string]$Runtime,
    [string]$SrDirectory,
    [string]$VcVars,
    [switch]$RayReconstruction,
    [ValidateSet(50,80)][int]$ScalePercent = 50,
    [switch]$CyberpunkProfile
)
$ErrorActionPreference = 'Stop'
if ($RayReconstruction -and $Backend -ne 'DLSS') { throw '-RayReconstruction requires -Backend DLSS.' }
if ($CyberpunkProfile -and !$RayReconstruction) { throw '-CyberpunkProfile requires -RayReconstruction.' }
$repo = Split-Path $PSScriptRoot -Parent
if (!$VcVars) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$installation) { throw 'Install Visual Studio C++ build tools, or supply -VcVars.' }
    $VcVars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
}
$out = Join-Path $repo 'x64/nr-private-upscaler-smoke'
New-Item -ItemType Directory -Force "$out/proxies" | Out-Null
# These include-path seams suppress the application PCH/loaders only. The adapter .cpp is unchanged.
foreach ($file in @('pch.h','proxies/NVNGX_Proxy.h','proxies/FfxApi_Proxy.h','proxies/XeSS_Proxy.h')) {
    Set-Content -LiteralPath (Join-Path $out $file) -Value '// Runtime-loader seam supplied by nr_private_upscaler_smoke.cpp'
}
$build = @"
@echo off
call "$VcVars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MD /DNR_SMOKE_SCALE=$ScalePercent /I "$out" /I OptiScaler /I OptiScaler/include /I external/nvngx_dlss_sdk /I external/xess/inc/xess /I external/FidelityFX-SDK/ffx-api/include/ffx_api tests/nr_private_upscaler_smoke.cpp /Fe:"$out/private_smoke.exe" /Fo:"$out/private_smoke.obj" /link /LIBPATH:OptiScaler/library/fsr2 ffx_fsr2_api_x64.lib ffx_fsr2_api_dx12_x64.lib d3d12.lib dxgi.lib dxguid.lib
"@
Set-Content -LiteralPath "$out/build.cmd" -Value $build
Push-Location $repo
try {
    & "$out/build.cmd"
    if ($LASTEXITCODE) { throw 'Private adapter smoke build failed.' }
    $arguments = @([array]::IndexOf(@('DLSS','FSR22','FFX','XeSS'),$Backend).ToString())
    if ($Backend -ne 'FSR22') {
        if (!$Runtime) { throw '-Runtime must name an installed runtime DLL.' }
        $arguments += (Resolve-Path -LiteralPath $Runtime).Path
    }
    if ($Backend -eq 'DLSS') {
        if (!$SrDirectory) { throw '-SrDirectory must contain your official nvngx_dlss.dll.' }
        $arguments += (Resolve-Path -LiteralPath $SrDirectory).Path
        if ($RayReconstruction) { $arguments += '--rr' }
    }
    $executable = "$out/private_smoke.exe"
    if ($CyberpunkProfile) {
        # Activate the NVIDIA executable-name profile with the offscreen harness, not the game binary.
        $executable = "$out/Cyberpunk2077.exe"
        Copy-Item -LiteralPath "$out/private_smoke.exe" -Destination $executable -Force
    }
    & $executable @arguments
    if ($LASTEXITCODE) { throw 'Private adapter GPU smoke failed.' }
} finally { Pop-Location }
