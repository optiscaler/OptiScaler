#include "pch.h"
#include "DLSSG_Dx12.h"
#include <hudfix/Hudfix_Dx12.h>
#include <menu/menu_overlay_dx.h>
#include <resource_tracking/ResTrack_dx12.h>

#include <nvapi/fakenvapi.h>

#include <magic_enum.hpp>
#include <DirectXMath.h>

using namespace DirectX;

// --- SL Initialization & Teardown ---

bool DLSSG_Dx12::InitStreamline(ID3D12Device* device)
{
    if (_slInitialized)
        return true;

    if (!SLProxy::InitSL())
    {
        LOG_ERROR("Failed to load Streamline DLLs");
        return false;
    }

    // Configure SL Preferences
    auto dllPath = Util::DllPath();
    std::filesystem::path slPluginPath = dllPath.parent_path() / L"sl";
    static std::wstring slPluginPathStr = slPluginPath.wstring();
    static const wchar_t* pluginPaths[] = { slPluginPathStr.c_str() };

    sl::Feature features[] = { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };

    sl::Preferences prefs {};
    prefs.showConsole = false;
    prefs.pathsToPlugins = pluginPaths;
    prefs.numPathsToPlugins = 1;
    prefs.pathToLogsAndData = nullptr;
    prefs.featuresToLoad = features;
    prefs.numFeaturesToLoad = _countof(features);
    prefs.renderAPI = sl::RenderAPI::eD3D12;
    prefs.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eBypassOSVersionCheck |
                   sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    prefs.engine = sl::EngineType::eCustom;
    prefs.engineVersion = "1.0.0";

    // applicationId is required for NGX initialization which DLSS-G depends on.
    // Use the game's app ID if already known, otherwise use a generic DLSS-G capable app ID.
    auto gameAppId = State::Instance().NVNGX_ApplicationId;
    prefs.applicationId = (gameAppId != 0 && gameAppId != 1337) ? (uint32_t) gameAppId : 231;
    LOG_INFO("Using SL applicationId: {} (game: {})", prefs.applicationId, gameAppId);

    prefs.logLevel = sl::LogLevel::eVerbose;
    prefs.logMessageCallback = [](sl::LogType type, const char* msg) {
        if (msg == nullptr)
            return;
        switch (type)
        {
        case sl::LogType::eInfo:
            LOG_INFO("[SL] {}", msg);
            break;
        case sl::LogType::eWarn:
            LOG_WARN("[SL] {}", msg);
            break;
        case sl::LogType::eError:
        case sl::LogType::eCount:
            LOG_ERROR("[SL] {}", msg);
            break;
        }
    };

    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        // Do NOT use DisableChecks("") here - we need LoadLibrary hooks to remain active
        // so that hookDlssg() and hookCommon() can install on sl.dlss_g.dll and sl.common.dll
        // during slInit's internal plugin loading. These hooks patch the JSON config
        // (hws.required=false) and spoof systemCaps->hwsSupported=true.

        auto result = SLProxy::Init()(prefs, sl::kSDKVersion);

        if (result != sl::Result::eOk)
        {
            LOG_ERROR("slInit failed: {}", (int) result);
            return false;
        }
    }

    LOG_INFO("slInit succeeded");

    // Register device
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

        auto result = SLProxy::SetD3DDevice()(device);

        if (result != sl::Result::eOk)
        {
            LOG_ERROR("slSetD3DDevice failed: {}", (int) result);
            SLProxy::Shutdown()();
            return false;
        }
    }

    _deviceRegistered = true;
    LOG_INFO("slSetD3DDevice succeeded");

    // Diagnostic: check feature support and loaded status
    if (SLProxy::IsFeatureSupported() != nullptr)
    {
        sl::AdapterInfo adapterInfo {};
        auto supportResult = SLProxy::IsFeatureSupported()(sl::kFeatureDLSS_G, adapterInfo);
        LOG_INFO("slIsFeatureSupported(DLSS_G): {} (0=OK)", (int) supportResult);
    }

    if (SLProxy::IsFeatureLoaded() != nullptr)
    {
        bool loaded = false;
        auto loadedResult = SLProxy::IsFeatureLoaded()(sl::kFeatureDLSS_G, loaded);
        LOG_INFO("slIsFeatureLoaded(DLSS_G): result={}, loaded={}", (int) loadedResult, loaded);
    }

    if (SLProxy::GetFeatureRequirements() != nullptr)
    {
        sl::FeatureRequirements reqs {};
        auto reqResult = SLProxy::GetFeatureRequirements()(sl::kFeatureDLSS_G, reqs);
        LOG_INFO("slGetFeatureRequirements(DLSS_G): result={}, flags={:#x}", (int) reqResult, (uint32_t) reqs.flags);
    }

    // Resolve DLSS-G feature functions (requires device to be set)
    if (!SLProxy::ResolveDLSSGFunctions())
    {
        LOG_ERROR("Failed to resolve DLSS-G feature functions");
        SLProxy::Shutdown()();
        return false;
    }

    _dlssgFeatureReady = true;

    // Resolve PCL feature functions (required for DLSS-G frame lifecycle)
    if (!SLProxy::ResolvePCLFunctions())
    {
        LOG_WARN("PCL feature functions not available - DLSS-G requires PCL markers!");
    }

    // Resolve Reflex feature functions (required for DLSS-G)
    if (!SLProxy::ResolveReflexFunctions())
    {
        LOG_WARN("Reflex feature functions not available - DLSS-G requires Reflex!");
    }

    // Initialize Reflex - DLSS-G REQUIRES Reflex to be active
    // The programming guide states: "It is REQUIRED for sl.reflex to be integrated"
    if (SLProxy::ReflexSetOptions() != nullptr)
    {
        sl::ReflexOptions reflexOptions {};
        reflexOptions.mode = sl::ReflexMode::eLowLatency;
        reflexOptions.useMarkersToOptimize = false;
        reflexOptions.virtualKey = 0;
        reflexOptions.idThread = 0;
        reflexOptions.frameLimitUs = 0;

        auto reflexResult = SLProxy::ReflexSetOptions()(reflexOptions);
        LOG_INFO("slReflexSetOptions (eLowLatency) result: {} (0=OK)", (int) reflexResult);
    }
    else
    {
        LOG_ERROR("Cannot initialize Reflex - slReflexSetOptions not available!");
    }

    // Query MFG capabilities
    sl::DLSSGState dlssgState {};
    sl::ViewportHandle viewport(0);
    auto stateResult = SLProxy::DLSSGGetState()(viewport, dlssgState, nullptr);
    if (stateResult == sl::Result::eOk)
    {
        _numFramesToGenerateMax = dlssgState.numFramesToGenerateMax;
        if (_numFramesToGenerateMax == 0)
            _numFramesToGenerateMax = 1;

        State::Instance().DLSSGMaxFramesToGenerate = _numFramesToGenerateMax;
        LOG_INFO("DLSS-G max frames to generate: {}", _numFramesToGenerateMax);
    }
    else
    {
        LOG_WARN("slDLSSGGetState failed during init: {}, defaulting max to 1", (int) stateResult);
        _numFramesToGenerateMax = 1;
    }

    // Clamp requested frame count
    if (_numFramesToGenerate > _numFramesToGenerateMax)
        _numFramesToGenerate = _numFramesToGenerateMax;

    // Set up fakenvapi for Reflex (LatencyFlex mode provides Reflex markers)
    auto fnaResult = fakenvapi::setModeAndContext(nullptr, Mode::LatencyFlex);
    LOG_DEBUG("fakenvapi::setModeAndContext (LatencyFlex): {}", fnaResult);

    _slInitialized = true;
    LOG_INFO("Streamline initialized successfully for DLSS-G output");
    return true;
}

