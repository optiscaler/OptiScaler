#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include "../OptiScaler/hooks/DxgiSwapchainSizing.h"

using Microsoft::WRL::ComPtr;
void expect(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
void check(HRESULT hr) { expect(SUCCEEDED(hr), "DXGI/D3D11 call failed"); }
struct Window
{
    HWND hwnd;
    Window(int width, int height)
        : hwnd(CreateWindowExW(0, L"STATIC", L"OptiScaler sizing test", WS_POPUP,
                               0, 0, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr))
    { expect(hwnd != nullptr, "hidden test window creation failed"); }
    ~Window() { DestroyWindow(hwnd); }
};

int main() try
{
    Window game(640, 360), tiny(32, 32), threshold(100, 100);
    DXGI_SWAP_CHAIN_DESC source {};
    source.OutputWindow = game.hwnd; source.Windowed = TRUE;
    source.BufferCount = 2; source.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    source.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    source.SampleDesc.Count = 1; source.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    auto local = source;
    expect(ResolveWindowSizedSwapchain(local), "normal hidden game window rejected");
    expect(local.BufferDesc.Width == 640 && local.BufferDesc.Height == 360, "wrong client dimensions");
    expect(source.BufferDesc.Width == 0 && source.BufferDesc.Height == 0, "caller descriptor changed");
    local = source; local.OutputWindow = tiny.hwnd;
    expect(!ResolveWindowSizedSwapchain(local) && local.BufferDesc.Width == 0, "tiny helper window accepted");
    local = source; local.OutputWindow = nullptr;
    expect(!ResolveWindowSizedSwapchain(local), "null HWND accepted");
    local = source; local.OutputWindow = reinterpret_cast<HWND>(static_cast<INT_PTR>(-1));
    expect(!ResolveWindowSizedSwapchain(local), "invalid HWND accepted");
    local = source; local.BufferDesc.Width = 32; local.BufferDesc.Height = 32;
    expect(!ResolveWindowSizedSwapchain(local) && local.BufferDesc.Width == 32, "explicit tiny size overridden");
    local = source; local.OutputWindow = threshold.hwnd;
    expect(ResolveWindowSizedSwapchain(local) && local.BufferDesc.Width == 100, "100px boundary rejected");

    // Verify the actual legacy DXGI contract with WARP, without injecting into a game or showing UI.
    ComPtr<ID3D11Device> device;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                           D3D11_SDK_VERSION, &device, nullptr, nullptr));
    ComPtr<IDXGIDevice> dxgiDevice; check(device.As(&dxgiDevice));
    ComPtr<IDXGIAdapter> adapter; check(dxgiDevice->GetAdapter(&adapter));
    ComPtr<IDXGIFactory> factory; check(adapter->GetParent(IID_PPV_ARGS(&factory)));
    for (bool normalize : { false, true })
    {
        auto descriptor = source;
        if (normalize) ResolveWindowSizedSwapchain(descriptor);
        ComPtr<IDXGISwapChain> swapchain;
        check(factory->CreateSwapChain(device.Get(), &descriptor, &swapchain));
        DXGI_SWAP_CHAIN_DESC resolved {}; check(swapchain->GetDesc(&resolved));
        expect(resolved.BufferDesc.Width == 640 && resolved.BufferDesc.Height == 360,
               "DXGI raw/normalized descriptors disagree");
    }
    expect(IsCompositionWindow(game.hwnd), "own hidden composition window rejected");
    expect(!IsCompositionWindow(tiny.hwnd) && !IsCompositionWindow(nullptr), "invalid composition window accepted");
    ComPtr<IDXGIFactory2> factory2; check(factory.As(&factory2));
    DXGI_SWAP_CHAIN_DESC1 composition {};
    composition.Width = 640; composition.Height = 360;
    composition.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    composition.SampleDesc.Count = 1; composition.BufferCount = 2;
    composition.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    composition.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    composition.Scaling = DXGI_SCALING_STRETCH;
    composition.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    ComPtr<IDXGISwapChain1> chain;
    check(factory2->CreateSwapChainForComposition(device.Get(), &composition, nullptr, &chain));
    ComPtr<IDCompositionDevice> compositor;
    check(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&compositor)));
    ComPtr<IDCompositionTarget> target; check(compositor->CreateTargetForHwnd(game.hwnd, TRUE, &target));
    ComPtr<IDCompositionVisual> visual; check(compositor->CreateVisual(&visual));
    check(visual->SetContent(chain.Get())); check(target->SetRoot(visual.Get())); check(compositor->Commit());
    HWND nativeWindow = nullptr;
    expect(FAILED(chain->GetHwnd(&nativeWindow)), "composition unexpectedly has native HWND");
    check(chain->ResizeBuffers(2, 800, 450, DXGI_FORMAT_UNKNOWN, 0));
    DXGI_SWAP_CHAIN_DESC1 resized {}; check(chain->GetDesc1(&resized));
    expect(resized.Width == 800 && resized.Height == 450 &&
           resized.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL && resized.Flags == 0,
           "composition resize changed contract");
    std::puts("PASS: client-size classification, descriptor preservation, raw/normalized 0x0 DXGI and composition create/attach/resize (WARP)");
    return 0;
}
catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
