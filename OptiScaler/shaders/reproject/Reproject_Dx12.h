#pragma once

#include "SysUtils.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <dxgi1_6.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>
#include <DirectXMath.h>

#define Reproject_NUM_OF_HEAPS 3

struct alignas(16) ReprojectionParams
{
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    float InvScreenWidth;
    float InvScreenHeight;

    float UiDiffThreshold;
    float DepthCutoff;
    float DitherWidthPx;
    uint32_t CutoffExpandPx;

    uint32_t EdgeMode;
    uint32_t ShowStaticElements;
    uint32_t InvertedDepth;
    float Pad1;

    float TanHalfFovX;
    float TanHalfFovY;
    float InvTanHalfFovX;
    float InvTanHalfFovY;

    // Inverse reprojection rotation matrix
    DirectX::XMFLOAT4 ReprojectionRow0;
    DirectX::XMFLOAT4 ReprojectionRow1;
    DirectX::XMFLOAT4 ReprojectionRow2;
};

class Reproject_Dx12 : public Shader_Dx12
{
  private:
    FrameDescriptorHeap _frameHeaps[Reproject_NUM_OF_HEAPS];

    ID3D12Resource* _buffer[Reproject_NUM_OF_HEAPS] = {};

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

  public:
    bool Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params,
                  ID3D12Resource* hudless, D3D12_RESOURCE_STATES state, ID3D12Resource* depth,
                  D3D12_RESOURCE_STATES depthState);

    Reproject_Dx12(std::string InName, ID3D12Device* InDevice);

    ~Reproject_Dx12();
};
