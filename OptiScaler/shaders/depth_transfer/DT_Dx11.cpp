#include "pch.h"
#include "DT_Dx11.h"

#include "DT_Common.h"
#include "../Shader_Common.h"
#include "precompile/dt_dx11_Shader_Dx11.h"

#include <Config.h>

bool DepthTransfer_Dx11::CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InResource)
{
    return CreateBufferResourceCommon(InDevice, InResource, _buffer,
                                      [](D3D11_TEXTURE2D_DESC& desc)
                                      {
                                          desc.Format = DXGI_FORMAT_R32_FLOAT;
                                          desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                                          desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
                                      });
}

bool DepthTransfer_Dx11::InitializeViews(ID3D11Texture2D* InResource, ID3D11Texture2D* OutResource)
{
    auto resultInput = InitializeSRV(InResource, _currentInResource, _srvInput);
    auto resultOutput = InitializeUAV(OutResource, _currentOutResource, _uavOutput);

    return resultInput && resultOutput;
}

bool DepthTransfer_Dx11::Dispatch(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                                  ID3D11Texture2D* OutResource)
{
    if (!_init || InDevice == nullptr || InContext == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    ScopedGpuTime_Dx11 scopedGpuTime(GpuTime.get(), InContext);

    _device = InDevice;

    if (!InitializeViews(InResource, OutResource))
        return false;

    // Set the compute shader and resources
    InContext->CSSetShader(_computeShader, nullptr, 0);
    InContext->CSSetShaderResources(0, 1, &_srvInput);
    InContext->CSSetUnorderedAccessViews(0, 1, &_uavOutput, nullptr);

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    D3D11_TEXTURE2D_DESC inDesc;
    InResource->GetDesc(&inDesc);

    dispatchWidth = (inDesc.Width + InNumThreadsX - 1) / InNumThreadsX;
    dispatchHeight = (inDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    InContext->Dispatch(dispatchWidth, dispatchHeight, 1);

    // Unbind resources
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV = nullptr;
    InContext->CSSetShaderResources(0, 1, &nullSRV);

    return true;
}

DepthTransfer_Dx11::DepthTransfer_Dx11(std::string InName, ID3D11Device* InDevice) : Shader_Dx11(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    auto result = CreateComputeShader(InDevice, _computeShader, reinterpret_cast<const void*>(dt_dx11_cso),
                                      sizeof(dt_dx11_cso), shaderCode.c_str());

    if (FAILED(result))
    {
        LOG_ERROR("[{0}] CreateComputeShader error: {1:X}", _name, result);
        return;
    }

    _init = true;
}
