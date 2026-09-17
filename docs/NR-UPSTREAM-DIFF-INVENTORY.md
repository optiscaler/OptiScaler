# Upstream difference inventory

Reference: official master `5ee53e38`. Each group states why its changed files are retained. See [tests and limits](NR-UPSTREAM-REVIEW.md) and [compatibility scope](COMPATIBILITY-CHANGES.md).

## NR packaging and installation

- `.github/workflows/package_release.yml`
- `package_release.ps1`
- `setup_windows.bat`

## Exclude local artifacts and collapse generated shader diffs

- `.gitignore`
- `.gitattributes`

## NR documentation, review and attribution

- `INSTALL-DLSSNR.md`
- `Licenses/RenoDX_ATTRIBUTION.txt`
- `README.md`
- `docs/COMPATIBILITY-CHANGES.md`
- `docs/CREDITS.md`
- `docs/NR-UPSTREAM-REVIEW.md`
- `docs/NR-UPSTREAM-DIFF-INVENTORY.md`
- `docs/DEFERRED-NR-DLSS.md`
- `docs/NR-COMPATIBILITY.md`
- `docs/NR-DLSS-ENLARGEMENT.md`
- `docs/NR-FINISHED-BRIDGES.md`
- `docs/NR-GPU-RETIREMENT.md`
- `docs/NR-MOTION-METADATA.md`
- `docs/NR-NATIVE-STREAMLINE-PRESENT.md`
- `docs/NR-PHOTO-DIAGNOSTIC.md`
- `docs/NR-PIPELINE-UI.md`
- `docs/NR-PRIVATE-RR.md`
- `docs/NR-VULKAN.md`
- `docs/PADDED-PRESR.md`
- `docs/RESIDUAL-ACROSS-RR.md`

## NR settings and legacy-key cleanup

- `OptiScaler.ini`
- `OptiScaler/Config.cpp`
- `OptiScaler/Config.h`

## Register NR and bridge sources

- `OptiScaler/OptiScaler.vcxproj`
- `OptiScaler/OptiScaler.vcxproj.filters`

## Expose the DLSS w/DX12 backend

- `OptiScaler/OptiTypes.cpp`
- `OptiScaler/OptiTypes.h`

## NR menu/comparison and Vulkan overlay cleanup

- `OptiScaler/State.h`
- `OptiScaler/menu/menu_common.cpp`
- `OptiScaler/menu/menu_common.h`
- `OptiScaler/menu/menu_overlay_vk.cpp`

## NR ownership, scheduling, controls and GPU lifetime

- `OptiScaler/dlssnr/DlssNr.h`
- `OptiScaler/dlssnr/DlssNrFeature_Dx12.h`
- `OptiScaler/dlssnr/DlssNrFeature_Vk.cpp`
- `OptiScaler/dlssnr/DlssNrFeature_Vk.h`
- `OptiScaler/dlssnr/DlssNrFeature_Vk_Internal.h`
- `OptiScaler/dlssnr/DlssNrFeature_Vk_Model.cpp`
- `OptiScaler/dlssnr/DlssNrFeature_Vk_Resources.cpp`
- `OptiScaler/dlssnr/DlssNrFinished_Vk.cpp`
- `OptiScaler/dlssnr/DlssNrFinished_Vk.h`
- `OptiScaler/dlssnr/DlssNrPipeline_Vk.h`
- `OptiScaler/dlssnr/DlssNr_Capture.h`
- `OptiScaler/dlssnr/DlssNr_ExposureAnchors.cpp`
- `OptiScaler/dlssnr/DlssNr_ExposureReadback.cpp`
- `OptiScaler/dlssnr/DlssNr_ExposureScan.cpp`
- `OptiScaler/dlssnr/DlssNr_ExposureScan.h`
- `OptiScaler/dlssnr/DlssNr_ExposureScan_Internal.h`
- `OptiScaler/dlssnr/DlssNr_FinishedPictureBridge_Dx11.h`
- `OptiScaler/dlssnr/DlssNr_FinishedReady.h`
- `OptiScaler/dlssnr/DlssNr_GpuLifetime.cpp`
- `OptiScaler/dlssnr/DlssNr_GpuLifetime.h`
- `OptiScaler/dlssnr/DlssNr_HoldParameters_Dx12.h`
- `OptiScaler/dlssnr/DlssNr_Menu.cpp`
- `OptiScaler/dlssnr/DlssNr_MenuBlend.cpp`
- `OptiScaler/dlssnr/DlssNr_MenuInput.cpp`
- `OptiScaler/dlssnr/DlssNr_MenuModel.cpp`
- `OptiScaler/dlssnr/DlssNr_MenuOverlay.cpp`
- `OptiScaler/dlssnr/DlssNr_MenuOverlay.h`
- `OptiScaler/dlssnr/DlssNr_MenuPlacement.cpp`
- `OptiScaler/dlssnr/DlssNr_MenuSections.h`
- `OptiScaler/dlssnr/DlssNr_PipelineCapture.h`
- `OptiScaler/dlssnr/DlssNr_PipelineUi.h`
- `OptiScaler/dlssnr/DlssNr_Pipeline_Dx12.cpp`
- `OptiScaler/dlssnr/DlssNr_Pipeline_Dx12.h`
- `OptiScaler/dlssnr/DlssNr_Placement.h`
- `OptiScaler/dlssnr/DlssNr_Proxy.cpp`
- `OptiScaler/dlssnr/DlssNr_Proxy.h`
- `OptiScaler/dlssnr/DlssNr_Status.cpp`
- `OptiScaler/dlssnr/DlssNr_Status.h`
- `OptiScaler/dlssnr/DlssNr_StreamlinePicture.cpp`
- `OptiScaler/dlssnr/DlssNr_StreamlinePicture.h`
- `OptiScaler/dlssnr/DlssNr_Upscaler.h`
- `OptiScaler/dlssnr/DlssNr_VkExtensions.h`
- `OptiScaler/dlssnr/PassProfiles.h`
- `OptiScaler/dlssnr/README.md`
- `OptiScaler/dlssnr/design/frame-hold.md`
- `OptiScaler/dlssnr/design/multi-point-anchoring.md`
- `OptiScaler/dlssnr/design/pre-sr-multipass.md`

