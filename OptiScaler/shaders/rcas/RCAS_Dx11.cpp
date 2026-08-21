#include "pch.h"

#include "RCAS_Dx11.h"

#include "precompile/RCAS_Shader_Dx11.h"
#include "precompile/da_das_sharpen_Shader_Dx11.h"
#include "precompile/da_rcas_sharpen_Shader_Dx11.h"

#include <Config.h>

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

void RCAS_Dx11::FillMotionConstants(InternalConstants& OutConstants, const RcasConstants& InConstants)
{
    if (Config::Instance()->ContrastEnabled.value_or_default())
        OutConstants.Contrast = Config::Instance()->Contrast.value_or_default();
    else
        OutConstants.Contrast = 0.0f;

    auto feature = State::Instance().currentFeature;

    OutConstants.Sharpness = InConstants.Sharpness;
    OutConstants.MvScaleX = InConstants.MvScaleX;
    OutConstants.MvScaleY = InConstants.MvScaleY;
    OutConstants.DisplaySizeMV = feature->LowResMV() ? 0 : 1;

    OutConstants.DisplayWidth = feature->TargetWidth();
    OutConstants.DisplayHeight = feature->TargetHeight();
    OutConstants.DynamicSharpenEnabled = Config::Instance()->MotionSharpnessEnabled.value_or_default() ? 1 : 0;
    OutConstants.MotionSharpness = Config::Instance()->MotionSharpness.value_or_default();
    OutConstants.Debug = Config::Instance()->MotionSharpnessDebug.value_or_default() ? 1 : 0;
    OutConstants.Threshold = Config::Instance()->MotionThreshold.value_or_default();
    OutConstants.ScaleLimit = Config::Instance()->MotionScaleLimit.value_or_default();

    if (feature->LowResMV())
    {
        OutConstants.MotionTextureScale = (float) feature->RenderWidth() / (float) feature->TargetWidth();
    }
    else
    {
        OutConstants.MotionTextureScale = 1.0f;
    }
}

void RCAS_Dx11::FillMotionConstants(InternalConstantsDA& OutConstants, const RcasConstants& InConstants)
{
    auto feature = State::Instance().currentFeature;

    OutConstants.Sharpness = InConstants.Sharpness * 2.0f;
    OutConstants.MvScaleX = InConstants.MvScaleX;
    OutConstants.MvScaleY = InConstants.MvScaleY;
    OutConstants.DisplaySizeMV = feature->LowResMV() ? 0 : 1;

    OutConstants.DynamicSharpenEnabled = Config::Instance()->MotionSharpnessEnabled.value_or_default() ? 1 : 0;
    OutConstants.Debug = Config::Instance()->MotionSharpnessDebug.value_or_default() ? 3 : 0;
    OutConstants.MotionSharpness = Config::Instance()->MotionSharpness.value_or_default();
    OutConstants.MotionThreshold = Config::Instance()->MotionThreshold.value_or_default();
    OutConstants.MotionScaleLimit = Config::Instance()->MotionScaleLimit.value_or_default();
    OutConstants.DisplayWidth = feature->TargetWidth();
    OutConstants.DisplayHeight = feature->TargetHeight();

    OutConstants.DepthIsLinear = Config::Instance()->DADepthIsLinear.value_or_default() ? 1 : 0;
    OutConstants.DepthIsReversed = feature->DepthInverted() ? 1 : 0;
    OutConstants.DepthScale =
        Config::Instance()->DADepthScale.value_or(OutConstants.DepthIsLinear == 0 ? 35.0f : 250.0f);
    OutConstants.DepthBias =
        Config::Instance()->DADepthBias.value_or(OutConstants.DepthIsLinear == 0 ? 0.001f : 0.0015f);

    OutConstants.DepthLinearA = InConstants.CameraNear * InConstants.CameraFar;
    OutConstants.DepthLinearB = InConstants.CameraFar;
    OutConstants.DepthLinearC = InConstants.CameraFar - InConstants.CameraNear;

    OutConstants.DepthTextureScale = (float) feature->RenderWidth() / (float) feature->TargetWidth();

    OutConstants.ClampOutput = Config::Instance()->DAClampOutput.value_or(feature->IsHdr()) ? 0 : 1;

    if (feature->LowResMV())
    {
        OutConstants.MotionWidth = feature->RenderWidth();
        OutConstants.MotionHeight = feature->RenderHeight();
    }
    else
    {
        OutConstants.MotionWidth = feature->TargetWidth();
        OutConstants.MotionHeight = feature->TargetHeight();
    }

    OutConstants.DepthWidth = feature->RenderWidth();
    OutConstants.DepthHeight = feature->RenderHeight();

    OutConstants.MotionTextureScale = (float) OutConstants.MotionWidth / (float) feature->TargetWidth();
}

