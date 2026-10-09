#include "pch.h"
#include "XeFG_Inputs_Dx12.h"
#include <proxies/XeLL_Proxy.h>
#include <detours/detours.h>

void XeFGInputs::PassApply(xefg_swapchain_handle_t context)
{
    auto& setting = Config::Instance()->FGXeFGOverrideInterpolationCount;
    const uint32_t enabled = setting.has_value() && setting.value() == 0 ? 0 : _passGameEnabled;
    const uint32_t count = std::min(setting.value_or(0) > 0 ? (uint32_t) setting.value() : _passGameCount, _passMax);

    // Each change is applied once, a failed call is logged instead of repeated every frame
    if (enabled != _passEnabled)
    {
        _passEnabled = enabled;
        auto result = XeFGProxy::SetEnabled()(context, enabled);
        LOG_INFO("XeFG passthrough: enabled {} (game {}), result: {}", enabled, _passGameEnabled, (int) result);
    }

    if (count >= 1 && count != _passCount && XeFGProxy::SetNumInterpolatedFrames() != nullptr)
    {
        _passCount = count;
        auto result = XeFGProxy::SetNumInterpolatedFrames()(context, count);
        LOG_INFO("XeFG passthrough: interpolated frames {} (game {}, max {}), result: {}", count, _passGameCount,
                 _passMax, (int) result);
    }
}

void XeFGInputs::PassInit(const xefg_swapchain_d3d12_init_params_t* params)
{
    // A new context starts with frame generation off until the game enables it
    _passContext = nullptr;
    _passGameCount = params ? params->maxInterpolatedFrames : 1;
    _passGameEnabled = 0;
    LOG_INFO("XeFG passthrough: game context init, interpolated frames: {}", _passGameCount);
}

xefg_swapchain_result_t XeFGInputs::PassInitFromSwapChain(xefg_swapchain_handle_t context, ID3D12CommandQueue* queue,
                                                          const xefg_swapchain_d3d12_init_params_t* params)
{
    PassInit(params);
    return XeFGProxy::D3D12InitFromSwapChain()(context, queue, params);
}

xefg_swapchain_result_t XeFGInputs::PassInitFromSwapChainDesc(xefg_swapchain_handle_t context, HWND hwnd,
                                                              const DXGI_SWAP_CHAIN_DESC1* desc,
                                                              const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
                                                              ID3D12CommandQueue* queue, IDXGIFactory2* factory,
                                                              const xefg_swapchain_d3d12_init_params_t* params)
{
    PassInit(params);
    return XeFGProxy::D3D12InitFromSwapChainDesc()(context, hwnd, desc, fullscreenDesc, queue, factory, params);
}

