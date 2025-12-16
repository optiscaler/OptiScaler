#include "OS_Dx11.h"

#include "OS_Common.h"

#define A_CPU
// FSR compute shader is from : https://github.com/fholger/vrperfkit/

#include "precompile/BCDS_lanczos_Shader_Dx11.h"
#include "precompile/BCDS_catmull_Shader_Dx11.h"
#include "precompile/BCDS_bicubic_Shader_Dx11.h"
#include "precompile/BCDS_magc_Shader_Dx11.h"

#include "precompile/BCUS_Shader_Dx11.h"

#include "fsr1/ffx_fsr1.h"
#include "fsr1/FSR_EASU_Shader_Dx11.h"

#include <Config.h>

#pragma warning(disable : 4244)

inline static DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format)
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
    default:
        return format;
    }
}

bool OS_Dx11::CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InResource, uint32_t InWidth,
                                   uint32_t InHeight)
{
    if (InDevice == nullptr || InResource == nullptr)
        return false;

    ID3D11Texture2D* originalTexture = nullptr;
    auto result = InResource->QueryInterface(IID_PPV_ARGS(&originalTexture));
    if (result != S_OK)
        return false;

    D3D11_TEXTURE2D_DESC texDesc;
    originalTexture->GetDesc(&texDesc);
    auto targetWidth = texDesc.Width > InWidth ? texDesc.Width : InWidth;
    auto targetHeight = texDesc.Height > InHeight ? texDesc.Height : InHeight;

    if (_buffer != nullptr)
    {
        D3D11_TEXTURE2D_DESC bufDesc;
        _buffer->GetDesc(&bufDesc);

        if (bufDesc.Width != targetWidth || bufDesc.Height != targetHeight || bufDesc.Format != texDesc.Format)
        {
            _buffer->Release();
            _buffer = nullptr;
        }
        else
            return true;
    }

    LOG_DEBUG("[{0}] Start!", _name);

    texDesc.Width = targetWidth;
    texDesc.Height = targetHeight;
    texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    result = InDevice->CreateTexture2D(&texDesc, nullptr, &_buffer);
    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource result: {1:x}", _name, result);
        return false;
    }

    return true;
}

bool OS_Dx11::InitializeViews(ID3D11Texture2D* InResource, ID3D11Texture2D* OutResource)
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
        srvDesc.Format = TranslateTypelessFormats(desc.Format);
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;

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
        uavDesc.Format = TranslateTypelessFormats(desc.Format);
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

