#include "pch.h"
#include "FlipMeterFG_Dx12.h"
#include "FlipMeterSwapChain.h"

#include <framegen/nvngx/Nvngx_FG.h>

#include <hudfix/Hudfix_Dx11.h>
#include <hudfix/Hudfix_Dx12.h>

#include <menu/menu_overlay_dx.h>
#include <misc/DeferredRelease.h>

#include <imgui/ImGuiNotify.hpp>
#include <magic_enum.hpp>

#include <DirectXMath.h>

using namespace DirectX;

// States Streamline hands the DLSSG resources over in, the replacements expect the same
constexpr auto BackbufferState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto InputState = D3D12_RESOURCE_STATE_COPY_DEST;

FlipMeterFG_Dx12::FlipMeterFG_Dx12() : IFGFeature_Dx12(), IFGFeature()
{
    _identity[0][0] = _identity[1][1] = _identity[2][2] = _identity[3][3] = 1.0f;
}

feature_version FlipMeterFG_Dx12::Version() { return Nvngx_FG::version(); }

void FlipMeterFG_Dx12::ReleaseObjects()
{
    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        DeferredRelease::Release(_uiCommandAllocator[i]);
        DeferredRelease::Release(_uiCommandList[i]);
        DeferredRelease::Release(_scCommandAllocator[i]);
        DeferredRelease::Release(_scCommandList[i]);

        _scCommandListResetted[i] = false;
        _scAllocatorFenceValues[i] = 0;

        _uiCommandListResetted[i] = false;
        _uiAllocatorFenceValues[i] = 0;
    }

    ReleaseFences();

    _renderUI.reset();
    _hudlessCompare.reset();
    _depthTransfer.reset();

    ReleaseResourceCopies();
}

void FlipMeterFG_Dx12::CreateObjects(ID3D12Device* InDevice)
{
    _device = InDevice;

    if (_uiCommandAllocator[0] != nullptr)
        return;

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        _scCommandListResetted[i] = false;
        _scAllocatorFenceValues[i] = 0;
        _uiCommandListResetted[i] = false;
        _uiAllocatorFenceValues[i] = 0;

        InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_uiCommandAllocator[i]));
        _uiCommandAllocator[i]->SetName(std::format(L"_uiCommandAllocator[{}]", i).c_str());

        InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _uiCommandAllocator[i], NULL,
                                    IID_PPV_ARGS(&_uiCommandList[i]));
        _uiCommandList[i]->SetName(std::format(L"_uiCommandList[{}]", i).c_str());
        _uiCommandList[i]->Close();

        InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_scCommandAllocator[i]));
        _scCommandAllocator[i]->SetName(std::format(L"_scCommandAllocator[{}]", i).c_str());

        InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _scCommandAllocator[i], NULL,
                                    IID_PPV_ARGS(&_scCommandList[i]));
        _scCommandList[i]->SetName(std::format(L"_scCommandList[{}]", i).c_str());
        _scCommandList[i]->Close();
    }

    if (_uiFence == nullptr)
    {
        InDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_uiFence));
        _uiFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }

    if (_scFence == nullptr)
    {
        InDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_scFence));
        _scFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }
}

bool FlipMeterFG_Dx12::CreateProxy(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd, UINT gameBufferCount,
                                   IDXGISwapChain** swapChain, const CreateRealFn& createReal)
{
    if (swapChain == nullptr)
        return false;

    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* queue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &queue))
        queue = cmdQueue;

    auto presentQueue = FlipMeterSwapChain::CreatePresentQueue(queue);
    if (presentQueue == nullptr)
        return false;

    IDXGISwapChain* real = nullptr;
    auto result = createReal(realFactory, presentQueue.Get(),
                             std::max(gameBufferCount, FlipMeterSwapChain::MinRealBuffers), &real);

    if (FAILED(result) || real == nullptr)
    {
        LOG_ERROR("Real swapchain creation failed: {:X}", (UINT) result);
        return false;
    }

    auto proxy = FlipMeterSwapChain::Create(real, queue, presentQueue.Get(), hwnd, gameBufferCount);

    State::Instance().amdFlipMeterSupported = proxy != nullptr;

    if (proxy == nullptr)
    {
        // The window gets a regular swapchain instead
        real->Release();

        LOG_WARN("Flip metering isn't supported, disabling FGOutput");

        ImGui::InsertNotification(
            { ImGuiToastType::Error, 20000, "Flip Meter output isn't supported\nby this GPU or its driver" });

        Config::Instance()->FGOutput.set_volatile_value(FGOutput::NoFG);
        State::Instance().activeFgOutput = FGOutput::NoFG;

        return false;
    }

    *swapChain = proxy;

    _proxy = proxy;
    _gameCommandQueue = queue;
    _swapChain = proxy;
    _hwnd = hwnd;

    return true;
}

