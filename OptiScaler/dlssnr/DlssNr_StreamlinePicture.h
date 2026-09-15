#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace DlssNr::StreamlinePicture
{
// Streamline's before-hook ABI (sl.api/internal.h). These access the app-facing FG buffers.
using GetIndex = UINT (*)(IDXGISwapChain*, bool&);
using GetBuffer = HRESULT (*)(IDXGISwapChain*, UINT, REFIID, void**, bool&);
inline constexpr GUID renderQueueKey = { 0xc1eb6f13, 0x550c, 0x46ab, { 0xa8, 0x29, 0xf8, 0x72, 0x12, 0x69, 0x3a, 0xb1 } };

inline Microsoft::WRL::ComPtr<ID3D12Resource> Read(IDXGISwapChain* swapchain, GetIndex indexHook, GetBuffer bufferHook)
{
    Microsoft::WRL::ComPtr<ID3D12Resource> picture;
    if (!swapchain || !indexHook || !bufferHook) return picture;
    bool handled = false;
    const UINT index = indexHook(swapchain, handled);
    if (!handled) return picture;
    handled = false;
    if (FAILED(bufferHook(swapchain, index, IID_PPV_ARGS(&picture), handled)) || !handled)
        picture.Reset();
    return picture; // Never substitute the asynchronous display buffer when the plugin declines.
}

inline Microsoft::WRL::ComPtr<ID3D12CommandQueue> RenderQueue(IDXGISwapChain* swapchain)
{
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    UINT size = sizeof(ID3D12CommandQueue*);
    if (swapchain && FAILED(swapchain->GetPrivateData(renderQueueKey, &size, queue.GetAddressOf()))) queue.Reset();
    return queue;
}
using GetFunction = void* (*)(const char*);
void* Wrap(const char* name, GetFunction getFunction);
}
