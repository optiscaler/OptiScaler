#pragma once
#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#define CC_NUM_OF_HEAPS 3

// Copies color into an RGBA16F buffer with Inf clamped to the fp16 range and NaN zeroed.
//
// The INT8 FSR 4 models (INT8, INT8 Lite) blow up when the color has a +Inf pixel and the camera moves: the bad
// values get reprojected with the history and spread over the frame as white, blocky garbage until a reset.
// FP8 handles the same input fine. Games can write +Inf into fp16 targets, e.g. overflowing additive effects.
class ColorClamp_Dx12 : public Shader_Dx12
{
  private:
    FrameDescriptorHeap _frameHeaps[CC_NUM_OF_HEAPS];

    ID3D12Resource* _buffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;

    UINT InNumThreadsX = 16;
    UINT InNumThreadsY = 16;

  public:
    // Same size as InSource, always RGBA16F, which holds every clamped value exactly
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);

    // InResource has to be readable by compute shaders, the buffer has to be in UNORDERED_ACCESS
    bool Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource);

    ID3D12Resource* Buffer() { return _buffer; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    // Only float formats can hold Inf or NaN
    static bool NeedsClamp(DXGI_FORMAT InFormat);

    ColorClamp_Dx12(std::string InName, ID3D12Device* InDevice);

    ~ColorClamp_Dx12();
};
