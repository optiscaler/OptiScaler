#include "pch.h"
#include "FlipMeterSwapChain.h"

#include <Util.h>
#include <low_latency/input/input_common.h>
#include <hooks/Amdxc64_Hooks.h>
#include <misc/IdentifyGpu.h>

#include <SetupAPI.h>
#include <devguid.h>

#include <fstream>

#pragma comment(lib, "setupapi.lib")

using Microsoft::WRL::ComPtr;

// The window's position is only looked at again every so many presents, it can change without a resize
constexpr UINT64 MeteringCheckInterval = 30;

static INT64 QpcNow()
{
    LARGE_INTEGER now {};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

struct DriverKey
{
    std::wstring key;  // Under the display class, like {4d36e968-e325-11ce-bfc1-08002be10318}\0000
    std::wstring name; // The device's description
};

// The adapter's display class key, found by matching its PCI location with the display devices'
static std::expected<DriverKey, std::string> FindDriverKey(LUID luid)
{
    D3DKMT_ADAPTERADDRESS address {};
    const auto status = IdentifyGpu::queryAdapterInfo(luid, KMTQAITYPE_ADAPTERADDRESS, &address, sizeof(address));

    if (status != 0)
        return std::unexpected(std::format("Couldn't get the GPU's PCI location: {:X}", (UINT) status));

    auto devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE)
        return std::unexpected("Couldn't list the display devices");

    std::expected<DriverKey, std::string> result =
        std::unexpected(std::format("No display device at PCI bus {}, device {}, function {}", address.BusNumber,
                                    address.DeviceNumber, address.FunctionNumber));

    SP_DEVINFO_DATA device { sizeof(SP_DEVINFO_DATA) };

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devices, i, &device); i++)
    {
        DWORD bus = 0;
        DWORD pciAddress = 0;

        if (!SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_BUSNUMBER, nullptr, (BYTE*) &bus, sizeof(bus),
                                               nullptr) ||
            !SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_ADDRESS, nullptr, (BYTE*) &pciAddress,
                                               sizeof(pciAddress), nullptr))
        {
            continue;
        }

        // A PCI device's address has its device number in the high word and its function in the low one
        if (bus != address.BusNumber || (pciAddress >> 16) != address.DeviceNumber ||
            (pciAddress & 0xFFFF) != address.FunctionNumber)
        {
            continue;
        }

        wchar_t key[256] {};
        wchar_t name[256] {};

        if (!SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_DRIVER, nullptr, (BYTE*) key, sizeof(key) - 2,
                                               nullptr))
        {
            result = std::unexpected("The GPU has no driver key");
            break;
        }

        SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_DEVICEDESC, nullptr, (BYTE*) name, sizeof(name) - 2,
                                          nullptr);

        result = DriverKey { key, name };
        break;
    }

    SetupDiDestroyDeviceInfoList(devices);

    return result;
}

std::expected<std::filesystem::path, std::string>
FlipMeterSwapChain::WriteMaxGenFramesRegFile(UINT generatedFrames) const
{
    auto driverKey = FindDriverKey(_device->GetAdapterLuid());

    if (!driverKey)
        return std::unexpected(driverKey.error());

    const auto path = Util::ExePath().parent_path() / L"OptiScaler_FlipMeterMaxGenFrames.reg";
    std::ofstream file(path, std::ios::trunc);

    if (!file)
        return std::unexpected(std::format("Couldn't write {}", wstring_to_string(path.wstring())));

    file << "Windows Registry Editor Version 5.00\n\n"
         << "; Made by OptiScaler for " << wstring_to_string(driverKey->name) << ".\n"
         << "; Lets AMD's driver flip metering pace up to " << generatedFrames << " generated frames per game frame ("
         << generatedFrames + 1 << "X)\n"
         << "; for OptiScaler's Flip Meter output, the driver's default is 1 (2X). Restart Windows after adding it.\n"
         << "; To undo, change the value line to: \"Gmx_DriverFramePacingMaxGenFrames\"=-\n\n"
         << "[HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\" << wstring_to_string(driverKey->key)
         << "]\n"
         << std::format("\"Gmx_DriverFramePacingMaxGenFrames\"=dword:{:08x}\n", generatedFrames);

    if (!file)
        return std::unexpected(std::format("Couldn't write {}", wstring_to_string(path.wstring())));

    LOG_INFO("Wrote {} for {}, key {}", wstring_to_string(path.wstring()), wstring_to_string(driverKey->name),
             wstring_to_string(driverKey->key));

    return path;
}

