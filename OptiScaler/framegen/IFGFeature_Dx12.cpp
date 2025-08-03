#include "IFGFeature_Dx12.h"

#include <State.h>
#include <Config.h>

bool IFGFeature_Dx12::CreateBufferResourceWithSize(ID3D12Device* device, ID3D12Resource* source,
                                                   D3D12_RESOURCE_STATES state, ID3D12Resource** target, UINT width,
                                                   UINT height, bool UAV, bool depth)
{
    if (device == nullptr || source == nullptr)
        return false;

    auto inDesc = source->GetDesc();

    if (UAV)
        inDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (depth)
        inDesc.Format = DXGI_FORMAT_R32_FLOAT;

    if (*target != nullptr)
    {
        auto bufDesc = (*target)->GetDesc();

        if (bufDesc.Width != width || bufDesc.Height != height || bufDesc.Format != inDesc.Format ||
            bufDesc.Flags != inDesc.Flags)
        {
            (*target)->Release();
            (*target) = nullptr;
        }
        else
        {
            return true;
        }
    }

    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags;
    HRESULT hr = source->GetHeapProperties(&heapProperties, &heapFlags);

    if (hr != S_OK)
    {
        LOG_ERROR("GetHeapProperties result: {:X}", (UINT64) hr);
        return false;
    }

    inDesc.Width = width;
    inDesc.Height = height;

    hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &inDesc, state, nullptr,
                                         IID_PPV_ARGS(target));

    if (hr != S_OK)
    {
        LOG_ERROR("CreateCommittedResource result: {:X}", (UINT64) hr);
        return false;
    }

    LOG_DEBUG("Created new one: {}x{}", inDesc.Width, inDesc.Height);

    return true;
}

bool IFGFeature_Dx12::CreateBufferResource(ID3D12Device* device, ID3D12Resource* source,
                                           D3D12_RESOURCE_STATES initialState, ID3D12Resource** target, bool UAV,
                                           bool depth)
{
    if (device == nullptr || source == nullptr)
        return false;

    auto inDesc = source->GetDesc();

    if (UAV)
        inDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (depth)
        inDesc.Format = DXGI_FORMAT_R32_FLOAT;

    if (*target != nullptr)
    {
        auto bufDesc = (*target)->GetDesc();

        if (bufDesc.Width != inDesc.Width || bufDesc.Height != inDesc.Height || bufDesc.Format != inDesc.Format ||
            bufDesc.Flags != inDesc.Flags)
        {
            (*target)->Release();
            (*target) = nullptr;
        }
        else
        {
            return true;
        }
    }

    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags;
    HRESULT hr = source->GetHeapProperties(&heapProperties, &heapFlags);

    if (hr != S_OK)
    {
        LOG_ERROR("GetHeapProperties result: {:X}", (UINT64) hr);
        return false;
    }

    hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &inDesc, initialState, nullptr,
                                         IID_PPV_ARGS(target));

    if (hr != S_OK)
    {
        LOG_ERROR("CreateCommittedResource result: {:X}", (UINT64) hr);
        return false;
    }

    LOG_DEBUG("Created new one: {}x{}", inDesc.Width, inDesc.Height);

    return true;
}

void IFGFeature_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
                                      D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

bool IFGFeature_Dx12::CopyResource(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* source, ID3D12Resource** target,
                                   D3D12_RESOURCE_STATES sourceState)
{
    auto result = true;

    ResourceBarrier(cmdList, source, sourceState, D3D12_RESOURCE_STATE_COPY_SOURCE);

    if (CreateBufferResource(State::Instance().currentD3D12Device, source, D3D12_RESOURCE_STATE_COPY_DEST, target))
        cmdList->CopyResource(*target, source);
    else
        result = false;

    ResourceBarrier(cmdList, source, D3D12_RESOURCE_STATE_COPY_SOURCE, sourceState);

    return result;
}

