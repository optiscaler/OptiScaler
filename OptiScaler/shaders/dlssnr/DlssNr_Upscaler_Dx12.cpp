#include "pch.h"
#include "DlssNr_Upscaler_Dx12.h"
#include <proxies/NVNGX_Proxy.h>
#include <cstring>
#include <cmath>
#include <vector>
#include <cstdio>

#include <dlssnr/DlssNr_Upscaler.h>
#include <proxies/FfxApi_Proxy.h>
#include <proxies/XeSS_Proxy.h>
#include <fsr2/ffx_fsr2.h>
#include <fsr2/dx12/ffx_fsr2_dx12.h>

namespace DlssNr
{
namespace
{
// NGX RR keys from NVIDIA's nvsdk_ngx_defs_dlssd.h; no additional runtime or SDK dependency.
constexpr const char* rrKeys[] = { "DLSS.Input.DiffuseAlbedo", "DLSS.Input.SpecularAlbedo",
    "GBuffer.Normals", "GBuffer.Roughness", "MotionVectorsReflection",
    "DLSSD.SpecularHitDistance", "DLSSD.DiffuseHitDistance" };
constexpr const char* rrOffsetPrefixes[] = { "DLSS.Input.DiffuseAlbedo", "DLSS.Input.SpecularAlbedo",
    "DLSS.Input.Normals", "DLSS.Input.Roughness", nullptr,
    "DLSSD.SpecularHitDistance", "DLSSD.DiffuseHitDistance" };
}

PrivateRrInputsDx12 PrivateUpscalerDx12::ReadRrInputs(NVSDK_NGX_Parameter* source, unsigned width, unsigned height)
{
    PrivateRrInputsDx12 result;
    if (!source) return result;
    source->Get("DLSS.Roughness.Mode", &result.roughnessMode);
    source->Get("DLSS.Use.HW.Depth", &result.hardwareDepth);
    for (unsigned i = 0; i < result.guides.size(); ++i)
    {
        auto*& resource = result.guides[i].resource;
        source->Get(rrKeys[i], &resource);
        if (!resource)
        {
            void* untyped = nullptr;
            source->Get(rrKeys[i], &untyped);
            resource = static_cast<ID3D12Resource*>(untyped);
        }
        if (rrOffsetPrefixes[i])
        {
            source->Get((std::string(rrOffsetPrefixes[i]) + ".Subrect.Base.X").c_str(), &result.baseX[i]);
            source->Get((std::string(rrOffsetPrefixes[i]) + ".Subrect.Base.Y").c_str(), &result.baseY[i]);
        }
        if (resource)
        {
            const auto desc = resource->GetDesc();
            if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
                desc.Width < UINT64(result.baseX[i]) + width || UINT64(desc.Height) < UINT64(result.baseY[i]) + height)
                return result;
        }
    }
    void *world = nullptr, *view = nullptr;
    source->Get("WorldToViewMatrix", &world);
    source->Get("ViewToClipMatrix", &view);
    if (world && view)
    {
        std::memcpy(result.worldToView.data(), world, sizeof(result.worldToView));
        std::memcpy(result.viewToClip.data(), view, sizeof(result.viewToClip));
        result.matrices = std::all_of(result.worldToView.begin(), result.worldToView.end(),
                                      [](float v) { return std::isfinite(v); }) &&
                          std::all_of(result.viewToClip.begin(), result.viewToClip.end(),
                                      [](float v) { return std::isfinite(v); });
    }
    result.valid = result.roughnessMode <= 1 && result.hardwareDepth <= 1 &&
                   result.guides[0].resource && result.guides[1].resource && result.guides[2].resource &&
                   (result.roughnessMode == 1 || result.guides[3].resource) &&
                   (result.guides[4].resource || (result.guides[5].resource && result.matrices));
    return result;
}