ComPtr<ID3D12CommandQueue> FlipMeterSwapChain::CreatePresentQueue(ID3D12CommandQueue* gameQueue)
{
    ComPtr<ID3D12Device> device;
    if (gameQueue == nullptr || FAILED(gameQueue->GetDevice(IID_PPV_ARGS(&device))))
        return nullptr;

    D3D12_COMMAND_QUEUE_DESC desc {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
    desc.NodeMask = gameQueue->GetDesc().NodeMask;

    ComPtr<ID3D12CommandQueue> queue;
    auto result = device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue));

    if (FAILED(result))
    {
        LOG_ERROR("Present queue creation failed: {:X}", (UINT) result);
        return nullptr;
    }

    queue->SetName(L"FlipMeterSwapChain present queue");

    return queue;
}

FlipMeterSwapChain* FlipMeterSwapChain::Create(IDXGISwapChain* real, ID3D12CommandQueue* queue,
                                               ID3D12CommandQueue* presentQueue, HWND hwnd, UINT gameBufferCount)
{
    if (real == nullptr || queue == nullptr || presentQueue == nullptr)
        return nullptr;

    ComPtr<ID3D12Device> device;
    if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))))
        return nullptr;

    IAmdExtFlipMeter* flipMeter = nullptr;
    auto result = Amdxc64Hooks::CreateFlipMeter(device.Get(), real, &flipMeter);

    if (FAILED(result) || flipMeter == nullptr)
    {
        LOG_WARN("No flip meter for the swapchain: {:X}", (UINT) result);
        return nullptr;
    }

    AmdExtFlipMeterCaps caps {};
    caps.status = AmdExtFlipMeterStatus_NotAvailable;
    result = flipMeter->GetCaps(0, &caps, sizeof(caps));

    LOG_INFO("Flip meter caps: modes {:X}, max generated frames {}, status {}, result {:X}", caps.supportedModes,
             caps.maxGeneratedFrames, caps.status, (UINT) result);

    const bool supported = SUCCEEDED(result) && (caps.supportedModes & AmdExtFlipMeterCaps_Adaptive) != 0 &&
                           (caps.supportedModes & AmdExtFlipMeterCaps_DoNotEnable) == 0 && caps.maxGeneratedFrames > 0;

    if (!supported)
    {
        flipMeter->Release();
        return nullptr;
    }

    auto swapChain = new FlipMeterSwapChain();
    swapChain->_real = real;
    swapChain->_queue = queue;
    swapChain->_presentQueue = presentQueue;
    swapChain->_device = device.Get();
    swapChain->_hwnd = hwnd;
    swapChain->_gameBufferCount = gameBufferCount;
    swapChain->_flipMeter = flipMeter;
    swapChain->_caps = caps;

    queue->AddRef();
    presentQueue->AddRef();
    device->AddRef();

    // Same object, they need no references of their own
    if (SUCCEEDED(real->QueryInterface(IID_PPV_ARGS(&swapChain->_real1))))
        swapChain->_real1->Release();

    if (SUCCEEDED(real->QueryInterface(IID_PPV_ARGS(&swapChain->_real2))))
        swapChain->_real2->Release();

    if (SUCCEEDED(real->QueryInterface(IID_PPV_ARGS(&swapChain->_real3))))
        swapChain->_real3->Release();

    if (SUCCEEDED(real->QueryInterface(IID_PPV_ARGS(&swapChain->_real4))))
        swapChain->_real4->Release();

    LARGE_INTEGER frequency {};
    QueryPerformanceFrequency(&frequency);
    swapChain->_qpcFrequency = frequency.QuadPart;

    DXGI_SWAP_CHAIN_DESC realDesc {};
    if (SUCCEEDED(real->GetDesc(&realDesc)))
        swapChain->_realBufferCount = realDesc.BufferCount;

    if (swapChain->_real3 == nullptr || !swapChain->CreateObjects() || !swapChain->CreateBuffers())
    {
        LOG_ERROR("Couldn't create the flip meter swapchain's objects");

        // The caller keeps its swapchain
        swapChain->Shutdown();
        swapChain->Release();
        return nullptr;
    }

    swapChain->_presenter = std::thread(&FlipMeterSwapChain::PresenterLoop, swapChain);

    LOG_INFO("Flip meter swapchain created, real: {:X}, game buffers: {}", (size_t) real, gameBufferCount);

    return swapChain;
}

static void WaitForFence(ID3D12Fence* fence, UINT64 value, HANDLE event)
{
    if (fence->GetCompletedValue() < value && SUCCEEDED(fence->SetEventOnCompletion(value, event)))
        WaitForSingleObject(event, 5000);
}

