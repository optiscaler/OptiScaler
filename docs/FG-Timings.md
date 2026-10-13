# Frame generation GPU timing

The menu footer displays `FG: <milliseconds>` beside the upscaler GPU cost. Hover it for the backend,
measured scope, rolling average, completed-sample count and sample age. `Off` means disabled/paused;
`N/A` means there is no fresh completed measurement. Missing data is never converted into zero cost.

## Measured routes

| Route | Scope and conditions |
| --- | --- |
| OptiScaler FSR-FG, DX12 | The generation callback's GPU workload, including optical flow and interpolation. Preparation and presentation/UI work outside that dispatch are excluded. Applies to the FFX runtime selected by the user, including its FSR 4 FG route. |
| OptiScaler FSR-FG, Vulkan | A generation GPU interval on an observed one-time command buffer with a known executing queue and completed SDK submission. The start/end pipeline stages can include preceding pending GPU work, including HUD preparation. Queues without timestamp support are unavailable. |
| Reprojection, DX12 | Reprojection dispatch and its output copies. |
| NGX to FFX, DX12 | One NGX evaluation's GPU work, including preparation and generation. The sample ID is an evaluation sequence, not a proven game/Present frame identity. |
| Nukem FSR-FG, DX12 | One NGX evaluation, only when `DLSSG.IsRecording` confirms an already-recording command list. Paths which reset or close it internally are unavailable at this boundary. |
| Combo, DX12 | Only the FSR-generated middle frame's evaluation. This is explicitly partial timing, not the full hybrid generation batch. |
| NVIDIA driver reports, DX12 | The real driver's documented `aiFrameTimeUs`, converted to milliseconds. Requires valid latency markers and populated completed reports. The vendor's stage boundaries and per-generated-frame interpretation are unspecified. No multiplier division is applied. |

DX11-to-DX12 interop uses the same timed DX12 output paths. FG input labels do not require separate
timers: an input converted to a measured output receives that output's measurement.

## Unavailable routes

XeFG's public API does not document a GPU FG duration or profiling ABI. The pinned Intel DLL exports
private profiling functions, but their signatures, enabling flags and result ownership are unpublished;
this build does not call them with guessed structures. XeLL's public frame reports contain CPU marker
timestamps, which cannot substitute for GPU interpolation time.

Opaque Enabler evaluations, native SDK passthrough without a usable driver report, Vulkan NGX mods,
and unobserved/unsupported execution routes display `N/A`. The public NVIDIA Vulkan report does not
expose the D3D `aiFrameTimeUs` field. Future XeFG submission instrumentation must establish coverage
and attribution, including the distinction between interpolation and proxy copies/UI composition.

## Interpretation and validation

These numbers describe scoped GPU execution intervals. Async work can overlap other queues, so
adding upscaler and FG milliseconds does not establish an FPS penalty or input latency. Different
vendor-reported scopes are not guaranteed to be directly comparable.

Readback uses bounded resources and never waits on the CPU for GPU completion or inserts queue-idle/dependency
waits. Samples are published only after observed completion and are deduplicated. Hook setup uses Detours thread
enlistment. DX12 readback additionally requires allocator-generation retirement through normal API usage. Context epochs
prevent delayed results from repopulating reset history. Measurements older than one second expire.
Unsafe/untracked or exhausted timing paths skip collection rather than reuse GPU resources early.

The supported producer paths record new work for each dispatch. Arbitrary instrumentation chains which replay
the same recording through unobserved queue implementations are unsupported for timing attribution; their
execution queue and timestamp frequency cannot be inferred. Late hook installation and third-party hook chains
also require runtime stress validation.
The API observers remain installed for process lifetime so in-progress game callbacks cannot lose
their trampolines during FG teardown. Timing resources and device metadata retire independently.

The standalone CPU tests cover invalid values, sample identity, epochs, history, freshness and Vulkan
timestamp conversion. A successful DLL build establishes compilation, not game/runtime acceptance.
Before treating a route as runtime-qualified, compare the scopes against PIX/Nsight/Intel Inspector;
exercise async on/off, MFG changes, repeated dispatch, FG pause/resume, resize/alt-tab and shutdown;
and compare frame pacing with collection enabled and disabled.

## Sources

- [Microsoft D3D12 GPU timing](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing)
- [NVIDIA public NVAPI](https://github.com/NVIDIA/nvapi/blob/9b181ea572f680327fe01a14a0f1f41c78034104/nvapi.h)
- [Intel public XeFG API](https://github.com/intel/xess/blob/8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0/inc/xess_fg/xefg_swapchain.h)
- [Intel engineering discussion of FG timing and copy overhead](https://github.com/intel/xess/issues/65#issuecomment-4284347595)
- [Nukem command-list lifecycle](https://github.com/Nukem9/dlssg-to-fsr3/blob/0e253f63c3c66ee7582406bfa3dec698df7ab2c1/source/maindll/FFFrameInterpolatorDX.cpp)
