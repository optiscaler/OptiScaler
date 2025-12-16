#pragma once

#include <pch.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shaders_Dx12Utils.h>

#define RF_NUM_OF_HEAPS 2

class RF_Dx12
{
  private:
    std::string _name = "";
    bool _init = false;
    int _counter = 0;

    ID3D12RootSignature* _rootSignature = nullptr;
    ID3D12PipelineState* _pipelineState = nullptr;
    FrameDescriptorHeap _frameHeaps[RF_NUM_OF_HEAPS];

    ID3D12Device* _device = nullptr;
    ID3D12Resource* _constantBuffer = nullptr;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                  ID3D12Resource* OutResource, UINT64 width, UINT height, bool velocity);

    bool IsInit() const { return _init; }

    RF_Dx12(std::string InName, ID3D12Device* InDevice);

    ~RF_Dx12();
};
