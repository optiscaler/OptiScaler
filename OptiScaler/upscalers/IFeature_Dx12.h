#pragma once
#include <d3d12.h>
#include "IFeature.h"

#include "SysUtils.h"
#include <Util.h>
#include <menu/menu_dx12.h>
#include <shaders/output_scaling/OS_Dx12.h>
#include <shaders/rcas/RCAS_Dx12.h>
#include <shaders/bias/Bias_Dx12.h>
#include <shaders/magnifier/Magnifier_Dx12.h>
#include <shaders/resource_copy/RC_Dx12.h>
#include <gpu_time/GpuTime_Dx12.h>

class IFeature_Dx12 : public virtual IFeature
{
  private:
    struct ShaderPass
    {
        // Requests the target buffer it needs to write to. Returns the buffer the PREVIOUS stage must write to
        std::function<ID3D12Resource*(ID3D12Resource* nextOutput)> Setup;

        // Runs the shader
        std::function<bool(ID3D12Resource* input, ID3D12Resource* output)> Dispatch;

        // Internal state tracked by the pipeline setup loop
        ID3D12Resource* inputBuffer = nullptr;
        ID3D12Resource* outputBuffer = nullptr;
    };

    std::vector<std::tuple<ID3D12Resource*, D3D12_RESOURCE_STATES, D3D12_RESOURCE_STATES>> _barrierTransitions;

    void RestoreBarriers(ID3D12GraphicsCommandList* InCommandList);

    // PadToMaxUpscaleSize quirk, the inputs padded to the max upscale size's render size and the output crop
    enum PadSlot : uint8_t
    {
        PadColor,
        PadDepth,
        PadMV,
        PadMask,
        PadReactive,
        PadTransparency,
        PadOutput,
        PadSlotCount
    };

    std::array<std::unique_ptr<RC_Dx12>, PadSlotCount> PadCopy;
    bool _padWarned = false;

    // Game's resources and render size the padding replaced, restored after EvaluateInternal
    struct PadRestore
    {
        std::vector<std::tuple<const char*, ID3D12Resource*, ID3D12Resource*>> Resources; // Name, game's, padded
        unsigned int RenderWidth = 0;
        unsigned int RenderHeight = 0;
    };

    bool IsPadBuffer(ID3D12Resource* InResource) const;
    ID3D12Resource* PadInput(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource, PadSlot InSlot,
                             uint32_t InWidth, uint32_t InHeight, uint32_t InPadWidth, uint32_t InPadHeight);

    // Replaces the inputs and the render size with padded ones and returns the upscale size to crop the output to.
    // A failed padding turns it off and recreates the upscaler for the upscale size.
    bool PadInputs(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters, PadRestore& OutRestore,
                   uint32_t& OutUpscaleWidth, uint32_t& OutUpscaleHeight);
    void RestorePadding(NVSDK_NGX_Parameter* InParameters, const PadRestore& InRestore);

  protected:
    ID3D12Device* Device = nullptr;
    static inline std::unique_ptr<Menu_Dx12> Imgui = nullptr;
    std::unique_ptr<OS_Dx12> OutputScaler = nullptr;
    std::unique_ptr<RCAS_Dx12> RCAS = nullptr;
    std::unique_ptr<Bias_Dx12> Bias = nullptr;
    std::unique_ptr<Magnifier_Dx12> Magnifier = nullptr;

    std::unique_ptr<GpuTime_Dx12> UpscalerTime = nullptr;

    // Created on the first view that doesn't start at (0, 0)
    std::array<std::unique_ptr<RC_Dx12>, (size_t) SubrectSlot::Count> SubrectCopy;
    std::array<ID3D12Resource*, (size_t) SubrectSlot::Count> _subrectCopied {}; // This frame's copies

    void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                         D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState) const;

    // Recorded transitions are undone by Evaluate after the shader passes
    void ForwardBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                        D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

    // Warns once when the subrect doesn't fit the resource, logs it once and creates its copy shader
    bool CheckSubrect(SubrectSlot InSlot, const Subrect& InRect, ID3D12Resource* InResource);

    // Resource for backends without subrect support, a copy of the view when it doesn't start at (0, 0).
    // Call after the barrier forwarding, the copy is ours and must not get ForwardBarrier.
    ID3D12Resource* SubrectInput(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                 SubrectSlot InSlot);

    // Masks with integer formats can't be read as float, they are treated as not given
    ID3D12Resource* MaskInput(ID3D12Resource* InResource);

    virtual bool InitInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) = 0;
    virtual bool EvaluateInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) = 0;

  public:
    bool Init(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters);
    bool Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters);

    API Api() const override { return API::DX12; }
    std::optional<double> ReadUpscalerTime(void* commandQueue) override;
    void ReadDetailedGpuTimes(void* commandQueue, std::vector<DetailedGpuTime>& detailedGpuTimes) override;

    IFeature_Dx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    ~IFeature_Dx12();
};
