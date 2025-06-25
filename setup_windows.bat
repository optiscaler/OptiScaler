REM Setup OptiScaler for your game
@echo off
cls
echo  ::::::::  :::::::::  ::::::::::: :::::::::::  ::::::::   ::::::::      :::     :::        :::::::::: :::::::::  
echo :+:    :+: :+:    :+:     :+:         :+:     :+:    :+: :+:    :+:   :+: :+:   :+:        :+:        :+:    :+: 
echo +:+    +:+ +:+    +:+     +:+         +:+     +:+        +:+         +:+   +:+  +:+        +:+        +:+    +:+ 
echo +#+    +:+ +#++:++#+      +#+         +#+     +#++:++#++ +#+        +#++:++#++: +#+        +#++:++#   +#++:++#:  
echo +#+    +#+ +#+            +#+         +#+            +#+ +#+        +#+     +#+ +#+        +#+        +#+    +#+ 
echo #+#    #+# #+#            #+#         #+#     #+#    #+# #+#    #+# #+#     #+# #+#        #+#        #+#    #+# 
echo  ########  ###            ###     ###########  ########   ########  ###     ### ########## ########## ###    ### 
echo.
echo Coping is strong with this one...
echo.

del "!! EXTRACT ALL FILES TO GAME FOLDER !!" 2>nul

setlocal enabledelayedexpansion

if not exist OptiScaler.dll (
    echo OptiScaler "OptiScaler.dll" file is not found!
    goto end
)

REM Set paths based on current directory
set "gamePath=%~dp0"
set "optiScalerFile=%gamePath%\OptiScaler.dll"
set setupSuccess=false

REM Check if the Engine folder exists
if exist "%gamePath%\Engine" (
    echo Found Engine folder, if this is an Unreal Engine game then please extract Optiscaler to #CODENAME#\Binaries\Win64
    echo.
    
    set /p continueChoice="Continue installation to current folder? [y/n]: "
    set continueChoice=!continueChoice: =!

    if "!continueChoice!"=="y" (
        goto selectFilename
    )

    goto end
)

REM Prompt user to select a filename for OptiScaler
:selectFilename
echo.
echo Choose a filename for OptiScaler (default is dxgi.dll):
echo  [1] dxgi.dll
echo  [2] winmm.dll
echo  [3] version.dll
echo  [4] dbghelp.dll
echo  [5] d3d12.dll
echo  [6] wininet.dll
echo  [7] winhttp.dll
echo  [8] OptiScaler.asi
set /p filenameChoice="Enter 1-8 (or press Enter for default): "

if "%filenameChoice%"=="" (
    set selectedFilename="dxgi.dll"
) else if "%filenameChoice%"=="1" (
    set selectedFilename="dxgi.dll"
) else if "%filenameChoice%"=="2" (
    set selectedFilename="winmm.dll"
) else if "%filenameChoice%"=="3" (
    set selectedFilename="version.dll"
) else if "%filenameChoice%"=="4" (
    set selectedFilename="dbghelp.dll"
) else if "%filenameChoice%"=="5" (
    set selectedFilename="d3d12.dll"
) else if "%filenameChoice%"=="6" (
    set selectedFilename="wininet.dll"
) else if "%filenameChoice%"=="7" (
    set selectedFilename="winhttp.dll"
) else if "%filenameChoice%"=="8" (
    set selectedFilename="OptiScaler.asi"
) else (
    echo Invalid choice. Please select a valid option.
    echo.
    goto selectFilename
)

if exist %selectedFilename% (
    echo.
    echo WARNING: %selectedFilename% already exists in the current folder.
    echo.
    set /p overwriteChoice="Do you want to overwrite %selectedFilename%? [y/n]: "
    set overwriteChoice=!overwriteChoice: =!
    
    echo.
    if "!overwriteChoice!"=="y" (
        goto checkWine
    )

    goto selectFilename
)

REM Wine doesn't support powershell
:checkWine
reg query HKEY_CURRENT_USER\Software\Wine >nul 2>&1
if %errorlevel%==0 (
    echo.
    echo Using wine, skipping over spoofing checks.
    echo If you need, you can disable spoofing by setting Dxgi=false in the config
    echo.
    pause
    goto completeSetup
) 

if exist %windir%\system32\nvapi64.dll (
    echo.
    echo Nvidia driver files detected.
    set isNvidia=true
) else (
    set isNvidia=false
)

