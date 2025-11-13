#include "XeFG_Dx12.h"

#include <hudfix/Hudfix_Dx12.h>
#include <menu/menu_overlay_dx.h>
#include <resource_tracking/ResTrack_dx12.h>

#include <nvapi/fakenvapi.h>

#include <magic_enum.hpp>

#include <DirectXMath.h>

using namespace DirectX;

void XeFG_Dx12::xefgLogCallback(const char* message, xefg_swapchain_logging_level_t level, void* userData)
{
    switch (level)
    {
    case XEFG_SWAPCHAIN_LOGGING_LEVEL_DEBUG:
        spdlog::debug("XeFG Log: {}", message);
        return;

    case XEFG_SWAPCHAIN_LOGGING_LEVEL_INFO:
        spdlog::info("XeFG Log: {}", message);
        return;

    case XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING:
        spdlog::warn("XeFG Log: {}", message);
        return;

    default:
        spdlog::error("XeFG Log: {}", message);
        return;
    }
}

bool XeFG_Dx12::CreateSwapchainContext(ID3D12Device* device)
{
    if (XeFGProxy::Module() == nullptr && !XeFGProxy::InitXeFG())
    {
        LOG_ERROR("XeFG proxy can't find libxess_fg.dll!");
        return false;
    }

    auto createResult = false;

    State::Instance().skipSpoofing = true;

    do
    {
        auto result = XeFGProxy::D3D12CreateContext()(device, &_swapChainContext);

        if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
        {
            LOG_ERROR("D3D12CreateContext error: {} ({})", magic_enum::enum_name(result), (UINT) result);
            return result;
        }

        LOG_INFO("XeFG context created");
        result = XeFGProxy::SetLoggingCallback()(_swapChainContext, XEFG_SWAPCHAIN_LOGGING_LEVEL_DEBUG, xefgLogCallback,
                                                 nullptr);

        if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
        {
            LOG_ERROR("SetLoggingCallback error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        }

        if (XeLLProxy::Context() == nullptr)
            XeLLProxy::CreateContext(device);

        if (XeLLProxy::Context() != nullptr)
        {
            xell_sleep_params_t sleepParams = {};
            sleepParams.bLowLatencyMode = true;
            sleepParams.bLowLatencyBoost = false;
            sleepParams.minimumIntervalUs = 0;

            auto xellResult = XeLLProxy::SetSleepMode()(XeLLProxy::Context(), &sleepParams);
            if (xellResult != XELL_RESULT_SUCCESS)
            {
                LOG_ERROR("SetSleepMode error: {} ({})", magic_enum::enum_name(xellResult), (UINT) xellResult);
                return result;
            }

            auto fnaResult = fakenvapi::setModeAndContext(XeLLProxy::Context(), Mode::XeLL);
            LOG_DEBUG("fakenvapi::setModeAndContext: {}", fnaResult);

            result = XeFGProxy::SetLatencyReduction()(_swapChainContext, XeLLProxy::Context());

            if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
            {
                LOG_ERROR("SetLatencyReduction error: {} ({})", magic_enum::enum_name(result), (UINT) result);
                return result;
            }
        };

        createResult = true;

    } while (false);

    State::Instance().skipSpoofing = false;

    return createResult;
}

const char* XeFG_Dx12::Name() { return "XeFG"; }

feature_version XeFG_Dx12::Version()
{
    if (XeFGProxy::InitXeFG())
    {
        auto ver = XeFGProxy::Version();
        return ver;
    }

    return { 0, 0, 0 };
}

HWND XeFG_Dx12::Hwnd() { return _hwnd; }

bool XeFG_Dx12::DestroySwapchainContext()
{
    LOG_DEBUG("");

    _isActive = false;

    if (_swapChainContext != nullptr && !State::Instance().isShuttingDown)
    {
        auto context = _swapChainContext;
        _swapChainContext = nullptr;

        auto result = XeFGProxy::Destroy()(context);

        LOG_INFO("Destroy result: {} ({})", magic_enum::enum_name(result), (UINT) result);

        // Set it back because context is not destroyed
        if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
            _swapChainContext = context;
        else
            State::Instance().currentFGSwapchain = nullptr;
    }

    return true;
}

xefg_swapchain_d3d12_resource_data_t XeFG_Dx12::GetResourceData(FG_ResourceType type)
{
    auto fIndex = GetIndex();

    xefg_swapchain_d3d12_resource_data_t resourceParam = {};

    if (!_frameResources[fIndex].contains(type))
        return resourceParam;

    auto fResource = &_frameResources[fIndex].at(type);

    resourceParam.validity = (fResource->validity == FG_ResourceValidity::ValidNow)
                                 ? XEFG_SWAPCHAIN_RV_ONLY_NOW
                                 : XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;

    resourceParam.resourceBase = { fResource->left, fResource->top };
    resourceParam.resourceSize = { static_cast<uint32_t>(fResource->width), fResource->height };
    resourceParam.pResource = fResource->GetResource();
    resourceParam.incomingState = fResource->state;

    switch (type)
    {
    case FG_ResourceType::Depth:
        resourceParam.type = XEFG_SWAPCHAIN_RES_DEPTH;
        break;

    case FG_ResourceType::HudlessColor:
        resourceParam.type = XEFG_SWAPCHAIN_RES_HUDLESS_COLOR;
        break;

    case FG_ResourceType::UIColor:
        resourceParam.type = XEFG_SWAPCHAIN_RES_UI;
        break;

    case FG_ResourceType::Velocity:
        resourceParam.type = XEFG_SWAPCHAIN_RES_MOTION_VECTOR;
        break;
    default:
        LOG_WARN("Unsupported resource type: {}", magic_enum::enum_name(type));
        return xefg_swapchain_d3d12_resource_data_t {};
    }

    return resourceParam;
}

bool XeFG_Dx12::CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                                IDXGISwapChain** swapChain)
{
    if (_swapChainContext == nullptr)
    {
        if (State::Instance().currentD3D12Device == nullptr)
            return false;

        CreateSwapchainContext(State::Instance().currentD3D12Device);

        if (_swapChainContext == nullptr)
            return false;

        _width = desc->BufferDesc.Width;
        _height = desc->BufferDesc.Height;
    }

    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* realQueue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &realQueue))
        realQueue = cmdQueue;

    IDXGIFactory2* factory12 = nullptr;
    if (realFactory->QueryInterface(IID_PPV_ARGS(&factory12)) != S_OK)
        return false;

    factory12->Release();

    HWND hwnd = desc->OutputWindow;
    DXGI_SWAP_CHAIN_DESC1 scDesc {};

    scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE; // No info
    scDesc.BufferCount = desc->BufferCount;
    scDesc.BufferUsage = desc->BufferUsage;
    scDesc.Flags = desc->Flags;
    scDesc.Format = desc->BufferDesc.Format;
    scDesc.Height = desc->BufferDesc.Height;
    scDesc.SampleDesc = desc->SampleDesc;

    switch (desc->BufferDesc.Scaling)
    {
    case DXGI_MODE_SCALING_CENTERED:
        scDesc.Scaling = DXGI_SCALING_ASPECT_RATIO_STRETCH;
        break;

    case DXGI_MODE_SCALING_STRETCHED:
        scDesc.Scaling = DXGI_SCALING_STRETCH;
        break;

    case DXGI_MODE_SCALING_UNSPECIFIED:
        scDesc.Scaling = DXGI_SCALING_NONE;
        break;
    }

    scDesc.Stereo = false; // No info
    scDesc.SwapEffect = desc->SwapEffect;
    scDesc.Width = desc->BufferDesc.Width;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc {};
    fsDesc.RefreshRate = desc->BufferDesc.RefreshRate;
    fsDesc.Scaling = desc->BufferDesc.Scaling;
    fsDesc.ScanlineOrdering = desc->BufferDesc.ScanlineOrdering;
    fsDesc.Windowed = desc->Windowed;

    xefg_swapchain_d3d12_init_params_t params {};
    params.maxInterpolatedFrames = 1;

    params.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_NONE;

    // if (State::Instance().activeFgInput != FGInput::Upscaler &&
    //     Config::Instance()->FGXeFGDepthInverted.value_or_default())
    //     params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH;

    if (Config::Instance()->FGXeFGJitteredMV.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_JITTERED_MV;

    if (Config::Instance()->FGXeFGHighResMV.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_HIGH_RES_MV;

    if (!Config::Instance()->FGUIPremultipliedAlpha.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_UITEXTURE_NOT_PREMUL_ALPHA;

    LOG_DEBUG("Inverted Depth: {}", Config::Instance()->FGXeFGDepthInverted.value_or_default());
    LOG_DEBUG("Jittered Velocity: {}", Config::Instance()->FGXeFGJitteredMV.value_or_default());
    LOG_DEBUG("High Res MV: {}", Config::Instance()->FGXeFGHighResMV.value_or_default());

    if (Config::Instance()->FGXeFGDepthInverted.value_or_default())
        _constants.flags |= FG_Flags::InvertedDepth;

    if (Config::Instance()->FGXeFGJitteredMV.value_or_default())
        _constants.flags |= FG_Flags::JitteredMVs;

    if (Config::Instance()->FGXeFGHighResMV.value_or_default())
        _constants.flags |= FG_Flags::DisplayResolutionMVs;

    State::Instance().skipSpoofing = true;

    auto result = XeFGProxy::D3D12InitFromSwapChainDesc()(_swapChainContext, hwnd, &scDesc, &fsDesc, realQueue,
                                                          factory12, &params);

    State::Instance().skipSpoofing = false;

    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12InitFromSwapChainDesc error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    LOG_INFO("XeFG swapchain created");
    result = XeFGProxy::D3D12GetSwapChainPtr()(_swapChainContext, IID_PPV_ARGS(swapChain));
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12GetSwapChainPtr error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    xefg_swapchain_properties_t props {};
    result = XeFGProxy::GetProperties()(_swapChainContext, &props);
    if (result == XEFG_SWAPCHAIN_RESULT_SUCCESS)
        LOG_INFO("Max supported interpolations: {}", props.maxSupportedInterpolations);
    else
        LOG_ERROR("Can't get swapchain properties: {} ({})", magic_enum::enum_name(result), (UINT) result);

    _gameCommandQueue = realQueue;
    _swapChain = *swapChain;
    _hwnd = hwnd;

    return true;
}

bool XeFG_Dx12::CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                 DXGI_SWAP_CHAIN_DESC1* desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                 IDXGISwapChain1** swapChain)
{
    if (_swapChainContext == nullptr)
    {
        if (State::Instance().currentD3D12Device == nullptr)
            return false;

        CreateSwapchainContext(State::Instance().currentD3D12Device);

        if (_swapChainContext == nullptr)
            return false;

        _width = desc->Width;
        _height = desc->Height;
    }

    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* realQueue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &realQueue))
        realQueue = cmdQueue;

    IDXGIFactory2* factory12 = nullptr;
    if (realFactory->QueryInterface(IID_PPV_ARGS(&factory12)) != S_OK)
        return false;

    factory12->Release();

    xefg_swapchain_d3d12_init_params_t params {};
    params.maxInterpolatedFrames = 1;

    params.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_NONE;

    // if (State::Instance().activeFgInput != FGInput::Upscaler &&
    //     Config::Instance()->FGXeFGDepthInverted.value_or_default())
    //     params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH;

    if (Config::Instance()->FGXeFGJitteredMV.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_JITTERED_MV;

    if (Config::Instance()->FGXeFGHighResMV.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_HIGH_RES_MV;

    if (!Config::Instance()->FGUIPremultipliedAlpha.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_UITEXTURE_NOT_PREMUL_ALPHA;

    LOG_DEBUG("Inverted Depth: {}", Config::Instance()->FGXeFGDepthInverted.value_or_default());
    LOG_DEBUG("Jittered Velocity: {}", Config::Instance()->FGXeFGJitteredMV.value_or_default());
    LOG_DEBUG("High Res MV: {}", Config::Instance()->FGXeFGHighResMV.value_or_default());

    if (Config::Instance()->FGXeFGDepthInverted.value_or_default())
        _constants.flags |= FG_Flags::InvertedDepth;

    if (Config::Instance()->FGXeFGJitteredMV.value_or_default())
        _constants.flags |= FG_Flags::JitteredMVs;

    if (Config::Instance()->FGXeFGHighResMV.value_or_default())
        _constants.flags |= FG_Flags::DisplayResolutionMVs;

    State::Instance().skipSpoofing = true;

    auto result = XeFGProxy::D3D12InitFromSwapChainDesc()(_swapChainContext, hwnd, desc, pFullscreenDesc, realQueue,
                                                          factory12, &params);

    State::Instance().skipSpoofing = false;

    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12InitFromSwapChainDesc error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    LOG_INFO("XeFG swapchain created");
    result = XeFGProxy::D3D12GetSwapChainPtr()(_swapChainContext, IID_PPV_ARGS(swapChain));
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12GetSwapChainPtr error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    xefg_swapchain_properties_t props {};
    result = XeFGProxy::GetProperties()(_swapChainContext, &props);
    if (result == XEFG_SWAPCHAIN_RESULT_SUCCESS)
        LOG_INFO("Max supported interpolations: {}", props.maxSupportedInterpolations);
    else
        LOG_ERROR("Can't get swapchain properties: {} ({})", magic_enum::enum_name(result), (UINT) result);

    _gameCommandQueue = realQueue;
    _swapChain = *swapChain;
    _hwnd = hwnd;

    return true;
}

void XeFG_Dx12::CreateContext(ID3D12Device* device, FG_Constants& fgConstants)
{
    LOG_DEBUG("");

    _device = device;
    CreateObjects(device);

    if (_fgContext == nullptr && _swapChainContext != nullptr)
    {
        _fgContext = _swapChainContext;
    }
}

void XeFG_Dx12::Activate()
{
    LOG_DEBUG("");

    auto currentFeature = State::Instance().currentFeature;
    bool nativeAA = false;
    if (State::Instance().activeFgInput == FGInput::Upscaler && currentFeature != nullptr)
        nativeAA = currentFeature->RenderWidth() == currentFeature->DisplayWidth();

    if (_swapChainContext != nullptr && _fgContext != nullptr && !_isActive && (IsLowResMV() || nativeAA))
    {
        auto result = XeFGProxy::SetEnabled()(_swapChainContext, true);

        if (result == XEFG_SWAPCHAIN_RESULT_SUCCESS)
            _isActive = true;

        LOG_INFO("SetEnabled: true, result: {} ({})", magic_enum::enum_name(result), (UINT) result);
    }
}

void XeFG_Dx12::Deactivate()
{
    LOG_DEBUG("");

    if (_isActive)
    {
        auto fIndex = GetIndex();
        if (_uiCommandListResetted[fIndex])
        {
            LOG_DEBUG("Executing _uiCommandList[fIndex][{}]: {:X}", fIndex, (size_t) _uiCommandList[fIndex]);
            auto closeResult = _uiCommandList[fIndex]->Close();

            if (closeResult == S_OK)
                _gameCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**) &_uiCommandList[fIndex]);
            else
                LOG_ERROR("_uiCommandList[{}]->Close() error: {:X}", fIndex, (UINT) closeResult);

            _uiCommandListResetted[fIndex] = false;
        }

        auto result = XeFGProxy::SetEnabled()(_swapChainContext, false);

        if (result == XEFG_SWAPCHAIN_RESULT_SUCCESS)
            _isActive = false;

        _lastDispatchedFrame = 0;
        _waitingNewFrameData = false;

        LOG_INFO("SetEnabled: false, result: {} ({})", magic_enum::enum_name(result), (UINT) result);
    }
}