bool FlipMeterFG_Dx12::CreateSwapchainInternal(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue,
                                               DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapChain)
{
    return CreateProxy(
        factory, cmdQueue, desc->OutputWindow, desc->BufferCount, swapChain,
        [&](IDXGIFactory* realFactory, ID3D12CommandQueue* queue, UINT bufferCount, IDXGISwapChain** real)
        {
            auto realDesc = *desc;
            realDesc.BufferCount = bufferCount;
            return realFactory->CreateSwapChain(queue, &realDesc, real);
        });
}

bool FlipMeterFG_Dx12::CreateSwapchain1Internal(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                                DXGI_SWAP_CHAIN_DESC1* desc,
                                                DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                                IDXGISwapChain1** swapChain)
{
    return CreateProxy(
        factory, cmdQueue, hwnd, desc->BufferCount, (IDXGISwapChain**) swapChain,
        [&](IDXGIFactory* realFactory, ID3D12CommandQueue* queue, UINT bufferCount, IDXGISwapChain** real)
        {
            Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
            auto result = realFactory->QueryInterface(IID_PPV_ARGS(&factory2));
            if (FAILED(result))
                return result;

            auto realDesc = *desc;
            realDesc.BufferCount = bufferCount;
            return factory2->CreateSwapChainForHwnd(queue, hwnd, &realDesc, pFullscreenDesc, nullptr,
                                                    (IDXGISwapChain1**) real);
        });
}

bool FlipMeterFG_Dx12::ReleaseSwapchain(HWND hwnd)
{
    if (hwnd != _hwnd || _hwnd == NULL)
        return false;

    LOG_DEBUG("");

    MenuOverlayDx::CleanupRenderTarget(true, NULL);

    DestroyFGContext();

    // The real swapchain goes with the references FG hooks release
    if (_proxy != nullptr)
    {
        _proxy->Shutdown();
        _proxy = nullptr;
    }

    return true;
}

void FlipMeterFG_Dx12::CreateContext(ID3D12Device* device, FG_Constants& fgConstants)
{
    _constants = fgConstants;

    CreateObjects(device);
}

int FlipMeterFG_Dx12::ProviderMaxInterpolationCount() { return std::max(Nvngx_FG::getMaxFakeFramesCount(), 1); }

int FlipMeterFG_Dx12::MaxInterpolationCount() const
{
    const int driverMax = _proxy != nullptr ? (int) _proxy->MaxGeneratedFrames() : 1;
    return std::clamp(driverMax, 1, ProviderMaxInterpolationCount());
}

UINT FlipMeterFG_Dx12::TargetCount() const
{
    if (_proxy == nullptr)
        return 0;

    const auto config = Config::Instance();
    const int count = config->FGInterpolationCount(config->FGFlipMeterInterpolationCount.value_or_default());

    return (UINT) std::clamp(count, 1, MaxInterpolationCount());
}

void FlipMeterFG_Dx12::Activate()
{
    if (_isActive || _proxy == nullptr)
        return;

    LOG_DEBUG("");

    _isActive = true;
    _lastDispatchedFrame = 0;
    _framesWithoutInputs = 0;
    _framesToInterpolate = (int) TargetCount();

    _proxy->SetGeneratedPerFrame(_framesToInterpolate);
}

void FlipMeterFG_Dx12::Deactivate()
{
    if (!_isActive)
        return;

    LOG_DEBUG("");

    _isActive = false;

    if (_proxy != nullptr)
    {
        _proxy->SetGeneratedFrames(0);
        _proxy->SetGeneratedPerFrame(0);
    }
}

void FlipMeterFG_Dx12::DestroyFGContext()
{
    Deactivate();
    ReleaseFeature();
    ReleaseObjects();
}

