#pragma once
#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#define RC_NUM_OF_HEAPS 3

// Copies a rectangle between textures through an SRV and a UAV, used to crop and place DLSS subrects
class RC_Dx12 : public Shader_Dx12
{
  private:
    struct alignas(256) InternalConstants
    {
        uint32_t SrcOffset[2];
        uint32_t DstOffset[2];
        uint32_t Size[2];
    };

    FrameDescriptorHeap _frameHeaps[RC_NUM_OF_HEAPS];

    ID3D12Resource* _buffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;
    bool _depth = false;

    UINT InNumThreadsX = 16;
    UINT InNumThreadsY = 16;

  public:
    // Grows only by default, so dynamic resolution doesn't recreate it every frame. Passes that size their work by
    // the buffer need it exact.
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint32_t InWidth, uint32_t InHeight,
                              D3D12_RESOURCE_STATES InState, bool InGrow = true);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource,
                  uint32_t InSrcX, uint32_t InSrcY, uint32_t InDstX, uint32_t InDstY, uint32_t InWidth,
                  uint32_t InHeight);

    ID3D12Resource* Buffer() { return _buffer; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    // InDepth: the source is a depth buffer, read as depth and copied to R32_FLOAT
    RC_Dx12(std::string InName, ID3D12Device* InDevice, bool InDepth = false);

    ~RC_Dx12();
};