void XeFG_Dx12::DestroyFGContext()
{
    Deactivate();

    if (_fgContext != nullptr)
        _fgContext = nullptr;

    if (State::Instance().isShuttingDown)
        ReleaseObjects();
}

bool XeFG_Dx12::Shutdown()
{
    MenuOverlayDx::CleanupRenderTarget(true, NULL);

    if (_fgContext != nullptr)
        DestroyFGContext();

    ReleaseObjects();

    if (_swapChainContext != nullptr)
        DestroySwapchainContext();

    return true;
}

bool XeFG_Dx12::Dispatch()
{
    LOG_DEBUG();

    auto fIndex = GetDispatchIndex();
    if (fIndex < 0)
        return false;

    if (!IsActive() || IsPaused())
        return false;

    auto& state = State::Instance();

    LOG_DEBUG("_frameCount: {}, _willDispatchFrame: {}, fIndex: {}", _frameCount, _willDispatchFrame, fIndex);

    if (!_resourceReady[fIndex].contains(FG_ResourceType::Depth) ||
        !_resourceReady[fIndex].at(FG_ResourceType::Depth) ||
        !_resourceReady[fIndex].contains(FG_ResourceType::Velocity) ||
        !_resourceReady[fIndex].at(FG_ResourceType::Velocity))
    {
        LOG_WARN("Depth or Velocity is not ready, skipping");
        return false;
    }

    if (!_haveHudless.has_value())
    {
        _haveHudless = IsUsingHudless(fIndex);
    }
    else
    {
        auto usingHudless = IsUsingHudless(fIndex);
        static auto version = Version();

        // SDK version 2.1.1 fixed this
        // https://github.com/intel/xess/issues/48
        if (version < feature_version { 1, 2, 2 } && _haveHudless.value() != usingHudless)
        {
            LOG_INFO("Hudless state changed {} -> {}, skipping rendering for 10 frames", _haveHudless.value(),
                     usingHudless);

            _haveHudless = usingHudless;
            state.FGchanged = true;
            UpdateTarget();
            Deactivate();

            return false;
        }
    }

    XeFGProxy::EnableDebugFeature()(_swapChainContext, XEFG_SWAPCHAIN_DEBUG_FEATURE_TAG_INTERPOLATED_FRAMES,
                                    Config::Instance()->FGXeFGDebugView.value_or_default(), nullptr);
    XeFGProxy::EnableDebugFeature()(_swapChainContext, XEFG_SWAPCHAIN_DEBUG_FEATURE_SHOW_ONLY_INTERPOLATION,
                                    state.FGonlyGenerated, nullptr);
    // XeFGProxy::EnableDebugFeature()(_swapChainContext, XEFG_SWAPCHAIN_DEBUG_FEATURE_PRESENT_FAILED_INTERPOLATION,
    //                                 state.FGonlyGenerated, nullptr);

    xefg_swapchain_frame_constant_data_t constData = {};

    if (_cameraPosition[fIndex][0] != 0.0f || _cameraPosition[fIndex][1] != 0.0f || _cameraPosition[fIndex][2] != 0.0f)
    {
        XMVECTOR right = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraRight[fIndex]));
        XMVECTOR up = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraUp[fIndex]));
        XMVECTOR forward = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraForward[fIndex]));
        XMVECTOR pos = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraPosition[fIndex]));

        float x = -XMVectorGetX(XMVector3Dot(pos, right));
        float y = -XMVectorGetX(XMVector3Dot(pos, up));
        float z = -XMVectorGetX(XMVector3Dot(pos, forward));

        XMMATRIX view = { XMVectorSet(XMVectorGetX(right), XMVectorGetX(up), XMVectorGetX(forward), 0.0f),
                          XMVectorSet(XMVectorGetY(right), XMVectorGetY(up), XMVectorGetY(forward), 0.0f),
                          XMVectorSet(XMVectorGetZ(right), XMVectorGetZ(up), XMVectorGetZ(forward), 0.0f),
                          XMVectorSet(x, y, z, 1.0f) };

        memcpy(constData.viewMatrix, view.r, sizeof(view));
    }

    if (Config::Instance()->FGXeFGDepthInverted.value_or_default())
        std::swap(_cameraNear[fIndex], _cameraFar[fIndex]);

    if (_infiniteDepth && _cameraFar[fIndex] > _cameraNear[fIndex])
        _cameraFar[fIndex] = std::numeric_limits<float>::infinity();
    else if (_infiniteDepth && _cameraNear[fIndex] > _cameraFar[fIndex])
        _cameraNear[fIndex] = std::numeric_limits<float>::infinity();

    // Cyberpunk seems to be sending LH so do the same
    // it also sends some extra data in usually empty spots but no idea what that is
    if (_cameraNear[fIndex] > 0.f && _cameraFar[fIndex] > 0.f &&
        !XMScalarNearEqual(_cameraVFov[fIndex], 0.0f, 0.00001f) &&
        !XMScalarNearEqual(_cameraAspectRatio[fIndex], 0.0f, 0.00001f))
    {
        if (XMScalarNearEqual(_cameraNear[fIndex], _cameraFar[fIndex], 0.00001f))
            _cameraFar[fIndex]++;

        auto projectionMatrix = XMMatrixPerspectiveFovLH(_cameraVFov[fIndex], _cameraAspectRatio[fIndex],
                                                         _cameraNear[fIndex], _cameraFar[fIndex]);
        memcpy(constData.projectionMatrix, projectionMatrix.r, sizeof(projectionMatrix));
    }
    else
    {
        LOG_WARN("Can't calculate projectionMatrix");
    }

    constData.jitterOffsetX = _jitterX[fIndex];
    constData.jitterOffsetY = _jitterY[fIndex];
    constData.motionVectorScaleX = _mvScaleX[fIndex];
    constData.motionVectorScaleY = _mvScaleY[fIndex];

    if (!Config::Instance()->FGSkipReset.value_or_default())
        constData.resetHistory = _reset[fIndex];
    else
        constData.resetHistory = false;

    constData.frameRenderTime = static_cast<float>(state.lastFGFrameTime);

    LOG_DEBUG("Reset: {}, FTDelta: {}", _reset[fIndex], constData.frameRenderTime);

    auto frameId = static_cast<uint32_t>(_willDispatchFrame);

    auto result = XeFGProxy::TagFrameConstants()(_swapChainContext, frameId, &constData);
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("TagFrameConstants error: {} ({})", magic_enum::enum_name(result), (UINT) result);

        state.FGchanged = true;
        UpdateTarget();
        Deactivate();

        return false;
    }

    result = XeFGProxy::SetPresentId()(_swapChainContext, frameId);
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("SetPresentId error: {} ({})", magic_enum::enum_name(result), (UINT) result);

        state.FGchanged = true;
        UpdateTarget();
        Deactivate();

        return false;
    }

    // When using Hudfix, we always copy hudless as swapchain size
    if (state.activeFgInput != FGInput::Upscaler)
    {
        uint32_t left = 0;
        uint32_t top = 0;

        if (_interpolationWidth[fIndex] == 0 && _interpolationHeight[fIndex] == 0)
        {
            LOG_WARN("Interpolation size is 0, using swapchain size");
            _interpolationWidth[fIndex] = state.currentSwapchainDesc.BufferDesc.Width;
            _interpolationHeight[fIndex] = state.currentSwapchainDesc.BufferDesc.Height;
        }
        else
        {
            auto calculatedLeft =
                ((int) state.currentSwapchainDesc.BufferDesc.Width - (int) _interpolationWidth[fIndex]) / 2;
            if (calculatedLeft > 0)
                left = Config::Instance()->FGRectLeft.value_or(_interpolationLeft[fIndex].value_or(calculatedLeft));

            auto calculatedTop =
                ((int) state.currentSwapchainDesc.BufferDesc.Height - (int) _interpolationHeight[fIndex]) / 2;
            if (calculatedTop > 0)
                top = Config::Instance()->FGRectTop.value_or(_interpolationTop[fIndex].value_or(calculatedTop));
        }

        LOG_DEBUG("SwapChain Res: {}x{}, Interpolation Res: {}x{}", state.currentSwapchainDesc.BufferDesc.Width,
                  state.currentSwapchainDesc.BufferDesc.Height, _interpolationWidth[fIndex],
                  _interpolationHeight[fIndex]);

        xefg_swapchain_d3d12_resource_data_t backbuffer = {};
        backbuffer.type = XEFG_SWAPCHAIN_RES_BACKBUFFER;
        backbuffer.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
        backbuffer.resourceBase = { (UINT) Config::Instance()->FGRectLeft.value_or(left),
                                    (UINT) Config::Instance()->FGRectTop.value_or(top) };
        backbuffer.resourceSize = { static_cast<uint32_t>(
                                        Config::Instance()->FGRectWidth.value_or(_interpolationWidth[fIndex])),
                                    (UINT) Config::Instance()->FGRectHeight.value_or(_interpolationHeight[fIndex]) };

        auto result =
            XeFGProxy::D3D12TagFrameResource()(_swapChainContext, (ID3D12CommandList*) 1, frameId, &backbuffer);

        if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
        {
            LOG_ERROR("D3D12TagFrameResource Backbuffer error: {} ({})", magic_enum::enum_name(result), (UINT) result);

            state.FGchanged = true;
            UpdateTarget();
            Deactivate();
        }
    }

    LOG_DEBUG("Result: Ok");

    return true;
}

