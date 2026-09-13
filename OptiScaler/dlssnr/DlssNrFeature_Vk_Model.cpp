#include "pch.h"

#include "DlssNrFeature_Vk_Internal.h"
#include "DlssNrFeature_Dx12.h"
#include "DlssNr_Status.h"
#include <nvsdk_ngx_vk.h>
#include "PassProfiles.h"

#include <Config.h>
#include <State.h>
#include <proxies/NVNGX_Proxy.h>

#include <shaders/dlssnr/DlssNr_Vk.h>
#include <shaders/dlssnr/DlssNr_Guides.h>
#include <shaders/output_scaling/OS_Vk.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace DlssNr
{

void ModelVk::Impl::Fail(const char* why)
{
    if (state.failed)
        return;

    state.failed = true;
    state.reason = why;
    LOG_ERROR("DLSS-NR Vulkan unavailable: {}", why);
}

bool ModelVk::Impl::InitDriver(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
{
    if (!NVNGXProxy::IsVulkanInited() &&
        !NVNGXProxy::InitVulkan(instance, physicalDevice, device, vkGetInstanceProcAddr, vkGetDeviceProcAddr))
    {
        Fail("the NVIDIA NGX Vulkan driver could not initialize");
        return false;
    }
    if (!NVNGXProxy::IsVulkanInited() || !NVNGXProxy::VULKAN_GetCapabilityParameters() ||
        !NVNGXProxy::VULKAN_CreateFeature1() || !NVNGXProxy::VULKAN_EvaluateFeature() ||
        !NVNGXProxy::VULKAN_ReleaseFeature() || !NVNGXProxy::VULKAN_DestroyParameters())
    {
        Fail("the NVIDIA NGX Vulkan driver interface is incomplete");
        return false;
    }
    if (!state.ngxInitialised)
        LOG_INFO("DLSS-NR Vulkan: using the NVIDIA NGX driver dispatcher");
    state.ngxInitialised = true;
    return true;
}

void ModelVk::Impl::ReleaseModels()
{
    // GPU work is retired by callers before feature/map teardown.
    for (auto& model : state.models)
    {
        if (model.feature && NVNGXProxy::VULKAN_ReleaseFeature())
            NVNGXProxy::VULKAN_ReleaseFeature()(model.feature);
        if (model.parameters && NVNGXProxy::VULKAN_DestroyParameters())
            NVNGXProxy::VULKAN_DestroyParameters()(model.parameters);
        model = {};
    }
}

void ModelVk::Impl::SetTuning(NVSDK_NGX_Parameter* parameters, const Profiles::NrPassTuning& tuning,
                      unsigned int style)
{
    parameters->Set("DLSSNR.Intensity", tuning.intensity);
    parameters->Set("DLSSNR.Style", style);
    parameters->Set("DLSSNR.LocalStructureStrength", tuning.structure);
    parameters->Set("DLSSNR.LocalToneStrength", tuning.tone);
    parameters->Set("DLSSNR.SkinStructureStrength", tuning.skin);
    parameters->Set("DLSSNR.ControlMask", static_cast<void*>(nullptr));
    parameters->Set("DLSSNR.UseAutoMask", tuning.autoMask ? 1u : 0u);
}

bool ModelVk::Impl::CreateModel(VkCommandBuffer commandBuffer, unsigned int passIndex, unsigned int width,
                 unsigned int height, const Config& config)
{
    auto& model = state.models[passIndex];
    if (model.feature)
        return true;
    if (!model.parameters)
    {
        const auto result = NVNGXProxy::VULKAN_GetCapabilityParameters()(&model.parameters);
        if (result != NVSDK_NGX_Result_Success || !model.parameters)
        {
            LOG_ERROR("DLSS-NR Vulkan: capability parameters for pass {} failed 0x{:X}", passIndex + 1,
                      static_cast<unsigned int>(result));
            Fail("the NVIDIA NGX driver could not allocate capability parameters");
            return false;
        }
    }
    auto* parameters = model.parameters;
    parameters->Set("DLSSNR.Enabled", 1u);
    parameters->Set("DLSSNR.Width", width);
    parameters->Set("DLSSNR.Height", height);
    parameters->Set("CreationNodeMask", 1u);
    parameters->Set("VisibilityNodeMask", 1u);
    parameters->Set("DLSSNR.Hint.Render.Preset", Profiles::PassPreset(config, passIndex));
    parameters->Set("DLSSNR.UICorrection", 1u);
    SetTuning(parameters, Profiles::PassTuning(config, passIndex), Profiles::PassStyle(config, passIndex));
    const auto result = NVNGXProxy::VULKAN_CreateFeature1()(
        state.device, commandBuffer, static_cast<NVSDK_NGX_Feature>(18), parameters, &model.feature);
    // Identical-profile layers still require distinct temporal histories. Reject aliasing rather
    // than silently sharing a driver handle, including an AlreadyExists response with a handle.
    if (model.feature)
    {
        for (unsigned int other = 0; other < DlssNr::MaxPassCount; ++other)
        {
            const auto* existing = state.models[other].feature;
            if (other != passIndex && existing &&
                (existing == model.feature || existing->Id == model.feature->Id))
            {
                model.feature = nullptr; // owned by the other layer; do not double-release
                Fail("the NVIDIA NGX driver reused a feature instead of creating an independent NR pass");
                return false;
            }
        }
    }
    if (result != NVSDK_NGX_Result_Success || !model.feature)
    {
        LOG_ERROR("DLSS-NR Vulkan: driver CreateFeature(18), pass {}, failed 0x{:X}", passIndex + 1,
                  static_cast<unsigned int>(result));
        Fail("the NVIDIA NGX driver could not create an independent Neural Rendering feature");
        return false;
    }
    return true;
}

NVSDK_NGX_Result ModelVk::Impl::EvaluateModel(VkCommandBuffer commandBuffer, unsigned int passIndex,
                              NVSDK_NGX_Resource_VK* colour, NVSDK_NGX_Resource_VK* depth,
                              NVSDK_NGX_Resource_VK* motion, NVSDK_NGX_Resource_VK* output,
                              unsigned int width, unsigned int height, const GuideRegions& guides,
                              bool depthInverted, float mvX, float mvY, const Config& config)
{
    auto& model = state.models[passIndex];
    auto* parameters = model.parameters;
    // The Vulkan SDK stores pointers to NVSDK_NGX_Resource_VK through the void-pointer overload.
    parameters->Set("DLSSNR.Color", static_cast<void*>(colour));
    parameters->Set("DLSSNR.Depth", static_cast<void*>(depth));
    parameters->Set("DLSSNR.MVec", static_cast<void*>(motion));
    parameters->Set("DLSSNR.Output", static_cast<void*>(output));
    parameters->Set("DLSSNR.Enabled", 1u);
    parameters->Set("DLSSNR.Width", width);
    parameters->Set("DLSSNR.Height", height);
    parameters->Set("DLSSNR.DepthInverted", depthInverted ? 1u : 0u);
    parameters->Set("DLSSNR.Reset", state.reset ? 1u : 0u);
    parameters->Set("DLSSNR.ColorSubrectBaseX", 0u);
    parameters->Set("DLSSNR.ColorSubrectBaseY", 0u);
    parameters->Set("DLSSNR.ColorSubrectWidth", width);
    parameters->Set("DLSSNR.ColorSubrectHeight", height);
    parameters->Set("DLSSNR.OutputSubrectBaseX", 0u);
    parameters->Set("DLSSNR.OutputSubrectBaseY", 0u);
    parameters->Set("DLSSNR.OutputSubrectWidth", width);
    parameters->Set("DLSSNR.OutputSubrectHeight", height);
    parameters->Set("DLSSNR.DepthSubrectBaseX", guides.depth.x);
    parameters->Set("DLSSNR.DepthSubrectBaseY", guides.depth.y);
    parameters->Set("DLSSNR.DepthSubrectWidth", guides.depth.width);
    parameters->Set("DLSSNR.DepthSubrectHeight", guides.depth.height);
    parameters->Set("DLSSNR.MVecSubrectBaseX", guides.motion.x);
    parameters->Set("DLSSNR.MVecSubrectBaseY", guides.motion.y);
    parameters->Set("DLSSNR.MVecSubrectWidth", guides.motion.width);
    parameters->Set("DLSSNR.MVecSubrectHeight", guides.motion.height);
    parameters->Set("DLSSNR.MVecScaleX", mvX);
    parameters->Set("DLSSNR.MVecScaleY", mvY);
    SetTuning(parameters, Profiles::PassTuning(config, passIndex), Profiles::PassStyle(config, passIndex));
    return NVNGXProxy::VULKAN_EvaluateFeature()(commandBuffer, model.feature, parameters, nullptr);
}

bool ModelVk::Impl::FormatCanHoldLinearHdr(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_R16G16B16_SFLOAT:
    case VK_FORMAT_R32G32B32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        return true;
    default:
        return false;
    }
}

void ModelVk::Impl::Shutdown()
{
    // The device is alive (real teardown): drain before freeing so nothing the GPU is still using is
    // destroyed under it, the same rule as the resize path.
    if (state.device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(state.device);

    ReleaseModels();
    state.activePasses = 0;
    if (state.creationReady != VK_NULL_HANDLE)
        vkDestroyEvent(state.device, state.creationReady, nullptr);
    state.creationReady = VK_NULL_HANDLE;
    state.creationPending = false;

    DestroyImage(state.output);
    DestroyImage(state.scratch);
    DestroyImage(state.passClamp);
    DestroyImage(state.proxy);
    DestroyImage(state.proxySmall);
    DestroyImage(state.outputNative);
    DestroyImage(state.keep);
    DestroyImage(state.meter);
    DestroyMeterReadback();

    state.superUp.reset();
    state.superDown.reset();
    state.nrScaler = Scaler::Count;

    if (state.queryPool != VK_NULL_HANDLE && state.device != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(state.device, state.queryPool, nullptr);
        state.queryPool = VK_NULL_HANDLE;
    }

    state.timedFrames = 0;
    state.lastGpuTime.reset();

    state.device = VK_NULL_HANDLE;
    state.width = 0;
    state.height = 0;
    state.ngxInitialised = false;
    state.reset = true;
}

bool ModelVk::Impl::PrepareModels(VkCommandBuffer cmdBuffer, const DlssNrFrameInfo_Vk& frame, uint32_t width, uint32_t height, uint32_t workWidth, uint32_t workHeight, float workScale, unsigned int passes)
{
    auto& cfg = *Config::Instance();
    const auto instance = state.instance;
    const auto physicalDevice = state.physicalDevice;
    const auto device = state.device;
    const bool beforeSr = frame.BeforeUpscale;
    const bool rayReconstruction = frame.RayReconstruction;
    const bool reduced = workWidth != width || workHeight != height;
    if (!InitDriver(instance, physicalDevice, device))
        return false;

    // A GPU event separates creation uploads from model evaluation.
    if (state.creationPending)
    {
        // A second NGX evaluate need not mean the previous command buffer was submitted.
        // Poll the GPU marker without waiting; repeated calls during warm-up stay clean.
        if (vkGetEventStatus(device, state.creationReady) != VK_EVENT_SET)
            return false;
        state.creationPending = false;
    }

    if (state.queryPool == VK_NULL_HANDLE)
    {
        VkPhysicalDeviceProperties props {};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);

        // A period of zero means the device does not support timestamps on this queue. The pass runs
        // regardless; it simply reports no cost, which is what the D3D12 path does when its heap is
        // unavailable.
        state.timestampPeriod = props.limits.timestampPeriod;

        if (state.timestampPeriod > 0.0f)
        {
            VkQueryPoolCreateInfo info {};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = kTimingSlots * 2;

            if (vkCreateQueryPool(device, &info, nullptr, &state.queryPool) != VK_SUCCESS)
            {
                state.queryPool = VK_NULL_HANDLE;
                LOG_INFO("DLSS-NR Vulkan: no timestamp pool, the pass will not report its cost");
            }
        }
    }

    if (!state.pass->IsInit())
        return false;

    // Resize. The feature is built for a size and has to be rebuilt when the frame OR the working
    // size changes -- moving the slider is a rebuild, which is why it is compared here.
    bool profileChanged = state.activePasses != passes;
    for (unsigned int pass = 0; pass < passes; ++pass)
        profileChanged |= state.builtTuning[pass] != Profiles::PassTuning(cfg, pass) ||
                          state.builtPreset[pass] != Profiles::PassPreset(cfg, pass) ||
                          state.builtStyle[pass] != Profiles::PassStyle(cfg, pass);
    if (state.width != width || state.height != height || state.workWidth != workWidth ||
        state.workHeight != workHeight || state.beforeSr != beforeSr ||
        state.rayReconstruction != rayReconstruction || profileChanged)
    {
        // This block releases the feature and frees the surfaces below IMMEDIATELY. A frame-size
        // change is already fenced by the game -- it recreates the swapchain around it -- but moving
        // the working-scale slider is not: the game is mid-flight and previous frames' command
        // buffers still reference the feature and images about to be destroyed. Freeing a Vulkan
        // resource that in-flight GPU work still touches is device removal (ERR_GFX_STATE, reproduced
        // on RDR2 and Enshrouded by dragging the model-resolution slider). Drain the device first.
        // Only the rare resize path reaches here, so the CPU stall is a one-off hitch, not per-frame.
        if (state.device != VK_NULL_HANDLE && vkDeviceWaitIdle(state.device) != VK_SUCCESS)
        {
            Fail("the Vulkan device could not retire previous model resources");
            return false;
        }

        ReleaseModels();
        DestroyImage(state.scratch);
        DestroyImage(state.passClamp);

        const VkFormat working = VK_FORMAT_R16G16B16A16_SFLOAT;

        // The meter is a fixed 8x8 whatever the frame is, so it is only built the once -- but it is
        // built alongside the rest so that a failure here is caught by the same check.
        const bool meterReady =
            (state.meter.Valid() || CreateImage(state.meter, kMeterSide, kMeterSide, VK_FORMAT_R32_SFLOAT, true)) &&
            CreateMeterReadback();

        if (!meterReady)
            LOG_WARN("DLSS-NR Vulkan: no exposure meter; the white point stays on the slider");

        DestroyImage(state.proxySmall);
        DestroyImage(state.outputNative);

        // output is the model's target, so it is the working size. proxy and keep are full: proxy is
        // the source the downsample reads, keep is the untouched frame the resolve composites onto.
        // outputNative is the native buffer the supersample down-leg averages the answer into.
        const bool ok = CreateImage(state.output, workWidth, workHeight, working, true) &&
                        (passes == 1 || (CreateImage(state.scratch, workWidth, workHeight, working, true) &&
                                         CreateImage(state.passClamp, workWidth, workHeight, working, true))) &&
                        CreateImage(state.proxy, width, height, working, true) &&
                        CreateImage(state.keep, width, height, working, true) &&
                        (!reduced || CreateImage(state.proxySmall, workWidth, workHeight, working, true)) &&
                        (workScale <= 1.0f || CreateImage(state.outputNative, width, height, working, true));

        if (!ok)
        {
            Fail("the pass could not allocate its own surfaces");
            return false;
        }

        state.width = width;
        state.height = height;
        state.workWidth = workWidth;
        state.workHeight = workHeight;
        state.beforeSr = beforeSr;
        state.rayReconstruction = rayReconstruction;
        state.activePasses = passes;
        for (unsigned int pass = 0; pass < passes; ++pass)
        {
            state.builtTuning[pass] = Profiles::PassTuning(cfg, pass);
            state.builtPreset[pass] = Profiles::PassPreset(cfg, pass);
            state.builtStyle[pass] = Profiles::PassStyle(cfg, pass);
        }
        state.reset = true;
    }

    bool created = false;
    for (unsigned int pass = 0; pass < passes; ++pass)
    {
        if (state.models[pass].feature)
            continue;
        if (!CreateModel(cmdBuffer, pass, workWidth, workHeight, cfg))
            return false;

        LOG_INFO("DLSS-NR Vulkan: pass {} built at {}x{} (frame {}x{}, {} SR)", pass + 1, workWidth, workHeight,
                 width, height, beforeSr ? "before" : "after");
        created = true;
        state.reset = true;
    }
    // NGX creation may record GPU uploads. Do not evaluate until a subsequent submission,
    // just as on D3D12. Leave this warm-up frame clean instead of evaluating unready weights.
    if (created)
    {
        if (state.creationReady == VK_NULL_HANDLE)
        {
            VkEventCreateInfo info { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO };
            if (vkCreateEvent(device, &info, nullptr, &state.creationReady) != VK_SUCCESS)
            {
                Fail("could not allocate the model creation marker");
                return false;
            }
        }
        else
            vkResetEvent(device, state.creationReady); // rebuild above drained previous GPU users
        vkCmdSetEvent(cmdBuffer, state.creationReady, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        state.creationPending = true;
        return false;
    }

    return true;
}

} // namespace DlssNr
