# Matched residual + DLSS

Choose this Enlargement mode below 100% model resolution for ordinary post-upscale or finished-picture NR. Keep both early-generation options off. INI: `Transfer=2`; spatial Matched residual remains the default (`1`). At 100%, ordinary full-resolution NR replaces the private context.

NR runs on a downsampled reconstructed picture. Encode its linear-proxy difference from that same input around neutral 0.5, enlarge with independent DLSS **SR**, then add it to the full-resolution original. Existing HDR, strength and skin controls compose the matched pair. Replace modes decode the reconstructed proxy; model debug view shows the actual low-resolution answer.

Encoding/decoding use a 1/64 linear-light scale to preserve small edits in FP16. This removed dense shadow speckles observed in KCD2 without changing model strength.

Depth/motion are resampled from their active regions; motion becomes pixels at model resolution. Jitter is zero because game reconstruction has already occurred. Finished-picture processing uses matching captured guides/metadata; hold zeroes velocity. The private context owns history/parameters, uses unit exposure and disables sharpening/auto-exposure. This route never invokes private RR.

The actual command-list submission identifies the producer queue; a Streamline presentation-queue hint must not invalidate it. Resize, authoritative queue changes and Retry retire the context through its own GPU tracker. Active/retired contexts receive reset/submission notifications; unrelated NR work cannot pin them. Retirement is bounded.

Failure retains the clean frame with a status, without automatic spatial fallback. Native Vulkan and pre-upscale placement are unsupported; D3D12 bridges reuse this implementation. Shader operations 9/10 preserve existing numbers/layout and have matching DX12/Vulkan binaries.

WARP checks cover reconstruction, guide scaling, neutral identity and signed shadow edits through FP16; Vulkan, lifetime/proxy and real DLSS adapter checks also passed. [Live checks](NR-UPSTREAM-REVIEW.md) found a remaining faint post-upscale grid in KCD2 and possibly Hogwarts. Spatial Matched residual avoids that observed artifact. HDR and motion quality remain experimental; private DLSS adds GPU/VRAM cost beyond model timing.