bool RCAS_Dx11::CreateBufferResource(ID3D11Device* InDevice, ID3D11Resource* InResource)
{
    if (InDevice == nullptr || InResource == nullptr)
        return false;

    ID3D11Texture2D* originalTexture = nullptr;
    auto result = InResource->QueryInterface(IID_PPV_ARGS(&originalTexture));
    if (result != S_OK)
        return false;

    D3D11_TEXTURE2D_DESC texDesc;
    originalTexture->Release();
    originalTexture->GetDesc(&texDesc);

    if (_buffer != nullptr)
    {
        D3D11_TEXTURE2D_DESC bufDesc;
        _buffer->GetDesc(&bufDesc);

        if (bufDesc.Width != (UINT64) (texDesc.Width) || bufDesc.Height != (UINT) (texDesc.Height) ||
            bufDesc.Format != texDesc.Format)
        {
            _buffer->Release();
            _buffer = nullptr;
        }
        else
            return true;
    }

    LOG_DEBUG("[{0}] Start!", _name);

    texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    result = InDevice->CreateTexture2D(&texDesc, nullptr, &_buffer);
    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource result: {1:x}", _name, result);
        return false;
    }

    return true;
}

bool RCAS_Dx11::InitializeViews(ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors,
                                ID3D11Texture2D* OutResource)
{
    if (!_init || InResource == nullptr || InMotionVectors == nullptr || OutResource == nullptr)
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

    if (InMotionVectors != _currentMotionVectors || _srvMotionVectors == nullptr)
    {
        if (_srvMotionVectors != nullptr)
            _srvMotionVectors->Release();

        InMotionVectors->GetDesc(&desc);

        // Create SRV for motion vectors
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = TranslateTypelessFormats(desc.Format);
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;

        auto hr = _device->CreateShaderResourceView(InMotionVectors, &srvDesc, &_srvMotionVectors);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] _srvMotionVectors CreateShaderResourceView error {1:x}", _name, hr);
            return false;
        }

        _currentMotionVectors = InMotionVectors;
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

bool RCAS_Dx11::InitializeViewsDA(ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors,
                                  ID3D11Texture2D* InDepth, ID3D11Texture2D* OutResource)
{
    if (!_init || InResource == nullptr || InMotionVectors == nullptr || InDepth == nullptr || OutResource == nullptr)
        return false;

    if (!InitializeViews(InResource, InMotionVectors, OutResource))
        return false;

    D3D11_TEXTURE2D_DESC desc;

    if (InDepth != _currentDepth || _srvDepth == nullptr)
    {
        if (_srvDepth != nullptr)
            _srvDepth->Release();

        InDepth->GetDesc(&desc);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = TranslateTypelessFormats(desc.Format);
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;

        auto hr = _device->CreateShaderResourceView(InDepth, &srvDesc, &_srvDepth);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] _srvDepth CreateShaderResourceView error {1:x}", _name, hr);
            return false;
        }

        _currentDepth = InDepth;
    }

    return true;
}

