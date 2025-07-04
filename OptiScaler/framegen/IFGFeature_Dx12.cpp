#include "IFGFeature_Dx12.h"

#include <State.h>
#include <Config.h>

bool IFGFeature_Dx12::CreateBufferResource(ID3D12Device* device, ID3D12Resource* source, D3D12_RESOURCE_STATES state,
                                           ID3D12Resource** target, bool UAV, bool depth)
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

void IFGFeature_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
                                      D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = 0;
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
    LOG_TRACE("Setting velocity, index: {}", index);

    if (cmdList == nullptr)
        return;

    _paramVelocity[index] = velocity;

    if (Config::Instance()->FGResourceFlip.value_or_default() && _device != nullptr &&
        CreateBufferResource(_device, velocity, D3D12_RESOURCE_STATE_COPY_DEST, &_paramVelocityCopy[index], true,
                             false))
    {
        if (_mvFlip.get() == nullptr)
        {
            _mvFlip = std::make_unique<RF_Dx12>("VelocityFlip", _device);
            return;
        }

        if (_mvFlip->IsInit())
        {
            ResourceBarrier(cmdList, _paramVelocityCopy[index], D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            auto feature = State::Instance().currentFeature;
            UINT width = feature->LowResMV() ? feature->RenderWidth() : feature->DisplayWidth();
            UINT height = feature->LowResMV() ? feature->RenderHeight() : feature->DisplayHeight();
            auto result = _mvFlip->Dispatch(_device, cmdList, velocity, _paramVelocityCopy[index], width, height, true);

            ResourceBarrier(cmdList, _paramVelocityCopy[index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);

            if (result)
                _paramVelocity[index] = _paramVelocityCopy[index];
        }

        return;
    }

    if (Config::Instance()->FGMakeMVCopy.value_or_default() &&
        CopyResource(cmdList, velocity, &_paramVelocityCopy[index], state))
    {
        _paramVelocity[index] = _paramVelocityCopy[index];
        return;
    }
}

void IFGFeature_Dx12::SetDepth(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth, D3D12_RESOURCE_STATES state)
{
    auto index = GetIndex();
    LOG_TRACE("Setting depth, index: {}", index);

    if (cmdList == nullptr)
        return;

    _paramDepth[index] = depth;

    if (Config::Instance()->FGResourceFlip.value_or_default() && _device != nullptr)
    {
        if (!CreateBufferResource(_device, depth, D3D12_RESOURCE_STATE_COPY_DEST, &_paramDepthCopy[index], true, true))
            return;

        if (_depthFlip.get() == nullptr)
        {
            _depthFlip = std::make_unique<RF_Dx12>("DepthFlip", _device);
            return;
        }

        if (_depthFlip->IsInit())
        {
            ResourceBarrier(cmdList, _paramDepthCopy[index], D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            auto feature = State::Instance().currentFeature;
            auto result = _depthFlip->Dispatch(_device, cmdList, depth, _paramDepthCopy[index], feature->RenderWidth(),
                                               feature->RenderHeight(), false);

            ResourceBarrier(cmdList, _paramDepthCopy[index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);

            if (result)
                _paramDepth[index] = _paramDepthCopy[index];
        }

        return;
    }

    if (Config::Instance()->FGMakeDepthCopy.value_or_default() &&
        CopyResource(cmdList, depth, &_paramDepthCopy[index], state))
    {
        _paramDepth[index] = _paramDepthCopy[index];
    }
}

void IFGFeature_Dx12::SetHudless(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* hudless,
                                 D3D12_RESOURCE_STATES state, bool makeCopy)
{
    auto index = GetIndex();
    LOG_TRACE("Setting hudless, index: {}, resource: {:X}", index, (size_t) hudless);

    if (cmdList == nullptr && !makeCopy)
    {
        _paramHudless[index] = hudless;
        SetHudlessReady();
        return;
    }

    if (makeCopy && CopyResource(cmdList, hudless, &_paramHudlessCopy[index], state))
        _paramHudless[index] = _paramHudlessCopy[index];
    else
        _paramHudless[index] = hudless;

    SetHudlessReady();
}

void IFGFeature_Dx12::CreateObjects(ID3D12Device* InDevice)
{
    if (_commandAllocators[0] != nullptr)
        return;

    _device = InDevice;

    LOG_DEBUG("");

    do
    {
        HRESULT result;

        for (size_t i = 0; i < BUFFER_COUNT; i++)
        {
            Util::ComPtr<ID3D12CommandAllocator> allocator;
            result = InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      IID_OUTPTR_ARGS(std::out_ptr(allocator)));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandAllocators _commandAllocators[{}]: {:X}", i, (unsigned long) result);
                break;
            }
            allocator->SetName(L"_commandAllocator");
            _commandAllocators[i] = CheckForRealObject(__FUNCTION__, std::move(allocator));

            Util::ComPtr<ID3D12GraphicsCommandList> cmdList;
            result = InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocators[i].get(), NULL,
                                                 IID_OUTPTR_ARGS(std::out_ptr(cmdList)));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandList _commandList[{}]: {:X}", i, (unsigned long) result);
                break;
            }
            cmdList->SetName(L"_commandList");
            _commandList[i] = CheckForRealObject(__FUNCTION__, std::move(cmdList));

            result = _commandList[i]->Close();
            if (result != S_OK)
            {
                LOG_ERROR("_commandList[{}]->Close: {:X}", i, (unsigned long) result);
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
        _commandAllocators[i].reset();
        _commandList[i].reset();
    }

    _mvFlip.reset();
    _depthFlip.reset();
}

bool IFGFeature_Dx12::IsFGCommandList(void* cmdList)
{
    auto found = false;

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (_commandList[i].get() == cmdList)
        {
            found = true;
            break;
        }
    }

    return found;
}

bool IFGFeature_Dx12::ExecuteHudlessCmdList()
{
    if (!_hudlessDispatchReady)
        return false;

    _mvAndDepthReady = false;
    _hudlessReady = false;
    _hudlessDispatchReady = false;

    auto fIndex = GetIndex();
    auto result = _commandList[fIndex]->Close();

    LOG_DEBUG("_commandList[{}]->Close() result: {:X}", fIndex, (UINT) result);

    if (result == S_OK)
    {
        ID3D12CommandList* cl[] = { cl[0] = _commandList[fIndex].get() };
        _gameCommandQueue->ExecuteCommandLists(1, cl);

        return true;
    }

    return false;
}

void IFGFeature_Dx12::SetVelocityAndDepthReady() { _mvAndDepthReady = true; }

void IFGFeature_Dx12::SetHudlessReady() { _hudlessReady = true; }

void IFGFeature_Dx12::SetHudlessDispatchReady() { _hudlessDispatchReady = true; }

void IFGFeature_Dx12::Present()
{
    if (!_mvAndDepthReady)
    {
        _mvAndDepthReady = false;
        _hudlessReady = false;
        _hudlessDispatchReady = false;
        return;
    }

    auto hudless = _hudlessReady;
    _mvAndDepthReady = false;
    _hudlessReady = false;
    _hudlessDispatchReady = false;

    DispatchHudless(hudless, State::Instance().lastFrameTime);
}

bool IFGFeature_Dx12::UpscalerInputsReady() { return _mvAndDepthReady; }
bool IFGFeature_Dx12::HudlessReady() { return _hudlessReady; }
bool IFGFeature_Dx12::ReadyForExecute() { return _mvAndDepthReady && _hudlessReady; }
