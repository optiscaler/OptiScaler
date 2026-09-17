#pragma once

// The composition pass for Neural Rendering.
//
// Neural Rendering is two things, and only one of them is a shader. The model is an NGX feature --
// created and evaluated, not dispatched -- and that stays where it is. This is the other half: the
// pass that builds the tone-mapped proxy the model is shown, and then transfers the model's answer
// back onto the real frame.
//
// It is an ordinary compute shader with a constant struct, so it belongs here alongside RCAS and
// Output Scaling rather than owning a bespoke root signature and descriptor ring of its own.
//
// One shader, three modes, because all three read and write the same set of resources and differ
// only in what they compute:
//
//   Encode   the frame -> a tone-mapped proxy, plus an untouched copy to transfer against later
//   Down     the proxy -> a smaller proxy, when the model is asked to work below full resolution
//   Resolve  proxy + model answer + untouched copy -> the frame, edited

#include "DlssNr_Common.h"
#include <dlssnr/DlssNrFeature_Dx12.h>
#include <memory>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12.h>
#include <shaders/Shader_Dx12Utils.h>

// Twelve-frame descriptor budget, including two clamp bindings and two DLSS enlargement passes.
// A model chain reuses those two bindings regardless of its pass count.
#define DLSSNR_NUM_OF_HEAPS 96

class DlssNr_Dx12 : public Shader_Dx12, public DlssNr_Common
{
  private:
    struct State;
    std::unique_ptr<State> _state;
    FrameDescriptorHeap _frameHeaps[DLSSNR_NUM_OF_HEAPS];

    // One constant buffer per heap, not one for the class.
    //
    // The shared buffer in the base class suits a shader that dispatches once a frame. Three
    // dispatches recorded onto one command list all map and overwrite the same upload buffer before
    // any of them executes, so every pass ends up reading whichever constants were written last --
    // encode and downsample would run with the resolve's parameters.
    ID3D12Resource* _constantBuffers[DLSSNR_NUM_OF_HEAPS] = {};

    uint32_t _heapIndex = 0;

    // The shader reads five inputs and writes two, and not every mode uses all of them. Unused slots
    // still need a view bound -- an unbound descriptor is not an empty read, it is a read from
    // nothing -- so a stand-in is written into whichever are spare.
    static constexpr uint32_t kSrvCount = 5;
    static constexpr uint32_t kUavCount = 2;

    uint32_t _numThreadsX = 8;
    uint32_t _numThreadsY = 8;

    // ResidualAcrossRR v2: a second compute PSO built from dlssnr_residual.hlsl's own blob,
    // reusing this class's root signature and descriptor table. Kept separate so the main
    // dlssnr.hlsl blob is never regenerated (a current dxc produces materially different DXIL
    // from the committed one). Null on backends/builds where the residual shader is absent.
    ID3D12PipelineState* _residualPipelineState = nullptr;
    ID3D12PipelineState* _finishedColorPipelineState = nullptr;

  public:
    DlssNr_Dx12(std::string InName, ID3D12Device* InDevice);
    ~DlssNr_Dx12();
    static void Retire(std::unique_ptr<DlssNr_Dx12> owner);
    bool ReadyToDestroy();

    // The pass. Resources in, and nothing read from anywhere the caller cannot see.
    //
    // This is the whole filter: it brings the model up if it is not already, builds the feature and
    // rebuilds it when the tuning or the resolution changes, evaluates it, and runs the compute passes
    // that show it the frame and bring its answer back. One call, like any other shader here.
    //
    // Sizes come from the resources. Everything the pass cannot work out for itself is in
    // DlssNrFrameInfo; everything the user chose stays in Config. colour and output may be the same
    // resource. timingQueue is the queue this list will be executed on, when the caller knows it.
    bool Dispatch(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colour, ID3D12Resource* depth,
                  ID3D12Resource* motion, ID3D12Resource* output, const DlssNrFrameInfo& frame,
                  ID3D12CommandQueue* timingQueue = nullptr);

    bool CreateBufferResource(ID3D12Device* device, ID3D12Resource* source, D3D12_RESOURCE_STATES state);
    void SetBufferState(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES state);
    ID3D12Resource* Buffer();
    bool CanRender() const;
    void DiagnosePipeline(unsigned stage, ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params,
                          ID3D12Resource* color, uint32_t flags, bool rr, bool success = true);
    void BeginInputHold(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* params,
                        const D3D12_RESOURCE_STATES* inputStates);
    void EndInputHold(NVSDK_NGX_Parameter* params);
    bool ProcessSeam(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* params, bool beforeUpscale,
                     ID3D12CommandQueue* queue, bool rayReconstruction, unsigned long long submissionEpoch,
                     bool interop = false, uint32_t featureFlags = 0);
    void ResetFinishedCommands(ID3D12CommandList* cmd);
    void SubmitFinishedCommands(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);
    bool WaitFinished();
    void ApplyFinished(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue);
    void ApplyStreamlineFinished(IDXGISwapChain* swapchain, ID3D12Resource* picture, ID3D12CommandQueue* queue);
    void ApplyFinishedDx11(IDXGISwapChain* swapchain);
    std::string FinishedStatus();
    std::string DeferredStatus();
    DlssNr::CalibrationReading CalibrationStatus();

    // Records one pass. Resources that a given mode does not read may be null; a stand-in is bound in
    // their place so every descriptor in the table is valid.
    // One compute pass. The public entry below drives three of these plus the model.
    bool DispatchPass(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                      ID3D12Resource* InSource, ID3D12Resource* InModel, ID3D12Resource* InOriginal,
                      ID3D12Resource* InMotion,
                      // Vestigial. Fed to the slot the removed edit accumulator read its history from;
                      // nothing reads it now and every caller passes nullptr. Kept only so the binding
                      // table keeps its shape -- not evidence that temporal accumulation exists.
                      ID3D12Resource* InPrevEdit, ID3D12Resource* OutTarget, ID3D12Resource* OutKeep,
                      // Initialize to UINT32_MAX. Reuse only with identical bindings/constants in one chain.
                      uint32_t* immutableSlot = nullptr);

    // One compute pass of the ResidualAcrossRR v2 shader (dlssnr_residual.hlsl). Same descriptor
    // table shape as DispatchPass; binds _residualPipelineState instead of _pipelineState. t4/u1
    // are bound with a stand-in for parity. Returns false (no-op) if the residual PSO is absent.
    bool DispatchResidualPass(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                              ID3D12Resource* InSource, ID3D12Resource* InModel, ID3D12Resource* InOriginal,
                              ID3D12Resource* InMotion, ID3D12Resource* OutTarget, bool finishedColor = false);
};
