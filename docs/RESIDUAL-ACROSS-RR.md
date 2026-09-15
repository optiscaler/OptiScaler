# Accumulating NR edits across RR

**Generate before upscale, apply after upscale** leaves the game's SR/RR input clean and enlarges NR's edit separately. Enable **Apply NR to the finished picture** to compose after effects/HUD. Ordinary pre-upscale NR does not enable this route automatically.

For source RR, form the scene-linear difference `edited - original`, reproject the previous difference with game motion vectors, then blend in the current edit. `ResidualAcrossRRBlend` controls new contribution: default 0.08, range 0.01–1. Cold/reset history and invalid motion fade in from zero.

Two render-resolution FP32 histories add 112.5 MiB at 2560×1440. Cuts, skipped/unmatched calls and mode changes reset them; GPU retirement protects their lifetime. There is no depth-based disocclusion rejection, so lag/smearing remain possible.

Compose the accumulated difference at input resolution, encode the signed or finished-picture log-gain carrier, then enlarge with the selected private upscaler. Its temporal history is separate from accumulation. DLSS uses private RR with compatible native material guides, otherwise SR. Finished-picture HDR response matching remains available.

Legacy `RunBeforeSR=true` plus `ResidualAcrossRR=true` selects this route. D3D12 and its bridges support the private seam; early-to-finished composition supports D3D12/D3D11. Native Vulkan has no private adapter. Failed or unsupported frames keep the clean image.

Synthetic checks cover reprojection, reset/history, seam pairing, composition and private contexts. See [configuration](DEFERRED-NR-DLSS.md), [private RR](NR-PRIVATE-RR.md) and [game evidence](NR-UPSTREAM-REVIEW.md).
