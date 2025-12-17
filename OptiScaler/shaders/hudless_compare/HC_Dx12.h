#pragma once

#include <pch.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <dxgi1_6.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#define HC_NUM_OF_HEAPS 2

class HC_Dx12 : public Shader_Dx12
{
  private:
    struct alignas(256) InternalCompareParams
    {
        float DiffThreshold = 0.02f;
        float PinkAmount = 1.0f;
        float InvOutputSize[2] = { 0, 0 };
    };

    FrameDescriptorHeap _frameHeaps[HC_NUM_OF_HEAPS];

    ID3D12Resource* _buffer[HC_NUM_OF_HEAPS] = {};
    D3D12_RESOURCE_STATES _bufferState[HC_NUM_OF_HEAPS] = { D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON };

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

  public:
    bool CreateBufferResource(UINT index, ID3D12Device* InDevice, ID3D12Resource* InSource,
                              D3D12_RESOURCE_STATES InState);
    void SetBufferState(UINT index, ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* hudless,
                  D3D12_RESOURCE_STATES state);

    HC_Dx12(std::string InName, ID3D12Device* InDevice);

    ~HC_Dx12();
};