xefg_swapchain_result_t XeFGInputs::PassSetEnabled(xefg_swapchain_handle_t context, uint32_t enable)
{
    _passGameEnabled = enable;

    if (context == _passContext)
        PassApply(context);

    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::PassSetNumInterpolatedFrames(xefg_swapchain_handle_t context, uint32_t count)
{
    _passGameCount = count;

    if (context == _passContext)
        PassApply(context);

    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::PassSetPresentId(xefg_swapchain_handle_t context, uint32_t presentId)
{
    if (context != _passContext)
    {
        xefg_swapchain_d3d12_init_params_t params {};
        auto getParams = XeFGProxy::D3D12GetInitializationParameters();
        _passContext = context;
        _passCount = 0;
        _passEnabled = UINT32_MAX;
        _passMax = getParams != nullptr && getParams(context, &params) == XEFG_SWAPCHAIN_RESULT_SUCCESS
                       ? params.maxInterpolatedFrames
                       : 0;
        LOG_INFO("XeFG passthrough: new context, max interpolated frames: {}", _passMax);
    }

    PassApply(context);
    return XeFGProxy::SetPresentId()(context, presentId);
}

// Each new present id starts a new frame of the FG output and all tags of that frame use its buffer index.
// The output's frame count may also follow the game's Reflex markers, so present ids are never written into it.
int XeFGInputs::FrameIndex(IFGFeature_Dx12* fg, uint32_t presentId)
{
    static std::mutex mutex;
    std::scoped_lock lock(mutex);

    for (int i = 0; i < BUFFER_COUNT; i++)
    {
        if (_frameIds[i] == (uint64_t) presentId + 1)
            return i;
    }

    fg->StartNewFrame();
    auto index = fg->GetIndex();
    _frameIds[index] = (uint64_t) presentId + 1;
    _gameDepthMV[index] = false;
    _reprojectionKnown[index] = false;
    _pendingDepth[index] = {};
    _pendingVelocity[index] = {};
    return index;
}

static bool CameraMotionFill() { return State::Instance().gameQuirks[GameQuirk::XeFGCameraMotionFill]; }

// A projection matrix has w = +-z, no w translation and doesn't mix x and y (row or column major, either handedness).
// The Witcher 3's has noise above 1e-4 in its w translation, a combined matrix of a turned camera mixes x and y.
static bool IsProjection(const DirectX::XMFLOAT4X4& matrix)
{
    auto m = &matrix._11;
    return std::abs(m[15]) < 1e-2f && std::abs(m[1]) < 1e-3f && std::abs(m[4]) < 1e-3f &&
           (std::abs(std::abs(m[11]) - 1.0f) < 1e-3f || std::abs(std::abs(m[14]) - 1.0f) < 1e-3f);
}

// A view matrix only rotates and translates
static bool IsView(const DirectX::XMFLOAT4X4& matrix)
{
    auto m = &matrix._11;
    auto zero = [](float value) { return std::abs(value) < 1e-4f; };
    return std::abs(m[15] - 1.0f) < 1e-4f &&
           ((zero(m[3]) && zero(m[7]) && zero(m[11])) || (zero(m[12]) && zero(m[13]) && zero(m[14])));
}

// Games pass the projection in either slot, or a view matrix and a combined view-projection matrix
static bool FindProjection(const xefg_swapchain_frame_constant_data_t* data, DirectX::XMFLOAT4X4& projection)
{
    using namespace DirectX;
    const XMFLOAT4X4 view(data->viewMatrix), proj(data->projectionMatrix);

    for (const auto& candidate : { proj, view })
    {
        if (IsProjection(candidate))
        {
            projection = candidate;
            return true;
        }
    }

    const std::pair<XMFLOAT4X4, XMFLOAT4X4> pairs[] = { { view, proj }, { proj, view } };

    for (const auto& [rigid, combined] : pairs)
    {
        if (!IsView(rigid))
            continue;

        auto inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&rigid));
        auto both = XMLoadFloat4x4(&combined);

        for (const auto& product : { XMMatrixMultiply(inverse, both), XMMatrixMultiply(both, inverse) })
        {
            XMStoreFloat4x4(&projection, product);

            if (IsProjection(projection))
                return true;
        }
    }

    return false;
}

// The Witcher 3 passes the projection as the view matrix and the other way around
static bool FindViewProjection(const xefg_swapchain_frame_constant_data_t* data, DirectX::XMFLOAT4X4& viewProjection)
{
    using namespace DirectX;
    const XMFLOAT4X4 first(data->viewMatrix), second(data->projectionMatrix);

    for (const auto& [view, projection] : { std::pair { &first, &second }, std::pair { &second, &first } })
    {
        if (IsView(*view) && IsProjection(*projection))
        {
            XMStoreFloat4x4(&viewProjection, XMMatrixMultiply(XMLoadFloat4x4(view), XMLoadFloat4x4(projection)));
            return true;
        }
    }

    return false;
}

