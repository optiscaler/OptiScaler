#include "pch.h"

#include "DlssNrFeature_Vk_Internal.h"
#include "DlssNrFeature_Dx12.h"
#include "DlssNr_Status.h"
#include "DlssNr_Placement.h"
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

bool ModelVk::Impl::Evaluate(VkCommandBuffer cmdBuffer, const VkImageInfo& colourInfo, const VkImageInfo& depthInfo,
              const VkImageInfo& motionInfo, const VkImageInfo& target, const DlssNrFrameInfo_Vk& frame,
              VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkImageLayout inputLayout)
{
    const bool beforeSr = frame.BeforeUpscale;
    const bool rayReconstruction = frame.RayReconstruction;
    auto& cfg = *Config::Instance();

    if (ResolvePlacement(cfg.DlssNrRunBeforeSr.value_or_default(), cfg.DlssNrDeferredDlss.value_or_default(),
                         cfg.DlssNrResidualAcrossRr.value_or_default(), cfg.DlssNrFinishedPicture.value_or_default()).deferred &&
        !frame.FinishedPicture)
    {

        if (!warnedDeferred)
        {
            LOG_WARN("DLSS-NR DeferredDLSS requires the D3D12 path or a D3D12 bridge; "
                     "native Vulkan leaves the clean SR frame unchanged");
            warnedDeferred = true;
        }
        return false;
    }

    if (cfg.DlssNrFinishedPicture.value_or_default() && !frame.FinishedPicture)
        return false; // the presentation stage owns this mode

    if (!cfg.DlssNrEnabled.value_or_default())
        return false;

    if (cmdBuffer == VK_NULL_HANDLE || device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE)
        return false;

    std::lock_guard<std::mutex> lock(mutex);
    if (cfg.DlssNrTransfer.value_or_default() == 2 && cfg.DlssNrWorkingScale.value_or_default() < 1.0f)
    {
        PublishStatus(this, Backend::Vulkan,
                      { false, "Matched residual + DLSS requires the DX12 processing path." });
        return false;
    }

    struct ReportStatus
    {
        Impl* owner;
        ~ReportStatus()
        {
            const auto& state = owner->state;
            ExposureStatus exposure {};
            exposure.seenFrames = state.frames;
            exposure.offeredNow = exposure.everOffered = state.exposureOffered;
            exposure.exposure = state.gameExposure;
            exposure.preExposure = state.gamePreExposure;
            PublishStatus(owner, Backend::Vulkan,
                          { state.models[0].feature != nullptr && !state.failed, state.reason, state.lastGpuTime,
                            state.frames, exposure, false });
        }
    } report { this };
    const auto requests = ReadControlRequests();
    if (requests.retryGeneration != retryGeneration)
    {
        retryGeneration = requests.retryGeneration;
        if (state.device && vkDeviceWaitIdle(state.device) != VK_SUCCESS)
        {
            Fail("the Vulkan device could not retire work for retry");
            return false;
        }
        Shutdown();
        state.failed = false;
        state.reason = "";
    }
    if (state.failed)
        return false;

    auto wrap = [](const VkImageInfo& image, bool readWrite)
    {
        NVSDK_NGX_Resource_VK resource {};
        resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
        resource.Resource.ImageViewInfo = { image.ImageView, image.Image, image.SubresourceRange,
                                            image.Format,    image.Width, image.Height };
        resource.ReadWrite = readWrite;
        return resource;
    };
    auto colourResource = wrap(colourInfo, false);
    auto depthResource = wrap(depthInfo, frame.DepthReadWrite);
    auto motionResource = wrap(motionInfo, frame.MotionReadWrite);
    auto* colour = &colourResource;
    auto* depth = &depthResource;
    auto* motion = &motionResource;

    // Game exposure is sampled in SHADER_READ_ONLY_OPTIMAL, matching the Vulkan input contract.
    // Keep its layout unchanged and retain the last valid exposure if a sample is unusable.
    auto* exposure = static_cast<NVSDK_NGX_Resource_VK*>(frame.ExposureTexture);
    const float preExposure = frame.PreExposure;
    const bool havePre = true;

    if (!saidExposure)
    {
        saidExposure = true;
        LOG_INFO("DLSS-NR Vulkan: exposure from the game: DLSS.Pre.Exposure {}, ExposureTexture {}",
                 havePre ? std::to_string(preExposure) : std::string("not supplied"),
                 exposure != nullptr ? "supplied" : "not supplied");
    }

    state.exposureOffered = exposure != nullptr;

    if (havePre && std::isfinite(preExposure) && preExposure > 0.0f)
        state.gamePreExposure = preExposure;

    // Take the grid written four frames ago. Retired by now, so this reads mapped memory rather than
    // waiting on the GPU -- which is the whole reason for the ring.
    if (state.meterFrames >= kMeterSlots)
    {
        const void* mapped = state.meterMapped[state.meterFrames % kMeterSlots];

        if (mapped != nullptr)
        {
            float measured = 0.0f;
            std::memcpy(&measured, mapped, sizeof(float));

            // Believed only if it could be an exposure. A texel read through a layout the game did
            // not leave it in, or a slot the game stopped filling, fails here and the last good
            // value stands.
            if (std::isfinite(measured) && measured > 0.0f)
                state.gameExposure = measured;
        }
    }

    // Said when it moves by more than a fiftieth, not every frame. Enough to see in a log that the
    // number is the game's and that it tracks the scene, without a line per frame.

    if (state.gameExposure > 1e-6f &&
        std::abs(loggedExposure - state.gameExposure) > std::max(0.02f * state.gameExposure, 1e-5f))
    {
        loggedExposure = state.gameExposure;
        LOG_INFO("DLSS-NR Vulkan: the game's exposure is {}, pre-exposure {}, so white point {}",
                 state.gameExposure, state.gamePreExposure, state.gamePreExposure / state.gameExposure);
    }

    if (colour->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW ||
        depth->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW ||
        motion->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW ||
        colour->Resource.ImageViewInfo.ImageView == VK_NULL_HANDLE ||
        depth->Resource.ImageViewInfo.ImageView == VK_NULL_HANDLE ||
        motion->Resource.ImageViewInfo.ImageView == VK_NULL_HANDLE)
        return false;

    uint32_t width = colour->Resource.ImageViewInfo.Width;
    uint32_t height = colour->Resource.ImageViewInfo.Height;
    const auto renderWidth = frame.RenderSubrectWidth, renderHeight = frame.RenderSubrectHeight;
    const auto baseX = frame.ColorSubrectBaseX, baseY = frame.ColorSubrectBaseY;
    if (beforeSr)
    {
        // Origin-zero padded inputs are common with dynamic resolution. Never use a preset table.
        if (baseX || baseY || ((renderWidth == 0) != (renderHeight == 0)) || renderWidth > width ||
            renderHeight > height)
            return false; // caller falls back to post-SR, without editing the input
        if (renderWidth && renderHeight)
        {
            width = renderWidth;
            height = renderHeight;
        }
    }
    const auto depthX = frame.DepthSubrectBaseX, depthY = frame.DepthSubrectBaseY;
    const auto motionX = frame.MotionSubrectBaseX, motionY = frame.MotionSubrectBaseY;
    const auto outputWidth = frame.OutputWidth ? frame.OutputWidth : target.Width;
    const auto outputHeight = frame.OutputHeight ? frame.OutputHeight : target.Height;
    const auto guides =
        ResolveGuideRegions({ depth->Resource.ImageViewInfo.Width, depth->Resource.ImageViewInfo.Height },
                            { motion->Resource.ImageViewInfo.Width, motion->Resource.ImageViewInfo.Height },
                            { renderWidth, renderHeight }, { outputWidth, outputHeight },
                            frame.MotionVectorsLowResolution, depthX, depthY, motionX, motionY);
    if (!guides.depth.valid() || !guides.motion.valid())
        return false;
    const auto guideWidth = guides.depth.width, guideHeight = guides.depth.height;

    if (!width || !height || width > target.Width || height > target.Height)
        return false;

    // The model uses working resolution; source and composition remain native.
    float workScale = cfg.DlssNrWorkingScale.value_or_default();
    workScale = std::isfinite(workScale) ? std::clamp(workScale, 0.25f, 2.0f) : 1.0f;
    const uint32_t workWidth = std::max(1u, (uint32_t) (width * workScale + 0.5f));
    const uint32_t workHeight = std::max(1u, (uint32_t) (height * workScale + 0.5f));
    const bool reduced = workWidth != width || workHeight != height;
    const unsigned int passes =
        std::clamp(cfg.DlssNrPasses.value_or_default(), 1u,
                   cfg.DlssNrUnlockPasses.value_or_default() ? DlssNr::MaxPassCount : DlssNr::DefaultMaxPassCount);

    state.instance = instance;
    state.physicalDevice = physicalDevice;

    // The shader belongs to one device. Replacement features own replacement models.
    if (state.device != VK_NULL_HANDLE && state.device != device)
    {
        Fail("a Vulkan model was dispatched on a different device");
        return false;
    }
    state.device = device;

    if (!PrepareModels(cmdBuffer, frame, width, height, workWidth, workHeight, workScale, passes))
        return false;

    // -----------------------------------------------------------------------------------------
    // Encode: the frame the upscaler wrote -> a display-referred proxy, plus an untouched copy
    // -----------------------------------------------------------------------------------------

    const bool gameSaysHdr = frame.ColourIsLinearHdr;
    const bool depthInverted = frame.DepthInverted;

    if (frame.Reset)
        state.reset = true;

    // Both have to agree. A game can set the HDR flag on a buffer that cannot hold open-ended light,
    // and encoding an already tone-mapped frame a second time looks washed out and banded.
    const bool linearHdr = gameSaysHdr && FormatCanHoldLinearHdr(colour->Resource.ImageViewInfo.Format);

    // Undo game pre-exposure/exposure with a bounded trim. Preserve the manual setting for switching back.
    float whitePoint = cfg.DlssNrWhitePointScale.value_or_default();

    if (cfg.DlssNrWhitePointSource.value_or_default() == 1 && state.gameExposure > 1e-6f)
    {
        const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
        whitePoint = std::clamp(state.gamePreExposure / state.gameExposure * trim, 0.01f, 4096.0f);
    }

    if (!saidEncoding)
    {
        saidEncoding = true;
        LOG_INFO("DLSS-NR Vulkan: the game's buffer is {} (flag {}, format {}), depth {}",
                 linearHdr ? "linear HDR" : "already tone-mapped", gameSaysHdr ? "set" : "clear",
                 (int) colour->Resource.ImageViewInfo.Format, depthInverted ? "inverted" : "normal");
    }

    if (frame.WhitePointOverride > 0.0f)
        whitePoint = frame.WhitePointOverride;

    auto encode = DlssNr_Common::MakeConstants(DlssNrMode_Encode, width, height, whitePoint, linearHdr, cfg);
    encode.GuideWidth = guideWidth;
    encode.GuideHeight = guideHeight;

    const VkImageSubresourceRange colourRange = colour->Resource.ImageViewInfo.SubresourceRange;

    // Open the measurement. Reset immediately before writing: a query pool slot must be reset before
    // it is written again, and doing it here rather than at the end keeps the two in one place.
    const uint32_t timingSlot = (uint32_t) (state.timedFrames % kTimingSlots);

    if (state.queryPool != VK_NULL_HANDLE)
    {
        vkCmdResetQueryPool(cmdBuffer, state.queryPool, timingSlot * 2, 2);
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, state.queryPool, timingSlot * 2);
    }

    // The game's colour is read here and written at the end. Its layout on arrival is GENERAL, which
    // is what NGX requires of a resource it is handed, so it is left alone.
    Transition(cmdBuffer, state.proxy, VK_IMAGE_LAYOUT_GENERAL);
    Transition(cmdBuffer, state.keep, VK_IMAGE_LAYOUT_GENERAL);

    // Read the caller's actual input layout; the resolve restores it after writing.
    if (!state.pass->Dispatch(cmdBuffer, encode, width, height, colour->Resource.ImageViewInfo.ImageView,
                              VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, state.proxy.view, state.keep.view,
                              inputLayout))
    {
        Fail("the encode dispatch failed");
        return false;
    }

    // The model's input: the full proxy, or a downsampled copy of it when the working scale is below
    // the frame. Mirrors the D3D12 path -- the encode always writes a full proxy, and a separate
    // downsample makes the small one the model actually reads.
    OwnedImage* modelInput = &state.proxy;

    if (reduced && state.proxySmall.Valid())
    {
        bool built = false;

        if (workScale > 1.0f)
        {
            // Supersample: upscale the proxy to the super-native working size with the chosen filter so
            // the model sees a clean input. Rebuild both scalers when the NR downscaler changed (baked
            // at construction). proxy -> SHADER_READ_ONLY (sampled), proxySmall -> GENERAL (storage).
            const Scaler wantScaler = cfg.DlssNrScalingDownscaler.value_or_default();
            if (state.nrScaler != wantScaler)
            {
                // Drain submitted work before replacing filter pipelines and descriptor resources.
                if (state.device != VK_NULL_HANDLE && vkDeviceWaitIdle(state.device) != VK_SUCCESS)
                {
                    Fail("the Vulkan device could not retire the supersampling filters");
                    return false;
                }
                state.superUp.reset();
                state.superDown.reset();
                state.nrScaler = wantScaler;
            }
            if (!state.superUp)
                state.superUp =
                    std::make_unique<OS_Vk>("DLSS-NR VK supersample up", device, physicalDevice, true, wantScaler);
            if (!state.superDown)
                state.superDown = std::make_unique<OS_Vk>("DLSS-NR VK supersample down", device, physicalDevice,
                                                          false, wantScaler);

            Transition(cmdBuffer, state.proxy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(cmdBuffer, state.proxySmall, VK_IMAGE_LAYOUT_GENERAL);

            VkImageInfo upin = ImageInfoOf(state.proxy);
            VkImageInfo upout = ImageInfoOf(state.proxySmall);

            if (state.superUp && state.superUp->IsInit() && state.superUp->DispatchResources(cmdBuffer, upin, upout))
                built = true;
            else
            {

                if (!warnedVkSuper)
                {
                    warnedVkSuper = true;
                    LOG_WARN("DLSS-NR Vulkan supersample: upscaler unavailable, falling back to box enlarge.");
                }
            }
        }

        if (!built)
        {
            DlssNrConstants down = encode;
            down.Mode = DlssNrMode_Downsample;
            down.Width = workWidth;
            down.Height = workHeight;

            Transition(cmdBuffer, state.proxy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(cmdBuffer, state.proxySmall, VK_IMAGE_LAYOUT_GENERAL);

            if (!state.pass->Dispatch(cmdBuffer, down, workWidth, workHeight, state.proxy.view, VK_NULL_HANDLE,
                                      VK_NULL_HANDLE, VK_NULL_HANDLE, state.proxySmall.view, VK_NULL_HANDLE,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
            {
                Fail("the downsample dispatch failed");
                return false;
            }
        }

        modelInput = &state.proxySmall;
    }

    // -----------------------------------------------------------------------------------------
    // The meter: the game's 1x1 exposure -> texel (0,0) of the grid -> a buffer the CPU can read
    // -----------------------------------------------------------------------------------------

    // Use the otherwise unused motion binding for exposure; bind it only for the selected exposure source.
    if (cfg.DlssNrWhitePointSource.value_or_default() == 1 && exposure != nullptr &&
        exposure->Type == NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW &&
        exposure->Resource.ImageViewInfo.ImageView != VK_NULL_HANDLE && state.meter.Valid())
    {
        const unsigned long long slot = state.meterFrames % kMeterSlots;

        if (state.meterReadback[slot] != VK_NULL_HANDLE)
        {
            DlssNrConstants meter = encode;
            meter.Mode = DlssNrMode_Meter;
            meter.Width = kMeterSide;
            meter.Height = kMeterSide;

            Transition(cmdBuffer, state.meter, VK_IMAGE_LAYOUT_GENERAL);

            if (state.pass->Dispatch(cmdBuffer, meter, kMeterSide, kMeterSide, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                     VK_NULL_HANDLE, exposure->Resource.ImageViewInfo.ImageView, state.meter.view,
                                     VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
            {
                Transition(cmdBuffer, state.meter, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

                VkBufferImageCopy region {};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                region.imageOffset = { 0, 0, 0 };
                region.imageExtent = { kMeterSide, kMeterSide, 1 };

                vkCmdCopyImageToBuffer(cmdBuffer, state.meter.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       state.meterReadback[slot], 1, &region);

                // The copy has to be visible to a host read, and only the host will read it.
                VkBufferMemoryBarrier toHost {};
                toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toHost.buffer = state.meterReadback[slot];
                toHost.offset = 0;
                toHost.size = kMeterBytes;

                vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                                     nullptr, 1, &toHost, 0, nullptr);

                state.meterFrames++;
            }
        }
    }

    // -----------------------------------------------------------------------------------------
    // The model
    // -----------------------------------------------------------------------------------------

    float mvX = frame.MvScaleX, mvY = frame.MvScaleY;
    // Match D3D12: preserve the game's vector encoding, then adjust only for the NR working scale.
    mvX *= (float) workWidth / width;
    mvY *= (float) workHeight / height;
    OwnedImage* answer = &state.output;
    OwnedImage* input = modelInput;
    bool clampFailed = false;
    uint32_t clampSlots[2] = { UINT32_MAX, UINT32_MAX };
    NVSDK_NGX_Result evaluated = NVSDK_NGX_Result_Success;
    for (unsigned int pass = 0; pass < passes; ++pass)
    {
        Transition(cmdBuffer, *input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Transition(cmdBuffer, *answer, VK_IMAGE_LAYOUT_GENERAL);
        evaluated = EvaluateModel(cmdBuffer, pass, &input->ngx, depth, motion, &answer->ngx,
                                  workWidth, workHeight, guides, depthInverted, mvX, mvY, cfg);
        if (evaluated != NVSDK_NGX_Result_Success)
            break;
        if (pass + 1 < passes)
        {
            Transition(cmdBuffer, *answer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(cmdBuffer, state.passClamp, VK_IMAGE_LAYOUT_GENERAL);
            DlssNrConstants clamp {};
            clamp.Mode = DlssNrMode_ClampProxy;
            clamp.Width = workWidth;
            clamp.Height = workHeight;
            if (!state.pass->Dispatch(cmdBuffer, clamp, workWidth, workHeight, answer->view, VK_NULL_HANDLE,
                                      VK_NULL_HANDLE, VK_NULL_HANDLE, state.passClamp.view, VK_NULL_HANDLE,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, &clampSlots[pass % 2]))
            {
                clampFailed = true;
                break; // Resolve the last valid answer and reset skipped histories next frame.
            }
            input = &state.passClamp;
            answer = answer == &state.output ? &state.scratch : &state.output;
        }
    }

    state.reset = clampFailed;
    state.frames++;

    if (evaluated != NVSDK_NGX_Result_Success)
    {
        LOG_ERROR("DLSS-NR Vulkan: evaluate returned 0x{:X}", static_cast<unsigned int>(evaluated));
        Fail("the model refused to evaluate");
        return false;
    }

    // -----------------------------------------------------------------------------------------
    // Resolve: proxy + the model's answer + the untouched copy -> the frame
    // -----------------------------------------------------------------------------------------

    DlssNrConstants resolve = encode;
    resolve.Mode = DlssNrMode_Resolve;

    // Downsample the model answer to native before composition.
    OwnedImage* resolveProxy = modelInput;
    OwnedImage* resolveAnswer = answer;

    if (workScale > 1.0f && state.superDown && state.superDown->IsInit() && state.outputNative.Valid())
    {
        Transition(cmdBuffer, *answer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Transition(cmdBuffer, state.outputNative, VK_IMAGE_LAYOUT_GENERAL);

        VkImageInfo dsin = ImageInfoOf(*answer);
        VkImageInfo dsout = ImageInfoOf(state.outputNative);

        if (state.superDown->DispatchResources(cmdBuffer, dsin, dsout))
        {
            resolveProxy = &state.proxy;
            resolveAnswer = &state.outputNative;
        }
    }

    Transition(cmdBuffer, *resolveProxy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmdBuffer, *resolveAnswer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmdBuffer, state.keep, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!state.pass->Dispatch(cmdBuffer, resolve, width, height, resolveProxy->view, resolveAnswer->view,
                              state.keep.view, VK_NULL_HANDLE, target.ImageView, VK_NULL_HANDLE,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
    {
        Fail("the resolve dispatch failed");
        return false;
    }

    // Close it, and read the pair from three frames ago -- retired by now, so the read does not wait.
    if (state.queryPool != VK_NULL_HANDLE)
    {
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, state.queryPool, timingSlot * 2 + 1);
        state.timedFrames++;

        if (state.timedFrames > kTimingSlots)
        {
            const uint32_t readSlot = (uint32_t) (state.timedFrames % kTimingSlots);
            uint64_t ticks[2] = {};

            // Without WAIT: a slot this old is retired, and if it somehow is not, NOT_READY is the
            // right answer rather than a stall.
            if (vkGetQueryPoolResults(device, state.queryPool, readSlot * 2, 2, sizeof(ticks), ticks,
                                      sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
                ticks[1] > ticks[0])
            {
                const double ms = (double) (ticks[1] - ticks[0]) * (double) state.timestampPeriod / 1e6;

                // A pass that appears to have taken over a second did not; the queue was reset under
                // it or the pair straddled a device change.
                if (ms > 0.0 && ms < 1000.0)
                    state.lastGpuTime = ms;
            }
        }
    }

    if (!reported && state.frames > 2)
    {
        reported = true;
        LOG_INFO("DLSS-NR Vulkan: running {} SR at {}x{}, guides {}x{}", beforeSr ? "before" : "after", width,
                 height, guideWidth, guideHeight);
    }
    return true;
}


ModelVk::ModelVk(DlssNr_Vk& shader) : _impl(std::make_unique<Impl>(shader)) {}
ModelVk::~ModelVk() = default;
bool ModelVk::Evaluate(VkCommandBuffer cmd, const VkImageInfo& colour, const VkImageInfo& depth,
                       const VkImageInfo& motion, const VkImageInfo& output, const DlssNrFrameInfo_Vk& frame,
                       VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkImageLayout inputLayout)
{
    return _impl->Evaluate(cmd, colour, depth, motion, output, frame, instance, physicalDevice, device, inputLayout);
}
} // namespace DlssNr
