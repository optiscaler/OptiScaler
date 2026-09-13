#include "pch.h"
#include "DlssNr_Dx12_State.h"
#include <dlssnr/DlssNr_StreamlinePicture.h>

void DlssNr_Dx12::ApplyStreamlineFinished(IDXGISwapChain* swapchain, ID3D12Resource* picture, ID3D12CommandQueue* queue)
{
    std::lock_guard lock(_state->mutex);
    if (swapchain && picture && queue)
        _state->ApplyFinishedColor(picture, queue, _state->FinishedColorSpace(swapchain, picture->GetDesc().Format), true);
}

auto DlssNr_Dx12::State::FinishedPictureResetCommandList(ID3D12CommandList* cmd) -> void
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    lifetime.ResetRecording(cmd);
    if (enlarger) enlarger->lifetime.ResetRecording(cmd);
    for (auto& old : retiredEnlargers) old->lifetime.ResetRecording(cmd);
    CollectEnlargers();
    ID3D12CommandList* real = nullptr;
    auto* identity = Util::CheckForRealObject(__FUNCTION__, cmd, (IUnknown**)&real) ? real : cmd;
    if (enlarger && !enlarger->submitted && enlarger->creation == identity)
    {
        enlarger->failed = true;
        enlargementStatus = "DLSS enlargement initialization was discarded; use Retry.";
    }
    for (auto& model : nr.models) model.ResetRecording(cmd);
    if (inputHold.captureCommands == cmd)
    {
        inputHold.active = false; // recording was discarded before submission
        inputHold.captureCommands = nullptr;
        nr.heldActive = false;
    }
    if (gpuTime)
        gpuTime->ResetRecording(cmd);
    if (ngxTime)
        ngxTime->ResetRecording(cmd);
    if (!late.tracking.load())
        return;
    for (auto& slot : late.slots)
        if (slot.pending && !slot.submitted && slot.producer == cmd)
        {
            slot.pending = false;
            slot.done = slot.ready - 1; // discarded recording: no GPU signal was promised
            late.reset = true;
        }
}

auto DlssNr_Dx12::State::WaitForFinishedPicture() -> bool
{
    if (!late.tracking.load())
        return true;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    late.Cancel();
    for (auto& slot : late.slots)
    {
        if (!slot.submitted || late.Finished(slot))
            continue;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            return false;
        const auto hr = slot.fence->SetEventOnCompletion(slot.done, event);
        const bool finished = SUCCEEDED(hr) && WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
        CloseHandle(event);
        if (!finished)
            return false;
    }
    return late.dx11.Drain();
}

auto DlssNr_Dx12::State::FinishedPictureStatus() -> std::string
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return late.status;
}

auto DlssNr_Dx12::State::FinishedPictureSubmitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) -> void
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    lifetime.Submitted(queue, count, lists);
    if (enlarger) enlarger->lifetime.Submitted(queue, count, lists);
    for (auto& old : retiredEnlargers) old->lifetime.Submitted(queue, count, lists);
    CollectEnlargers();
    if (enlarger && !enlarger->submitted)
    {
        ID3D12CommandQueue* real = nullptr;
        auto* identity = Util::CheckForRealObject(__FUNCTION__, queue, (IUnknown**)&real) ? real : queue;
        for (UINT i = 0; i < count; ++i)
        {
            ID3D12CommandList* realList = nullptr;
            auto* list = Util::CheckForRealObject(__FUNCTION__, lists[i], (IUnknown**)&realList) ? realList : lists[i];
            if (list == enlarger->creation)
            {
                enlarger->queue = identity;
                enlarger->submitted = true;
                LOG_INFO("NR DLSS enlargement initialization submitted on producer queue {}", (void*)identity);
            }
        }
    }
    for (auto& model : nr.models) model.Submitted(queue, count, lists);
    for (UINT i = 0; i < count; ++i)
        if (lists[i] == inputHold.captureCommands)
            inputHold.captureCommands = nullptr;
    if (gpuTime)
        gpuTime->Submitted(queue, count, lists);
    if (ngxTime)
        ngxTime->Submitted(queue, count, lists);
    if (!late.tracking.load())
        return;
    for (auto& slot : late.slots)
        if (slot.pending && !slot.submitted)
            for (UINT i = 0; i < count; ++i)
                if (lists[i] == slot.producer)
                {
                    // Signal after ExecuteCommandLists, never when merely recording the copy.
                    slot.submitted = true;
                    late.producerQueue = queue;
                    ID3D12CommandQueue* realQueue = nullptr;
                    slot.producerQueue = Util::CheckForRealObject(__FUNCTION__, queue, (IUnknown**) &realQueue)
                                             ? realQueue : queue;
                    if (FAILED(queue->Signal(slot.fence.Get(), slot.ready)))
                    {
                        // The copy already executed. Keep its unsignalled fence protecting
                        // the slot instead of treating this as a discarded recording.
                        slot.pending = false;
                        late.Say("The graphics queue stopped. Restart the game to retry.");
                    }
                    break;
                }
}