REM Query user for GPU type
echo.
echo Are you using an Nvidia GPU or AMD/Intel GPU?
echo [1] AMD/Intel
echo [2] Nvidia
if "%isNvidia%"=="true" (
    set /p gpuChoice="Enter 1 or 2 (or press Enter for Nvidia): "
) else (
    set /p gpuChoice="Enter 1 or 2 (or press Enter for AMD/Intel): "
)

REM Skip spoofing if Nvidia
if "%gpuChoice%"=="2" (
    goto completeSetup
)

if "%gpuChoice%"=="" (
    if "%isNvidia%"=="true" (
        goto completeSetup
    )
)

REM Query user for DLSS
echo.
echo Will you try to use DLSS inputs? (enables spoofing, required for DLSS FG, Reflex-^>AL2)
echo [1] Yes
echo [2] No
set /p enablingSpoofing="Enter 1 or 2 (or press Enter for Yes): "

set configFile=OptiScaler.ini
if "%enablingSpoofing%"=="2" (
    if not exist "%configFile%" (
        echo Config file not found: %configFile%
        pause
    )

    powershell -Command "(Get-Content '%configFile%') -replace 'Dxgi=auto', 'Dxgi=false' | Set-Content '%configFile%'"
)

:completeSetup
REM Rename OptiScaler file
echo.
if "!overwriteChoice!"=="y" (
    echo Removing previous %selectedFilename%...
    del /F %selectedFilename% 
)

echo Renaming OptiScaler file to %selectedFilename%...
rename "%optiScalerFile%" %selectedFilename%
if errorlevel 1 (
    echo.
    echo ERROR: Failed to rename OptiScaler file to %selectedFilename%.
    goto end
)

goto create_uninstaller
:create_uninstaller_return

cls
echo  OptiScaler setup completed successfully...
echo.
echo   ___                 
echo  (_         '        
echo  /__  /)   /  () (/  
echo          _/      /    
echo.

set setupSuccess=true

:end
pause

if "%setupSuccess%"=="true" (
    REM Remove "setup_windows.bat"
    del %0
)

exit /b

:create_uninstaller
copy /y NUL "Remove setup_windows.bat"

(
echo @echo off
echo cls
echo echo  ::::::::  :::::::::  ::::::::::: :::::::::::  ::::::::   ::::::::      :::     :::        :::::::::: :::::::::  
echo echo :+:    :+: :+:    :+:     :+:         :+:     :+:    :+: :+:    :+:   :+: :+:   :+:        :+:        :+:    :+: 
echo echo +:+    +:+ +:+    +:+     +:+         +:+     +:+        +:+         +:+   +:+  +:+        +:+        +:+    +:+ 
echo echo +#+    +:+ +#++:++#+      +#+         +#+     +#++:++#++ +#+        +#++:++#++: +#+        +#++:++#   +#++:++#:  
echo echo +#+    +#+ +#+            +#+         +#+            +#+ +#+        +#+     +#+ +#+        +#+        +#+    +#+ 
echo echo #+#    #+# #+#            #+#         #+#     #+#    #+# #+#    #+# #+#     #+# #+#        #+#        #+#    #+# 
echo echo  ########  ###            ###     ###########  ########   ########  ###     ### ########## ########## ###    ### 
echo echo.
echo echo Coping is strong with this one...
echo echo.
echo.
echo set /p removeChoice="Do you want to remove OptiScaler? [y/n]: "
echo.
echo if "%%removeChoice%%"=="y" ^(
echo    del OptiScaler.log
echo    del OptiScaler.ini
echo    del %selectedFilename%
echo    del /Q D3D12_Optiscaler\*
echo    rd D3D12_Optiscaler
echo    del /Q DlssOverrides\*
echo    rd DlssOverrides
echo    del /Q Licenses\*
echo    rd Licenses
echo    echo.
echo    echo OptiScaler removed!
echo    echo.
echo ^) else ^(
echo    echo.
echo    echo Operation cancelled.
echo    echo.
echo ^)
echo.
echo pause
echo if "%%removeChoice%%"=="y" ^(
echo 	del %%0
echo ^)
) >> "Remove OptiScaler.bat"

echo.
echo Uninstaller created.
echo.

goto create_uninstaller_return