void DLSSG_Dx12::ShutdownStreamline()
{
    if (!_slInitialized)
        return;

    if (_dlssgFeatureReady)
    {
        // Turn off DLSS-G
        sl::DLSSGOptions options {};
        options.mode = sl::DLSSGMode::eOff;
        sl::ViewportHandle viewport(0);
        SLProxy::DLSSGSetOptions()(viewport, options);

        if (SLProxy::FreeResources() != nullptr)
        {
            sl::ViewportHandle vp(0);
            SLProxy::FreeResources()(sl::kFeatureDLSS_G, vp);
        }
    }

    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        State::DisableChecks(0x534C4F50, "");

        SLProxy::Shutdown()();

        State::EnableChecks(0x534C4F50);
    }

    _slInitialized = false;
    _deviceRegistered = false;
    _dlssgFeatureReady = false;
    _currentFrameToken = nullptr;

    LOG_INFO("Streamline shut down");
}

// --- SL Resource Helper ---

sl::Resource DLSSG_Dx12::MakeSLResource(ID3D12Resource* d3dResource, D3D12_RESOURCE_STATES state)
{
    auto desc = d3dResource->GetDesc();
    sl::Resource slRes(sl::ResourceType::eTex2d, d3dResource, (uint32_t) state);
    slRes.width = (uint32_t) desc.Width;
    slRes.height = (uint32_t) desc.Height;
    slRes.nativeFormat = (uint32_t) desc.Format;
    slRes.mipLevels = desc.MipLevels;
    slRes.arrayLayers = desc.DepthOrArraySize;
    return slRes;
}

// --- IFGFeature Implementation ---

const char* DLSSG_Dx12::Name()
{
    static std::string nameBuffer;

    if (nameBuffer.empty())
    {
        if (_numFramesToGenerateMax <= 1)
        {
            nameBuffer = "DLSS-G";
        }
        else
        {
            auto count = _numFramesToGenerate + 1;
            nameBuffer = "DLSS-G " + std::to_string(count) + "x";
        }
    }

    return nameBuffer.c_str();
}

feature_version DLSSG_Dx12::Version()
{
    if (SLProxy::IsReady())
        return SLProxy::Version();

    return { 0, 0, 0 };
}

HWND DLSSG_Dx12::Hwnd() { return _hwnd; }

// --- Swapchain Creation ---
// SL interposer hooks DXGI internally. We init SL, then let the game create the
// swapchain normally. SL will intercept and wrap it transparently.

bool DLSSG_Dx12::CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                                  IDXGISwapChain** swapChain)
{
    if (State::Instance().currentFGSwapchain != nullptr && _hwnd == desc->OutputWindow)
    {
        LOG_WARN("FG swapchain already created for the same output window!");
        auto result = State::Instance().currentFGSwapchain->ResizeBuffers(desc->BufferCount, desc->BufferDesc.Width,
                                                                          desc->BufferDesc.Height,
                                                                          desc->BufferDesc.Format, desc->Flags) == S_OK;
        *swapChain = State::Instance().currentFGSwapchain;
        return result;
    }

    if (State::Instance().currentD3D12Device == nullptr)
        return false;

    // Initialize Streamline if not done yet
    if (!_slInitialized && !InitStreamline(State::Instance().currentD3D12Device))
    {
        LOG_ERROR("Failed to initialize Streamline for swapchain creation");
        return false;
    }

    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* realQueue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &realQueue))
        realQueue = cmdQueue;

    // Upgrade factory through SL interposer so it hooks the swapchain
    if (SLProxy::UpgradeInterface() != nullptr)
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        ScopedSkipSpoofing skipSpoofing {};
        ScopedSkipParentWrapping skipWrapping {};
        State::DisableChecks(0x534C4F50, "");

        auto result = SLProxy::UpgradeInterface()((void**)&realFactory);

        State::EnableChecks(0x534C4F50);

        if (result != sl::Result::eOk)
        {
            LOG_WARN("slUpgradeInterface on factory failed: {}, continuing anyway", (int) result);
        }
    }

    // Create the swapchain - SL will intercept this via its DXGI hooks
    IDXGIFactory2* factory2 = nullptr;
    if (realFactory->QueryInterface(IID_PPV_ARGS(&factory2)) != S_OK)
    {
        LOG_ERROR("Failed to get IDXGIFactory2");
        return false;
    }

    HWND hwnd = desc->OutputWindow;
    DXGI_SWAP_CHAIN_DESC1 scDesc {};
    scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
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

    scDesc.Stereo = false;
    scDesc.SwapEffect = desc->SwapEffect;
    scDesc.Width = desc->BufferDesc.Width;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc {};
    fsDesc.RefreshRate = desc->BufferDesc.RefreshRate;
    fsDesc.Scaling = desc->BufferDesc.Scaling;
    fsDesc.ScanlineOrdering = desc->BufferDesc.ScanlineOrdering;
    fsDesc.Windowed = desc->Windowed;

    IDXGISwapChain1* swapChain1 = nullptr;
    HRESULT hr;
    {
        ScopedSkipSpoofing skipSpoofing {};
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        ScopedSkipParentWrapping skipWrapping {};

        hr = factory2->CreateSwapChainForHwnd(realQueue, hwnd, &scDesc, &fsDesc, nullptr, &swapChain1);
    }

    factory2->Release();

    if (FAILED(hr) || swapChain1 == nullptr)
    {
        LOG_ERROR("CreateSwapChainForHwnd failed: {:X}", (UINT) hr);
        return false;
    }

    *swapChain = swapChain1;
    _gameCommandQueue = realQueue;
    _swapChain = *swapChain;
    _hwnd = hwnd;

    LOG_INFO("DLSS-G swapchain created via SL interposer");
    return true;
}