auto DlssNr_Dx12::State::FinishedPictureColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE colorSpace) -> void
{
    swapchain->SetPrivateData(late.colorSpaceKey, sizeof(colorSpace), &colorSpace);
}

auto DlssNr_Dx12::State::FinishedColorSpace(IDXGISwapChain* swapchain, DXGI_FORMAT format) -> DXGI_COLOR_SPACE_TYPE
{
    auto space = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
                                                        : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    UINT size = sizeof(space);
    swapchain->GetPrivateData(late.colorSpaceKey, &size, &space);
    return space;
}

auto DlssNr_Dx12::State::ApplyToFinishedPicture(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue) -> void
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!swapchain || !queue)
        return;
    // Native Streamline owns an app-facing buffer set. Its before-present hook handles NR.
    // Editing the underlying display swapchain here races/is overwritten by DLSSG's own copies.
    if (DlssNr::StreamlinePicture::RenderQueue(swapchain))
        return;
    LateContext::ComPtr<IDXGISwapChain3> sc;
    LateContext::ComPtr<ID3D12Resource> color;
    if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&sc))) ||
        FAILED(sc->GetBuffer(sc->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&color))))
        return;
    ApplyFinishedColor(color.Get(), queue, FinishedColorSpace(swapchain, color->GetDesc().Format));
}

auto DlssNr_Dx12::State::ApplyToFinishedPictureDx11(IDXGISwapChain* swapchain) -> void
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!swapchain || !late.device || !late.producerQueue ||
        !Config::Instance()->DlssNrFinishedPicture.value_or_default() ||
        !Config::Instance()->DlssNrEnabled.value_or_default())
        return;
    const bool heldPicture = Config::Instance()->DlssNrHoldFrame.value_or_default() && inputHold.active &&
                             late.heldValid && late.heldGeneration == inputHold.generation;
    if (!heldPicture && std::none_of(late.slots.begin(), late.slots.end(), [](const auto& slot) {
            return slot.pending && slot.submitted;
        }))
        return;
    LateContext::ComPtr<IDXGISwapChain3> sc;
    LateContext::ComPtr<ID3D11Texture2D> picture;
    DXGI_SWAP_CHAIN_DESC desc {};
    if (FAILED(swapchain->GetDesc(&desc)))
        return;
    // Blt-model chains expose buffer zero; flip-model chains rotate the current index.
    UINT index = 0;
    if ((desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD || desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) &&
        SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&sc))))
        index = sc->GetCurrentBackBufferIndex();
    if (FAILED(swapchain->GetBuffer(index, IID_PPV_ARGS(&picture))))
        return;
    auto* color = late.dx11.Begin(picture.Get(), late.device.Get(), late.producerQueue.Get());
    if (!color)
    {
        late.Say("The DirectX 11 finished-picture bridge is unavailable for this device or screen format.");
        return;
    }
    const bool ran = ApplyFinishedColor(color, late.producerQueue.Get(),
                                       FinishedColorSpace(swapchain, color->GetDesc().Format));
    if (!late.dx11.End(picture.Get(), ran))
        late.Say("The DirectX 11 finished-picture transfer failed. Restart the game to retry.");
}
