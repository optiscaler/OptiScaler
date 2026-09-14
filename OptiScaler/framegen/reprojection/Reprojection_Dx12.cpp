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
                float pink = 0.0f;

                auto presentToPresentMouseDelta = InputCollection::getInstance().readPresentDelta();

                ReprojectionParams params {};
                FilloutStruct(params, diffThreshold, pink, (float) _interpolationWidth[fIndex],
                              (float) _interpolationHeight[fIndex], fIndex,
                              { (float) mouseDeltaSinceSim.x, (float) mouseDeltaSinceSim.y },
                              { (float) presentToPresentMouseDelta.x, (float) presentToPresentMouseDelta.y });

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

void Reprojection_Dx12::FilloutStruct(ReprojectionParams& params, float diffThreshold, float pinkAmount, float resX,
                                      float resY, int currIndex, DirectX::XMFLOAT2 direction,
                                      DirectX::XMFLOAT2 fullFrameMouseDelta)
{
    using namespace DirectX;

    params.DiffThreshold = diffThreshold;
    params.PinkAmount = pinkAmount;
    params.MouseDeltaX = -direction.x;
    params.MouseDeltaY = -direction.y;
    params.ScreenWidth = resX;
    params.ScreenHeight = resY;

    params.CameraVFov = _cameraVFov[currIndex];
    params.CameraAspectRatio = _cameraAspectRatio[currIndex];
    params.EdgeMode = Config::Instance()->ReprojectionFillMode.value_or_default() == ReprojectionFill::StrechEdge;

    const float tanHalfFovY = std::tan(params.CameraVFov * 0.5f);
    const float pixelAngle = 2.0f * std::atan(tanHalfFovY / resY);

    params.TanHalfFovY = tanHalfFovY;
    params.InvTanHalfFovY = 1.0f / tanHalfFovY;
    params.InvTanHalfFovX = 1.0f / (tanHalfFovY * params.CameraAspectRatio);

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

    XMMATRIX rotPitch = XMMatrixRotationAxis(camRight, -pitch);
    XMMATRIX rotYaw = XMMatrixRotationAxis(XMVectorSet(0, 0, 1, 0), yaw);

    XMMATRIX rotation = XMMatrixTranspose(viewToWorld * rotPitch * rotYaw * worldToView);

    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, rotation);

    params.ReprojectionRow0 = XMFLOAT3(m._11, m._12, m._13);
    params.ReprojectionRow1 = XMFLOAT3(m._21, m._22, m._23);
    params.ReprojectionRow2 = XMFLOAT3(m._31, m._32, m._33);
}

void Reprojection_Dx12::SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue) { _gameCommandQueue = queue; }

void* Reprojection_Dx12::FrameGenerationContext() { return nullptr; }

void* Reprojection_Dx12::SwapchainContext() { return nullptr; }