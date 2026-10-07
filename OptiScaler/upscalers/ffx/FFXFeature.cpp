#include <pch.h>
#include <Config.h>
#include "FFXFeature.h"
#include <proxies/FfxApi_Proxy.h>
#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3d12_Proxy.h>
#include <misc/IdentifyGpu.h>

static void FfxLogCallback(uint32_t type, const wchar_t* message)
{
    std::wstring string(message);
    LOG_DEBUG("FSR Runtime: {0}", wstring_to_string(string));
}

double FFXFeature::GetDeltaTime()
{
    double currentTime = Util::MillisecondsNow();
    double deltaTime = (currentTime - _lastFrameTime);
    _lastFrameTime = currentTime;
    return deltaTime;
}

bool FFXFeature::QueryVersionsDx12(ID3D12Device* device)
{
    auto& versions = State::Instance().ffxUpscalerVersionsDx12;

    // Get number of versions for allocation
    ffxQueryDescGetVersions versionQuery {};
    versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    versionQuery.device = device;
    uint64_t versionCount = 0;
    versionQuery.outputCount = &versionCount;

    if (FfxApiProxy::D3D12_Query(nullptr, &versionQuery.header) != FFX_API_RETURN_OK)
        return false;

    // Fill version ids and names arrays
    versions.ids.resize(versionCount);
    versions.names.resize(versionCount);
    versionQuery.versionIds = versions.ids.data();
    versionQuery.versionNames = versions.names.data();

    return FfxApiProxy::D3D12_Query(nullptr, &versionQuery.header) == FFX_API_RETURN_OK;
}

bool FFXFeature::QueryVersionsVulkan()
{
    auto& versions = State::Instance().ffxUpscalerVersionsVk;

    // Get number of versions for allocation
    ffxQueryDescGetVersions versionQuery {};
    versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    // versionQuery.device = Device; // only for DirectX 12 applications
    uint64_t versionCount = 0;
    versionQuery.outputCount = &versionCount;

    if (FfxApiProxy::VULKAN_Query()(nullptr, &versionQuery.header) != FFX_API_RETURN_OK)
        return false;

    // Fill version ids and names arrays
    versions.ids.resize(versionCount);
    versions.names.resize(versionCount);
    versionQuery.versionIds = versions.ids.data();
    versionQuery.versionNames = versions.names.data();

    return FfxApiProxy::VULKAN_Query()(nullptr, &versionQuery.header) == FFX_API_RETURN_OK;
}

void FFXFeature::EnsureVersionsDx12()
{
    static bool queryTried = false;

    if (queryTried || !State::Instance().ffxUpscalerVersionsDx12.names.empty())
        return;

    queryTried = true;

    FfxApiProxy::InitFfxDx12();

    if (!FfxApiProxy::IsSRReady(false))
        return;

    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

    // The device is used for filtering out unsupported upscalers (FSR4)
    auto device = State::Instance().currentD3D12Device;
    ID3D12Device* tempDevice = nullptr;

    // DX11 / Vulkan before any w/Dx12 upscaler got created
    if (device == nullptr)
    {
        ScopedSkipVulkanHooks skipVulkanHooks {};
        ScopedCreatingD3DDevice creatingD3DDevice {};

        IDXGIFactory2* factory = nullptr;
        HRESULT result;

        if (DxgiProxy::Module() == nullptr)
            result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        else
            result = DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory), &factory);

        if (result != S_OK || factory == nullptr)
        {
            LOG_ERROR("Can't create factory: {:X}", (UINT) result);
            return;
        }

        IDXGIAdapter* hwAdapter = nullptr;
        IdentifyGpu::getHardwareAdapter(factory, &hwAdapter, D3D_FEATURE_LEVEL_12_0);

        if (D3d12Proxy::Module() == nullptr)
            result = D3D12CreateDevice(hwAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&tempDevice));
        else
            result = D3d12Proxy::D3D12CreateDevice_()(hwAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&tempDevice));

        if (hwAdapter != nullptr)
            hwAdapter->Release();

        factory->Release();

        if (result != S_OK || tempDevice == nullptr)
        {
            LOG_ERROR("Can't create temp D3D12 device: {:X}", (UINT) result);
            return;
        }

        device = tempDevice;
    }

    if (!QueryVersionsDx12(device))
        LOG_ERROR("Querying FFX DX12 upscaler versions failed");

    if (tempDevice != nullptr)
        tempDevice->Release();
}

void FFXFeature::EnsureVersionsVulkan()
{
    static bool queryTried = false;

    if (queryTried || !State::Instance().ffxUpscalerVersionsVk.names.empty())
        return;

    queryTried = true;

    if (!FfxApiProxy::InitFfxVk())
        return;

    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

    if (!QueryVersionsVulkan())
        LOG_ERROR("Querying FFX Vulkan upscaler versions failed");
}