void IFGFeature_Dx12::SetVelocity(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* velocity,
                                  D3D12_RESOURCE_STATES state)
{
    auto index = GetIndex();

    if (cmdList == nullptr)
        return;

    velocity->SetName(std::format(L"VelocityResource_{}", index).c_str());
    _paramVelocity[index].resource = velocity;
    _paramVelocity[index].setState(state);

    _paramVelocityCopy[index].setState(D3D12_RESOURCE_STATE_COPY_DEST);

    if (State::Instance().activeFgInput == FGInput::Upscaler && Config::Instance()->FGResourceFlip.value_or_default() &&
        _device != nullptr &&
        CreateBufferResource(_device, velocity, _paramVelocityCopy[index].getState(),
                             &_paramVelocityCopy[index].resource, true, false))
    {
        if (_mvFlip.get() == nullptr)
        {
            _mvFlip = std::make_unique<RF_Dx12>("VelocityFlip", _device);
            return;
        }

        if (_mvFlip->IsInit())
        {
            ResourceBarrier(cmdList, _paramVelocityCopy[index].resource, _paramVelocityCopy->getState(),
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            auto feature = State::Instance().currentFeature;
            UINT width = feature->LowResMV() ? feature->RenderWidth() : feature->DisplayWidth();
            UINT height = feature->LowResMV() ? feature->RenderHeight() : feature->DisplayHeight();
            auto result =
                _mvFlip->Dispatch(_device, cmdList, velocity, _paramVelocityCopy[index].resource, width, height, true);

            ResourceBarrier(cmdList, _paramVelocityCopy[index].resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            _paramVelocityCopy->getState());

            if (result)
            {
                LOG_TRACE("Setting velocity from flip, index: {}", index);
                _paramVelocity[index] = _paramVelocityCopy[index];
            }
        }

        return;
    }

    if (Config::Instance()->FGMakeMVCopy.value_or_default() &&
        CopyResource(cmdList, _paramVelocity[index].resource, &_paramVelocityCopy[index].resource,
                     _paramVelocity[index].getState()))
    {
        auto name = std::format(L"VelocityCopyResource_{}", index);
        _paramVelocityCopy[index].resource->SetName(name.c_str());
        _paramVelocity[index] = _paramVelocityCopy[index];
    }

    LOG_TRACE("Setting velocity, index: {}", index);
}

void IFGFeature_Dx12::SetDepth(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth, D3D12_RESOURCE_STATES state)
{
    auto index = GetIndex();

    if (cmdList == nullptr)
        return;

    // TODO: hack for DRG
    // if (state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    //    state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    depth->SetName(std::format(L"DepthResource_{}", index).c_str());
    _paramDepth[index].resource = depth;
    _paramDepth[index].setState(state);

    _paramDepthCopy[index].setState(D3D12_RESOURCE_STATE_COPY_DEST);

    if (Config::Instance()->FGResourceFlip.value_or_default() && _device != nullptr)
    {
        if (!CreateBufferResource(_device, depth, _paramDepthCopy[index].getState(), &_paramDepthCopy[index].resource,
                                  true, true))
            return;

        if (_depthFlip.get() == nullptr)
        {
            _depthFlip = std::make_unique<RF_Dx12>("DepthFlip", _device);
            return;
        }

        if (_depthFlip->IsInit())
        {
            ResourceBarrier(cmdList, _paramDepthCopy[index].resource, _paramDepthCopy[index].getState(),
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            auto feature = State::Instance().currentFeature;
            auto result = _depthFlip->Dispatch(_device, cmdList, depth, _paramDepthCopy[index].resource,
                                               feature->RenderWidth(), feature->RenderHeight(), false);

            ResourceBarrier(cmdList, _paramDepthCopy[index].resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            _paramDepthCopy[index].getState());

            if (result)
            {
                LOG_TRACE("Setting depth from flip, index: {}", index);
                _paramDepth[index] = _paramDepthCopy[index];
            }
        }

        return;
    }

    if (Config::Instance()->FGMakeDepthCopy.value_or_default() &&
        CopyResource(cmdList, _paramDepth[index].resource, &_paramDepthCopy[index].resource,
                     _paramDepth[index].getState()))
    {
        auto name = std::format(L"DepthCopyResource_{}", index);
        _paramDepthCopy[index].resource->SetName(name.c_str());
        _paramDepth[index] = _paramDepthCopy[index];
    }

    LOG_TRACE("Setting depth, index: {}", index);
}

void IFGFeature_Dx12::SetHudless(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* hudless,
                                 D3D12_RESOURCE_STATES state, bool makeCopy)
{
    auto index = GetIndex();
    LOG_TRACE("Setting hudless, index: {}, Resource: {:X}, CmdList: {:X}", index, (size_t) hudless, (size_t) cmdList);

    hudless->SetName(std::format(L"HudlessResource_{}", index).c_str());

    if (cmdList == nullptr || !makeCopy)
    {
        _paramHudless[index].resource = hudless;
        _paramHudless[index].setState(state);
        return;
    }

    if (makeCopy && CopyResource(cmdList, hudless, &_paramHudlessCopy[index].resource, state))
    {
        _paramHudless[index].resource = _paramHudlessCopy[index].resource;
        _paramHudless[index].setState(D3D12_RESOURCE_STATE_COPY_DEST);
        _paramHudlessCopy[index].resource->SetName(std::format(L"HudlessCopyResource_{}", index).c_str());
    }
    else
    {
        _paramHudless[index].resource = hudless;
        _paramHudless[index].setState(state);
    }
}

void IFGFeature_Dx12::SetUI(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* ui, D3D12_RESOURCE_STATES state,
                            bool makeCopy)
{
    auto index = GetIndex();
    LOG_TRACE("Setting ui, index: {}, Resource: {:X}, CmdList: {:X}", index, (size_t) ui, (size_t) cmdList);

    ui->SetName(std::format(L"UiResource_{}", index).c_str());

    if (cmdList == nullptr || !makeCopy)
    {
        _paramUi[index].resource = ui;
        _paramUi[index].setState(state);
        return;
    }

    if (makeCopy && CopyResource(cmdList, ui, &_paramUiCopy[index].resource, state))
    {
        _paramUi[index].resource = _paramUiCopy[index].resource;
        _paramUi[index].setState(D3D12_RESOURCE_STATE_COPY_DEST);
        _paramUiCopy[index].resource->SetName(std::format(L"UiCopyResource_{}", index).c_str());
    }
    else
    {
        _paramUi[index].resource = ui;
        _paramUi[index].setState(state);
    }
}

void IFGFeature_Dx12::CreateObjects(ID3D12Device* InDevice)
{
    _device = InDevice;

    if (_commandAllocators[0] != nullptr)
        ReleaseObjects();

    LOG_DEBUG("");

    do
    {
        HRESULT result;

        for (size_t i = 0; i < BUFFER_COUNT; i++)
        {
            ID3D12CommandAllocator* allocator = nullptr;
            result = InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandAllocators _commandAllocators[{}]: {:X}", i, (unsigned long) result);
                break;
            }
            allocator->SetName(std::format(L"_commandAllocator_{}", i).c_str());
            if (!CheckForRealObject(__FUNCTION__, allocator, (IUnknown**) &_commandAllocators[i]))
                _commandAllocators[i] = allocator;

            ID3D12GraphicsCommandList* cmdList = nullptr;
            result = InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocators[i], NULL,
                                                 IID_PPV_ARGS(&cmdList));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandList _hudlessCommandList[{}]: {:X}", i, (unsigned long) result);
                break;
            }
            cmdList->SetName(std::format(L"_hudlessCommandList_{}", i).c_str());
            if (!CheckForRealObject(__FUNCTION__, cmdList, (IUnknown**) &_commandList[i]))
                _commandList[i] = cmdList;

            result = _commandList[i]->Close();
            if (result != S_OK)
            {
                LOG_ERROR("_hudlessCommandList[{}]->Close: {:X}", i, (unsigned long) result);
                break;
            }
        }

    } while (false);
}