bool DLSSG_Dx12::CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                   DXGI_SWAP_CHAIN_DESC1* desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                   IDXGISwapChain1** swapChain)
{
    if (State::Instance().currentFGSwapchain != nullptr && _hwnd == hwnd)
    {
        LOG_WARN("FG swapchain already created for the same output window!");
        auto result = State::Instance().currentFGSwapchain->ResizeBuffers(desc->BufferCount, desc->Width, desc->Height,
                                                                          desc->Format, desc->Flags) == S_OK;
        *swapChain = (IDXGISwapChain1*) State::Instance().currentFGSwapchain;
        return result;
    }

    if (State::Instance().currentD3D12Device == nullptr)
        return false;

    // Initialize Streamline if not done yet
    if (!_slInitialized && !InitStreamline(State::Instance().currentD3D12Device))
    {
        LOG_ERROR("Failed to initialize Streamline for swapchain creation");
        return false;
    }

    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* realQueue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &realQueue))
        realQueue = cmdQueue;

    // Upgrade factory through SL interposer
    if (SLProxy::UpgradeInterface() != nullptr)
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        ScopedSkipSpoofing skipSpoofing {};
        ScopedSkipParentWrapping skipWrapping {};
        State::DisableChecks(0x534C4F50, "");

        auto result = SLProxy::UpgradeInterface()((void**)&realFactory);

        State::EnableChecks(0x534C4F50);

        if (result != sl::Result::eOk)
        {
            LOG_WARN("slUpgradeInterface on factory failed: {}, continuing anyway", (int) result);
        }
    }

    IDXGIFactory2* factory2 = nullptr;
    if (realFactory->QueryInterface(IID_PPV_ARGS(&factory2)) != S_OK)
    {
        LOG_ERROR("Failed to get IDXGIFactory2");
        return false;
    }

    HRESULT hr;
    {
        ScopedSkipSpoofing skipSpoofing {};
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        ScopedSkipParentWrapping skipWrapping {};

        hr = factory2->CreateSwapChainForHwnd(realQueue, hwnd, desc, pFullscreenDesc, nullptr, swapChain);
    }

    factory2->Release();

    if (FAILED(hr) || *swapChain == nullptr)
    {
        LOG_ERROR("CreateSwapChainForHwnd failed: {:X}", (UINT) hr);
        return false;
    }

    _gameCommandQueue = realQueue;
    _swapChain = *swapChain;
    _hwnd = hwnd;

    LOG_INFO("DLSS-G swapchain1 created via SL interposer");
    return true;
}

// --- Context Management ---

void DLSSG_Dx12::CreateContext(ID3D12Device* device, FG_Constants& fgConstants)
{
    LOG_DEBUG("");

    _device = device;

    if (!_objectsCreated)
    {
        CreateObjects(device);
        _objectsCreated = true;
    }

    if (!_slInitialized)
    {
        InitStreamline(device);
    }

    if (_slInitialized && _dlssgFeatureReady)
    {
        // Do NOT set DLSS-G mode=On here. The FG input (upscaler) may not be providing
        // data yet (e.g. during loading screens). If we set mode=On now, SL's present
        // hooks will expect slEvaluateFeature calls that never come, causing a deadlock.
        // Instead, keep mode=eOff and let Activate() set mode=On when EvaluateState()
        // confirms the upscaler is actually running.
        LOG_INFO("DLSS-G context created, feature ready (mode=Off until Activate)");

        _lastDispatchedFrame = 0;
    }

    if (_isActive)
    {
        LOG_INFO("FG context recreated while active, pausing");
        State::Instance().FGchanged = true;
        UpdateTarget();
        Deactivate();
    }
}

void DLSSG_Dx12::Activate()
{
    LOG_DEBUG("");

    if (!_slInitialized || !_dlssgFeatureReady || !_objectsCreated)
        return;

    if (!_isActive)
    {
        sl::DLSSGOptions options {};
        options.mode = sl::DLSSGMode::eOn;
        options.numFramesToGenerate = _numFramesToGenerate;
        options.flags = sl::DLSSGFlags::eEnableFullscreenMenuDetection;

        sl::ViewportHandle viewport(0);
        auto result = SLProxy::DLSSGSetOptions()(viewport, options);

        if (result == sl::Result::eOk)
        {
            _isActive = true;
            _lastDispatchedFrame = 0;
            LOG_INFO("DLSS-G activated, numFramesToGenerate={}", _numFramesToGenerate);
        }
        else
        {
            LOG_ERROR("Failed to activate DLSS-G: {}", (int) result);
        }
    }
}

void DLSSG_Dx12::Deactivate()
{
    LOG_DEBUG("");

    if (_isActive)
    {
        auto fIndex = GetIndex();
        if (_uiCommandListResetted[fIndex])
        {
            auto closeResult = _uiCommandList[fIndex]->Close();
            if (closeResult == S_OK)
                _gameCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**) &_uiCommandList[fIndex]);
            else
                LOG_ERROR("_uiCommandList[{}]->Close() error: {:X}", fIndex, (UINT) closeResult);

            _uiCommandListResetted[fIndex] = false;
        }

        if (_slInitialized && _dlssgFeatureReady)
        {
            sl::DLSSGOptions options {};
            options.mode = sl::DLSSGMode::eOff;

            sl::ViewportHandle viewport(0);
            auto result = SLProxy::DLSSGSetOptions()(viewport, options);
            if (result == sl::Result::eOk)
                _isActive = false;
            else
                LOG_ERROR("Failed to deactivate DLSS-G: {}", (int) result);
        }
        else
        {
            _isActive = false;
        }

        _waitingNewFrameData = false;
        LOG_INFO("DLSS-G deactivated");
    }
}

