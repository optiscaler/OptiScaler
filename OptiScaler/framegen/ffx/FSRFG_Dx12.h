#pragma once

#include <framegen/IFGFeature_Dx12.h>

#include <proxies/FfxApi_Proxy.h>

#include <dx12/ffx_api_dx12.h>
#include <ffx_framegeneration.h>

class FSRFG_Dx12 : public virtual IFGFeature_Dx12
{
  private:
    ffxContext _swapChainContext = nullptr;
    ffxContext _fgContext = nullptr;
    uint32_t _lastHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
    uint32_t _usingHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;

  public:
    // IFGFeature
    const char* Name() override final;
    feature_version Version() override final;

    void StopAndDestroyContext(bool destroy, bool shutDown, bool useMutex) override final;

    // IFGFeature_Dx12
    bool CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                         IDXGISwapChain** swapChain) override final;
    bool CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd, DXGI_SWAP_CHAIN_DESC1* desc,
                          DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGISwapChain1** swapChain) override final;
    bool ReleaseSwapchain(HWND hwnd) override final;

    void CreateContext(ID3D12Device* device, FG_Constants& fgConstants) override final;

    void EvaluateState(ID3D12Device* device, FG_Constants& fgConstants) override final;

    bool Dispatch(ID3D12GraphicsCommandList* cmdList, bool useHudless, double frameTime) override final;

    void* FrameGenerationContext() override final;
    void* SwapchainContext() override final;

    // Methods
    void ConfigureFramePaceTuning();

    ffxReturnCode_t DispatchCallback(ffxDispatchDescFrameGeneration* params);

    FSRFG_Dx12() : IFGFeature_Dx12(), IFGFeature()
    {
        //
    }
};
