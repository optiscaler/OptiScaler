#include "pch.h"
#include "Reprojection_Dx12.h"

#include <hudfix/Hudfix_Dx12.h>
#include <menu/menu_overlay_dx.h>

#include <magic_enum.hpp>
#include "InputCollection.h"
#include <numbers>

using namespace DirectX;

void Reprojection_Dx12::ReleaseObjects()
{
    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        SAFE_RELEASE(_uiCommandAllocator[i]);
        SAFE_RELEASE(_uiCommandList[i]);
        SAFE_RELEASE(_scCommandAllocator[i]);
        SAFE_RELEASE(_scCommandList[i]);

        _scCommandListResetted[i] = false;
        _scAllocatorFenceValues[i] = 0;

        _uiCommandListResetted[i] = false;
        _uiAllocatorFenceValues[i] = 0;
    }

    _renderUI.reset();
    _hudlessCompare.reset();
    _mvFlip.reset();
    _depthFlip.reset();
}

void Reprojection_Dx12::CreateObjects(ID3D12Device* InDevice)
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

        // UI Command List setup
        InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_uiCommandAllocator[i]));
        _uiCommandAllocator[i]->SetName(std::format(L"_uiCommandAllocator[{}]", i).c_str());

        InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _uiCommandAllocator[i], NULL,
                                    IID_PPV_ARGS(&_uiCommandList[i]));
        _uiCommandList[i]->SetName(std::format(L"_uiCommandList[{}]", i).c_str());
        _uiCommandList[i]->Close();

        if (_uiFence == nullptr)
        {
            InDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_uiFence));
            _uiFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        }

        // Swapchain Command List setup
        InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_scCommandAllocator[i]));
        _scCommandAllocator[i]->SetName(std::format(L"_scCommandAllocator[{}]", i).c_str());

        InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _scCommandAllocator[i], NULL,
                                    IID_PPV_ARGS(&_scCommandList[i]));
        _scCommandList[i]->SetName(std::format(L"_scCommandList[{}]", i).c_str());
        _scCommandList[i]->Close();

        if (_scFence == nullptr)
        {
            InDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_scFence));
            _scFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        }
    }
}

bool Reprojection_Dx12::CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                                        IDXGISwapChain** swapChain, bool readyToRelease)
{
    // Normal swapchain creation, no proxy upgrades
    auto result = factory->CreateSwapChain(cmdQueue, desc, swapChain);

    if (result == S_OK)
    {
        _gameCommandQueue = cmdQueue;
        _swapChain = *swapChain;
        _hwnd = desc->OutputWindow;
        return true;
    }
    return false;
}

bool Reprojection_Dx12::CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                         DXGI_SWAP_CHAIN_DESC1* desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                         IDXGISwapChain1** swapChain, bool readyToRelease)
{
    IDXGIFactory2* factory2 = nullptr;
    if (factory->QueryInterface(IID_PPV_ARGS(&factory2)) != S_OK)
        return false;

    // Normal swapchain creation
    auto result = factory2->CreateSwapChainForHwnd(cmdQueue, hwnd, desc, pFullscreenDesc, nullptr, swapChain);
    factory2->Release();

    if (result == S_OK)
    {
        _gameCommandQueue = cmdQueue;
        _swapChain = *swapChain;
        _hwnd = hwnd;
        return true;
    }
    return false;
}

bool Reprojection_Dx12::ReleaseSwapchain(HWND hwnd)
{
    if (hwnd != _hwnd || _hwnd == NULL)
        return false;

    MenuOverlayDx::CleanupRenderTarget(true, NULL);
    ReleaseObjects();
    return true;
}

void Reprojection_Dx12::CreateContext(ID3D12Device* device, FG_Constants& fgConstants)
{
    if (_device != nullptr)
        return;

    _device = device;
    CreateObjects(device);
}

void Reprojection_Dx12::Activate()
{
    if (!_isActive)
    {
        UpdateTarget();
        _isActive = true;
    }
}

void Reprojection_Dx12::Deactivate()
{
    if (_isActive)
        _isActive = false;
}

