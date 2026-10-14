@echo off
setlocal
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
if not exist tests\out mkdir tests\out
if not defined VSCMD_VER (
  "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > tests\out\vs-install.txt
  for /f "usebackq delims=" %%i in ("tests\out\vs-install.txt") do call "%%i\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
)
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /fp:precise tests\FgTimingTests.cpp /Fo:tests\out\FgTimingTests.obj /Fe:tests\out\FgTimingTests.exe
if errorlevel 1 exit /b 1
tests\out\FgTimingTests.exe
exit /b %errorlevel%
