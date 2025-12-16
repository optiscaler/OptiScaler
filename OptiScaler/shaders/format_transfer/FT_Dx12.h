#pragma once

#include <pch.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shaders_Dx12Utils.h>

#define FT_NUM_OF_HEAPS 2

class FT_Dx12
{
  private:
    std::string _name = "";
    bool _init = false;
    int _counter = 0;

    ID3D12RootSignature* _rootSignature = nullptr;
    ID3D12PipelineState* _pipelineState = nullptr;
    FrameDescriptorHeap _frameHeaps[FT_NUM_OF_HEAPS];

    ID3D12Device* _device = nullptr;
    ID3D12Resource* _buffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;
    DXGI_FORMAT format;

    uint32_t bufferWidth = 0;
    uint32_t bufferHeight = 0;

    uint32_t InNumThreadsX = 512;
    uint32_t InNumThreadsY = 1;

  public:
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                  ID3D12Resource* OutResource);

    ID3D12Resource* Buffer() { return _buffer; }
    bool IsInit() const { return _init; }
    bool CanRender() const { return _init && _buffer != nullptr; }
    DXGI_FORMAT Format() const { return format; }

    FT_Dx12(std::string InName, ID3D12Device* InDevice, DXGI_FORMAT InFormat);

    bool IsFormatCompatible(DXGI_FORMAT InFormat);

    ~FT_Dx12();
};
