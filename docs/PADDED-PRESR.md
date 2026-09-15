# Padded pre-upscale inputs

D3D12 NR accepts an origin-zero active image inside a larger single-sample 2D colour texture. A 2558×1439 image in a 2560×1440 allocation runs at the active size, instead of unnecessarily falling back to post-upscale 4K.

Copy the active rectangle into compact UAV scratch, process it, then copy only that rectangle back on success. Padding is untouched; no resampling is added and the game texture need not support UAVs. Model size is active size × `WorkingScale`. Hold, comparison and capture use this compact image.

Size changes rebuild histories/models and can cause DRS overhead. Nonzero colour origins, partial/out-of-bounds dimensions, arrays and MSAA use guarded post-upscale fallback. Both dimensions zero mean allocation size. SR/RR share placement controls; this note describes the D3D12 path.

## Verification

Replace the proxy actually loaded by the game. With early generation, one pass and 100% model resolution, inspect `OptiScaler.log` for `staging active ... from padded Color allocation` and `running before SR: target ..., model ...`. Fallback messages identify unsupported dimensions/origins; odd-size model rejection is a separate runtime issue.

From an x64 VS prompt:

```bat
cl /nologo /std:c++20 /EHsc tests\nr_active_color_smoke.cpp /Fe:x64\nr_active_color_smoke.exe /Fo:x64\nr_active_color_smoke.obj /link d3d12.lib dxgi.lib
x64\nr_active_color_smoke.exe
```

WARP readback checked every cropped/copied-back pixel for 2558×1439, 2227×1253 and exact 1920×1080 inputs, no-UAV textures, failed evaluation and rejected layouts. The debug layer was unavailable. This does not validate NVIDIA model acceptance of every odd size or establish a Dawnwalker-specific fix.
