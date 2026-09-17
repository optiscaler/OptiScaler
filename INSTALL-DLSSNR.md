# Install Neural Rendering

NR is experimental and disabled by default. Do not use injection mods in anti-cheat-protected multiplayer games.

## Requirements

- A 64-bit game using an OptiScaler D3D12 path, a supported D3D11/Vulkan bridge, or native Vulkan NR.
- An NVIDIA driver whose installed NGX core supports NR (feature 18).
- The complete OptiScaler package and a separately supplied `nvngx_dlssnr.dll`.

NR runs inside OptiScaler. No NR helper DLL is required; remove the obsolete `nvngx.dll_dlssnr.dll` when upgrading. Keep the package's ordinary backend dependencies.

### Runtime identification

These are the 310.8 variants used during development, not a verified GPU support matrix. The installed driver must accept the runtime.

| Variant | Intended GPUs | SHA-256 |
| --- | --- | --- |
| Original NVIDIA-signed | RTX 50 | `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E` |
| ShortFuse compatibility | RTX 20/30/40; retains RTX 50 path | `E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A` |

The compatibility runtime is available in [ShortFuse's pinned RenoDX thread](https://discord.com/channels/1408098019194310818/1543976771920330884). Modification invalidates its NVIDIA signature: verify the hash and keep security protection enabled. Its RTX 20/30 path is substantially heavier. The RenoDX add-on itself is not needed; two NR injectors can conflict.

```powershell
Get-FileHash .\nvngx_dlssnr.dll -Algorithm SHA256
```

## Installation

1. Close the game and launcher; back up the existing OptiScaler files and INI.
2. Extract the complete package beside the **real game executable**, including backend folders.
3. Place `nvngx_dlssnr.dll` there and run `setup_windows.bat`. It selects a proxy name and creates an uninstaller.
4. On upgrades, replace the **proxy the game loads**: adding `OptiScaler.dll` beside an old `dxgi.dll` does not update it. Preserve your INI and other mods' loaders.
5. Press `Insert`, enable NR and start with one pass. For native Vulkan, enable NR in the INI before launch so device/swapchain support is prepared.

| Game | Executable directory | Tested/configured route |
| --- | --- | --- |
| Baldur's Gate 3 | `bin` | DX11: `Dx11Upscaler=dlss_12`; native Vulkan: `VulkanUpscaler=dlss` (gameplay unverified) |
| Hogwarts Legacy | `Phoenix/Binaries/Win64` | `dxgi.dll` |
| Cyberpunk 2077 | `bin/x64` | `dbghelp.dll`; existing loaders may need chaining |

Keep `[ProcessFilter] TargetProcessName=auto` for portable configurations. A different executable name intentionally disables injection, including the menu.

```ini
[DlssNr]
Enabled=true
RunBeforeSR=true
Passes=1
WorkingScale=1.0
```

## Placement and resolution

| Setting | Behaviour |
| --- | --- |
| Generate model before upscale | Edit the active input before SR or RR+SR. Off runs NR afterward. |
| Generate before upscale, apply after upscale | Upscale NR's edit separately; the game reconstructs its clean input. |
| Apply NR to the finished picture | Apply after effects/HUD. With early generation enabled, carry the edit to presentation. |

Model resolution is relative to the image at the selected stage. At 1440p input/4K output, 100% means 1440p before SR and 4K afterward. Lower percentages reduce NR work without changing the game's upscaler preset. See [pipeline controls](docs/NR-PIPELINE-UI.md) and [enlargement](docs/NR-DLSS-ENLARGEMENT.md).

Finished-picture NR supports D3D12, its D3D11 bridge and native Vulkan with supported SDR/HDR10/scRGB formats. It can alter HUD/menus. Early generation plus finished-picture application requires D3D12 or the D3D11 bridge; Vulkan cannot carry that private edit. Unsupported frames retain the game image. See [bridges](docs/NR-FINISHED-BRIDGES.md).

SR and RR share pass count, profiles and model resolution. Separate-edit placement stays manual. Keep genuine `nvngx_dlssd.dll` (RR) separate from `nvngx_dlssnr.dll` (NR). If Cyberpunk's RR option is unavailable with a `d3d12.dll` proxy, [a reported workaround](https://github.com/Dagherbou/OptiScaler_DLSSNR/issues/8) is `dxgi.dll`; check existing loaders first.

Turning off **Apply model** hides the edit while NR still runs; the master switch stops NR. GPU timings measure elapsed work and can overlap other work; compare total frame time for performance. A private upscaler adds cost beyond the NR model timer.

## Troubleshooting

Enable `[Log] LogToFile=true` and `LogLevel=2`, then enter a rendered scene.

- **No log/menu:** check executable directory, loaded proxy, process filter, quarantine and loader conflicts. Try `Alt+Insert` for alternate keyboard layouts.
- **Menu opens but ignores input:** try `[Hotfix] ManualInputPolling=true` and disable conflicting overlays.
- **Model initialization fails:** check runtime hash and driver support; include the exact log error in a report.
- **Unexpected model size:** inspect active/target/model dimensions and fallback messages; [padded input](docs/PADDED-PRESR.md) explains supported rectangles.

Follow [upstream OptiFG guidance](https://github.com/optiscaler/OptiScaler/wiki/OptiFG) for frame generation. NR success alone does not establish FG compatibility. See [tested games and remaining issues](docs/NR-UPSTREAM-REVIEW.md).