void DLSSG_Dx12::DestroyFGContext()
{
    Deactivate();
    ReleaseObjects();
}

bool DLSSG_Dx12::Shutdown()
{
    MenuOverlayDx::CleanupRenderTarget(true, NULL);

    DestroyFGContext();
    ShutdownStreamline();

    return true;
}

// --- State Evaluation ---

void DLSSG_Dx12::EvaluateState(ID3D12Device* device, FG_Constants& fgConstants)
{
    LOG_FUNC();

    auto& state = State::Instance();

    if (!SLProxy::IsReady() || state.currentFGSwapchain == nullptr)
        return;

    if (state.isShuttingDown)
    {
        DestroyFGContext();
        return;
    }

    // If FG Enabled from menu
    if (Config::Instance()->FGEnabled.value_or_default())
    {
        if (!_slInitialized)
        {
            CreateContext(device, fgConstants);
            UpdateTarget();
        }
        else if (state.FGchanged)
        {
            LOG_DEBUG("FGChanged");
            Deactivate();
            UpdateTarget();

            if (state.SCchanged)
                DestroyFGContext();
        }

        // Create command lists/allocators if not yet done (InitStreamline may have
        // been called during CreateSwapchain, but CreateObjects deferred until now)
        if (_slInitialized && _dlssgFeatureReady && !_objectsCreated)
        {
            _device = device;
            CreateObjects(device);
            _objectsCreated = true;
            _lastDispatchedFrame = 0;
            LOG_INFO("DLSS-G objects created (deferred from CreateSwapchain)");
        }

        if (_slInitialized && _dlssgFeatureReady && _objectsCreated &&
            State::Instance().activeFgInput == FGInput::Upscaler && !IsPaused() && !IsActive())
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
        UpdateTarget();

        if (Mutex.getOwner() == 2)
            Mutex.unlockThis(2);
    }

    state.SCchanged = false;
}

// --- Resource Tagging for SL ---

void DLSSG_Dx12::SetSLConstants(int fIndex)
{
    sl::Constants slConstants {};

    // Depth inversion flag
    slConstants.depthInverted =
        IsInvertedDepth() ? sl::Boolean::eTrue : sl::Boolean::eFalse;

    // Motion vector flags
    slConstants.motionVectorsJittered =
        IsJitteredMVs() ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    slConstants.motionVectorsDilated =
        IsLowResMV() ? sl::Boolean::eFalse : sl::Boolean::eTrue;
    slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
    slConstants.motionVectors3D = sl::Boolean::eFalse;

    // Camera values
    slConstants.cameraNear = _cameraNear[fIndex];
    slConstants.cameraFar = _cameraFar[fIndex];
    slConstants.cameraFOV = _cameraVFov[fIndex];
    slConstants.cameraAspectRatio = _cameraAspectRatio[fIndex];

    // Jitter
    slConstants.jitterOffset = sl::float2(_jitterX[fIndex], _jitterY[fIndex]);

    // MV scale - reverse the multiplication done in Streamline_Inputs_Dx12
    auto& state = State::Instance();
    float scWidth = (float) state.currentSwapchainDesc.BufferDesc.Width;
    float scHeight = (float) state.currentSwapchainDesc.BufferDesc.Height;

    if (scWidth > 0.0f && scHeight > 0.0f)
        slConstants.mvecScale = sl::float2(_mvScaleX[fIndex] / scWidth, _mvScaleY[fIndex] / scHeight);
    else
        slConstants.mvecScale = sl::float2(_mvScaleX[fIndex], _mvScaleY[fIndex]);

    // Camera position and orientation
    slConstants.cameraPos = sl::float3(
        _cameraPosition[fIndex][0], _cameraPosition[fIndex][1], _cameraPosition[fIndex][2]);
    slConstants.cameraUp = sl::float3(
        _cameraUp[fIndex][0], _cameraUp[fIndex][1], _cameraUp[fIndex][2]);
    slConstants.cameraRight = sl::float3(
        _cameraRight[fIndex][0], _cameraRight[fIndex][1], _cameraRight[fIndex][2]);
    slConstants.cameraFwd = sl::float3(
        _cameraForward[fIndex][0], _cameraForward[fIndex][1], _cameraForward[fIndex][2]);

    // Reset flag
    slConstants.reset = _reset[fIndex] ? sl::Boolean::eTrue : sl::Boolean::eFalse;

    // Build projection matrices from camera data.
    // The Upscaler input path only provides near/far/fov/aspect (no camera position
    // or orientation vectors), so we always build the projection matrix from those.
    // When camera vectors are available, we also build view-dependent matrices.
    {
        float nearVal = _cameraNear[fIndex];
        float farVal = _cameraFar[fIndex];
        float vfov = _cameraVFov[fIndex];
        float aspect = _cameraAspectRatio[fIndex];

        if (nearVal > 0.f && !XMScalarNearEqual(vfov, 0.0f, 0.00001f) &&
            !XMScalarNearEqual(aspect, 0.0f, 0.00001f))
        {
            // Ensure far > near
            if (farVal <= 0.f || XMScalarNearEqual(nearVal, farVal, 0.00001f))
                farVal = nearVal + 10000.0f;

            // Build perspective projection (this IS viewToClip when view = identity)
            XMMATRIX proj;
            if (IsInvertedDepth())
                proj = XMMatrixPerspectiveFovLH(vfov, aspect, farVal, nearVal);  // reversed-Z
            else
                proj = XMMatrixPerspectiveFovLH(vfov, aspect, nearVal, farVal);

            XMMATRIX viewToClip = proj;
            XMMATRIX clipToView = XMMatrixInverse(nullptr, viewToClip);

            memcpy(&slConstants.cameraViewToClip, viewToClip.r, sizeof(sl::float4x4));
            memcpy(&slConstants.clipToCameraView, clipToView.r, sizeof(sl::float4x4));

            // clipToPrevClip and prevClipToClip: use identity (SL can derive from MVs)
            // If we had previous-frame camera data we could compute these, but the
            // Upscaler input path doesn't track previous-frame projection changes.
            XMMATRIX identity = XMMatrixIdentity();
            memcpy(&slConstants.clipToPrevClip, identity.r, sizeof(sl::float4x4));
            memcpy(&slConstants.prevClipToClip, identity.r, sizeof(sl::float4x4));
        }

        // If we have camera position, set pinhole offset for SL
        bool hasCamPos = (_cameraPosition[fIndex][0] != 0.0f || _cameraPosition[fIndex][1] != 0.0f ||
                          _cameraPosition[fIndex][2] != 0.0f);
        if (hasCamPos)
        {
            // cameraPinholeOffset = 0 (no stereo offset)
            slConstants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
        }
        else
        {
            // Even without camera data, set pinhole to 0 to silence the SL warning
            slConstants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
        }
    }

    // Set constants via SL
    sl::ViewportHandle viewport(0);
    auto result = SLProxy::SetConstants()(slConstants, *_currentFrameToken, viewport);
    if (result != sl::Result::eOk)
    {
        LOG_ERROR("slSetConstants failed: {}", (int) result);
    }
}

