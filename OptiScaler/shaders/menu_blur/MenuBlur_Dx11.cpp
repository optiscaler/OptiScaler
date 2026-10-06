#include "pch.h"
#include "MenuBlur_Dx11.h"

#include <Config.h>
#include <State.h>
#include "precompile/MenuBlur_Shader_Dx11.h"

using Microsoft::WRL::ComPtr;

DXGI_FORMAT MenuBlur_Dx11::CopyFormat(DXGI_FORMAT InFormat)
{
    // Typeless so the copy can be read without sRGB conversion
    switch (InFormat)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;

    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;

    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_TYPELESS;

    default:
        return InFormat;
    }
}

DXGI_FORMAT MenuBlur_Dx11::CopySrvFormat(DXGI_FORMAT InFormat)
{
    // Read raw values, the menu is rendered with a non-sRGB RTV
    switch (CopyFormat(InFormat))
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;

    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;

    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return DXGI_FORMAT_B8G8R8X8_UNORM;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;

    default:
        return TranslateTypelessFormats(InFormat);
    }
}

void MenuBlur_Dx11::ReleaseTextures()
{
    SAFE_RELEASE(_copySrv);
    SAFE_RELEASE(_copy);
    SAFE_RELEASE(_blurASrv);
    SAFE_RELEASE(_blurBSrv);
    SAFE_RELEASE(_blurAUav);
    SAFE_RELEASE(_blurBUav);
    SAFE_RELEASE(_blurA);
    SAFE_RELEASE(_blurB);
}

bool MenuBlur_Dx11::Prepare(ID3D11Texture2D* InBackBuffer)
{
    if (!_init || InBackBuffer == nullptr)
        return false;

    D3D11_TEXTURE2D_DESC bbDesc {};
    InBackBuffer->GetDesc(&bbDesc);

    if (bbDesc.SampleDesc.Count > 1 || bbDesc.ArraySize > 1)
        return false;

    UINT width = (bbDesc.Width + DownsampleFactor - 1) / DownsampleFactor;
    UINT height = (bbDesc.Height + DownsampleFactor - 1) / DownsampleFactor;

    if (_copy != nullptr)
    {
        D3D11_TEXTURE2D_DESC copyDesc {};
        _copy->GetDesc(&copyDesc);

        if (copyDesc.Width == bbDesc.Width && copyDesc.Height == bbDesc.Height &&
            copyDesc.Format == CopyFormat(bbDesc.Format))
        {
            return true;
        }

        ReleaseTextures();
    }

    do
    {
        D3D11_TEXTURE2D_DESC copyDesc {};
        copyDesc.Width = bbDesc.Width;
        copyDesc.Height = bbDesc.Height;
        copyDesc.MipLevels = 1;
        copyDesc.ArraySize = 1;
        copyDesc.Format = CopyFormat(bbDesc.Format);
        copyDesc.SampleDesc.Count = 1;
        copyDesc.Usage = D3D11_USAGE_DEFAULT;
        copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        auto hr = _device->CreateTexture2D(&copyDesc, nullptr, &_copy);
        if (hr != S_OK)
        {
            LOG_ERROR("[{0}] CreateTexture2D(copy) error {1:x}", _name, (unsigned int) hr);
            break;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC copySrvDesc {};
        copySrvDesc.Format = CopySrvFormat(bbDesc.Format);
        copySrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        copySrvDesc.Texture2D.MipLevels = 1;

        hr = _device->CreateShaderResourceView(_copy, &copySrvDesc, &_copySrv);
        if (hr != S_OK)
        {
            LOG_ERROR("[{0}] CreateShaderResourceView(copy) error {1:x}", _name, (unsigned int) hr);
            break;
        }

        D3D11_TEXTURE2D_DESC blurDesc = copyDesc;
        blurDesc.Width = width;
        blurDesc.Height = height;
        blurDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        blurDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        if (_device->CreateTexture2D(&blurDesc, nullptr, &_blurA) != S_OK ||
            _device->CreateTexture2D(&blurDesc, nullptr, &_blurB) != S_OK ||
            _device->CreateShaderResourceView(_blurA, nullptr, &_blurASrv) != S_OK ||
            _device->CreateShaderResourceView(_blurB, nullptr, &_blurBSrv) != S_OK ||
            _device->CreateUnorderedAccessView(_blurA, nullptr, &_blurAUav) != S_OK ||
            _device->CreateUnorderedAccessView(_blurB, nullptr, &_blurBUav) != S_OK)
        {
            LOG_ERROR("[{0}] Failed to create blur textures", _name);
            break;
        }

        _uvScale = ImVec2((float) bbDesc.Width / (float) (width * DownsampleFactor),
                          (float) bbDesc.Height / (float) (height * DownsampleFactor));

        LOG_DEBUG("[{0}] Created blur textures {1}x{2}", _name, width, height);

        return true;

    } while (false);

    ReleaseTextures();
    return false;
}

void MenuBlur_Dx11::DispatchPass(ID3D11DeviceContext* InContext, ID3D11ShaderResourceView* InSrv,
                                 ID3D11UnorderedAccessView* OutUav, BlurMode InMode, float InSpacing, UINT InWidth,
                                 UINT InHeight)
{
    InternalMenuBlurParams params {};
    params.Mode = (uint32_t) InMode;
    params.Spacing = InSpacing;

    D3D11_MAPPED_SUBRESOURCE mapped {};
    if (InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped) != S_OK)
        return;

    memcpy(mapped.pData, &params, sizeof(params));
    InContext->Unmap(_constantBuffer, 0);

    // Unbind the UAV first, a resource can't be bound as SRV and UAV at the same time
    ID3D11UnorderedAccessView* nullUav = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
    InContext->CSSetShaderResources(0, 1, &InSrv);
    InContext->CSSetUnorderedAccessViews(0, 1, &OutUav, nullptr);
    InContext->Dispatch((InWidth + 7) / 8, (InHeight + 7) / 8, 1);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    InContext->CSSetShaderResources(0, 1, &nullSrv);
}

