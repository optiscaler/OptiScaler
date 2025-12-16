#pragma once

#include <pch.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shaders_Dx12Utils.h>

class DI_Dx12
{
  private:
    std::string _name = "";
    bool _init = false;
    ID3D12RootSignature* _rootSignature = nullptr;
    ID3D12PipelineState* _pipelineState = nullptr;
    ID3D12DescriptorHeap* _srvHeap[3] = { nullptr, nullptr, nullptr };
    D3D12_CPU_DESCRIPTOR_HANDLE _cpuSrvHandle[2] { { NULL }, { NULL } };
    D3D12_CPU_DESCRIPTOR_HANDLE _cpuUavHandle[2] { { NULL }, { NULL } };
    D3D12_GPU_DESCRIPTOR_HANDLE _gpuSrvHandle[2] { { NULL }, { NULL } };
    D3D12_GPU_DESCRIPTOR_HANDLE _gpuUavHandle[2] { { NULL }, { NULL } };
    int _counter = 0;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    ID3D12Device* _device = nullptr;
    ID3D12Resource* _buffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;

  public:
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint64_t InWidth, uint32_t InHeight,
                              D3D12_RESOURCE_STATES InState);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                  ID3D12Resource* OutResource);

    ID3D12Resource* Buffer() { return _buffer; }
    bool IsInit() const { return _init; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    DI_Dx12(std::string InName, ID3D12Device* InDevice);

    ~DI_Dx12();
};
