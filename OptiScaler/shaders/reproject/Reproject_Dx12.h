#pragma once

#include "SysUtils.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <dxgi1_6.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>
#include "Reproject.h"

#define Reproject_NUM_OF_HEAPS 3

class Reproject_Dx12 : public Shader_Dx12, public Reproject
{
  private:
    FrameDescriptorHeap _frameHeaps[Reproject_NUM_OF_HEAPS];

    // Late latching
    ID3D12PipelineState* _latchPipeline = nullptr;
    ID3D12Resource* _liveInput = nullptr; // upload heap, persistently mapped, written by the input thread
    void* _liveInputMapped = nullptr;
    ID3D12Resource* _latchedOutput = nullptr; // rotation rows + used mouse delta, written by the latch pass

    ID3D12Resource* _buffer[Reproject_NUM_OF_HEAPS] = {};
    ID3D12Resource* _constantBuffers[Reproject_NUM_OF_HEAPS] = {};

    // Depth cutout mask prepass, separable dilation by CutoffExpandPx
    // Both are kept in D3D12_RESOURCE_STATE_UNORDERED_ACCESS between dispatches
    ID3D12PipelineState* _maskPipelineH = nullptr;
    ID3D12PipelineState* _maskPipelineV = nullptr;
    ID3D12Resource* _depthMaskTemp = nullptr;
    ID3D12Resource* _depthMask = nullptr;

    // Motion vector statistics, see reproject_stats.hlsl
    ID3D12PipelineState* _statsPipeline = nullptr;
    ID3D12Resource* _statsOutput = nullptr; // kept in D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    ID3D12Resource* _statsReadback[StatsLatency] = {};

    // Direction cache, ping-ponged cubemaps, color + (distance, confidence)
    // Kept in D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE between dispatches
    ID3D12PipelineState* _cachePipeline = nullptr;
    ID3D12Resource* _cacheColor[2] = {};
    ID3D12Resource* _cacheGeo[2] = {};

    DXGI_FORMAT _loggedPresentFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT _loggedHudlessFormat = DXGI_FORMAT_UNKNOWN;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    bool CreateMaskResource(ID3D12Resource** resource, uint32_t width, uint32_t height, const wchar_t* name);
    bool CreateCacheResources(uint32_t size, DXGI_FORMAT colorFormat);
    bool CreateStatsResources();
    void ReleaseCacheResources();

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

  public:
    // Leaves present unmodified, new image available via GetCurrentBuffer
    bool Dispatch(ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params, ID3D12Resource* realPresent,
                  D3D12_RESOURCE_STATES realPresentState, ID3D12Resource* fakePresent,
                  D3D12_RESOURCE_STATES fakePresentState, ID3D12Resource* hudless, D3D12_RESOURCE_STATES hudlessState,
                  ID3D12Resource* depth, D3D12_RESOURCE_STATES depthState, ID3D12Resource* velocity = nullptr,
                  D3D12_RESOURCE_STATES velocityState = D3D12_RESOURCE_STATE_COMMON);

    bool Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params,
                  ID3D12Resource* hudless, D3D12_RESOURCE_STATES state, ID3D12Resource* depth,
                  D3D12_RESOURCE_STATES depthState, ID3D12Resource* velocity = nullptr,
                  D3D12_RESOURCE_STATES velocityState = D3D12_RESOURCE_STATE_COMMON);

    // Can be called only after Dispatch
    // Expected to be in D3D12_RESOURCE_STATE_COPY_SOURCE
    ID3D12Resource* GetCurrentBuffer();

    Reproject_Dx12(std::string InName, ID3D12Device* InDevice);

    ~Reproject_Dx12();
};
