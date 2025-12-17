#pragma once

#include <pch.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#define RF_NUM_OF_HEAPS 2

class RF_Dx12 : public Shader_Dx12
{
  private:
    FrameDescriptorHeap _frameHeaps[RF_NUM_OF_HEAPS];

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                  ID3D12Resource* OutResource, UINT64 width, UINT height, bool velocity);

    RF_Dx12(std::string InName, ID3D12Device* InDevice);

    ~RF_Dx12();
};
