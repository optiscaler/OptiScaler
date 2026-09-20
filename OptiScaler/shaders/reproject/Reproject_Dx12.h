#pragma once

#include "SysUtils.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <dxgi1_6.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>
#include <DirectXMath.h>

#define Reproject_NUM_OF_HEAPS 2

struct alignas(16) ReprojectionParams
{
    float UiDiffThreshold;
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    uint32_t EdgeMode;

    float TanHalfFovX;
    float TanHalfFovY;
    float InvTanHalfFovX;
    float InvTanHalfFovY;

    float DepthCutoff;
    uint32_t InvertedDepth;
    uint32_t ShowStaticElements;
    float Pad0;

    // Inverse reprojection rotation matrix
    DirectX::XMFLOAT4 ReprojectionRow0;
    DirectX::XMFLOAT4 ReprojectionRow1;
    DirectX::XMFLOAT4 ReprojectionRow2;
};

class Reproject_Dx12 : public Shader_Dx12
{
  private:
    FrameDescriptorHeap _frameHeaps[Reproject_NUM_OF_HEAPS];

    ID3D12Resource* _buffer {};

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

  public:
    bool CreateBufferResource(UINT index, ID3D12Device* InDevice, ID3D12Resource* InSource,
                              D3D12_RESOURCE_STATES InState);
    void SetBufferState(UINT index, ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);

    bool Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params,
                  ID3D12Resource* hudless, D3D12_RESOURCE_STATES state, ID3D12Resource* depth,
                  D3D12_RESOURCE_STATES depthState);

    Reproject_Dx12(std::string InName, ID3D12Device* InDevice);

    ~Reproject_Dx12();
};
