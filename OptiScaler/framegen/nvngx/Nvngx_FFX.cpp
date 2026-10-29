#include "pch.h"
#include "Nvngx_FFX.h"

#include <NVNGX_Parameter.h>
#include <nvsdk_ngx_vk.h>

#include "proxies/NVNGX_Proxy.h"
#include "proxies/Ntdll_Proxy.h"
#include <proxies/FfxApi_Proxy.h>
#include <numbers>
#include <misc/IdentifyGpu.h>
#include <hooks/Vulkan_Hooks.h>
#include <framegen/ffx/FSRFG_Vk.h>

using Microsoft::WRL::ComPtr;

bool Nvngx_FFX::Init()
{
    if (inited)
        return true;

    constexpr bool sendNotification = false;
    if (!FfxApiProxy::IsFGReady(sendNotification))
        FfxApiProxy::InitFfxDx12();

    if (!FfxApiProxy::IsFGReady())
    {
        LOG_ERROR("Couldn't get FG ready");
        return false;
    }

    inited = true;

    LOG_INFO("Nvngx FFX initialized");

    return inited;
}

bool Nvngx_FFX::InitVk()
{
    if (!FfxApiProxy::IsFGReadyVk())
        FfxApiProxy::InitFfxVk();

    if (!FfxApiProxy::IsFGReadyVk())
    {
        LOG_ERROR("Couldn't get Vulkan FG ready");
        return false;
    }

    return true;
}

NVSDK_NGX_Result Nvngx_FFX::D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                       ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                       NVSDK_NGX_Version InSDKVersion)
{
    return D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
}

NVSDK_NGX_Result Nvngx_FFX::D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                           ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                           const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    if (!Init())
        return NVSDK_NGX_Result_Fail;

    initDevice = InDevice;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_FFX::D3D12_Shutdown() { return D3D12_Shutdown1(nullptr); }

NVSDK_NGX_Result Nvngx_FFX::D3D12_Shutdown1(ID3D12Device* InDevice) { return NVSDK_NGX_Result_Success; }