bool DLSSG_Dx12::TagResources(int fIndex, uint64_t willDispatchFrame)
{
    if (_currentFrameToken == nullptr)
        return false;

    sl::ViewportHandle viewport(0);
    std::vector<sl::ResourceTag> tags;

    // Persistent storage for sl::Resource objects — must outlive the slSetTagForFrame call.
    // ResourceTag stores a raw Resource* pointer, so locals going out of scope would dangle.
    std::vector<sl::Resource> slResources;
    std::vector<sl::Extent> extents;
    slResources.reserve(5);
    extents.reserve(5);

    // Depth
    if (_frameResources[fIndex].contains(FG_ResourceType::Depth))
    {
        auto& fRes = _frameResources[fIndex][FG_ResourceType::Depth];
        slResources.push_back(MakeSLResource(fRes.GetResource(), fRes.state));
        extents.push_back({ fRes.top, fRes.left, (uint32_t) fRes.width, fRes.height });
        tags.emplace_back(&slResources.back(), sl::kBufferTypeDepth,
                          (fRes.validity == FG_ResourceValidity::ValidNow) ? sl::eOnlyValidNow : sl::eValidUntilPresent,
                          &extents.back());
    }

    // Motion Vectors
    if (_frameResources[fIndex].contains(FG_ResourceType::Velocity))
    {
        auto& fRes = _frameResources[fIndex][FG_ResourceType::Velocity];
        slResources.push_back(MakeSLResource(fRes.GetResource(), fRes.state));
        extents.push_back({ fRes.top, fRes.left, (uint32_t) fRes.width, fRes.height });
        tags.emplace_back(&slResources.back(), sl::kBufferTypeMotionVectors,
                          (fRes.validity == FG_ResourceValidity::ValidNow) ? sl::eOnlyValidNow : sl::eValidUntilPresent,
                          &extents.back());
    }

    // HUDLess Color
    if (_frameResources[fIndex].contains(FG_ResourceType::HudlessColor))
    {
        auto& fRes = _frameResources[fIndex][FG_ResourceType::HudlessColor];
        slResources.push_back(MakeSLResource(fRes.GetResource(), fRes.state));
        extents.push_back({ fRes.top, fRes.left, (uint32_t) fRes.width, fRes.height });
        tags.emplace_back(&slResources.back(), sl::kBufferTypeHUDLessColor,
                          (fRes.validity == FG_ResourceValidity::ValidNow) ? sl::eOnlyValidNow : sl::eValidUntilPresent,
                          &extents.back());
    }

    // UI Color
    if (_frameResources[fIndex].contains(FG_ResourceType::UIColor))
    {
        auto& fRes = _frameResources[fIndex][FG_ResourceType::UIColor];
        slResources.push_back(MakeSLResource(fRes.GetResource(), fRes.state));
        extents.push_back({ fRes.top, fRes.left, (uint32_t) fRes.width, fRes.height });
        tags.emplace_back(&slResources.back(), sl::kBufferTypeUIColorAndAlpha,
                          (fRes.validity == FG_ResourceValidity::ValidNow) ? sl::eOnlyValidNow : sl::eValidUntilPresent,
                          &extents.back());
    }

    // Bidirectional Distortion Field
    if (_frameResources[fIndex].contains(FG_ResourceType::Distortion))
    {
        auto& fRes = _frameResources[fIndex][FG_ResourceType::Distortion];
        slResources.push_back(MakeSLResource(fRes.GetResource(), fRes.state));
        extents.push_back({ fRes.top, fRes.left, (uint32_t) fRes.width, fRes.height });
        tags.emplace_back(&slResources.back(), sl::kBufferTypeBidirectionalDistortionField,
                          (fRes.validity == FG_ResourceValidity::ValidNow) ? sl::eOnlyValidNow : sl::eValidUntilPresent,
                          &extents.back());
    }

    if (tags.empty())
    {
        LOG_WARN("No resources to tag");
        return false;
    }

    // Use the tag command list to submit tags
    auto& tagAlloc = _tagCommandAllocator[fIndex];
    auto& tagCmdList = _tagCommandList[fIndex];

    if (tagAlloc != nullptr && tagCmdList != nullptr)
    {
        tagAlloc->Reset();
        tagCmdList->Reset(tagAlloc, nullptr);

        auto result = SLProxy::SetTagForFrame()(*_currentFrameToken, viewport, tags.data(), (uint32_t) tags.size(),
                                                 (sl::CommandBuffer*) tagCmdList);

        tagCmdList->Close();
        _gameCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**) &tagCmdList);

        if (result != sl::Result::eOk)
        {
            LOG_ERROR("slSetTagForFrame failed: {}", (int) result);
            return false;
        }
    }
    else
    {
        // Fallback: tag without command list (resources must be eValidUntilPresent)
        auto result = SLProxy::SetTagForFrame()(*_currentFrameToken, viewport, tags.data(), (uint32_t) tags.size(),
                                                 nullptr);
        if (result != sl::Result::eOk)
        {
            LOG_ERROR("slSetTagForFrame (no cmdList) failed: {}", (int) result);
            return false;
        }
    }

    LOG_DEBUG("Tagged {} resources for frame {} (Depth:{}, MV:{}, Hudless:{}, UI:{}, Dist:{})",
              tags.size(), (uint32_t) willDispatchFrame,
              _frameResources[fIndex].contains(FG_ResourceType::Depth) ? 1 : 0,
              _frameResources[fIndex].contains(FG_ResourceType::Velocity) ? 1 : 0,
              _frameResources[fIndex].contains(FG_ResourceType::HudlessColor) ? 1 : 0,
              _frameResources[fIndex].contains(FG_ResourceType::UIColor) ? 1 : 0,
              _frameResources[fIndex].contains(FG_ResourceType::Distortion) ? 1 : 0);
    return true;
}