void Reprojection_Dx12::DestroyFGContext()
{
    Deactivate();
    ReleaseObjects();
}

bool Reprojection_Dx12::Shutdown()
{
    MenuOverlayDx::CleanupRenderTarget(true, NULL);
    DestroyFGContext();
    return true;
}

void Reprojection_Dx12::EvaluateState(ID3D12Device* device, FG_Constants& fgConstants)
{
    OwnedLockGuard lock(Mutex, 555);

    if (State::Instance().isShuttingDown)
        return;

    _constants = fgConstants;

    if (Config::Instance()->FGEnabled.value_or_default())
    {
        if (_device == nullptr)
            CreateContext(device, fgConstants);

        if (!IsPaused() && !IsActive())
            Activate();
    }
    else
    {
        Deactivate();
    }
}

bool Reprojection_Dx12::Present()
{
    auto fIndex = GetIndex();

    auto mouseDeltaSinceSim = InputCollection::getInstance().readDelta(_frameCount);
    LOG_DEBUG("mouseDeltaSinceSim: x:{}, y:{}", mouseDeltaSinceSim.x, mouseDeltaSinceSim.y);

    // 1. Dispatch custom UI and Hudless shaders
    if (Config::Instance()->FGDrawUIOverFG.value_or_default())
    {
        auto ui = GetResource(FG_ResourceType::UIColor, fIndex);
        if (ui && (ui->validity != FG_ResourceValidity::ValidNow))
        {
            if (_renderUI.get() == nullptr ||
                Config::Instance()->FGUIPremultipliedAlpha.value_or_default() != _renderUI->IsPreMultipliedAlpha())
            {
                _renderUI = std::make_unique<RUI_Dx12>("RenderUI", _device,
                                                       Config::Instance()->FGUIPremultipliedAlpha.value_or_default());
            }
            else if (_renderUI->IsInit())
            {
                auto commandList = GetSCCommandList(fIndex);
                _renderUI->Dispatch((IDXGISwapChain3*) _swapChain, commandList, ui->GetResource(), ui->state);
            }
        }
    }

    if (_reproject && _reproject->IsInit() && _gameCommandQueue)
    {
        if (auto result = _reproject->ReadGpuTime(_gameCommandQueue))
        {
            LOG_DEBUG("Reprojection GPU time: {} ms", result.value());
        }
    }

    if (IsActive() && !IsPaused())
    {
        auto hudless = GetResource(FG_ResourceType::HudlessColor, fIndex);
        auto depth = GetResource(FG_ResourceType::Depth, fIndex);
        if (hudless && (hudless->validity != FG_ResourceValidity::ValidNow) && depth &&
            (depth->validity != FG_ResourceValidity::ValidNow))
        {
            if (_reproject.get() == nullptr)
            {
                _reproject = std::make_unique<Reproject_Dx12>("Reproject", _device);
            }
            else if (_reproject->IsInit())
            {
                auto commandList = GetSCCommandList(fIndex);
                auto previousIndex = (fIndex + BUFFER_COUNT - 1) % BUFFER_COUNT;

                float diffThreshold = 0.01f;

                auto mouseDeltaSimToSim = InputCollection::getInstance().readSimDelta(_frameCount);

                ReprojectionParams params {};
                FilloutStruct(params, diffThreshold, (uint32_t) _interpolationWidth[fIndex],
                              (uint32_t) _interpolationHeight[fIndex], fIndex, mouseDeltaSinceSim, mouseDeltaSimToSim);

                _reproject->Dispatch((IDXGISwapChain3*) _swapChain, commandList, params, hudless->GetResource(),
                                     hudless->state, depth->GetResource(), depth->state);
            }
        }
    }

    // 2. Execute the populated command lists
    if (_uiCommandListResetted[fIndex])
    {
        if (_uiCommandList[fIndex]->Close() == S_OK)
            _gameCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**) &_uiCommandList[fIndex]);

        _gameCommandQueue->Signal(_uiFence, _uiAllocatorFenceValues[fIndex]);
        _uiCommandListResetted[fIndex] = false;
    }

    if (_scCommandListResetted[fIndex])
    {
        if (_scCommandList[fIndex]->Close() == S_OK)
            _gameCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**) &_scCommandList[fIndex]);

        _scCommandListResetted[fIndex] = false;
    }

    // Returning false usually means "I didn't present, the caller still needs to call original Present",
    // which aligns with standard pass-through logic if you are purely injecting shaders before Present.
    return false;
}