void* XeFG_Dx12::FrameGenerationContext() { return _fgContext; }

void* XeFG_Dx12::SwapchainContext() { return _swapChainContext; }

XeFG_Dx12::~XeFG_Dx12() { Shutdown(); }

void XeFG_Dx12::EvaluateState(ID3D12Device* device, FG_Constants& fgConstants)
{
    LOG_FUNC();

    auto& state = State::Instance();

    // If needed hooks are missing or XeFG proxy is not inited or FG swapchain is not created
    if (!XeFGProxy::InitXeFG() || state.currentFGSwapchain == nullptr)
        return;

    if (state.isShuttingDown)
    {
        DestroyFGContext();
        return;
    }

    _infiniteDepth = static_cast<bool>(fgConstants.flags & FG_Flags::InfiniteDepth);

    // If FG Enabled from menu
    if (Config::Instance()->FGEnabled.value_or_default())
    {
        // If FG context is nullptr
        if (_fgContext == nullptr)
        {
            // Create it again
            CreateContext(device, fgConstants);

            // Pause for 10 frames
            UpdateTarget();
        }
        // If there is a change deactivate it
        else if (state.FGchanged)
        {
            LOG_DEBUG("FGChanged");
            Deactivate();

            // Pause for 10 frames
            UpdateTarget();

            // Destroy if Swapchain has a change destroy FG Context too
            if (state.SCchanged)
                DestroyFGContext();
        }

        if (_fgContext != nullptr && !IsPaused() && !IsActive())
            Activate();
    }
    else
    {
        LOG_DEBUG("!FGEnabled");
        Deactivate();

        state.ClearCapturedHudlesses = true;
        Hudfix_Dx12::ResetCounters();
    }

    if (state.FGchanged)
    {
        LOG_DEBUG("FGchanged");

        state.FGchanged = false;

        Hudfix_Dx12::ResetCounters();

        // Pause for 10 frames
        UpdateTarget();

        // Release FG mutex
        if (Mutex.getOwner() == 2)
            Mutex.unlockThis(2);
    }

    state.SCchanged = false;
}