void FFXFeature::InitFlags()
{
    _contextDesc.flags = 0;
    _contextDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;

#ifdef _DEBUG
    LOG_INFO("Debug checking enabled!");
    _contextDesc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
    _contextDesc.fpMessage = FfxLogCallback;
#endif

    if (DepthInverted())
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED;

    if (AutoExposure())
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;

    if (IsHdr())
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;

    if (JitteredMV())
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

    if (!LowResMV())
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

    if (Config::Instance()->FsrDebugView.value_or_default())
    {
        LOG_INFO("Debug view enabled!");
        _contextDesc.flags |= 512; // FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION
    }

    if (Config::Instance()->FsrNonLinearColorSpace.value_or_default())
    {
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        LOG_INFO("contextDesc.initFlags (NonLinearColorSpace) {0:b}", _contextDesc.flags);
    }

    if (Config::Instance()->OutputScalingEnabled.value_or_default() && (LowResMV() || RenderWidth() == DisplayWidth()))
    {
        float ssMulti = Config::Instance()->OutputScalingMultiplier.value_or_default();

        if (ssMulti < 0.5f)
        {
            ssMulti = 0.5f;
            Config::Instance()->OutputScalingMultiplier.set_volatile_value(ssMulti);
        }
        else if (ssMulti > 3.0f)
        {
            ssMulti = 3.0f;
            Config::Instance()->OutputScalingMultiplier.set_volatile_value(ssMulti);
        }

        _targetWidth = static_cast<unsigned int>(DisplayWidth() * ssMulti);
        _targetHeight = static_cast<unsigned int>(DisplayHeight() * ssMulti);
    }
    else
    {
        _targetWidth = DisplayWidth();
        _targetHeight = DisplayHeight();
    }

    // Extended limits changes resolution handling
    if (Config::Instance()->ExtendedLimits.value_or_default() && RenderWidth() > DisplayWidth())
    {
        _contextDesc.maxRenderSize.width = RenderWidth();
        _contextDesc.maxRenderSize.height = RenderHeight();

        Config::Instance()->OutputScalingMultiplier.set_volatile_value(1.0f);

        // If output scaling is active, let it handle downsampling
        if (Config::Instance()->OutputScalingEnabled.value_or_default() &&
            (LowResMV() || RenderWidth() == DisplayWidth()))
        {
            _contextDesc.maxUpscaleSize.width = _contextDesc.maxRenderSize.width;
            _contextDesc.maxUpscaleSize.height = _contextDesc.maxRenderSize.height;

            // Update target resolution
            _targetWidth = _contextDesc.maxRenderSize.width;
            _targetHeight = _contextDesc.maxRenderSize.height;
        }
        else
        {
            _contextDesc.maxUpscaleSize.width = DisplayWidth();
            _contextDesc.maxUpscaleSize.height = DisplayHeight();
        }
    }
    else
    {
        _contextDesc.maxRenderSize.width = TargetWidth() > DisplayWidth() ? TargetWidth() : DisplayWidth();
        _contextDesc.maxRenderSize.height = TargetHeight() > DisplayHeight() ? TargetHeight() : DisplayHeight();
        _contextDesc.maxUpscaleSize.width = TargetWidth();
        _contextDesc.maxUpscaleSize.height = TargetHeight();
    }

    // Set stability values as default if not set by user
    {
        auto config = Config::Instance();
        auto const scaleRatioX = (float) TargetWidth() / (float) RenderWidth();
        auto const scaleRatioY = (float) TargetHeight() / (float) RenderHeight();
        auto const scaleRatio = std::max(scaleRatioX, scaleRatioY);

        if (scaleRatio > 0.0f && !std::isinf(scaleRatio))
        {
            if (config->FsrVelocity.value_for_config() == std::nullopt)
                config->FsrVelocity.set_volatile_value(0.5f);

            if (config->FsrReactiveScale.value_for_config() == std::nullopt)
                config->FsrReactiveScale.set_volatile_value(0.25f);

            if (config->FsrShadingScale.value_for_config() == std::nullopt)
                config->FsrShadingScale.set_volatile_value(0.5f / scaleRatio);

            if (config->FsrAccAddPerFrame.value_for_config() == std::nullopt)
                config->FsrAccAddPerFrame.set_volatile_value(scaleRatio / 10.0f);

            if (config->FsrMinDisOccAcc.value_for_config() == std::nullopt)
                config->FsrMinDisOccAcc.set_volatile_value(scaleRatio / 20.0f);
        }
    }
}

FFXFeature::FFXFeature(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters) : IFeature(InHandleId, InParameters)
{
    _initParameters = SetInitParameters(InParameters);
    _lastFrameTime = Util::MillisecondsNow();
}

FFXFeature::~FFXFeature()
{
    if (!IsInited())
        return;

    SetInit(false);
}
