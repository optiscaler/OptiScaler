#pragma once

#include <framegen/IFGFeature_Dx12.h>

#include <proxies/SL_Proxy.h>

#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>

class DLSSG_Dx12 : public virtual IFGFeature_Dx12
{
  private:
    // SL state tracking
    bool _slInitialized = false;
    bool _deviceRegistered = false;
    bool _dlssgFeatureReady = false;

    // MFG state
    uint32_t _numFramesToGenerateMax = 1;
    uint32_t _numFramesToGenerate = 1;

    // SL frame management
    uint64_t _slFrameIndex = 0;
    const sl::FrameToken* _currentFrameToken = nullptr;

    // Tagging command lists
    ID3D12GraphicsCommandList* _tagCommandList[BUFFER_COUNT] {};
    ID3D12CommandAllocator* _tagCommandAllocator[BUFFER_COUNT] {};

    // Internal helpers
    bool InitStreamline(ID3D12Device* device);
    void ShutdownStreamline();

    bool TagResources(int fIndex, uint64_t willDispatchFrame);
    void SetSLConstants(int fIndex);
    bool Dispatch();

    sl::Resource MakeSLResource(ID3D12Resource* d3dResource, D3D12_RESOURCE_STATES state);

  protected:
    void ReleaseObjects() override final;
    void CreateObjects(ID3D12Device* InDevice) override final;

  public:
    // IFGFeature
    const char* Name() override final;
    feature_version Version() override final;
    HWND Hwnd() override final;

    // IFGFeature_Dx12
    bool CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                         IDXGISwapChain** swapChain) override final;
    bool CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd, DXGI_SWAP_CHAIN_DESC1* desc,
                          DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGISwapChain1** swapChain) override final;
    bool ReleaseSwapchain(HWND hwnd) override final;

    void CreateContext(ID3D12Device* device, FG_Constants& fgConstants) override final;
    void Activate() override final;
    void Deactivate() override final;
    void DestroyFGContext() override final;
    bool Shutdown() override final;

    void EvaluateState(ID3D12Device* device, FG_Constants& fgConstants) override final;

    bool Present() override final;

    bool SetResource(Dx12Resource* inputResource) override final;
    void SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue) override final;

    void* FrameGenerationContext() override final;
    void* SwapchainContext() override final;

    // MFG accessors
    uint32_t GetMaxFramesToGenerate() const { return _numFramesToGenerateMax; }

    DLSSG_Dx12(UINT framesToInterpolate = 1) : IFGFeature_Dx12(), IFGFeature(framesToInterpolate)
    {
        _numFramesToGenerate = framesToInterpolate;
        if (SLProxy::Module() == nullptr)
            SLProxy::InitSL();
    }

    ~DLSSG_Dx12();

    // Inherited via IFGFeature_Dx12
    bool SetInterpolatedFrameCount(UINT interpolatedFrameCount) override;
};
