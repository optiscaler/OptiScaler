#pragma once
#include "Magnifier_Common.h"

#include "SysUtils.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <dxgi1_6.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#define Magnifier_NUM_OF_HEAPS 3

class Magnifier_Dx12 : public Shader_Dx12, public Magnifier_Common
{
  private:
    FrameDescriptorHeap _frameHeaps[Magnifier_NUM_OF_HEAPS];

    ID3D12Resource* _buffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

  public:
    bool Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState);

    ID3D12Resource* Buffer() { return _buffer; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    Magnifier_Dx12(std::string InName, ID3D12Device* InDevice);

    ~Magnifier_Dx12();
};