bool OS_Dx11::Dispatch(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                       ID3D11Texture2D* OutResource)
{
    if (!_init || InDevice == nullptr || InContext == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _device = InDevice;

    if (!InitializeViews(InResource, OutResource))
        return false;

    D3D11_TEXTURE2D_DESC inDesc;
    InResource->GetDesc(&inDesc);

    // fsr upscaling
    if (Config::Instance()->OutputScalingUseFsr.value_or_default())
    {
        UpscaleShaderConstants constants {};

        FsrEasuCon(constants.const0, constants.const1, constants.const2, constants.const3,
                   State::Instance().currentFeature->TargetWidth(), State::Instance().currentFeature->TargetHeight(),
                   inDesc.Width, inDesc.Height, State::Instance().currentFeature->DisplayWidth(),
                   State::Instance().currentFeature->DisplayHeight());

        // Copy the updated constant buffer data to the constant buffer resource
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        auto hr = InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] Map error {1:x}", _name, hr);
            return false;
        }

        memcpy(mappedResource.pData, &constants, sizeof(constants));
        InContext->Unmap(_constantBuffer, 0);
    }
    else
    {
        Constants constants {};
        constants.srcWidth = State::Instance().currentFeature->TargetWidth();
        constants.srcHeight = State::Instance().currentFeature->TargetHeight();
        constants.destWidth = State::Instance().currentFeature->DisplayWidth(); // static_cast<uint32_t>(outDesc.Width);
        constants.destHeight = State::Instance().currentFeature->DisplayHeight(); // outDesc.Height;

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        auto hr = InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] Map error {1:x}", _name, hr);
            return false;
        }

        memcpy(mappedResource.pData, &constants, sizeof(constants));
        InContext->Unmap(_constantBuffer, 0);
    }

    // Set the compute shader and resources
    InContext->CSSetShader(_computeShader, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);
    InContext->CSSetShaderResources(0, 1, &_srvInput);
    InContext->CSSetUnorderedAccessViews(0, 1, &_uavOutput, nullptr);

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    // if (_upsample || Config::Instance()->OutputScalingUseFsr.value_or_default())
    //{
    dispatchWidth =
        static_cast<UINT>((State::Instance().currentFeature->DisplayWidth() + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (State::Instance().currentFeature->DisplayHeight() + InNumThreadsY - 1) / InNumThreadsY;
    //}
    // else
    //{
    //    dispatchWidth = static_cast<UINT>((State::Instance().currentFeature->TargetWidth() + InNumThreadsX - 1) /
    //    InNumThreadsX); dispatchHeight = (State::Instance().currentFeature->TargetHeight() + InNumThreadsY - 1) /
    //    InNumThreadsY;
    //}

    InContext->Dispatch(dispatchWidth, dispatchHeight, 1);

    // Unbind resources
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    InContext->CSSetShaderResources(0, 2, nullSRV);

    return true;
}

OS_Dx11::OS_Dx11(std::string InName, ID3D11Device* InDevice, bool InUpsample)
    : _name(InName), _device(InDevice), _upsample(InUpsample)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (Config::Instance()->UsePrecompiledShaders.value_or_default() ||
        Config::Instance()->OutputScalingUseFsr.value_or_default())
    {
        HRESULT hr;

        // fsr upscaling
        if (Config::Instance()->OutputScalingUseFsr.value_or_default())
        {
            hr = _device->CreateComputeShader(reinterpret_cast<const void*>(fsr_easu_cso), sizeof(fsr_easu_cso),
                                              nullptr, &_computeShader);
        }
        else
        {

            if (_upsample)
            {
                hr = _device->CreateComputeShader(reinterpret_cast<const void*>(BCUS_cso), sizeof(BCUS_cso), nullptr,
                                                  &_computeShader);
            }
            else
            {
                switch (Config::Instance()->OutputScalingDownscaler.value_or_default())
                {
                case 0:
                    hr = _device->CreateComputeShader(reinterpret_cast<const void*>(bcds_bicubic_cso),
                                                      sizeof(bcds_bicubic_cso), nullptr, &_computeShader);
                    break;

                case 1:
                    hr = _device->CreateComputeShader(reinterpret_cast<const void*>(bcds_lanczos_cso),
                                                      sizeof(bcds_lanczos_cso), nullptr, &_computeShader);
                    break;

                case 2:
                    hr = _device->CreateComputeShader(reinterpret_cast<const void*>(bcds_catmull_cso),
                                                      sizeof(bcds_catmull_cso), nullptr, &_computeShader);
                    break;

                case 3:
                    hr = _device->CreateComputeShader(reinterpret_cast<const void*>(bcds_magc_cso),
                                                      sizeof(bcds_magc_cso), nullptr, &_computeShader);
                    break;

                default:
                    hr = _device->CreateComputeShader(reinterpret_cast<const void*>(bcds_bicubic_cso),
                                                      sizeof(bcds_bicubic_cso), nullptr, &_computeShader);
                    break;
                }
            }
        }

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateComputeShader error: {1:X}", _name, hr);
            return;
        }
    }
    else
    {
        ID3DBlob* shaderBlob = nullptr;

        // Compile shader blobs
        if (_upsample)
        {
            shaderBlob = OS_CompileShader(upsampleCode.c_str(), "CSMain", "cs_5_0");
        }
        else
        {
            switch (Config::Instance()->OutputScalingDownscaler.value_or_default())
            {
            case 0:
                shaderBlob = OS_CompileShader(downsampleCodeBC.c_str(), "CSMain", "cs_5_0");
                break;

            case 1:
                shaderBlob = OS_CompileShader(downsampleCodeLanczos.c_str(), "CSMain", "cs_5_0");
                break;

            case 2:
                shaderBlob = OS_CompileShader(downsampleCodeCatmull.c_str(), "CSMain", "cs_5_0");
                break;

            case 3:
                shaderBlob = OS_CompileShader(downsampleCodeMAGIC.c_str(), "CSMain", "cs_5_0");
                break;

            default:
                shaderBlob = OS_CompileShader(downsampleCodeBC.c_str(), "CSMain", "cs_5_0");
                break;
            }
        }

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

    // CBV
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(Constants);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    auto result = InDevice->CreateBuffer(&cbDesc, nullptr, &_constantBuffer);
    if (result != S_OK)
    {
        LOG_ERROR("CreateBuffer error: {0:X}", (UINT) result);
        return;
    }

    // FSR upscaling
    if (Config::Instance()->OutputScalingUseFsr.value_or_default())
    {
        InNumThreadsX = 16;
        InNumThreadsY = 16;
    }
    _init = true;
}

OS_Dx11::~OS_Dx11()
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