NVSDK_NGX_Result Nvngx_FFX::D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                       const NVSDK_NGX_Parameter* InParameters, size_t* OutSizeInBytes)
{
    if (!OutSizeInBytes)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    *OutSizeInBytes = 0;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_FFX::CreateFeature(NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                          NVSDK_NGX_Handle** OutHandle, ID3D12Device* InDevice, VkDevice InVkDevice)
{
    if (!OutHandle || !InParameters)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (InFeatureID != NVSDK_NGX_Feature_FrameGeneration)
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;

    // Can't create FFX context yet, missing data
    auto handle = new Nvngx_FFX_Handle { .Id = lastIdCreated++, .device = InDevice, .vkDevice = InVkDevice };

    InParameters->Get("Width", &handle->swapchainWidth);
    InParameters->Get("Height", &handle->swapchainHeight);

    *OutHandle = (NVSDK_NGX_Handle*) handle;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_FFX::D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                                NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    if (!Init())
        return NVSDK_NGX_Result_Fail;

    if (!InCmdList)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    ID3D12Device* pDevice = nullptr;

    if (InCmdList->GetDevice(IID_PPV_ARGS(&pDevice)) != S_OK)
        return NVSDK_NGX_Result_FAIL_PlatformError;

    auto result = CreateFeature(InFeatureID, InParameters, OutHandle, pDevice, VK_NULL_HANDLE);

    if (result != NVSDK_NGX_Result_Success)
        pDevice->Release();

    return result;
}

NVSDK_NGX_Result Nvngx_FFX::D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    Nvngx_FFX_Handle* InOurHandle = (Nvngx_FFX_Handle*) InHandle;

    if (!InOurHandle)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (InOurHandle->device)
        InOurHandle->device->Release();

    delete InOurHandle; // destroys FFX context automatically

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_FFX::D3D12_GetFeatureRequirements(IDXGIAdapter* Adapter,
                                                         const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                         NVSDK_NGX_FeatureRequirement* OutSupported)
{
    if (!OutSupported)
        return NVSDK_NGX_Result_Fail;

    OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_Supported;
    OutSupported->MinHWArchitecture = 0x0;
    strcpy_s(OutSupported->MinOSVersion, "10.0.0");

    return NVSDK_NGX_Result_Success;
}

// FFX only interpolates a hudless of the backbuffer's precision group, as its GetFormatPrecisionGroup says
static int FormatPrecisionGroup(uint32_t format)
{
    switch (format)
    {
    case FFX_API_SURFACE_FORMAT_R32G32B32A32_TYPELESS:
    case FFX_API_SURFACE_FORMAT_R32G32B32A32_FLOAT:
    case FFX_API_SURFACE_FORMAT_R32G32B32_FLOAT:
        return 0;
    case FFX_API_SURFACE_FORMAT_R16G16B16A16_TYPELESS:
    case FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT:
        return 1;
    case FFX_API_SURFACE_FORMAT_R8G8B8A8_TYPELESS:
    case FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM:
    case FFX_API_SURFACE_FORMAT_B8G8R8A8_TYPELESS:
    case FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM:
        return 2;
    case FFX_API_SURFACE_FORMAT_R8G8B8A8_SNORM:
        return 3;
    case FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB:
    case FFX_API_SURFACE_FORMAT_B8G8R8A8_SRGB:
        return 4;
    case FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT:
        return 5;
    case FFX_API_SURFACE_FORMAT_R10G10B10A2_TYPELESS:
    case FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM:
        return 6;
    case FFX_API_SURFACE_FORMAT_R9G9B9E5_SHAREDEXP:
        return 7;
    default:
        return -1;
    }
}

static void fgLogCallback(uint32_t type, const wchar_t* message)
{
    auto message_str = wstring_to_string(std::wstring(message));

    if (type == FFX_API_MESSAGE_TYPE_ERROR)
        spdlog::error("FFX FG Callback: {}", message_str);
    else if (type == FFX_API_MESSAGE_TYPE_WARNING)
        spdlog::warn("FFX FG Callback: {}", message_str);
}

static D3D12_RESOURCE_STATES GetD3D12State(FfxApiResourceState state)
{
    switch (state)
    {
    case FFX_API_RESOURCE_STATE_COMMON:
        return D3D12_RESOURCE_STATE_COMMON;
    case FFX_API_RESOURCE_STATE_UNORDERED_ACCESS:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case FFX_API_RESOURCE_STATE_COMPUTE_READ:
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_PIXEL_READ:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ:
        return (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    case FFX_API_RESOURCE_STATE_COPY_SRC:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case FFX_API_RESOURCE_STATE_COPY_DEST:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case FFX_API_RESOURCE_STATE_GENERIC_READ:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case FFX_API_RESOURCE_STATE_INDIRECT_ARGUMENT:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case FFX_API_RESOURCE_STATE_RENDER_TARGET:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    default:
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

static void CopyTextureDx12(void* CommandList, const FfxApiResource* Destination, const FfxApiResource* Source)
{
    const auto cmdList12 = static_cast<ID3D12GraphicsCommandList*>(CommandList);

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = static_cast<ID3D12Resource*>(Destination->resource); // Destination
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = GetD3D12State((FfxApiResourceState) Destination->state);
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

    barriers[1] = barriers[0];
    barriers[1].Transition.pResource = static_cast<ID3D12Resource*>(Source->resource); // Source
    barriers[1].Transition.StateBefore = GetD3D12State((FfxApiResourceState) Source->state);
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

    cmdList12->ResourceBarrier(2, barriers);
    cmdList12->CopyResource(barriers[0].Transition.pResource, barriers[1].Transition.pResource);
    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    cmdList12->ResourceBarrier(2, barriers);
}

static void CopyTextureVk(void* CommandList, const FfxApiResource* Destination, const FfxApiResource* Source)
{
    const auto cmdBuffer = static_cast<VkCommandBuffer>(CommandList);
    const auto dst = static_cast<VkImage>(Destination->resource);
    const auto src = static_cast<VkImage>(Source->resource);
    const auto dstLayout = FSRFG_Vk::GetLayout(Destination->state);
    const auto srcLayout = FSRFG_Vk::GetLayout(Source->state);

    // Only color images get copied, VK_FORMAT_UNDEFINED barriers the color aspect
    IFGFeature_Vk::ImageBarrier(cmdBuffer, dst, VK_FORMAT_UNDEFINED, dstLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    IFGFeature_Vk::ImageBarrier(cmdBuffer, src, VK_FORMAT_UNDEFINED, srcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageCopy region {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = region.srcSubresource;
    region.extent = { Source->description.width, Source->description.height, 1 };
    vkCmdCopyImage(cmdBuffer, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &region);

    IFGFeature_Vk::ImageBarrier(cmdBuffer, dst, VK_FORMAT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dstLayout);
    IFGFeature_Vk::ImageBarrier(cmdBuffer, src, VK_FORMAT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcLayout);
}

static FfxApiResource GetResourceDx12(NVSDK_NGX_Parameter* InParameters, const char* name, uint32_t state)
{
    ID3D12Resource* resource = nullptr;
    InParameters->Get(name, &resource);
    return ffxApiGetResourceDX12(resource, state);
}

static FfxApiResource GetResourceVk(NVSDK_NGX_Parameter* InParameters, const char* name, uint32_t state)
{
    NVSDK_NGX_Resource_VK* resource = nullptr;
    InParameters->Get(name, (void**) &resource);

    if (!resource)
        return {};

    const auto& info = resource->Resource.ImageViewInfo;

    FfxApiResourceDescription desc {};
    desc.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    desc.format = ffxApiGetSurfaceFormatVK(info.Format);
    desc.width = info.Width;
    desc.height = info.Height;
    desc.depth = 1;
    desc.mipCount = 1;
    desc.usage = (resource->ReadWrite || state == FFX_API_RESOURCE_STATE_UNORDERED_ACCESS)
                     ? FFX_API_RESOURCE_USAGE_UAV
                     : FFX_API_RESOURCE_USAGE_READ_ONLY;

    if (ffxApiIsDepthFormat(info.Format))
        desc.usage |= FFX_API_RESOURCE_USAGE_DEPTHTARGET;

    return ffxApiGetResourceVK(info.Image, desc, state);
}

static constexpr Nvngx_FFX_Api ApiDx12 {
    FfxApiProxy::D3D12_CreateContext,
    FfxApiProxy::D3D12_DestroyContext,
    FfxApiProxy::D3D12_Configure,
    FfxApiProxy::D3D12_Query,
    FfxApiProxy::D3D12_Dispatch,
    GetResourceDx12,
    CopyTextureDx12,
};

static constexpr Nvngx_FFX_Api ApiVk {
    FfxApiProxy::VULKAN_CreateContext,
    FfxApiProxy::VULKAN_DestroyContext,
    FfxApiProxy::VULKAN_Configure,
    FfxApiProxy::VULKAN_Query,
    FfxApiProxy::VULKAN_Dispatch,
    GetResourceVk,
    CopyTextureVk,
};

// Implementation used as baseline
// https://github.com/Nukem9/dlssg-to-fsr3/blob/0e253f63c3c66ee7582406bfa3dec698df7ab2c1/source/maindll/FFFrameInterpolator.cpp
bool QueryHDRLuminanceRange(NVSDK_NGX_Parameter* InParameters, Nvngx_FFX_Handle* handle)
{
    if (handle->hdrRangeSet)
        return true;

    // Microsoft DirectX 12 HDR sample
    // https://github.com/microsoft/DirectX-Graphics-Samples/blob/b5f92e2251ee83db4d4c795b3cba5d470c52eaf8/Samples/Desktop/D3D12HDR/src/D3D12HDR.cpp#L1064
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIOutput> output = nullptr;

    if (CreateDXGIFactory1(IID_PPV_ARGS(&factory)) == S_OK)
    {
        ComPtr<IDXGIAdapter> adapter;
        IdentifyGpu::getHardwareAdapter(factory.Get(), adapter.GetAddressOf(), D3D_FEATURE_LEVEL_12_0);

        // Then check the first HDR-capable output
        for (uint32_t j = 0; adapter->EnumOutputs(j, &output) == S_OK; j++)
        {
            if (IDXGIOutput6* output6; output->QueryInterface(IID_PPV_ARGS(&output6)) == S_OK)
            {
                DXGI_OUTPUT_DESC1 outputDesc = {};
                output6->GetDesc1(&outputDesc);
                output6->Release();

                if (outputDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 && !handle->hdrRangeSet)
                {
                    handle->hdrMinLuminance = outputDesc.MinLuminance;
                    handle->hdrMaxLuminance = outputDesc.MaxLuminance;
                    handle->hdrRangeSet = true;
                }
            }
        }
    }

    // Keep using hardcoded defaults even if we didn't find a valid output
    handle->hdrRangeSet = true;

    LOG_INFO("Using assumed HDR luminance range: {} to {} nits", handle->hdrMinLuminance, handle->hdrMaxLuminance);

    return true;
}

NVSDK_NGX_Result Nvngx_FFX::D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                  const NVSDK_NGX_Handle* InFeatureHandle,
                                                  NVSDK_NGX_Parameter* InParameters,
                                                  PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    // This is our handle so can cast away const
    Nvngx_FFX_Handle* InOurHandle = (Nvngx_FFX_Handle*) InFeatureHandle;

    if (!InParameters || !InOurHandle)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (!Init())
        return NVSDK_NGX_Result_Fail;

    ffxCreateBackendDX12Desc backendDesc {};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = InOurHandle->device;

    return Evaluate(ApiDx12, &backendDesc.header, InOurHandle->device, InCmdList, InOurHandle, InParameters);
}

NVSDK_NGX_Result Nvngx_FFX::Evaluate(const Nvngx_FFX_Api& api, ffxApiHeader* backend, void* device, void* cmdList,
                                     Nvngx_FFX_Handle* InOurHandle, NVSDK_NGX_Parameter* InParameters)
{
    const auto backbuffer = api.GetResource(InParameters, "DLSSG.Backbuffer", FFX_API_RESOURCE_STATE_COMPUTE_READ);
    const auto hudless = api.GetResource(InParameters, "DLSSG.HUDLess", FFX_API_RESOURCE_STATE_COPY_DEST);
    const auto depth = api.GetResource(InParameters, "DLSSG.Depth", FFX_API_RESOURCE_STATE_COPY_DEST);
    const auto motionVectors = api.GetResource(InParameters, "DLSSG.MVecs", FFX_API_RESOURCE_STATE_COPY_DEST);
    const auto outputFfx =
        api.GetResource(InParameters, "DLSSG.OutputInterpolated", FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

    uint32_t depthInverted = 0;
    InParameters->Get("DLSSG.DepthInverted", &depthInverted);

    uint32_t hdr = 0;
    InParameters->Get("DLSSG.ColorBuffersHDR", &hdr);

    float cameraNear {};
    float cameraFar {};
    float cameraFovAngleVertical {};

    bool depthPlaneInfinite = false;

    uint32_t reset = 0;

    auto loadCameraMatrix = [&]()
    {
        return false;

        uint32_t isOrthographicProjection = 0;
        InParameters->Get("DLSSG.OrthoProjection", &isOrthographicProjection);

        if (isOrthographicProjection)
            return false;

        float (*cameraViewToClip)[4] = nullptr;
        InParameters->Get("DLSSG.CameraViewToClip", reinterpret_cast<void**>(&cameraViewToClip));

        if (!cameraViewToClip)
            return false;

        float projMatrix[4][4];
        memcpy(projMatrix, cameraViewToClip, sizeof(projMatrix));

        // BUG: Various RTX Remix-based games pass in an identity matrix which is completely useless. No
        // idea why.
        const bool isEmptyOrIdentityMatrix = [&]()
        {
            float m[4][4] = {};
            if (memcmp(projMatrix, m, sizeof(m)) == 0)
                return true;

            m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
            return memcmp(projMatrix, m, sizeof(m)) == 0;
        }();

        if (isEmptyOrIdentityMatrix)
            return false;

        // a 0 0 0
        // 0 b 0 0
        // 0 0 c e
        // 0 0 d 0
        const double b = projMatrix[1][1];
        const double c = projMatrix[2][2];
        const double d = projMatrix[3][2];
        const double e = projMatrix[2][3];

        if (e < 0.0)
        {
            cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (d / c));
            cameraFar = static_cast<float>(d / (c + 1.0));
        }
        else
        {
            cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (-d / c));
            cameraFar = static_cast<float>(-d / (c - 1.0));
        }

        if (depthInverted)
            std::swap(cameraNear, cameraFar);

        cameraFovAngleVertical = static_cast<float>(2.0 * std::atan(1.0 / b));
        return true;
    };

    if (!loadCameraMatrix())
    {
        // Some games pass in CameraFOV as degrees. Some games pass in CameraFOV as radians. Which is
        // correct? Who knows. I sure as hell don't.
        InParameters->Get("DLSSG.CameraFOV", &cameraFovAngleVertical);

        // BUG: RTX Remix-based games pass in a FOV of 0. This is a kludge.
        if (cameraFovAngleVertical == 0.0f)
            cameraFovAngleVertical = 90.0f;

        if (cameraFovAngleVertical > 10.0f)
            cameraFovAngleVertical *= std::numbers::pi_v<float> / 180.0f;

        InParameters->Get("DLSSG.CameraNear", &cameraNear);
        InParameters->Get("DLSSG.CameraFar", &cameraFar);
    }

    if (cameraNear != 0.0f && cameraFar == 0.0f)
    {
        // A CameraFar value of zero indicates an infinite far plane. Due to a bug in FSR's
        // setupDeviceDepthToViewSpaceDepthParams function, CameraFar must always be greater than
        // CameraNear when in use.
        depthPlaneInfinite = true;
        cameraFar = cameraNear + 1.0f;
    }

    const auto backbufferFormatFfx = backbuffer.description.format;
    const auto hudlessFormatFfx = hudless.description.format;
    const auto hudlessGroup = FormatPrecisionGroup(hudlessFormatFfx);
    const bool useHudless =
        hudless.resource && hudlessGroup != -1 && hudlessGroup == FormatPrecisionGroup(backbufferFormatFfx);
    const bool newHudlessFormat =
        useHudless && hudlessFormatFfx != backbufferFormatFfx && hudlessFormatFfx != InOurHandle->hudlessFormat;

    if (hudless.resource && !useHudless && hudless.description.format != InOurHandle->ignoredHudlessFormat)
    {
        LOG_WARN("Ignoring hudless of format {}, it doesn't match the backbuffer format {}", hudless.description.format,
                 backbufferFormatFfx);
        InOurHandle->ignoredHudlessFormat = hudless.description.format;
    }

    const bool formatChanged = backbufferFormatFfx != InOurHandle->backbufferFormat || newHudlessFormat;

    const bool outputMismatch = outputFfx.description.width != InOurHandle->swapchainWidth ||
                                outputFfx.description.height != InOurHandle->swapchainHeight;

    const bool recreate = State::Instance().fgChanged || outputMismatch || formatChanged;

    if (recreate)
        InOurHandle->createFailed = false;
    else if (InOurHandle->createFailed)
        return NVSDK_NGX_Result_Fail;

    if (recreate && InOurHandle->fgContext && InOurHandle->fgContext->ctx)
    {
        if (formatChanged)
            LOG_DEBUG("Backbuffer or hudless format changed, reiniting to adjust");

        if (outputMismatch)
        {
            InOurHandle->swapchainWidth = outputFfx.description.width;
            InOurHandle->swapchainHeight = outputFfx.description.height;
        }

        Util::DelayedDestroy(std::move(InOurHandle->fgContext));
    }

    State::Instance().fgChanged = false;

    if (!InOurHandle->fgContext)
        InOurHandle->fgContext = std::make_unique<ffxContext_wrap>(nullptr, api.DestroyContext);

    if (InOurHandle->fgContext->ctx == nullptr)
    {
        ffxQueryDescGetVersions versionQuery {};
        versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        versionQuery.device = device;
        uint64_t versionCount = 0;
        versionQuery.outputCount = &versionCount;
        // get number of versions for allocation
        api.Query(nullptr, &versionQuery.header);

        State::Instance().ffxFGVersionIds.resize(versionCount);
        State::Instance().ffxFGVersionNames.resize(versionCount);
        versionQuery.versionIds = State::Instance().ffxFGVersionIds.data();
        versionQuery.versionNames = State::Instance().ffxFGVersionNames.data();
        // fill version ids and names arrays.
        api.Query(nullptr, &versionQuery.header);

        ffxCreateContextDescFrameGeneration createFg {};
        createFg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;

        createFg.displaySize = { outputFfx.description.width, outputFfx.description.height };
        createFg.maxRenderSize = { outputFfx.description.width, outputFfx.description.height };

        // TODO: consider grabbing that in some other way, maybe depth and reinit if higher than what we set
        // createFg.maxRenderSize = { fgConstants.displayWidth, fgConstants.displayHeight };

        createFg.flags = 0;

        if (hdr)
            createFg.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;

        if (depthInverted)
        {
            createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
        }

        if (uint32_t mvsJittered = 0;
            InParameters->Get("DLSSG.MvecJittered", &mvsJittered) == NVSDK_NGX_Result_Success && mvsJittered)
        {
            createFg.flags |= FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
        }

        uint32_t mvsWidth = 0;
        InParameters->Get("DLSSG.MVecsSubrectWidth", &mvsWidth);

        uint32_t mvsHeight = 0;
        InParameters->Get("DLSSG.MVecsSubrectHeight", &mvsHeight);

        if (mvsWidth == InOurHandle->swapchainWidth && mvsHeight == InOurHandle->swapchainHeight)
        {
            createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
        }

        if (Config::Instance()->FGAsync.value_or_default())
            createFg.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;

        if (depthPlaneInfinite)
            createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;

        if (spdlog::default_logger()->level() == SPDLOG_LEVEL_TRACE)
            createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;

        createFg.backBufferFormat = backbufferFormatFfx;
        createFg.header.pNext = backend;

        InOurHandle->backbufferFormat = backbufferFormatFfx;

        // Keeps the previous hudless format while it still fits the backbuffer
        if (newHudlessFormat)
            InOurHandle->hudlessFormat = hudlessFormatFfx;
        else if (FormatPrecisionGroup(InOurHandle->hudlessFormat) != FormatPrecisionGroup(backbufferFormatFfx) ||
                 InOurHandle->hudlessFormat == backbufferFormatFfx)
            InOurHandle->hudlessFormat = 0;

        ffxCreateContextDescFrameGenerationHudless hudlessDesc {};

        if (InOurHandle->hudlessFormat != 0)
        {
            hudlessDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS;
            hudlessDesc.hudlessBackBufferFormat = InOurHandle->hudlessFormat;

            hudlessDesc.header.pNext = createFg.header.pNext;
            createFg.header.pNext = &hudlessDesc.header;
        }

        {
            // Currently 0 is non-ML FG and 1 is ML FG
            if (Config::Instance()->FfxFGIndex.value_or_default() < 0 ||
                Config::Instance()->FfxFGIndex.value_or_default() >= State::Instance().ffxFGVersionIds.size())
                Config::Instance()->FfxFGIndex.set_volatile_value(0);

            ffxOverrideVersion override = { 0 };
            override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
            override.versionId = State::Instance().ffxFGVersionIds[Config::Instance()->FfxFGIndex.value_or_default()];

            backend->pNext = &override.header;

            _version.parse_version(
                State::Instance().ffxFGVersionNames[Config::Instance()->FfxFGIndex.value_or_default()]);

            ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
            ScopedSkipHeapCapture skipHeapCapture {};
            ffxReturnCode_t retCode = api.CreateContext(&InOurHandle->fgContext->ctx, &createFg.header, nullptr);

            // The override is only valid during creation
            backend->pNext = nullptr;

            if (retCode != FFX_API_RETURN_OK)
            {
                LOG_ERROR("Failed to create FFX context: {:X}", retCode);

                // Only retry on change, failed create context seems to leak memory
                InOurHandle->createFailed = true;

                return NVSDK_NGX_Result_Fail;
            }
        }
    }

    InParameters->Get("DLSSG.Reset", &reset);

    InOurHandle->lastFrameId++; // TODO: somehow grab the proper one

    ffxConfigureDescFrameGeneration configureDesc {};
    configureDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    configureDesc.frameGenerationEnabled = true;
    configureDesc.allowAsyncWorkloads = Config::Instance()->FGAsync.value_or_default();
    configureDesc.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;

    if (Config::Instance()->FGDebugView.value_or_default())
        configureDesc.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW;

    configureDesc.frameID = InOurHandle->lastFrameId;

    if (InParameters->Get("DLSSG.BackbufferSubrectBaseX", &configureDesc.generationRect.left) !=
            NVSDK_NGX_Result_Success ||
        InParameters->Get("DLSSG.BackbufferSubrectBaseY", &configureDesc.generationRect.top) !=
            NVSDK_NGX_Result_Success ||
        InParameters->Get("DLSSG.BackbufferSubrectWidth", &configureDesc.generationRect.width) !=
            NVSDK_NGX_Result_Success ||
        InParameters->Get("DLSSG.BackbufferSubrectHeight", &configureDesc.generationRect.height) !=
            NVSDK_NGX_Result_Success)
    {
        // Fallback
        configureDesc.generationRect = { 0, 0, (int) InOurHandle->swapchainWidth, (int) InOurHandle->swapchainHeight };
    }

    if (useHudless)
        configureDesc.HUDLessColor = hudless;

    auto retCode = api.Configure(&InOurHandle->fgContext->ctx, &configureDesc.header);

    if (retCode != FFX_API_RETURN_OK)
    {
        LOG_ERROR("Failed to configure FFX");
        return NVSDK_NGX_Result_Fail;
    }

    ffxConfigureDescGlobalDebug1 fgLogging = {};
    fgLogging.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
    fgLogging.fpMessage = &fgLogCallback;
    fgLogging.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_VERBOSE;
    ffxReturnCode_t loggingRetCode = api.Configure(&InOurHandle->fgContext->ctx, &fgLogging.header);

    ffxDispatchDescFrameGenerationPrepareCameraInfo dfgCameraData {};
    dfgCameraData.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
    dfgCameraData.header.pNext = backend;

    InParameters->Get("DLSSG.CameraPosX", &dfgCameraData.cameraPosition[0]);
    InParameters->Get("DLSSG.CameraPosY", &dfgCameraData.cameraPosition[1]);
    InParameters->Get("DLSSG.CameraPosZ", &dfgCameraData.cameraPosition[2]);
    InParameters->Get("DLSSG.CameraUpX", &dfgCameraData.cameraUp[0]);
    InParameters->Get("DLSSG.CameraUpY", &dfgCameraData.cameraUp[1]);
    InParameters->Get("DLSSG.CameraUpZ", &dfgCameraData.cameraUp[2]);
    InParameters->Get("DLSSG.CameraRightX", &dfgCameraData.cameraRight[0]);
    InParameters->Get("DLSSG.CameraRightY", &dfgCameraData.cameraRight[1]);
    InParameters->Get("DLSSG.CameraRightZ", &dfgCameraData.cameraRight[2]);
    InParameters->Get("DLSSG.CameraFwdX", &dfgCameraData.cameraForward[0]);
    InParameters->Get("DLSSG.CameraFwdY", &dfgCameraData.cameraForward[1]);

    const bool cameraSuccess =
        InParameters->Get("DLSSG.CameraFwdZ", &dfgCameraData.cameraForward[2]) == NVSDK_NGX_Result_Success;

    ffxDispatchDescFrameGenerationPrepare dispatchPrepareDesc {};
    dispatchPrepareDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;

    if (cameraSuccess)
        dispatchPrepareDesc.header.pNext = &dfgCameraData.header;
    else
        dispatchPrepareDesc.header.pNext = backend;

    dispatchPrepareDesc.commandList = cmdList;
    dispatchPrepareDesc.frameID = InOurHandle->lastFrameId;
    dispatchPrepareDesc.flags = 0;

    // Assumes that we never get dilated depth
    InParameters->Get("DLSSG.DepthSubrectWidth", &dispatchPrepareDesc.renderSize.width);
    InParameters->Get("DLSSG.DepthSubrectHeight", &dispatchPrepareDesc.renderSize.height);

    InParameters->Get("DLSSG.JitterOffsetX", &dispatchPrepareDesc.jitterOffset.x);
    InParameters->Get("DLSSG.JitterOffsetY", &dispatchPrepareDesc.jitterOffset.y);

    InParameters->Get("DLSSG.MvecScaleX", &dispatchPrepareDesc.motionVectorScale.x);
    InParameters->Get("DLSSG.MvecScaleY", &dispatchPrepareDesc.motionVectorScale.y);

    dispatchPrepareDesc.viewSpaceToMetersFactor = 1.0f; // TODO: try query
    dispatchPrepareDesc.frameTimeDelta = 1000.0f / 60.0f;

    dispatchPrepareDesc.cameraNear = cameraNear;
    dispatchPrepareDesc.cameraFar = cameraFar;
    dispatchPrepareDesc.cameraFovAngleVertical = cameraFovAngleVertical;

    dispatchPrepareDesc.motionVectors = motionVectors;
    dispatchPrepareDesc.depth = depth;

    retCode = api.Dispatch(&InOurHandle->fgContext->ctx, &dispatchPrepareDesc.header);

    if (retCode != FFX_API_RETURN_OK)
    {
        LOG_ERROR("Failed to dispatch[1] FFX");
        return NVSDK_NGX_Result_Fail;
    }

    ffxDispatchDescFrameGeneration dispatchDesc {};
    dispatchDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    // dispatchDesc.header.pNext = &dispatchPrepareDesc.header;

    dispatchDesc.commandList = cmdList;
    dispatchDesc.frameID = InOurHandle->lastFrameId;

    dispatchDesc.backbufferTransferFunction =
        hdr ? FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ : FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;

    if (hdr && QueryHDRLuminanceRange(InParameters, InOurHandle))
    {

        dispatchDesc.minMaxLuminance[0] = InOurHandle->hdrMinLuminance;
        dispatchDesc.minMaxLuminance[1] = InOurHandle->hdrMaxLuminance;
    }
    else
    {
        dispatchDesc.minMaxLuminance[0] = 0.0001f;
        dispatchDesc.minMaxLuminance[1] = 1000.0f;
    }

    dispatchDesc.numGeneratedFrames = 1;
    dispatchDesc.outputs[0] = outputFfx;
    dispatchDesc.presentColor = backbuffer;
    dispatchDesc.reset = reset;

    dispatchDesc.generationRect = configureDesc.generationRect;

    retCode = api.Dispatch(&InOurHandle->fgContext->ctx, &dispatchDesc.header);

    if (retCode != FFX_API_RETURN_OK)
    {
        LOG_ERROR("Failed to dispatch[2] FFX");
        // return NVSDK_NGX_Result_Fail;
    }

    auto outputRealFfx = api.GetResource(InParameters, "DLSSG.OutputReal", FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

    // Copy backbuffer to output real
    if (outputRealFfx.resource)
    {
        // Only fake frames gets the debug view, copy the fake frame to output to prevent flickering
        if (configureDesc.flags & FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW)
            api.CopyTexture(cmdList, &outputRealFfx, &dispatchDesc.outputs[0]);
        else
            api.CopyTexture(cmdList, &outputRealFfx, &dispatchDesc.presentColor);

        if (retCode != FFX_API_RETURN_OK)
        {
            api.CopyTexture(cmdList, &dispatchDesc.outputs[0], &dispatchDesc.presentColor);
        }
    }

    if (retCode != FFX_API_RETURN_OK)
    {
        // LOG_ERROR("Failed to dispatch[2] FFX");
        return NVSDK_NGX_Result_Fail;
    }

    return NVSDK_NGX_Result_Success;
}

static NVSDK_NGX_Result GetCurrentSettingsCallback(Nvngx_FFX_Handle* InHandle, NVSDK_NGX_Parameter* InParameters)
{
    if (!InHandle || !InParameters)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    InParameters->Set("DLSSG.MustCallEval", 1);
    InParameters->Set("DLSSG.BurstCaptureRunning", 0);

    return NVSDK_NGX_Result_Success;
}

static NVSDK_NGX_Result EstimateVRAMCallback(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                             uint32_t, uint32_t, size_t* EstimatedSize)
{
    // Assume 300MB
    if (EstimatedSize)
        *EstimatedSize = 300 * 1024 * 1024;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_FFX::D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    if (!InParameters)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    InParameters->Set("DLSSG.GetCurrentSettingsCallback", &GetCurrentSettingsCallback);
    InParameters->Set("DLSSG.EstimateVRAMCallback", &EstimateVRAMCallback);

    // if (inited) // Query FFX
    InParameters->Set("DLSSG.MultiFrameCountMax", 1);

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_FFX::VULKAN_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                        VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                        PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                        const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                        NVSDK_NGX_Version InSDKVersion)
{
    return VULKAN_Init_Ext(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InSDKVersion,
                           InFeatureInfo);
}

NVSDK_NGX_Result Nvngx_FFX::VULKAN_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                            VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                            NVSDK_NGX_Version InSDKVersion,
                                            const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    if (!InitVk())
        return NVSDK_NGX_Result_Fail;

    vkInitDevice = InDevice;
    vkPhysicalDevice = InPD;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_FFX::VULKAN_Init_Ext2(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                             VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                             PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                             NVSDK_NGX_Version InSDKVersion,
                                             const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    return VULKAN_Init_Ext(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InSDKVersion,
                           InFeatureInfo);
}

NVSDK_NGX_Result Nvngx_FFX::VULKAN_Shutdown() { return NVSDK_NGX_Result_Success; }

NVSDK_NGX_Result Nvngx_FFX::VULKAN_Shutdown1(VkDevice InDevice) { return NVSDK_NGX_Result_Success; }

NVSDK_NGX_Result Nvngx_FFX::VULKAN_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                        const NVSDK_NGX_Parameter* InParameters, size_t* OutSizeInBytes)
{
    return D3D12_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
}

NVSDK_NGX_Result Nvngx_FFX::VULKAN_CreateFeature(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Feature InFeatureID,
                                                 NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    return VULKAN_CreateFeature1(vkInitDevice, InCmdBuffer, InFeatureID, InParameters, OutHandle);
}

NVSDK_NGX_Result Nvngx_FFX::VULKAN_CreateFeature1(VkDevice InDevice, VkCommandBuffer InCmdList,
                                                  NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                                  NVSDK_NGX_Handle** OutHandle)
{
    if (!InitVk())
        return NVSDK_NGX_Result_Fail;

    if (InDevice == VK_NULL_HANDLE || vkPhysicalDevice == VK_NULL_HANDLE)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    return CreateFeature(InFeatureID, InParameters, OutHandle, nullptr, InDevice);
}

NVSDK_NGX_Result Nvngx_FFX::VULKAN_ReleaseFeature(NVSDK_NGX_Handle* InHandle) { return D3D12_ReleaseFeature(InHandle); }

NVSDK_NGX_Result Nvngx_FFX::VULKAN_GetFeatureRequirements(const VkInstance Instance,
                                                          const VkPhysicalDevice PhysicalDevice,
                                                          const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                          NVSDK_NGX_FeatureRequirement* OutSupported)
{
    return D3D12_GetFeatureRequirements(nullptr, FeatureDiscoveryInfo, OutSupported);
}

NVSDK_NGX_Result Nvngx_FFX::VULKAN_EvaluateFeature(VkCommandBuffer InCmdList, const NVSDK_NGX_Handle* InFeatureHandle,
                                                   NVSDK_NGX_Parameter* InParameters,
                                                   PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    // This is our handle so can cast away const
    Nvngx_FFX_Handle* InOurHandle = (Nvngx_FFX_Handle*) InFeatureHandle;

    if (!InParameters || !InOurHandle)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (!InitVk())
        return NVSDK_NGX_Result_Fail;

    // The proxy swaps in the backend type of the FFX module creating the context
    ffxCreateBackendVKDesc backendDesc {};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backendDesc.vkDevice = InOurHandle->vkDevice;
    backendDesc.vkPhysicalDevice = vkPhysicalDevice;
    backendDesc.vkDeviceProcAddr = &VulkanHooks::GetDeviceProcAddr;

    // Version queries take a VkDeviceContext, which is what follows the header
    return Evaluate(ApiVk, &backendDesc.header, &backendDesc.vkDevice, InCmdList, InOurHandle, InParameters);
}

NVSDK_NGX_Result Nvngx_FFX::VULKAN_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    return D3D12_PopulateParameters_Impl(InParameters);
}
