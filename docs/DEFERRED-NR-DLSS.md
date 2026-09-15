# Generate early, apply later

Enable **Generate before upscale, apply after upscale**, then choose **Private NR upscaler**. NR edits an owned input copy; the game reconstructs its clean input. A private context enlarges only NR's contribution and applies it after SR/RR+SR. **Apply NR to the finished picture** moves composition after effects/HUD.

```ini
[DlssNr]
Enabled=true
DeferredDLSS=true
PrivateUpscaler=0
WorkingScale=1.0
Passes=1
```

Model scale is relative to the active render input. NR evaluates every rendered frame. Start with one pass, **Apply model** on and inspection views off for comparison.

| PrivateUpscaler | Backend |
| --- | --- |
| 0 (default) | DLSS; [private RR](NR-PRIVATE-RR.md) with compatible native RR guides, otherwise SR |
| 1 | Linked FSR 2.2 |
| 2 | Installed FidelityFX provider; FSR4 availability is not guaranteed |
| 3 | XeSS, if its supported input range contains the carrier size |

Missing/invalid values select DLSS. Backends use independent parameters/history, unit exposure and no sharpening, auto-exposure or game masks. Selection never changes the game's upscaler or NR's own runtime requirements. FidelityFX provider choice is independent of the main pass override.

## Image path

1. Copy the active colour to owned scratch and run NR/composition there. Strength and skin controls apply once.
2. On source RR, [accumulate the signed edit with motion vectors](RESIDUAL-ACROSS-RR.md).
3. For post-upscale application, encode `d = (edited - original) / preExposure` as `0.5 + 0.5*d/(1+abs(d))` in RGBA16F. For finished-picture application, encode bounded relative RGB log-gain instead.
4. Enlarge the carrier using copied jitter, depth, motion and camera metadata. Restore borrowed guide states.
5. Decode and compose with the clean output, preserving alpha. Finished-picture slots retain the edit/reference until presentation.

The carrier is a compressed RGB difference, not a lighting buffer. Nonlinear decoding and FP16/temporal filtering can distort it. Decode clamps signed magnitude to 0.999; final RGB is nonnegative. Finished-picture transfer approximates the unavailable game tone mapping.

## Scheduling and limits

The private seam supports D3D12 and its bridges; early-to-finished composition supports D3D12/D3D11. Native Vulkan has no private adapter. One upscale per submission epoch and a known same-device direct queue are required; multi-view and unusual async submission need further work.

Creation must be submitted before evaluation. Backend, size, format, device/queue, RR mode or destination changes create a new generation. Cuts, missed frames and generation changes reset history. Completion markers protect bounded retired generations; backlog, unsupported layouts/guides or runtime failures retain the clean frame. There is no automatic alternate-backend fallback. Restart is the reliable retry for a latched runtime failure.

Unresolved teardown work survives until process exit. The private pass, copies and final composition add GPU/VRAM cost beyond NR's timer; smaller model input does not guarantee a faster frame.

## Checks

Production-adapter hardware tests exercised two contexts, neutral/signed carriers, reset/history and guide-state restoration for DLSS, FSR 2.2, FidelityFX and XeSS. WARP checks cover carrier round trips, alpha and non-finite guards. These static checks do not establish motion quality or game scheduling. See [test commands](../tests/nr_private_upscaler_smoke.md), [GPU lifetime](NR-GPU-RETIREMENT.md) and [live results](NR-UPSTREAM-REVIEW.md).