void XeFG_Dx12::ReleaseObjects()
{
    _mvFlip.reset();
    _depthFlip.reset();
    _depthInvert.reset();
}

void XeFG_Dx12::CreateObjects(ID3D12Device* InDevice)
{
    if (_uiCommandAllocator[0] != nullptr)
        return;

    LOG_DEBUG("");

    do
    {
        HRESULT result;
        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* cmdList = nullptr;
        ID3D12CommandQueue* cmdQueue = nullptr;

        // FG
        for (size_t i = 0; i < BUFFER_COUNT; i++)
        {
            result =
                InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_uiCommandAllocator[i]));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandAllocators _uiCommandAllocator[{}]: {:X}", i, (unsigned long) result);
                break;
            }

            _uiCommandAllocator[i]->SetName(std::format(L"_uiCommandAllocator[{}]", i).c_str());
            if (CheckForRealObject(__FUNCTION__, _uiCommandAllocator[i], (IUnknown**) &allocator))
                _uiCommandAllocator[i] = allocator;

            result = InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _uiCommandAllocator[i], NULL,
                                                 IID_PPV_ARGS(&_uiCommandList[i]));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandList _hudlessCommandList[{}]: {:X}", i, (unsigned long) result);
                break;
            }
            _uiCommandList[i]->SetName(std::format(L"_uiCommandList[{}]", i).c_str());
            if (CheckForRealObject(__FUNCTION__, _uiCommandList[i], (IUnknown**) &cmdList))
                _uiCommandList[i] = cmdList;

            result = _uiCommandList[i]->Close();
            if (result != S_OK)
            {
                LOG_ERROR("_uiCommandList[{}]->Close: {:X}", i, (unsigned long) result);
                break;
            }
        }

    } while (false);
}

