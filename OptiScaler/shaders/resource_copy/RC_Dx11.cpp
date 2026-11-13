#include "pch.h"
#include "RC_Dx11.h"

#include "RC_Common.h"
#include "precompile/rc_offset_Shader_Dx11.h"

bool RC_Dx11::CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InSource, uint32_t InWidth,
                                   uint32_t InHeight, bool InGrow)
{
    D3D11_TEXTURE2D_DESC current {};

    if (_buffer != nullptr)
        _buffer->GetDesc(&current);

    return CreateBufferResourceCommon(InDevice, InSource, _buffer,
                                      [&](D3D11_TEXTURE2D_DESC& desc)
                                      {
                                          desc.Format = RC_Formats::BufferFormat(desc.Format, _depth);
                                          desc.Width = InWidth;
                                          desc.Height = InHeight;

                                          if (InGrow && _buffer != nullptr && current.Format == desc.Format)
                                          {
                                              desc.Width = std::max(desc.Width, current.Width);
                                              desc.Height = std::max(desc.Height, current.Height);
                                          }

                                          desc.MipLevels = 1;
                                          desc.ArraySize = 1;
                                          desc.SampleDesc = { 1, 0 };
                                          desc.Usage = D3D11_USAGE_DEFAULT;
                                          desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                                          desc.CPUAccessFlags = 0;
                                          desc.MiscFlags = 0;
                                      });
}

bool RC_Dx11::Dispatch(ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource, ID3D11Texture2D* OutResource,
                       uint32_t InSrcX, uint32_t InSrcY, uint32_t InDstX, uint32_t InDstY, uint32_t InWidth,
                       uint32_t InHeight)
{
    if (!_init || InContext == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    D3D11_TEXTURE2D_DESC inDesc;
    InResource->GetDesc(&inDesc);

    if (!InitializeSRV(InResource, _currentInResource, _srvInput, RC_Formats::SrvFormat(inDesc.Format, _depth)) ||
        !InitializeUAV(OutResource, _currentOutResource, _uavOutput))
    {
        return false;
    }

    InternalConstants constants {};
    constants.SrcOffset[0] = InSrcX;
    constants.SrcOffset[1] = InSrcY;
    constants.DstOffset[0] = InDstX;
    constants.DstOffset[1] = InDstY;
    constants.Size[0] = InWidth;
    constants.Size[1] = InHeight;

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

    InContext->CSSetShader(_computeShader, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);
    InContext->CSSetShaderResources(0, 1, &_srvInput);
    InContext->CSSetUnorderedAccessViews(0, 1, &_uavOutput, nullptr);

    InContext->Dispatch((InWidth + InNumThreadsX - 1) / InNumThreadsX, (InHeight + InNumThreadsY - 1) / InNumThreadsY,
                        1);

    // Unbind resources
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV = nullptr;
    InContext->CSSetShaderResources(0, 1, &nullSRV);

    return true;
}

RC_Dx11::RC_Dx11(std::string InName, ID3D11Device* InDevice, bool InDepth)
    : Shader_Dx11(InName, InDevice), _depth(InDepth)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    _device = InDevice;

    auto result = CreateComputeShader(InDevice, _computeShader, reinterpret_cast<const void*>(rc_offset_cso),
                                      sizeof(rc_offset_cso), nullptr);

    if (FAILED(result))
    {
        LOG_ERROR("[{0}] CreateComputeShader error: {1:X}", _name, result);
        return;
    }

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