bool RCAS_Dx11::DispatchRCAS(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                             ID3D11Texture2D* InMotionVectors, RcasConstants InConstants, ID3D11Texture2D* OutResource)
{
    (void) InDevice;

    if (!InitializeViews(InResource, InMotionVectors, OutResource))
        return false;

    InternalConstants constants {};
    FillMotionConstants(constants, InConstants);

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    auto hr = InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] Map error {1:x}", _name, hr);

        if (hr == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    memcpy(mappedResource.pData, &constants, sizeof(constants));
    InContext->Unmap(_constantBuffer, 0);

    InContext->CSSetShader(_computeShader, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);
    InContext->CSSetShaderResources(0, 1, &_srvInput);
    InContext->CSSetShaderResources(1, 1, &_srvMotionVectors);
    InContext->CSSetUnorderedAccessViews(0, 1, &_uavOutput, nullptr);

    auto feature = State::Instance().currentFeature;
    UINT dispatchWidth = (feature->TargetWidth() + InNumThreadsX - 1) / InNumThreadsX;
    UINT dispatchHeight = (feature->TargetHeight() + InNumThreadsY - 1) / InNumThreadsY;

    InContext->Dispatch(dispatchWidth, dispatchHeight, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    InContext->CSSetShaderResources(0, 2, nullSRV);

    return true;
}

bool RCAS_Dx11::DispatchDepthAdaptive(ID3D11Device* InDevice, ID3D11DeviceContext* InContext,
                                      ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors,
                                      ID3D11Texture2D* InDepth, RcasConstants InConstants, ID3D11Texture2D* OutResource)
{
    (void) InDevice;

    if (InDepth == nullptr || _computeShaderDA == nullptr)
        return false;

    if (!InitializeViewsDA(InResource, InMotionVectors, InDepth, OutResource))
        return false;

    InternalConstantsDA constants {};
    FillMotionConstants(constants, InConstants);

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    auto hr = InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] Map error {1:x}", _name, hr);

        if (hr == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    memcpy(mappedResource.pData, &constants, sizeof(constants));
    InContext->Unmap(_constantBuffer, 0);

    InContext->CSSetShader(_computeShaderDA, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);

    ID3D11ShaderResourceView* srvs[3] = { _srvInput, _srvMotionVectors, _srvDepth };
    InContext->CSSetShaderResources(0, 3, srvs);
    InContext->CSSetUnorderedAccessViews(0, 1, &_uavOutput, nullptr);

    UINT dispatchWidth = (constants.DisplayWidth + InNumThreadsX - 1) / InNumThreadsX;
    UINT dispatchHeight = (constants.DisplayHeight + InNumThreadsY - 1) / InNumThreadsY;

    InContext->Dispatch(dispatchWidth, dispatchHeight, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[3] = { nullptr, nullptr, nullptr };
    InContext->CSSetShaderResources(0, 3, nullSRV);

    return true;
}

bool RCAS_Dx11::DispatchDASDepthAdaptive(ID3D11Device* InDevice, ID3D11DeviceContext* InContext,
                                         ID3D11Texture2D* InResource, ID3D11Texture2D* InMotionVectors,
                                         ID3D11Texture2D* InDepth, RcasConstants InConstants,
                                         ID3D11Texture2D* OutResource)
{
    (void) InDevice;

    if (InDepth == nullptr || _computeShaderDASDA == nullptr)
        return false;

    if (!InitializeViewsDA(InResource, InMotionVectors, InDepth, OutResource))
        return false;

    InternalConstantsDA constants {};
    FillMotionConstants(constants, InConstants);

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    auto hr = InContext->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] Map error {1:x}", _name, hr);

        if (hr == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    memcpy(mappedResource.pData, &constants, sizeof(constants));
    InContext->Unmap(_constantBuffer, 0);

    InContext->CSSetShader(_computeShaderDASDA, nullptr, 0);
    InContext->CSSetConstantBuffers(0, 1, &_constantBuffer);

    ID3D11ShaderResourceView* srvs[3] = { _srvInput, _srvMotionVectors, _srvDepth };
    InContext->CSSetShaderResources(0, 3, srvs);
    InContext->CSSetUnorderedAccessViews(0, 1, &_uavOutput, nullptr);

    UINT dispatchWidth = (constants.DisplayWidth + InNumThreadsX - 1) / InNumThreadsX;
    UINT dispatchHeight = (constants.DisplayHeight + InNumThreadsY - 1) / InNumThreadsY;

    InContext->Dispatch(dispatchWidth, dispatchHeight, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    InContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[3] = { nullptr, nullptr, nullptr };
    InContext->CSSetShaderResources(0, 3, nullSRV);

    return true;
}

bool RCAS_Dx11::Dispatch(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, ID3D11Texture2D* InResource,
                         ID3D11Texture2D* InMotionVectors, RcasConstants InConstants, ID3D11Texture2D* OutResource,
                         ID3D11Texture2D* InDepth)
{
    if (!_init || InDevice == nullptr || InContext == nullptr || InResource == nullptr || OutResource == nullptr ||
        InMotionVectors == nullptr)
    {
        return false;
    }

    LOG_DEBUG("[{0}] Start!", _name);

    _device = InDevice;

    const bool useDepthAdaptive = Config::Instance()->UseDepthAwareSharpen.value_or_default() && InDepth != nullptr;
    const bool useDASDepthAdaptive =
        Config::Instance()->UseDASDepthAwareSharpen.value_or_default() && InDepth != nullptr;

    if (useDepthAdaptive)
        return DispatchDepthAdaptive(InDevice, InContext, InResource, InMotionVectors, InDepth, InConstants,
                                     OutResource);

    if (useDASDepthAdaptive)
        return DispatchDASDepthAdaptive(InDevice, InContext, InResource, InMotionVectors, InDepth, InConstants,
                                        OutResource);

    return DispatchRCAS(InDevice, InContext, InResource, InMotionVectors, InConstants, OutResource);
}

RCAS_Dx11::RCAS_Dx11(std::string InName, ID3D11Device* InDevice) : _name(InName), _device(InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    {
        auto hr = _device->CreateComputeShader(reinterpret_cast<const void*>(rcas_cso), sizeof(rcas_cso), nullptr,
                                               &_computeShader);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateComputeShader error: {1:X}", _name, hr);
            return;
        }

        hr = _device->CreateComputeShader(reinterpret_cast<const void*>(da_rcas_sharpen_cso),
                                          sizeof(da_rcas_sharpen_cso), nullptr, &_computeShaderDA);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateComputeShader error for depth aware shader: {1:X}", _name, hr);
            return;
        }

        hr = _device->CreateComputeShader(reinterpret_cast<const void*>(da_das_sharpen_cso), sizeof(da_das_sharpen_cso),
                                          nullptr, &_computeShaderDASDA);
        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateComputeShader error for DAS depth aware shader: {1:X}", _name, hr);
            return;
        }
    }
    else
    {
        // Compile shader blobs
        ID3DBlob* shaderBlob = RCAS_CompileShader(rcasCode.c_str(), "CSMain", "cs_5_0");

        HRESULT hr = E_FAIL;

        if (shaderBlob != nullptr)
        {
            // create pso objects
            hr = _device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr,
                                              &_computeShader);
        }
        else
        {
            LOG_ERROR("[{0}] RCAS_CompileShader error!", _name);
            hr = _device->CreateComputeShader(reinterpret_cast<const void*>(rcas_cso), sizeof(rcas_cso), nullptr,
                                              &_computeShader);
        }

        if (shaderBlob != nullptr)
        {
            shaderBlob->Release();
            shaderBlob = nullptr;
        }

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateComputeShader error for rcas shader: {1:X}", _name, hr);
            return;
        }

        shaderBlob = RCAS_CompileShader(daRcasSharpenCode.c_str(), "CSMain", "cs_5_0");

        if (shaderBlob != nullptr)
        {
            hr = _device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr,
                                              &_computeShaderDA);
        }
        else
        {
            LOG_ERROR("[{0}] RCAS_CompileShader error for depth aware RCAS shader!", _name);
            hr = _device->CreateComputeShader(reinterpret_cast<const void*>(da_rcas_sharpen_cso),
                                              sizeof(da_rcas_sharpen_cso), nullptr, &_computeShaderDA);
        }

        shaderBlob = RCAS_CompileShader(dasDASharpenCode.c_str(), "CSMain", "cs_5_0");

        if (shaderBlob != nullptr)
        {
            hr = _device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr,
                                              &_computeShaderDASDA);
        }
        else
        {
            LOG_ERROR("[{0}] RCAS_CompileShader error for depth aware DAS shader!", _name);
            hr = _device->CreateComputeShader(reinterpret_cast<const void*>(da_das_sharpen_cso),
                                              sizeof(da_das_sharpen_cso), nullptr, &_computeShaderDASDA);
        }

        if (shaderBlob != nullptr)
        {
            shaderBlob->Release();
            shaderBlob = nullptr;
        }

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateComputeShader error for depth adaptive shader: {1:X}", _name, hr);
            return;
        }
    }

    // CBV
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(InternalConstantsDA);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    auto result = InDevice->CreateBuffer(&cbDesc, nullptr, &_constantBuffer);
    if (result != S_OK)
    {
        LOG_ERROR("CreateBuffer error: {0:X}", (UINT) result);
        return;
    }

    _init = true;
}

RCAS_Dx11::~RCAS_Dx11()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_computeShader != nullptr)
        _computeShader->Release();

    if (_computeShaderDA != nullptr)
        _computeShaderDA->Release();

    if (_constantBuffer != nullptr)
        _constantBuffer->Release();

    if (_srvInput != nullptr)
        _srvInput->Release();

    if (_srvMotionVectors != nullptr)
        _srvMotionVectors->Release();

    if (_srvDepth != nullptr)
        _srvDepth->Release();

    if (_uavOutput != nullptr)
        _uavOutput->Release();

    if (_buffer != nullptr)
        _buffer->Release();
}
