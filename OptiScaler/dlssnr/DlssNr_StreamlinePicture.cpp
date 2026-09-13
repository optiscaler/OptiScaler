#include "pch.h"
#include "DlssNr_StreamlinePicture.h"
#include "DlssNrFeature_Dx12.h"
#include <Config.h>
#include <State.h>
#include <Util.h>
#include <cstring>

namespace DlssNr::StreamlinePicture
{
namespace
{
using Present = HRESULT (*)(IDXGISwapChain*, UINT, UINT, bool&);
using Present1 = HRESULT (*)(IDXGISwapChain*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*, bool&);
using CreateHwnd = HRESULT (*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                              const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**, bool&);
Present originalPresent = nullptr;
Present1 originalPresent1 = nullptr;
CreateHwnd originalCreate = nullptr;
GetIndex getIndex = nullptr;
GetBuffer getBuffer = nullptr;

void Apply(IDXGISwapChain* swapchain, UINT flags, bool skip)
{
    const auto& cfg = *Config::Instance();
    if (skip || (flags & DXGI_PRESENT_TEST) || !cfg.DlssNrEnabled.value_or_default() ||
        !cfg.DlssNrFinishedPicture.value_or_default()) return;
    auto queue = RenderQueue(swapchain);
    if (!queue) return;
    auto picture = Read(swapchain, getIndex, getBuffer);
    if (!picture) return;
    ApplyToStreamlinePicture(swapchain, picture.Get(), queue.Get());
}

HRESULT BeforePresent(IDXGISwapChain* swapchain, UINT interval, UINT flags, bool& skip)
{
    Apply(swapchain, flags, skip);
    return originalPresent(swapchain, interval, flags, skip);
}
HRESULT BeforePresent1(IDXGISwapChain* swapchain, UINT interval, UINT flags,
                       const DXGI_PRESENT_PARAMETERS* parameters, bool& skip)
{
    Apply(swapchain, flags, skip);
    return originalPresent1(swapchain, interval, flags, parameters, skip);
}
HRESULT CreateForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND window, const DXGI_SWAP_CHAIN_DESC1* desc,
                      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* output,
                      IDXGISwapChain1** swapchain, bool& skip)
{
    const auto result = originalCreate(factory, device, window, desc, fullscreen, output, swapchain, skip);
    // A handled create is the native game's DLSSG swapchain. Remember the app's queue,
    // not the internal presentation queue that DLSSG passes to the native DXGI factory.
    if (SUCCEEDED(result) && skip && device && swapchain && *swapchain)
    {
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&queue))))
        {
            ID3D12CommandQueue* real = nullptr;
            if (Util::CheckForRealObject(__FUNCTION__, queue.Get(), (IUnknown**) &real)) queue = real;
            if (SUCCEEDED((*swapchain)->SetPrivateDataInterface(renderQueueKey, queue.Get())))
                LOG_INFO("DLSS-NR: native Streamline finished-picture handoff registered on the game render queue");
        }
    }
    return result;
}
}

void* Wrap(const char* name, GetFunction getFunction)
{
    if (!getFunction) return nullptr;
    const bool present = std::strcmp(name, "slHookPresent") == 0;
    const bool present1 = std::strcmp(name, "slHookPresent1") == 0;
    const bool create = std::strcmp(name, "slHookCreateSwapChainForHwnd") == 0;
    if (!present && !present1 && !create) return nullptr;
    auto* function = getFunction(name);
    getIndex = reinterpret_cast<GetIndex>(getFunction("slHookGetCurrentBackBufferIndex"));
    getBuffer = reinterpret_cast<GetBuffer>(getFunction("slHookGetBuffer"));
    if (!function || !getIndex || !getBuffer || !getFunction("slHookPresent") || !getFunction("slHookPresent1"))
        return nullptr;
    if (present) { originalPresent = reinterpret_cast<Present>(function); return &BeforePresent; }
    if (present1) { originalPresent1 = reinterpret_cast<Present1>(function); return &BeforePresent1; }
    originalCreate = reinterpret_cast<CreateHwnd>(function);
    return &CreateForHwnd;
}
}
