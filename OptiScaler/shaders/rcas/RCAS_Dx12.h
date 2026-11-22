#pragma once
#include "RCAS_Common.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#define RCAS_NUM_OF_HEAPS 3

class RCAS_Dx12 : public Shader_Dx12, public RCAS_Common
{
  private:
    FrameDescriptorHeap _frameHeaps[RCAS_NUM_OF_HEAPS];

    ID3D12Resource* _buffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;

    ID3D12PipelineState* _pipelineStateDA = nullptr;
    ID3D12PipelineState* _pipelineStateDASDA = nullptr;

    // BaBa Neural and OptiSharpen are created when they are first used, OptiSharpen one per precision
    ID3D12PipelineState* _pipelineStateBaba = nullptr;
    bool _babaPipelineFailed = false;
    ID3D12PipelineState* _pipelineStateOpti[(size_t) OptiPrecision::Count] {};
    bool _optiPipelineFailed[(size_t) OptiPrecision::Count] {};

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    bool DispatchRCAS(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* InMotionVectors,
                      RcasConstants InConstants, ID3D12Resource* OutResource, FrameDescriptorHeap& currentHeap);
    bool DispatchDepthAdaptive(ID3D12PipelineState* pipelineState, ID3D12GraphicsCommandList* InCmdList,
                               ID3D12Resource* InResource, ID3D12Resource* InMotionVectors, ID3D12Resource* InDepth,
                               RcasConstants InConstants, ID3D12Resource* OutResource,
                               FrameDescriptorHeap& currentHeap);
    // BaBa Neural or OptiSharpen, same bindings and constants. InPrecision only matters for OptiSharpen.
    bool DispatchNeural(SharpenShader InShader, OptiPrecision InPrecision, ID3D12GraphicsCommandList* InCmdList,
                        ID3D12Resource* InResource, ID3D12Resource* InMotionVectors, RcasConstants InConstants,
                        ID3D12Resource* OutResource, FrameDescriptorHeap& currentHeap);
    bool EnsureNeuralPipeline(SharpenShader InShader, OptiPrecision InPrecision);
    ID3D12PipelineState* NeuralPipeline(SharpenShader InShader, OptiPrecision InPrecision);

  public:
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* InMotionVectors,
                  RcasConstants InConstants, ID3D12Resource* OutResource, ID3D12Resource* InDepth = nullptr);

    ID3D12Resource* Buffer() { return _buffer; }
    bool CanRender() const { return _init && _buffer != nullptr; }

    RCAS_Dx12(std::string InName, ID3D12Device* InDevice);

    ~RCAS_Dx12();
};