// --- Frame Generation Dispatch ---

bool DLSSG_Dx12::Dispatch()
{
    LOG_FUNC();

    // Check preconditions BEFORE GetDispatchIndex, which has the side effect
    // of advancing _lastDispatchedFrame. Without these guards, the frame counter
    // gets out of sync when DLSS-G isn't ready yet.
    if (!IsActive() || IsPaused())
        return false;

    if (!_slInitialized || !_dlssgFeatureReady || !_objectsCreated)
        return false;

    UINT64 willDispatchFrame = 0;
    auto fIndex = GetDispatchIndex(willDispatchFrame);
    if (fIndex < 0)
        return false;

    LOG_DEBUG("_frameCount: {}, willDispatchFrame: {}, fIndex: {}", _frameCount, willDispatchFrame, fIndex);

    // Check required resources
    if (!_resourceReady[fIndex].contains(FG_ResourceType::Depth) ||
        !_resourceReady[fIndex].at(FG_ResourceType::Depth) ||
        !_resourceReady[fIndex].contains(FG_ResourceType::Velocity) ||
        !_resourceReady[fIndex].at(FG_ResourceType::Velocity))
    {
        LOG_WARN("Depth or Velocity is not ready, skipping");
        return false;
    }

    // Copy late-sent resources
    if (!_noHudless[fIndex])
    {
        auto res = &_frameResources[fIndex][FG_ResourceType::HudlessColor];
        if (res->validity != FG_ResourceValidity::ValidNow)
        {
            res->validity = FG_ResourceValidity::UntilPresentFromDispatch;
            res->frameIndex = fIndex;
            SetResource(res);
        }
    }

    if (!_noUi[fIndex])
    {
        auto res = &_frameResources[fIndex][FG_ResourceType::UIColor];
        if (res->validity != FG_ResourceValidity::ValidNow)
        {
            res->validity = FG_ResourceValidity::UntilPresentFromDispatch;
            res->frameIndex = fIndex;
            SetResource(res);
        }
    }

    if (!_noDistortionField[fIndex])
    {
        auto res = &_frameResources[fIndex][FG_ResourceType::Distortion];
        if (res->validity != FG_ResourceValidity::ValidNow)
        {
            res->validity = FG_ResourceValidity::UntilPresentFromDispatch;
            res->frameIndex = fIndex;
            SetResource(res);
        }
    }

    // DLSS-G REQUIRES kBufferTypeHUDLessColor. If Hudfix didn't capture one,
    // use the current backbuffer as a fallback. At this point in the Present
    // chain the backbuffer contains the fully rendered frame (the best hudless
    // approximation we have). DLSS-G's eEnableFullscreenMenuDetection flag
    // provides its own HUD detection on top of this.
    if (!_frameResources[fIndex].contains(FG_ResourceType::HudlessColor) ||
        _frameResources[fIndex][FG_ResourceType::HudlessColor].resource == nullptr)
    {
        if (_swapChain != nullptr)
        {
            IDXGISwapChain3* sc3 = nullptr;
            if (((IDXGISwapChain*) _swapChain)->QueryInterface(IID_PPV_ARGS(&sc3)) == S_OK)
            {
                UINT bbIndex = sc3->GetCurrentBackBufferIndex();
                ID3D12Resource* backBuffer = nullptr;
                if (sc3->GetBuffer(bbIndex, IID_PPV_ARGS(&backBuffer)) == S_OK && backBuffer != nullptr)
                {
                    auto desc = backBuffer->GetDesc();
                    auto& fRes = _frameResources[fIndex][FG_ResourceType::HudlessColor];
                    fRes.type = FG_ResourceType::HudlessColor;
                    fRes.resource = backBuffer;
                    fRes.state = D3D12_RESOURCE_STATE_PRESENT;
                    fRes.validity = FG_ResourceValidity::UntilPresent;
                    fRes.left = 0;
                    fRes.top = 0;
                    fRes.width = (LONG) desc.Width;
                    fRes.height = desc.Height;
                    fRes.cmdList = nullptr;
                    fRes.frameIndex = fIndex;
                    _noHudless[fIndex] = false;
                    backBuffer->Release();  // GetBuffer AddRef'd; resource stays alive via swapchain
                    LOG_DEBUG("Using backbuffer as HudlessColor fallback ({}x{})", desc.Width, desc.Height);
                }
                sc3->Release();
            }
        }
    }

    // Get new frame token — per the DLSS-G programming guide, this should be called
    // "at the start of each frame" (simulation start). In OptiScaler's architecture,
    // Dispatch() is called during FGPresent, which is the per-frame work before Present.
    sl::FrameToken* newToken = nullptr;
    auto tokenResult = SLProxy::GetNewFrameToken()(newToken, nullptr);
    _currentFrameToken = newToken;
    if (tokenResult != sl::Result::eOk || _currentFrameToken == nullptr)
    {
        LOG_ERROR("slGetNewFrameToken failed: {}", (int) tokenResult);
        return false;
    }

    // PCL marker: Simulation start
    // The DLSS-G programming guide requires these markers to be in sync with frame tokens.
    // "If you see 'common constants cannot be found for frame N', PCL markers are out of sync."
    if (SLProxy::PCLSetMarker() != nullptr)
    {
        SLProxy::PCLSetMarker()(sl::PCLMarker::eSimulationStart, *_currentFrameToken);
    }

    // Set SL constants for this frame — must be as early as possible after frame token
    SetSLConstants(fIndex);

    // PCL marker: Simulation end
    if (SLProxy::PCLSetMarker() != nullptr)
    {
        SLProxy::PCLSetMarker()(sl::PCLMarker::eSimulationEnd, *_currentFrameToken);
    }

    // PCL marker: Render submit start
    if (SLProxy::PCLSetMarker() != nullptr)
    {
        SLProxy::PCLSetMarker()(sl::PCLMarker::eRenderSubmitStart, *_currentFrameToken);
    }

    // Tag resources
    if (!TagResources(fIndex, willDispatchFrame))
    {
        LOG_ERROR("Failed to tag resources for DLSS-G");
        return false;
    }

    // PCL marker: Render submit end
    if (SLProxy::PCLSetMarker() != nullptr)
    {
        SLProxy::PCLSetMarker()(sl::PCLMarker::eRenderSubmitEnd, *_currentFrameToken);
    }

    // DLSS-G frame generation is triggered automatically by the SL interposer's
    // Present hook — there is no slEvaluateFeature callback for DLSS-G.
    // Our job in Dispatch is just to tag resources, set constants, and provide
    // PCL markers each frame. The actual interpolation happens during Present.
    //
    // CRITICAL: ePresentStart and ePresentEnd markers must be called around
    // the actual swapchain Present call in FG_Hooks.cpp, not here.

    _slFrameIndex++;
    LOG_DEBUG("DLSS-G dispatch OK, frame index: {}", _slFrameIndex);

    return true;
}