// Current clip space to the previous frame's
void XeFGInputs::UpdateReprojection(uint32_t presentId, const xefg_swapchain_frame_constant_data_t* data, int index)
{
    // Constants sent again for the same frame keep its previous frame
    if (_cameraPresentId != (uint64_t) presentId + 1)
    {
        _cameraPresentId = (uint64_t) presentId + 1;
        _previousViewProjection = _viewProjection;
        _previousCameraKnown = _cameraKnown;
    }

    _cameraKnown = FindViewProjection(data, _viewProjection);

    static bool warned = false;

    if (!_cameraKnown && !std::exchange(warned, true))
        LOG_WARN("XeFG input: no view and projection matrices, camera motion is left out");
    else if (!_cameraKnown)
        LOG_DEBUG("XeFG input: no view and projection matrices for present id {}", presentId);

    using namespace DirectX;
    auto reprojection = XMMatrixIdentity();
    XMVECTOR determinant {};
    auto inverse = XMMatrixInverse(&determinant, XMLoadFloat4x4(&_viewProjection));

    if (_cameraKnown && _previousCameraKnown && !data->resetHistory && XMVectorGetX(determinant) != 0.0f)
        reprojection = XMMatrixMultiply(inverse, XMLoadFloat4x4(&_previousViewProjection));

    XMStoreFloat4x4(&_reprojection[index], reprojection);
    _reprojectionKnown[index] = true;
}

// Velocity in pixels as previous - current position, with the camera's motion where the game wrote none
bool XeFGInputs::FillVelocity(IFGFeature_Dx12* fg, ID3D12GraphicsCommandList* cmdList, int index)
{
    auto& depth = _pendingDepth[index];
    auto& velocity = _pendingVelocity[index];

    if (depth.resource == nullptr || velocity.resource == nullptr)
        return false;

    if (_cameraMotion == nullptr || _cameraMotionDevice != _device)
    {
        _cameraMotion = std::make_unique<CM_Dx12>("CameraMotion", _device);
        _cameraMotionDevice = _device;
    }

    static bool listWarned = false;

    if (depth.cmdList != velocity.cmdList && !std::exchange(listWarned, true))
        LOG_WARN("XeFG input: depth and motion vectors are tagged on different command lists");

    if (cmdList == nullptr)
        cmdList = fg->GetUICommandList(index);

    static bool warned = false;

    if (!_reprojectionKnown[index] && !std::exchange(warned, true))
        LOG_WARN("XeFG input: depth and motion vectors came before the frame constants, camera motion is left out");

    CMConstants constants {};

    if (_reprojectionKnown[index])
        constants.Reprojection = _reprojection[index];
    else
        DirectX::XMStoreFloat4x4(&constants.Reprojection, DirectX::XMMatrixIdentity());

    constants.Width = (uint32_t) velocity.width;
    constants.Height = velocity.height;
    constants.Left = velocity.left;
    constants.Top = velocity.top;
    constants.DepthScaleX = (float) depth.width / (float) velocity.width;
    constants.DepthScaleY = (float) depth.height / (float) velocity.height;
    constants.DepthLeft = depth.left;
    constants.DepthTop = depth.top;

    auto output = _cameraMotion->Dispatch(cmdList, index, velocity.resource, velocity.state, depth.resource,
                                          depth.state, constants);

    if (output == nullptr)
    {
        LOG_ERROR("XeFG input: camera motion failed, frame {} has no motion vectors", index);
        return false;
    }

    // The output belongs to this frame index, it stays valid until the frame is presented
    velocity.resource = output;
    velocity.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    velocity.cmdList = cmdList;
    velocity.validity = FG_ResourceValidity::UntilPresent;
    fg->SetResource(&velocity);

    depth = {};
    velocity = {};
    return true;
}

