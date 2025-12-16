#pragma once

#include <pch.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shaders_Dx12Utils.h>

#define OS_NUM_OF_HEAPS 2

class OS_Dx12
{
  private:
    std::string _name = "";
    bool _init = false;
    int _counter = 0;
    bool _upsample = false;

    ID3D12RootSignature* _rootSignature = nullptr;
    ID3D12PipelineState* _pipelineState = nullptr;
    FrameDescriptorHeap _frameHeaps[OS_NUM_OF_HEAPS];

    ID3D12Device* _device = nullptr;
    ID3D12Resource* _buffer = nullptr;
    ID3D12Resource* _constantBuffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint32_t InWidth, uint32_t InHeight,
                              D3D12_RESOURCE_STATES InState);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                  ID3D12Resource* OutResource);

    ID3D12Resource* Buffer() { return _buffer; }
    bool IsUpsampling() { return _upsample; }
    bool IsInit() const { return _init; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    OS_Dx12(std::string InName, ID3D12Device* InDevice, bool InUpsample);

    ~OS_Dx12();
};