// --- Present ---

bool DLSSG_Dx12::Present()
{
    auto fIndex = GetIndexWillBeDispatched();
    LOG_DEBUG("fIndex: {}", fIndex);

    // Early out if DLSS-G is not fully initialized yet
    // (objects may not be created until the upscaler starts providing data)
    if (!_objectsCreated || !_slInitialized || !_dlssgFeatureReady)
    {
        _fgFramePresentId++;
        return false;
    }

    if (IsActive() && !IsPaused() && State::Instance().FGHudlessCompare)
    {
        auto hudless = GetResource(FG_ResourceType::HudlessColor, fIndex);
        if (hudless != nullptr && (hudless->validity == FG_ResourceValidity::UntilPresent ||
                                   hudless->validity == FG_ResourceValidity::JustTrackCmdlist ||
                                   hudless->validity == FG_ResourceValidity::UntilPresentFromDispatch))
        {
            if (_hudlessCompare.get() == nullptr)
            {
                _hudlessCompare = std::make_unique<HC_Dx12>("HudlessCompare", _device);
            }
            else if (_hudlessCompare->IsInit())
            {
                auto commandList = GetUICommandList(fIndex);
                _hudlessCompare->Dispatch((IDXGISwapChain3*) _swapChain, commandList, hudless->GetResource(),
                                          hudless->state);
            }
        }
    }

    // Execute UI command list if pending
    {
        if (_uiCommandListResetted[fIndex])
        {
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

// --- Resource Management ---

bool DLSSG_Dx12::SetResource(Dx12Resource* inputResource)
{
    if (inputResource == nullptr || inputResource->resource == nullptr || !IsActive() || IsPaused())
        return false;

    auto fIndex = inputResource->frameIndex;
    if (fIndex < 0)
        fIndex = GetIndex();

    auto& type = inputResource->type;

    std::unique_lock<std::shared_mutex> lock(_resourceMutex[fIndex]);

    if (type == FG_ResourceType::HudlessColor)
    {
        if (Config::Instance()->FGDisableHudless.value_or_default())
            return false;

        if (!_noHudless[fIndex] && (_frameResources[fIndex][type].validity == FG_ResourceValidity::ValidNow))
            return false;

        if (!_noHudless[fIndex] && Config::Instance()->FGOnlyAcceptFirstHudless.value_or_default() &&
            inputResource->validity != FG_ResourceValidity::UntilPresentFromDispatch)
            return false;
    }

    if (type == FG_ResourceType::UIColor)
    {
        if (Config::Instance()->FGDisableUI.value_or_default())
            return false;

        if (!_noUi[fIndex] && (_frameResources[fIndex][type].validity == FG_ResourceValidity::ValidNow))
            return false;
    }

    if (type == FG_ResourceType::Distortion)
    {
        if (!_noDistortionField[fIndex] && (_frameResources[fIndex][type].validity == FG_ResourceValidity::ValidNow))
            return false;
    }

    if ((type == FG_ResourceType::Depth || type == FG_ResourceType::Velocity) && _frameResources[fIndex].contains(type))
        return false;

    if (inputResource->cmdList == nullptr && inputResource->validity == FG_ResourceValidity::ValidNow)
    {
        LOG_ERROR("{}, validity == ValidNow but cmdList is nullptr!", magic_enum::enum_name(type));
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

    if (willFlip && _device != nullptr)
        FlipResource(fResource);

    // Copy resources that need it
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

    if ((type == FG_ResourceType::Depth || type == FG_ResourceType::Velocity) ||
        (fResource->validity != FG_ResourceValidity::UntilPresent &&
         fResource->validity != FG_ResourceValidity::JustTrackCmdlist))
    {
        fResource->validity = (fResource->validity != FG_ResourceValidity::ValidNow || willFlip)
                                  ? FG_ResourceValidity::UntilPresent
                                  : FG_ResourceValidity::ValidNow;

        SetResourceReady(type, fIndex);
    }

    LOG_TRACE("_frameResources[{}][{}]: {:X}", fIndex, magic_enum::enum_name(type), (size_t) fResource->GetResource());

    return true;
}

void DLSSG_Dx12::SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue) { _gameCommandQueue = queue; }

// --- Object Lifecycle ---

void DLSSG_Dx12::CreateObjects(ID3D12Device* InDevice)
{
    if (_uiCommandAllocator[0] != nullptr)
        return;

    LOG_DEBUG("");

    do
    {
        HRESULT result;
        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* cmdList = nullptr;

        for (size_t i = 0; i < BUFFER_COUNT; i++)
        {
            // UI command resources (same as XeFG/FSRFG pattern)
            result =
                InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_uiCommandAllocator[i]));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandAllocator _uiCommandAllocator[{}]: {:X}", i, (unsigned long) result);
                break;
            }
            _uiCommandAllocator[i]->SetName(std::format(L"DLSSG_uiCommandAllocator[{}]", i).c_str());
            if (CheckForRealObject(__FUNCTION__, _uiCommandAllocator[i], (IUnknown**) &allocator))
                _uiCommandAllocator[i] = allocator;

            result = InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _uiCommandAllocator[i], NULL,
                                                 IID_PPV_ARGS(&_uiCommandList[i]));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandList _uiCommandList[{}]: {:X}", i, (unsigned long) result);
                break;
            }
            _uiCommandList[i]->SetName(std::format(L"DLSSG_uiCommandList[{}]", i).c_str());
            if (CheckForRealObject(__FUNCTION__, _uiCommandList[i], (IUnknown**) &cmdList))
                _uiCommandList[i] = cmdList;

            result = _uiCommandList[i]->Close();
            if (result != S_OK)
            {
                LOG_ERROR("_uiCommandList[{}]->Close: {:X}", i, (unsigned long) result);
                break;
            }

            // Tag command resources for SL resource tagging
            result =
                InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_tagCommandAllocator[i]));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandAllocator _tagCommandAllocator[{}]: {:X}", i, (unsigned long) result);
                break;
            }
            _tagCommandAllocator[i]->SetName(std::format(L"DLSSG_tagCommandAllocator[{}]", i).c_str());
            if (CheckForRealObject(__FUNCTION__, _tagCommandAllocator[i], (IUnknown**) &allocator))
                _tagCommandAllocator[i] = allocator;

            result = InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _tagCommandAllocator[i], NULL,
                                                 IID_PPV_ARGS(&_tagCommandList[i]));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandList _tagCommandList[{}]: {:X}", i, (unsigned long) result);
                break;
            }
            _tagCommandList[i]->SetName(std::format(L"DLSSG_tagCommandList[{}]", i).c_str());
            if (CheckForRealObject(__FUNCTION__, _tagCommandList[i], (IUnknown**) &cmdList))
                _tagCommandList[i] = cmdList;

            result = _tagCommandList[i]->Close();
            if (result != S_OK)
            {
                LOG_ERROR("_tagCommandList[{}]->Close: {:X}", i, (unsigned long) result);
                break;
            }
        }
    } while (false);
}

