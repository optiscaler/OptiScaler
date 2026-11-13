#pragma once
#include "SysUtils.h"

#include <shaders/Shader_Dx11.h>
#include <d3d11.h>

// Copies a rectangle between textures through an SRV and a UAV, used to crop and place DLSS subrects
class RC_Dx11 : public Shader_Dx11
{
  private:
    struct alignas(256) InternalConstants
    {
        uint32_t SrcOffset[2];
        uint32_t DstOffset[2];
        uint32_t Size[2];
    };

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;
    bool _depth = false;

  public:
    // Grows only by default, so dynamic resolution doesn't recreate it every frame. Passes that size their work by
    // the buffer need it exact.
    bool CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InSource, uint32_t InWidth, uint32_t InHeight,
                              bool InGrow = true);
    bool Dispatch(ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource, ID3D11Texture2D* OutResource,
                  uint32_t InSrcX, uint32_t InSrcY, uint32_t InDstX, uint32_t InDstY, uint32_t InWidth,
                  uint32_t InHeight);

    // InDepth: the source is a depth buffer, read as depth and copied to R32_FLOAT
    RC_Dx11(std::string InName, ID3D11Device* InDevice, bool InDepth = false);
};
