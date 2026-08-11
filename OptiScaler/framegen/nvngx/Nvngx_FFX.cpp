#include "pch.h"
#include "Nvngx_FFX.h"

#include <NVNGX_Parameter.h>

#include "proxies/NVNGX_Proxy.h"
#include "proxies/Ntdll_Proxy.h"
#include <proxies/FfxApi_Proxy.h>
#include <numbers>

bool Nvngx_FFX::Init()
{
    LOG_FUNC();

    if (inited)
        return true;

    if (!FfxApiProxy::IsFGReady())
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

NVSDK_NGX_Result Nvngx_FFX::D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                                NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    Nvngx_FFX_Handle** OutOurHandle = (Nvngx_FFX_Handle**) OutHandle;

    if (!Init())
        return NVSDK_NGX_Result_Fail;

    if (!OutOurHandle || !InParameters)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (InFeatureID != NVSDK_NGX_Feature_FrameGeneration)
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;

    ID3D12Device* pDevice = nullptr;

    if (InCmdList->GetDevice(IID_PPV_ARGS(&pDevice)) != S_OK)
        return NVSDK_NGX_Result_FAIL_PlatformError;

    // Can't create FFX context yet, missing data
    *OutOurHandle = new Nvngx_FFX_Handle(lastIdCreated++ + NVNGX_PROVIDER_ID_OFFSET, nullptr, pDevice);

    InParameters->Get("Width", &(*OutOurHandle)->swapchainWidth);
    InParameters->Get("Height", &(*OutOurHandle)->swapchainHeight);

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_Result Nvngx_FFX::D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    Nvngx_FFX_Handle* InOurHandle = (Nvngx_FFX_Handle*) InHandle;

    if (!InOurHandle || InOurHandle->Id < NVNGX_PROVIDER_ID_OFFSET)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (inited && InOurHandle->fgContext)
    {
        auto retCode = FfxApiProxy::D3D12_DestroyContext(&InOurHandle->fgContext, nullptr);

        if (retCode == FFX_API_RETURN_OK)
            InOurHandle->fgContext = nullptr;
        else
            LOG_WARN("Could destroy FFX context");
    }

    InOurHandle->device->Release();
    delete InOurHandle;

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

static void CopyTexture(ID3D12GraphicsCommandList* CommandList, const FfxApiResource* Destination,
                        const FfxApiResource* Source)
{
    const auto cmdList12 = reinterpret_cast<ID3D12GraphicsCommandList*>(CommandList);

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

NVSDK_NGX_Result Nvngx_FFX::D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                  const NVSDK_NGX_Handle* InFeatureHandle,
                                                  NVSDK_NGX_Parameter* InParameters,
                                                  PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    // This is our handle so can cast away const
    Nvngx_FFX_Handle* InOurHandle = (Nvngx_FFX_Handle*) InFeatureHandle;

    if (!InParameters || !InOurHandle || InOurHandle->Id < NVNGX_PROVIDER_ID_OFFSET)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (!Init())
        return NVSDK_NGX_Result_Fail;

    ID3D12Resource* backbuffer = nullptr;
    ID3D12Resource* hudless = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motionVectors = nullptr;
    ID3D12Resource* output = nullptr;

    InParameters->Get("DLSSG.Backbuffer", &backbuffer);
    InParameters->Get("DLSSG.HUDLess", &hudless);
    InParameters->Get("DLSSG.Depth", &depth);
    InParameters->Get("DLSSG.MVecs", &motionVectors);
    InParameters->Get("DLSSG.OutputInterpolated", &output);

    uint32_t depthInverted = 0;
    InParameters->Get("DLSSG.DepthInverted", &depthInverted);

    uint32_t hdr = 0;
    InParameters->Get("DLSSG.ColorBuffersHDR", &hdr);

    float cameraNear {};
    float cameraFar {};
    float cameraFovAngleVertical {};

    float mvecScaleX = 1.0f;
    float mvecScaleY = 1.0f;

    float jitterOffsetX = 0.0f;
    float jitterOffsetY = 0.0f;

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

    ffxCreateBackendDX12Desc backendDesc {};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = InOurHandle->device;

    if (InOurHandle->fgContext == nullptr)
    {
        ffxCreateContextDescFrameGeneration createFg {};
        createFg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;

        createFg.displaySize = { InOurHandle->swapchainWidth, InOurHandle->swapchainHeight };
        createFg.maxRenderSize = { InOurHandle->swapchainWidth, InOurHandle->swapchainHeight };

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

        // if (fgConstants.flags & FG_Flags::Async)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;

        if (depthPlaneInfinite)
            createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;

        if (spdlog::default_logger()->level() == SPDLOG_LEVEL_TRACE)
            createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;

        createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(backbuffer->GetDesc().Format);
        createFg.header.pNext = &backendDesc.header;

        // TODO: add code for hudless with different formats

        ffxReturnCode_t retCode = FfxApiProxy::D3D12_CreateContext(&InOurHandle->fgContext, &createFg.header, nullptr);

        if (retCode != FFX_API_RETURN_OK)
        {
            LOG_ERROR("Failed to create FFX context");
            return NVSDK_NGX_Result_Fail;
        }
    }

    InParameters->Get("DLSSG.MvecScaleX", &mvecScaleX);
    InParameters->Get("DLSSG.MvecScaleY", &mvecScaleY);

    InParameters->Get("DLSSG.JitterOffsetX", &jitterOffsetX);
    InParameters->Get("DLSSG.JitterOffsetY", &jitterOffsetY);

    InParameters->Get("DLSSG.Reset", &reset);

    InOurHandle->lastFrameId++; // TODO: somehow grab the proper one

    ffxConfigureDescFrameGeneration configureDesc {};
    configureDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    configureDesc.frameGenerationEnabled = true;
    // configureDesc.allowAsyncWorkloads = true; // TODO: can crash
    configureDesc.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    configureDesc.frameID = InOurHandle->lastFrameId;
    configureDesc.generationRect = { 0, 0, (int) InOurHandle->swapchainWidth, (int) InOurHandle->swapchainHeight };

    if (hudless)
        configureDesc.HUDLessColor = ffxApiGetResourceDX12(hudless, FFX_API_RESOURCE_STATE_COPY_DEST);

    auto retCode = FfxApiProxy::D3D12_Configure(&InOurHandle->fgContext, &configureDesc.header);

    ffxConfigureDescGlobalDebug1 fgLogging = {};
    fgLogging.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
    fgLogging.fpMessage = &fgLogCallback;
    fgLogging.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_VERBOSE;
    ffxReturnCode_t loggingRetCode = FfxApiProxy::D3D12_Configure(&InOurHandle->fgContext, &fgLogging.header);

    ffxDispatchDescFrameGenerationPrepareCameraInfo dfgCameraData {};
    dfgCameraData.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
    dfgCameraData.header.pNext = &backendDesc.header;

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
        dispatchPrepareDesc.header.pNext = &backendDesc.header;

    dispatchPrepareDesc.commandList = InCmdList;
    dispatchPrepareDesc.frameID = InOurHandle->lastFrameId;
    dispatchPrepareDesc.flags = 0;

    auto depthDesc = depth->GetDesc();
    dispatchPrepareDesc.renderSize = { (uint32_t) depthDesc.Width, depthDesc.Height };
    dispatchPrepareDesc.jitterOffset = { jitterOffsetX, jitterOffsetY };
    dispatchPrepareDesc.motionVectorScale = { mvecScaleX, mvecScaleY };
    dispatchPrepareDesc.viewSpaceToMetersFactor = 1.0f; // TODO: try query
    dispatchPrepareDesc.frameTimeDelta = 1000.0f / 60.0f;

    dispatchPrepareDesc.cameraNear = cameraNear;
    dispatchPrepareDesc.cameraFar = cameraFar;
    dispatchPrepareDesc.cameraFovAngleVertical = cameraFovAngleVertical;

    dispatchPrepareDesc.motionVectors = ffxApiGetResourceDX12(motionVectors, FFX_API_RESOURCE_STATE_COPY_DEST);
    dispatchPrepareDesc.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COPY_DEST);

    retCode = FfxApiProxy::D3D12_Dispatch(&InOurHandle->fgContext, &dispatchPrepareDesc.header);

    ffxDispatchDescFrameGeneration dispatchDesc {};
    dispatchDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    // dispatchDesc.header.pNext = &dispatchPrepareDesc.header;

    dispatchDesc.commandList = InCmdList;
    dispatchDesc.frameID = InOurHandle->lastFrameId;
    dispatchDesc.generationRect = { 0, 0, (int) InOurHandle->swapchainWidth, (int) InOurHandle->swapchainHeight };

    // dispatchDesc.backbufferTransferFunction =
    //     hdr ? FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ : FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    // dispatchDesc.minMaxLuminance[0] = 0.0001f;
    // dispatchDesc.minMaxLuminance[1] = 1000.0f;

    dispatchDesc.numGeneratedFrames = 1;
    dispatchDesc.outputs[0] = ffxApiGetResourceDX12(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatchDesc.presentColor = ffxApiGetResourceDX12(backbuffer, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatchDesc.reset = reset;

    retCode = FfxApiProxy::D3D12_Dispatch(&InOurHandle->fgContext, &dispatchDesc.header);

    if (retCode != FFX_API_RETURN_OK)
    {
        LOG_ERROR("Failed to create FFX context");
        return NVSDK_NGX_Result_Fail;
    }

    ID3D12Resource* outputReal = nullptr;
    InParameters->Get("DLSSG.OutputReal", &outputReal);
    auto outputRealFfx = ffxApiGetResourceDX12(outputReal, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

    // Copy backbuffer to output real
    if (outputReal)
        CopyTexture(InCmdList, &outputRealFfx, &dispatchDesc.presentColor);

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
