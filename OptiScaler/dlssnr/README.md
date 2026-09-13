# Neural Rendering implementation

NR calls NVIDIA feature 18 through the installed driver's NGX core. Users supply `nvngx_dlssnr.dll`; there is no NR helper DLL. See [installation](../../INSTALL-DLSSNR.md) and [credits](../../docs/CREDITS.md).

## Ownership and dispatch

`IFeature_Dx12` and `IFeature_Vk` own model contexts, scratch/history, captures and timers. Resources are lazy; NR defaults off. Vulkan needs NR enabled before device/swapchain creation to prepare extensions and transfer usage.

Adapters pass explicit colour, depth, motion, output and frame metadata into shared pipeline ordering. D3D11/Vulkan bridges reuse D3D12 NR; native Vulkan owns its implementation. Hooks select live owners; menus exchange values/requests without holding GPU resources.

`RunBeforeSR` redirects owned colour before SR/RR+SR, then restores game bindings and resource states. Invalid pre-colour rectangles fall back after SR where valid. Colour must start at origin zero; depth/motion have independent regions.

| Route | Implementation notes |
| --- | --- |
| Ordinary pre/post NR | [Placement and independent pass histories](design/pre-sr-multipass.md) |
| Early generation, late application | [Private edit upscaling](../../docs/DEFERRED-NR-DLSS.md); source RR adds [motion accumulation](../../docs/RESIDUAL-ACROSS-RR.md) |
| Finished picture | Guides captured at the upscaler seam; [presentation bridges](../../docs/NR-FINISHED-BRIDGES.md) |
| Reduced post/finished NR | [Matched residual + DLSS](../../docs/NR-DLSS-ENLARGEMENT.md), always private SR |

## Source map

Paths are relative to `OptiScaler/dlssnr` unless prefixed with `shaders/`, which starts at `OptiScaler`.

| Files | Purpose |
| --- | --- |
| `DlssNr_Pipeline_Dx12.*`, `DlssNrPipeline_Vk.h` | Upscaler adapters |
| `DlssNrFeature_Vk*`, `DlssNrFinished_Vk*` | Native Vulkan model/resources/presentation |
| `DlssNr_Exposure*` | Discovery, readback and calibration |
| `DlssNr_Menu*`, `DlssNr_PipelineUi.h` | Controls and chart |
| `shaders/dlssnr/DlssNr_Dx12_{Run,Encode,Evaluate}*` | Frame processing and composition |
| `shaders/dlssnr/DlssNr_Dx12_{Models,Resources,State,ModelState}*` | Model/resource ownership |
| `shaders/dlssnr/DlssNr_Dx12_{DeferredSr,Enlarge}*`, `shaders/dlssnr/DlssNr_Upscaler_Dx12*` | Private upscaling |
| `shaders/dlssnr/DlssNr_Dx12_{Late,FinishedQueue,FinishedCompose}*` | Capture, submission and presentation |
| `shaders/dlssnr/DlssNr_Dx12_{Exposure,Hold}*` | Exposure and held inputs |
| `shaders/dlssnr/DlssNr_Common.h`, `shaders/dlssnr/precompile/*.hlsl` | Shared codec contract and shaders |

D3D12 completion markers protect retirement; CPU frame counts only pair logical frames. Replaced owners stay registered until recordings and GPU work finish. Unresolved teardown work remains alive for process exit. Vulkan drains before resource replacement and separates creation/evaluation with events. See [GPU lifetime](../../docs/NR-GPU-RETIREMENT.md).

The exposure scanner accepts one device until safe shutdown; per-feature ownership does not imply unrestricted multi-device NGX support.

## Validation

From an x64 Visual Studio developer PowerShell:

```powershell
msbuild OptiScaler.sln /m /p:Configuration=Release /p:Platform=x64 /p:PostBuildEventUseInBuild=false
./tests/dlssnr_proxy/run.ps1
./tests/run_nr_gpu_lifetime.ps1
./tests/run_nr_pipeline_capture.ps1
./tests/run_nr_private_upscaler_smoke.ps1
```

Shader edits must regenerate DX12 and Vulkan binaries/headers with matching constants and stable operation numbers. C++ units include `pch.h` first per [CONTRIBUTING.md](../../CONTRIBUTING.md). See [runtime test boundaries](../../tests/nr_private_upscaler_smoke.md) and [game results](../../docs/NR-UPSTREAM-REVIEW.md).