bool XeFG_Dx12::Present()
{
    if (IsActive() && !IsPaused() && State::Instance().FGHudlessCompare)
    {
        auto hudless = GetResource(FG_ResourceType::HudlessColor);
        if (hudless != nullptr && hudless->validity == FG_ResourceValidity::UntilPresent)
        {
            if (_hudlessCompare.get() == nullptr)
            {
                _hudlessCompare = std::make_unique<HC_Dx12>("HudlessCompare", _device);
            }
            else
            {
                if (_hudlessCompare->IsInit())
                {
                    _hudlessCompare->Dispatch((IDXGISwapChain3*) _swapChain, _gameCommandQueue, hudless->GetResource(),
                                              hudless->state);
                }
            }
        }
    }

    bool result = false;

    if (IsActive() && !IsPaused())
    {
        auto fIndex = GetIndex();
        if (_uiCommandListResetted[fIndex])
        {
            LOG_DEBUG("Executing _uiCommandList[{}]: {:X}", fIndex, (size_t) _uiCommandList[fIndex]);
            auto closeResult = _uiCommandList[fIndex]->Close();

            if (closeResult == S_OK)
                _gameCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**) &_uiCommandList[fIndex]);
            else
                LOG_ERROR("_uiCommandList[{}]->Close() error: {:X}", fIndex, (UINT) closeResult);

            _uiCommandListResetted[fIndex] = false;
        }
    }

    if ((_fgFramePresentId - _lastFGFramePresentId) > 3 && IsActive() && !_waitingNewFrameData)
    {
        LOG_DEBUG("Pausing FG");
        Deactivate();
        _waitingNewFrameData = true;
        return false;
    }

    _fgFramePresentId++;

    return Dispatch();
}

