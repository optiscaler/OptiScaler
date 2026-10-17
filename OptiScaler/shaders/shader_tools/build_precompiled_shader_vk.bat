@echo off

if "%~1"=="" (
    echo Usage: %~nx0 ShaderName
    exit /b 1
)

set ShaderName=%1

rem Anything after the name is passed on to dxc
set ExtraArgs=
:collect
if "%~2"=="" goto build
set ExtraArgs=%ExtraArgs% %2
shift /2
goto collect
:build

echo Creating Vulkan Spir-V
"%~dp0dxc.exe" -spirv -T cs_6_0 -E CSMain -O3 -Qstrip_debug -D VK_MODE -Cc -Vi "%ShaderName%.hlsl" -Fo "%ShaderName%_Shader_Vk.spv"%ExtraArgs%

echo Creating Vulkan Header
python "%~dp0create_header.py" "%ShaderName%_Shader_Vk.spv" "%ShaderName%_Shader_Vk.h" %ShaderName%_spv
