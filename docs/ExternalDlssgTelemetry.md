# External DLSSG source-frame telemetry

When DLSSG is managed outside OptiScaler (for example by a DLL/ASI or ReShade
add-on), OptiScaler may have no active FG backend from which to obtain a
multiplier. The FPS overlay can optionally read source-frame telemetry from
an already loaded provider exporting `DLSSG_GetFrameTelemetry`.

Enable the FPS overlay as usual. With valid external data, the existing FPS
counter is followed by the measured source rate and the label
`DLSSG source telemetry`. The FPS-only layout omits the label. OptiScaler's
existing active FG backend always takes precedence over external telemetry.

This reader does not enable FG, select a backend, load a provider DLL, or
change presentation, Reflex, VSync or frame pacing. Keep generation enabled
through the game/external provider. Backend replacement is not needed merely
to display the measurement.

## Provider setup

A compatible upcoming `dlssg_for_sm86` build enables collection with this
setting in its own `dlssg_sm86.ini`, followed by a game restart:

```ini
[Telemetry]
Enabled=1
```

This is a provider setting, not an OptiScaler setting. Older provider builds
without the export cannot supply data by adding the INI key alone. The provider
DLL/ASI or standalone add-on must be loaded in the same process.

## Measurement and ABI

The version-1 layout is in `OptiScaler/menu/frame_telemetry_api.h`. The export
has signature `int __stdcall DLSSG_GetFrameTelemetry(DLSSGFrameTelemetry*)`.
The caller initializes `size` and `version`; nonzero return means the record
was read. The reader also checks layout/version, enabled and valid flags,
finite positive `base_fps` and age no greater than 1000 ms.

The current producer measures CPU submission cadence of successful, distinct
Streamline source-frame tokens over approximately 500 ms. It is not a GPU
completion rate, displayed-frame count, input-latency measurement or an
instantaneous dynamic multiplier. Duplicate token submissions do not add
frames. The first FPS counter retains OptiScaler's existing measurement;
its sampling window may differ from the source-rate window.

The reader searches loaded modules at most once per second until it finds
exactly one exporter, then pins that module to keep its function pointer valid.
Missing, disabled, invalid, stale or ambiguous data leaves the original overlay
unchanged. Unsupported producer integrations, including unobserved direct NGX
paths, likewise provide no source number. No data is consumed by reading it.