void IFGFeature_Dx12::ReleaseObjects()
{
    LOG_DEBUG("");

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (_commandAllocators[i] != nullptr)
        {
            _commandAllocators[i]->Release();
            _commandAllocators[i] = nullptr;
        }

        if (_commandList[i] != nullptr)
        {
            _commandList[i]->Release();
            _commandList[i] = nullptr;
        }
    }

    _mvFlip.reset();
    _depthFlip.reset();
}

ID3D12CommandList* IFGFeature_Dx12::GetCommandList() { return _commandList[GetIndex()]; }

bool IFGFeature_Dx12::NoHudless() { return _noHudless[GetIndex()]; }

void IFGFeature_Dx12::SetVelocityReady() { _velocityReady[GetIndex()] = true; }

void IFGFeature_Dx12::SetDepthReady() { _depthReady[GetIndex()] = true; }

void IFGFeature_Dx12::SetHudlessReady() { _hudlessReady[GetIndex()] = true; }

void IFGFeature_Dx12::SetUIReady() { _uiReady[GetIndex()] = true; }

void IFGFeature_Dx12::SetHudlessDispatchReady() { _hudlessDispatchReady[GetIndex()] = true; }

void IFGFeature_Dx12::Present()
{
    auto fIndex = LastDispatchedFrame() % BUFFER_COUNT;
    _velocityReady[fIndex] = false;
    _depthReady[fIndex] = false;
    _hudlessReady[fIndex] = false;
    _uiReady[fIndex] = false;
    _hudlessDispatchReady[fIndex] = false;
}

bool IFGFeature_Dx12::UpscalerInputsReady() { return _velocityReady[GetIndex()] && _depthReady[GetIndex()]; }
bool IFGFeature_Dx12::HudlessReady() { return _hudlessReady[GetIndex()]; }

// Only makes sense for upscaler inputs because the list of buffers
// we want ready might differ depending on FG inputs and what the game provides
bool IFGFeature_Dx12::ReadyForExecute()
{
    auto fIndex = GetIndex();
    return _velocityReady[fIndex] && _depthReady[fIndex] && _hudlessReady[fIndex];
}
