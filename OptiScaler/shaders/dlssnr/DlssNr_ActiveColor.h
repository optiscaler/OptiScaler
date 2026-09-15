#pragma once

#include <d3d12.h>
#include <optional>

namespace DlssNr
{
struct ColorExtent
{
    unsigned int width;
    unsigned int height;
};

// NGX reports the active image separately from the allocation. No preset names or standard
// resolutions belong here. Non-zero origins still need a separate guide/colour-offset integration.
inline std::optional<ColorExtent> PreSrColorExtent(const D3D12_RESOURCE_DESC& allocation,
                                                  unsigned int renderWidth, unsigned int renderHeight,
                                                  unsigned int baseX = 0, unsigned int baseY = 0)
{
    if (allocation.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        allocation.SampleDesc.Count != 1 || allocation.DepthOrArraySize != 1 ||
        allocation.Width == 0 || allocation.Width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        allocation.Height == 0 || allocation.Height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        baseX != 0 || baseY != 0)
        return std::nullopt;

    if (renderWidth == 0 && renderHeight == 0)
        return ColorExtent { (unsigned int) allocation.Width, allocation.Height };

    if (renderWidth == 0 || renderHeight == 0 || renderWidth > allocation.Width ||
        renderHeight > allocation.Height)
        return std::nullopt;

    return ColorExtent { renderWidth, renderHeight };
}

// Both resources must be in COPY_SOURCE/COPY_DEST respectively. An explicit box is essential:
// a whole-resource copy either has mismatched dimensions or overwrites the game's padding.
inline void CopyActiveColor(ID3D12GraphicsCommandList* commands, ID3D12Resource* destination,
                            ID3D12Resource* source, ColorExtent active)
{
    D3D12_TEXTURE_COPY_LOCATION src {};
    src.pResource = source;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst {};
    dst.pResource = destination;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    const D3D12_BOX box { 0, 0, 0, active.width, active.height, 1 };
    commands->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
}
} // namespace DlssNr
