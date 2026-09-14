#pragma once

#include "SysUtils.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <dxgi1_6.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>
#include <DirectXMath.h>

#define Reproject_NUM_OF_HEAPS 2

struct alignas(256) ReprojectionParams
{
    float DiffThreshold;
    float PinkAmount;
    float MouseDeltaX;
    float MouseDeltaY;

    float ScreenWidth;
    float ScreenHeight;
    float CameraVFov;
    float CameraAspectRatio;

    uint32_t EdgeMode;
    float TanHalfFovY;
    float InvTanHalfFovY;
    float InvTanHalfFovX;

    // Row 0 of inverse reprojection rotation matrix
    DirectX::XMFLOAT3 ReprojectionRow0;
    float _pad0;

    // Row 1
    DirectX::XMFLOAT3 ReprojectionRow1;
    float _pad1;

    // Row 2
    DirectX::XMFLOAT3 ReprojectionRow2;
    float _pad2;
};

class Reproject_Dx12 : public Shader_Dx12
{
  private:
    FrameDescriptorHeap _frameHeaps[Reproject_NUM_OF_HEAPS];

    ID3D12Resource* _buffer[Reproject_NUM_OF_HEAPS] = {};
    D3D12_RESOURCE_STATES _bufferState[Reproject_NUM_OF_HEAPS] {};

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
