# Compatibility changes

Installed integration builds include these fixes alongside NR. [PR #1157](https://github.com/optiscaler/OptiScaler/pull/1157) separates active Streamline binding, depth-plane SRVs, Vulkan framebuffer cleanup and the base revision's syntax correction. Other fixes remain in the NR proposal because they share integration points.

| Change | Purpose |
| --- | --- |
| Active Streamline binding | Resolve functions through the initialized interposer; a driver-selected plugin can differ from the bundled DLL. Calling the inactive Reflex export caused startup access violations. |
| Depth-plane SRV format | Preserve a valid depth view; the invalid format caused Jedi: Survivor device removal. |
| D3D11/bridge state | Retain saved COM bindings; translate optional exposure/reactive inputs to the destination API or null, then restore them. |
| DXGI sizing/composition | Resolve window dimensions, preserve caller descriptors/flags and call the factory once. Window association remains best effort for multi-window apps. |
| Frame identity | Advance on successful real Present, excluding TEST; preserve deferred pairs across mid-evaluation Present. GPU completion uses separate markers. |
| Vulkan | Identify the actual physical device under Proton, guard framebuffer cleanup and preserve menu/FG interlocks. |
| KCD2 | Retain process-local Streamline queries and waitable-swapchain ownership fixes; no driver-profile write or hardware unlock. |
| Output scaling | Preserve ordinary target/display dimensions; NR uses private `DispatchResources` and its own filter. |
| NR state restoration | Cover creation, evaluation and failures; see [Onimusha/BG3 details](NR-COMPATIBILITY.md). |

Provenance: [janblade's sizing PR #2](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/2), frame-identity [PR #8](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/8) and [PR #11](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/11), and [y4my4my4m's Vulkan work](NR-VULKAN.md) (`7b7220bb`, including `7c3b65dc`). See [credits](CREDITS.md).

`tests/streamline_active_plugin_smoke.cpp` reproduces the old Reflex crash and checks active exports. `tests/dxgi_window_size_smoke.cpp` exercises real WARP window/composition swapchains. These checks do not certify game presentation; [observed results](NR-UPSTREAM-REVIEW.md) remain bounded.
