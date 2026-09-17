#include "pch.h"

#include "DlssNr_Pipeline_Dx12.h"
#include <Config.h>
#include <State.h>
#include <shaders/dlssnr/DlssNr_Dx12.h>
#include <shaders/dlssnr/DlssNr_ActiveColor.h>

namespace
{
bool HasSupportedNrSubrects(NVSDK_NGX_Parameter* parameters, bool beforeUpscale)
{
    const char* offsets[] { NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X,
                            NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y };
    for (const auto* name : offsets)
    {
        unsigned int offset = 0;
        if (parameters->Get(name, &offset) == NVSDK_NGX_Result_Success && offset != 0)
            return false;
    }
    if (beforeUpscale)
    {
        unsigned int x = 0, y = 0;
        parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, &x);
        parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, &y);
        if (x != 0 || y != 0)
            return false;
    }
    return true;
}

ID3D12Resource* NrResource(NVSDK_NGX_Parameter* parameters, const char* name, const char* fallback)
{
    ID3D12Resource* resource = GetUpscalerResource_Dx12(parameters, name);
    if (resource == nullptr)
        resource = GetUpscalerResource_Dx12(parameters, fallback);
    return resource;
}

void NrBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
               D3D12_RESOURCE_STATES after)
{
    if (resource == nullptr || before == after)
        return;
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
    commandList->ResourceBarrier(1, &barrier);
}

} // namespace

bool DlssNr::CanRunBeforeUpscale_Dx12(NVSDK_NGX_Parameter* parameters)
{
    auto* color = NrResource(parameters, NVSDK_NGX_Parameter_Color, "DLSSD.Color");
    if (color == nullptr || !HasSupportedNrSubrects(parameters, true))
        return false;
    unsigned int width = 0, height = 0;
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &width);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &height);
    const auto desc = color->GetDesc();
    return desc.MipLevels == 1 && DlssNr::PreSrColorExtent(desc, width, height).has_value();
}

DlssNr::InputStates_Dx12 DlssNr::ResolveInputStates_Dx12(bool interop)
{
    constexpr auto readable = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (interop)
        return { readable, readable, readable, readable };

    const auto& cfg = *Config::Instance();
    const bool unreal = State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                        State::Instance().gameEngine == GameEngineType::Unreal ||
                        (State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine);
    return { static_cast<D3D12_RESOURCE_STATES>(
                 cfg.ColorResourceBarrier.value_or(unreal ? D3D12_RESOURCE_STATE_RENDER_TARGET : readable)),
             static_cast<D3D12_RESOURCE_STATES>(cfg.DepthResourceBarrier.value_or(readable)),
             static_cast<D3D12_RESOURCE_STATES>(
                 cfg.MVResourceBarrier.value_or(unreal ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : readable)),
             static_cast<D3D12_RESOURCE_STATES>(cfg.ExposureResourceBarrier.value_or(readable)) };
}

