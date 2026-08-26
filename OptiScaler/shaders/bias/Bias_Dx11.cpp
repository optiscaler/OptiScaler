#include "pch.h"
#include "Bias_Dx11.h"

#include "Bias_Common.h"
#include "../Shader_Common.h"
#include "precompile/Bias_Shader_Dx11.h"

#include <Config.h>

bool Bias_Dx11::CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InResource)
{
    return CreateBufferResourceCommon(InDevice, InResource, _buffer, [](D3D11_TEXTURE2D_DESC& desc)
                                      { desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS; });
}

bool Bias_Dx11::InitializeViews(ID3D11Texture2D* InResource, ID3D11Texture2D* OutResource)
{
    auto resultInput = InitializeSRV(InResource, _currentInResource, _srvInput);
    auto resultOutput = InitializeUAV(OutResource, _currentOutResource, _uavOutput);

    return resultInput && resultOutput;
}

bool Bias_Dx11::Dispatch(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                         float InBias, ID3D11Texture2D* OutResource)
{
    if (!_init || InDevice == nullptr || InContext == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _device = InDevice;

    if (!InitializeViews(InResource, OutResource))
        return false;

    InternalConstants constants {};
    constants.Bias = InBias;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    auto hr = InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        LOG_ERROR("[{0}] Map error {1:x}", _name, hr);
        return false;
    }

    memcpy(mappedResource.pData, &constants, sizeof(constants));
    InContext->Unmap(_constantBuffer, 0);

    // Set the compute shader and resources
    InContext->CSSetShader(_computeShader, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);
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
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    InContext->CSSetShaderResources(0, 2, nullSRV);

    return true;
}

Bias_Dx11::Bias_Dx11(std::string InName, ID3D11Device* InDevice) : Shader_Dx11(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    auto result = CreateComputeShader(InDevice, _computeShader, reinterpret_cast<const void*>(bias_cso),
                                      sizeof(bias_cso), biasShader.c_str());

    if (FAILED(result))
    {
        LOG_ERROR("[{0}] CreateComputeShader error: {1:X}", _name, result);
        return;
    }

    // CBV
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(InternalConstants);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    result = InDevice->CreateBuffer(&cbDesc, nullptr, &_constantBuffer);
    if (result != S_OK)
    {
        LOG_ERROR("CreateBuffer error: {0:X}", (UINT) result);
        return;
    }

    _init = true;
}