bool FlipMeterSwapChain::CopyContext::Create(ID3D12Device* device, const wchar_t* name)
{
    for (auto& commandAllocator : allocators)
    {
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator))))
            return false;

        commandAllocator->SetName(name);
    }

    if (FAILED(
            device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[0], nullptr, IID_PPV_ARGS(&list))))
    {
        return false;
    }

    list->SetName(name);
    list->Close();

    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
        return false;

    event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    return event != nullptr;
}

void FlipMeterSwapChain::CopyContext::Release()
{
    if (list != nullptr)
        list->Release();

    for (auto& commandAllocator : allocators)
    {
        if (commandAllocator != nullptr)
            commandAllocator->Release();
    }

    if (fence != nullptr)
        fence->Release();

    if (event != nullptr)
        CloseHandle(event);

    *this = {};
}

bool FlipMeterSwapChain::CopyContext::Begin()
{
    WaitForFence(fence, allocatorValues[allocator], event);
    return SUCCEEDED(allocators[allocator]->Reset());
}

UINT64 FlipMeterSwapChain::CopyContext::End(ID3D12CommandQueue* queue)
{
    allocatorValues[allocator] = ++value;
    queue->Signal(fence, value);
    allocator = (allocator + 1) % BUFFER_COUNT;

    return value;
}

bool FlipMeterSwapChain::CreateObjects()
{
    _timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    _readyEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    return _readyEvent != nullptr && _gameCopies.Create(_device, L"FlipMeterSwapChain game copies") &&
           _presenterCopies.Create(_device, L"FlipMeterSwapChain presenter copies");
}

bool FlipMeterSwapChain::CreateBuffers()
{
    DXGI_SWAP_CHAIN_DESC desc {};
    if (FAILED(_real->GetDesc(&desc)))
        return false;

    D3D12_RESOURCE_DESC bufferDesc {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    bufferDesc.Width = desc.BufferDesc.Width;
    bufferDesc.Height = desc.BufferDesc.Height;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = desc.BufferDesc.Format;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    _buffers.assign(_gameBufferCount, nullptr);
    _index = 0;

    for (UINT i = 0; i < _gameBufferCount; i++)
    {
        auto result =
            _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_PRESENT,
                                             nullptr, IID_PPV_ARGS(&_buffers[i]));

        if (FAILED(result))
        {
            LOG_ERROR("Game buffer {} creation failed: {:X}", i, (UINT) result);
            ReleaseBuffers();
            return false;
        }

        _buffers[i]->SetName(std::format(L"FlipMeterSwapChain buffer {}", i).c_str());
    }

    return true;
}

// Callers wait for the GPU first
void FlipMeterSwapChain::ReleaseBuffers()
{
    for (auto& buffer : _buffers)
    {
        if (buffer != nullptr)
            buffer->Release();
    }

    for (UINT set = 0; set < 2; set++)
    {
        for (auto& buffer : _generated[set])
        {
            if (buffer != nullptr)
                buffer->Release();
        }

        _generated[set].clear();

        if (_realCopies[set] != nullptr)
        {
            _realCopies[set]->Release();
            _realCopies[set] = nullptr;
        }
    }

    _buffers.clear();
    _pendingGenerated = 0;
}

void FlipMeterSwapChain::WaitForGpu()
{
    FlushPresenter();

    if (_queue == nullptr || _gameCopies.fence == nullptr)
        return;

    const auto value = ++_gameCopies.value;

    if (SUCCEEDED(_queue->Signal(_gameCopies.fence, value)))
        WaitForFence(_gameCopies.fence, value, _gameCopies.event);

    // The idle presenter signaled all its copies
    if (_presenterCopies.fence != nullptr)
        WaitForFence(_presenterCopies.fence, _presenterCopies.value, _gameCopies.event);
}

static ID3D12Resource* CreateLike(ID3D12Resource* model, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                                  const std::wstring& name)
{
    ComPtr<ID3D12Device> device;
    if (FAILED(model->GetDevice(IID_PPV_ARGS(&device))))
        return nullptr;

    auto desc = model->GetDesc();
    desc.Flags = flags;

    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ID3D12Resource* resource = nullptr;
    auto result =
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource));

    if (FAILED(result))
    {
        LOG_ERROR("{} creation failed: {:X}", wstring_to_string(name), (UINT) result);
        return nullptr;
    }

    resource->SetName(name.c_str());

    return resource;
}

