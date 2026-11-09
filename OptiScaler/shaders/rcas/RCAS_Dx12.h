#pragma once
#include "RCAS_Common.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#define RCAS_NUM_OF_HEAPS 3

class RCAS_Dx12 : public Shader_Dx12
{
  private:
    enum class SharpenShader : uint32_t
    {
        RCAS,
        DepthAdaptive
    };

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

    struct alignas(256) InternalConstantsDA
    {
        float Sharpness;

        int DepthIsLinear;
        int DepthIsReversed;

        float DepthScale;
        float DepthBias;

        float DepthLinearA;
        float DepthLinearB;
        float DepthLinearC;

        int DynamicSharpenEnabled;
        int DisplaySizeMV;
        int Debug;

        float MotionSharpness;
        float MotionTextureScale;
        float MvScaleX;
        float MvScaleY;
        float MotionThreshold;
        float MotionScaleLimit;

        float DepthTextureScale;

        int ClampOutput;

        int DisplayWidth;
        int DisplayHeight;
        int MotionWidth;
        int MotionHeight;
        int DepthWidth;
        int DepthHeight;
    };

    FrameDescriptorHeap _frameHeaps[RCAS_NUM_OF_HEAPS];
    ID3D12Resource* _constantBuffers[RCAS_NUM_OF_HEAPS] {};

    ID3D12Resource* _buffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;

    ID3D12PipelineState* _pipelineStateDA = nullptr;
    ID3D12PipelineState* _pipelineStateLCDA = nullptr;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    bool CreatePipelineState(ID3D12Device* InDevice, const void* InShaderData, size_t InShaderSize,
                             ID3D12PipelineState** OutPipelineState);
    bool CreatePipelineState(ID3D12Device* InDevice, const std::string& InShaderCode,
                             ID3D12PipelineState** OutPipelineState, D3D12_SHADER_BYTECODE byteCode);
    static void FillMotionConstants(InternalConstants& OutConstants, const RcasConstants& InConstants);
    static void FillMotionConstants(InternalConstantsDA& OutConstants, const RcasConstants& InConstants);
    bool DispatchRCAS(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                      ID3D12Resource* InMotionVectors, RcasConstants InConstants, ID3D12Resource* OutResource,
                      FrameDescriptorHeap& currentHeap);
    bool DispatchDepthAdaptive(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                               ID3D12Resource* InMotionVectors, ID3D12Resource* InDepth, RcasConstants InConstants,
                               ID3D12Resource* OutResource, FrameDescriptorHeap& currentHeap);
    bool DispatchDASDepthAdaptive(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList,
                                  ID3D12Resource* InResource, ID3D12Resource* InMotionVectors, ID3D12Resource* InDepth,
                                  RcasConstants InConstants, ID3D12Resource* OutResource,
                                  FrameDescriptorHeap& currentHeap);

  public:
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                  ID3D12Resource* InMotionVectors, RcasConstants InConstants, ID3D12Resource* OutResource,
                  ID3D12Resource* InDepth = nullptr);

    ID3D12Resource* Buffer() { return _buffer; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    RCAS_Dx12(std::string InName, ID3D12Device* InDevice);

    ~RCAS_Dx12();
};
