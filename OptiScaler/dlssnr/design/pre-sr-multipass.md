# Placement and multipass

`RunBeforeSR` edits owned colour before SR or RR+SR; the default is post-upscale. Invalid pre-colour regions fall back afterward where possible. Colour/output require origin-zero rectangles; [depth/motion regions](../../../docs/NR-MOTION-METADATA.md) are independent.

Each pass owns a persistent model and temporal history. Later passes inherit the base profile except local tone, which defaults to zero. `Passes` selects the count; `UnlockPasses` permits advanced counts up to the module limit. A failed extra pass leaves the ready contiguous prefix active.

Encode once, preserve the original input and alternate model outputs A/B. Compose the final answer against the original once, avoiding compounded colour transforms.

Profile, placement, format and size changes rebuild affected resources. D3D12 markers protect creation and retirement; submission epochs only pair calls. Vulkan uses creation events and drains before replacement.

`DeferredDLSS` instead upscales a separate edit for post-upscale or finished-picture application. Source RR accumulates that edit using motion vectors before private enlargement. DLSS can use private RR with compatible native guides; other cases use SR. See [private upscaling](../../../docs/DEFERRED-NR-DLSS.md).