bool Reprojection_Dx12::SetResource(Dx12Resource* inputResource)
{
    if (inputResource == nullptr || inputResource->resource == nullptr)
        return false;

    auto fIndex = inputResource->frameIndex < 0 ? GetIndex() : inputResource->frameIndex;
    auto& type = inputResource->type;

    std::unique_lock<std::shared_mutex> lock(_resourceMutex[fIndex]);

    if (type == FG_ResourceType::HudlessColor)
    {
        if (Config::Instance()->FGDisableHudless.value_or_default())
            return false;

        // Making a copy if it's just valid now to be able to use it later
        if (IsActive() && inputResource->validity == FG_ResourceValidity::ValidNow)
            inputResource->validity = FG_ResourceValidity::ValidButMakeCopy;

        if (!_noHudless[fIndex] && (_frameResources[fIndex][type].validity == FG_ResourceValidity::ValidNow))
        {
            return false;
        }

        if (!_noHudless[fIndex] && Config::Instance()->FGOnlyAcceptFirstHudless.value_or_default() &&
            inputResource->validity != FG_ResourceValidity::UntilPresentFromDispatch)
        {
            return false;
        }
    }
    else if (type == FG_ResourceType::Depth)
    {
        // Making a copy if it's just valid now to be able to use it later
        if (IsActive() && inputResource->validity == FG_ResourceValidity::ValidNow)
            inputResource->validity = FG_ResourceValidity::ValidButMakeCopy;
    }

    auto fResource = &_frameResources[fIndex][type];
    fResource->type = type;
    fResource->state = inputResource->state;
    fResource->validity = inputResource->validity;
    fResource->resource = inputResource->resource;
    fResource->cmdList = inputResource->cmdList;

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

    SetResourceReady(type, fIndex);
    return true;
}

