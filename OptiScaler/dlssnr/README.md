# DLSS 5 Neural Rendering (`OptiScaler/dlssnr`)

A self-contained module that drives NVIDIA's DLSS Neural Rendering model (`nvngx_dlssnr.dll`, NGX
feature 18) over the frames OptiScaler already handles. Nothing in it is officially supported by
NVIDIA; the model ships in driver packages and is not redistributed here.

## For maintainers: how to remove it

**Set `OPTI_DLSSNR` to `0` in `dlssnr/DlssNr_Switch.h`.** That is the whole procedure. The switch
lives in a header containing nothing but the macro, so `Config.h` can test it without pulling in
D3D12.

With it at `0`: the guarded call sites vanish, the three module sources become empty translation
units, the settings panel loses its section, and the `[DlssNr]` config entries are not even
declared. Verified by building it both ways — the resulting `OptiScaler.dll` contains **zero**
occurrences of `DlssNr`, `dlssnr` or `Neural Rendering`, and is 117 KB smaller.

To remove the *source* as well: delete this directory, drop `dlssnr_forwarder.vcxproj` from the
solution, and delete the eight `#if OPTI_DLSSNR` blocks listed below. Nothing else refers to it.

| File | Blocks | What the calls do |
|---|---|---|
| `inputs/NVNGX_DLSS_Dx12.cpp` | 2 | the pass after an upscale, on each of the two evaluate routes |
| `menu/menu_common.cpp` | 2 | the settings panel, and the cost row in the timing table |
| `upscalers/IFeature_Dx11wDx12.cpp` | 1 | the pass inside the D3D11-on-D3D12 bridge |
| `Config.h` / `Config.cpp` | 3 | the `[DlssNr]` declarations and their read/write runs |

One change outside the module is **a genuine upstream fix, separable on its own and worth taking
regardless of this feature**: `shaders/output_scaling/OS_Dx12.cpp` sized its dispatch from the global
current feature rather than from the resources passed in. Those coincide for the conventional Output
Scaling chain, so the bug stayed invisible until something else called it.

## Files

| File | Role |
|---|---|
| `DlssNr_Switch.h` | the `OPTI_DLSSNR` macro, and nothing else |
| `DlssNr.h` | umbrella header; documents the call sites |
| `DlssNr_Dx12.h/.cpp` | the model: forwarder loading, feature lifetime, the single evaluate path, encode/resolve orchestration, white point metering, capture |
| `DlssNr_Menu.cpp` | the settings panel |
| `DlssNr_Codec.h` | the compute shader: encode (scale and sRGB-encode with a soft knee), resolve (RenoDX's two-branch composition, OkLab hue correction, AP1 clamp), downsample |
| `DlssNr_Capture.h` | matched before/after frame dumps |
| `forwarder/` | the caller-gate shim, built by `dlssnr_forwarder.vcxproj` into the release layout |

## Attribution

The colour composition -- the two-branch luminance ratio, the OkLab hue correction and the blend
between a luminance-only result and the model's own colour -- is **taken from RenoDX's DLSS 5 addon
by clshortfuse** (https://github.com/clshortfuse/renodx). It is their design, reimplemented here with
different names; that does not make it ours. See `Licenses/RenoDX_ATTRIBUTION.txt`, which must carry
their upstream licence text before any build is distributed.

What is not theirs: the OkLab matrices are Bjorn Ottosson's published constants, and the AP1, sRGB
and PQ transforms are standard colour science.

## Why a forwarder DLL exists

The model's snippet resolves the module that owns its caller's return address and refuses any whose
path does not contain `nvngx.dll`. The forwarder (`nvngx.dll_dlssnr.dll`, ~13 KB) exists only to
satisfy that check; every NGX call to the model originates from it. It contains no NVIDIA code, is
part of the solution, and builds with everything else.

## Design notes worth knowing before changing anything

- **Ratio composition, not a delta.** The model is shown an encoded proxy; what it returns is
  composed back as a ratio against the original's luminance, scaled by a measured slope, with the
  chroma added. Composing it additively — which earlier revisions did — discards the model's
  behaviour in highlights and makes every arrangement look alike. At strength zero the frame is
  bit-identical, always.
- **Create-time parameters.** The model's tuning (preset, style, intensity, local *) is latched at
  feature creation; changes rebuild the feature after a settle. The driver's parameter block is not
  the SDK header's vtable (floats sit at slot 6); the forwarder probes it. Rebuilding every frame
  exhausts the driver's latches and the feature stops responding until the process restarts, which
  is why the rebuild is debounced.
- **Never free under the GPU.** Every retired feature or surface is parked and freed 32 evaluates
  later; every internal feature is created on a private queue and fenced before use. Both rules were
  paid for with device hangs.
- **One lock.** Every caller is on the game's render thread now, but the D3D11-on-D3D12 bridge
  enters from its own call site, and the lock is CPU-side on a path that already records command
  lists. It was added after a period of crashes that looked random and were not.
- **Temporal filtering of the model's answer was measured to be a dead end** (twice, including with
  a trained DLAA pass): the model re-decides detail with the framing, so old answers do not belong
  to new frames. There is no accumulator; the composition is re-anchored to the model every frame
  instead, which is what makes it steady.
- **The model's own UI correction went with it.** It only ever acted on a UI layer the game tagged
  through Streamline, which almost no title does, and it could not be shown to change anything when
  one did. Removing it removed the Streamline tag hook as well, so the module no longer touches that
  file at all. The model is created with the parameter at its own default.
- **HUD detection was tried and removed.** Measured with grain, chromatic aberration and depth of
  field all off, a static HUD pixel still scored 0.31 on the "did not change" test, because game
  interfaces are translucent and animated. Separation from the world was 2.5:1 — not a detector at
  any threshold. The interface is safe because the pass runs before it is drawn, not because
  anything looks for it.
- **The split pipeline was removed.** It ran Ray Reconstruction at 1:1, the model on that frame,
  then an internal Super Resolution pass to the target size, to give the model a real temporal
  accumulator behind it. It was removed once the plain path did the same job — but note that the
  plain path had a bug that stopped it running the model at all, so the split was never fairly
  compared. If detail shimmers in motion, that is the thing to look at again; it is in the history.