bool FlipMeterFG_Dx12::Shutdown()
{
    MenuOverlayDx::CleanupRenderTarget(true, NULL);
    DestroyFGContext();
    return true;
}

void FlipMeterFG_Dx12::EvaluateState(ID3D12Device* device, FG_Constants& fgConstants)
{
    OwnedLockGuard lock(Mutex, 555);

    auto& state = State::Instance();

    // Without its swapchain there is nothing to generate for
    if (state.isShuttingDown || _proxy == nullptr)
        return;

    _constants = fgConstants;

    if (Config::Instance()->IsFGEnabled())
    {
        if (_uiCommandAllocator[0] == nullptr)
        {
            CreateContext(device, fgConstants);

            // Pause for 10 frames
            UpdateTarget();
        }
        else if (state.fgChanged)
        {
            LOG_DEBUG("FGChanged");
            Deactivate();

            // Pause for 10 frames
            UpdateTarget();
        }
        else if (!IsActive() && !IsPaused())
        {
            Activate();
        }
    }
    else if (IsActive())
    {
        LOG_DEBUG("!FGEnabled");
        Disable();
        ReleaseResourceCopies();

        state.clearCapturedHudlesses = true;
        Hudfix_Dx12::ResetCounters();
        Hudfix_Dx11::ResetCounters();
    }

    if (state.fgChanged)
    {
        state.fgChanged = false;

        Hudfix_Dx12::ResetCounters();
        Hudfix_Dx11::ResetCounters();

        // Release FG mutex
        if (Mutex.getOwner() == 2)
            Mutex.unlockThis(2);
    }

    state.scChanged = false;
}

void FlipMeterFG_Dx12::ReleaseFeature()
{
    if (_handle != nullptr)
    {
        auto result = Nvngx_FG::D3D12_ReleaseFeature(_handle);
        LOG_DEBUG("ReleaseFeature result: {:X}", (UINT) result);
        _handle = nullptr;
    }

    _featureFailed = false;
}

bool FlipMeterFG_Dx12::EnsureFeature(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* backbuffer)
{
    const auto desc = backbuffer->GetDesc();
    const bool changed = desc.Width != _featureWidth || desc.Height != _featureHeight || desc.Format != _featureFormat;

    if (changed)
        ReleaseFeature();
    else if (_handle != nullptr)
        return true;
    else if (_featureFailed)
        return false;

    _featureWidth = desc.Width;
    _featureHeight = desc.Height;
    _featureFormat = desc.Format;

    // The game's own NGX init already reaches the replacement
    if (!_ngxInited && !State::Instance().nvngxDx12Inited)
    {
        NVSDK_NGX_FeatureCommonInfo featureInfo {};
        auto result = Nvngx_FG::D3D12_Init_Ext(0x1337, L".", _device, NVSDK_NGX_Version_API, &featureInfo);

        if (result != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("Nvngx replacement init failed: {:X}", (UINT) result);
            _featureFailed = true;
            return false;
        }

        _ngxInited = true;
    }

    _parameters.Reset();
    Nvngx_FG::D3D12_PopulateParameters_Impl(&_parameters);

    _parameters.Set("Width", (unsigned int) desc.Width);
    _parameters.Set("Height", (unsigned int) desc.Height);
    _parameters.Set("DLSSG.BackbufferFormat", (unsigned int) desc.Format);
    _parameters.Set("CreationNodeMask", 1u);
    _parameters.Set("VisibilityNodeMask", 1u);

    auto result = Nvngx_FG::D3D12_CreateFeature(cmdList, NVSDK_NGX_Feature_FrameGeneration, &_parameters, &_handle);

    if (result != NVSDK_NGX_Result_Success || _handle == nullptr)
    {
        LOG_ERROR("Nvngx replacement CreateFeature failed: {:X}", (UINT) result);
        _handle = nullptr;
        _featureFailed = true;
        return false;
    }

    LOG_INFO("Nvngx replacement feature created: {}x{}, format: {}", desc.Width, desc.Height, (UINT) desc.Format);

    return true;
}

static void SetSubrect(NVNGX_Parameters& parameters, const std::string& name, UINT left, UINT top, UINT width,
                       UINT height)
{
    parameters.Set((name + "SubrectBaseX").c_str(), left);
    parameters.Set((name + "SubrectBaseY").c_str(), top);
    parameters.Set((name + "SubrectWidth").c_str(), width);
    parameters.Set((name + "SubrectHeight").c_str(), height);
}

