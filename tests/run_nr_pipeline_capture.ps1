$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path ([System.IO.Path]::GetTempPath()) ('nr-pipeline-build-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $build | Out-Null
Set-Content -LiteralPath (Join-Path $build 'pch.h') -Value '// Standalone production-helper build.'
Set-Content -LiteralPath (Join-Path $build 'Util.h') -Value '#include <unknwn.h>
namespace Util { inline bool CheckForRealObject(const char*, IUnknown*, IUnknown**) { return false; } }'
& cl.exe /nologo /std:c++20 /EHsc /W4 /DNOMINMAX "/I$build" "/Fo$build/" "/Fe$build/smoke.exe" `
    "$PSScriptRoot/nr_pipeline_capture_smoke.cpp" "$repo/OptiScaler/dlssnr/DlssNr_GpuLifetime.cpp" d3d12.lib dxgi.lib ole32.lib
if ($LASTEXITCODE -ne 0) { throw 'Pipeline capture compilation failed.' }
& "$build/smoke.exe"
if ($LASTEXITCODE -ne 0) { throw 'Pipeline capture test failed.' }
