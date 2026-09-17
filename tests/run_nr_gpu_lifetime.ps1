# Run from a Visual Studio developer PowerShell (cl.exe on PATH). Uses WARP, no NGX runtime.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path ([System.IO.Path]::GetTempPath()) ('nr-lifetime-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $build | Out-Null
Set-Content -LiteralPath (Join-Path $build 'pch.h') -Value '// Standalone production-helper build.'
Set-Content -LiteralPath (Join-Path $build 'Util.h') -Value '#include <unknwn.h>
namespace TestUtil { inline IUnknown* wrapped = nullptr; inline IUnknown* real = nullptr; }
namespace Util { inline bool CheckForRealObject(const char*, IUnknown* object, IUnknown** real)
{ if (object != TestUtil::wrapped) return false; *real = TestUtil::real; return true; } }'
& cl.exe /nologo /std:c++20 /EHsc /W4 /DNOMINMAX "/I$build" "/Fo$build/" "/Fe$build/smoke.exe" `
    "$PSScriptRoot/nr_gpu_lifetime_smoke.cpp" "$repo/OptiScaler/dlssnr/DlssNr_GpuLifetime.cpp" d3d12.lib dxgi.lib ole32.lib
if ($LASTEXITCODE -ne 0) { throw 'NR lifetime test compilation failed.' }
& "$build/smoke.exe"
if ($LASTEXITCODE -ne 0) { throw "NR lifetime test failed (exit $LASTEXITCODE)." }