void Reprojection_Dx12::FilloutStruct(ReprojectionParams& params, float diffThreshold, uint32_t resX, uint32_t resY,
                                      int currIndex, DirectX::XMINT2 direction, DirectX::XMINT2 fullFrameMouseDelta)
{
    params.UiDiffThreshold = diffThreshold;
    params.ScreenWidth = resX;
    params.ScreenHeight = resY;

    params.DepthCutoff = Config::Instance()->ReprojectionDepthCutoff.value_or_default();
    params.InvertedDepth = _constants.flags[FG_Flags::InvertedDepth];
    params.ShowStaticElements = State::Instance().fgHudlessCompare;

    auto fillMode = Config::Instance()->ReprojectionFillMode.value_or_default();
    if (fillMode == ReprojectionFill::Black)
        params.EdgeMode = 0;
    else if (fillMode == ReprojectionFill::StrechEdge)
        params.EdgeMode = 1;
    else if (fillMode == ReprojectionFill::Dithering)
        params.EdgeMode = 2;
    else if (fillMode == ReprojectionFill::Noise)
        params.EdgeMode = 3;

    const float tanHalfFovY = std::tan(_cameraVFov[currIndex] * 0.5f);
    const float pixelAngle = 2.0f * std::atan(tanHalfFovY / resY);

    params.TanHalfFovY = tanHalfFovY;
    params.TanHalfFovX = tanHalfFovY * _cameraAspectRatio[currIndex];
    params.InvTanHalfFovY = 1.0f / tanHalfFovY;
    params.InvTanHalfFovX = 1.0f / params.TanHalfFovX;

    const float mouseX = fullFrameMouseDelta.x * pixelAngle;
    const float mouseY = fullFrameMouseDelta.y * pixelAngle;

    // Compute actual camera rotation deltas between frames
    if (!_isFirstFrame)
    {
        const int prevIndex = (currIndex == 0) ? (BUFFER_COUNT - 1) : (currIndex - 1);

        XMVECTOR prevFwd =
            XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraForward[prevIndex])));
        XMVECTOR currFwd =
            XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraForward[currIndex])));

        XMFLOAT3 p, c;
        XMStoreFloat3(&p, prevFwd);
        XMStoreFloat3(&c, currFwd);

        // Yaw delta (wrapped to [-pi, pi])
        float camYawDelta = std::atan2(c.y, c.x) - std::atan2(p.y, p.x);

        if (camYawDelta > std::numbers::pi_v<float>)
            camYawDelta -= 2.0f * std::numbers::pi_v<float>;

        if (camYawDelta < -std::numbers::pi_v<float>)
            camYawDelta += 2.0f * std::numbers::pi_v<float>;

        // Pitch delta
        const float camPitchDelta = std::asin(std::clamp(c.z, -1.0f, 1.0f)) - std::asin(std::clamp(p.z, -1.0f, 1.0f));

        _calibration.Update(mouseX, mouseY, camYawDelta, camPitchDelta);
    }
    _isFirstFrame = false;

    // Transform mouse input using calibrated coefficients
    const float curMouseX = direction.x * pixelAngle;
    const float curMouseY = direction.y * pixelAngle;

    const float yaw = curMouseX * _calibration.yawFromX + curMouseY * _calibration.yawFromY;
    const float pitch = curMouseX * _calibration.pitchFromX + curMouseY * _calibration.pitchFromY;

    // Compute camera reprojection matrix
    XMVECTOR camRight = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraRight[currIndex])));
    XMVECTOR camUp = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraUp[currIndex])));
    XMVECTOR camForward =
        XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraForward[currIndex])));

    XMMATRIX viewToWorld = XMMATRIX(camRight, camUp, camForward, XMVectorSet(0, 0, 0, 1));
    XMMATRIX worldToView = XMMatrixTranspose(viewToWorld);

    constexpr float TEST_ANGLE = 0.001f;

    // Find which matrix rotation direction corresponds to positive camera motion.
    XMVECTOR testPitchForward =
        XMVector3Normalize(XMVector3TransformNormal(camForward, XMMatrixRotationAxis(camRight, TEST_ANGLE)));

    float pitchTest = std::asin(std::clamp(XMVectorGetZ(testPitchForward), -1.0f, 1.0f)) -
                      std::asin(std::clamp(XMVectorGetZ(camForward), -1.0f, 1.0f));

    int pitchSign = (pitchTest >= 0.0f) ? 1 : -1;

    XMVECTOR testYawForward = XMVector3Normalize(
        XMVector3TransformNormal(camForward, XMMatrixRotationAxis(XMVectorSet(0, 0, 1, 0), TEST_ANGLE)));

    float yawTest = std::atan2(XMVectorGetY(testYawForward), XMVectorGetX(testYawForward)) -
                    std::atan2(XMVectorGetY(camForward), XMVectorGetX(camForward));

    if (yawTest > std::numbers::pi_v<float>)
        yawTest -= 2.0f * std::numbers::pi_v<float>;

    if (yawTest < -std::numbers::pi_v<float>)
        yawTest += 2.0f * std::numbers::pi_v<float>;

    int yawSign = (std::abs(yawTest) >= 1e-8f && yawTest >= 0.0f) ? 1 : -1;

    XMMATRIX rotPitch = XMMatrixRotationAxis(camRight, pitch * pitchSign);
    XMMATRIX rotYaw = XMMatrixRotationAxis(XMVectorSet(0, 0, 1, 0), yaw * yawSign);

    XMMATRIX rotation = XMMatrixTranspose(viewToWorld * rotPitch * rotYaw * worldToView);

    XMStoreFloat4(&params.ReprojectionRow0, rotation.r[0]);
    XMStoreFloat4(&params.ReprojectionRow1, rotation.r[1]);
    XMStoreFloat4(&params.ReprojectionRow2, rotation.r[2]);
}

void Reprojection_Dx12::SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue) { _gameCommandQueue = queue; }

void* Reprojection_Dx12::FrameGenerationContext() { return nullptr; }

void* Reprojection_Dx12::SwapchainContext() { return nullptr; }