// XeFG takes the frame render time in milliseconds, some games (The Witcher 3, Cyberpunk 2077) pass seconds.
// The first frames decide: below 1 it can't be milliseconds, frame generation doesn't run at 1000 fps.
float XeFGInputs::FrameTimeMs(float frameTime)
{
    if (frameTime <= 0.0f)
        return 0.0f;

    if (_frameTimeScale == 0.0f)
    {
        _frameTimeMax = std::max(_frameTimeMax, frameTime);

        if (++_frameTimeSamples < 10)
            return frameTime < 1.0f ? frameTime * 1000.0f : frameTime;

        _frameTimeScale = _frameTimeMax < 1.0f ? 1000.0f : 1.0f;
        LOG_INFO("XeFG input: frame render time in {}", _frameTimeScale == 1.0f ? "milliseconds" : "seconds");
    }

    return frameTime * _frameTimeScale;
}

xefg_swapchain_result_t XeFGInputs::CreateContext(ID3D12Device* device, xefg_swapchain_handle_t* handle)
{
    _device = device;
    *handle = (xefg_swapchain_handle_t) &_device;
    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::GetProperties(xefg_swapchain_handle_t, xefg_swapchain_properties_t* properties)
{
    // Small non-zero sizes keep the game's optional heap allocations valid
    *properties = { 1, 65536, 65536, 256, 1 };
    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::D3D12GetProperties(xefg_swapchain_handle_t handle,
                                                       const xefg_swapchain_d3d12_init_params_t*, uint32_t, uint32_t,
                                                       DXGI_FORMAT, xefg_swapchain_properties_t* properties)
{
    return GetProperties(handle, properties);
}

void XeFGInputs::SetGameMVFlags()
{
    auto flags = _initParams.initFlags;
    _constants.flags.set(FG_Flags::JitteredMVs, flags & XEFG_SWAPCHAIN_INIT_FLAG_JITTERED_MV);
    _constants.flags.set(FG_Flags::DisplayResolutionMVs, flags & XEFG_SWAPCHAIN_INIT_FLAG_HIGH_RES_MV);
}

xefg_swapchain_result_t XeFGInputs::InitFromSwapChain(xefg_swapchain_handle_t, ID3D12CommandQueue*,
                                                      const xefg_swapchain_d3d12_init_params_t* params)
{
    // The game's swapchain was created through OptiScaler's hooks, so it already is the FG output's swapchain
    if (State::Instance().currentFGSwapchain == nullptr || params->pApplicationSwapChain == nullptr)
        return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;

    _swapChain = params->pApplicationSwapChain;
    _enabled = false;
    _frameTimeScale = 0.0f;
    _frameTimeMax = 0.0f;
    _frameTimeSamples = 0;
    _upscalerMVs = false;
    std::fill(std::begin(_frameIds), std::end(_frameIds), 0);
    std::fill(std::begin(_gameDepthMV), std::end(_gameDepthMV), false);
    _cameraKnown = false;
    _previousCameraKnown = false;
    _cameraPresentId = 0;
    _initParams = *params;
    _initParams.pApplicationSwapChain = nullptr;
    _initParams.maxInterpolatedFrames = 1;

    auto flags = params->initFlags;
    _constants = {};
    _constants.flags.set(FG_Flags::InvertedDepth, flags & XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH);
    _constants.flags.set(FG_Flags::Async, Config::Instance()->FGAsync.value_or_default());
    SetGameMVFlags();

    LOG_INFO("XeFG input initialized, flags: {:X}", flags);
    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::InitFromSwapChainDesc(xefg_swapchain_handle_t handle, HWND hwnd,
                                                          const DXGI_SWAP_CHAIN_DESC1* desc,
                                                          const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
                                                          ID3D12CommandQueue* queue, IDXGIFactory2* factory,
                                                          const xefg_swapchain_d3d12_init_params_t* params)
{
    IDXGISwapChain1* swapChain = nullptr;
    auto appParams = *params;

    if (factory->CreateSwapChainForHwnd(queue, hwnd, desc, fullscreenDesc, nullptr, &swapChain) != S_OK)
        return XEFG_SWAPCHAIN_RESULT_ERROR_HRESULT_FAILURE;

    appParams.pApplicationSwapChain = swapChain;
    auto result = InitFromSwapChain(handle, queue, &appParams);

    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
        swapChain->Release();

    return result;
}

xefg_swapchain_result_t XeFGInputs::GetSwapChainPtr(xefg_swapchain_handle_t, REFIID riid, void** swapChain)
{
    if (_swapChain == nullptr || _swapChain->QueryInterface(riid, swapChain) != S_OK)
        return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;

    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::GetInitializationParameters(xefg_swapchain_handle_t,
                                                                xefg_swapchain_d3d12_init_params_t* params)
{
    *params = _initParams;
    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::SetEnabled(xefg_swapchain_handle_t, uint32_t enable)
{
    if (_enabled != (enable != 0))
        LOG_INFO("XeFG input: game SetEnabled {}", enable);

    _enabled = enable != 0;

    // The game stops tagging frames once it disables FG, so the FG output has to stop now
    if (auto fg = State::Instance().currentFG; !_enabled && fg != nullptr && fg->IsActive())
    {
        fg->Deactivate();
        fg->ResetCounters();
    }

    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::TagFrameConstants(xefg_swapchain_handle_t, uint32_t presentId,
                                                      const xefg_swapchain_frame_constant_data_t* data)
{
    auto fg = State::Instance().currentFG;

    if (fg == nullptr || _swapChain == nullptr)
        return XEFG_SWAPCHAIN_RESULT_SUCCESS;

    auto index = FrameIndex(fg, presentId);

    // Camera values from the projection matrix
    DirectX::XMFLOAT4X4 projection {};
    bool cameraKnown = FindProjection(data, projection);

    static bool cameraWarned = false;

    if (!cameraKnown && !std::exchange(cameraWarned, true))
        LOG_WARN("XeFG input: no projection matrix found");

    auto m = &projection._11;
    double b = std::abs(m[5]), c = m[10], d = m[14], e = m[11];

    if (std::abs(std::abs(e) - 1.0) > 1e-3)
        std::swap(d, e);

    float nearZ = (float) (c == 0.0 ? 0.0 : (e < 0.0 ? d / c : -d / c));
    float farZ = (float) (e < 0.0 ? d / (c + 1.0) : -d / (c - 1.0));

    if (_constants.flags[FG_Flags::InvertedDepth])
        std::swap(nearZ, farZ);

    // A zero far plane is infinite
    bool infiniteDepth = cameraKnown && nearZ != 0.0f && farZ == 0.0f;
    _constants.flags.set(FG_Flags::InfiniteDepth, infiniteDepth);

    if (infiniteDepth)
        farZ = nearZ + 1.0f;

    fg->EvaluateState(_device, _constants);

    // Follow the game's FG toggle, the FG output needs the camera to start
    if (_enabled && cameraKnown && Config::Instance()->FGEnabled.value_or_default() && !fg->IsActive() &&
        !fg->IsPaused())
    {
        fg->Activate();
        fg->ResetCounters();
    }
    else if (!_enabled && fg->IsActive())
    {
        fg->Deactivate();
        fg->ResetCounters();
    }

    if (cameraKnown && b != 0.0 && m[0] != 0.0f)
        fg->SetCameraValues(nearZ, farZ, (float) (2.0 * std::atan(1.0 / b)), (float) (b / std::abs(m[0])), 0.0f, index);

    // NDC velocity to pixels, as for XeSS upscaler inputs
    float mvScaleX = data->motionVectorScaleX;
    float mvScaleY = data->motionVectorScaleY;

    if (CameraMotionFill())
    {
        // The camera motion shader outputs pixels
        UpdateReprojection(presentId, data, index);
        mvScaleX = 1.0f;
        mvScaleY = 1.0f;
    }
    else if (_initParams.initFlags & XEFG_SWAPCHAIN_INIT_FLAG_USE_NDC_VELOCITY)
    {
        mvScaleX *= _mvSize.x * 0.5f;
        mvScaleY *= _mvSize.y * -0.5f;
    }

    // For the game's MVs, the upscaler fallback replaces it later in the frame
    fg->SetJitter(data->jitterOffsetX, data->jitterOffsetY, index);
    fg->SetMVScale(mvScaleX, mvScaleY, index);
    fg->SetReset(data->resetHistory, index);
    fg->SetFrameTimeDelta(FrameTimeMs(data->frameRenderTime), index);

    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::TagFrameResource(xefg_swapchain_handle_t, ID3D12CommandList* cmdList,
                                                     uint32_t presentId,
                                                     const xefg_swapchain_d3d12_resource_data_t* data)
{
    static const FG_ResourceType types[] = { FG_ResourceType::HudlessColor, FG_ResourceType::Depth,
                                             FG_ResourceType::Velocity, FG_ResourceType::UIColor };
    auto fg = State::Instance().currentFG;

    if (fg == nullptr || _swapChain == nullptr || data->type >= XEFG_SWAPCHAIN_RES_COUNT)
        return XEFG_SWAPCHAIN_RESULT_SUCCESS;

    auto index = FrameIndex(fg, presentId);

    // The hudless color or backbuffer region is the interpolated area
    if (data->type == XEFG_SWAPCHAIN_RES_HUDLESS_COLOR || data->type == XEFG_SWAPCHAIN_RES_BACKBUFFER)
    {
        fg->SetInterpolationRect(data->resourceSize.x, data->resourceSize.y, index);
        fg->SetInterpolationPos(data->resourceBase.x, data->resourceBase.y, index);
    }

    if (data->type == XEFG_SWAPCHAIN_RES_BACKBUFFER)
        return XEFG_SWAPCHAIN_RESULT_SUCCESS;

    if (data->type == XEFG_SWAPCHAIN_RES_MOTION_VECTOR)
        _mvSize = data->resourceSize;

    if (data->type == XEFG_SWAPCHAIN_RES_DEPTH || data->type == XEFG_SWAPCHAIN_RES_MOTION_VECTOR)
    {
        _gameDepthMV[index] = true;

        if (std::exchange(_upscalerMVs, false))
        {
            LOG_INFO("XeFG input: game tags depth and motion vectors again");
            SetGameMVFlags();
        }
    }

    Dx12Resource res {};
    res.type = types[data->type];
    res.resource = data->pResource;
    res.cmdList = (ID3D12GraphicsCommandList*) cmdList;
    res.left = data->resourceBase.x;
    res.top = data->resourceBase.y;
    res.width = data->resourceSize.x;
    res.height = data->resourceSize.y;
    res.state = data->incomingState;
    res.frameIndex = index;

    // Without a command list a resource can only be used until present
    res.validity = data->validity == XEFG_SWAPCHAIN_RV_ONLY_NOW && cmdList != nullptr
                       ? FG_ResourceValidity::ValidNow
                       : FG_ResourceValidity::UntilPresent;

    if (CameraMotionFill() &&
        (data->type == XEFG_SWAPCHAIN_RES_DEPTH || data->type == XEFG_SWAPCHAIN_RES_MOTION_VECTOR))
    {
        if (data->type == XEFG_SWAPCHAIN_RES_DEPTH)
        {
            fg->SetResource(&res);
            _pendingDepth[index] = res;
        }
        else
        {
            _pendingVelocity[index] = res;
        }

        FillVelocity(fg, res.cmdList, index);
        return XEFG_SWAPCHAIN_RESULT_SUCCESS;
    }

    fg->SetResource(&res);
    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::GetLastPresentStatus(xefg_swapchain_handle_t,
                                                         xefg_swapchain_present_status_t* status)
{
    auto fg = State::Instance().currentFG;
    uint32_t active = fg != nullptr && fg->IsActive() && !fg->IsPaused();
    *status = { active ? fg->GetInterpolatedFrameCount() + 1 : 1, XEFG_SWAPCHAIN_RESULT_SUCCESS, active };
    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_result_t XeFGInputs::Destroy(xefg_swapchain_handle_t)
{
    LOG_INFO("XeFG input: game context destroyed");

    if (auto fg = State::Instance().currentFG; fg != nullptr && fg->IsActive())
        fg->Deactivate();

    if (_swapChain != nullptr)
        _swapChain->Release();

    _swapChain = nullptr;
    _enabled = false;
    return XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

template <size_t N> bool XeFGInputs::Attach(HMODULE module, HookEntry (&hooks)[N])
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (auto& hook : hooks)
    {
        hook.target = (PVOID) KernelBaseProxy::GetProcAddress_()(module, hook.name);

        if (hook.target != nullptr)
            DetourAttach(&hook.target, hook.function);
    }

    return DetourTransactionCommit() == NO_ERROR;
}

// XeFG reads the XeLL context directly, it needs the real one behind it
xefg_swapchain_result_t XeFGInputs::GameSetLatencyReduction(xefg_swapchain_handle_t hSwapChain, void* hXeLLContext)
{
    InputXeLL::AttachXeFG(hXeLLContext);
    return ((decltype(&xefgSwapChainSetLatencyReduction)) _nativeHooks[0].target)(hSwapChain, hXeLLContext);
}

// Only while it generates frames the game's XeLL runs as without OptiScaler
xefg_swapchain_result_t XeFGInputs::GameSetEnabled(xefg_swapchain_handle_t hSwapChain, uint32_t enable)
{
    auto result = ((decltype(&xefgSwapChainSetEnabled)) _nativeHooks[1].target)(hSwapChain, enable);

    if (result == XEFG_SWAPCHAIN_RESULT_SUCCESS)
        InputXeLL::SetXeFGEnabled(enable != 0);

    return result;
}

// Every export of the game's library jumps to the same export of OptiScaler's copy
BOOL CALLBACK XeFGInputs::RedirectExport(PVOID library, ULONG, LPCSTR name, PVOID target)
{
    PVOID replacement = nullptr;

    for (const auto& hook : _passHooks)
    {
        if (name != nullptr && strcmp(name, hook.name) == 0)
            replacement = hook.function;
    }

    if (name != nullptr && replacement == nullptr)
        replacement = (PVOID) KernelBaseProxy::GetProcAddress_()((HMODULE) library, name);

    if (name != nullptr && replacement == nullptr)
        _redirectIncomplete = true;

    if (replacement != nullptr && _redirectCount < std::size(_redirects))
    {
        _redirects[_redirectCount] = target;
        DetourAttach(&_redirects[_redirectCount++], replacement);
    }

    return TRUE;
}

bool XeFGInputs::Redirect(HMODULE from, HMODULE to)
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    _redirectIncomplete = false;
    DetourEnumerateExports(from, to, RedirectExport);

    if (_redirectIncomplete)
    {
        LOG_WARN("OptiScaler's library is missing exports of the game's, not redirecting");
        DetourTransactionAbort();
        return false;
    }

    return DetourTransactionCommit() == NO_ERROR;
}

bool XeFGInputs::Passthrough()
{
    // Decided at startup like the other FG settings, a changed FG Output needs a restart
    static const bool passthrough = State::Instance().activeFgInput == FGInput::XeFG &&
                                    Config::Instance()->FGOutput.value_or_default() == FGOutput::XeFG;
    return passthrough;
}

void XeFGInputs::SetUpscalerInputs(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* parameters,
                                   IFeature_Dx12* feature)
{
    auto fg = State::Instance().currentFG;

    if (State::Instance().activeFgInput != FGInput::XeFG || Passthrough() || fg == nullptr || _swapChain == nullptr ||
        !_enabled)
        return;

    // The game's frame constants for this frame come before the upscaler, so it's the latest frame
    auto index = fg->GetIndex();

    // Cyberpunk 2077 only skips them with DLSS, the game's own tags are used whenever they come
    if (_gameDepthMV[index])
        return;

    if (!std::exchange(_upscalerMVs, true))
        LOG_INFO("XeFG input: game doesn't tag depth and motion vectors, using the upscaler's");

    _constants.flags.set(FG_Flags::JitteredMVs, feature->JitteredMV());
    _constants.flags.set(FG_Flags::DisplayResolutionMVs, !feature->LowResMV());

    float mvScaleX = 0.0f;
    float mvScaleY = 0.0f;
    parameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvScaleX);
    parameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvScaleY);
    fg->SetMVScale(mvScaleX, mvScaleY, index);

    ID3D12Resource* velocity = nullptr;
    if (parameters->Get(NVSDK_NGX_Parameter_MotionVectors, &velocity) != NVSDK_NGX_Result_Success)
        parameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &velocity);

    ID3D12Resource* depth = nullptr;
    if (parameters->Get(NVSDK_NGX_Parameter_Depth, &depth) != NVSDK_NGX_Result_Success)
        parameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &depth);

    if (velocity != nullptr)
    {
        Dx12Resource res {};
        res.type = FG_ResourceType::Velocity;
        res.resource = velocity;
        res.cmdList = cmdList;
        res.width = feature->LowResMV() ? feature->RenderWidth() : feature->TargetWidth();
        res.height = feature->LowResMV() ? feature->RenderHeight() : feature->TargetHeight();
        res.state = (D3D12_RESOURCE_STATES) Config::Instance()->MVResourceBarrier.value_or(
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        res.validity = FG_ResourceValidity::ValidNow;
        res.frameIndex = index;
        fg->SetResource(&res);
    }

    if (depth != nullptr)
    {
        Dx12Resource res {};
        res.type = FG_ResourceType::Depth;
        res.resource = depth;
        res.cmdList = cmdList;
        res.width = feature->RenderWidth();
        res.height = feature->RenderHeight();
        res.state = (D3D12_RESOURCE_STATES) Config::Instance()->DepthResourceBarrier.value_or(
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        res.validity = FG_ResourceValidity::ValidNow;
        res.frameIndex = index;
        fg->SetResource(&res);
    }
}

void XeFGInputs::Hook(HMODULE libxessFg)
{
    static HMODULE hooked = nullptr;

    if (libxessFg == nullptr || libxessFg == hooked)
        return;

    if (State::Instance().activeFgInput != FGInput::XeFG)
    {
#ifdef LOW_LATENCY_INPUTS
        // One library, its trampoline is kept in the table
        if (_nativeHooks[0].target == nullptr)
        {
            hooked = libxessFg;
            LOG_INFO("XeFG XeLL hook: {}", Attach(libxessFg, _nativeHooks));
        }
#endif
        return;
    }

    if (libxessFg == XeFGProxy::Module() && Passthrough())
        return;

    hooked = libxessFg;

    if (!Passthrough())
    {
        LOG_INFO("XeFG input hooks: {}", Attach(libxessFg, _xefgHooks));
        return;
    }

    bool result = XeFGProxy::Module() != nullptr && Redirect(libxessFg, XeFGProxy::Module());

#ifndef LOW_LATENCY_INPUTS
    // XeFG and XeLL contexts must come from the same pair of libraries. With low latency inputs the game's XeLL
    // already goes to OptiScaler's XeLL input, which uses OptiScaler's libxell while XeFG runs.
    auto libxell = KernelBaseProxy::GetModuleHandleW_()(L"libxell.dll");

    if (result && libxell != nullptr && XeLLProxy::Module() != nullptr && libxell != XeLLProxy::Module())
        result = Redirect(libxell, XeLLProxy::Module());
#endif

    LOG_INFO("XeFG passthrough: {}", result);
}