// Only the private NR carrier uses this adapter. Reuse the existing runtime loaders and linked
// FSR2 backend without entering IFeature's game settings, post-processing, overlay or NR hooks.
// The owning Generation must wait for its GPU completion marker before destroying this object.
struct PrivateUpscalerDx12::Impl
{
    PrivateUpscaler backend;
    FfxFsr2Context fsr2 {};
    void* scratch = nullptr;
    bool fsr2Ready = false;
    ffxContext ffx = nullptr;
    xess_context_handle_t xess = nullptr;
    unsigned width = 0, height = 0, outWidth = 0, outHeight = 0;
    bool rayReconstruction = false;
    unsigned roughnessMode = 0, hardwareDepth = 1;
    std::string error = "runtime/device/input size";
    bool NgxError(const char* operation, NVSDK_NGX_Result result)
    {
        char message[96];
        std::snprintf(message, sizeof(message), "%s returned 0x%08X", operation, (unsigned)result);
        error = message;
        return false;
    }

    static void Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                        D3D12_RESOURCE_STATES after)
    {
        if (before == after)
            return;
        D3D12_RESOURCE_BARRIER b {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = { resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after };
        cmd->ResourceBarrier(1, &b);
    }

    // Camera values have already been resolved and snapshotted by the NR seam.
    template <class Dispatch> void FrameParameters(Dispatch& d, const PrivateUpscalerFrameDx12& f)
    {
        d.jitterOffset = { f.jitterX,
                           f.jitterY };
        d.motionVectorScale = { f.motionScaleX,
                                f.motionScaleY };
        d.renderSize = { width, height };
        d.frameTimeDelta = std::max(f.frameTimeMs, 0.01f);
        d.preExposure = 1.0f;
        d.reset = f.reset != 0;
        d.cameraNear = f.cameraNear;
        d.cameraFar = f.cameraFar;
        d.cameraFovAngleVertical = f.cameraFovVertical;
        d.viewSpaceToMetersFactor = f.viewSpaceToMeters;
        // Zero initialization leaves sharpening, reactive masks and auto exposure disabled.
    }

  public:
    explicit Impl(PrivateUpscaler selected) : backend(selected) {}
    NVSDK_NGX_Parameter* parameters = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    ~Impl()
    {
        if (feature)
            NVNGXProxy::D3D12_ReleaseFeature()(feature);
        if (parameters)
            NVNGXProxy::D3D12_DestroyParameters()(parameters);
        if (fsr2Ready)
            ffxFsr2ContextDestroy(&fsr2);
        free(scratch);
        if (ffx)
            FfxApiProxy::D3D12_DestroyContext(&ffx, nullptr);
        if (xess)
            XeSSProxy::DestroyContext()(xess);
    }

    bool Init(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const PrivateUpscalerCreateDx12& info)
    {
        width = info.width;
        height = info.height;
        outWidth = info.outputWidth;
        outHeight = info.outputHeight;
        const bool inverted = info.depthInverted;
        const bool jittered = info.jitteredMotion;
        const bool highMv = !info.lowResolutionMotion;
        if (backend == PrivateUpscaler::DLSS)
        {
            rayReconstruction = info.rayReconstruction;
            roughnessMode = info.roughnessMode;
            hardwareDepth = info.hardwareDepth;
            if (!NVNGXProxy::InitDx12(device) || !NVNGXProxy::D3D12_AllocateParameters() ||
                !NVNGXProxy::D3D12_DestroyParameters() || !NVNGXProxy::D3D12_CreateFeature() ||
                !NVNGXProxy::D3D12_EvaluateFeature() || !NVNGXProxy::D3D12_ReleaseFeature())
            {
                error = "NGX initialization or required entry point unavailable";
                return false;
            }
            const auto allocated = NVNGXProxy::D3D12_AllocateParameters()(&parameters);
            if (allocated != NVSDK_NGX_Result_Success) return NgxError("AllocateParameters", allocated);
            if (!parameters) { error = "AllocateParameters returned no parameter table"; return false; }
            auto* p = parameters;
            p->Set(NVSDK_NGX_Parameter_Width, width);
            p->Set(NVSDK_NGX_Parameter_Height, height);
            p->Set(NVSDK_NGX_Parameter_OutWidth, outWidth);
            p->Set(NVSDK_NGX_Parameter_OutHeight, outHeight);
            p->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
            p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
            p->Set(NVSDK_NGX_Parameter_PerfQualityValue, info.quality);
            unsigned flags = (inverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0) |
                             (jittered ? NVSDK_NGX_DLSS_Feature_Flags_MVJittered : 0) |
                             (!highMv ? NVSDK_NGX_DLSS_Feature_Flags_MVLowRes : 0) |
                             (rayReconstruction ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0);
            p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, flags);
            if (rayReconstruction)
            {
                // RR game profiles can reject LDR creation (Cyberpunk: BAD00005).
                // The bounded residual is a linear float signal; keep exposure fixed at 1.
                // Match NVIDIA's RR creation helper, including required fields whose
                // absence may appear to work with a standalone driver's default table.
                p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, static_cast<int>(flags));
                p->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0);
                p->Set("DLSS.Denoise.Mode", 1);
                p->Set("DLSS.Roughness.Mode", roughnessMode);
                p->Set("DLSS.Use.HW.Depth", hardwareDepth);
            }
            // Match the game-facing DLSSD creation path: private heaps are not game FG heaps.
            ScopedSkipHeapCapture skipHeapCapture {};
            NVNGXProxy::ScopedFeatureCreationTrace trace;
            const auto created = NVNGXProxy::D3D12_CreateFeature()(
                cmd, rayReconstruction ? NVSDK_NGX_Feature_RayReconstruction : NVSDK_NGX_Feature_SuperSampling,
                p, &feature);
            if (created != NVSDK_NGX_Result_Success) return NgxError("CreateFeature", created);
            if (!feature) { error = "CreateFeature returned no feature handle"; return false; }
            return true;
        }
        ScopedSkipSpoofingGlobal skipSpoofing {};
        ScopedSkipHeapCapture skipCapture {};
        if (backend == PrivateUpscaler::FSR22)
        {
            FfxFsr2ContextDescription d {};
            const auto size = ffxFsr2GetScratchMemorySizeDX12();
            scratch = calloc(size, 1);
            if (!scratch || ffxFsr2GetInterfaceDX12(&d.callbacks, device, scratch, size) != FFX_OK)
                return false;
            d.device = ffxGetDeviceDX12(device);
            d.maxRenderSize = { width, height };
            d.displaySize = { outWidth, outHeight };
            if (inverted)
                d.flags |= FFX_FSR2_ENABLE_DEPTH_INVERTED;
            if (jittered)
                d.flags |= FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
            if (highMv)
                d.flags |= FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
            fsr2Ready = ffxFsr2ContextCreate(&fsr2, &d) == FFX_OK;
            return fsr2Ready;
        }
        if (backend == PrivateUpscaler::FFX)
        {
            FfxApiProxy::InitFfxDx12();
            if (!FfxApiProxy::IsSRReady(false))
                return false;
            ffxCreateBackendDX12Desc dx {};
            dx.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
            dx.device = device;
            ffxCreateContextDescUpscale d {};
            d.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            d.header.pNext = &dx.header;
            d.maxRenderSize = { width, height };
            d.maxUpscaleSize = { outWidth, outHeight };
            if (inverted)
                d.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
            if (jittered)
                d.flags |= FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
            if (highMv)
                d.flags |= FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
            // Let the loaded runtime choose its supported provider. Do not change the game's
            // FfxUpscalerIndex or provider list, and do not promise FSR4 on unsupported hardware.
            return FfxApiProxy::D3D12_CreateContext(&ffx, &d.header, nullptr) == FFX_API_RETURN_OK && ffx;
        }
        if (backend == PrivateUpscaler::XeSS)
        {
            XeSSProxy::InitXeSS();
            if (!XeSSProxy::D3D12CreateContext() || !XeSSProxy::D3D12Init() || !XeSSProxy::D3D12Execute() ||
                !XeSSProxy::DestroyContext() || !XeSSProxy::SetVelocityScale() ||
                !XeSSProxy::GetOptimalInputResolution())
                return false;
            if (XeSSProxy::D3D12CreateContext()(device, &xess) != XESS_RESULT_SUCCESS || !xess)
                return false;
            xess_d3d12_init_params_t d {};
            d.outputResolution = { outWidth, outHeight };
            d.initFlags = XESS_INIT_FLAG_LDR_INPUT_COLOR | XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE;
            if (inverted)
                d.initFlags |= XESS_INIT_FLAG_INVERTED_DEPTH;
            if (jittered)
                d.initFlags |= XESS_INIT_FLAG_JITTERED_MV;
            if (highMv)
                d.initFlags |= XESS_INIT_FLAG_HIGH_RES_MV;
            // Select the closest supported quality mode whose dynamic input range includes the
            // actual carrier size. NGX quality enums and XeSS ratios are not interchangeable.
            bool found = false;
            unsigned long long best = ~0ull;
            for (auto quality :
                 { XESS_QUALITY_SETTING_AA, XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, XESS_QUALITY_SETTING_ULTRA_QUALITY,
                   XESS_QUALITY_SETTING_QUALITY, XESS_QUALITY_SETTING_BALANCED, XESS_QUALITY_SETTING_PERFORMANCE,
                   XESS_QUALITY_SETTING_ULTRA_PERFORMANCE })
            {
                xess_2d_t optimal {}, minimum {}, maximum {};
                if (XeSSProxy::GetOptimalInputResolution()(xess, &d.outputResolution, quality, &optimal, &minimum,
                                                           &maximum) != XESS_RESULT_SUCCESS)
                    continue;
                if (width < minimum.x || height < minimum.y || width > maximum.x || height > maximum.y)
                    continue;
                auto distance = static_cast<unsigned long long>(std::abs((long long) optimal.x - width) +
                                                                std::abs((long long) optimal.y - height));
                if (distance < best)
                {
                    best = distance;
                    d.qualitySetting = quality;
                    found = true;
                }
            }
            return found && XeSSProxy::D3D12Init()(xess, &d) == XESS_RESULT_SUCCESS;
        }
        return false;
    }

    bool Evaluate(ID3D12GraphicsCommandList* cmd, const PrivateUpscalerFrameDx12& f)
    {
        auto* color = f.color.resource;
        auto* output = f.output.resource;
        auto* depth = f.depth.resource;
        auto* motion = f.motion.resource;
        auto* exposure = f.exposure.resource;
        if (!color || !output || !depth || !motion || !exposure ||
            f.width != width || f.height != height || f.outputWidth != outWidth || f.outputHeight != outHeight)
            return false;
        if (rayReconstruction && (!f.rr.valid || f.rr.roughnessMode != roughnessMode ||
                                   f.rr.hardwareDepth != hardwareDepth)) return false;
        std::vector<PrivateUpscalerResourceDx12> inputs;
        auto addInput = [&](PrivateUpscalerResourceDx12 input)
        {
            if (input.resource && std::none_of(inputs.begin(), inputs.end(),
                    [&](auto existing) { return existing.resource == input.resource; })) inputs.push_back(input);
        };
        for (auto input : { f.color, f.depth, f.motion, f.exposure }) addInput(input);
        if (rayReconstruction)
            for (auto input : f.rr.guides) addInput(input);
        for (auto input : inputs)
            Barrier(cmd, input.resource, input.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(cmd, output, f.output.state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        bool result = false;
        if (backend == PrivateUpscaler::DLSS && feature)
        {
            auto* p = parameters;
            p->Set(NVSDK_NGX_Parameter_Color, color);
            p->Set(NVSDK_NGX_Parameter_Output, output);
            p->Set(NVSDK_NGX_Parameter_Depth, depth);
            p->Set(NVSDK_NGX_Parameter_MotionVectors, motion);
            p->Set(NVSDK_NGX_Parameter_ExposureTexture, exposure);
            p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, f.width);
            p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, f.height);
            p->Set(NVSDK_NGX_Parameter_Reset, (unsigned) f.reset);
            p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, f.jitterX);
            p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, f.jitterY);
            p->Set(NVSDK_NGX_Parameter_MV_Scale_X, f.motionScaleX);
            p->Set(NVSDK_NGX_Parameter_MV_Scale_Y, f.motionScaleY);
            p->Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, f.frameTimeMs);
            p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.0f);
            p->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0f);
            p->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
            if (rayReconstruction)
            {
                for (unsigned i = 0; i < f.rr.guides.size(); ++i)
                {
                    p->Set(rrKeys[i], f.rr.guides[i].resource);
                    if (rrOffsetPrefixes[i])
                    {
                        p->Set((std::string(rrOffsetPrefixes[i]) + ".Subrect.Base.X").c_str(), f.rr.baseX[i]);
                        p->Set((std::string(rrOffsetPrefixes[i]) + ".Subrect.Base.Y").c_str(), f.rr.baseY[i]);
                    }
                }
                p->Set("WorldToViewMatrix", f.rr.matrices ? (void*)f.rr.worldToView.data() : nullptr);
                p->Set("ViewToClipMatrix", f.rr.matrices ? (void*)f.rr.viewToClip.data() : nullptr);
            }
            const auto evaluated = NVNGXProxy::D3D12_EvaluateFeature()(cmd, feature, p, nullptr);
            result = evaluated == NVSDK_NGX_Result_Success;
            if (!result) NgxError("EvaluateFeature", evaluated);
        }
        else if (backend == PrivateUpscaler::FSR22 && fsr2Ready)
        {
            FfxFsr2DispatchDescription d {};
            d.commandList = ffxGetCommandListDX12(cmd);
            d.color = ffxGetResourceDX12(&fsr2, color);
            d.depth = ffxGetResourceDX12(&fsr2, depth);
            d.motionVectors = ffxGetResourceDX12(&fsr2, motion);
            d.exposure = ffxGetResourceDX12(&fsr2, exposure);
            d.output = ffxGetResourceDX12(&fsr2, output, nullptr, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            FrameParameters(d, f);
            result = ffxFsr2ContextDispatch(&fsr2, &d) == FFX_OK;
        }
        else if (backend == PrivateUpscaler::FFX && ffx)
        {
            ffxDispatchDescUpscale d {};
            d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
            d.commandList = cmd;
            d.color = ffxApiGetResourceDX12(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            d.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            d.motionVectors = ffxApiGetResourceDX12(motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            d.exposure = ffxApiGetResourceDX12(exposure, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            d.output = ffxApiGetResourceDX12(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
            d.upscaleSize = { outWidth, outHeight };
            FrameParameters(d, f);
            result = FfxApiProxy::D3D12_Dispatch(&ffx, &d.header) == FFX_API_RETURN_OK;
        }
        else if (backend == PrivateUpscaler::XeSS && xess)
        {
            xess_d3d12_execute_params_t d {};
            d.pColorTexture = color;
            d.pOutputTexture = output;
            d.pDepthTexture = depth;
            d.pVelocityTexture = motion;
            d.pExposureScaleTexture = exposure;
            d.inputWidth = width;
            d.inputHeight = height;
            d.jitterOffsetX = f.jitterX;
            d.jitterOffsetY = f.jitterY;
            d.resetHistory = f.reset;
            d.exposureScale = 1.0f;
            result =
                XeSSProxy::SetVelocityScale()(xess, f.motionScaleX,
                                              f.motionScaleY) == XESS_RESULT_SUCCESS &&
                XeSSProxy::D3D12Execute()(xess, cmd, &d) == XESS_RESULT_SUCCESS;
        }
        Barrier(cmd, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, f.output.state);
        for (auto input : inputs)
            Barrier(cmd, input.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, input.state);
        return result;
    }
};
PrivateUpscalerDx12::PrivateUpscalerDx12(PrivateUpscaler selected) : impl(std::make_unique<Impl>(selected)) {}
PrivateUpscalerDx12::~PrivateUpscalerDx12() = default;
const char* PrivateUpscalerDx12::Name() const
{
    return impl->rayReconstruction ? "DLSS RR" : PrivateUpscalerName(impl->backend);
}
const std::string& PrivateUpscalerDx12::Error() const { return impl->error; }
bool PrivateUpscalerDx12::Init(ID3D12Device* device, ID3D12GraphicsCommandList* cmd,
                                const PrivateUpscalerCreateDx12& info)
{
    return impl->Init(device, cmd, info);
}
bool PrivateUpscalerDx12::Evaluate(ID3D12GraphicsCommandList* cmd, const PrivateUpscalerFrameDx12& frame)
{
    return impl->Evaluate(cmd, frame);
}
} // namespace DlssNr
