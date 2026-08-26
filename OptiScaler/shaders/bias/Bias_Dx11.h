#pragma once
#include "SysUtils.h"

#include <shaders/Shader_Dx11.h>
#include <d3d11.h>

class Bias_Dx11 : public Shader_Dx11
{
  private:
    struct alignas(256) InternalConstants
    {
        float Bias;
    };

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    bool InitializeViews(ID3D11Texture2D* InResource, ID3D11Texture2D* OutResource);

  public:
    bool CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InSource);
    bool Dispatch(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource, float InBias,
                  ID3D11Texture2D* OutResource);

    Bias_Dx11(std::string InName, ID3D11Device* InDevice);
};
