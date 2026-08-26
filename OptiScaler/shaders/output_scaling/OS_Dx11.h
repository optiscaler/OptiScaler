#pragma once
#include "SysUtils.h"

#include <shaders/Shader_Dx11.h>
#include <d3d11.h>

class OS_Dx11 : public Shader_Dx11
{
  private:
    bool _upsample = false;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    bool InitializeViews(ID3D11Texture2D* InResource, ID3D11Texture2D* OutResource);

  public:
    bool CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InSource, uint32_t InWidth, uint32_t InHeight);
    bool Dispatch(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                  ID3D11Texture2D* OutResource);

    bool IsUpsampling() const { return _upsample; }

    OS_Dx11(std::string InName, ID3D11Device* InDevice, bool InUpsample);
};
