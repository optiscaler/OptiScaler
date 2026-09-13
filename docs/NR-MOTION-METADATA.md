# NR motion metadata

Adapted from [cmh1448's commit 6446cc8](https://github.com/cmh1448/OptiScaler_DLSSNR/commit/6446cc8dcc76e2c2b786384300e217f352b2c18b).

Depth and motion keep independent origins and valid sizes, bounded by their own allocations. Missing dimensions use that resource's available region; empty regions skip NR. `MVLowRes` selects render-resolution vectors, otherwise output-resolution vectors, even for pre-SR NR.

DX12/Vulkan scale X and Y independently. Every model pass receives the metadata; DX11/Vulkan-to-DX12 bridges preserve parameter blocks while substituting shared resources. Model history, composition and FG ownership are unchanged.

`tests/nr_guides_smoke.cpp` covers render/output vectors, padded allocations, independent origins, clipping and invalid regions. WARP also checks active-colour copies. These checks do not establish reduced shimmering or native Vulkan gameplay compatibility.
