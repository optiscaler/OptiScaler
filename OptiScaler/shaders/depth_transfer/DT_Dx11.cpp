#include "pch.h"
#include "DT_Dx11.h"

#include "DT_Common.h"
#include "precompile/dt_dx11_Shader_Dx11.h"

#include <Config.h>

bool DepthTransfer_Dx11::CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InResource)
{
    if (InDevice == nullptr || InResource == nullptr)
        return false;

    ID3D11Texture2D* originalTexture = nullptr;
    auto result = InResource->QueryInterface(IID_PPV_ARGS(&originalTexture));
    if (result != S_OK)
        return false;

    D3D11_TEXTURE2D_DESC texDesc;
    originalTexture->GetDesc(&texDesc);

    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    result = InDevice->CreateTexture2D(&texDesc, nullptr, &_buffer);
    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource result: {1:x}", _name, result);
        return false;
    }

    return true;
}

bool DepthTransfer_Dx11::InitializeViews(ID3D11Texture2D* InResource, ID3D11Texture2D* OutResource)
{
    if (!_init || InResource == nullptr || OutResource == nullptr)
        return false;

    D3D11_TEXTURE2D_DESC desc;

    if (InResource != _currentInResource || _srvInput == nullptr)
    {
        if (_srvInput != nullptr)
            _srvInput->Release();

        InResource->GetDesc(&desc);

        // Create SRV for input texture
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

        if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS)
            srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        else if (desc.Format == DXGI_FORMAT_R32G8X24_TYPELESS)
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        else
            srvDesc.Format = desc.Format;

        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        auto hr = _device->CreateShaderResourceView(InResource, &srvDesc, &_srvInput);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] _srvInput CreateShaderResourceView error {1:x}", _name, hr);
            return false;
        }

        _currentInResource = InResource;
    }

    if (OutResource != _currentOutResource || _uavOutput == nullptr)
    {
        if (_uavOutput != nullptr)
            _uavOutput->Release();

        OutResource->GetDesc(&desc);

        // Create UAV for output texture
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

        auto hr = _device->CreateUnorderedAccessView(OutResource, &uavDesc, &_uavOutput);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateUnorderedAccessView error {1:x}", _name, hr);
            return false;
        }

        _currentOutResource = OutResource;
    }

    return true;
}

bool DepthTransfer_Dx11::Dispatch(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                                  ID3D11Texture2D* OutResource)
{
    if (!_init || InDevice == nullptr || InContext == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

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

DepthTransfer_Dx11::DepthTransfer_Dx11(std::string InName, ID3D11Device* InDevice) : _name(InName), _device(InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    {
        HRESULT hr;
        hr = _device->CreateComputeShader(reinterpret_cast<const void*>(dt_dx11_cso), sizeof(dt_dx11_cso), nullptr,
                                          &_computeShader);

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateComputeShader error: {1:X}", _name, hr);
            return;
        }
    }
    else
    {
        // Compile shader blobs
        ID3DBlob* shaderBlob = DT_CompileShader(shaderCode.c_str(), "CSMain", "cs_5_0");
        if (shaderBlob == nullptr)
        {
            LOG_ERROR("[{0}] CompileShader error!", _name);
            return;
        }

        // create pso objects
        auto hr = _device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr,
                                               &_computeShader);

        if (shaderBlob != nullptr)
        {
            shaderBlob->Release();
            shaderBlob = nullptr;
        }

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateComputeShader error: {1:X}", _name, hr);
            return;
        }
    }

    _init = true;
}

DepthTransfer_Dx11::~DepthTransfer_Dx11()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_computeShader != nullptr)
        _computeShader->Release();

    if (_constantBuffer != nullptr)
        _constantBuffer->Release();

    if (_srvInput != nullptr)
        _srvInput->Release();

    if (_uavOutput != nullptr)
        _uavOutput->Release();

    if (_buffer != nullptr)
        _buffer->Release();
}
