#pragma once

#include <pch.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <dxgi1_6.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#define HudCopy_NUM_OF_HEAPS 2

class HudCopy_Dx12 : public Shader_Dx12
{
  private:
    struct alignas(256) InternalCompareParams
    {
        float DiffThreshold = 0.02f;
    };

    FrameDescriptorHeap _frameHeaps[HudCopy_NUM_OF_HEAPS];

    ID3D12Resource* _buffer = nullptr;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

  public:
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* hudless,
                  ID3D12Resource* present, D3D12_RESOURCE_STATES hudlessState, D3D12_RESOURCE_STATES presentState);

    HudCopy_Dx12(std::string InName, ID3D12Device* InDevice);

    ~HudCopy_Dx12();
};
