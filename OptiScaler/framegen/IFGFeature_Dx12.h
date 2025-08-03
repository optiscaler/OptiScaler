#pragma once
#include <pch.h>
#include "IFGFeature.h"

#include <upscalers/IFeature.h>

#include <shaders/resource_flip/RF_Dx12.h>

#include <dxgi1_6.h>
#include <d3d12.h>
#include <ffx_api_types.h>

struct Dx12Resource
{
  private:
    D3D12_RESOURCE_STATES state {};

  public:
    ID3D12Resource* resource {};

    void setState(D3D12_RESOURCE_STATES state) { this->state = state; }
    D3D12_RESOURCE_STATES getState() { return state; }
    FfxApiResourceState getFfxApiState()
    {
        switch (state)
        {
        case D3D12_RESOURCE_STATE_COMMON:
            return FFX_API_RESOURCE_STATE_COMMON;
        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
            return FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
            return FFX_API_RESOURCE_STATE_COMPUTE_READ;
        case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
            return FFX_API_RESOURCE_STATE_PIXEL_READ;
        case (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE):
            return FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ;
        case D3D12_RESOURCE_STATE_COPY_SOURCE:
            return FFX_API_RESOURCE_STATE_COPY_SRC;
        case D3D12_RESOURCE_STATE_COPY_DEST:
            return FFX_API_RESOURCE_STATE_COPY_DEST;
        case D3D12_RESOURCE_STATE_GENERIC_READ:
            return FFX_API_RESOURCE_STATE_GENERIC_READ;
        case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT:
            return FFX_API_RESOURCE_STATE_INDIRECT_ARGUMENT;
        case D3D12_RESOURCE_STATE_RENDER_TARGET:
            return FFX_API_RESOURCE_STATE_RENDER_TARGET;
        default:
            return FFX_API_RESOURCE_STATE_COMMON;
        }
    }
};

class IFGFeature_Dx12 : public virtual IFGFeature
{
  private:
    std::unique_ptr<RF_Dx12> _mvFlip;
    std::unique_ptr<RF_Dx12> _depthFlip;
    ID3D12Device* _device = nullptr;

  protected:
    IDXGISwapChain* _swapChain = nullptr;
    ID3D12CommandQueue* _gameCommandQueue = nullptr;
    HWND _hwnd = NULL;

    bool _velocityReady[BUFFER_COUNT] = { false, false, false, false };
    bool _depthReady[BUFFER_COUNT] = { false, false, false, false };
    bool _hudlessReady[BUFFER_COUNT] = { false, false, false, false };
    bool _uiReady[BUFFER_COUNT] = { false, false, false, false };
    bool _hudlessDispatchReady[BUFFER_COUNT] = { false, false, false, false };
    bool _noHudless[BUFFER_COUNT] = { false, false, false, false };

    Dx12Resource _paramVelocity[BUFFER_COUNT] {};
    Dx12Resource _paramVelocityCopy[BUFFER_COUNT] {};
    Dx12Resource _paramDepth[BUFFER_COUNT] {};
    Dx12Resource _paramDepthCopy[BUFFER_COUNT] {};
    Dx12Resource _paramHudless[BUFFER_COUNT] {};
    Dx12Resource _paramHudlessCopy[BUFFER_COUNT] {};
    Dx12Resource _paramUi[BUFFER_COUNT] {};
    Dx12Resource _paramUiCopy[BUFFER_COUNT] {};

    ID3D12GraphicsCommandList* _commandList[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };
    ID3D12CommandAllocator* _commandAllocators[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };

    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState,
                              ID3D12Resource** OutResource, bool UAV = false, bool depth = false);
    bool CreateBufferResourceWithSize(ID3D12Device* device, ID3D12Resource* source, D3D12_RESOURCE_STATES state,
                                      ID3D12Resource** target, UINT width, UINT height, bool UAV, bool depth);
    void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                         D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);
    bool CopyResource(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* source, ID3D12Resource** target,
                      D3D12_RESOURCE_STATES sourceState);

  public:
    virtual bool CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                                 IDXGISwapChain** swapChain) = 0;
    virtual bool CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                  DXGI_SWAP_CHAIN_DESC1* desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                  IDXGISwapChain1** swapChain) = 0;
    virtual bool ReleaseSwapchain(HWND hwnd) = 0;

    virtual void CreateContext(ID3D12Device* device, FG_Constants& fgConstants) = 0;

    virtual void EvaluateState(ID3D12Device* device, FG_Constants& fgConstants) = 0;

    virtual bool Dispatch(ID3D12GraphicsCommandList* cmdList, bool useHudless, double frameTime) = 0;

    virtual void* FrameGenerationContext() = 0;
    virtual void* SwapchainContext() = 0;

    // IFGFeature
    void ReleaseObjects() override final;

    void CreateObjects(ID3D12Device* InDevice);

    void SetVelocity(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* velocity, D3D12_RESOURCE_STATES state);
    void SetDepth(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth, D3D12_RESOURCE_STATES state);
    void SetHudless(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* hudless, D3D12_RESOURCE_STATES state,
                    bool makeCopy = false);
    void SetUI(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* ui, D3D12_RESOURCE_STATES state,
               bool makeCopy = false);

    bool NoHudless();
    ID3D12CommandList* GetCommandList();

    IFGFeature_Dx12() = default;

    // Inherited via IFGFeature
    void SetVelocityReady() override;
    void SetDepthReady() override;
    void SetHudlessReady() override;
    void SetUIReady() override;
    void SetHudlessDispatchReady() override;
    void Present() override;
    bool UpscalerInputsReady() override;
    bool HudlessReady() override;
    bool ReadyForExecute() override;
};
