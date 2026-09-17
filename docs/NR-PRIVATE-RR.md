# Private RR for early NR residuals

With DLSS selected as the private upscaler, compatible native D3D12 RR guides select an independent RR context. This applies to early edits composed after SR/RR or at presentation. Placement stays manual. Source RR first uses [motion accumulation](RESIDUAL-ACROSS-RR.md).

The pre-upscale seam snapshots albedo, normals, roughness, reflection motion/hit distances, offsets and camera matrices. Private RR receives the encoded residual, its own output/history and those guides in **linear HDR mode, exposure 1**. No PQ/game exposure is applied to the carrier. Game colour/output remain separate; borrowed guide states are restored and aliases transition once.

Missing/incompatible guides use private SR. DX11/Vulkan bridges lack RR material/reflection guides and retain SR; native Vulkan has no private adapter. Post/finished **Matched residual + DLSS** always uses SR. Generation changes retire safely; failures keep the clean frame and report the NGX error.

## Cyberpunk initialization

Cyberpunk's executable profile rejected LDR RR creation with `0xBAD00005` (InvalidParameter), while the same standalone call succeeded under another name. Adding `NVSDK_NGX_DLSS_Feature_Flags_IsHDR` fixed both creation and evaluation in the production-adapter profile test. Ordinary SR retains LDR creation.

The [test runner](../tests/nr_private_upscaler_smoke.md) can use `-RayReconstruction -CyberpunkProfile`; it renames the offscreen harness, not the game. Two Quality-mode contexts at 1440p→4K with inverted depth passed. Neutral samples ranged about 0.4993–0.5005; signed bands were preserved within roughly 0.0005. This establishes initialization/broad signal preservation, not exact neutrality or motion quality; the debug layer was unavailable.

RR sees compressed edits while its guides describe the original scene. That mismatch remains experimental. Guide requirements follow NVIDIA's [RR integration guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md) and `nvsdk_ngx_defs_dlssd.h`.