void DLSSG_Dx12::ReleaseObjects()
{
    _objectsCreated = false;
    _mvFlip.reset();
    _depthFlip.reset();
    _hudlessCompare.reset();

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (_uiCommandList[i] != nullptr)
        {
            _uiCommandList[i]->Release();
            _uiCommandList[i] = nullptr;
        }
        if (_uiCommandAllocator[i] != nullptr)
        {
            _uiCommandAllocator[i]->Release();
            _uiCommandAllocator[i] = nullptr;
        }
        if (_tagCommandList[i] != nullptr)
        {
            _tagCommandList[i]->Release();
            _tagCommandList[i] = nullptr;
        }
        if (_tagCommandAllocator[i] != nullptr)
        {
            _tagCommandAllocator[i]->Release();
            _tagCommandAllocator[i] = nullptr;
        }
    }
}

void* DLSSG_Dx12::FrameGenerationContext() { return _slInitialized ? (void*) 1 : nullptr; }
void* DLSSG_Dx12::SwapchainContext() { return _slInitialized ? (void*) 1 : nullptr; }

bool DLSSG_Dx12::ReleaseSwapchain(HWND hwnd)
{
    if (hwnd != _hwnd || _hwnd == NULL)
        return false;

    LOG_DEBUG("");

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Waiting Mutex 1, current: {}", Mutex.getOwner());
        Mutex.lock(1);
        LOG_TRACE("Acquired Mutex: {}", Mutex.getOwner());
    }

    MenuOverlayDx::CleanupRenderTarget(true, NULL);

    DestroyFGContext();

    if (State::Instance().isShuttingDown)
        ShutdownStreamline();

    ReleaseObjects();

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Releasing Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    }

    return true;
}

DLSSG_Dx12::~DLSSG_Dx12() { Shutdown(); }

void DLSSG_Dx12::SetPCLPresentStart()
{
    if (_currentFrameToken != nullptr && SLProxy::PCLSetMarker() != nullptr)
    {
        auto result = SLProxy::PCLSetMarker()(sl::PCLMarker::ePresentStart, *_currentFrameToken);
        LOG_DEBUG("PCL ePresentStart result: {}", (int) result);
    }
}

void DLSSG_Dx12::SetPCLPresentEnd()
{
    if (_currentFrameToken != nullptr && SLProxy::PCLSetMarker() != nullptr)
    {
        auto result = SLProxy::PCLSetMarker()(sl::PCLMarker::ePresentEnd, *_currentFrameToken);
        LOG_DEBUG("PCL ePresentEnd result: {}", (int) result);
    }
}

bool DLSSG_Dx12::SetInterpolatedFrameCount(UINT interpolatedFrameCount)
{
    if (interpolatedFrameCount < 1)
        interpolatedFrameCount = 1;
    if (interpolatedFrameCount > _numFramesToGenerateMax)
        interpolatedFrameCount = _numFramesToGenerateMax;

    _numFramesToGenerate = interpolatedFrameCount;
    _framesToInterpolate = interpolatedFrameCount;

    if (_slInitialized && _dlssgFeatureReady && _isActive)
    {
        sl::DLSSGOptions options {};
        options.mode = sl::DLSSGMode::eOn;
        options.numFramesToGenerate = _numFramesToGenerate;
        options.flags = sl::DLSSGFlags::eEnableFullscreenMenuDetection;

        sl::ViewportHandle viewport(0);
        auto result = SLProxy::DLSSGSetOptions()(viewport, options);
        if (result != sl::Result::eOk)
        {
            LOG_ERROR("SetInterpolatedFrameCount: slDLSSGSetOptions failed: {}", (int) result);
            return false;
        }

        LOG_INFO("DLSS-G frame count updated to {}", _numFramesToGenerate);
    }

    return true;
}