ShaderPass_Dx12 MakeDlssNrPass(DlssNr_Dx12& shader, ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
                               NVSDK_NGX_Parameter* parameters, bool beforeUpscale, unsigned int featureFlags,
                               ID3D12CommandQueue* timingQueue, bool interop, bool rayReconstruction,
                               uint64_t submissionEpoch)
{
    auto* color = NrResource(parameters, NVSDK_NGX_Parameter_Color, "DLSSD.Color");
    auto* depth = NrResource(parameters, NVSDK_NGX_Parameter_Depth, "DLSSD.Depth");
    auto* motion = NrResource(parameters, NVSDK_NGX_Parameter_MotionVectors, "DLSSD.MotionVectors");
    auto* exposure = NrResource(parameters, NVSDK_NGX_Parameter_ExposureTexture, "DLSSD.ExposureTexture");
    const auto states = DlssNr::ResolveInputStates_Dx12(interop);
    const bool supportedSubrects = HasSupportedNrSubrects(parameters, beforeUpscale);

    DlssNrFrameInfo frame {};
    frame.BeforeUpscale = beforeUpscale;
    frame.PrivateColorCopy = beforeUpscale;
    frame.IndependentCommands = interop;
    frame.RayReconstruction = rayReconstruction;
    frame.SubmissionEpoch = submissionEpoch;
    frame.OutputArrivalState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    frame.DepthInverted = (featureFlags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;
    frame.MotionVectorsLowResolution = (featureFlags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) != 0;
    frame.ColourIsLinearHdr = (featureFlags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR) != 0;
    auto* finalOutput = NrResource(parameters, NVSDK_NGX_Parameter_Output, "DLSSD.Output");
    auto* colourAuthority = finalOutput != nullptr ? finalOutput : color;
    if (colourAuthority != nullptr)
    {
        switch (colourAuthority->GetDesc().Format)
        {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
            break;
        default:
            frame.ColourIsLinearHdr = false;
        }
    }
    if (finalOutput != nullptr)
    {
        frame.OutputWidth = static_cast<unsigned int>(finalOutput->GetDesc().Width);
        frame.OutputHeight = finalOutput->GetDesc().Height;
    }
    unsigned int outputWidth = 0, outputHeight = 0;
    parameters->Get(NVSDK_NGX_Parameter_OutWidth, &outputWidth);
    parameters->Get(NVSDK_NGX_Parameter_OutHeight, &outputHeight);
    if (outputWidth != 0 && outputHeight != 0)
    {
        frame.OutputWidth = outputWidth;
        frame.OutputHeight = outputHeight;
    }
    unsigned int reset = 0;
    parameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    frame.Reset = reset != 0;
    parameters->Get(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, &frame.FrameTimeMs);
    parameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &frame.MvScaleX);
    parameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &frame.MvScaleY);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &frame.PreExposure);
    if (frame.PreExposure <= 1e-6f)
        frame.PreExposure = 1.0f;
    frame.ExposureTexture = exposure;
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &frame.RenderSubrectWidth);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &frame.RenderSubrectHeight);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, &frame.DepthSubrectBaseX);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, &frame.DepthSubrectBaseY);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, &frame.MotionSubrectBaseX);
    parameters->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, &frame.MotionSubrectBaseY);

    return {
        [=, &shader](ID3D12Resource* nextOutput) -> ID3D12Resource*
        {
            if (!supportedSubrects || !Config::Instance()->DlssNrEnabled.value_or_default() || !shader.IsInit() ||
                depth == nullptr || motion == nullptr || nextOutput == nullptr)
                return nullptr;
            if (beforeUpscale)
                return color;
            if (!shader.CreateBufferResource(device, nextOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                return nullptr;
            shader.SetBufferState(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            return shader.Buffer();
        },
        [=, &shader](ID3D12Resource* input, ID3D12Resource* output) -> bool
        {
            // Every guide is returned to the upscaler's input state, including failed NR evaluations.
            struct RestoreInputs
            {
                ID3D12GraphicsCommandList* commandList;
                std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> resources;
                void Read(ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
                {
                    if (resource == nullptr ||
                        std::any_of(resources.begin(), resources.end(),
                                    [resource](const auto& entry) { return entry.first == resource; }))
                        return;
                    resources.emplace_back(resource, state);
                    NrBarrier(commandList, resource, state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }
                ~RestoreInputs()
                {
                    for (auto it = resources.rbegin(); it != resources.rend(); ++it)
                        NrBarrier(commandList, it->first, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, it->second);
                }
            } restore { commandList };

            if (beforeUpscale)
            {
                restore.Read(input, states.color);
                shader.SetBufferState(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            else
                shader.SetBufferState(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            restore.Read(depth, states.depth);
            restore.Read(motion, states.motion);
            restore.Read(exposure, states.exposure);

            const bool result = shader.Dispatch(commandList, input, depth, motion, output, frame, timingQueue);
            if (beforeUpscale)
            {
                shader.SetBufferState(commandList, states.color);
                return result;
            }
            if (!result)
            {
                // A disabled/failed optional pass must still provide the next stage with the original frame.
                shader.SetBufferState(commandList, D3D12_RESOURCE_STATE_COPY_SOURCE);
                NrBarrier(commandList, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
                commandList->CopyResource(output, input);
                NrBarrier(commandList, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            return true;
        }
    };
}

ID3D12Resource* PrepareDlssNrInput(DlssNr_Dx12& shader, ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
                                   NVSDK_NGX_Parameter* parameters, unsigned int featureFlags,
                                   ID3D12CommandQueue* timingQueue, bool interop, bool rayReconstruction,
                                   uint64_t submissionEpoch)
{
    auto* color = NrResource(parameters, NVSDK_NGX_Parameter_Color, "DLSSD.Color");
    if (color == nullptr || !shader.IsInit() || !DlssNr::CanRunBeforeUpscale_Dx12(parameters) ||
        !Config::Instance()->DlssNrEnabled.value_or_default())
        return nullptr;
    const auto desc = color->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
        desc.DepthOrArraySize != 1 || desc.MipLevels != 1 ||
        !shader.CreateBufferResource(device, color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        return nullptr;

    ShaderPipeline_Dx12 pipeline;
    pipeline.push_back(MakeDlssNrPass(shader, device, commandList, parameters, true, featureFlags, timingQueue, interop,
                                      rayReconstruction, submissionEpoch));
    SetupShaderPipeline(pipeline, shader.Buffer());
    if (pipeline.front().inputBuffer != nullptr && DispatchShaderPipeline(pipeline))
        return shader.Buffer();
    return nullptr;
}
