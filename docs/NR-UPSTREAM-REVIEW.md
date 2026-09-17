# Neural Rendering review

NR adds optional image processing before/after upscaling or on the finished picture. Separate-edit routes reconstruct the clean game image and upscale NR's contribution independently. It defaults off, runs inside OptiScaler and requires a separately supplied NVIDIA NR runtime.

## Scope

The proposal starts at official master `5ee53e38`, including [compatibility PR #1157](https://github.com/optiscaler/OptiScaler/pull/1157). [NR PR #1158](https://github.com/optiscaler/OptiScaler/pull/1158) includes that prerequisite until merged. Other [compatibility fixes](COMPATIBILITY-CHANGES.md) share NR integration points; [every changed path is mapped](NR-UPSTREAM-DIFF-INVENTORY.md).

Features include multipass/tuning, model resolution, strength/skin controls, HDR/exposure, capture, timing, hold/comparison and private edit enlargement. Source RR adds motion accumulation; private RR needs compatible native guides. Post/finished **Matched residual + DLSS** uses SR. D3D11/Vulkan bridges share D3D12 processing; native Vulkan has no private adapter. See [controls](NR-PIPELINE-UI.md) and [implementation](../OptiScaler/dlssnr/README.md).

Fork MFG unlocks, external-FG mode, residual interpolation, NVFP4 selection and helper-DLL packaging are removed. Official FG/MFG remains. Removed INI keys are ignored on load and deleted on save without resetting other settings.

## Game checks — 13 September 2026

Windows, RTX 5090, driver 616.64; existing saves/settings and INIs preserved. Installed clean build: `9c27f775 / 20260913_052119`, SHA-256 `87BBEDDB99FAA6D4EEF7DE04A36D114DFB4A9E1BCF71AAA2019BB061ECC78BA4`. The review build additionally retains newer upstream DX11 swapchain adaptation; its build/regression checks do not inherit these gameplay results.

| Game / route | Observations |
| --- | --- |
| BG3 / DX11→DX12 DLSS, 1440p→4K HDR10 | Earlier `c855e93d`: ~15 minutes across all placements, 1/2 passes, 50/81% scale, three enlargement choices and deferred hold. Clean follow-up: early/post 100%, post 56% spatial/DLSS and finished 56% DLSS, two passes. Both private paths evaluated; no crash. |
| KCD2 / DX12 DLSS SR, 1440p→4K SDR | Earlier `b3618c05`: ~14 minutes across placements. Precision retest used the clean build's production shader with bounded diagnostics: ~13 minutes, post/finished 48% spatial/DLSS and hold. No crash; dense speckles removed, faint dark-scene grid remains. Deferred brightness/banding unresolved. |
| Jedi: Survivor / DX12 DLSS SR, 2260×1272→4K HDR10 | Invalid depth SRV initially caused device removal. Fixed diagnostic run exceeded four minutes. Clean ~5-minute retest covered main placements, finished 48% spatial/DLSS, 1/2 passes and a jump; no device removal. |
| Hogwarts Legacy / DX12 RR, 2558×1439→4K HDR10 | Earlier `b3618c05`: ~15 minutes, placements, two passes, 53% enlargement choices, hold/comparison. Clean ~7-minute follow-up: finished/deferred+finished 100%, finished 43% spatial/DLSS, post 43% DLSS, two passes. No crash. Earlier possible grid remains unresolved. |
| Cyberpunk 2077 | User reported the submission-fixed build working well. Clean DLL installed/hash verified; no independent certification of every mode. |

All five received the clean DLL; earlier tests cover different builds. BG3's follow-up reported FG OFF/Reflex inactive; KCD2 reported its HDR10 requirement. These sessions do not establish ordinary FG/MFG quality or cover every option combination.

## Automated checks

Release x64 passed: integration 26 warnings, review 64, zero errors. Review GPU-lifetime, pipeline-capture, NGX proxy and WARP window/composition regressions passed. The installed-runtime Streamline test reproduced the old Reflex access violation and passed active-plugin calls; it does not test FG presentation.

Precision tests passed production HLSL on WARP and Vulkan on RTX 5090; matching binaries/headers were regenerated. Prior guide, timing, active-region and residual tests support synthetic coverage. Packaging passed with an explicit manifest, excluding NVIDIA NR/private FG runtimes and custom helpers.

## Limits

HDR brightness, temporal smearing and reduced-resolution grids need quality work. Clipping also appeared with NR application off, so its cause is unresolved. RR accumulation adds 112.5 MiB at 1440p and lacks depth-based disocclusion rejection.

Short runs and mixed-mode memory samples cannot prove long-session stability. Native Vulkan/Proton gameplay, other GPUs/drivers, prolonged traversal/combat, loading, HDR mappings and exhaustive hold/FG/mode combinations remain unverified. This is a draft for review.
