#pragma once

#include <pch.h>

#include "RCAS_Common.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shaders_Common.h>

#define RCAS_NUM_OF_HEAPS 2

class RCAS_Dx12
{
  private:
    struct alignas(256) InternalConstants
    {
        float Sharpness;
        float Contrast;

        // Motion Vector Stuff
        int DynamicSharpenEnabled;
        int DisplaySizeMV;
        int Debug;

        float MotionSharpness;
        float MotionTextureScale;
        float MvScaleX;
        float MvScaleY;
        float Threshold;
        float ScaleLimit;
        int DisplayWidth;
        int DisplayHeight;
    };

    std::string _name = "";
    bool _init = false;
    int _counter = 0;

    ID3D12RootSignature* _rootSignature = nullptr;
    ID3D12PipelineState* _pipelineState = nullptr;
    FrameDescriptorHeap _frameHeaps[RCAS_NUM_OF_HEAPS];

    inline static bool CreateComputeShader(ID3D12Device* device, ID3D12RootSignature* rootSignature,
                                           ID3D12PipelineState** pipelineState, ID3DBlob* shaderBlob);

    ID3D12Device* _device = nullptr;
    ID3D12Resource* _buffer = nullptr;
    ID3D12Resource* _constantBuffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;

    UINT InNumThreadsX = 32;
    UINT InNumThreadsY = 32;

  public:
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                  ID3D12Resource* InMotionVectors, RcasConstants InConstants, ID3D12Resource* OutResource);

    ID3D12Resource* Buffer() { return _buffer; }
    bool IsInit() const { return _init; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    RCAS_Dx12(std::string InName, ID3D12Device* InDevice);

    ~RCAS_Dx12();
};
