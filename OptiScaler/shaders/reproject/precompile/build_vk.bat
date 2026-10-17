@echo off
pushd "%~dp0"

rem Vulkan bindings are the HLSL registers shifted by type, Reproject_Vk.cpp uses the same numbers
set Shifts=-fvk-b-shift 0 0 -fvk-t-shift 10 0 -fvk-u-shift 30 0 -fvk-s-shift 50 0

for %%s in (reproject reproject_mask_h reproject_mask_v reproject_latch reproject_cache reproject_stats) do (
    call "..\..\shader_tools\build_precompiled_shader_vk.bat" %%s %Shifts%
)

popd