ID3D12Resource* FlipMeterSwapChain::GeneratedBuffer(UINT index)
{
    if (_buffers.empty() || _buffers[0] == nullptr)
        return nullptr;

    WaitForSet(_set);

    auto& generated = _generated[_set];

    if (index >= generated.size())
        generated.resize(index + 1, nullptr);

    // Frame generation writes it from compute
    if (generated[index] == nullptr)
    {
        generated[index] =
            CreateLike(_buffers[0], D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       std::format(L"FlipMeterSwapChain generated {}.{}", _set, index));
    }

    return generated[index];
}

ID3D12Resource* FlipMeterSwapChain::RealCopy(UINT set)
{
    if (_realCopies[set] == nullptr)
    {
        _realCopies[set] = CreateLike(_buffers[0], D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                      std::format(L"FlipMeterSwapChain real copy {}", set));
    }

    return _realCopies[set];
}

void FlipMeterSwapChain::SetGeneratedPerFrame(UINT count)
{
    count = std::min(count, _caps.maxGeneratedFrames);

    if (count == _generatedPerFrame)
        return;

    _generatedPerFrame = count;
    _meteringDirty = true;
}

bool FlipMeterSwapChain::SupportsMode(AmdExtFlipMeterMode mode) const
{
    return mode != AmdExtFlipMeterMode_Off && (_caps.supportedModes & (1u << (mode - 1))) != 0;
}

