#pragma once

#include "SysUtils.h"

#include <shaders/Shader_Dx11.h>
#include "RCAS_Common.h"

#include <d3d11.h>

class RCAS_Dx11 : public RCAS_Common, public Shader_Dx11
{
  private:
    ID3D11ComputeShader* _computeShaderDA = nullptr;
    ID3D11ComputeShader* _computeShaderDASDA = nullptr;

    // BaBa Neural and OptiSharpen are created when they are first used, OptiSharpen one per precision
    // (only fp32 on Dx11)
    ID3D11ComputeShader* _computeShaderBaba = nullptr;
    bool _babaShaderFailed = false;
    ID3D11ComputeShader* _computeShaderOpti[(size_t) OptiPrecision::Count] {};
    bool _optiShaderFailed[(size_t) OptiPrecision::Count] {};
    ID3D11ShaderResourceView* _srvMotionVectors = nullptr;
    ID3D11ShaderResourceView* _srvDepth = nullptr;

    ID3D11Texture2D* _currentMotionVectors = nullptr;
    ID3D11Texture2D* _currentDepth = nullptr;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    bool InitializeViews(ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors, ID3D11Texture2D* OutResource);
    bool InitializeViewsDA(ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors, ID3D11Texture2D* InDepth,
                           ID3D11Texture2D* OutResource);
    bool DispatchRCAS(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                      ID3D11Texture2D* InMotionVectors, RcasConstants InConstants, ID3D11Texture2D* OutResource);
    bool DispatchDepthAdaptive(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                               ID3D11Texture2D* InMotionVectors, ID3D11Texture2D* InDepth, RcasConstants InConstants,
                               ID3D11Texture2D* OutResource);
    bool DispatchDASDepthAdaptive(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                                  ID3D11Texture2D* InMotionVectors, ID3D11Texture2D* InDepth, RcasConstants InConstants,
                                  ID3D11Texture2D* OutResource);
    // BaBa Neural or OptiSharpen, same bindings and constants. InPrecision only matters for OptiSharpen.
    bool DispatchNeural(SharpenShader InShader, OptiPrecision InPrecision, ID3D11DeviceContext* InContext,
                        ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors, RcasConstants InConstants,
                        ID3D11Texture2D* OutResource);
    bool EnsureNeuralShader(SharpenShader InShader, OptiPrecision InPrecision, ID3D11Device* InDevice);

  public:
    bool CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InSource);
    bool Dispatch(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                  ID3D11Texture2D* InMotionVectors, RcasConstants InConstants, ID3D11Texture2D* OutResource,
                  ID3D11Texture2D* InDepth = nullptr);

    RCAS_Dx11(std::string InName, ID3D11Device* InDevice);

    ~RCAS_Dx11();
};
