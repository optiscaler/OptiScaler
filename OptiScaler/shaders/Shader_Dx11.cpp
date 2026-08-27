#include "pch.h"
#include "Shader_Dx11.h"

using Microsoft::WRL::ComPtr;

Shader_Dx11::Shader_Dx11(std::string InName, ID3D11Device* InDevice) : _name(InName), _device(InDevice)
{
    GpuTime = std::make_unique<GpuTime_Dx11>(InDevice);
}

Shader_Dx11::~Shader_Dx11()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    SAFE_RELEASE(_computeShader);
    SAFE_RELEASE(_constantBuffer);
    SAFE_RELEASE(_buffer);
    SAFE_RELEASE(_srvInput);
    SAFE_RELEASE(_uavOutput);

    SAFE_RELEASE(_currentInResource);
    SAFE_RELEASE(_currentOutResource);
}

DXGI_FORMAT Shader_Dx11::TranslateTypelessFormats(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32G32B32_TYPELESS:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UINT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16_TYPELESS:
        return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS:
        return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R24G8_TYPELESS:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    default:
        return format;
    }
}

bool Shader_Dx11::CreateBufferResourceCommon(ID3D11Device* InDevice, ID3D11Resource* InResource,
                                             ID3D11Texture2D*& OutBuffer,
                                             std::function<void(D3D11_TEXTURE2D_DESC&)> modifyDescCallback)
{
    if (InDevice == nullptr || InResource == nullptr)
        return false;

    ID3D11Texture2D* originalTexture = nullptr;
    auto result = InResource->QueryInterface(IID_PPV_ARGS(&originalTexture));
    if (result != S_OK)
        return false;

    D3D11_TEXTURE2D_DESC texDesc;
    originalTexture->GetDesc(&texDesc);
    originalTexture->Release();

    if (modifyDescCallback)
    {
        modifyDescCallback(texDesc);
    }

    if (OutBuffer != nullptr)
    {
        D3D11_TEXTURE2D_DESC bufDesc;
        OutBuffer->GetDesc(&bufDesc);

        if (bufDesc.Width != texDesc.Width || bufDesc.Height != texDesc.Height || bufDesc.Format != texDesc.Format)
        {
            OutBuffer->Release();
            OutBuffer = nullptr;
        }
        else
        {
            return true;
        }
    }

    LOG_DEBUG("[{0}] Start!", _name);

    result = InDevice->CreateTexture2D(&texDesc, nullptr, &OutBuffer);
    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource result: {1:x}", _name, result);
        return false;
    }

    return true;
}

HRESULT Shader_Dx11::CreateComputeShader(ID3D11Device* device, ID3D11ComputeShader*& computeShader,
                                         const void* bytecode, size_t bytecodeSize, const char* shaderCode)
{
    ComPtr<ID3DBlob> shaderBlob;

    // Compile if not using precompiled
    if (!Config::Instance()->UsePrecompiledShaders.value_or_default() && shaderCode)
        shaderBlob = CompileShader(shaderCode, "CSMain", "cs_5_0");

    auto shaderBlob_p = shaderBlob.Get();

    if (shaderBlob_p)
    {
        bytecode = shaderBlob_p->GetBufferPointer();
        bytecodeSize = shaderBlob_p->GetBufferSize();
    }

    return device->CreateComputeShader(bytecode, bytecodeSize, nullptr, &computeShader);
}

bool Shader_Dx11::InitializeSRV(ID3D11Texture2D* resource, ID3D11Texture2D*& currentResource,
                                ID3D11ShaderResourceView*& targetSrv)
{
    if (!_init || !_device || !resource)
        return false;

    if (resource != currentResource || targetSrv == nullptr)
    {
        if (targetSrv != nullptr)
            targetSrv->Release();

        D3D11_TEXTURE2D_DESC desc;
        resource->GetDesc(&desc);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = TranslateTypelessFormats(desc.Format);
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;

        auto hr = _device->CreateShaderResourceView(resource, &srvDesc, &targetSrv);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateShaderResourceView error {1:x}", _name, hr);
            return false;
        }

        currentResource = resource;
    }

    return true;
}

bool Shader_Dx11::InitializeUAV(ID3D11Texture2D* resource, ID3D11Texture2D*& currentResource,
                                ID3D11UnorderedAccessView*& targetUav)
{
    if (!_init || !_device || !resource)
        return false;

    if (resource != currentResource || targetUav == nullptr)
    {
        if (targetUav != nullptr)
            targetUav->Release();

        D3D11_TEXTURE2D_DESC desc;
        resource->GetDesc(&desc);

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = TranslateTypelessFormats(desc.Format);
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

        auto hr = _device->CreateUnorderedAccessView(resource, &uavDesc, &targetUav);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateUnorderedAccessView error {1:x}", _name, hr);
            return false;
        }

        currentResource = resource;
    }

    return true;
}