// Inputs don't always give the size of what they tag
static void ResourceSize(Dx12Resource* resource, UINT& width, UINT& height)
{
    width = (UINT) resource->width;
    height = resource->height;

    if (width == 0 || height == 0)
    {
        const auto desc = resource->GetResource()->GetDesc();
        width = (UINT) desc.Width;
        height = desc.Height;
    }
}

// DLSSG's HDR flag describes the backbuffer, the input's describes the upscaler's colors, HDR before tone mapping
// even with an 8 bit backbuffer. Apex picks its program by the pair: scRGB for float, PQ for 10 bit and sRGB for 8
// bit, and has none for 8 bit with HDR.
static bool BackbufferHdr(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return true;

    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return State::Instance().outputColorSpace.transfer == ColorTransfer::PQ;

    default:
        return false;
    }
}

void FlipMeterFG_Dx12::SetFrameParameters(int fIndex, ID3D12Resource* backbuffer, Dx12Resource* depth,
                                          Dx12Resource* velocity, Dx12Resource* hudless)
{
    const auto backbufferDesc = backbuffer->GetDesc();

    UINT64 rectWidth = 0;
    UINT rectHeight = 0;
    UINT rectLeft = 0;
    UINT rectTop = 0;
    GetInterpolationRect(rectWidth, rectHeight, fIndex);
    GetInterpolationPos(rectLeft, rectTop, fIndex);

    if (rectWidth == 0 || rectHeight == 0 || rectLeft + rectWidth > backbufferDesc.Width ||
        rectTop + rectHeight > backbufferDesc.Height)
    {
        rectWidth = backbufferDesc.Width;
        rectHeight = backbufferDesc.Height;
        rectLeft = 0;
        rectTop = 0;
    }

    UINT depthWidth = 0;
    UINT depthHeight = 0;
    UINT mvWidth = 0;
    UINT mvHeight = 0;
    ResourceSize(depth, depthWidth, depthHeight);
    ResourceSize(velocity, mvWidth, mvHeight);

    _parameters.Set("DLSSG.Backbuffer", backbuffer);
    _parameters.Set("DLSSG.Depth", depth->GetResource());
    _parameters.Set("DLSSG.MVecs", velocity->GetResource());
    _parameters.Set("DLSSG.HUDLess", hudless != nullptr ? hudless->GetResource() : (ID3D12Resource*) nullptr);
    _parameters.Set("DLSSG.UI", (ID3D12Resource*) nullptr);
    _parameters.Set("DLSSG.OutputReal", (ID3D12Resource*) nullptr);

    SetSubrect(_parameters, "DLSSG.Backbuffer", rectLeft, rectTop, (UINT) rectWidth, rectHeight);
    SetSubrect(_parameters, "DLSSG.OutputInterpolated", rectLeft, rectTop, (UINT) rectWidth, rectHeight);
    SetSubrect(_parameters, "DLSSG.Depth", 0, 0, depthWidth, depthHeight);
    SetSubrect(_parameters, "DLSSG.MVecs", 0, 0, mvWidth, mvHeight);
    SetSubrect(_parameters, "DLSSG.HUDLess", rectLeft, rectTop, (UINT) rectWidth, rectHeight);
    SetSubrect(_parameters, "DLSSG.UI", 0, 0, 0, 0);

    // DLSSG wants near < far, the upscaler input has them swapped for inverted depth like FFX wants
    float cameraNear = std::min(_cameraNear[fIndex], _cameraFar[fIndex]);
    float cameraFar = std::max(_cameraNear[fIndex], _cameraFar[fIndex]);

    if (IsInfiniteDepth())
        cameraFar = std::numeric_limits<float>::infinity();

    const float vFov = _cameraVFov[fIndex];
    const float aspectRatio = _cameraAspectRatio[fIndex];

    memset(_cameraViewToClip, 0, sizeof(_cameraViewToClip));
    memset(_clipToCameraView, 0, sizeof(_clipToCameraView));

    // XMMatrixPerspectiveFovRH fails on invalid values, the replacements fall back to near, far and FOV without it
    if (cameraNear > 0.0f && std::isfinite(cameraFar) && !XMScalarNearEqual(vFov, 0.0f, 0.00001f) &&
        !XMScalarNearEqual(aspectRatio, 0.0f, 0.00001f))
    {
        const float matrixFar = XMScalarNearEqual(cameraNear, cameraFar, 0.00001f) ? cameraFar + 1.0f : cameraFar;

        // A reverse-Z projection for inverted depth, replacements read near and far back out of it
        const auto viewToClip = XMMatrixPerspectiveFovRH(vFov, aspectRatio, IsInvertedDepth() ? matrixFar : cameraNear,
                                                         IsInvertedDepth() ? cameraNear : matrixFar);

        XMStoreFloat4x4((XMFLOAT4X4*) _cameraViewToClip, viewToClip);
        XMStoreFloat4x4((XMFLOAT4X4*) _clipToCameraView, XMMatrixInverse(nullptr, viewToClip));
    }

    _parameters.Set("DLSSG.CameraViewToClip", (void*) _cameraViewToClip);
    _parameters.Set("DLSSG.ClipToCameraView", (void*) _clipToCameraView);
    _parameters.Set("DLSSG.ClipToLensClip", (void*) _identity);
    _parameters.Set("DLSSG.ClipToPrevClip", (void*) _identity);
    _parameters.Set("DLSSG.PrevClipToClip", (void*) _identity);

    _parameters.Set("DLSSG.CameraNear", cameraNear);
    _parameters.Set("DLSSG.CameraFar", cameraFar);
    _parameters.Set("DLSSG.CameraFOV", vFov);
    _parameters.Set("DLSSG.CameraAspectRatio", aspectRatio);
    _parameters.Set("DLSSG.CameraPinholeOffsetX", 0.0f);
    _parameters.Set("DLSSG.CameraPinholeOffsetY", 0.0f);

    // Inputs without camera vectors leave them at zero
    const bool hasCamera =
        _cameraPosition[fIndex][0] != 0.0f || _cameraPosition[fIndex][1] != 0.0f || _cameraPosition[fIndex][2] != 0.0f;

    const float defaultUp[3] = { 0.0f, 0.0f, 1.0f };
    const float defaultRight[3] = { 0.0f, 1.0f, 0.0f };
    const float defaultForward[3] = { 1.0f, 0.0f, 0.0f };

    auto setVector = [&](const char* name, const float* vector)
    {
        _parameters.Set(std::format("DLSSG.Camera{}X", name).c_str(), vector[0]);
        _parameters.Set(std::format("DLSSG.Camera{}Y", name).c_str(), vector[1]);
        _parameters.Set(std::format("DLSSG.Camera{}Z", name).c_str(), vector[2]);
    };

    setVector("Pos", _cameraPosition[fIndex]);
    setVector("Up", hasCamera ? _cameraUp[fIndex] : defaultUp);
    setVector("Right", hasCamera ? _cameraRight[fIndex] : defaultRight);
    setVector("Fwd", hasCamera ? _cameraForward[fIndex] : defaultForward);

    _parameters.Set("DLSSG.JitterOffsetX", _jitterX[fIndex]);
    _parameters.Set("DLSSG.JitterOffsetY", _jitterY[fIndex]);

    // Streamline normalizes its mvecScale to the buffer size, but the NGX parameter it hands DLSSG is the scale to
    // pixels, which the replacements pass to FFX as is. The inputs already give the scale to pixels.
    _parameters.Set("DLSSG.MvecScaleX", _mvScaleX[fIndex]);
    _parameters.Set("DLSSG.MvecScaleY", _mvScaleY[fIndex]);

    const bool reset = _reset[fIndex] != 0 && !Config::Instance()->FGSkipReset.value_or_default();

    _parameters.Set("DLSSG.Reset", reset ? 1u : 0u);
    _parameters.Set("DLSSG.BackbufferFrameID", (unsigned int) _frameCount);
    _parameters.Set("DLSSG.ColorBuffersHDR", BackbufferHdr(backbufferDesc.Format) ? 1u : 0u);
    _parameters.Set("DLSSG.DepthInverted", IsInvertedDepth() ? 1u : 0u);
    _parameters.Set("DLSSG.CameraMotionIncluded", 1u);
    _parameters.Set("DLSSG.OrthoProjection", 0u);
    _parameters.Set("DLSSG.MvecJittered", IsJitteredMVs() ? 1u : 0u);
    _parameters.Set("DLSSG.MvecDilated", IsLowResMV() ? 0u : 1u);
    _parameters.Set("DLSSG.NotRenderingGameFrames", 0u);
    _parameters.Set("DLSSG.EnableInterp", 1u);

    // The list is open and gets submitted here. Without IsRecording, replacements like Nukem's reset the list with
    // CmdAlloc, close it and submit it on CmdQueue themselves, leaving the rest of the list closed.
    _parameters.Set("DLSSG.IsRecording", 1u);
    _parameters.Set("DLSSG.CmdAlloc", (void*) _scCommandAllocator[fIndex]);
    _parameters.Set("DLSSG.CmdQueue", (void*) _gameCommandQueue);
}

