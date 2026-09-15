# NR pipeline controls

Click a chart box to open its settings. Linear routes use one column; a separate edit branches and rejoins where applied. Muted boxes show game stages. The chart shows configuration; runtime status reports execution or fallback. **Inspect NR** is collapsible below the chart.

| Controls | Purpose |
| --- | --- |
| Enable NR / Apply model | Stop NR work / hide its edit while still evaluating |
| Generate model before upscale | Run on the active input before SR or RR+SR |
| Generate before upscale, apply after upscale | Process a separate edit; forces early generation |
| Apply NR to the finished picture | Apply after game effects/HUD, directly or using the early edit |
| Prepare NR input | Model resolution, scaling filters, enlargement, HDR mapping and exposure |
| Exposure calibration | Scan candidate, white-point anchors, inversion and trim |
| NR model | Pass count and per-pass style, intensity, local structure/tone, skin structure and auto mask |
| Apply NR edit | Detail/colour strength, highlight guard and optional skin/environment filter |
| Inspect NR | Hold, comparisons, wipe/zoom, labels and debug views |

Ordinary routes run prepare → model → compose together before or after SR/RR. Finished-picture mode puts them after effects/HUD. Separate-edit routes leave the game input clean and join after SR/RR or at presentation. Depth/motion are guides, not colour stages; the chart does not configure FG.

The pass selector shows **1** in muted green and **2** in muted red, scaled with HDR text brightness. Later passes inherit pass 1 except local tone (default zero); reset clears overrides. Sliders commit on release. Preset hints remain advanced because their effect is unverified.

The model node is green. Timing shows measured NR GPU elapsed time plus estimated remaining frame time, using real DXGI frames rather than generated frames. Overlap can make NR time exceed the frame interval; the bar then reports overlap. It is not an additive workload measurement.

Hold freezes input and guides for early/deferred processing; early-to-finished mode also holds one clean final image across presentation slots. It does not pause the game. See [hold limits](../OptiScaler/dlssnr/design/frame-hold.md), [bridges](NR-FINISHED-BRIDGES.md) and [enlargement](NR-DLSS-ENLARGEMENT.md).

Offscreen ImGui/WARP checks covered all routes, selection, wrapped labels, forced controls and timing overlap. GPU/proxy checks covered held inputs, rotating resources and restoration after failure. [Live results](NR-UPSTREAM-REVIEW.md) describe the narrower game coverage.
