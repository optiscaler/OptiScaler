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
    ID3D12RootSignature* _rootSignature = nullptr;
    ID3D12PipelineState* _pipelineState = nullptr;
    ID3D12DescriptorHeap* _srvHeap[3] = { nullptr, nullptr, nullptr };
    D3D12_CPU_DESCRIPTOR_HANDLE _cpuSrvHandle[2] { { NULL }, { NULL } };
    D3D12_CPU_DESCRIPTOR_HANDLE _cpuUavHandle[2] { { NULL }, { NULL } };
    D3D12_CPU_DESCRIPTOR_HANDLE _cpuCbvHandle[2] { { NULL }, { NULL } };
    D3D12_GPU_DESCRIPTOR_HANDLE _gpuSrvHandle[2] { { NULL }, { NULL } };
    D3D12_GPU_DESCRIPTOR_HANDLE _gpuUavHandle[2] { { NULL }, { NULL } };
    D3D12_GPU_DESCRIPTOR_HANDLE _gpuCbvHandle[2] { { NULL }, { NULL } };
    int _counter = 0;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    ID3D12Device* _device = nullptr;
    ID3D12Resource* _constantBuffer = nullptr;

  public:
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                  ID3D12Resource* OutResource, UINT64 width, UINT height, bool velocity);

    bool IsInit() const { return _init; }

    RF_Dx12(std::string InName, ID3D12Device* InDevice);

    ~RF_Dx12();
};