## KCD2 swapchain and Vulkan menu/FG compatibility

- `OptiScaler/framegen/dlssg/DLSSG_Dx12.cpp`

## Vulkan NR submission, state and presentation

- `OptiScaler/hooks/CommandBuffer_StateTracker.h`
- `OptiScaler/hooks/Vulkan_Hooks.cpp`
- `OptiScaler/hooks/VulkanwDx12_Hooks.cpp`

## Exposure discovery and root-signature restoration

- `OptiScaler/hooks/D3D12_Hooks.cpp`
- `OptiScaler/hooks/D3D12_Hooks.h`

## NR presentation, swapchain sizing and composition

- `OptiScaler/hooks/DxgiFactory_Hooks.cpp`
- `OptiScaler/hooks/DxgiFactory_Hooks.h`
- `OptiScaler/hooks/DxgiSwapchainSizing.h`
- `OptiScaler/hooks/FG_Hooks.cpp`

## Native NR presentation, capability ABI and Vulkan FG

- `OptiScaler/hooks/Streamline_Hooks.cpp`
- `OptiScaler/hooks/Streamline_Hooks.h`

## NR routing, feature registry and NGX lifetime

- `OptiScaler/inputs/NVNGX_DLSS_Dx11.cpp`
- `OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp`
- `OptiScaler/inputs/NVNGX_DLSS_Vk.cpp`
- `OptiScaler/inputs/NgxFeatureRegistry.h`

## KCD2 process-local Streamline query

- `OptiScaler/nvapi/NvApiHooks.cpp`

## NR runtime discovery and initialization

- `OptiScaler/proxies/NVNGX_Proxy.h`

## Active Streamline binding and NR search path

- `OptiScaler/proxies/Streamline_Proxy.h`

## Exposure observation, submissions and lifetime fences

- `OptiScaler/resource_tracking/ResTrack_dx12.cpp`
- `OptiScaler/resource_tracking/ResTrack_dx12.h`

## Depth SRV fix for Jedi device removal

- `OptiScaler/shaders/Shader_Dx12.cpp`

## NR execution, guides, timing, hold and private upscaling

- `OptiScaler/shaders/dlssnr/DlssNr_ActiveColor.h`
- `OptiScaler/shaders/dlssnr/DlssNr_Common.h`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12.h`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_DeferredSr.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_Encode.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_Enlarge.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_Evaluate.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_Exposure.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_FinishedCompose.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_FinishedQueue.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_Hold.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_Late.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_ModelState.h`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_Models.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_Resources.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_State.h`
- `OptiScaler/shaders/dlssnr/DlssNr_Dx12_Status.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_GpuTime.h`
- `OptiScaler/shaders/dlssnr/DlssNr_Guides.h`
- `OptiScaler/shaders/dlssnr/DlssNr_ResidualPair.h`
- `OptiScaler/shaders/dlssnr/DlssNr_SeamClock.h`
- `OptiScaler/shaders/dlssnr/DlssNr_Upscaler_Dx12.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Upscaler_Dx12.h`
- `OptiScaler/shaders/dlssnr/DlssNr_Vk.cpp`
- `OptiScaler/shaders/dlssnr/DlssNr_Vk.h`

## NR shaders and matching DX12/Vulkan binaries

- `OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader.cso`
- `OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader.h`
- `OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader_Vk.h`
- `OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader_Vk.spv`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr_finished_color.hlsl`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr_finished_color_Shader.cso`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr_finished_color_Shader.h`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr_finished_color_Shader_Vk.h`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr_finished_color_Shader_Vk.spv`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr_residual.hlsl`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr_residual_Shader.cso`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr_residual_Shader.h`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr_residual_Shader_Vk.h`
- `OptiScaler/shaders/dlssnr/precompile/dlssnr_residual_Shader_Vk.spv`