bool FlipMeterFG_Dx12::EvaluateFrame(ID3D12GraphicsCommandList* cmdList, UINT multiFrameCount, UINT multiFrameIndex,
                                     ID3D12Resource* output)
{
    _parameters.Set("DLSSG.MultiFrameCount", multiFrameCount);
    _parameters.Set("DLSSG.MultiFrameIndex", multiFrameIndex);
    _parameters.Set("DLSSG.OutputInterpolated", output);

    auto result = Nvngx_FG::D3D12_EvaluateFeature(cmdList, _handle, &_parameters, nullptr);

    if (result != NVSDK_NGX_Result_Success)
    {
        LOG_ERROR("Nvngx replacement evaluate {} of {} failed: {:X}", multiFrameIndex, multiFrameCount, (UINT) result);
        return false;
    }

    return true;
}

// Apex only copies a 24 bit depth plane when its copy rows are exactly 4 bytes a texel, 256 byte aligned. AMD pads
// them more (9728 bytes for 2305 texels), Apex then generates nothing. Float depth has no planes to copy.
Dx12Resource FlipMeterFG_Dx12::FloatDepth(ID3D12GraphicsCommandList* cmdList, Dx12Resource& depth)
{
    auto source = depth.GetResource();
    const auto format = source->GetDesc().Format;

    if (format != DXGI_FORMAT_R24G8_TYPELESS && format != DXGI_FORMAT_D24_UNORM_S8_UINT)
        return depth;

    if (_depthTransfer == nullptr)
        _depthTransfer = std::make_unique<FT_Dx12>("FlipMeterDepth", _device, DXGI_FORMAT_R32_FLOAT);

    if (!_depthTransfer->IsInit() ||
        !_depthTransfer->CreateBufferResource(_device, source, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
    {
        return depth;
    }

    constexpr auto readState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    _depthTransfer->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ResourceBarrier(cmdList, source, depth.state, readState);
    const bool converted = _depthTransfer->Dispatch(cmdList, source, _depthTransfer->Buffer());
    ResourceBarrier(cmdList, source, readState, depth.state);

    if (!converted)
        return depth;

    _depthTransfer->SetBufferState(cmdList, InputState);

    auto result = depth;
    result.resource = _depthTransfer->Buffer();
    result.copy = nullptr;
    result.state = InputState;

    return result;
}

bool FlipMeterFG_Dx12::Dispatch(int fIndex)
{
    if (_proxy == nullptr || !IsActive() || IsPaused())
        return false;

    if (!IsResourceReady(FG_ResourceType::Depth, fIndex) || !IsResourceReady(FG_ResourceType::Velocity, fIndex))
    {
        // Metering more frames than get presented paces the game's frames to a fraction of the refresh rate, and the
        // FPS limit scales with the generated frame count
        if (_framesWithoutInputs < IdleAfterFrames && ++_framesWithoutInputs == IdleAfterFrames)
        {
            LOG_INFO("No Depth or Velocity for {} frames, metering off until they come back", IdleAfterFrames);
            _framesToInterpolate = 0;
            _proxy->SetGeneratedPerFrame(0);
        }
        else if (_framesWithoutInputs < IdleAfterFrames)
        {
            LOG_WARN("Depth or Velocity is not ready, skipping");
        }

        return false;
    }

    _framesWithoutInputs = 0;

    const UINT count = TargetCount();

    if (count != (UINT) _framesToInterpolate)
    {
        LOG_INFO("Interpolation count changed {} -> {}", _framesToInterpolate, count);
        _framesToInterpolate = (int) count;
        _proxy->SetGeneratedPerFrame(count);
    }

    // The swapchain only shows generated frames while the driver paces them, it applies a new count with its next
    // present
    if (count == 0 || !_proxy->IsMetering())
        return false;

    auto depth = GetResource(FG_ResourceType::Depth, fIndex);
    auto velocity = GetResource(FG_ResourceType::Velocity, fIndex);
    auto hudless = GetResource(FG_ResourceType::HudlessColor, fIndex);
    auto backbuffer = _proxy->GameBuffer();

    if (!depth || !velocity || backbuffer == nullptr)
        return false;

    const bool useHudless = hudless && IsResourceReady(FG_ResourceType::HudlessColor, fIndex) &&
                            !Config::Instance()->FGDisableHudless.value_or_default();

    auto cmdList = GetSCCommandList(fIndex);

    if (cmdList == nullptr || !EnsureFeature(cmdList, backbuffer))
        return false;

    auto depthInput = FloatDepth(cmdList, *depth);
    Dx12Resource* inputs[] = { &depthInput, &*velocity, useHudless ? &*hudless : nullptr };

    ResourceBarrier(cmdList, backbuffer, D3D12_RESOURCE_STATE_PRESENT, BackbufferState);

    for (auto input : inputs)
    {
        if (input != nullptr)
            ResourceBarrier(cmdList, input->GetResource(), input->state, InputState);
    }

    SetFrameParameters(fIndex, backbuffer, &depthInput, &*velocity, useHudless ? &*hudless : nullptr);

    UINT generated = 0;

    for (UINT i = 0; i < count; i++)
    {
        auto output = _proxy->GeneratedBuffer(i);

        if (output == nullptr || !EvaluateFrame(cmdList, count, i + 1, output))
            break;

        generated++;
    }

    for (auto input : inputs)
    {
        if (input != nullptr)
            ResourceBarrier(cmdList, input->GetResource(), InputState, input->state);
    }

    ResourceBarrier(cmdList, backbuffer, BackbufferState, D3D12_RESOURCE_STATE_PRESENT);

    _proxy->SetGeneratedFrames(generated);

    LOG_DEBUG("_frameCount: {}, fIndex: {}, generated: {} of {}", _frameCount, fIndex, generated, count);

    return generated > 0;
}

bool FlipMeterFG_Dx12::Present()
{
    UINT64 willDispatchFrame = 0;
    auto dispatchIndex = GetDispatchIndex(willDispatchFrame);
    auto fIndex = dispatchIndex < 0 ? GetIndex() : dispatchIndex;

    LOG_DEBUG("fIndex: {}, dispatchIndex: {}", fIndex, dispatchIndex);

    // What gets drawn on the game's buffer here is also in the generated frames
    if (Config::Instance()->FGDrawUIOverFG.value_or_default())
    {
        auto ui = GetResource(FG_ResourceType::UIColor, fIndex);

        if (ui && ui->validity != FG_ResourceValidity::ValidNow)
        {
            if (_renderUI.get() == nullptr ||
                Config::Instance()->FGUIPremultipliedAlpha.value_or_default() != _renderUI->IsPreMultipliedAlpha())
            {
                _renderUI = std::make_unique<RUI_Dx12>("RenderUI", _device,
                                                       Config::Instance()->FGUIPremultipliedAlpha.value_or_default());
            }
            else if (_renderUI->IsInit())
            {
                _renderUI->Dispatch((IDXGISwapChain3*) _swapChain, GetSCCommandList(fIndex), ui->GetResource(),
                                    ui->state);
            }
        }
    }

    if (State::Instance().fgHudlessCompare && IsActive() && !IsPaused())
    {
        auto hudless = GetResource(FG_ResourceType::HudlessColor, fIndex);

        if (hudless && hudless->validity != FG_ResourceValidity::ValidNow)
        {
            if (_hudlessCompare.get() == nullptr)
            {
                _hudlessCompare = std::make_unique<HC_Dx12>("HudlessCompare", _device);
            }
            else if (_hudlessCompare->IsInit())
            {
                _hudlessCompare->Dispatch((IDXGISwapChain3*) _swapChain, GetSCCommandList(fIndex),
                                          hudless->GetResource(), hudless->state);
            }
        }
    }

    if (_uiCommandListResetted[fIndex])
    {
        if (_uiCommandList[fIndex]->Close() == S_OK)
            _gameCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**) &_uiCommandList[fIndex]);

        _gameCommandQueue->Signal(_uiFence, _uiAllocatorFenceValues[fIndex]);
        _uiCommandListResetted[fIndex] = false;
    }

    // The game keeps presenting without new frame data, like in menus
    if ((_fgFramePresentId - _lastFGFramePresentId) > 3 && IsActive() && !_waitingNewFrameData)
    {
        LOG_DEBUG("Pausing FG");
        SubmitSCCommandList(fIndex);
        Deactivate();
        _waitingNewFrameData = true;
        return false;
    }

    _fgFramePresentId++;

    // Recorded on the list of the draws above, so it's submitted once and in order
    const bool generated = dispatchIndex >= 0 && Dispatch(fIndex);

    SubmitSCCommandList(fIndex);

    return generated;
}

