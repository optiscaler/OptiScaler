# Assemble a release zip.
#
# The build output directory is not the release: it also holds import libraries, export files, debug
# symbols, and whatever earlier experiments left behind. Shipping that folder wholesale is how a
# release ends up containing a DLL nobody meant to publish, so this copies an explicit list and
# refuses anything not on it.
#
# What is deliberately NOT here: nvngx_dlssnr.dll. That is NVIDIA's, it is not ours to redistribute,
# and the user supplies their own copy per game folder. Only the ~108 KB forwarder ships.

param(
    [string]$Version = "v0.1.0-dlssnr",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

# Derived rather than hardcoded, so this packages whichever checkout it is sitting in. There is more
# than one now -- the experiment runs in a git worktree beside the main tree, and a hardcoded root
# silently packages the other one's build output while reporting success.
$root = Split-Path -Parent $PSCommandPath
$msb = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
$stage = "$root\release\$Version"
$zip = "$root\release\OptiScaler-DLSSNR-$Version.zip"

if (-not $SkipBuild) {
    foreach ($proj in @("$root\OptiScaler\dlssnr\forwarder\dlssnr_forwarder.vcxproj", "$root\OptiScaler.sln")) {
        $out = & $msb $proj /p:Configuration=Release /p:Platform=x64 /v:minimal /m 2>&1
        $err = $out | Select-String "error "
        if ($err) { Write-Host "FAILED: $proj"; $err | Select-Object -First 6; exit 1 }
    }
    Write-Host "built"
}

$src = "$root\x64\Release\a"

# The forwarder is taken from its own build output, not from the shared folder. The solution build
# does not reliably rebuild it, and a stale one here would ship silently.
#
# A fresh checkout has no per-project output directory until that project has been built on its own,
# so fall back to the shared folder rather than failing. The export check below is what actually
# guards against a stale one, and it runs either way.
$forwarder = "$root\OptiScaler\dlssnr\forwarder\x64\Release\a\nvngx.dll_dlssnr.dll"

if (-not (Test-Path $forwarder)) {
    $forwarder = "$root\x64\Release\a\nvngx.dll_dlssnr.dll"
    Write-Host "forwarder: using the shared build output ($forwarder)"
}

$exports = @("dlssnr_call_create", "dlssnr_call_evaluate", "dlssnr_call_set_extras")
$bytes = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($forwarder))
$missing = @($exports | Where-Object { $bytes.IndexOf($_) -lt 0 })

if ($missing.Count -gt 0) {
    Write-Host "STALE forwarder: missing $($missing -join ', ')"
    exit 1
}

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# Files, then folders. Anything not named here does not ship.
$files = @(
    "OptiScaler.dll",
    "OptiScaler.ini",
    "setup_windows.bat",
    "setup_linux.sh",
    "!! EXTRACT ALL FILES TO GAME FOLDER !!",
    "READ ME - DLSS Neural Rendering.txt"
)

foreach ($f in $files) {
    if (Test-Path "$src\$f") { Copy-Item "$src\$f" "$stage\$f" -Force }
    else { Write-Host "missing from build output: $f" }
}

foreach ($d in @("Licenses", "OptiScaler")) {
    Copy-Item "$src\$d" "$stage\$d" -Recurse -Force
}

Copy-Item $forwarder "$stage\nvngx.dll_dlssnr.dll" -Force

# Logging on, in the release only.
#
# Upstream ships LogToFile=auto, which resolves to false, and the source ini is theirs -- changing it
# in the repo would put a log-behaviour change into a PR that is about neural rendering. But this is
# an experimental build whose notes ask people to attach OptiScaler.log, and the first release shipped
# asking for a file that was never written.
#
# Info rather than Trace: every line explaining why the pass did not start is Info or worse, so it
# answers the common report at almost no cost. Crash reports need Trace and synchronous writes, and
# the notes say so rather than everyone paying for it.
$iniPath = "$stage\OptiScaler.ini"
$ini = Get-Content $iniPath -Raw
$ini = $ini -replace '(?m)^LogToFile=auto', 'LogToFile=true'
$ini = $ini -replace '(?m)^LogLevel=auto', 'LogLevel=2'
Set-Content $iniPath $ini -Encoding utf8 -NoNewline

$check = Select-String -Path $iniPath -Pattern '^LogToFile=|^LogLevel=' | ForEach-Object { $_.Line }
Write-Host "log settings: $($check -join ', ')"

# Belt and braces: nothing that is a build artifact, and nothing from the abandoned warp work, may
# survive into the zip regardless of how it got into the staging folder.
Get-ChildItem $stage -Recurse -Include *.exp, *.lib, *.pdb, *.ilk, *latewarp* | Remove-Item -Force

# No feature may ship switched on by accident.
#
# A global regex on "^Enabled=auto" once turned on five sections at once -- output scaling,
# sharpening, the magnifier and two more -- while trying to enable one, because the ini has six keys
# called Enabled in six different sections. That was in a test install rather than a release, and
# only because nothing was checking. This checks.
$on = Select-String -Path "$stage\OptiScaler.ini" -Pattern '^Enabled=true'

if ($on) {
    Write-Host "REFUSING: the packaged ini has features switched on:"
    $on | ForEach-Object { "  line $($_.LineNumber): $($_.Line)" }
    exit 1
}

Write-Host "ini verified: nothing switched on by default"

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip -CompressionLevel Optimal

Write-Host ""
Write-Host "staged at $stage"
Get-ChildItem $stage | ForEach-Object { "  {0,-42} {1,10:N0}" -f $_.Name, $_.Length }
Write-Host ""
Write-Host ("zip: {0}  ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