bool XeFG_Dx12::SetResource(Dx12Resource* inputResource)
{
    if (inputResource == nullptr || inputResource->resource == nullptr || !IsActive() || IsPaused())
        return false;

    // For late sent SL resources
    // we use provided frame index
    auto fIndex = inputResource->frameIndex;
    if (fIndex < 0)
        fIndex = GetIndex();

    auto& type = inputResource->type;

    if (_resourceFrame[type] == _frameCount || _frameResources[fIndex][type].resource != nullptr)
    {
        LOG_WARN("Repeating resource tagging for {}[{}], ignoring.", magic_enum::enum_name(type), fIndex);
        return false;
    }

    if (type == FG_ResourceType::HudlessColor && Config::Instance()->FGDisableHudless.value_or_default())
        return false;

    if (type == FG_ResourceType::UIColor && Config::Instance()->FGDisableUI.value_or_default())
        return false;

    std::lock_guard<std::mutex> lock(_frMutex);

    if (inputResource->cmdList == nullptr && inputResource->validity == FG_ResourceValidity::ValidNow)
    {
        LOG_ERROR("{}, validity == ValidNow but cmdList is nullptr!", magic_enum::enum_name(type));
        return false;
    }

    if (type == FG_ResourceType::Distortion)
    {
        LOG_TRACE("Distortion field is not supported by XeFG");
        return false;
    }

    auto fResource = &_frameResources[fIndex][type];
    fResource->type = type;
    fResource->state = inputResource->state;
    fResource->validity = inputResource->validity;
    fResource->resource = inputResource->resource;
    fResource->top = inputResource->top;
    fResource->left = inputResource->left;
    fResource->width = inputResource->width;
    fResource->height = inputResource->height;
    fResource->cmdList = inputResource->cmdList;

    auto willFlip = State::Instance().activeFgInput == FGInput::Upscaler &&
                    Config::Instance()->FGResourceFlip.value_or_default() &&
                    (type == FG_ResourceType::Velocity || type == FG_ResourceType::Depth);

    // Resource flipping
    if (willFlip && _device != nullptr)
    {
        FlipResource(fResource);
    }

    // Depth Invert
    // https://github.com/intel/xess/issues/50
    static auto version = Version();

    // SDK version 2.1.1 fixed this
    if (version < feature_version { 1, 2, 2 } && _device != nullptr && type == FG_ResourceType::Depth &&
        !Config::Instance()->FGXeFGDepthInverted.value_or_default())
    {
        if (_depthInvert.get() == nullptr)
        {
            _depthInvert = std::make_unique<DI_Dx12>("DepthInvert", _device);
        }
        else if (_depthInvert->IsInit())
        {
            if (_depthInvert->CreateBufferResource(_device, fResource->GetResource(), fResource->width,
                                                   fResource->height, fResource->state) &&
                _depthInvert->Buffer() != nullptr)
            {
                auto cmdList = (fResource->cmdList != nullptr) ? fResource->cmdList : GetUICommandList(fIndex);

                _depthInvert->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                if (_depthInvert->Dispatch(_device, cmdList, fResource->GetResource(), _depthInvert->Buffer()))
                {
                    fResource->copy = _depthInvert->Buffer();
                }

                _depthInvert->SetBufferState(cmdList, fResource->state);
            }
        }
    }

    // We usually don't copy any resources for XeFG, the ones with this tag are the exception
    if (inputResource->cmdList != nullptr && fResource->validity == FG_ResourceValidity::ValidButMakeCopy)
    {
        LOG_DEBUG("Making a resource copy of: {}", magic_enum::enum_name(type));

        ID3D12Resource* copyOutput = nullptr;

        if (_resourceCopy[fIndex].contains(type))
            copyOutput = _resourceCopy[fIndex][type];

        if (!CopyResource(inputResource->cmdList, inputResource->resource, &copyOutput, inputResource->state))
        {
            LOG_ERROR("{}, CopyResource error!", magic_enum::enum_name(type));
            return false;
        }

        _resourceCopy[fIndex][type] = copyOutput;
        _resourceCopy[fIndex][type]->SetName(std::format(L"_resourceCopy[{}][{}]", fIndex, (UINT) type).c_str());
        fResource->copy = copyOutput;
        fResource->state = D3D12_RESOURCE_STATE_COPY_DEST;

        fResource->validity = FG_ResourceValidity::UntilPresent;
    }

    if (type == FG_ResourceType::UIColor)
        _noUi[fIndex] = false;
    else if (type == FG_ResourceType::Distortion)
        _noDistortionField[fIndex] = false;
    else if (type == FG_ResourceType::HudlessColor)
        _noHudless[fIndex] = false;

    fResource->validity = (fResource->validity != FG_ResourceValidity::ValidNow || willFlip)
                              ? FG_ResourceValidity::UntilPresent
                              : FG_ResourceValidity::ValidNow;

    xefg_swapchain_d3d12_resource_data_t resourceParam = GetResourceData(type);

    // SDK version 2.1.1 fixes those issues
    if (version < feature_version { 1, 2, 2 })
    {
        // HACK: XeFG docs lie and cmd list is technically required as it checks for it
        // But it doesn't seem to use it when the validity is UNTIL_NEXT_PRESENT
        // https://github.com/intel/xess/issues/45
        if (fResource->cmdList == nullptr && resourceParam.validity == XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT)
            fResource->cmdList = (ID3D12GraphicsCommandList*) 1;

        // HACK: XeFG seems to crash if the resource is in COPY_SOURCE state
        // even though the docs say it's the preferred state
        // https://github.com/intel/xess/issues/47
        if (inputResource->state == D3D12_RESOURCE_STATE_COPY_SOURCE)
        {
            ResourceBarrier(inputResource->cmdList, inputResource->resource, inputResource->state,
                            D3D12_RESOURCE_STATE_COPY_DEST);
            resourceParam.incomingState = D3D12_RESOURCE_STATE_COPY_DEST;
        }
    }

    auto frameId = static_cast<uint32_t>(_frameCount);
    auto result = XeFGProxy::D3D12TagFrameResource()(_swapChainContext, fResource->cmdList, frameId, &resourceParam);

    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12TagFrameResource {} error: {} ({})", magic_enum::enum_name(type), magic_enum::enum_name(result),
                  (int32_t) result);

        State::Instance().FGchanged = true;
        UpdateTarget();
        Deactivate();

        return false;
    }

    // Potentially we don't need to restore but do it just to be safe
    if (inputResource->state == D3D12_RESOURCE_STATE_COPY_SOURCE)
    {
        ResourceBarrier(inputResource->cmdList, inputResource->resource, D3D12_RESOURCE_STATE_COPY_DEST,
                        inputResource->state);
    }

    SetResourceReady(type, fIndex);

    LOG_TRACE("_frameResources[{}][{}]: {:X}", fIndex, magic_enum::enum_name(type), (size_t) fResource->GetResource());

    return true;
}

void XeFG_Dx12::SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue) { _gameCommandQueue = queue; }

bool XeFG_Dx12::ReleaseSwapchain(HWND hwnd)
{
    if (hwnd != _hwnd || _hwnd == NULL)
        return false;

    LOG_DEBUG("");

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Waiting Mutex 1, current: {}", Mutex.getOwner());
        Mutex.lock(1);
        LOG_TRACE("Accuired Mutex: {}", Mutex.getOwner());
    }

    MenuOverlayDx::CleanupRenderTarget(true, NULL);

    if (_fgContext != nullptr)
        DestroyFGContext();

    if (State::Instance().isShuttingDown && _swapChainContext != nullptr)
        DestroySwapchainContext();

    // ReleaseObjects();

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Releasing Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    }

    return true;
}
