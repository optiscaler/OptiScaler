# Compare gameplay and live photo mode

This captures ordinary native DX12 pre-upscale NR without changing NR/RR settings or histories.

1. Enable NR and **Generate model before upscale**. Disable separate early-generation/late-application, finished-picture, hold and comparison/debug views. Keep graphics/model/exposure settings fixed.
2. Where gameplay loses the effect, press **Ctrl+F8** and wait at least five seconds.
3. Enter live photo mode with similar framing. When the effect appears, press **Ctrl+F9** and wait five seconds.
4. Exit normally; inspect `nr-pipeline-captures` beside the executable (`bin/x64` in Cyberpunk) and `OptiScaler.log`.

Shortcuts require game focus and this route. Each records four eligible evaluations; labels are manual, not photo-mode detection. Limits are two requests per process, 256 MiB/frame and 2 GiB total. Existing captures are preserved. Capture can hitch, so do not benchmark it.

Each frame contains raw textures and `manifest.txt`:

| Texture | Meaning |
| --- | --- |
| `before_nr` | Original scene-linear input |
| `after_nr` | Exact colour supplied to the main SR/RR pass |
| `after_rr` | Main output before subsequent shaders; consult manifest `rr` to distinguish SR/RR |
| `motion`, `depth`, `exposure` | Available source guides |

Decode using `storedFormat`, dimensions and `rowPitch`. Copies include allocation padding; metadata identifies the active subrect, resets, evaluation/input replacement, jitter, motion scale, exposure, frame time and RR guides. Unsupported resources, budget skips and failures are marked.

Production lifetime tracking requires closed recordings and completed submissions before mapping; an end timestamp distinguishes executed from discarded work. `tests/run_nr_pipeline_capture.ps1` checks stage pixels, blocked queues, retirement and discarded recordings on WARP. In-game captures are needed to diagnose the visual difference.
