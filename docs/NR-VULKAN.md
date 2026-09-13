# Native Vulkan NR

Incorporates [y4my4my4m's commit 7b7220bb](https://github.com/y4my4my4m/OptiScaler_DLSSNR_Multipass_MFG/commit/7b7220bb) (GPL-3.0).

`IFeature_Vk` owns NR like RCAS/output scaling, using reverse setup and forward dispatch around SR or RR+SR. Early processing uses owned colour scratch; game pointers/layouts are restored. Invalid active rectangles fall back after SR where supported. [Depth/motion regions](NR-MOTION-METADATA.md) remain independent.

Passes own separate features/history, ping-pong outputs and compose once. A GPU event separates creation from evaluation. Profiles, resolution, filters and skin controls share D3D12 behaviour; Vulkan-to-D3D12 bridges run D3D12 NR only.

Native Vulkan supports exposure readback and [finished-picture NR](NR-FINISHED-BRIDGES.md). Exposure scanning and private-upscaler composition require D3D12. Enable NR before device/swapchain creation to prepare extensions/transfer usage; late enabling may require restart. Resources allocate lazily.

```ini
[Upscalers]
VulkanUpscaler=dlss
[DlssNr]
Enabled=true
RunBeforeSR=true
DeferredDLSS=false
Passes=1
WorkingScale=1.0
```

Game settings select SR quality; model scale is relative to the chosen NR stage. Production shader checks passed odd/padded sizes on RTX 5090. Native Vulkan/Proton gameplay, DRS and motion quality remain unverified. See [compatibility fixes](COMPATIBILITY-CHANGES.md) and [validation scope](NR-UPSTREAM-REVIEW.md).