bool FlipMeterFG_Dx12::SetResource(Dx12Resource* inputResource)
{
    if (inputResource == nullptr || inputResource->resource == nullptr || _proxy == nullptr ||
        (inputResource->type != FG_ResourceType::UIColor && (!IsActive() || IsPaused())))
    {
        return false;
    }

    // Late Streamline resources come with their frame's index
    auto fIndex = inputResource->frameIndex < 0 ? GetIndex() : inputResource->frameIndex;
    auto& type = inputResource->type;

    std::unique_lock<std::shared_mutex> lock(_resourceMutex[fIndex]);

    if (_frameResources[fIndex].contains(type) &&
        _frameResources[fIndex][type].validity == FG_ResourceValidity::ValidNow)
    {
        return false;
    }

    if (type == FG_ResourceType::HudlessColor)
    {
        if (Config::Instance()->FGDisableHudless.value_or_default())
            return false;

        if (!_noHudless[fIndex] && Config::Instance()->FGOnlyAcceptFirstHudless.value_or_default() &&
            inputResource->validity != FG_ResourceValidity::UntilPresentFromDispatch)
        {
            return false;
        }
    }

    if (type == FG_ResourceType::UIColor && Config::Instance()->FGDisableUI.value_or_default())
        return false;

    if (inputResource->cmdList == nullptr && inputResource->validity == FG_ResourceValidity::ValidNow)
    {
        LOG_ERROR("{}, validity == ValidNow but cmdList is nullptr!", magic_enum::enum_name(type));
        return false;
    }

    _frameResources[fIndex][type] = {};
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
    fResource->flip = inputResource->flip;

    const bool willFlip = WillFlip(fResource);

    if (willFlip && _device != nullptr)
        FlipResource(fResource, fIndex);

    if (type == FG_ResourceType::UIColor)
        _noUi[fIndex] = false;
    else if (type == FG_ResourceType::Distortion)
        _noDistortionField[fIndex] = false;
    else if (type == FG_ResourceType::HudlessColor)
        _noHudless[fIndex] = false;

    // Everything is used at present, what's only valid now gets copied
    if (fResource->validity == FG_ResourceValidity::ValidButMakeCopy)
        fResource->validity = FG_ResourceValidity::ValidNow;

    if (fResource->validity == FG_ResourceValidity::ValidNow && !(willFlip && fResource->copy != nullptr))
    {
        ID3D12Resource* copyOutput = nullptr;

        if (_resourceCopy[fIndex].contains(type))
            copyOutput = _resourceCopy[fIndex].at(type);

        if (!CopyResource(inputResource->cmdList, inputResource->resource, &copyOutput, inputResource->state))
        {
            LOG_ERROR("{}, CopyResource error!", magic_enum::enum_name(type));
            _frameResources[fIndex].erase(type);
            return false;
        }

        copyOutput->SetName(std::format(L"_resourceCopy[{}][{}]", fIndex, (UINT) type).c_str());

        _resourceCopy[fIndex][type] = copyOutput;
        fResource->copy = copyOutput;
        fResource->state = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    fResource->validity = FG_ResourceValidity::UntilPresent;

    SetResourceReady(type, fIndex);

    LOG_TRACE("_frameResources[{}][{}]: {:X}", fIndex, magic_enum::enum_name(type), (size_t) fResource->GetResource());

    return true;
}

void FlipMeterFG_Dx12::SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue) { _gameCommandQueue = queue; }