bool MenuBlur_Dx11::Dispatch(ID3D11DeviceContext* InContext, ID3D11Texture2D* InBackBuffer)
{
    if (!_init || _copy == nullptr || InContext == nullptr || InBackBuffer == nullptr)
        return false;

    InContext->CopyResource(_copy, InBackBuffer);

    // Backup the bindings used by the blur, game state persists between frames
    ComPtr<ID3D11ComputeShader> restoreShader;
    ID3D11ClassInstance* restoreInstances[256] = {};
    UINT restoreInstanceCount = 256;
    ComPtr<ID3D11ShaderResourceView> restoreSrv;
    ComPtr<ID3D11UnorderedAccessView> restoreUav;
    ComPtr<ID3D11Buffer> restoreCb;
    ComPtr<ID3D11SamplerState> restoreSampler;

    InContext->CSGetShader(restoreShader.GetAddressOf(), restoreInstances, &restoreInstanceCount);
    InContext->CSGetShaderResources(0, 1, restoreSrv.GetAddressOf());
    InContext->CSGetUnorderedAccessViews(0, 1, restoreUav.GetAddressOf());
    InContext->CSGetConstantBuffers(0, 1, restoreCb.GetAddressOf());
    InContext->CSGetSamplers(0, 1, restoreSampler.GetAddressOf());

    D3D11_TEXTURE2D_DESC blurDesc {};
    _blurA->GetDesc(&blurDesc);

    // Strength scales the distance between taps, 1.0 is a regular 9 tap gaussian
    float spacing = Config::Instance()->MenuBlurStrength.value_or_default();

    InContext->CSSetShader(_computeShader, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);
    InContext->CSSetSamplers(0, 1, &_sampler);

    DispatchPass(InContext, _copySrv, _blurAUav, BlurMode::Downsample, spacing, blurDesc.Width, blurDesc.Height);

    if (spacing > 0.0f)
    {
        for (uint32_t i = 0; i < BlurIterations; i++)
        {
            DispatchPass(InContext, _blurASrv, _blurBUav, BlurMode::Horizontal, spacing, blurDesc.Width,
                         blurDesc.Height);
            DispatchPass(InContext, _blurBSrv, _blurAUav, BlurMode::Vertical, spacing, blurDesc.Width, blurDesc.Height);
        }
    }

    // Restore
    ID3D11UnorderedAccessView* nullUav = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);

    InContext->CSSetShader(restoreShader.Get(), restoreInstances, restoreInstanceCount);
    InContext->CSSetShaderResources(0, 1, restoreSrv.GetAddressOf());
    InContext->CSSetUnorderedAccessViews(0, 1, restoreUav.GetAddressOf(), nullptr);
    InContext->CSSetConstantBuffers(0, 1, restoreCb.GetAddressOf());
    InContext->CSSetSamplers(0, 1, restoreSampler.GetAddressOf());

    for (UINT i = 0; i < restoreInstanceCount; i++)
        SAFE_RELEASE(restoreInstances[i]);

    return true;
}

MenuBlur_Dx11::MenuBlur_Dx11(std::string InName, ID3D11Device* InDevice) : Shader_Dx11(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    auto result = CreateComputeShader(InDevice, _computeShader, reinterpret_cast<const void*>(MenuBlur_cso),
                                      sizeof(MenuBlur_cso), shaderCode.c_str());

    if (FAILED(result))
    {
        LOG_ERROR("[{0}] CreateComputeShader error: {1:X}", _name, (UINT) result);
        return;
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(InternalMenuBlurParams);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    result = InDevice->CreateBuffer(&cbDesc, nullptr, &_constantBuffer);
    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateBuffer error: {1:X}", _name, (UINT) result);
        SAFE_RELEASE(_computeShader);
        return;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    result = InDevice->CreateSamplerState(&samplerDesc, &_sampler);
    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateSamplerState error: {1:X}", _name, (UINT) result);
        SAFE_RELEASE(_constantBuffer);
        SAFE_RELEASE(_computeShader);
        return;
    }

    _init = true;
}

MenuBlur_Dx11::~MenuBlur_Dx11()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    ReleaseTextures();
    SAFE_RELEASE(_sampler);
}