## Private NR scaling; preserve ordinary dimensions

- `OptiScaler/shaders/output_scaling/OS_Dx12.cpp`
- `OptiScaler/shaders/output_scaling/OS_Dx12.h`
- `OptiScaler/shaders/output_scaling/OS_Vk.cpp`
- `OptiScaler/shaders/output_scaling/OS_Vk.h`

## Vulkan device identification under Proton

- `OptiScaler/spoofing/Vulkan_Spoofing.cpp`

## Shared NR pipelines, bridges and resource restoration

- `OptiScaler/upscalers/FeatureProvider_Dx11.cpp`
- `OptiScaler/upscalers/FeatureProvider_Vk.cpp`
- `OptiScaler/upscalers/IFeature_Dx11.cpp`
- `OptiScaler/upscalers/IFeature_Dx11wDx12.cpp`
- `OptiScaler/upscalers/IFeature_Dx12.cpp`
- `OptiScaler/upscalers/IFeature_Dx12.h`
- `OptiScaler/upscalers/IFeature_Vk.cpp`
- `OptiScaler/upscalers/IFeature_Vk.h`
- `OptiScaler/upscalers/IFeature_VkwDx12.cpp`
- `OptiScaler/upscalers/NgxOptionalDx12Inputs.h`
- `OptiScaler/upscalers/ShaderPipeline_Dx12.h`
- `OptiScaler/upscalers/ShaderPipeline_Vk.h`
- `OptiScaler/upscalers/dlss/DLSSFeature.h`
- `OptiScaler/upscalers/dlss/DLSSFeature_Dx11.cpp`
- `OptiScaler/upscalers/dlss/DLSSFeature_Dx11On12.cpp`
- `OptiScaler/upscalers/dlss/DLSSFeature_Dx11On12.h`
- `OptiScaler/upscalers/dlss/DLSSFeature_Dx12.cpp`
- `OptiScaler/upscalers/dlss/DLSSFeature_Vk.cpp`
- `OptiScaler/upscalers/dlss/DLSSFeature_VkOn12.cpp`
- `OptiScaler/upscalers/dlss/DLSSFeature_VkOn12.h`
- `OptiScaler/upscalers/dlssd/DLSSDFeature.h`
- `OptiScaler/upscalers/dlssd/DLSSDFeature_Dx11.cpp`
- `OptiScaler/upscalers/dlssd/DLSSDFeature_Dx12.cpp`
- `OptiScaler/upscalers/dlssd/DLSSDFeature_Vk.cpp`

## Finished-picture swapchains and frame accounting

- `OptiScaler/with_dx12/dx11_with_dx12_sc.cpp`
- `OptiScaler/wrapped/wrapped_swapchain.cpp`
- `OptiScaler/wrapped/wrapped_swapchain.h`

## NR and compatibility regressions

- `tests/dlssnr_proxy/MockNgx.h`
- `tests/dlssnr_proxy/ProxyTests.cpp`
- `tests/dlssnr_proxy/run.ps1`
- `tests/dxgi_window_size_smoke.cpp`
- `tests/nr_active_color_smoke.cpp`
- `tests/nr_dx11_finished_bridge_smoke.cpp`
- `tests/nr_finished_color_smoke.cpp`
- `tests/nr_finished_queue_smoke.cpp`
- `tests/nr_gpu_lifetime_smoke.cpp`
- `tests/nr_gpu_time_smoke.cpp`
- `tests/nr_guides_smoke.cpp`
- `tests/nr_ngx_routing_smoke.cpp`
- `tests/nr_pipeline_capture_smoke.cpp`
- `tests/nr_private_upscaler_smoke.cpp`
- `tests/nr_private_upscaler_smoke.md`
- `tests/nr_residual_dlss_smoke.cpp`
- `tests/nr_residual_rr_smoke.cpp`
- `tests/nr_seam_clock_smoke.cpp`
- `tests/nr_skin_shader_smoke.cpp`
- `tests/nr_streamline_picture_smoke.cpp`
- `tests/nr_vulkan_shader_smoke.cpp`
- `tests/run_nr_gpu_lifetime.ps1`
- `tests/run_nr_pipeline_capture.ps1`
- `tests/run_nr_private_upscaler_smoke.ps1`
- `tests/streamline_active_plugin_smoke.cpp`

Official version metadata and submodule revisions are unchanged. Generated shaders account for much of the diff.
