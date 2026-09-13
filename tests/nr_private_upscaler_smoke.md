# Private NR upscaler smoke test

Requires PowerShell, Visual Studio C++ tools, Windows SDK and supported hardware/runtime binaries:

```powershell
./tests/run_nr_private_upscaler_smoke.ps1 -Backend FSR22
./tests/run_nr_private_upscaler_smoke.ps1 -Backend FFX -Runtime 'C:/installed/amd_fidelityfx_upscaler_dx12.dll'
./tests/run_nr_private_upscaler_smoke.ps1 -Backend XeSS -Runtime 'C:/installed/libxess.dll'
./tests/run_nr_private_upscaler_smoke.ps1 -Backend DLSS -Runtime 'C:/installed/nvngx.dll' -SrDirectory 'C:/installed/DLSS'
```

For private RR, add `-RayReconstruction -CyberpunkProfile` to the DLSS command and supply `nvngx_dlssd.dll` alongside SR. The profile option renames the offscreen harness, not the game. `-VcVars` overrides Visual Studio discovery. Output goes to ignored `x64/nr-private-upscaler-smoke`; nothing is downloaded/installed. FSR22 uses linked libraries; DLSS selects NVIDIA hardware.

The harness compiles the production adapter with standalone loader/PCH seams. Backend creation, transitions, evaluation and destruction are unchanged; game discovery, spoofing and hooks are bypassed. It checks creation of the requested SR or RR feature.

Two contexts run neutral/signed carriers for eight evaluations, exercising reset/history, unit exposure, zero motion, UAV guide-state restoration and missing-guide rejection. Readback checks finite pixels and signal preservation. Fences precede allocator reset/destruction; debug messages are checked when the SDK layer exists.

DLSS, FSR22, FidelityFX and XeSS passed locally on 12 September 2026 without the debug layer. The Cyberpunk-profile RR check also passed with HDR creation; see [private RR](../docs/NR-PRIVATE-RR.md). These static tests do not establish moving-scene quality, game hooks, mode-switch stress or delayed/replayed-command lifetime safety.
