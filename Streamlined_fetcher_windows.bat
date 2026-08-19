@echo off
setlocal enabledelayedexpansion

REM Config
set "REPO=NVIDIA-RTX/Streamline"
set "DEST=%~dp0streamline"
set "WORKDIR=%TEMP%\streamline_dl_%RANDOM%"
set "ZIPFILE=%WORKDIR%\release.zip"
set "EXTRACTDIR=%WORKDIR%\extracted"

echo ==========================
echo  Streamline files fetcher
echo ==========================
echo v1.0
echo.
echo.
echo [1] Download latest Streamline DLLs
echo.
echo [2] Delete "streamline" folder
echo.
choice /c 12 /n /m "Select an option (1 or 2): "
echo.

if errorlevel 2 goto :removal
if errorlevel 1 goto :downloader

:removal
echo.
if exist "%DEST%" (
    echo Deleting "streamline" folder and all its contents...
    rmdir /s /q "%DEST%"
    echo Done.
) else (
    echo Folder "streamline" does not exist. Nothing to delete.
)
echo.
pause
exit /b 0

:downloader

mkdir "%WORKDIR%" 2>nul
mkdir "%EXTRACTDIR%" 2>nul
if not exist "%DEST%" mkdir "%DEST%"

REM Locate the latest ZIP release
echo --- Looking up latest release info ---
echo.
powershell -NoProfile -Command ^
    "$ErrorActionPreference='Stop';" ^
    "$r = Invoke-RestMethod -Uri 'https://api.github.com/repos/%REPO%/releases/latest' -Headers @{ 'User-Agent' = 'batch-script' };" ^
    "$asset = $r.assets | Where-Object { $_.name -like '*.zip' } | Select-Object -First 1;" ^
    "if (-not $asset) { Write-Error 'No .zip asset found in latest release.'; exit 1 }" ^
    "$asset.browser_download_url | Out-File -Encoding ascii '%WORKDIR%\asset_url.txt';" ^
    "$asset.name | Out-File -Encoding ascii '%WORKDIR%\asset_name.txt';" ^
    "Write-Host ('Found asset: ' + $asset.name)"

if not exist "%WORKDIR%\asset_url.txt" (
    echo ERROR: Could not determine the latest release asset URL.
    goto :cleanup_fail
)

set /p ASSET_URL=<"%WORKDIR%\asset_url.txt"
set /p ASSET_NAME=<"%WORKDIR%\asset_name.txt"

if "%ASSET_URL%"=="" (
    echo ERROR: Asset URL was empty.
    goto :cleanup_fail
)

echo.
echo Download URL: %ASSET_URL%
echo.

REM Download the ZIP
echo --- Downloading %ASSET_NAME% ---
echo.
curl -L --fail -o "%ZIPFILE%" "%ASSET_URL%"
if errorlevel 1 (
    echo ERROR: Download failed.
    goto :cleanup_fail
)
echo.

REM ZIP extraction
echo --- Extracting archive ---
powershell -NoProfile -Command "Expand-Archive -LiteralPath '%ZIPFILE%' -DestinationPath '%EXTRACTDIR%' -Force"
if errorlevel 1 (
    echo ERROR: Extraction failed.
    goto :cleanup_fail
)
echo.

REM Locate DLL files and transfer to folder
echo --- Searching for bin\x64 folders and copying .dll files ---
set FOUND=0
for /f "delims=" %%D in ('dir /ad /b /s "%EXTRACTDIR%" ^| findstr /i "\\bin\\x64$"') do (
    set FOUND=1
    echo   Found: %%D
    xcopy "%%D\*.dll" "%DEST%\" /Y /Q >nul
)

if "%FOUND%"=="0" (
    echo WARNING: No bin\x64 folder was found inside the archive.
    echo The archive layout may have changed - check "%EXTRACTDIR%" manually.
    goto :cleanup_fail
)

echo.
echo Done. DLLs copied to: %DEST%
echo.
dir "%DEST%\*.dll" /b

goto :cleanup_success

:cleanup_fail
echo.
echo Script finished with errors. Temp files kept at:
echo   %WORKDIR%
echo.
pause
exit /b 1

:cleanup_success
rmdir /s /q "%WORKDIR%" 2>nul
echo.
echo Temp files cleaned up.
echo.
pause
exit /b 0
