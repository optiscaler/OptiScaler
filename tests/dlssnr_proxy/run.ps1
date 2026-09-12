# Run from a Visual Studio developer PowerShell with cl.exe on PATH.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = Join-Path ([System.IO.Path]::GetTempPath()) ('optiscaler-dlssnr-tests-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path (Join-Path $build 'proxies') -Force | Out-Null
foreach ($header in @('pch.h', 'Config.h', 'Logger.h', 'd3d12.h', 'proxies/NVNGX_Proxy.h')) {
    Set-Content -LiteralPath (Join-Path $build $header) -Value '// Dependency supplied by MockNgx.h.'
}
& cl.exe /nologo /std:c++20 /EHsc /W4 "/FI$PSScriptRoot/MockNgx.h" "/I$build" `
    "/I$repo/external/nvngx_dlss_sdk" "/Fo$build/ProxyTests.obj" "/Fe$build/ProxyTests.exe" `
    "$PSScriptRoot/ProxyTests.cpp"
if ($LASTEXITCODE -ne 0) { throw 'DLSS-NR proxy test compilation failed.' }
& "$build/ProxyTests.exe"
if ($LASTEXITCODE -ne 0) { throw 'DLSS-NR proxy regression tests failed.' }
Write-Output 'DLSS-NR proxy regression tests passed.'
