# NR controls and compatibility

## Skin controls

`AutoMask` requests NVIDIA's automatic mask. An explicit `DLSSNR.ControlMask` overrides it, so reusable parameter tables clear stale entries. See the [independent mask probes](https://github.com/kibblerz/DLSS5-Reshade-AIO/blob/main/lab/PRIVATE-CONTRACT-FINDINGS.md#controlmask). `SkinStructure=-1` follows `LocalStructure`; setting it to 0 does not disable every skin lighting/colour change.

**Skin and environment (final edit)** is a separate, opt-in colour filter, applied once after all model passes on DX12/Vulkan. Its four strengths default to 1. It adds no model pass and does not expose NVIDIA's semantic mask. The preview shows selection in white: warm materials can match, while coloured lighting can hide skin.

Colour off preserves hue/chroma in fully selected pixels; lighting can still change brightness. Set detail/lighting to 0 too to restore those pixels completely. Soft edges blend skin/environment settings. `tests/nr_skin_shader_smoke.cpp` checks identity, bypass, separation, preview and colour preservation on WARP; it does not validate NVIDIA's mask.

## Onimusha: Way of the Sword

Both `onimushawots_demo.exe` and `onimushawots.exe` receive the restore/spoofing defaults addressing [reported NR crashes](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/22). An explicit old `[Hotfix] RestoreComputeSignature=false` overrides them; use `auto` or `true`.

State restoration covers model creation, recreation, evaluation and early returns. NR skips recording when required state cannot be restored and rejects partially returned handles after creation errors. This is a candidate fix: the retail game/RTX 30 case was not reproduced here.

For a report, verify the loaded proxy was replaced; test one pass with FG off, then loading/fast travel and FSR output separately. Include driver, runtime hash, DLSS version, executable, INI and log. These changes do not establish that every loading crash is fixed.

## BG3 buffer routing

NVIDIA's DX11 parameter table accepts bridge DX12 resources through `void*`, but ignores typed `ID3D12Resource*` setters/getters. Post-SR NR previously read an unwritten image; pre-SR colour replacement never reached DLSS. Deferred placement avoided those substitutions.

The shared pipeline now preserves the table's supported access type when redirecting/restoring resources. Production helpers were checked against mock tables and NVIDIA's real DX11 table.

## Experimental HDR brightness transfer

With early generation and finished-picture application enabled, select **Apply NR edit → Match HDR brightness response (experimental)**. `[DlssNr] HdrTransfer=false` defaults off. It supports scene-linear input with HDR10/scRGB output on DX12 and the DX11 bridge. SDR/nonlinear input uses the existing transfer; native Vulkan lacks this early-to-finished route.

At presentation, compare 1,024 samples against a fence-protected clean SR reference, normalized by pre-exposure. Local regression and outlier rejection fit 48 half-stop luminance bins over −12..12 stops. Consistent curves are smoothed; resets, resize, stale slots and major exposure/colour changes invalidate history.

Application estimates `finished + T(edited scene) - T(clean scene)` for luminance, retaining the existing RGB transfer's chromaticity. Missing, inconsistent or non-monotonic fits fall back to bounded transfer; neutral edits preserve the image. This cannot recover full colour grading, local tone mapping, bloom or HUD composition.

Cost: a full-size reference per used slot, small curve textures and GPU analysis/composition reads; no CPU readback or extra NR pass. Disabling safely retires optional resources. Shader operations 6–9 leave 0–5 stable.

`tests/nr_finished_color_smoke.cpp` checks HDR/SDR conversion, identity, fallback and history. A synthetic shoulder test reduced summed luminance error from 603.58 to 9.37; game colour accuracy and overhead remain unverified. See [live results](NR-UPSTREAM-REVIEW.md) and [other compatibility fixes](COMPATIBILITY-CHANGES.md).