bool FlipMeterSwapChain::CoversOutput() const
{
    BOOL fullscreen = FALSE;
    if (SUCCEEDED(_real->GetFullscreenState(&fullscreen, nullptr)) && fullscreen)
        return true;

    MONITORINFO monitorInfo { sizeof(MONITORINFO) };
    RECT client {};
    POINT origin {};

    if (_hwnd == nullptr || !GetClientRect(_hwnd, &client) || !ClientToScreen(_hwnd, &origin) ||
        !GetMonitorInfo(MonitorFromWindow(_hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo))
    {
        return false;
    }

    const auto& monitor = monitorInfo.rcMonitor;

    return origin.x <= monitor.left && origin.y <= monitor.top && origin.x + client.right >= monitor.right &&
           origin.y + client.bottom >= monitor.bottom;
}

void FlipMeterSwapChain::ApplyConfig(bool enable)
{
    AmdExtFlipMeterConfig config {};
    config.mode = enable ? Mode() : AmdExtFlipMeterMode_Off;
    config.generatedFrames = std::max(_generatedPerFrame, 1u);
    config.status = AmdExtFlipMeterStatus_NotAvailable;

    // As FSR sets them
    if (_caps.supportedModes & AmdExtFlipMeterCaps_FlipTimer)
        config.flags |= AmdExtFlipMeterConfig_AllowFlipTimer;

    if (_caps.supportedModes & AmdExtFlipMeterCaps_Bit5)
        config.flags |= AmdExtFlipMeterConfig_Bit1;

    if (_caps.supportedModes & AmdExtFlipMeterCaps_Bit6)
        config.flags |= AmdExtFlipMeterConfig_Bit2;

    // Queued frames were made for the config before
    FlushPresenter();

    auto result = _flipMeter->SetConfig(&config);

    // A failing config is retried with every check, only its first failure gets logged
    if (SUCCEEDED(result) || SUCCEEDED(_configResult))
    {
        LOG_INFO("Flip meter mode: {}, generated frames: {}, flags: {:X}, pacer: {}, status: {}, result: {:X}",
                 config.mode, config.generatedFrames, config.flags, config.pacerType, config.status, (UINT) result);
    }

    _configResult = result;
    _configStatus = config.status;
    _metering = SUCCEEDED(result) && enable;
    _discontinuity = true;
}

void FlipMeterSwapChain::UpdateMetering()
{
    if (_flipMeter == nullptr)
        return;

    if (!_meteringDirty && (_presentCount % MeteringCheckInterval) != 0)
        return;

    _coversOutput = CoversOutput();

    const bool wanted = _generatedPerFrame > 0 && SupportsMode(Mode()) && _coversOutput;

    if (wanted != _metering || (wanted && _meteringDirty))
        ApplyConfig(wanted);

    _meteringDirty = false;
}

bool FlipMeterSwapChain::Copy(CopyContext& context, ID3D12CommandQueue* queue, ID3D12Resource* source,
                              D3D12_RESOURCE_STATES sourceState, ID3D12Resource* target,
                              D3D12_RESOURCE_STATES targetState)
{
    auto list = context.list;

    if (FAILED(list->Reset(context.allocators[context.allocator], nullptr)))
        return false;

    D3D12_RESOURCE_BARRIER barriers[2] {};
    UINT count = 0;

    auto transition = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        if (before == after)
            return;

        auto& barrier = barriers[count++];
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
    };

    transition(source, sourceState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(target, targetState, D3D12_RESOURCE_STATE_COPY_DEST);

    if (count > 0)
        list->ResourceBarrier(count, barriers);

    list->CopyResource(target, source);

    for (UINT i = 0; i < count; i++)
        std::swap(barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);

    if (count > 0)
        list->ResourceBarrier(count, barriers);

    if (FAILED(list->Close()))
        return false;

    queue->ExecuteCommandLists(1, (ID3D12CommandList**) &list);

    return true;
}

HRESULT FlipMeterSwapChain::PresentFrames(UINT syncInterval, UINT flags,
                                          const DXGI_PRESENT_PARAMETERS* presentParameters)
{
    if (_real == nullptr || _buffers.empty())
        return DXGI_ERROR_INVALID_CALL;

    if (flags & DXGI_PRESENT_TEST)
    {
        return presentParameters == nullptr ? _real->Present(syncInterval, flags)
                                            : _real1->Present1(syncInterval, flags, presentParameters);
    }

    const auto now = QpcNow();

    if (_lastGamePresentQpc != 0)
    {
        const auto delta = now - _lastGamePresentQpc;
        _averageFrameQpc = _averageFrameQpc == 0 ? delta : _averageFrameQpc + (delta - _averageFrameQpc) / 8;
    }

    _lastGamePresentQpc = now;

    UpdateMetering();

    if (RealBufferCount() > _realBufferCount)
        GrowRealBuffers();

    UINT generated = std::min(_pendingGenerated.exchange(0), (UINT) _generated[_set].size());

    // Generated frames are only shown while the driver paces them
    if (!_metering || _averageFrameQpc == 0)
        generated = 0;

    const UINT frames = generated + 1;

    // Each frame an even share of the game's frame time after the frame before it, minus a margin that keeps the last
    // one from running into the next game frame. frameTimeNs as FSR sets it: that share times the frames.
    const INT64 margin = _qpcFrequency / 10000;
    const INT64 step = generated > 0 ? std::max<INT64>(_averageFrameQpc / frames - margin, 0) : 0;

    // The game reuses its buffer, the presenter shows it up to a game frame later
    WaitForSet(_set);
    auto realCopy = RealCopy(_set);

    if (realCopy == nullptr || !_gameCopies.Begin())
        return DXGI_ERROR_INVALID_CALL;

    const bool copied = Copy(_gameCopies, _queue, _buffers[_index], D3D12_RESOURCE_STATE_PRESENT, realCopy,
                             D3D12_RESOURCE_STATE_COPY_SOURCE);

    // Also after a failed copy, Begin needs its End
    const auto readyValue = _gameCopies.End(_queue);

    if (!copied)
    {
        LOG_ERROR("Copy of the game's frame failed");
        return DXGI_ERROR_INVALID_CALL;
    }

    // ponytail: Present1's dirty rects and scroll aren't passed on, they don't apply to the generated frames
    QueuedFrame frame;
    frame.set = _set;
    frame.generated = generated;
    frame.step = step;
    frame.frameTimeNs = (UINT32) (step * 1'000'000'000 / _qpcFrequency * frames);
    frame.metered = _metering;
    frame.discontinuity = _discontinuity && generated > 0;
    frame.presentId = _presentCount;
    frame.syncInterval = syncInterval;
    frame.flags = flags;
    frame.readyValue = readyValue;

    {
        std::scoped_lock lock(_presenterMutex);
        _queued.push_back(frame);
        _setBusy[_set]++;
    }

    _presenterCv.notify_all();

    _discontinuity = generated == 0;
    _set ^= 1;
    _index = (_index + 1) % (UINT) _buffers.size();
    _presentCount++;

    // What the presenter got for the frames before
    return _lastResult;
}

void FlipMeterSwapChain::PresenterLoop()
{
    SetThreadDescription(GetCurrentThread(), L"OptiScaler Flip Meter Presenter");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    for (;;)
    {
        QueuedFrame frame;

        {
            std::unique_lock lock(_presenterMutex);
            _presenterCv.wait(lock, [this] { return _stopPresenter || !_queued.empty(); });

            if (_stopPresenter)
                return;

            frame = _queued.front();
        }

        _lastResult = PresentQueued(frame);

        {
            std::scoped_lock lock(_presenterMutex);
            _queued.pop_front();
            _setBusy[frame.set]--;
            _setReleased[frame.set] = _presenterCopies.value;
        }

        _presenterCv.notify_all();
    }
}

HRESULT FlipMeterSwapChain::PresentQueued(const QueuedFrame& frame)
{
    // Paced from when its frames are ready, frame generation takes its time on the GPU
    WaitForFence(_gameCopies.fence, frame.readyValue, _readyEvent);

    // Chained from the previous game frame's last frame, kept between now and a step ahead
    const INT64 now = QpcNow();
    INT64 targetQpc = std::clamp(_lastTargetQpc + frame.step, now, now + frame.step);

    if (!_presenterCopies.Begin())
        return DXGI_ERROR_INVALID_CALL;

    HRESULT result = S_OK;

    for (UINT i = 0; i <= frame.generated; i++)
    {
        const bool isGenerated = i < frame.generated;

        if (i > 0)
            targetQpc += frame.step;

        // The driver times the frames from when the first one arrives and holds one flip at a time, so the others
        // only need to come in after the flip before them. With one generated frame they can all come at once.
        if (frame.generated > 1)
            WaitUntil(i == 0 ? targetQpc : targetQpc - frame.step / 2);

        auto source = isGenerated ? _generated[frame.set][i] : _realCopies[frame.set];
        const auto sourceState = isGenerated ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COPY_SOURCE;

        ComPtr<ID3D12Resource> target;
        result = _real3->GetBuffer(_real3->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&target));

        if (FAILED(result))
            break;

        if (!Copy(_presenterCopies, _presentQueue, source, sourceState, target.Get(), D3D12_RESOURCE_STATE_PRESENT))
        {
            LOG_ERROR("Copy to the real back buffer failed");
            result = DXGI_ERROR_INVALID_CALL;
            break;
        }

        if (frame.metered)
        {
            AmdExtFlipMeterPresentInfo info {};
            info.presentId = frame.presentId;
            info.targetQpc = (UINT64) targetQpc;
            info.frameTimeNs = frame.frameTimeNs;
            info.framesLeft = frame.generated - i;
            info.flags = frame.discontinuity ? AmdExtFlipMeterPresent_Discontinuity : 0;

            UINT32 status = AmdExtFlipMeterStatus_NotAvailable;
            const auto meterResult = _flipMeter->Present(target.Get(), &info, &status);

            if (FAILED(meterResult))
                LOG_DEBUG("Flip meter present failed: {:X}, status: {}", (UINT) meterResult, status);
        }

        // The real swapchain can release its buffers from within its present
        target.Reset();

        if (frame.generated > 0)
            InputCommon::fg_frame_type(isGenerated);

        result = _real->Present(frame.syncInterval, frame.flags);

        if (FAILED(result))
            break;
    }

    _lastTargetQpc = targetQpc;
    _presenterCopies.End(_presentQueue);

    return result;
}

// The timer wakes up to about half a millisecond late, the rest is spun
void FlipMeterSwapChain::WaitUntil(INT64 qpc) const
{
    const INT64 spin = _qpcFrequency / 2000;

    for (INT64 left = qpc - QpcNow(); left > 0; left = qpc - QpcNow())
    {
        LARGE_INTEGER due {};
        due.QuadPart = -((left - spin) * 10'000'000 / _qpcFrequency);

        if (left <= spin || _timer == nullptr || !SetWaitableTimerEx(_timer, &due, 0, nullptr, nullptr, nullptr, 0))
            YieldProcessor();
        else
            WaitForSingleObject(_timer, 100);
    }
}

void FlipMeterSwapChain::WaitForSet(UINT set)
{
    std::unique_lock lock(_presenterMutex);
    _presenterCv.wait(lock, [&] { return _setBusy[set] == 0; });

    // The presenter's copies run on its own queue
    if (_setReleased[set] != 0)
        _queue->Wait(_presenterCopies.fence, _setReleased[set]);
}

void FlipMeterSwapChain::FlushPresenter()
{
    std::unique_lock lock(_presenterMutex);
    _presenterCv.wait(lock, [this] { return _queued.empty() || !_presenter.joinable(); });
}

// What's still queued doesn't get shown
void FlipMeterSwapChain::StopPresenter()
{
    if (!_presenter.joinable())
        return;

    {
        std::scoped_lock lock(_presenterMutex);
        _stopPresenter = true;
    }

    _presenterCv.notify_all();
    _presenter.join();

    std::scoped_lock lock(_presenterMutex);
    _queued.clear();
    _setBusy[0] = _setBusy[1] = 0;
    _presenterCv.notify_all();
}

// The game only has references to its own buffers, the real ones can change any time
void FlipMeterSwapChain::GrowRealBuffers()
{
    DXGI_SWAP_CHAIN_DESC1 desc {};
    if (FAILED(_real1->GetDesc1(&desc)))
        return;

    const UINT count = RealBufferCount();

    WaitForGpu();

    auto result = _real->ResizeBuffers(count, desc.Width, desc.Height, desc.Format, desc.Flags);

    LOG_INFO("Real swapchain buffers {} -> {}, result: {:X}", _realBufferCount, count, (UINT) result);

    // Not retried, the copies to the real back buffers just wait for free ones then
    _realBufferCount = count;
}

HRESULT FlipMeterSwapChain::Resize(UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags,
                                   const UINT* nodeMasks, IUnknown* const* presentQueues)
{
    if (_real == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    WaitForGpu();

    if (_metering)
        ApplyConfig(false);

    ReleaseBuffers();

    const UINT previousCount = _gameBufferCount;

    if (bufferCount != 0)
        _gameBufferCount = bufferCount;

    const UINT realCount = RealBufferCount();
    HRESULT result;

    if (nodeMasks != nullptr && presentQueues != nullptr)
    {
        // One entry per buffer, the game's arrays only cover its own count. The real swapchain keeps presenting on
        // the present queue, the game's queue gets the game's copies.
        std::vector<UINT> masks(realCount);
        std::vector<IUnknown*> queues(realCount, _presentQueue);
        const UINT gameEntries = bufferCount != 0 ? bufferCount : previousCount;

        for (UINT i = 0; i < realCount; i++)
            masks[i] = nodeMasks[std::min(i, gameEntries - 1)];

        result = _real3->ResizeBuffers1(realCount, width, height, format, flags, masks.data(), queues.data());

        ComPtr<ID3D12CommandQueue> queue;
        if (SUCCEEDED(result) && presentQueues[0] != nullptr &&
            SUCCEEDED(presentQueues[0]->QueryInterface(IID_PPV_ARGS(&queue))) && queue.Get() != _queue)
        {
            _queue->Release();
            _queue = queue.Detach();
        }
    }
    else
    {
        result = _real->ResizeBuffers(realCount, width, height, format, flags);
    }

    if (FAILED(result))
    {
        LOG_ERROR("Real swapchain resize failed: {:X}", (UINT) result);
        _gameBufferCount = previousCount;
    }
    else
    {
        _realBufferCount = realCount;
    }

    // Also after a failed resize, the real swapchain keeps its old buffers then
    if (!CreateBuffers() && SUCCEEDED(result))
        result = E_OUTOFMEMORY;

    _meteringDirty = true;
    _discontinuity = true;
    _averageFrameQpc = 0;
    _lastGamePresentQpc = 0;

    // The presenter is idle until the next present
    _lastTargetQpc = 0;

    return result;
}

void FlipMeterSwapChain::Shutdown()
{
    StopPresenter();
    WaitForGpu();

    if (_flipMeter != nullptr)
    {
        if (_metering)
            ApplyConfig(false);

        _flipMeter->Release();
        _flipMeter = nullptr;
        _metering = false;
    }

    ReleaseBuffers();

    _gameCopies.Release();
    _presenterCopies.Release();

    for (auto handle : { &_timer, &_readyEvent })
    {
        if (*handle != nullptr)
        {
            CloseHandle(*handle);
            *handle = nullptr;
        }
    }

    _real = nullptr;
    _real1 = nullptr;
    _real2 = nullptr;
    _real3 = nullptr;
    _real4 = nullptr;
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    const bool supported =
        riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISwapChain) || (riid == __uuidof(IDXGISwapChain1) && _real1 != nullptr) ||
        (riid == __uuidof(IDXGISwapChain2) && _real2 != nullptr) ||
        (riid == __uuidof(IDXGISwapChain3) && _real3 != nullptr) ||
        (riid == __uuidof(IDXGISwapChain4) && _real4 != nullptr) || riid == __uuidof(FlipMeterSwapChain);

    if (!supported)
    {
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    AddRef();
    *ppvObject = this;
    return S_OK;
}

ULONG STDMETHODCALLTYPE FlipMeterSwapChain::AddRef() { return InterlockedIncrement(&_refcount); }

ULONG STDMETHODCALLTYPE FlipMeterSwapChain::Release()
{
    const ULONG count = InterlockedDecrement(&_refcount);

    if (count == 0)
    {
        auto real = _real;
        Shutdown();

        if (real != nullptr)
        {
            const auto realCount = real->Release();
            LOG_DEBUG("Real swapchain released, refCount: {}", realCount);
        }

        if (_queue != nullptr)
            _queue->Release();

        if (_presentQueue != nullptr)
            _presentQueue->Release();

        if (_device != nullptr)
            _device->Release();

        delete this;
    }

    return count;
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData)
{
    return _real->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown)
{
    return _real->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData)
{
    return _real->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetParent(REFIID riid, void** ppParent)
{
    return _real->GetParent(riid, ppParent);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetDevice(REFIID riid, void** ppDevice)
{
    return _real->GetDevice(riid, ppDevice);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::Present(UINT SyncInterval, UINT Flags)
{
    return PresentFrames(SyncInterval, Flags, nullptr);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface)
{
    if (ppSurface == nullptr)
        return E_POINTER;

    if (Buffer >= _buffers.size() || _buffers[Buffer] == nullptr)
    {
        *ppSurface = nullptr;
        return DXGI_ERROR_INVALID_CALL;
    }

    return _buffers[Buffer]->QueryInterface(riid, ppSurface);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget)
{
    FlushPresenter();

    auto result = _real->SetFullscreenState(Fullscreen, pTarget);

    LOG_DEBUG("Fullscreen: {}, result: {:X}", Fullscreen, (UINT) result);

    _meteringDirty = true;

    return result;
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget)
{
    return _real->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc)
{
    auto result = _real->GetDesc(pDesc);

    if (SUCCEEDED(result) && pDesc != nullptr)
        pDesc->BufferCount = _gameBufferCount;

    return result;
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                            DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    return Resize(BufferCount, Width, Height, NewFormat, SwapChainFlags, nullptr, nullptr);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters)
{
    _meteringDirty = true;
    return _real->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetContainingOutput(IDXGIOutput** ppOutput)
{
    return _real->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats)
{
    return _real->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetLastPresentCount(UINT* pLastPresentCount)
{
    return _real->GetLastPresentCount(pLastPresentCount);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc)
{
    auto result = _real1->GetDesc1(pDesc);

    if (SUCCEEDED(result) && pDesc != nullptr)
        pDesc->BufferCount = _gameBufferCount;

    return result;
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
    return _real1->GetFullscreenDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetHwnd(HWND* pHwnd) { return _real1->GetHwnd(pHwnd); }

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetCoreWindow(REFIID refiid, void** ppUnk)
{
    return _real1->GetCoreWindow(refiid, ppUnk);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::Present1(UINT SyncInterval, UINT PresentFlags,
                                                       const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    return PresentFrames(SyncInterval, PresentFlags, pPresentParameters);
}

BOOL STDMETHODCALLTYPE FlipMeterSwapChain::IsTemporaryMonoSupported(void) { return _real1->IsTemporaryMonoSupported(); }

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput)
{
    return _real1->GetRestrictToOutput(ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::SetBackgroundColor(const DXGI_RGBA* pColor)
{
    return _real1->SetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetBackgroundColor(DXGI_RGBA* pColor)
{
    return _real1->GetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::SetRotation(DXGI_MODE_ROTATION Rotation)
{
    return _real1->SetRotation(Rotation);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetRotation(DXGI_MODE_ROTATION* pRotation)
{
    return _real1->GetRotation(pRotation);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::SetSourceSize(UINT Width, UINT Height)
{
    return _real2->SetSourceSize(Width, Height);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetSourceSize(UINT* pWidth, UINT* pHeight)
{
    return _real2->GetSourceSize(pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::SetMaximumFrameLatency(UINT MaxLatency)
{
    return _real2->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetMaximumFrameLatency(UINT* pMaxLatency)
{
    return _real2->GetMaximumFrameLatency(pMaxLatency);
}

HANDLE STDMETHODCALLTYPE FlipMeterSwapChain::GetFrameLatencyWaitableObject(void)
{
    return _real2->GetFrameLatencyWaitableObject();
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix)
{
    return _real2->SetMatrixTransform(pMatrix);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix)
{
    return _real2->GetMatrixTransform(pMatrix);
}

UINT STDMETHODCALLTYPE FlipMeterSwapChain::GetCurrentBackBufferIndex(void) { return _index; }

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                     UINT* pColorSpaceSupport)
{
    return _real3->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    return _real3->SetColorSpace1(ColorSpace);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
                                                             DXGI_FORMAT Format, UINT SwapChainFlags,
                                                             const UINT* pCreationNodeMask,
                                                             IUnknown* const* ppPresentQueue)
{
    // DXGI needs both arrays, the real swapchain rejects the call without them
    if (pCreationNodeMask == nullptr || ppPresentQueue == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    return Resize(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
}

HRESULT STDMETHODCALLTYPE FlipMeterSwapChain::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void* pMetaData)
{
    return _real4->SetHDRMetaData(Type, Size, pMetaData);
}
